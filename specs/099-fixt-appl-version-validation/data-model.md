# Data model: 099

## E-1 `dict::version_registry` (public, additive)

| Addition | Meaning |
|---|---|
| `transport() const noexcept` → `std::shared_ptr<const Dictionary>` | the FIXT.1.1 dictionary among the engine dictionaries, last writer wins as for every slot; null if none |
| `fixt_table(application_version v) const` → `expected_t<std::shared_ptr<const table_view> const*>` | stable-address owner of `v`'s table with the FIXT framing surface; same miss error and same one-way fallback as `get(v)`; the fallback shares the FIX Latest slot's table |
| `transport_table() const` → same type | the transport dictionary's table |
| private `std::unique_ptr<table_cache>` | one `std::once_flag` + owner per slot, transport included |

Invariants: a table is built at most once per slot. An owner's address never changes for the registry's lifetime. A build that
throws leaves its flag unset. The registry stays movable and non-copyable. Registering a `vt11` dictionary no longer drops it.

## E-2 `dict::session_to_application(session_version)` (public)

Moved from `version_registry.cpp`'s anonymous namespace to `version_profile.hpp`. `vt11` and `Unknown` map to `Unknown`.

## E-3 `Dictionary::as_table_view(fixt_framing)` (public overload)

Adds the framing surface for every version but `vt11`. `as_table_view()` is unchanged, so a non-FIXT session is unchanged.

## E-4 `wire::dictionary_driven_validator`

New constructor `(std::shared_ptr<const table_view> dict, std::shared_ptr<const table_view> envelope = {})`. All reads go through
one `const table_view&`. The by-value constructor keeps its `noexcept` specification and its by-value ownership, with no envelope.
Copy and move: whatever the current uses need, stated by the implementer; a reference member must not dangle after a copy.

## E-5 `wire::MessageView`

`resolved_application_version()` → `dict::application_version`, default `Unknown`; written only by the session, through a detail
key. A clone carries it.

## E-6 Session state (internal)

```text
route_entry { shared_ptr<const table_view> const* owner; dictionary_driven_validator const* validator; }

fixt_routing_     bool: begin_string is FIXT.1.1 (open() has guaranteed the registry and default_appl_ver_id)
app_routes_       array<route_entry, kApplicationVersionSlots>, indexed by application_version
transport_route_  route_entry
session_route_    route_entry over inbound_tv_ and validator_
own_fixt_tv_      shared_ptr<const table_view>: S with framing, built only for a v40–v44 S on a FIXT session
fixt_validators_  array<unique_ptr<validator>, N+1>: only when validating
```

Written once in `open()`; read on the session's strand with no lock. Non-FIXT: only `session_route_` is used.

## E-7 Per-frame route (internal, value)

```text
inbound_route {
  route_entry entry;
  application_version resolved;     // Unknown: admin, non-FIXT, or not resolved
  route_refusal refusal;            // none | empty_value | unknown_value | no_dictionary
}
```

Selection, computed once at arm entry:

1. Not `fixt_routing_` → `session_route_`.
2. `peek_fixt_route(frame)`. Admin MsgType → `transport_route_`.
3. 1128 present and empty → refusal `empty_value`, `session_route_`.
4. 1128 present → resolve. A failure → refusal `unknown_value`, `session_route_`. A miss on the slot → refusal `no_dictionary`,
   `session_route_`, `resolved` set.
5. 1128 absent → the negotiated slot. `Unknown` or a miss (reachable only when not validating, or before Logon) → `session_route_`.

A refusal is acted on only by a validating session's gate. A non-validating session uses the entry it carries, which is `session_route_`.

## E-8 C ABI

`fixpp_engine_config_t` gains `std::vector<std::shared_ptr<const Dictionary>> dictionaries`, moved into `EngineConfig` in
`fixpp_engine_create`.

## State transitions

No new FSM states. New endings: the initiator's Logon refusal (LogonSent → Disconnected, as the acceptor's arm), and an application
frame in an establishment arm of a validating FIXT session (→ Disconnected, already validation-off's ending).
