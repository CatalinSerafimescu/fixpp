# Contract: Reify Dispatch Bridge + Handle Construction (057)

Interfaces this feature exposes/completes. All are C++ (no C-ABI). No new wire/error/dependency surface (FR-012).

## C-1 — Private declaring header `src/dictionary/reify_dispatch_bridge.hpp` (NOT shipped/public)

```cpp
namespace fixpp::dict {
// Delegates (in the generated-aware TU only) to dispatch::dispatch_fixt.
[[nodiscard]] core::expected_t<owning_message_handle>
reify_dispatch_fixt(wire::MessageView<wire::access_mode::Index> const& view,
                    char msg_type, version_profile profile,
                    std::pmr::memory_resource* mr) noexcept;

// Delegates (in the generated-aware TU only) to dispatch::dispatch_application.
[[nodiscard]] core::expected_t<owning_message_handle>
reify_dispatch_application(wire::MessageView<wire::access_mode::Index> const& view,
                           std::string_view msg_type, application_version appver,
                           version_profile profile,
                           std::pmr::memory_resource* mr) noexcept;
}
```

**Contract.**
- Private same-module declaring header. It lives under `src/dictionary/`, so `check_layers.py` **DOES scan
  it** (module=`dictionary`, `ALLOWED={core}`), and the scan checks **direct** `#include` lines only (it does
  not follow transitive includes). It therefore **MUST** `#include` ONLY `<fixpp/dict/reify.hpp>` (a dict→dict
  self-include — allowed) plus std headers (`<string_view>`, `<memory_resource>`). Every type its two
  declarations name (`owning_message_handle`, `wire::MessageView<Index>`, `version_profile`,
  `application_version`, `core::expected_t`) arrives **transitively through `reify.hpp`** — invisible to the
  direct-only scanner. It **MUST NOT** directly `#include` any build-tree `_dispatch/`/`vXX` header (NFR-003-8)
  **or any `<fixpp/wire/...>` header** — either direct include is a `dictionary → wire` violation →
  `check_layers` RED, breaking the SC-005 "check_layers unchanged / green / no exempt" claim. It is NOT a
  shipped `include/` header (no public C++ surface widening — FR-012).
- Both are `noexcept`; failure is via `expected_t` error, never exception.
- Return values / errors match the E-6 error table (success → live handle; unknown MsgType →
  `dict_reify_unknown_msg_type`; OOM → `dict_reify_oom`).
- The **only** definitions live in the **build-tree-generated** TU
  `${build}/_codegen/reify_dispatch_bridge.cpp` (`configure_file`d from
  `cmake/templates/reify_dispatch_bridge.cpp.in`) — the sole build-tree includer, outside the `check_layers.py`
  scan by design.

## C-2 — `detail::owning_message_handle_from_frame` (construction seam)

```cpp
// A single hand-written free function in `fixpp::dict::detail` — NOT a member of the public
// owning_message_handle type (no public C++ construction surface — FR-012). owning_message_handle
// `friend`s this ONE stable name to reach its private ctor/storage (standard passkey/attorney pattern).
// Declared in reify.hpp under `namespace fixpp::dict::detail`; DEFINED out-of-line in reify.cpp /
// fixpp_dictionary (the handle is a heap pimpl — data-model E-1 — so the factory cannot be inline in the
// header). Called by the generated dispatch functions in the build-tree bridge TU. (A `detail::`-only
// passkey tag threaded through a private ctor is an equally valid alternative and needs no friend; either
// resolves it — this bundle picks the free function + single friend.) Friending the many emitter-controlled
// generated dispatch-function names is what D-2 rejected as fragile — this single fixed name is NOT that.
namespace fixpp::dict::detail {
[[nodiscard]] core::expected_t<owning_message_handle>
owning_message_handle_from_frame(resolved_message_version rmv,
                                 wire::MessageView<wire::access_mode::Index> const& view,
                                 std::pmr::memory_resource* mr) noexcept;
}
```

