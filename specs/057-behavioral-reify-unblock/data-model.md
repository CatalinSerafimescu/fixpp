# Data Model: Behavioral Reify Unblock (057)

No new persisted or wire entities. This feature wires existing types end-to-end and completes the
`owning_message_handle` storage. Entities below are the runtime types the design touches.

## E-1 — `owning_message_handle` (COMPLETED — the only real storage change)

The runtime-dispatch return of `dict::reify()`. Move-only, heap pimpl.

| Field (in `impl`) | Type | Role |
|---|---|---|
| `version` | `resolved_message_version` | resolved `{kind, session, application}` metadata (already present) |
| `bytes_` | `std::pmr::vector<std::byte>` | **NEW** — deep copy of the full validated frame span, from caller `mr` |
| `view_cache_` | `mutable std::optional<wire::MessageView<access_mode::Index>>` | **NEW** — lazily re-framed view over `bytes_` |

- **Invariants.** `bytes_` is a byte-faithful copy of `view.bytes()` at reify time; the handle outlives and is
  independent of the source parse buffer (FR-005). `view_cache_` is populated on first `view()` call via
  `Framer::feed` over `bytes_` (same pattern as `owning_<Msg>::view()`), then reused; move resets both source
  and destination caches (mirror `owning_<Msg>` move semantics).
- **Construction.** Fallible factory `from_frame(resolved_message_version, MessageView<Index> const&,
  std::pmr::memory_resource*) -> core::expected_t<owning_message_handle>`: deep-copies inside `try`, maps
  `std::bad_alloc → dict_reify_oom`. Non-user-callable (private ctor + friend to the dispatch bridge /
  `reify()`, or a `detail::` factory — Gate-A detail). No public-builder / C-ABI surface added (FR-012).
- **Accessors (unchanged signatures, `reify.hpp:84-94`).** `version()` returns the stored tuple;
  `msg_type()` = `view().get<35>()...as_string()`; `view()` returns the cached re-framed view;
  `field_value(tag)` = `view().get(tag)`. `as<Msg>()` **stays stubbed (T059, out of scope)**.
- **NOT type-erased.** Storage equals a concrete `owning_<Msg>` minus typed accessors; no `owner_base`/
  `owner_impl` (see research D-2). A future `as<Msg>()` materializes an `owning_<Msg>` from `bytes_`.

## E-2 — `resolved_message_version` (existing; consumed, not changed)

`{k: {application | session_admin}, session: <session version>, application: <application_version>}`.
Constructed in the emitter per arm:
- application arm → `{application, profile.session, application_version::<vNN>}` (`emit_dispatch.cpp:263-270`);
- FIXT-admin arm → `{session_admin, profile.session, Unknown}` (`emit_dispatch_fixt`).
Surfaced by `owning_message_handle::version()` (FR-003) and asserted by the flipped oracle tests.

## E-3 — Dispatch bridge functions (NEW, internal)

Declared in shipped `include/fixpp/dict/reify_dispatch_bridge.hpp`; defined in the generated-aware TU
`src/dictionary/reify_dispatch_bridge.cpp`.

| Function | Signature | Delegates to |
|---|---|---|
| `reify_dispatch_fixt` | `(MessageView<Index> const&, char msg_type, version_profile, mr) -> expected_t<owning_message_handle>` | inline `dispatch::dispatch_fixt` (`_dispatch/reify_dispatch_fixt.hpp`) |
| `reify_dispatch_application` | `(MessageView<Index> const&, string_view msg_type, application_version, version_profile, mr) -> expected_t<owning_message_handle>` | inline `dispatch::dispatch_application` (`_dispatch/reify_dispatch_application.hpp`) |

- The **only** TU that `#include`s the build-tree `_dispatch/*.hpp` (NFR-003-8; `check_layers.py` per-file exempt).
- `reify.cpp` calls these at `:211` (FIXT-admin) and `:236` (application), replacing the placeholder returns.

## E-4 — Two-level MsgType dispatch key (emitter-generated)

Generated inside `dispatch::dispatch_application`. Length-first:
- `len == 1` → existing `switch (msg_type[0])`.
- `len == 2` → `switch (static_cast<uint16_t>(msg_type[0]) << 8 | static_cast<uint8_t>(msg_type[1]))`, case
  labels `('A'<<8)|'S'`, etc. Safe: proven max MsgType length is 2.
- `empty()` / unmatched → `dict_reify_unknown_msg_type` (FR-009).
Arms emitted in existing bytewise-sorted order (determinism, B-003-3 / SC-005). Known multi-char arms: v44=68,
v50sp2=210 (v42=0, FIXT-admin=0). Each known arm body is uniform: `return owning_message_handle::from_frame(...)`.

## E-5 — `reify_as<Msg>` typed path (NEW inline definition)

`reify_as<Msg>(view, mr) -> expected_t<owning_message_t<Msg>>`. Header-only in `reify.hpp`; guards
`view.get<35>() != Msg::msg_type_v → dict_reify_msg_type_mismatch`; else delegates to
`owning_message_t<Msg>::from_view(view, mr)`. No bridge (compile-time-known `Msg`).

## E-6 — Test frame fixtures (NEW helper siblings)

In `tests/support/reify_test_frame.hpp` (existing `make_nos_frame()` is FIX 4.4 NOS `35=D,11=ORD1,55=AAPL`):

| Helper | Frame | Proves (SC-002) |
|---|---|---|
| `make_nos_frame()` (exists) | v44 NewOrderSingle `35=D` | application single-char, v44 |
| `make_*_frame()` v42 | a v42 application msg w/ known body field | v42 dispatch path |
| `make_*_frame()` v50sp2 | a v50sp2 application msg w/ known body field | v50sp2 dispatch path |
| `make_fixt_admin_frame()` | a FIXT-admin frame (single-char admin MsgType) | FIXT-admin path |
| `make_allocation_report_frame()` | `35=AS` AllocationReport (v44) w/ real body field | **multi-char** dispatch (discriminating field read) |

All rebuilt into a `MessageView<Index>` via the existing `frame_view_factory.hpp::make_frame_view()`.

## Error mapping (FR-009 — preserved exactly, all pre-existing `core::error` values)

| Condition | Returned error | Notes |
|---|---|---|
| Known MsgType (1- or 2-char), generated arm | *(success — live handle)* | FR-001/002/014 |
| Unknown MsgType (any length) | `dict_reify_unknown_msg_type` | default arms + `get<35>`-absent branch (D-7) |
| Unresolvable application version | `dict_unresolved_application_version` / `dict_unknown_appl_ver_id` | B-003-5 preserved |
| Allocation failure in deep-copy | `dict_reify_oom` | in `from_frame` try/catch |
| `reify_as<Msg>` MsgType ≠ `Msg::msg_type_v` | `dict_reify_msg_type_mismatch` | contract `reify.hpp:104` |
| Runtime-XML-only version | `dict_reify_unknown_msg_type` | outer-switch default; L-003-2 preserved |

`dict_reify_wire_body_not_ready`: **zero live producers after 057** (retired from all success + the
get<35>-absent paths); remains defined in the enum (enum-value removal out of scope).
