# Contract: Reify Dispatch Bridge + Handle Construction (057)

Interfaces this feature exposes/completes. All are C++ (no C-ABI). No new wire/error/dependency surface (FR-012).

## C-1 — Shipped declaring header `include/fixpp/dict/reify_dispatch_bridge.hpp`

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
- Declaring header only — **MUST NOT** `#include` any build-tree `_dispatch/`/`vXX` header (NFR-003-8).
- Both are `noexcept`; failure is via `expected_t` error, never exception.
- Return values / errors match the E-6 error table (success → live handle; unknown MsgType →
  `dict_reify_unknown_msg_type`; OOM → `dict_reify_oom`).
- The **only** definitions live in `src/dictionary/reify_dispatch_bridge.cpp` (the sole build-tree includer).

## C-2 — `owning_message_handle::from_frame` (construction seam)

```cpp
// non-user-callable (private ctor + friend, or detail:: factory — Gate-A detail)
static core::expected_t<owning_message_handle>
from_frame(resolved_message_version rmv,
           wire::MessageView<wire::access_mode::Index> const& view,
           std::pmr::memory_resource* mr) noexcept;
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

**Contract.** `view.get<35>().as_string() != Msg::msg_type_v → dict_reify_msg_type_mismatch`; else return
`owning_message_t<Msg>::from_view(view, mr)` (propagates `dict_reify_oom`). No runtime dispatch/bridge.
`dict_reify_version_mismatch` is **not** a failure mode (dropped per 003 RC#1, `reify.hpp:105`).

## C-4 — Emitter output contract (`emit_dispatch.cpp` → generated `_dispatch/*.hpp`)

- Each **known** application/FIXT arm (single- or 2-char) emits `return
  ::fixpp::dict::owning_message_handle::from_frame(<rmv>, view, mr);` — uniform, no `owning_<Msg>` reference.
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

- Shipped `reify.cpp` + `reify_dispatch_bridge.hpp` include **no** build-tree header — `check_layers.py`
  green.
- `reify_dispatch_bridge.cpp` gets a **per-file** exempt-include (its two `_dispatch/*.hpp`), NOT via the
  shared `BRIDGE_EXEMPT_INCLUDES` (which would de-guard `reify.cpp`).
- **Discriminating guard check (owed):** a test/assertion proving `check_layers.py` still **exits non-zero**
  if `reify.cpp` (not the bridge TU) includes a build-tree header — the guard must bite, not just observe.
