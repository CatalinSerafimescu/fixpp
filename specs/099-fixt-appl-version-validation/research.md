# Research: 099 FIXT application version, the validation half

Read at `c0a828ef` unless a row says otherwise. QuickFIX C++ `386ce46e`, QuickFIX/J `9ac173b2`, QuickFIX/Go
`eff945dd`. Citations name symbols, not lines.

## R-1 Which dictionary answers (ruling 2, L-098-1)

**Decision.** Model QuickFIX C++ and J's split: transport dictionary for admin messages, application dictionary for
application messages, chosen per frame. The session dictionary answers its own version so an overlay survives.

- Own version is `session_to_application(S.which_session_version())`. That mapping is private to
  `src/dictionary/version_registry.cpp`, and maps `vt11` to `Unknown`; it becomes public.
- A slot `v` answers with S when `v` is S's own version, or when `registry.get(v)` returns a dictionary of S's
  `session_version` (this catches the one-way FIX50SP2 → FIX Latest fallback, whose answer would otherwise drop S's
  overlay). Otherwise it answers with the registry's dictionary, or with nothing.
- The Logon admission gates stay registry-only, as today (`Session::on_inbound_not_connected_`). A session dictionary
  that is not in the engine dictionaries answers 1128 traffic but does not make its version admissible at Logon. 098
  tells a FIXT user to put a merged dictionary in both.

**Rejected.** Replacing S with the registry's dictionary for the same version (loses the overlay). Comparing to
`cfg_.default_appl_ver_id` or the negotiated version (a FIXT11 S has no version; the negotiated version is not S's).

## R-2 Transport check (FR-003)

**Decision.** `defines_fixt_admin(const Dictionary&)`: all seven MsgTypes are in `messages()`, and
`field_valid_for("A", 1137)`. Content, not label. There is no `has_message`; `messages()` is walked at `open()` only.
The probe confirmed it splits the shapes: FIXT11 and FIX Latest pass; FIX50-family has no admin messages; FIX44 has
them but rejects 1137.

The transport dictionary from the engine is a new registry slot: today `version_registry`'s constructor drops `vt11`
silently. A FIXT11 dictionary in the engine dictionaries that fails the check does not count.

Non-validating session with no transport dictionary: admin frames use S, as today.

## R-3 Tables, ownership and cost (NFR-003)

Facts. `table_view` holds its stores by value, so a copy is deep. `as_table_view()` walks everything. The owned parse
route stores the address of a `shared_ptr` object that must not move or be reassigned and must outlive every view
("The pointee table is not enough: views read `&owner`"). `dictionary_driven_validator` holds a table by value and has
no other mutable state, so reading it from several sessions is safe by inspection.

Correction to the spec's premise: an acceptor does not build a `Session` per connection. Sessions are built lazily, one
per registered entry; the accept loop returns after the first peer, and reconnect does not re-run `open()`. The cost to
bound is sessions times dictionaries, not connections.

**Decision.** The registry owns one table per dictionary, built once, on first request, under a `std::once_flag`.

- Lazy, so a FIX.4.x engine that holds dictionaries pays nothing. Requested only from a FIXT session's `open()`.
- The flags and owners live behind a `unique_ptr` so the registry stays movable, and the `shared_ptr` objects have
  stable addresses for the engine's lifetime, which outlives every session.
- The `dictionary_driven_validator` gains a constructor taking `std::shared_ptr<const table_view>`, so a session's N
  validators share tables instead of N deep copies. The by-value constructor and its `noexcept` specification, pinned
  by `tests/wire/validator_domain_test.cpp`, are unchanged. Reads go through a `const table_view&`, one indirection,
  so `validator_bench` runs paired.

**Rejected.** Building in the Engine constructor (a walk for engines that never use it, and `version_registry`'s
constructor is `noexcept`). Per-session tables (sessions × dictionaries walks). Engine-held validators (`dict` cannot
depend on `wire`; direct-constructed test sessions have no engine).

## R-4 FIXT framing on FIX 4.x tables (FR-007)

`as_table_view()` adds the framing surface for v50/sp1/sp2 only. A non-FIXT session must keep rejecting 1128 on a FIX
4.4 table. **Decision:** an overload, `as_table_view(fixt_framing)`, adds it for every version but `vt11`. The registry
uses it. S's own table is `inbound_tv_` when it already has the surface, else a second table built once per session for
S at `open()`, for v40–v44 only. FIX Latest's own header declares 1128/1129/1156.

## R-5 Reading 1128 before the parse (FR-001, FR-012)

Facts. `MessageView::get` returns the first occurrence. `scan_frame_header` walks the whole frame, last-wins per member,
with its hooks chosen by the table, so a table-specific Length+Data pair can change how it splits fields: the scan cannot
be the thing that picks the table.

**Decision.** `peek_fixt_route(frame)`: a dictionary-free walk from the frame's start with `dict_hooks::none()` (standard
pairs only, so `SecureData` and `XmlData` are safe), reading MsgType and the first 1128, stopping at the first tag that
is neither a FIXT framing tag nor declared in the session table's header. No body field is read, so a `1128=` inside a
custom pair's data cannot be mistaken for the header field. Then the arm scans with the selected table's hooks.

