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

## R-2 Transport check (FR-003, FR-004)

**Decision.** `defines_fixt_admin(const Dictionary&)`: all seven MsgTypes are in `messages()`, and
`field_valid_for("A", 1137)`. Content, not label. There is no `has_message`; `messages()` is walked at configuration time only.
The probe confirmed it splits the shapes: FIXT11 and FIX Latest pass; FIX50-family has no admin messages; FIX44 has
them but rejects 1137.

The transport dictionary from the engine is a new registry slot: today `version_registry`'s constructor drops `vt11`
silently. A FIXT11 dictionary in the engine dictionaries that fails the check does not count.

Non-validating session with no transport dictionary: admin frames use S, as today.

**One check, two callers.** `fixt_session_config_valid(cfg, registry)`, in the session library beside
`advertised_max_message_size_in_range`, holds `Session::open()`'s FIXT guards and this one.

- `Session::open()` calls it for every FIXT session, with today's guards, as today.
- `Engine::register_session` calls it for a **validating** FIXT session. Reason: on an Engine route `open()` runs later inside
  `run_connect_loop` or `run_accept_loop`, whose failure branch ends the loop with no status to anyone (read from source). Without the
  registration call, a C, Python or TOML host gets `FIXPP_ERR_OK` and a session that never comes up. Precedent: 093's `logon_timeout_ms`
  and `advertised_max_message_size_in_range` guards, each in both places. Ruling 2 says "fails at `open()`"; this adds to its wording, so it is
  on the owner's list.
- A **non-validating** FIXT session keeps its refusal at `open()` only, because refusing it at registration turns a `fixpp_session_open`
  that returns `FIXPP_ERR_OK` today into a failing call, which `[const §X.7]` defines as a breaking change, against ruling 3's "additive". A validating
  session cannot reach that clause: C has no validation setter before this feature. The loop's failure branch gains an error log line through
  `EngineConfig::logger`, so the refusal is at least visible when a logger is set. That residue gets a live L-row; the owner may choose the breaking form.

## R-3 Tables, ownership and cost (NFR-003)

Facts. `table_view` holds its stores by value, so a copy is deep. `as_table_view()` walks everything. The owned parse
route stores the address of a `shared_ptr` object that must not move or be reassigned and must outlive every view
("The pointee table is not enough: views read `&owner`"). `dictionary_driven_validator` holds a table by value and has
no other mutable state: `grep -n mutable` over `table_view.hpp`, `validator.hpp` and `dict_hooks.hpp` finds only the word in comments (read
2026-11-03, the same grep over `parser.hpp` hits, so the instrument can report). Reading one validator from several strands is therefore safe by inspection,
and TSan checks it (W-15).

The acceptor and connect loops build one `Session` per run, not per connection, and reconnect does not re-run `open()`. So the cost to bound
is sessions times dictionaries.

Measured 2026-11-03 with `research R-9`'s probe, heap through a counting `operator new`. These are debug-tree figures; a release-tree run of the same probe
gave the same bytes.

| Dictionary | One deep copy: heap, allocations | One walk, `-O0` | One walk, `-O3` |
|---|---:|---:|---:|
| FIXT11 | 18.6 KB, 498 | 0.4 ms | 0.1 ms |
| FIX44 | 0.70 MB, 19,260 | 24 ms | 3 ms |
| FIX50SP2 | 13.35 MB, 368,984 | 1.6 s | 234 ms |
| FIX Latest | 14.69 MB, 388,778 | 1.7 s | 243 ms |

One validator per slot per session would add about 28.7 MB (debug-tree figure) per T-shape session on those last three dictionaries. That is the shape the spec forbids.

**Layering, checked first.** The registry cannot own a validator. `[arch §2.3]` lets `dictionary` include `core` only, and `tools/check_layers.py` enforces it on `src/dictionary/`.
`[arch §2.4]`'s carve-out is an exact file list for the reify and field-traits bridge, and it says outright that it does not widen the whitelist. A `version_registry` member of
`wire::dictionary_driven_validator` would need `wire/validator.hpp` in `version_registry.cpp`. The CMake targets do not stop it (`fixpp_dictionary` and `fixpp_wire` link each other as a
permitted archive cycle), which is why the lint is the fact that matters. `session` may include both.

**Decision.** A session-library `fixt_route_set` (data-model E-1), owned by the Engine, holds one heap-held `const dictionary_driven_validator` per distinct `Dictionary` object: every
registered slot, the transport dictionary, and each FIXT entry's S. The table lives inside the validator, and the parse route's owner is an aliasing `shared_ptr<const table_view>` onto it, so one
dictionary has one table.

- **When.** Built once in `Engine::start()`, before any loop is spawned, only when a registered entry is FIXT. `Engine::start()` is legal once, `register_session` runs before it (`engine.hpp`), and C's
  `fixpp_engine_start` runs it while nothing polls the `io_context`. So there is no concurrent build, no lock, no atomic. M concurrent opens read, and walk nothing.
- **Cost moved.** `Engine::start()` now blocks for the walks: about 0.5 s (`-O3`) or 3.3 s (`-O0`) for FIX50SP2 and FIX Latest, on this machine, measured as above. It is a synchronous function, so nothing blocks inside a coroutine (`[const §XV.9]`). On the owner's list as a fact.
- **Direct-constructed sessions.** The `Session` constructor gains a fourth defaulted parameter beside `app_version_registry`: a non-owning route set. Engine routes pass theirs. A direct-constructed session with a registry passes nothing and builds a private set
  in `open()` from its registry and S, before the first observable mutation. It walks for itself, chiefly on test paths, since the constructor is public. Its private set omits a registry dictionary of S's `session_version`, which the slot rule never returns, so such a session walks no more than today. Today's null-registry FIXT guard still refuses a session with no registry.
