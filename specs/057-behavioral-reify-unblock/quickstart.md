# Quickstart: Behavioral Reify (057)

## What you get

After 057, a parsed inbound frame can be turned into a self-owning, readable handle — the reify round-trip is
live for application (single- and multi-char) and FIXT-admin messages across v42/v44/v50sp2.

```cpp
#include <fixpp/dict/reify.hpp>

// `view` is a wire::MessageView<Index> over a parsed inbound frame; `profile` the version profile.
auto r = fixpp::dict::reify(view, profile, mr);          // runtime dispatch on MsgType(35)
if (r) {
    fixpp::dict::owning_message_handle h = std::move(*r);
    h.version();                 // {application|session_admin, session, application} — FR-003
    h.msg_type();                // e.g. "D", or "AS" (multi-char) — FR-014
    auto f = h.field_value(11);  // untyped read of ClOrdID — exact wire bytes (FR-004)
    // h owns a deep copy: valid after `view`'s buffer is reused (FR-005)
}
// Compile-time-known type — no dispatch:
auto typed = fixpp::dict::reify_as<fixpp::v44::NewOrderSingle>(view, mr);   // -> owning_<NOS> or error
```

Errors (all pre-existing): unknown MsgType → `dict_reify_unknown_msg_type`; OOM → `dict_reify_oom`;
`reify_as` MsgType mismatch → `dict_reify_msg_type_mismatch`. `dict_reify_wire_body_not_ready` is no longer
returned by any path.

## Verify (local, mirrors `/speckit-verify` intent)

1. **Emitter + FORCED regen (the footgun — do NOT skip):**
   ```
   cmake --build <build> --target fixpp-codegen        # rebuild the generator
   rm -rf <build>/_codegen                              # force regen (mtime won't trigger it)
   cmake --preset linux-clang-debug                     # reconfigure → regenerates
   # ASSERT BOTH regenerated dispatch headers are live BEFORE compiling tests
   # (application + FIXT — emit_dispatch_fixt is mirrored, so its header is regenerated + compiled too):
   for h in reify_dispatch_application.hpp reify_dispatch_fixt.hpp; do
     grep -L dict_reify_wire_body_not_ready <build>/_codegen/include/fixpp/_dispatch/$h             # placeholder gone
     grep -c 'detail::owning_message_handle_from_frame' <build>/_codegen/include/fixpp/_dispatch/$h  # >=1 live factory call
   done
   ```
2. **Discriminating witness (RED→GREEN, mutation-tested):** `ctest -R reify_dispatch` — the per-field
   assertions (`field_value(11) == "ORD1"`, the `AS` multi-char body read) must pass; reverting one arm to the
   placeholder must turn them RED.
3. **Layer hygiene:** `python tools/check_layers.py` exits 0 (`reify.cpp` + the private
   `src/dictionary/reify_dispatch_bridge.hpp` include no build-tree header; the sole build-tree includer is the
   build-tree-generated `${build}/_codegen/reify_dispatch_bridge.cpp`, which is outside the scan by design — no
   exempt added, `check_layers.py` unchanged); and the discriminating check — injecting a build-tree
   `#include` into `reify.cpp` makes it exit non-zero (the guard bites, not just observes).
4. **Determinism / build-graph:** `ctest -R 'determinism|build_graph'` green (byte-identical regen; clean tree).
5. **Full gate:** ASan/UBSan/TSan presets (one at a time), coverage ≥95/85 on `src/dictionary/` +
   `include/fixpp/dict/`, clang-tidy/format/cppcheck/iwyu clean.

## Out of scope (don't be surprised)

- `owning_message_handle::as<Msg>()` is declared-only (T059) — instantiating it is ill-formed until then, it
  is not a callable nullptr-returning stub. Read via `field_value(tag)` / `view()`, or use `reify_as<Msg>()`
  for a typed owner.
- Per-message typed builders + full per-message read coverage (A/M/P/N rows) are downstream; 057 unblocks the
  dispatch mechanism only.