Cost: a handful of fields per frame, FIXT sessions only.

**The occurrence rule (spec edge case).** Routing reads 1128 from the header only, as FIXT defines it. A frame whose first
1128 is past the header:
- validating: Reject 373=14, 371=1128 (`wire_header_out_of_order`'s reason), as QuickFIX C++, J and Go give for a header
  field out of place. This is what makes validation and `dict::reify` unable to disagree on a delivered message: `reify`
  reads the first occurrence, and a delivered validated frame has its first occurrence in the header.
- non-validating: routed by the negotiated version and delivered, as today. `reify` can read the body value on that
  malformed frame. Disclosed as a B-row.

**Empty `1128=`:** treated as present and unknown, never as absent. Validating: Reject 373=4, 371=1128, as QuickFIX's
`checkHasValue`. Non-validating: S. `resolve_application_version` is not called with it, because its empty-input rule
means "use the default".

## R-6 Header tags the session dictionary adds (FR-008)

`table_view::is_header_tag` is exact and allocation-free. **Decision:** the validator gains an optional second table, the
*envelope* table (S's). On a tag failing `valid_tags.contains` and `is_fixt_framing_tag`, the tag passes if the envelope's
`is_header_tag` holds, and its enum and type are then checked against the envelope. The accept path is untouched.
Non-FIXT validators have no envelope.

**Narrowing FR-008 to the header.** The table has no trailer store, and adding one to the by-value table costs layout (098
measured about +5.5 % on NewOrderSingle from a 24-byte member). A tag an overlay adds to the *trailer* is rejected on an
application message. Disclosed as an L-row. The standard trailer tags are in the framing table and are unaffected.

A Length+Data pair an overlay adds to S's *header* is not split by an application table. Disclosed in the same row.

## R-7 Sites (FR-005, FR-012)

Inbound sites of `session.cpp`, from the graph and a grep over `inbound_tv_`, `session_hooks(`, `validate_inbound_` and
`parse_and_dispatch_`:

| Group | Functions | Change |
|---|---|---|
| Frame scans | `on_inbound_not_connected_`, `on_inbound_active_`, `on_inbound_logout_sent_`, `on_inbound_logon_sent_` | select once at arm entry; scan with its hooks |
| Validate gate | the three arms calling `validate_inbound_` | takes the route; parses and validates with it |
| Logon interpret | `on_inbound_not_connected_`, `on_inbound_logon_sent_` | transport route's hooks |
| Dispatch | `on_inbound_active_`, `on_inbound_active_sequence_reset_`, `on_inbound_active_out_of_sequence_` (both sites), `on_inbound_active_gap_fill_`, `on_inbound_active_logout_` | `parse_and_dispatch_` takes the route |

Unchanged, all outbound or stored-outbound: `fire_to_admin_`, `inbound_business_reject_leaf_`, `build_send_frame_`,
`send_impl`, `store_then_emit`, `replay_outbound_range_`. So a `toApp` view on a FIXT11 session still knows no application
group, and a resend replay still scans with S's hooks. Disclosed as an L-row; outbound is not what S-026's inbound half
needs, and FR-015 pins only that 1128 survives.

Three table routes exist today (owned dispatch, borrowed validate parse, the validator's own copy). All three take the
route's table.

## R-8 Application frame before Logon completes

The two establishment arms validate before `interpret_logon`, with no negotiated version. Today's endings differ by
dictionary: a `FIXT11` S draws a Reject and stays in the arm; a FIX Latest S, and validation off, disconnect.
**Decision:** on a FIXT session those arms validate admin frames only. An application frame goes straight to
`interpret_logon`'s refusal and disconnects: one condition, one ending, and the ending QuickFIX reaches through `verify`.

## R-9 Refusals and their endings (ruling 1)

All go through `validate_outcome::reject` from `validate_inbound_`, so the Active arm's `consume_rejected_seqnum_` and each
arm's existing emit apply unchanged.

| Input (application frame, validating) | Ending |
|---|---|
| 1128 present, value not resolvable (`99`, `abc`, `0`, `1`) | Reject 373=5, 371=1128 |
| 1128 resolvable, slot has no dictionary | Reject 373=5, 371=1128 |
| 1128 empty | Reject 373=4, 371=1128 |
| first 1128 past the header | Reject 373=14, 371=1128 |
| no 1128 | the negotiated slot, which Logon guarantees on a validating session |

Initiator (`on_inbound_logon_sent_`), validating: the acceptor's 1137 disposition becomes one helper used by both roles.
Absent: 373=1. Unparseable or registry miss: 373=5. Both 371=1137, then `disconnect_unless_superseded_`. Non-validating
initiator: FR-004a, unchanged. The acceptor runs its check validating or not, as today.

## R-10 The accessor (FR-014)

`fromApp` has no session parameter, the C callback no session either, and `negotiated_version_profile()` is strand-confined.
`fixpp_msg_version` reads tag 1137, which an application message does not carry, so it cannot be repurposed without a
break. **Decision:** one byte on `MessageView`, `resolved_application_version()`, default `Unknown`, set by the session from
the route before the callback through the existing `detail::message_view_membership_access`-style key. `Unknown` on a
non-FIXT session, an admin message, or a value that did not resolve. C: `fixpp_msg_get_resolved_appl_ver_id`, returning the
wire value through `render_appl_ver_id`, `FIXPP_ERR_TAG_NOT_FOUND` when `Unknown`. Python follows. A clone must carry it. The
only size pin that sees `MessageView` is an equality with a typed wrapper, so it holds.

**Rejected.** A session lookup from the callback (blocks in C; no handle). Reading the table's version (with validation off
and no dictionary the table is S's, which is not the resolved version).

## R-11 C ABI (FR-016)

Additive 1.14. `fixpp_session_config_set_default_appl_ver_id(cfg, const char* wire_value)` parses with
`resolve_application_version` and returns `FIXPP_ERR_CAPI_CONFIG_INVALID` on a bad or empty value.
`fixpp_engine_config_add_dictionary(cfg, dict)` copies the `shared_ptr`, in order; the builder's list reaches `EngineConfig`
in `fixpp_engine_create`. Handle errors follow the existing setters.

**Found, added to scope:** the C API has no `validate_inbound_messages` setter, so validation is unreachable from C and
Python today. `fixpp_session_config_set_validate_inbound_messages(cfg, int)` is added, or US3 and US1 cannot be reached there.

An `invalid_session_config` from `open()` surfaces as `FIXPP_ERR_THREAD_CONFIG` (`src/capi/error.cpp`), a misleading name kept
because the mapping is frozen. Documented on the new setter.

Registration surfaces, from 098's diff: `include/fix/c_api/{session,engine,message}.h`, `version.h` notes and MINOR,
`tests/abi/golden/fixpp_capi_symbols.txt`, `tests/capi/version_test.cpp`, `.specify/2i-capi.md`, `.specify/api-contract.md`,
`brain/components/c-api.md`, `bindings/python/fixpp.i` (GIL table), `bindings/python/tests/wheel/test_import_surface.py`. The
symbol map is a wildcard. SWIG strips the prefix.

## R-12 TOML (FR-017)

`[dictionary] extra_paths = ["FIXT11.xml", "orchestra/OrchestraFIXLatest.xml"]`. Same path resolution and `dict::load_any`
trap as `path`, each failure a diagnostic at `dictionary.extra_paths`; a non-array or non-string is one too. Pushed after
`path`, so `engine.dictionaries.front()` stays the session default.

**Lead, not in scope:** `recognize_keys` walks top-level keys only, so a misspelt `[dictionary]` sub-key is silently ignored
(read from the code, not run). A misspelt `extra_paths` then fails loud at `open()` or at Logon. To file as an issue.

## R-13 Live cell (NFR-005) — needs the owner's ruling

`HP-*-init-fixt11-fix50sp2-app-bn` sends one 35=BN from fixpp. The peer sends **no** application message: the logs show only
its Logon reaching fixpp. Turning validation on there validates one Logon with FIXT11 and never reaches the changed path.

- **Option A, arm flip only.** fixpp test edits and the harness's `arm`. No republish indicated, unrun. Proves Logon only.
- **Option B, add a peer-to-fixpp application message.** `send_conversation_message` builds from the script's intent in
  `quickfix-cpp/counterparty`, so a script-only step looks possible; QuickFIX/J's path is unverified end to end. It changes a
  digest-bound script and may need a counterparty image republish (`REMAINING-WORK.md` assumes one). It is the only form that
  puts a real peer's frame through the new selection.

Recommendation: B, one MsgType, attempted without a republish first, and back to the owner if a republish proves necessary.

## R-14 Performance gates (NFR-001, NFR-002)

`bench/session/on_inbound_frame_bench.cpp` (`InSequence`, `InSequence_Validated`) and `scan_frame_header_bench.cpp` exist but are
ungated in `bench/ci-suite.txt`. **Plan:** paired base-vs-candidate by B41's method for `on_inbound_frame` and `validator_bench` on
a non-FIX session shape (FIX.4.4), a new FIXT case reported for information.

`tests/alloc_guard/test_validate_gate_alloc_guard.cpp`, which the spec named, never constructs a `Session`. The real-Session
guard is `tests/perf/test_session_recovery_alloc_guard.cpp`, so NFR-002's witness is modelled on that file. Corrected in the spec.

## R-15 Records to re-read (FR-021)

`L-041-2`, `L-033-1`, `L-097-1`, `S-026` appear in: `.specify/architecture.md`, `.specify/constitution.md` (an amendment log, not
edited), `brain/components/dictionary.md`, `tests/dictionary/fixt_header_merge_test.cpp`, `tests/interop/happy/hp_fixt50sp2_app_message_test.cpp`,
`tests/wire/fixt_header_validate_test.cpp`, `tests/wire/required_scope_two_tier_test.cpp`, `spec/coverage-index.md`, and in the parent
`REMAINING-WORK.md`, `remaining-work/{post-v1.0-deferred,release-engineering,session-conformance}.md`. No constitution article speaks
to per-message routing, and nothing forbids it. `[const §X.6]` applies: `/clarify`, `/analyze`, Gate A and the owner's `/plan` sign-off.