- **S.** A FIXT session takes S's entry from the set, so N sessions on the same `Dictionary` object share it, and takes `inbound_tv_` as a copy of that owner, with no walk. A C-ABI session's `cfg_.dict_snapshot` keeps being adopted for `inbound_tv_`, as today; that
  snapshot is one walk and one table per C session, for its outbound commit path. It predates this feature, is unchanged, and W-15 reports it rather than asserting M on the C route. `validator_` stays null on a FIXT session.
- **Non-FIXT.** No set is built; layout and hot path of the validator do not move (E-4).
- **Build failure** is `out_of_memory` (FR-028). The set is built whole or not at all, so there is no route with a hole.

**Rejected.** The registry owning validators, as the Gate A review proposed: layering, above. The registry owning only tables, published by compare-exchange beside session-owned validators: it needs a heap cache in a `noexcept` constructor, null-cache answers, and deleted move assignment
(Gate A Codex 2, N-8), plus M × D racing walks at start (N-6), all to serve a case that a build in `Engine::start()` removes; and it holds each table twice. A `shared_ptr`-holding validator: an indirection on every FIX 4.x validation, where 098
needed an owner ruling for a 24-byte layout change. Building in the Engine constructor: a walk for engines that never use it, with no way to return an error. A warm step in `register_session`: one walk per call against entries still arriving, and no answer for direct-constructed sessions. `std::once_flag` or a mutex in `open()`: blocks a strand thread inside a coroutine. Per-session tables: the N×M above.

## R-4 FIXT framing on FIX 4.x tables (FR-007)

`as_table_view()` adds the framing surface for v50/sp1/sp2 only. A non-FIXT session must keep rejecting 1128 on a FIX
4.4 table. **Decision:** an overload, `as_table_view(fixt_framing)`, adds it for v40–v44 as well. The route set uses it. `vt11` and FIX Latest stay as `as_table_view()` builds them: measured, both declare 1128, 1129 and 1156 in their own header, and
`field_type_of_with_framing` prefers the framing type, so adding the surface could only change their answers.

