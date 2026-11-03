# Data model: 099

## E-1 `session::detail::fixt_route_set` (private to the session library)

`src/session/fixt_route_set.hpp`. Forward-declared in `session.hpp` and `engine.hpp`, so neither pulls `validator.hpp` into an awaitable closure (`[const §XV.9]`).

```text
class fixt_route_set                   // not copyable, not movable
  struct entry {
    shared_ptr<const wire::dictionary_driven_validator> validator;  // holds the one table by value
    shared_ptr<const dict::table_view> owner;                       // aliases validator->table(); the parse route's owner object
  };
  static expected_t<unique_ptr<const fixt_route_set>>
      build(const dict::version_registry&, span<const dict::Dictionary* const> session_dictionaries) noexcept;
  const entry* find(const dict::Dictionary*) const noexcept;        // null if absent
```

One entry per distinct `Dictionary` object: every registry slot, the transport dictionary, and each listed session dictionary, skipping null (a non-validating entry may have none, and its `open()` refuses it). The tables come from `as_table_view(fixt_framing)` (E-3), moved into the validator.

Invariants:

- **Written once, in `build`; read-only after.** No lock, no atomic. The set is handed out only after `build` returns, so readers on other strands see a fully built object.
- **Owner-object rule** (`Parser`'s owned-route constructor). Entries are heap-held and never moved or reassigned, so `&entry.owner` is stable. The set outlives every session that reads it: the Engine owns it for its lifetime, a direct-constructed session owns its private one.
- **Whole or nothing.** `build` catches `std::bad_alloc` and returns `out_of_memory`, publishing nothing.
- **The fallback.** A FIX50SP2 lookup answered by the registry's one-way fallback gets FIX Latest's dictionary pointer, and so shares that entry.

**Who builds it.** `Engine::start()`, after `validate_engine_config` and before it publishes the counter or spawns a loop, when at least one registered entry is FIXT, passing each FIXT entry's `config.dictionary`. A failure returns from `start()` with nothing spawned. A direct-constructed FIXT session builds a private one in `open()` (E-6), omitting any registry dictionary of S's `session_version`, which the slot rule never returns.

## E-1a `dict::version_registry` (public, additive)

| Addition | Meaning |
|---|---|
| `transport() const noexcept` → `std::shared_ptr<const Dictionary>` | the FIXT.1.1 dictionary among the engine dictionaries, last writer wins as for every slot; null if none |

Registering a `vt11` dictionary no longer drops it. `get()` is unchanged: a `vt11` dictionary is not an application version. No mutable state, no cache, and its move operations and `noexcept` constructors stay as they are.

## E-2 Public `dict` helpers

| Symbol | From | Meaning |
|---|---|---|
| `session_to_application(session_version)` | moved from `version_registry.cpp`'s anonymous namespace to `version_profile.hpp` | `vt11` and `Unknown` map to `Unknown` |
| `is_fixt_admin_msgtype(string_view) noexcept` | made public from `reify.cpp`'s `is_fixt_admin` | the seven-member set of the spec's Terminology; `reify.cpp` calls it |
| `is_fixt_standard_header_tag(uint16_t) noexcept` | new, `dictionary.cpp`, over `kFixtFramingTable` minus 10, 89, 93 | the peek's standard header set; `kFixtFramingTable` stays private to `src/dictionary/` |

## E-3 `Dictionary::as_table_view(fixt_framing)` (public overload)

Adds the framing surface for v40–v44. `v50`, `v50sp1` and `v50sp2` have it already. `vt11` and FIX Latest declare those tags themselves, and stay as `as_table_view()` builds them. `as_table_view()` is unchanged, so a non-FIXT session is unchanged.

**Preservation contract.** The result equals `as_table_view()`'s on every store but `fixt_framing_tags_` and `fixt_framing_types_`. Both forms bump `as_table_view_call_count`.

## E-4 `wire::dictionary_driven_validator`

Still holds its table by value, with the same constructor and `noexcept` specification. Adds no data member, so its layout does not move. `Validator`'s five pure virtuals are untouched (`[const §XIV.2]`): the two additions are non-virtual and on this `final` class only.

| Addition | Meaning |
|---|---|
| `validate(msg, scratch_mr, ref_tag_out, const table_view* envelope) const noexcept` | the virtual `validate` forwards here with a null envelope |
| `const table_view& table() const noexcept` | what the route set's aliasing owner points at |

`envelope` is read only on the failing branch of Step 1's tag check: a tag failing `valid_tags.contains` and `is_fixt_framing_tag`, with the envelope's `is_header_tag` true, passes, and **its enum and type are checked against the envelope**. The accept path is unchanged.

## E-5 The resolved application version: one contract

**Owner: the view.** `wire::MessageView::resolved_application_version()` returns a `dict::application_version`, default `Unknown`. It is one byte on `MessageView`.

**Original writer: the session only.** `parse_and_dispatch_` writes it from the frame's route (E-7), after the parse and before the callback, through `wire::detail::message_view_resolved_access`, a detail key shaped like `message_view_membership_access`. It is `Unknown` on a non-FIXT session, an admin message, an unresolved value, and any view that was not an inbound dispatch.

**Copy writers: exactly two, each from its source view, through the same key.** Both re-parse into a fresh view, so without this they would read `Unknown`:

- `fixpp_msg_clone` (`src/capi/message_write.cpp`), on the dict-backed and the dict-free branch.
- `dict::detail::owning_message_handle_from_frame` (`src/dictionary/reify.cpp`), on both branches. So `dict::reify(...)`'s handle's `view()` answers as its source did. A bridge file under `[arch §2.4]`, which already reads `message_view_membership_access`.

**Readers.**

- C: `fixpp_msg_get_resolved_appl_ver_id`, for an inbound view and a clone. `FIXPP_ERR_TAG_NOT_FOUND` when `Unknown`.
- Python: the flat SWIG function.
- `fixpp_msg_version` reads no part of this: it returns BeginString and raw tag 1137, unchanged (`Updated (099-…)` notes in research R-11).

**Against `dict::reify`'s own answer.** `owning_message_handle::version().application` is computed by `reify` from the frame's first 1128, else the profile, with the same resolver (FR-001). The two agree on every message a validating session delivers, because FR-026 keeps a delivered frame's first 1128 in its header (W-10). They can differ on a non-validating session's malformed frame with a 1128 past the header: disclosed (research R-5).

## E-6 Session state (internal)

```text
route_entry {
  shared_ptr<const table_view> const* owner;
  dictionary_driven_validator const*  validator;   // null only when not validating, non-FIXT
  table_view const*                   envelope;    // S's table when this route's table is not S's, else null
}

fixt_routing_     bool: begin_string is FIXT.1.1 (the FIXT check has guaranteed the registry and default_appl_ver_id)
route_negotiated_ application_version: selection's read of the negotiated version; written on every Logon, Unknown when an
                  initiator's reply has nothing it can resolve. negotiated_appl_version_ keeps today's writes (FR-027)
routes_           fixt_route_set const*: the Engine's, or own_routes_.get()
own_routes_       unique_ptr<const fixt_route_set>: a direct-constructed FIXT session only
app_routes_       array<route_entry, slot count>, indexed by application_version
transport_route_  route_entry
session_route_    route_entry
```

Non-FIXT: `session_route_ = {&inbound_tv_, validator_.get(), nullptr}`, and nothing else is used. FIXT: `session_route_` is S's entry from the set, `inbound_tv_` is a copy of that owner unless `cfg_.dict_snapshot` supplies it (the C-ABI path, as today), and `validator_` stays null.

The gate's guard reads `session_route_.validator`, not `validator_`. `session_test_access::has_validator` becomes `cfg_.validate_inbound_messages && session_route_.validator`, which W-18's `hello` record and the run-thread rig read.

**Written in `open()`, before the first observable mutation**, on the same side of the commit as the carry and parse blocks. Read on the session's strand with no lock. A failed `open()` leaves `state_` at `never_opened`, and a retry may reassign `own_routes_`, under the same owner-object carve-out as `inbound_tv_`: no view exists then.

**Slot rule (FR-002).** For each version `v`: `registry.get(v)` is a dictionary `d`, or `v` is S's own version, or neither. S's entry if `v` is S's own version or `d`'s `session_version` is S's. Otherwise `routes_->find(d)`, with the envelope. Otherwise a null owner, which is a miss.

**Transport route.** S's entry if `defines_fixt_admin(S)`. Otherwise `registry.transport()`'s entry, if it passes the check, with the envelope. Otherwise, reachable only when not validating, S's entry.

## E-7 Per-frame route (internal, value)

```text
inbound_route {
  route_entry entry;
  application_version resolved;     // Unknown: admin, non-FIXT, or not resolved
  route_refusal refusal;            // none | empty_value | unknown_value | no_dictionary
  bool application;                 // the third field's MsgType is not admin
  bool header_1128;                 // the peek saw 1128 before stopping
  bool second_35_in_header;         // the peek saw another 35 before stopping
}
```

**The peek's oracle** (one clause, research R-5). Walks from the frame's start with S's hooks. MsgType is the third field's. Stops at the first tag for which `inbound_tv_->is_header_tag(tag) || dict::is_fixt_standard_header_tag(tag)` is false; 10, 89 and 93 satisfy neither. Admin is `dict::is_fixt_admin_msgtype`.

Selection, computed once at arm entry:

1. Not `fixt_routing_` → `session_route_`.
2. `peek_fixt_route(frame, …)`. Unreadable → `session_route_`, and the arm's fault and garble dispositions run as today. Admin MsgType →
   `transport_route_`.
3. 1128 present and empty → refusal `empty_value`, `session_route_`.
4. 1128 present → resolve. A failure → refusal `unknown_value`, `session_route_`. A miss on the slot → refusal `no_dictionary`,
   `session_route_`, `resolved` set.
5. 1128 absent → the slot of `route_negotiated_`. `Unknown` or a miss (reachable only when not validating, or before Logon) → `session_route_`.

A refusal is acted on only by a validating session's gate. A non-validating session uses the entry it carries, which is `session_route_`.

**FIXT pre-gate, validating only, in the order of research R-9.** After the arm's garble, fault and supersede checks:

1. `hdr.msg_type != hdr.fault_ref_msg_type` → reject {13 if `second_35_in_header`, else 14; 371=35}. Ahead of the 3/5 exemption.
2. Establishment arms: MsgType `A` → the 1137 disposition (research R-9), Reject and disconnect on a failure. `application` → skip the gate; `interpret_logon` refuses and the arm disconnects.
3. `refusal` → reject {`empty_value`: 4; else 5; 371=1128}.
4. Today's 3/5 exemption, then `validate_inbound_` with the route: parse with the route's table; if `application && !header_1128 && mv.get(1128)` has a value → reject {14; 371=1128}; then validate with the route's envelope.

Rejects use the third field's value as RefMsgType.

## E-8 C ABI

`fixpp_engine_config` gains `std::vector<std::shared_ptr<const Dictionary>> dictionaries`, **copied** into `EngineConfig` in `fixpp_engine_create`, not moved: a failed create leaves the builder and its list with the caller, for a retry.

## E-9 Check helper

`fixt_session_config_valid(const SessionConfig&, const dict::version_registry*) noexcept`, in `src/session/`, with `defines_fixt_admin`: `Session::open()`'s FIXT guards plus the transport guard. Called from `open()` for every FIXT session and from `Engine::register_session` for a validating one (research R-2).

## State transitions

No new FSM states. New endings: a Logon refused on 1137 by a validating session of either role (→ Disconnected); an application frame in an establishment arm of a validating FIXT session (→ Disconnected, already validation-off's ending). A Reject on FR-025 or FR-026 changes no state, like any validation Reject.