**Contract.** Deep-copies `view.bytes()` into `mr`; `std::bad_alloc → dict_reify_oom`. Resulting handle is
independent of the source buffer (FR-005). Sets `version = rmv`; leaves `view_cache_` empty (lazily built).
Public accessor signatures (`version/msg_type/view/field_value/as<Msg>`) are **unchanged** (`reify.hpp:84-94`);
only storage + construction are added. `as<Msg>()` remains stubbed (T059).

## C-3 — `reify_as<Msg>` (typed entry, inline)

```cpp
template <class Msg>
[[nodiscard]] core::expected_t<owning_message_t<Msg>>
reify_as(wire::MessageView<wire::access_mode::Index> const& view,
         std::pmr::memory_resource* mr) noexcept;
```

**Contract.** `get<35>()` returns `expected_t<field_view>`, so unwrap it: if it is an **error / absent tag
35**, return `dict_reify_msg_type_mismatch` (an absent MsgType cannot match `Msg`, mirroring `reify.cpp`'s
`!mt_fv` guard); if present and `mt.value().as_string() != Msg::msg_type_v`, return
`dict_reify_msg_type_mismatch`; else return `owning_message_t<Msg>::from_view(view, mr)` (propagates
`dict_reify_oom`). No runtime dispatch/bridge. `dict_reify_version_mismatch` is **not** a failure mode (dropped
per 003 RC#1, `reify.hpp:105`).

## C-4 — Emitter output contract (`emit_dispatch.cpp` → generated `_dispatch/*.hpp`)

- Each **known** application/FIXT arm (single- or 2-char) emits `return
  ::fixpp::dict::detail::owning_message_handle_from_frame(<rmv>, view, mr);` — uniform, no `owning_<Msg>`
  reference.
- `empty()` MsgType and unmatched arms emit `return ::std::unexpected{...dict_reify_unknown_msg_type};`.
- Two-level dispatch: `len==1` char switch; `len==2` packed-`uint16` switch; arms in bytewise-sorted order.
- Output **byte-deterministic** run-to-run (B-003-3); `determinism_test` + build-graph-clean gates must pass.
- **Zero** occurrences of `dict_reify_wire_body_not_ready` in the regenerated `_dispatch/*.hpp` (D-7 assert).

## C-5 — `dict::reify()` delegation (shipped `reify.cpp`)

At the two placeholder sites: FIXT-admin (`:211`) → `return reify_dispatch_fixt(view, mt_sv.front(),
profile, mr);`; application (`:236`) → `return reify_dispatch_application(view, mt_sv, *app_ver, profile,
mr);`. The `get<35>`-absent branch (`:181-192`) returns `dict_reify_unknown_msg_type` (was
`dict_reify_wire_body_not_ready`). Stale frozen-stub comments corrected. No build-tree include added.

## Layer-hygiene contract (NFR-003-8 / arch §2.4 v0.3)

- Shipped `reify.cpp` + the private `src/dictionary/reify_dispatch_bridge.hpp` include **no** build-tree header
  — `check_layers.py` green. NFR-003-8 is satisfied **literally** (no shipped `src/**` build-tree includer),
  WITHOUT extending the arch §2.4 carve-out and WITHOUT an amendment.
- The build-tree-generated `${build}/_codegen/reify_dispatch_bridge.cpp` is the sole build-tree includer; being
  outside `src/**` it is **not scanned** by `check_layers.py`, so **no exempt is added** (neither the shared
  `BRIDGE_EXEMPT_INCLUDES` nor a per-file exempt) — `check_layers.py` is unchanged.
- **Discriminating guard check (owed — survives the pivot):** a test/assertion proving `check_layers.py` still
  **exits non-zero** for BOTH `reify.cpp` AND the new `src/dictionary/reify_dispatch_bridge.hpp` if either
  gains a direct build-tree (`_dispatch/`/`vXX`) or `<fixpp/wire/...>` include — no exempt does not mean no
  guard; it must bite, not just observe. Precedent: `reify.cpp` IS a `BRIDGE_SOURCE_FILE` and legitimately
  includes the wire stub directly today; the private header is NOT a `BRIDGE_SOURCE_FILE` and must not include
  wire at all.