**Preservation contract (Gate A Codex 6).** The overload's result MUST equal `as_table_view()`'s on every store but the two validator-private framing stores (`dictionary.cpp`'s load-bearing invariant: framing population does not touch `valid_`, `required_`, `types_`).
W-17 compares every store `table_view_test_access` reads, on every vendored file.

FIX 4.x with the surface is **not measured**: the overload does not exist yet, and a plain FIX44 table rejects 1128. W-1 and W-2's FIX44 cells are the first evidence, and they are RED-first.

## R-5 Reading 1128 before the parse (FR-001, FR-012, FR-026)

Facts. `MessageView::get` returns the first occurrence. `scan_frame_header` walks the whole frame, last-wins per member,
with its hooks chosen by the table, so a table-specific Length+Data pair can change how it splits fields: the scan cannot
be the thing that picks the table.

**Decision.** `peek_fixt_route(frame, s_hooks, s_table)`, one oracle (Gate A root cause 6):

- **Walk** with S's hooks, through `length_data_carry::read_value`, the primitive `scan_frame_header` uses. Not `dict_hooks::none()`: measured, with `none()` a 1128 inside an S header pair's Data is read as the header's 1128, and non-tag bytes in that Data
  make the frame unreadable (Gate A Codex 1); with S's hooks both read correctly.
- **MsgType** is the third field's. Anything else there is unreadable. A second 35 before the walk stops is recorded (FR-025).
- **1128** is the first one before the walk stops.
- **Stop** at the first tag for which `s_table.is_header_tag(tag) || dict::is_fixt_standard_header_tag(tag)` is false. The second is new, a public function over `kFixtFramingTable` minus its three trailer tags, because that table is private to
  `src/dictionary/` and contains 10, 89 and 93. One rule on every shape. Measured on the sampled tags: `is_header_tag` is false for 10, 89 and 93 on FIXT11, FIX44, FIX50SP2 and FIX Latest; true for 8, 9, 34, 35, 49, 91, 627 and 628 on FIXT11, FIX44 and FIX Latest; false for 1128, 1129 and 1156 on FIX44; and false for all of them on FIX50SP2. W-21 pins the standard set to `FIXT11.xml`'s `<header>`.
- **Admin** is `dict::is_fixt_admin_msgtype`, the seven-member set, made public from `reify.cpp`'s private `is_fixt_admin`. Not the session's `detail::is_admin_msgtype`, which excludes `A` on purpose.

Cost: a handful of fields per frame, FIXT sessions only.

A frame the peek cannot read routes to `session_route_`. The arm's scan then uses S's hooks and the same primitive, so it meets the same fault or garble, and the existing dispositions run unchanged (W-19).

**The occurrence rule.** Routing reads 1128 from the header only, as FIXT defines it. A frame whose first 1128 is past the header:

- validating, application route: Reject 373=14, 371=1128. Mechanism: inside the gate after the parse and before the validator, the peek reported no header 1128 and `mv.get(1128)` has a value. `mv.get` is an offset-table probe with no heap. The validator cannot produce this: its one `wire_header_out_of_order` return only tests that 35 is the first non-framing tag, and it sets no `ref_tag`; measured, the frame passes (R-9).
  Engines: C++ `Message::setString` clears `m_validStructure` for a header field after the body starts and `DataDictionary::validate` throws `TagOutOfOrder(field)` before it iterates; J `Message.parseBody` throws `TAG_SPECIFIED_OUT_OF_REQUIRED_ORDER`; Go `validateFieldContent` returns `tagSpecifiedOutOfRequiredOrder`. For an empty value past the header Go answers 373=4 first; fixpp takes C++ and J's order.
  This is also what makes validation and `dict::reify` unable to disagree on a delivered message: `reify` reads the first occurrence, and a delivered validated frame has its first occurrence in the header.
- A dictionary that declares 1128 in a message body would be false-rejected. Measured 2026-11-03: `ApplVerID` is in no `<messages>` block of FIX40–FIX50SP2 or FIXT11, walking components and groups, and its one Orchestra `fieldRef` is in component 1024, StandardHeader. W-4 recomputes this over the vendored files; a hit fails the test.
- validating, admin route: the transport dictionary judges it, as any field (measured: a TestRequest with a body 1128 passes).
- non-validating: routed by the negotiated version and delivered, as today. `reify` can read the body value on that malformed frame. Disclosed as a B-row.

**Empty `1128=`:** treated as present and unknown, never as absent. Validating: Reject 373=4, 371=1128. All three engines agree
(C++ and J `checkHasValue`, Go `TagSpecifiedWithoutAValue`). Non-validating: S. `resolve_application_version` is not called with it, because its empty-input rule
means "use the default".

**Two 1128s in the header:** the peek and `reify` both read the first; the validator passes (measured). C++ and Go would answer 373=13; the validator has no repeated-tag rule on any session, and this feature does not add one.

**MsgType repeated with a different value (FR-025).** `scan_frame_header` is last-wins on 35; the validator and `MessageView::msg_type()` are first-wins; the Active arm dispatches on `hdr.msg_type`. Measured: a frame with third field `0` and a later `35=D` passes the transport validator on T, A and L, and would reach `fromApp` under a view whose `msg_type()` is `0`, with no resolved version. A frame `D … 35=3` draws no validation at all, because the gate exempts `hdr.msg_type` 3.
The check: on a validating FIXT session, `hdr.msg_type != hdr.fault_ref_msg_type` (`FrameHeader` writes the third field's value on a fault-free frame too), ahead of the 3/5 exemption. Engines on a later 35: C++ and Go 373=14 past the header, 373=13 inside it; J 373=13 past the header and last-wins inside it. fixpp takes C++ and Go. Non-validating: unchanged, B-row. The mismatch predates 099; the routing decision is what makes it consequential.

## R-6 Header tags the session dictionary adds (FR-008)

`table_view::is_header_tag` is exact and allocation-free. **Decision:** the validator gains an optional *envelope* table (S's), passed per call on a non-virtual overload, not held, because one validator serves many sessions. On a tag failing
`valid_tags.contains` and `is_fixt_framing_tag`, the tag passes if the envelope's `is_header_tag` holds, and its enum and type are then checked against the envelope, not the validator's table. The accept path is untouched, and non-FIXT callers pass none.

A route carries the envelope when its table is not S's: every application route but S's own, and the transport route in shape A. Measured without it, on a FIX50SP2-plus-overlay S:

| Frame, overlay tag in the header | Routed table, no envelope | The merged dictionary's own answer, the comparand |
|---|---|---|
| NOS, 5020 scalar | FIX50SP2: 373=2, 371=5020 | pass |
| NOS, 5021 closed enum, bad value | FIX50SP2: 373=2, 371=5021 | 373=5, 371=5021 |
| NOS, 5022 int, `abc` | FIX50SP2: 373=2, 371=5022 | 373=5, 371=5022 |
| Logon, 5020 scalar | FIXT11: 373=2, 371=5020 | the session cannot establish without the envelope |

**Narrowing FR-008 to the header.** The table has no trailer store, and adding one to the by-value table costs layout (098
measured about +5.5 % on NewOrderSingle from a 24-byte member). A tag an overlay adds to the *trailer* is rejected on an
application message. Disclosed as an L-row. The standard trailer tags are in the framing table and are unaffected.

**A Length+Data pair an overlay adds to S's header.** On S's own route it is honoured. On a frame routed to another table it is not, and the failure is not only a Reject. Measured, an S header pair whose Data holds `SOH 1128=10 SOH 34=7`, scanned with a plain FIX50SP2 table's hooks: MsgSeqNum reads 7, not the header's 2. With non-tag bytes in the Data the scan reports a fault. The validator with the envelope would walk the Data's contents as fields. Disclosed as an L-row and on the owner's list; refusing such a config, or composing hooks, is the alternative.

## R-7 Sites (FR-005, FR-012)

Inbound sites of `session.cpp`, from the graph and a grep over `inbound_tv_`, `session_hooks(`, `validate_inbound_` and
`parse_and_dispatch_`, re-enumerated independently at Gate A:

| Group | Functions | Change |
|---|---|---|
| Frame scans | `on_inbound_not_connected_`, `on_inbound_active_`, `on_inbound_logout_sent_`, `on_inbound_logon_sent_` | select once at arm entry; scan with its hooks |
| Validate gate | the three arms calling `validate_inbound_` | takes the route; parses and validates with it |
| Logon interpret | `on_inbound_not_connected_`, `on_inbound_logon_sent_` | transport route's hooks |
| Dispatch | D-1 `on_inbound_active_` `fromAdmin`; D-2 `on_inbound_active_` `fromApp`; D-3 `on_inbound_active_sequence_reset_`; D-4 `on_inbound_active_out_of_sequence_` PossDup redelivery; D-5 `on_inbound_active_out_of_sequence_` with `validate_sequence_numbers` off; D-6 `on_inbound_active_gap_fill_`; D-7 `on_inbound_active_logout_` | `parse_and_dispatch_` takes the route, and writes the accessor byte from it |

The route is passed by const reference down the arm's call tree. D-2, D-4 and D-5 deliver to `fromApp`, so each carries its own witness cell and mutant (W-7).

Unchanged, all outbound or stored-outbound: `fire_to_admin_`, `inbound_business_reject_leaf_`, `build_send_frame_`,
`send_impl`, `store_then_emit`, `replay_outbound_range_`. So a `toApp` view on a FIXT11 session still knows no application
group, and a resend replay still scans with S's hooks. Disclosed as an L-row; outbound is not what S-026's inbound half
needs, and FR-015 pins only that 1128 survives.

Three table routes exist today (owned dispatch, borrowed validate parse, the validator's own copy). All three take the
route's table; the third is by construction, since the route's table is the validator's.

## R-8 Application frame before Logon completes

The two establishment arms validate before `interpret_logon`, with no negotiated version. Today's endings differ by
dictionary: a `FIXT11` S draws a Reject and stays in the arm; a FIX Latest S, and validation off, disconnect.
**Recommended:** on a FIXT session those arms validate admin frames only. An application frame goes straight to
`interpret_logon`'s refusal and disconnects: one condition, one ending. **On the owner's list.**

Engines, all three now traced:

| | Acceptor, before its Logon | Initiator, before the Logon reply, frame valid for the dictionary consulted | Initiator, frame also invalid |
|---|---|---|---|
| C++ | as the initiator columns: `next()` validates against its initial `ApplVerID_FIX50` default first | `verify` → `validLogonState` false → disconnect | Reject, stays |
| J | `AcceptorIoHandler.processMessage` closes the socket before `Session.next` | with 1128: `verify` → `validLogonState` false → `SessionException` → `disconnect`. **With no 1128: below** | with 1128: Reject, stays. **With no 1128: below** |
| Go | disconnects without validating | disconnects without validating | disconnects without validating |

J's arm with no 1128, read at `9ac173b2a69ef2305258330d88685eccad80512d`. `targetDefaultApplVerID` is an empty `AtomicReference` on a FIXT session until Logon, so `Session.next` passes null to `DefaultDataDictionaryProvider.getApplicationDataDictionary`.
`SimpleCache.computeIfAbsent(key)` calls `ConcurrentHashMap.get(key)` first, which throws `NullPointerException` on a null key; the loader is never reached. The NPE matches none of the validation catches. `next`'s `catch (Throwable)` rethrows it as `RuntimeError` unless `rejectMessageOnUnhandledException` is set, in which case an application frame draws a BusinessMessageReject. `SingleThreadedEventHandlingStrategy` catches it and logs. So J, by default, logs an error, sends nothing, does not disconnect and does not consume the sequence number, and the session waits out its Logon timeout. That is read from source, not run; the other event-handling strategy was not read.

So no engine gives one ending to all of it. fixpp cannot validate without a negotiated version, and the recommendation matches Go on every cell and the C++ and J disconnect on a valid frame. It departs from C++'s Reject on an invalid frame, and from J's silent wait.
**Alternative:** C++'s form, validate against S and Reject in the arm: today's T ending, which keeps one condition with two endings by shape.

**Correction at Gate A.** This section used to say no existing test sets a validating FIXT session, and the Gate A review relied on that. It is false. The recipe searched for `validate_inbound_messages = true`, and these tests assign a variable. See R-17, which lists them and what this feature does to them.

## R-9 Refusals and their endings (ruling 1, FR-009, FR-010, FR-025, FR-026)

**Derived by measurement.** Every input below went through the real `Parser` and `dictionary_driven_validator`, the real `scan_frame_header` and `resolve_application_version`, and a scratch prototype of the peek, per shape.

- Probe: `scratchpad/p099/probe3.cpp` in this session's scratch directory, never in the tree; W-4 and W-5 are its durable form. Built 2026-11-03 against `build/linux-clang-debug` at `68b9a5dd` (equal to `c0a828ef` outside this bundle): `ninja fixpp_wire fixpp_dictionary fixpp_dict_dispatch_bridge fixpp_core` reported no work, and `strings` on the binary shows `load_any_with_overlay`'s text, so 098's code is linked. Command: `clang++ -std=c++23 -stdlib=libstdc++ -g -Iinclude -Isrc probe3.cpp <those four archives, twice> libpugixml.a`, run as `probe3 dictionaries <dir of two overlay files>`. Re-run against the release tree: every cell identical.
- Engine dictionaries in each shape: FIXT11, FIX50SP2, FIX Latest; negotiated version 9. `1128=4` is therefore the resolvable value with no dictionary.
- **All of this is validator-level. No result here was driven through a `Session`.** The "Ending" column is the design's arm order applied to those answers; the arm order itself is read from source. W-4 and W-5 are the first `Session`-level evidence. FIX 4.x tables with the framing surface are unmeasured (R-4).

Application frames, validating. The middle columns are what the validator alone would answer, which is why each refusal cannot be left to it. `2/8` means 373=2, 371=8.

| Input | Resolver | Validator on S: T / A / L | Validator on the negotiated slot | Ending, all shapes |
|---|---|---|---|---|
| no 1128, valid | — | 2/8 / pass / pass | pass | delivered |
| header `1128=99`, `abc` | fails | 2/8 / 5/1128 / 5/1128 | not consulted | Reject 373=5, 371=1128 |
| header `1128=0`, `1` | fails | 2/8 / pass / pass | not consulted | Reject 373=5, 371=1128 |
| header `1128=4` | resolves, no dictionary | 2/8 / pass / pass | not consulted | Reject 373=5, 371=1128 |
| header `1128=` | not called | 2/8 / pass / pass | not consulted | Reject 373=4, 371=1128 |
| body `1128=6` | not called | 2/8 / pass / pass | pass, and `mv.get(1128)` is `6` | Reject 373=14, 371=1128 |
| body `1128=abc` | not called | 2/8 / 5/1128 / 5/1128 | 5/1128 | Reject 373=14, 371=1128: the probe runs first |
| body `1128=` | not called | 2/8 / pass / pass | pass | Reject 373=14, 371=1128 |
| after the trailer's 93 and 89, `1128=6` | not called | peek stops at 93 | `mv.get(1128)` is `6` | Reject 373=14, 371=1128 |
| third field `0`, later `35=D` | — | transport validator: pass, T / A / L | — | Reject 373=14, 371=35 |
| third field `D`, later `35=0` or `35=3` | — | negotiated slot: pass | — | Reject 373=14, 371=35 |
| third field `3`, later `35=D` | — | transport validator: pass | — | Reject 373=14, 371=35 |
| `35=0`, `35=D` adjacent | — | transport validator: pass; peek records the second 35 | — | Reject 373=13, 371=35 |
| `35=D`, `35=D` adjacent | — | negotiated slot: pass | — | delivered: no split, as today |
| header `1128=9`, then `1128=10` | first resolves | — | slot 9: pass | delivered, read as 9 |

Logon, validating, both roles. Today's gate answers first and does not disconnect, which is the defect.

| Input | Transport validator: FIXT11 / FIX Latest | Today, validating (read from source; shape A cannot establish) | Ending, all shapes |
|---|---|---|---|
| no 1137 | 1/1137 / 1/1137 | Reject, stays in the arm | Reject 373=1, 371=1137, disconnect |
| `1137=99`, `abc` | pass / 5/1137 | T acceptor: Reject, disconnect. T initiator: recorded, Active. L: Reject, stays | Reject 373=5, 371=1137, disconnect |
| `1137=4`, no dictionary | pass / pass | Acceptor: Reject, disconnect. Initiator: recorded, Active | Reject 373=5, 371=1137, disconnect |
| `1137=`, empty | pass / pass | Acceptor: Reject, disconnect (the resolver has no default, so not serviceable). Initiator: Active, nothing recorded | Reject 373=5, 371=1137, disconnect |
| `1137=9`, `10` | pass / pass | proceeds | proceeds |

Admin frames carry no refusal: the transport dictionary judges 1128 on them. Measured: Heartbeat with `1128=10` is 5/1128 on FIXT11, pass on FIX Latest; `1128=99` is 5/1128 on both; `1128=` passes on both.

**Order in an arm, validating FIXT session.** 1) Today's garble, fault and supersede checks, from the scan. 2) MsgType mismatch (FR-025). 3) In an establishment arm: a Logon's 1137 disposition (FR-010); an application frame skips the gate and goes to `interpret_logon`'s refusal. 4) The route's refusal. 5) Today's 3/5 exemption. 6) The gate: parse, past-header probe (FR-026), validator with the route's envelope.

Every Reject but the 1137 one goes through `validate_outcome::reject`, so the Active arm's `consume_rejected_seqnum_` and each arm's existing emit apply unchanged. RefMsgType is the third field's value, which differs from `hdr.msg_type` only on FR-025's frames.

**The 1137 disposition (Gate A N-1).** One helper, both roles, on a validating FIXT session: read 1137 from the frame through `detail::scan_logon_default_appl_ver_id`, factored out of `interpret_logon`'s field walk so the two reads share one occurrence rule, with the transport route's hooks. Absent: 373=1. Unparseable, empty or registry miss: 373=5. Both 371=1137, then `disconnect_unless_superseded_`. The write to `negotiated_appl_version_` stays where it is, after `interpret_logon`, so a refused Logon records nothing. Non-validating sessions keep today's order: the acceptor's check after `interpret_logon`, and FR-004a on the initiator.
Engines: C++ `next()` reads it with `FIELD_GET_REF` before `validate`, and its `FieldNotFound` catch disconnects on a Logon; J `next()` reads it before `DataDictionary.validate`. This reorders a validating session's Logon only. **On the owner's list**, with the alternative: gate first, where an absent 1137 stays in the arm until the Logon timeout, and `1137=99` disconnects on T and stays on L.

**Stale negotiated version (FR-027).** `negotiated_appl_version_` has no reset: the acceptor overwrites it per Logon, the initiator only when the reply resolves. Before this feature that value only reached `negotiated_version_profile()`, and ruling 1 keeps validation-off sessions unchanged, so it keeps today's writes. Selection reads a new `route_negotiated_`, written at the same two sites on every Logon, and `Unknown` when the initiator's reply has nothing it can resolve. A non-validating initiator on a later connection then uses S's view, where this feature would otherwise have used a table from the previous connection.

## R-10 The accessor (FR-014)

`fromApp` has no session parameter, the C callback no session either, and `negotiated_version_profile()` is strand-confined.
`fixpp_msg_version` reads tag 1137, which an application message does not carry, so it cannot be repurposed without a
break. **Decision:** one byte on `MessageView`, under the single contract in data-model E-5. C: `fixpp_msg_get_resolved_appl_ver_id`, returning the
wire value through `render_appl_ver_id`, `FIXPP_ERR_TAG_NOT_FOUND` when `Unknown`. Python follows. The
only size pin that sees `MessageView` is an equality with a typed wrapper, so it holds.

**Rejected.** A session lookup from the callback (blocks in C; no handle). Reading the table's version (with validation off
and no dictionary the table is S's, which is not the resolved version). The three-byte tag `[2c §5]` commitment 1 describes: see R-11.

## R-11 C ABI (FR-016)

Additive 1.14. `fixpp_session_config_set_default_appl_ver_id(cfg, const char* wire_value)` parses with
`resolve_application_version` and returns `FIXPP_ERR_CAPI_CONFIG_INVALID` on a bad or empty value.
`fixpp_engine_config_add_dictionary(cfg, dict)` copies the `shared_ptr`, in order. Its handle errors follow `fixpp_session_config_set_dictionary` exactly (`contracts/surface.md`). `fixpp_engine_create` copies the builder's list into `EngineConfig`, it does not move it, because a failed create leaves the builder with the caller for a retry.

**Found, added to scope:** the C API has no `validate_inbound_messages` setter, so validation is unreachable from C and
Python today. `fixpp_session_config_set_validate_inbound_messages(cfg, bool)` is added, or US3 and US1 cannot be reached there.

**What each surface reports on a refused config, traced from source.**

| Surface | The call | What the caller gets |
|---|---|---|
| C++, Engine | `Engine::register_session` | `error::invalid_session_config`; no registry entry |
| C++, direct | `Session::open()` | `error::invalid_session_config`, state still `never_opened` |
| C | `fixpp_session_open`, which calls only `register_session` | `FIXPP_ERR_THREAD_CONFIG` through `translate`, a misleading name kept because the mapping is frozen; builder not consumed. Documented on `fixpp_session_open` and on the new validation setter |
| Python | `session_open`, `Engine.open_session` | raises `fixpp.SessionError` with `.name` `FIXPP_ERR_THREAD_CONFIG` (`_map_to_class`'s 300 block) |
| TOML | `load_toml_config` succeeds: it builds no engine | the host's `Engine::register_session` returns the error |
| Table build failure | `Engine::start()`; `fixpp_engine_start`; Python `engine_start` | `error::out_of_memory`; `FIXPP_ERR_UNKNOWN` through `translate` (L-049-2's mapping); `fixpp.Unknown` |

Without the registration check, none of the C, Python or TOML rows can happen.

Registration surfaces, from 098's diff: `include/fix/c_api/{session,engine,message}.h`, `version.h` notes and MINOR,
`tests/abi/golden/fixpp_capi_symbols.txt`, `tests/capi/version_test.cpp`, `.specify/2i-capi.md`, `.specify/api-contract.md`,
`brain/components/c-api.md`, `bindings/python/fixpp.i` (GIL table; `engine_start`'s row says "returns immediately", which a FIXT engine now falsifies: it stays `hold`, since no worker is running and no callback can want the GIL, and the row says so), `bindings/python/tests/wheel/test_import_surface.py`. The
symbol map is a wildcard. SWIG strips the prefix.

**Design-document notes (Gate A root cause 3).** `[2c §5]` commitment 1 calls a three-byte `(kind, session, application)` tag "non-negotiable", and `[2i §3.10]` and `[2i §4.6]` ratify it; the shipped `fixpp_msg_version` returns BeginString and tag 1137. The divergence predates 099, which is the feature that finally carries a resolved version. Ruling 3 says additive, so the resolution is notes, not a break.

Process. `[const §XX]` governs amendments to the constitution, and §XX.1's rule is about a conflict with *that document*. No article states the three-byte contract: `grep -n fixpp_msg_version .specify/constitution.md` returned nothing on 2026-11-03. So §XX's PR, Sync Impact Report and version bump do not apply. A design-doc `Updated (<feature>)` note is the established form: 098 used it at the same `[2c §5]` for items 2 and 6. `[const §X.1]` and Appendix A require Gate A and the owner's `/plan` sign-off, both already mandatory here. Because 2c says "non-negotiable", the owner sees this one by name in the sign-off list.

The text, to apply to `.specify/` at implement (plan.md, Phase 6):

> **At `[2c §5]`, after the existing note:** Updated (099-fixt-appl-version-validation): commitment 1. The three-byte tag never shipped and is not adopted. `fixpp_msg_version` returns BeginString(8) and the raw DefaultApplVerID(1137) field (`fixpp_resolved_msg_version_t` in `include/fix/c_api/message.h`), and keeps doing so for C-ABI compatibility. A message's resolved application version is carried on `wire::MessageView` and read at the C ABI with `fixpp_msg_get_resolved_appl_ver_id` (C-ABI 1.14), through a clone as well; `FIXPP_ERR_TAG_NOT_FOUND` stands where this commitment's application byte would be `FIXPP_APPL_VER_UNKNOWN`. The kind and session bytes are not exposed. Contract: `specs/099-fixt-appl-version-validation/data-model.md` E-5.
>
> **At `[2i §3.10]`, under commitment 1:** Updated (099-fixt-appl-version-validation): as the note at `[2c §5]`. `fixpp_msg_version` does not carry the resolved per-message version; `fixpp_msg_get_resolved_appl_ver_id` does.
>
> **At `[2i §4.6]`, after the `fixpp_resolved_msg_version_t` listing:** Updated (099-fixt-appl-version-validation): this listing is not the shipped shape, which is `include/fix/c_api/message.h`'s. The resolved application version is `fixpp_msg_get_resolved_appl_ver_id`; reentrancy `requires-session-lock`, as `fixpp_msg_version`.
>
> **At `[2c §4.6]`:** Updated (099-fixt-appl-version-validation): `Dictionary::as_table_view(fixt_framing)` adds the validator-private framing stores for FIX 4.0–4.4, and equals `as_table_view()` on every other store.
>
> **At `[2c §4.9]`:** Updated (099-fixt-appl-version-validation): the registry keeps a FIXT.1.1 dictionary and answers `transport()`; `get()` is unchanged. Validators and tables shared across sessions live in the session library's route set, because `[arch §2.3]` keeps the registry from owning a validator.
>
> **At `[2b §6.5]`:** Updated (099-fixt-appl-version-validation): `dictionary_driven_validator` gains a non-virtual `validate` overload taking an envelope table and a `table()` accessor. `Validator` keeps its five pure virtuals (`[const §XIV.2]`), and the class adds no data member.

## R-12 TOML (FR-017)

`[dictionary] extra_paths = ["FIXT11.xml", "orchestra/OrchestraFIXLatest.xml"]`. Same path resolution and `dict::load_any`
trap as `path`, each failure a diagnostic at `dictionary.extra_paths`; a non-array or non-string is one too. Pushed after
`path`, so `engine.dictionaries.front()` stays the session default.

**Lead, not in scope:** `recognize_keys` walks top-level keys only, so a misspelt `[dictionary]` sub-key is silently ignored
(read from the code, not run). A misspelt `extra_paths` then fails loud at `open()` or at Logon. To file as an issue.

## R-13 Live cell (NFR-005) — needs the owner's ruling

`HP-*-init-fixt11-fix50sp2-app-bn` sends one 35=BN from fixpp. The peer sends **no** application message: the logs show only
its Logon reaching fixpp. `fixt_app_script.yaml` has one fixpp-to-peer step. Turning validation on there validates one Logon with FIXT11 and never reaches the changed path.

- **Option A, arm flip only.** fixpp test edits and the harness's `arm`. No republish indicated, unrun. Proves Logon only. It can satisfy NFR-005's words while unable to go RED on selection: a T-shape Logon passes today.
- **Option B, add a peer-to-fixpp application message.** `send_conversation_message` builds from the script's intent in
  `quickfix-cpp/counterparty`, so a script-only step looks possible; QuickFIX/J's path is unverified end to end. It changes a
  digest-bound script and may need a counterparty image republish (`REMAINING-WORK.md` assumes one). It is the only form that
  puts a real peer's frame through the new selection.

Recommendation: B, one MsgType, attempted without a republish first, and back to the owner if a republish proves necessary. `contracts/witnesses.md` W-18 states both options as runnable rows, so the ruling picks a row. Under A, S-026's row states that its live evidence is Logon-only.

## R-14 Performance gates (NFR-001, NFR-002, `[const §VIII.3]`)

`bench/session/on_inbound_frame_bench.cpp` (`InSequence`, `InSequence_Validated`) and `scan_frame_header_bench.cpp` exist but are
ungated in `bench/ci-suite.txt`. **Plan:** paired base-vs-candidate by B41's method for `on_inbound_frame` and `validator_bench` on
a non-FIX session shape (FIX.4.4), and a new FIXT case in `on_inbound_frame_bench.cpp`, reported for information, so the peek's cost is on record in the same PR.

`tests/alloc_guard/test_validate_gate_alloc_guard.cpp`, which the spec named, never constructs a `Session`. The real-Session
guard is `tests/perf/test_session_recovery_alloc_guard.cpp`, so NFR-002's witness is modelled on that file. Corrected in the spec.

## R-15 Records to re-read (FR-021)

`L-041-2`, `L-033-1`, `L-097-1`, `S-026` appear in: `.specify/architecture.md`, `.specify/constitution.md` (an amendment log, not
edited), `brain/components/dictionary.md`, `tests/dictionary/fixt_header_merge_test.cpp`, `tests/interop/happy/hp_fixt50sp2_app_message_test.cpp`,
`tests/wire/fixt_header_validate_test.cpp`, `tests/wire/required_scope_two_tier_test.cpp`, `spec/coverage-index.md`, and in the parent
`REMAINING-WORK.md`, `remaining-work/{post-v1.0-deferred,release-engineering,session-conformance}.md`. No constitution article speaks
to per-message routing, and nothing forbids it. `[const §X.6]` applies: `/clarify`, `/analyze`, Gate A and the owner's `/plan` sign-off.

## R-16 What closing S-026 honestly requires (FR-020, FR-024)

These are tasks, plan.md Phase 8. A condition unmet means the row stays open and the PR says so.

| Row | Condition | Honest under this plan? |
|---|---|---|
| S-026 → `done` | W-4 and W-5 green on {T, A, L}, since refusal correctness is part of what it promises; W-1, W-2, W-3, W-7, W-10 green; `[const §VI.6]`'s Gate A and Gate B | Yes offline. Live: only under R-13 Option B does a real peer's frame reach selection. Under Option A the row MUST say its live evidence is Logon-only |
| `coverage-index.md` §5.3.4 | Today it maps to S-026 only, so S-026 going `done` would claim it. Re-point to `—` with a disposition naming the new L-row, the form §5.3.6 uses. §5.3.2 and §5.3.5 stay on S-026; §5.3.3 stays on S-025. Also correct §4.3.7's, §5.3's and the FIXT section's "S-026 tolerate-only" and "`backlog`" wording | Yes |
| L-041-2 → closed | Its open claim is the two-dictionary resolution. W-1 and W-2 green. Its accept-only divergence stays as B-081-1, and R-6's residue gets new rows | Yes |
| L-033-1 → closed | Its stated workaround is "implement routing at the `fromApp` level". Retiring it removes that advice, which is honest only if what replaces it works: W-10's clone and reify arms green, and W-12's C arm | Yes, given E-5 |
| L-097-1 → closed | All three of its claims (TOML one dictionary; C cannot fill `EngineConfig::dictionaries`; no `default_appl_ver_id` setter) answered by FR-016 and FR-017, witnessed live by W-12, W-13, W-14 | Yes, with a new live L-row for what remains: a non-validating FIXT session with a bad config is refused only at `open()` inside the role loop, logged, not returned (R-2), unless the owner takes the breaking form |
| L-033-5 | The `Session::open()` guard comment claims to close its "no config-load failure" on an Engine, and does not on a non-validating session. Correct the comment and the row | Yes |

## R-17 Existing tests this feature changes

Recipe, 2026-11-03: the files under `tests/` matching `FIXT.1.1` intersected with those matching `validate_inbound`, then read by hand for one session that is both. It reports positives (below), so its zero on the other files means something. Re-run it at implement.

| File, cells | Session | What 099 does |
|---|---|---|
| `tests/session/test_fixt_logon_establishment.cpp`, `FixtLogonEstablishment097.W7a`…`W7c0`, `W7v1` | validating, T, registry one application version | Stays green. A real-`Session` pin of a T Logon established, `1137=10` on a registry with no FIX Latest drawing 373=5, 371=1137 and a disconnect, and an undeclared Logon tag drawing the gate's Reject, not Active. The pre-gate gives the same answers |
| same file, `W7d`, `W7d0`, `W7v2` | validating, L, registry FIX Latest | Stays green: `1137=9` answers through the fallback, whose dictionary is S's version, so S answers |
| same file, `W7e`, `W7e0`, `W7f`, `W7f0` | validating, S `fix44_dictionary_with_1137()`, registry one minimal FIX50SP2 | **Changes.** That dictionary defines Heartbeat and Logon as its only admin messages, so it fails FR-003, and with no FIXT11 registered `open()` now refuses (FR-004). They are named characterization pins of the old single-validator answer. Each needs a FIXT11 in the registry, and then, derived from the route rules (data-model E-6, E-7): **W7e0** stays established. **W7e** becomes established too, because FIXT11 accepts 1407 on a Logon (`W7a` shows it), so its purpose, a dictionary-decided Reject, is gone; drop it or restate it with a tag FIXT11 lacks. **W7f and W7f0** do not hold as written: `establish_then_feed` logs on with `1137=9` and the NOS has no 1128, so the NOS resolves to v50sp2, which the minimal FIX50SP2 answers, and it defines no NOS, so **the control W7f0 goes RED too**. Rewrite: put `1128=6` on the NOS. v44 is S's own version, so S answers with no registry slot. Then W7f0 is delivered, and W7f's 1156 passes through the framing surface (FR-007), so W7f is delivered too, where it used to be a Reject. Those literals go in the tests from this table. A run that disagrees stops for a decision; the literal is not re-recorded from the run |
| same file, `W7b`, `W7b0` | validating, T, registry FIX Latest | Stays green by the route rules. A direct-constructed session now walks Orchestra FIX Latest in `open()` where it did not, about 1.7 s at `-O0` (R-3); check the test's pump budget at implement |
| `tests/session/unparseable_frame_disposition_test.cpp`, `ProfileCell` with `Profile::fixt11` and validation on: `ProfileRoleMatrix`, `ValidatorLive`, `RowByValidation` | validating FIXT, S is the same partial test dictionary | **Changes.** `open()` refuses, as above. Fix the fixture: add a FIXT11 to the cell's registry, for the validating cells. Derived, **unverified until run**: their assertions then hold, because a Logon with `1137=6` is judged by FIXT11 and resolves to S's own version, faults are disposed of before the gate, and the missing ClOrdID is judged by S with the framing surface added, 373=1, 371=11 |
| `tests/session/engine_reset_unit_stop_test.cpp` | its FIXT case and its validating cases are separate sessions | None |

A validating FIXT session on a partial S used to work in C++, for the messages that S declares. It is now refused. That follows from FR-003's "all seven admin messages", and it goes in a B-row. SC-005 protects non-FIXT tests only, so these edits are allowed, but they are FIXT behaviour changing under existing tests and the PR says so.
