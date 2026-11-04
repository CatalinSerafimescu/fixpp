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
- `Engine::register_session` calls it when `cfg.validate_inbound_messages && cfg.begin_string == "FIXT.1.1"`. The predicate is not `cfg.is_fixt()`, which is false when `default_appl_ver_id` is absent, so it would miss the malformed config the check exists for; `open()`'s own guard tests `begin_string` for the same reason. Reason for the call: on an Engine route `open()` runs later inside
  `run_connect_loop` or `run_accept_loop`, whose failure branch ends the loop with no status to anyone (read from source). Without the
  registration call, a C, Python or TOML host gets `FIXPP_ERR_OK` and a session that never comes up. Precedent: 093's `logon_timeout_ms`
  and `advertised_max_message_size_in_range` guards, each in both places. Ruling 2 says "fails at `open()`"; the owner ruled this addition (item 4).
- A **non-validating** FIXT session keeps its refusal at `open()` only, because refusing it at registration turns a `fixpp_session_open`
  that returns `FIXPP_ERR_OK` today into a failing call, which `[const §X.7]` defines as a breaking change, against ruling 3's "additive". A validating
  session cannot reach that clause: C has no validation setter before this feature. The loop's failure branch gains an error log line through
  `SessionConfig::logger_override` ahead of `EngineConfig::logger`, so the refusal is at least visible when a logger is set. That residue gets a live L-row (owner item 5).

## R-3 Tables, ownership and cost (NFR-003)

Facts. `table_view` holds its stores by value, so a copy is deep. `as_table_view()` walks everything. The owned parse
route stores the address of a `shared_ptr` object that must not move or be reassigned and must outlive every view
("The pointee table is not enough: views read `&owner`"). `dictionary_driven_validator` holds a table by value and has
no other mutable state: `grep -n mutable` over `table_view.hpp`, `validator.hpp` and `dict_hooks.hpp` finds only the word in comments (read
2026-11-03, the same grep over `parser.hpp` hits, so the instrument can report). Those hits, and `offset_table.hpp`'s group and nested caches, are per view, not per table, so sharing a table shares none of them. A grep for `static` and `thread_local` at line start over `validator.hpp`, `validator.cpp`, `table_view.hpp` and `dict_hooks.hpp` finds only `constexpr` constants (read 2026-11-03; the same grep over `view.hpp` hits). Reading one validator from several strands is therefore safe by inspection,
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

**Decision.** A session-library `fixt_route_set` (data-model E-1), held by the Engine and its sessions, holds one heap-held `const dictionary_driven_validator` per distinct route table (R-20): every
registered slot, the transport dictionary, and each FIXT entry's S. The table lives inside the validator, and the parse route's owner is an aliasing `shared_ptr<const table_view>` onto it, so one
dictionary has one table.

- **When.** Built once in `Engine::start()`, before any loop is spawned, only when a registered entry is FIXT. `Engine::start()` is legal once successfully, `register_session` runs before it (`engine.hpp`), and C's
  `fixpp_engine_start` runs it while nothing polls the `io_context`. So there is no concurrent build, no lock, no atomic. M concurrent opens read, and walk nothing.
- **Cost moved.** `Engine::start()` now blocks for the walks: about 0.5 s (`-O3`) or 3.3 s (`-O0`) for FIX50SP2 and FIX Latest, on this machine, measured as above. The call stays legal from a handler (`contracts/surface.md`); the comments in `engine.hpp`, `src/session/engine.cpp` and `src/capi/engine.cpp` that say it does not block are corrected in the route-set phase, and W-23 greps for them, because a C++ host that calls `start()` from a handler on `exec`, for instance a second engine on one `io_context`, stalls every strand on that thread for the walks. C's `fixpp_engine_start` runs it with no worker polling this engine, so only another engine's threads can notice. Stated to the owner as a fact.
- **Ownership.** One `std::shared_ptr<const fixt_route_set> routes_` on `Session`, a null one on a non-FIXT session. The Engine holds the same pointer, set through `session_engine_access::adopt_routes` right after `make_shared<Session>` and before `open()`. There is no public signature for it: `Session`'s constructor is unchanged. The set dies when the last of the Engine and its sessions lets go, so no declaration order on the Engine matters, and nothing in `Session` dereferences the set from a destructor (`route_entry` holds raw pointers and has no destructor).
- **Direct-constructed sessions.** A session with no adopted set builds a private one in `open()` from its registry and S, before the first observable mutation, and walks for itself, chiefly on test paths. It holds every registry slot, including one the slot rule never returns: T with registry `{FIX50SP2}` walks twice where it walked once, and R-17's `W7b` walks FIX Latest. `W7d` already walks FIX Latest inside `open()` under the same 200 ms `run_sync` window, and passes (run here at `-O0`, 2026-11-03): `run_for` cannot pre-empt a handler, and the walk happens inside one. Read from source; not run under a sanitizer here. Today's null-registry FIXT guard still refuses a session with no registry. NFR-003's "inside a coroutine" is about an Engine, and the rejection of `std::once_flag` below is of a lock that makes one strand wait on another's walk; this walk stays on its own session's thread, as `open()`'s walk of S does today.
- **Cost to the witnesses.** Measured 2026-11-03 through `h.cpp` (R-18) at `c0a828ef`, `-O0`: T's 240 cells take 1.47 s, about 6 ms a cell with the dictionary loads included, and L's take 384 s, 1.6 s a cell, the walk of S. W-4, W-5, W-6 and W-9 walk six logs of 240 lines, and W-25 and the other rows add about 700 sessions, so a private build per cell, about 3.3 s, is about 2 h per run of `session_pure_tests`, more under a sanitizer, and again per mutant row. `FixtSetup`'s `run_sync` is a second cost: its `run_for` window takes 200 ms a call (`W7a` takes 202 ms). tasks.md T007 is the answer.
- **S.** A FIXT session takes S's entry from the set, so N sessions on the same `Dictionary` object share it, and takes `inbound_tv_` as a copy of that owner, with no walk. A C-ABI session's `cfg_.dict_snapshot` keeps being adopted for `inbound_tv_`, as today; that
  snapshot is one walk and one table per C session, for its outbound commit path. It predates this feature, is unchanged, and W-15 reports it rather than asserting M on the C route. `validator_` stays null on a FIXT session.
- **Non-FIXT.** No set is built; layout and hot path of the validator do not move (E-4).
- **Build failure** is `out_of_memory` (FR-028). The set is built whole or not at all, so there is no route with a hole.

**Rejected.** The registry owning validators, as the Gate A review proposed: layering, above. The registry owning only tables, published by compare-exchange beside session-owned validators: it needs a heap cache in a `noexcept` constructor, null-cache answers, and deleted move assignment,
plus M × D racing walks at start, all to serve a case that a build in `Engine::start()` removes; and it holds each table twice. A `shared_ptr`-holding validator: an indirection on every FIX 4.x validation, where 098
needed an owner ruling for a 24-byte layout change. Building in the Engine constructor: a walk for engines that never use it, with no way to return an error. A warm step in `register_session`: one walk per call against entries still arriving, and no answer for direct-constructed sessions. `std::once_flag` or a mutex in `open()`: blocks a strand thread inside a coroutine. Per-session tables: the N×M above.

## R-4 FIXT framing on FIX 4.x tables (FR-007)

`as_table_view()` adds the framing surface for v50/sp1/sp2 only. A non-FIXT session must keep rejecting 1128 on a FIX
4.4 table. **Decision:** an overload, `as_table_view(fixt_framing)`, adds it for v40–v44 as well. The route set uses it. `vt11` and FIX Latest stay as `as_table_view()` builds them: measured, both declare 1128, 1129 and 1156 in their own header, and
so they need none. Measured by M-48 at the commit of `099 P1: dictionary, validator, FIXT11.xml`: on FIX Latest the surface changes `is_fixt_framing_tag` and no type answer, because for every tag of `kFixtFramingTable` that file's own type and the framing type collapse to one `field_type`.

**Preservation contract.** The overload's result MUST equal `as_table_view()`'s on every store but the two validator-private framing stores (`dictionary.cpp`'s load-bearing invariant: framing population does not touch `valid_`, `required_`, `types_`) and NoHops's group metadata, below.
W-17 compares every store `table_view_test_access` reads, on every vendored file.

**NoHops's structure.** The framing surface carries tags, and Step 3 asks the routed table for structure. Measured 2026-11-03, real `Parser` and validator (`v.cpp`, the loop 2 round 1 review), the frames `627=2|628=X` and `627=1|630=5`:

| Table | Count high | No delimiter | Valid `627=1\|628=X` |
|---|---|---|---|
| FIXT11, FIX44, FIX Latest | 373=1, 371=628 | 373=1, 371=628 | pass |
| FIX50SP2 | pass | pass | pass |
| FIX42, plain | 373=2, 371=627 | 373=2, 371=627 | 373=2, 371=627. With the probe's FR-007 surface on real tables, not through a `Session` (`gate_a_l2r2/nh.log`, below), it passes all three as FIX50SP2 does. FIX 4.0 draws 373=2, 371=60 on each of those frames, the tag 60 the probe's NewOrderSingle carries, so its NoHops answer is not shown. FIX 4.1: derived, not run |

Through a real `Session` (`s dictionaries nohops`, the same review): T Heartbeat and L NOS `R 1/628` stays c; T Logon `R 1/628` stays k; validation off, delivered. So with the surface alone, on T a NOS with `627=2|628=X` is delivered on the FIX50SP2 route and draws 1/628 on the FIX44 and FIX Latest routes, and L, which answers 1/628 today, passes it once FIX50SP2 is registered. All three engines give one ending per route, because the transport dictionary owns the header: C++ `DataDictionary::iterate` skips `checkGroupCount` for a header field; J's `DataDictionary.validate` iterates the header with the session dictionary, and Go's `validateWalk` takes a header tag's definition from the transport dictionary and `validateVisitGroupField` checks the count, each answering 373=16 (read, not run).

**Decision:** the overload registers NoHops's metadata where the table's own header does not declare it (data-model E-3), which answers from the existing bare fallback with no validator branch. It keeps today's answer on T and L, so no owner item. With validation off the view gains `group(627)` on shape A, which the clarification's "the view follows the route" already implies (B-row, W-6's `off-hops`). Measured on real tables with E-3's calls applied through `table_view_test_access` (the parent's `research/probes/099_fixt_session_today/gate_a_l2r2/nh.log`): on FIX50SP2, FIX 5.0 and, through the probe's own FR-007 surface, FIX42, both bad frames draw `1/628` and the valid one passes, as the FIX44 and FIX Latest comparands do, and the view, `fixpp_msg_get_group`, a clone and a `reify` handle read one answer, with the metadata off and with it on. Through a `Session` it is not run; W-3 (h) runs first. 373=1 is the validator's existing mapping, and B85 owns that wording. **Rejected:** an envelope for Step 3, which does not reach shape A, whose S declares no header.

FIX 4.x with the surface is measured only on `nh.log`'s frames, which carry no 1128, on real tables through the probe's surface, because the overload does not exist yet, and not through a `Session`: FIX42's validator and readers, FIX 4.0's readers, and FIX44 as a comparand. **Not measured:** FIX 4.1 and 4.3, and 1128 on any FIX 4.x route, where a plain FIX44 table rejects it. W-1 and W-2's FIX44 cells are the first evidence, and they are RED-first.

## R-5 Reading 1128 before the parse (FR-001, FR-012, FR-026)

Facts. `MessageView::get` returns the first occurrence. `scan_frame_header` walks the whole frame, last-wins per member,
with its hooks chosen by the table, so a table-specific Length+Data pair can change how it splits fields: the scan cannot
be the thing that picks the table.

**Decision.** `peek_fixt_route(frame, s_hooks, s_table)`, one oracle:

- **Walk** with S's hooks, through `length_data_carry::read_value`, the primitive `scan_frame_header` uses. Not `dict_hooks::none()`: measured, with `none()` a 1128 inside an S header pair's Data is read as the header's 1128, and non-tag bytes in that Data
  make the frame unreadable; with S's hooks both read correctly.
- **A non-standard header pair only a routed dictionary declares.** Its Length tag is neither S's header tag nor standard, so the walk stops there and a later header 1128 reads as past it. No vendored dictionary declares one.
- **MsgType** is the third field's. Anything else there is unreadable. Whether a second 35 fell inside the header is not computed here: it is read only on a mismatch, so the cold path re-walks with the same stop rule (FR-025).
- **1128** is the first one before the walk stops.
- **Stop** at the first tag for which `s_table.is_header_tag(tag) || dict::is_fixt_standard_header_tag(tag)` is false. The second is new, a public function over `kFixtFramingTable` minus its three trailer tags, plus 370, because that table is private to
  `src/dictionary/` and contains 10, 89 and 93. One rule on every shape. **Why 370.** OnBehalfOfSendingTime is in the FIX 4.2 and 4.3 StandardHeaders and in no other vendored one (recompute: the set difference of each vendored `<header>` against `FIXT11.xml`'s, read 2026-11-03). Without it, `…|370=…|1128=4` stops the peek at 370 and draws 373=14 while `…|1128=4|370=…` draws 373=5, one input with two endings by field order. With it, 370 is a header tag in every order: accepted on a FIX 4.2 or 4.3 route, 373=2 on any other. QuickFIX C++ refuses it on every route, because its session dictionary judges the header; that departure is a B-row. Measured on the sampled tags: `is_header_tag` is false for 10, 89 and 93 on FIXT11, FIX44, FIX50SP2 and FIX Latest; true for 8, 9, 34, 35, 49, 91, 627 and 628 on FIXT11, FIX44 and FIX Latest; false for 1128, 1129 and 1156 on FIX44; and false for all of them on FIX50SP2. W-21 pins the standard set to `FIXT11.xml`'s `<header>`.
- **Admin** is `dict::is_fixt_admin_msgtype`, the seven-member set, made public from `reify.cpp`'s private `is_fixt_admin`. Not the session's `detail::is_admin_msgtype`, which excludes `A` on purpose.

Cost: a handful of fields per frame, FIXT sessions only.

A frame the peek cannot read routes to `session_route_`. The arm's scan then uses S's hooks and the same primitive, so it meets the same fault or garble, and the existing dispositions run unchanged (W-19).

**The occurrence rule.** Routing reads 1128 from the header only, as FIXT defines it. A frame whose first 1128 is past the header:

- validating, application route: Reject 373=14, 371=1128. Mechanism: inside the gate after the parse and before the validator, the peek reported no header 1128 and `mv.get(1128)` has a value. `mv.get` is an offset-table probe with no heap. The validator cannot produce this: its one `wire_header_out_of_order` return only tests that 35 is the first non-framing tag, and it sets no `ref_tag`; measured, the frame passes (R-9).
  Engines: C++ `Message::setString` clears `m_validStructure` for a header field after the body starts and `DataDictionary::validate` throws `TagOutOfOrder(field)` before it iterates; J `Message.parseBody` throws `TAG_SPECIFIED_OUT_OF_REQUIRED_ORDER`; Go `validateFieldContent` returns `tagSpecifiedOutOfRequiredOrder`. For an empty value past the header Go answers 373=4 first; fixpp takes C++ and J's order.
  This is also what makes validation and `dict::reify` unable to disagree on a delivered message: `reify` reads the first occurrence, and a delivered validated frame has its first occurrence in the header.
- A dictionary that declares 1128 in a message body would be false-rejected. Measured 2026-11-03: `ApplVerID` is in no `<messages>` block of FIX40–FIX50SP2 or FIXT11, walking components and groups, and its one Orchestra `fieldRef` is in component 1024, StandardHeader. W-4 recomputes this over the vendored files; a hit fails the test.
- validating, admin route: the transport dictionary judges it, as any field (measured: a TestRequest with a body 1128 passes).
- non-validating: routed by the negotiated version and delivered, as today. `reify` now takes the byte, so it follows the routing on that frame when a version was resolved. Disclosed as a B-row.

**Empty `1128=`:** treated as present and unknown, never as absent. Validating: Reject 373=4, 371=1128. All three engines agree
(C++ and J `checkHasValue`, Go `TagSpecifiedWithoutAValue`). Non-validating: S. `resolve_application_version` is not called with it, because its empty-input rule
means "use the default".

**Two 1128s in the header:** the peek and `reify` both read the first; the validator passes (measured). C++ and Go would answer 373=13; the validator has no repeated-tag rule on any session, and this feature does not add one.

**MsgType repeated with a different value (FR-025).** `scan_frame_header` is last-wins on 35; the validator and `MessageView::msg_type()` are first-wins; the Active arm dispatches on `hdr.msg_type`. Measured: a frame with third field `0` and a later `35=D` passes the transport validator on T, A and L, and would reach `fromApp` under a view whose `msg_type()` is `0`, with no resolved version. A frame `D … 35=3` draws no validation at all, because the gate exempts `hdr.msg_type` 3.
The check: on a validating FIXT session, `hdr.msg_type != hdr.fault_ref_msg_type` (`FrameHeader` writes the third field's value on a fault-free frame too), ahead of the 3/5 exemption. Engines on a later 35: C++ and Go 373=14 past the header, 373=13 inside it; J 373=13 past the header and last-wins inside it. fixpp takes C++ and Go. Non-validating: unchanged, B-row. The mismatch predates 099; the routing decision is what makes it consequential.

## R-6 Header tags the session dictionary adds (FR-008)

`table_view::is_header_tag` is exact and allocation-free. **Decision (owner):** the validator gains an optional *envelope* table (S's), passed per call on a non-virtual overload, not held, because one validator serves many sessions. On an envelope route, a tag S declares in its header, other than 35, 1128, 1129 and 1156, takes membership, enum and type from the envelope, ahead of the routed table. That is QuickFIX's rule on T: C++ `Message::setString` classifies a field as header by tag through `isHeaderField(field, pSessionDD)`, and `DataDictionary::validate` checks the header with that dictionary. Non-FIXT callers pass none.

**The four tags.** `FIXT11.xml` closes 1128 at 9, so S judging it would refuse `1128=10` on a FIX Latest route, against US1 scenario 2. 35: a FIX 4.x S closes it at its own version's MsgTypes, so S would refuse a MsgType only the routed version has. 1129 and 1156 name the route's version as 1128 does.

**What the rule changes, measured 2026-11-03** (real `Parser` and validator, `ov_close347_t.xml` closing `MessageEncoding(347)` to `{UTF-8}`, today's validator standing for the routed table): S `FIXT11.xml` plus overlay rejects `347=Shift_JIS` on a Heartbeat and passes `347=UTF-8`; plain `FIXT11.xml` passes it; plain FIX50SP2, the table a NOS routes to on T, passes it, which is the closure this rule stops dropping. Read from the XML, not run: `FIX44.xml` closes 347 at four values and `FIXT11.xml` leaves it open, so a FIX 4.4 route refused a value QuickFIX accepts, and with this rule does not.

**S with no header of its own.** `is_header_tag` is false for everything on the FIX 5.0 XML files, so on shape A the routed table judges standard tags, as it would with no envelope. An overlay's added header tag is true there, so FR-008 holds. The cost, measured 2026-11-03: `347=KOI8-R` on a NOS draws 373=5, 371=347 on FIX44's table and passes on FIXT11's, so with FIX44 registered `1128=6|347=KOI8-R` passes on T and draws 373=5 on A. All three engines judge it with the transport dictionary, so A departs and T does not. It follows from owner item 12: a B-row, and W-3's cell.

**Past the body.** Such a tag is judged and passes, in every field order, on every route. QuickFIX answers 373=14. fixpp's validator tests position on no session, and only 1128 gained a probe (FR-026). Owner, a B-row.

A route carries the envelope when its table is not S's: every application route but S's own, and the transport route in shape A. Measured without the envelope, on a FIX50SP2-plus-overlay S:

| Frame, overlay tag in the header | Routed table, no envelope | The merged dictionary's own answer, the comparand |
|---|---|---|
| NOS, 5020 scalar | FIX50SP2: 373=2, 371=5020 | pass |
| NOS, 5021 closed enum, bad value | FIX50SP2: 373=2, 371=5021 | 373=5, 371=5021 |
| NOS, 5022 int, `abc` | FIX50SP2: 373=2, 371=5022 | 373=5, 371=5022 |
| Logon, 5020 scalar | FIXT11: 373=2, 371=5020 | the session cannot establish without the envelope |

**Narrowing FR-008 to the header.** The table has no trailer store, and adding one to the by-value table costs layout (098
measured about +5.5 % on NewOrderSingle from a 24-byte member). A tag an overlay adds to the *trailer* is rejected on an
application message. Disclosed as an L-row. The standard trailer tags are in the framing table and are unaffected.

**A Length+Data pair an overlay adds to S's header:** R-20.

## R-7 Sites (FR-005, FR-012)

Inbound sites of `session.cpp`, from the graph and a grep over `inbound_tv_`, `session_hooks(`, `validate_inbound_` and
`parse_and_dispatch_`, re-enumerated independently at Gate A:

| Group | Functions | Change |
|---|---|---|
| Frame scans | `on_inbound_not_connected_`, `on_inbound_active_`, `on_inbound_logout_sent_`, `on_inbound_logon_sent_` | select once at arm entry; scan with its hooks |
| Validate gate | the three arms calling `validate_inbound_` | takes the route; parses and validates with it |
| Logon interpret | `on_inbound_not_connected_`, `on_inbound_logon_sent_` | transport route's hooks |
| Dispatch | D-1 `on_inbound_active_` `fromAdmin`; D-2 `on_inbound_active_` `fromApp`; D-3 `on_inbound_active_sequence_reset_`; D-4 `on_inbound_active_out_of_sequence_` PossDup redelivery; D-5 `on_inbound_active_out_of_sequence_` with `validate_sequence_numbers` off; D-6 `on_inbound_active_gap_fill_`; D-7 `on_inbound_active_logout_` | `parse_and_dispatch_` takes the route, and writes the accessor byte from it |

The route is passed by const reference down the arm's call tree. It lives in the arm's coroutine frame, so it survives a suspension; it holds pointers fixed at `open()`, and nothing re-reads a per-connection value after selection. D-2, D-4 and D-5 deliver to `fromApp`, so each carries its own witness cell and mutant (W-7).

Unchanged, all outbound or stored-outbound: `fire_to_admin_`, `inbound_business_reject_leaf_`, `build_send_frame_`,
`send_impl`, `store_then_emit`, `replay_outbound_range_`. So a `toApp` view on a FIXT11 session still knows no application
group, and a resend replay still scans with S's hooks. Disclosed as an L-row; outbound is not what S-026's inbound half
needs, and FR-015 pins only that 1128 survives.

Three table routes exist today (owned dispatch, borrowed validate parse, the validator's own copy). All three take the
route's table; the third is by construction, since the route's table is the validator's.

## R-8 Application frame before Logon completes

The two establishment arms validate before `interpret_logon`, with no negotiated version. Measured (R-18): today's endings differ by dictionary and by the frame's validity: T draws a Reject and stays on every application frame; L and validation off disconnect on a valid one; L draws a Reject and stays on an invalid one. **Ruled by the owner:** on a validating FIXT session an establishment arm disconnects, sending nothing, on any frame whose third field is not admin or whose last MsgType differs from its third field's. One condition, one ending (data-model E-7, cells `NC-a` to `NC-d`, `LS-a` to `LS-d`).

The test reads the third field, never what `interpret_logon` reads. Measured: `interpret_logon` takes the last 35, so `35=D … 98=0|108=30|1137=9|35=A` establishes a validation-off session of either role. Skipping the gate and leaving the refusal to `interpret_logon`, as this section used to recommend, would have established such a frame on a validating session too. Neither review caught that; the measurement did.

Engines, read from source:

| | Acceptor, before its Logon | Initiator, before the Logon reply, frame valid for the dictionary consulted | Initiator, frame also invalid |
|---|---|---|---|
| C++ | as the initiator columns: `next()` validates against its initial `ApplVerID_FIX50` default first | `verify` → `validLogonState` false → disconnect | Reject, stays |
| J | `AcceptorIoHandler.processMessage` closes the socket before `Session.next` | with 1128: `verify` → `validLogonState` false → `SessionException` → `disconnect`. **With no 1128: below** | with 1128: Reject, stays. **With no 1128: below** |
| Go | disconnects without validating | disconnects without validating | disconnects without validating |

J's arm with no 1128, read at `9ac173b2a69ef2305258330d88685eccad80512d`. `targetDefaultApplVerID` is an empty `AtomicReference` on a FIXT session until Logon, so `Session.next` passes null to `DefaultDataDictionaryProvider.getApplicationDataDictionary`.
`SimpleCache.computeIfAbsent(key)` calls `ConcurrentHashMap.get(key)` first, which throws `NullPointerException` on a null key; the loader is never reached. The NPE matches none of the validation catches. `next`'s `catch (Throwable)` rethrows it as `RuntimeError` unless `rejectMessageOnUnhandledException` is set, in which case an application frame draws a BusinessMessageReject. `SingleThreadedEventHandlingStrategy` catches it and logs. So J, by default, logs an error, sends nothing, does not disconnect and does not consume the sequence number, and the session waits out its Logon timeout. That is read from source, not run; the other event-handling strategy was not read.

So no engine gives one ending to all of it. fixpp cannot validate without a negotiated version, and the ruling matches Go on every cell and the C++ and J disconnect on a valid frame. It departs from C++'s Reject on an invalid frame, and from J's silent wait.

## R-9 Refusals (ruling 1, FR-009, FR-010, FR-025, FR-026)

**Why each refusal cannot be left to the validator.** Validator-level, through the real `Parser` and `dictionary_driven_validator`, the real `scan_frame_header` and `resolve_application_version`, and a scratch prototype of the peek, per shape.

- Probe: `validator_level/probe3.cpp` under the parent repo's `research/G19-fix-fpml-iso20022/research/probes/099_fixt_session_today/`, never in the tree. Built 2026-11-03 against `build/linux-clang-debug` at `68b9a5dd` (equal to `c0a828ef` outside this bundle): `ninja fixpp_wire fixpp_dictionary fixpp_dict_dispatch_bridge fixpp_core` reported no work, and `strings` on the binary shows `load_any_with_overlay`'s text, so 098's code is linked. Command: `clang++ -std=c++23 -stdlib=libstdc++ -g -Iinclude -Isrc probe3.cpp <those four archives, twice> libpugixml.a`, run as `probe3 dictionaries <dir of two overlay files>`. Re-run against the release tree: every cell identical.
- Engine dictionaries in each shape: FIXT11, FIX50SP2, FIX Latest; negotiated version 9. `1128=4` is therefore the resolvable value with no dictionary.
- This table is why, not what happens. What a session does is data-model E-7, from R-18's `Session` measurement. FIX 4.x tables with the framing surface are unmeasured (R-4).

Application frames, validating. `2/8` means 373=2, 371=8.

| Input | Resolver | Validator on S: T / A / L | Validator on the negotiated slot | E-7 cell |
|---|---|---|---|---|
| no 1128, valid | — | 2/8 / pass / pass | pass | `AC-d` |
| header `1128=99`, `abc` | fails | 2/8 / 5/1128 / 5/1128 | not consulted | `AC-b` |
| header `1128=0`, `1` | fails | 2/8 / pass / pass | not consulted | `AC-b` |
| header `1128=4` | resolves, no dictionary | 2/8 / pass / pass | not consulted | `AC-b` |
| header `1128=` | not called | 2/8 / pass / pass | not consulted | `AC-b` |
| body `1128=6` | not called | 2/8 / pass / pass | pass, and `mv.get(1128)` is `6` | `AC-c` |
| body `1128=abc` | not called | 2/8 / 5/1128 / 5/1128 | 5/1128 | `AC-c`: the probe runs first |
| body `1128=` | not called | 2/8 / pass / pass | pass | `AC-c` |
| after the trailer's 93 and 89, `1128=6` | not called | peek stops at 93 | `mv.get(1128)` is `6` | `AC-c` |
| third field `0`, later `35=D` | — | transport validator: pass, T / A / L | — | `AC-a` |
| third field `D`, later `35=0` or `35=3` | — | negotiated slot: pass | — | `AC-a` |
| third field `3`, later `35=D` | — | transport validator: pass | — | `AC-a` |
| `35=0`, `35=D` adjacent | — | transport validator: pass | — | `AC-a`, 373=13 |
| `35=D`, `35=D` adjacent | — | negotiated slot: pass | — | `AC-d`: no split |
| header `1128=9`, then `1128=10` | first resolves | — | slot 9: pass | delivered, read as 9 |

Admin frames carry no refusal: the transport dictionary judges 1128 on them. Measured: Heartbeat with `1128=10` is 5/1128 on FIXT11, pass on FIX Latest; `1128=99` is 5/1128 on both; `1128=` passes on both.

**The 1137 check's order (FR-010, owner item 3).** Gate first; a gate Reject naming 1137 on a Logon disconnects; the initiator gains the acceptor's block.

What the engines do. Both read BeginString first and throw `UnsupportedVersion` on a mismatch (QuickFIX C++ `Session::next`, J `Session.next`). On a FIXT Logon both then read 1137 before they validate, but only its presence matters there. An absent one: C++ `FIELD_GET_REF` throws `FieldNotFound`, whose catch sends Reject 373=1 and disconnects; J's `getString` throws `FieldNotFound`, whose catch logs and disconnects, with no Reject. A present value is stored with no check in C++. J checks it in `nextLogon`, after validating, against the session dictionary's ApplVerID enum, and answers with a Logout and a disconnect (`RejectLogon`). So "C++ and J read 1137 first", which this section used to say, holds for presence only and neither engine has a value check ahead of validation. Both also keep the sequence number on a Reject of a Logon (`generateReject`'s `msgType != Logon && != SequenceReset` test in each).

Consequences at fixpp, each measured or read from source:

| Input | Ending |
|---|---|
| Ruling 1's "a disconnect" | met for every single fault |
| Single fault, NextNumIn | kept for absent and L's `99`, `abc` (gate path); consumed for `4`, empty and T and A's `99`, `abc` (block path, after `check_inbound`). Observable only on an initiator's reconnect: an Engine acceptor's session ends with its first peer |
| `99` + stale 52 | T and A 10/52, L 5/1137 with a disconnect |
| absent + stale 52 | 1/1137, disconnect |
| absent + an undeclared tag | **2/that tag, stays**, until the Logon timeout: the gate names the other tag, so ruling 1's disconnect does not fire for this double fault |
| `99` + posture mismatch | T and A Logout, L 5/1137 with a disconnect |
| `99` + off-list mTLS | acceptor: T and A event, nothing sent; L Reject, as today, with a disconnect. Initiator: Reject, no event, on every shape, since its block is ahead of mTLS, on a direct-constructed session only (R-18) |
| wrong BeginString + `99` | T and A Disconnected, L Reject with a disconnect |
| `141=Y` + `4`, initiator | no reset: its block is ahead of the reset unit |
| New code | one branch in each establishment gate, one new block on the initiator |

**That named pin does not say what its name suggests.** On a validating session, absent 1137 plus a stale 52 already answers 1/1137 at `c0a828ef`, on T and L, and stays: 52 does not win. The pin holds because it runs non-validating, so that input has two endings by validation setting today.

**Why the initiator's block is where it is.** Read from source: the arm runs `check_inbound`, then the 141=Y reset unit, then mTLS, then today's 1137 record. At that record site a reply with `141=Y` and a bad 1137, a single fault, would durably reset both stores and then be refused. So the block goes right after `check_inbound`, which costs the mTLS order above on a double fault. The acceptor's received-141 reset is in its reply block, after its 1137 block, so it has no such problem; its `reset_on_logon` knob resets ahead of `check_inbound`, today and on a non-validating session, and that is unchanged. The negotiated version is still written only where it is today, so a refused Logon records nothing.

**Why an empty 1137 or 1130 is 373=5 and an empty 1128 is 373=4.** The first two are answered by S3's block, whose test is today's acceptor's, past a gate that lets an empty value through (measured: `1128=` passes). The third is a new refusal, and takes the code all three engines give.

**Reject fields.** Every arm passes `hdr.msg_type`, last-wins, to `emit_session_reject_`, and the Active arm also to `consume_rejected_seqnum_`. Measured: `D…4` and `D…A` are kept with 372 `4` and `A`; `4…D` is consumed. So `RejectDecision` gains a flag for the FIXT refusals, and with it the arm takes the Reject's RefMsgType and RefSeqNum from the header scan's fault record: RefMsgType, and the value that keys the consume, are the third field's; RefSeqNum is `fault_ref_seq_num`, the first 34, because S's hooks split a pair S does not know on a refused frame, and an embedded `34=7` would otherwise become `hdr.msg_seq_num`. A non-FIXT session leaves it false and reads `hdr` as today, so SC-005 holds.

**Stale negotiated version (FR-027).** `negotiated_appl_version_` has no reset: the acceptor overwrites it per Logon, the initiator only when the reply resolves. Before this feature that value only reached `negotiated_version_profile()`, and ruling 1 keeps validation-off sessions unchanged, so it keeps today's writes. Selection reads a new `route_negotiated_`, written at the same two sites on every Logon, and `Unknown` when the initiator's reply has nothing it can resolve. Selection reads it only outside the establishment arms: `install_reconnected_transport` re-enters LogonSent with the old value still there, and an application frame in LogonSent takes `session_route_` without looking at it. So nothing needs a reset at the connection boundary. **A stale read there would show.** Measured 2026-11-03, varying which table scans, as a proxy: a non-validating initiator in LogonSent given `35=D … 98=0|108=30|1137=9|5030=<n>|5031=x<SOH>34=7|35=A` ends Disconnected, NextNumIn kept, on a table without the pair, and Active on one declaring it. W-5 carries the cell and M-27b the mutant.

## R-10 The accessor (FR-014)

`fromApp` has no session parameter, the C callback no session either, and `negotiated_version_profile()` is strand-confined. **Decision:** one byte on `MessageView`, under the single contract in data-model E-5. The only size pin that sees `MessageView` is an equality with a typed wrapper, so it holds. C reads it through `fixpp_msg_version` (R-11, data-model E-8).

**Rejected.** A session lookup from the callback (blocks in C; no handle). Reading the table's version (with validation off and no dictionary the table is S's, which is not the resolved version).

## R-11 C ABI (FR-016)

C ABI 1.14: three added symbols, added constants, and one declared break. `fixpp_session_config_set_default_appl_ver_id(cfg, const char* wire_value)` parses with
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

**The inventory is a recipe, not a list** (098's, `specs/098-dialect-overlay/data-model.md` §C): `git grep -l -e 'include/fix/c_api' -- tools .github/workflows ci tests/abi`, each hit read for whether a header edit reaches it. Run again at implement. On 2026-11-03 it answers:

| Hit | What 099 does |
|---|---|
| `tools/capi_freeze.sha256`, `tools/check_capi_freeze.sh` | **Changes.** Five frozen headers change (`dict.h` gains the constants), so the gate fails until the manifest is refreshed by the recipe in the script's header, in the same PR. W-12 shows RED first |
| `tools/check_capi_reentrancy.sh` | Reads each new symbol's doc block: exactly one reentrancy token. The header text must carry it |
| `tools/check_capi_occupancy.sh`, `tools/test_capi_occupancy_negative.sh`, `tools/abi_history/error_codes_v1.txt` | No new error code, so unchanged; run to confirm |
| `.github/workflows/tier1.yml`, `ci/test-check-wheel-payload.sh`, `ci/test-tier1-python-policy.sh` | Name the path. No change |

Beyond the recipe, by the same reading of 098's diff: `include/fix/c_api/{session,engine,message,dict}.h`, `version.h` notes and MINOR, `tests/abi/golden/fixpp_capi_symbols.txt`, `tests/capi/version_test.cpp`, `.specify/2i-capi.md`, `.specify/api-contract.md`, `brain/components/c-api.md`, `bindings/python/tests/wheel/test_import_surface.py`, and `src/log/format_registry.cpp` for the role loops' new log line (the session's `FIXPP_SLOG` idiom registers each format string there). The symbol map is a wildcard. SWIG strips the prefix.

`bindings/python/fixpp.i`'s GIL table: `engine_start`'s row says "returns immediately", which a FIXT engine now falsifies. It stays `hold`, under the reason `dict_load_from_xml`'s row already uses: CPU-bound, no worker-deadlock class, releasing for throughput deferred. Not "no callback can want the GIL", which is false in a process with a second engine, whose worker waits in `PyGILState_Ensure` for the walks.

**The break (owner item 6, `[const §X.7]`).** `gh release list --exclude-drafts` shows no public release, run 2026-11-03, so the bump is MINOR. `[2c §5]` commitment 1 and `[2i §4.6]` specify `{kind, session, application}`. The shipped symbol returned BeginString and raw 1137, which meant that an application message read `appl_ver_id` as null. The symbol, signature and handle errors stay; the struct's layout and the result's meaning change, each a listed breaking effect in `api-contract.md` §11. The clause's four bullets are tasks:

- `FIXPP_C_ABI_VERSION_MINOR` to 14.
- **BREAKING** on `fixpp_msg_version` and the struct in `message.h`, in `version.h`'s version comment, in the PR description and in the B&L delta. The migration line: read raw values with `fixpp_msg_get_string(msg, 8, …)` and `fixpp_msg_get_string(msg, 1137, …)`.
- Every in-repository consumer in the same PR. Recipe, run 2026-11-03 over `tests bindings bench tools` and the parent's `phase-9-harness`, whose 41 files use `fixpp_` and none `fixpp_msg_`: `tests/capi/message_read_test.cpp` only, with `tests/abi/golden/fixpp_capi_symbols.txt` naming the symbol unchanged. `bindings/python/fixpp.i` does not `%include` `message.h`, and declares what it exposes one by one, so `fixpp_msg_version` is added there with an output typemap. `brain/components/c-api.md`, CA-008's row and `spec/behaviors-and-limitations.md` read too. Re-run at implement.
- All four Appendix A controls run anyway.

**Design-document notes.** `[const §XX]` governs the constitution only, and no article states this shape, so `Updated (<feature>)` notes are the established form (098 used it at the same `[2c §5]`). Text, to apply at implement (plan.md, the C ABI, Python and TOML phase):

> **At `[2c §4.3]`:** Updated (099-fixt-appl-version-validation): a FIXT session resolves 1128, else the peer's Message Type Default for the MsgType, else 1137. `version_profile` stays 4 bytes and carries no per-MsgType default. When the session wrote `MessageView::resolved_application_version()`, `dict::reify` returns `{application, vt11, that byte}` and reads no profile; otherwise it reads 1128 and the profile.
>
> **At `[2c §5]`, commitment 1:** Updated (099-fixt-appl-version-validation): shipped in C-ABI 1.14, a declared break under `[const §X.7]`. Before it `fixpp_msg_version` returned BeginString and raw tag 1137. **Commitment 5:** the constant set gains `FIXPP_APPL_VER_FIXLATEST`, and `FIXPP_SESSION_VER_*` and `FIXPP_MSG_KIND_*`, because the struct's bytes need names. They are in `c_api/dict.h`.
>
> **At `[2c §4.6]`:** Updated (099-fixt-appl-version-validation): `Dictionary::as_table_view(fixt_framing)` adds the validator-private framing stores for FIX 4.0–4.4, registers NoHops's group metadata on a table whose own header does not declare it, and equals `as_table_view()` on every other store. A form taking extra pairs also registers them, last.
>
> **At `[2c §4.9]`:** Updated (099-fixt-appl-version-validation): the registry keeps a FIXT.1.1 dictionary and answers `transport()`; `get()` is unchanged. Validators and tables shared across sessions live in the session library's route set, because `[arch §2.3]` keeps the registry from owning a validator.
>
> **At `[2b §6.5]`:** Updated (099-fixt-appl-version-validation): `dictionary_driven_validator` gains a non-virtual `validate` overload taking an envelope table and a `table()` accessor. `Validator` keeps its five pure virtuals (`[const §XIV.2]`), and the class adds no data member.
>
> **At `[2i §3.10]`, commitment 5:** Updated (099-fixt-appl-version-validation): `c_api/dict.h` also holds `FIXPP_APPL_VER_FIXLATEST`, `FIXPP_SESSION_VER_*` and `FIXPP_MSG_KIND_*`.
>
> **At `[2i §4.6]`:** Updated (099-fixt-appl-version-validation): shipped in C-ABI 1.14, a declared break under `[const §X.7]`, with the three added symbols of `specs/099-fixt-appl-version-validation/contracts/surface.md`. "Set at parse time" reads: the session writes the byte after the parse and before the callback, on its strand, so no reader sees it change. The headers spell the token `thread-safe`, which `tools/check_capi_reentrancy.sh` counts; this document's `FIXPP_THREAD_SAFE` is that token.
>
> **At `api-contract.md` §11:** Updated (099-fixt-appl-version-validation): `fixpp_msg_version`'s layout and result change, a breaking effect declared under `[const §X.7]`, and its migration line is in that feature's `contracts/surface.md`.

## R-12 TOML (FR-017)

`[dictionary] extra_paths = ["FIXT11.xml", "orchestra/OrchestraFIXLatest.xml"]`. Same path resolution and `dict::load_any`
trap as `path`, each failure a diagnostic at `dictionary.extra_paths`; a non-array or non-string is one too. Pushed after
`path`, so `engine.dictionaries.front()` stays the session default.

**Lead, not in scope:** `recognize_keys` walks top-level keys only, so a misspelt `[dictionary]` sub-key is silently ignored
(read from the code, not run). A misspelt `extra_paths` then fails loud at registration, `open()` or Logon only where a dictionary it would have added is needed: a non-validating session whose default is the `path` file's version opens, and a `1128=10` frame takes S's view with no failure (FR-013). To file as an issue.

## R-13 Live cell (NFR-005)

`HP-*-init-fixt11-fix50sp2-app-bn` sends one 35=BN from fixpp. The peer sends **no** application message: the logs show only its Logon reaching fixpp. Turning validation on alone would validate one Logon with FIXT11 and never reach the changed path.

**Ruled by the owner:** add one peer-to-fixpp 35=BN to `fixt_app_script.yaml`. `send_conversation_message` builds from the script's intent in `quickfix-cpp/counterparty`, so a script-only step looks possible; QuickFIX/J's path is unverified end to end. It changes a digest-bound script and may need a counterparty image republish (`REMAINING-WORK.md` assumes one). Attempted without a republish first; back to the owner if one proves necessary. "First" means the pre-merge attempt only (plan.md, owner item 16).

**The step's id is `B-02`.** Read from `interop_counterparty_main.cpp` and `InteropCounterparty.java`, and checked by `strings` in the image: `handle_conversation_fromApp` and `handleConversationFromApp` look up `("B-02", "peer")` at stage 0, the first inbound application frame, in both counterparties, and `P-01`, sent at logon, is QuickFIX-cpp's alone. So a step under another id is never sent, and that id is what needs no counterparty change.

## R-14 Performance gates (NFR-001, NFR-002, `[const §VIII.3]`)

`bench/session/on_inbound_frame_bench.cpp` (`InSequence`, `InSequence_Validated`) and `scan_frame_header_bench.cpp` exist but are
ungated in `bench/ci-suite.txt`. **Plan:** paired base-vs-candidate by B41's method for `on_inbound_frame` and `validator_bench` on
a non-FIX session shape (FIX.4.4), and a new FIXT case in `on_inbound_frame_bench.cpp`, reported for information, so the peek's cost is on record in the same PR. It has no budget, by decision: the base has no routed frame to pair against, and this case reports what a budget would bound, the peek's walk and the envelope's per-field test.

`tests/alloc_guard/test_validate_gate_alloc_guard.cpp`, which the spec named, never constructs a `Session`. The real-Session
guard is `tests/perf/test_session_recovery_alloc_guard.cpp`, so NFR-002's witness is modelled on that file. Corrected in the spec.

## R-15 Records to re-read (FR-021)

`L-041-2`, `L-033-1`, `L-097-1`, `S-026` appear in: `.specify/architecture.md`, `.specify/constitution.md` (an amendment log, not
edited), `brain/components/dictionary.md`, `tests/dictionary/fixt_header_merge_test.cpp`, `tests/interop/happy/hp_fixt50sp2_app_message_test.cpp`,
`tests/wire/fixt_header_validate_test.cpp`, `tests/wire/required_scope_two_tier_test.cpp`, `spec/coverage-index.md`, and in the parent
`REMAINING-WORK.md`, `remaining-work/{post-v1.0-deferred,release-engineering,session-conformance}.md`. No constitution article speaks
to per-message routing, and nothing forbids it. `[const §X.6]` applies: `/clarify`, `/analyze`, Gate A and the owner's `/plan` sign-off.

## R-16 What closing S-026 honestly requires (FR-020, FR-024)

These are tasks, in plan.md's records phase. A condition unmet means the row stays open and the PR says so.

| Row | Condition | Honest under this plan? |
|---|---|---|
| S-026 → `done` | W-4, W-5 and W-9 green on {T, A, L}, since refusal correctness is part of what it promises; W-1, W-2, W-3, W-7, W-10 green; W-18 green at T118, and a red at T143 goes to the owner under NFR-005; `[const §VI.6]`'s Gate A and Gate B | Yes, with W-25 for §5.3.4, which no live cell can witness: the row says so |
| `coverage-index.md` §5.3.4 | Maps to S-026, and now S-026 delivers it. §5.3.2 and §5.3.5 stay on S-026; §5.3.3 stays on S-025. Correct §4.3.7's, §5.3's and the FIXT section's "S-026 tolerate-only" and "`backlog`" wording | Yes |
| L-041-2 → closed | Its open claim is the two-dictionary resolution. W-1 and W-2 green. Its accept-only divergence stays as B-081-1, and R-6's residue gets new rows | Yes |
| L-033-1 → closed | Its stated workaround is "implement routing at the `fromApp` level". Retiring it removes that advice, which is honest only if what replaces it works: W-10's clone and reify arms green, and W-12's C arm | Yes, given E-5 |
| L-097-1 → closed | All three of its claims (TOML one dictionary; C cannot fill `EngineConfig::dictionaries`; no `default_appl_ver_id` setter) answered by FR-016 and FR-017, witnessed live by W-12, W-13, W-14 | Yes, with a new live L-row for what remains: a non-validating FIXT session with a bad config is refused only at `open()` inside the role loop, logged, not returned (R-2) |
| L-033-5 | The `Session::open()` guard comment claims to close its "no config-load failure" on an Engine, and does not on a non-validating session. Correct the comment and the row | Yes |

## R-17 Existing tests this feature changes

Recipe, 2026-11-03: the files under `tests/` matching `FIXT.1.1` intersected with those matching `validate_inbound`, then read by hand for one session that is both. It reports positives (below), so its zero on the other files means something. Re-run it at implement.

| File, cells | Session | What 099 does |
|---|---|---|
| `tests/session/test_fixt_logon_establishment.cpp`, `FixtLogonEstablishment097.W7a`…`W7c0`, `W7v1` | validating, T, registry one application version | Stays green. A real-`Session` pin of a T Logon established, `1137=10` on a registry with no FIX Latest drawing 373=5, 371=1137 and a disconnect, and an undeclared Logon tag drawing the gate's Reject, not Active. E-7 gives the same answers: the gate's Reject on 1407 names another tag, so it stays in the arm |
| same file, `W7d`, `W7d0`, `W7v2` | validating, L, registry FIX Latest | Stays green: `1137=9` answers through the fallback, whose dictionary is S's version, so S answers |
| same file, `W7e`, `W7e0`, `W7f`, `W7f0` | validating, S `fix44_dictionary_with_1137()`, registry one minimal FIX50SP2 | **Changes.** That dictionary defines Heartbeat and Logon as its only admin messages, so it fails FR-003, and with no FIXT11 registered `open()` now refuses (FR-004). They are named characterization pins of the old single-validator answer. Each needs a FIXT11 in the registry, and then, derived from the route rules (data-model E-6, E-7): **W7e0** stays established. **W7e** becomes established too, because FIXT11 accepts 1407 on a Logon (`W7a` shows it), so its purpose, a dictionary-decided Reject, is gone; drop it or restate it with a tag FIXT11 lacks. **W7f and W7f0** do not hold as written: `establish_then_feed` logs on with `1137=9` and the NOS has no 1128, so the NOS resolves to v50sp2, which the minimal FIX50SP2 answers, and it defines no NOS, so **the control W7f0 goes RED too**. Rewrite: put `1128=6` on the NOS. v44 is S's own version, so S answers with no registry slot. Then W7f0 is delivered, and W7f's 1156 passes through the framing surface (FR-007), so W7f is delivered too, where it used to be a Reject. Those literals go in the tests from this table. A run that disagrees stops for a decision; the literal is not re-recorded from the run |
| same file, `W7b`, `W7b0` | validating, T, registry FIX Latest | Stays green by the route rules. A direct-constructed session now walks Orchestra FIX Latest in `open()` where it did not, about 1.7 s at `-O0`. `W7d` shows that shape passes its pump budget at `-O0` (R-3); a sanitizer multiple is unchecked until it runs |
| `tests/session/unparseable_frame_disposition_test.cpp`, `ProfileCell` with `Profile::fixt11` and validation on: `ProfileRoleMatrix`, `ValidatorLive`, `UnparseableFrameDisposition/LogonRefusal.MalformedTagLogon_Refused/ValOn_FIXT11_{Acceptor,Initiator}` | validating FIXT, S is the same partial test dictionary | **Changes.** `open()` refuses, as above. Fix the fixture: add a FIXT11 to the cell's registry, for the validating cells. Derived, **unverified until run**: their assertions then hold, because a Logon with `1137=6` is judged by FIXT11 and resolves to S's own version, faults are disposed of before the gate, and the missing ClOrdID is judged by S with the framing surface added, 373=1, 371=11 |
| `tests/session/engine_reset_unit_stop_test.cpp` | its FIXT case and its validating cases are separate sessions | None |
| `tests/capi/message_read_test.cpp`, every `fixpp_msg_version` case | C | **Changes**, as `[const §X.7]` requires. The tag-8-absent case keeps its code |
| `tests/session/test_fixt_logon_establishment.cpp`, `W_StaleSendingTime_Beats1137Gate_Reject52_No1137` | not validating, S `kMinimalFix44Xml` | None. Its name reads wider than it is: R-9 |

A validating FIXT session on a partial S used to work in C++, for the messages that S declares. It is now refused. That follows from FR-003's "all seven admin messages", and it goes in a B-row. SC-005 protects non-FIXT tests only, so these edits are allowed, but they are FIXT behaviour changing under existing tests and the PR says so.

**FR-032's edit, what pins the file.** The recipe above cannot see these. Measured by mutation on data-model E-10's final placement, 2026-11-03 (the parent's `research/probes/099_fixt_session_today/gate_a_l2r2/mtpin/`): 55 distinct test binaries, whole and unfiltered, with the edited file bind-mounted, and `ctest -R read-tier-byte-diff`. Five binaries fail on the edit, plus the four `vt11` hashes, and each passes outside the namespace.

| Binary, test | Pin | What 099 does |
|---|---|---|
| `codegen_082_class_xml_consistency_test`, `FlyweightSetEqualsStructuralGroupTags`, `PerMessageTopLevelGroupRefsMatchStructure` | `tests/codegen/test_082_class_xml_consistency_test.cpp`, `expect_groups = 1` | Update from the failing assertion on the edited tree |
| `codegen_determinism_test`, `GeneratedMatchesGolden`, `AdditiveOffOnByteDiff` | `specs/003-dictionary-codegen/contracts/golden/vt11_Messages.golden.hpp` | Regenerate |
| `dictionary_pure_tests`, `ReusedTagCensus` | `tests/dictionary/reused_tag_census_test.cpp`, `{"FIXT11.xml", 1}` | Update from the failing assertion: Reject already carries members the group reuses, so the new count is not assumed |
| `dictionary_pure_tests`, `XmlEnumCodeset` | `tests/dictionary/xml_enum_codeset_test.cpp` | 9 and 56 became 10 and 58 in that run |
| `dictionary_required_scope_census_test`, `SixUnchangedDictionariesBareStoreExactSet` | `tests/dictionary/required_scope_census_test.cpp`, `expected_count = 1` | Update from the failing assertion |
| `required_scope_parity_test`, `QuickFixGroupGoldenMatchesOracleAcrossNineDicts` | `tools/quickfix_required_golden/golden_groups.csv` | A new `FIXT11,A` context, regenerated through `tools/quickfix_required_golden` |
| `ctest` `read-tier-byte-diff`, not one of the 55 | `tests/codegen/read_tier_byte_diff_test.cmake`: exactly `vt11/{Fields,Messages,Reify,Validator}.hpp`, the other twelve OK | Update the four hashes from the regenerated files |
| `dictionary_pure_tests`, W-17's no-argument cell. Not part of the measurement above: this feature creates the test, so the 55 could not include it | `tests/dictionary/test_099_fixt_framing_table_view.cpp`, `FIXT11.xml`'s literal only | Update from the failing assertion, right after the edit: the hash seen failing first, as for the four `vt11` hashes, and `git diff c0a828ef -- src include` empty at the record. The other files' literals stay as first recorded, and SC-005 rests on them |

`dictionary_overlay_tests` passes, 314 tests: the placement keeps `FIXT11.patch` applying (below). Not the edit: `config_045_tests`' two failures are the namespace, whose mapped root passes `access(W_OK)`, and `interop_golden_check` is a tool that exits 2 with no arguments. **The limit:** these binaries are compiled against the unedited generated `vt11` code, so a compile-time pin on the regenerated headers shows only in a real build. Not run against the edit: the `dictionary_sha1[FIXT11]` rows of `golden.csv` and `golden_groups.csv`, which `required_scope_parity_test`'s regeneration rewrites (read); the rest of the session and C-ABI suites, Python, packaging or interop.

**Re-derivation.** `unshare -Urm`, then a private `mount --bind` of the edited file over `dictionaries/FIXT11.xml`, then each binary under `build/linux-clang-debug/bin`, whole and unfiltered, with its exit code logged. `mtpin/edit1/` is this recipe; `mtpin/edit/` also bound the config fixture, agrees on every binary, and is the run that holds `read-tier-byte-diff`. Re-run each failure outside the namespace, and `sha1sum` the tree's file before and after. A hit proves the instrument reports.

**The patch fixture depends on where the fields go.** `overlay_patch.hpp` requires a hunk's context block to match exactly once, and `FIXT11.patch`'s `fields` hunk has `</field>` then `</fields>`, so a three-field append breaks it. Hence 384 and 385 after 383, and 1410 directly before 1409, out of numeric order by one, so that 1409's `</field>` stays directly above `</fields>`. Each of the two hunks' context blocks matches once, as in the base, and `dictionary_overlay_tests` passes on it.

**Mirrors.** `tests/fuzz/corpus/dict_xml_loader/seed_fixt11_real_dictionary.xml` stays: `tests/fuzz/CMakeLists.txt` rules that a corpus seed is a frozen record. `tests/config/fixtures/FIXT11.xml` stays, because no W-14 cell sends the group. W-14's cells and the TOML live cell load that mirror, so a green TOML cell is no evidence for the edited file.

## R-18 What a real `Session` does today (the oracle data-model E-7 points at)

**Recipe.** `h.cpp` under the parent repo's `research/G19-fix-fpml-iso20022/research/probes/099_fixt_session_today/`, never in the tree; W-4, W-5 and W-9 are its durable form. Modelled on `tests/session/test_fixt_logon_establishment.cpp`'s `FixtSetup`: a direct-constructed `Session`, `mock_clock`, raw frames through `on_inbound_frame`, `transport_send` captured, `NextNumIn` read through `session_test_access::seqnum_mgr`, `fromApp` and `fromAdmin` counted, `recent_events()` read for `session_event_compid_authorization_failed`. Built by that directory's `compile.sh` and `link.sh`: the test's compile flags plus `-Isrc`, and `ninja -t commands session_fixt_logon_establishment`'s link line with `h.o` in place of the test object, against `build/linux-clang-debug`. Run 2026-11-03 as `h dictionaries {T|A|L} {on|off} > {T|A|L}_{on|off}.log` from that directory, one log per pair, all rc 0, and `h dictionaries mtd > mtd.log`, which also drives its 5999, `141=Y` and full-group Message Type Default inputs with validation off.

**Commit.** `c0a828ef`. `git diff --stat c0a828ef fab0d94e -- . ':!specs/099-fixt-appl-version-validation'` shows only `.specify/feature.json`, so the tree at `fab0d94e` is that commit's library. `ninja fixpp_session fixpp_mock_clock` compiled nothing. A library file older than 098's merge date is not staleness: ninja compares source mtimes and dependencies, and the worktree's `session.cpp` predates the library. `strings` on the binary finds `load_any_with_overlay`'s text and the 093 format string, and `nm` finds `kSendHeaderTags` from 098's `send_header_tags.hpp`, so 098 is linked.

**Configuration that decides the results.** Registry built from FIXT11, FIX50SP2 and FIX Latest, of which `version_registry` drops FIXT11 at this commit. `default_appl_ver_id` v50sp2. S is FIXT11 (T), FIX50SP2 (A) or FIX Latest (L). `one_way_ca` except column h. Column h: `mtls_ca`, a policy listing another principal, and `CN=STRANGER` delivered through `attach_accepted_transport`, with the capture on that transport because live writes go there. Column i: `session_posture::production`, frame with `464=Y`. Stale means `52` four years behind the clock. Peer sequence 1 in the establishment arms and 2 after.

**Result.** The logs, one tab-separated line per cell, `shape`, `on|off`, `arm`, `input` and the ending, as `h.cpp` prints them and tasks.md T007's trace must spell them, are the oracle; data-model E-7 points at them. On 2026-11-03 the harness produced them with these checks:

- **A known positive.** `off`, NotConnected, stale Logon with no 1137 answers 373=10, 371=52, Disconnected: what `W_StaleSendingTime_Beats1137Gate_Reject52_No1137` pins.
- **A seeded row that must differ.** The same run with one input changed: an undefined tag 5999 in the body. The first placement, on `NOS` in Active on T, reported **no difference**, because T already rejects that frame on tag 8, so the seed could not move it; a zero from a seed needs a cell that can move. Placed on `Heartbeat` in Active on T, `diff` reports that row alone: delivered becomes Reject 373=2, 371=5999. In a second placement, `h dictionaries seedcheck` on L's `NOS` in Active, delivered becomes the same Reject, and the harness exits non-zero if the two match.
- **Determinism.** Two runs of T differ in nothing. A full re-run of all six, made to add column i, left every other line identical.

**Not reached. These cells are not measurements.**

- A, validating, Active and LogoutSent: the session cannot establish. The log says so per row.
- A, validating, every Logon in NotConnected and LogonSent: tag 98 is rejected first, so those cells measure the 98 fault, not the column's input.
- LogonReceived: `on_inbound_active_`'s arm, the same code as Active, and not separately reachable with a frame: the Logon arm enters it and leaves it inside one call.
- Columns h and i in Active and LogoutSent: authorization and the posture check run in the establishment arms only. Read from source; not driven.
- Column h on an initiator: the harness delivers `CN=STRANGER` through `attach_accepted_transport`, the acceptor's seam. On an Engine route `drive_reconnect` authorizes the identity and drops the connection before LogonSent, so `LS-h` is reachable only on a direct-constructed session. Read from source; not driven.
- An Engine's accept loop filters a first frame by `SessionId` before a session exists, so a wrong BeginString on an Engine acceptor's first frame does not reach `NC-f`. Read from source; the harness constructs the session directly.

**What it changed.**

- An establishment arm's "skip the gate, `interpret_logon` refuses" would have established `35=D … 35=A` carrying Logon fields, because `interpret_logon` reads the last 35 (R-8). Reviews and bundle both missed it.
- Absent 1137 plus a stale 52 already answers 1/1137 on a validating session, so 52 does not win there (R-9). The review's "named pin" concern is narrower than stated: no option introduces that split.
- Wrong BeginString plus a dictionary fault already draws a Reject ahead of `interpret_logon`, on T for an absent 1137 and on L for `99`. So the departure from C++ and J's BeginString-first order predates this feature for gate-faulted frames.
- Off-list mTLS plus `99` on L already Rejects with no authorization event, so mTLS order already depends on shape.
- The acceptor's 1137 block consumes the number (1→2); a gate Reject in an establishment arm keeps it. An mTLS refusal consumes it too.
- `D…5` in Active logs the session out on T, L and validation off, and in LogoutSent `D…5` confirms the Logout, so E-7 must say what FR-025 does there.
- `D…3` in Active is consumed in silence: no Reject, no callback.
- A Logon with `384=1|372=D|1130=10`, or the same with `385=S|1410=Y` (`mtd.log`), is refused on T, validating, 373=2, 371=384, because `FIXT11.xml` declares no 384, and establishes on L and with validation off. So FR-032's edit is what makes Message Type Default reachable on T and A.
- The validator answers an undeclared tag ahead of a missing or unknown 1137, in `mtd.log`, whichever field comes first: absent plus 5999 is 2/5999 on T and L, `99` plus 5999 is 2/5999 on T and 5/1137 on L.
- `141=Y` with `1137=4` goes Active on a validating initiator today. Whether its reset ran is read from source, not measured: a fresh session cannot show it through NextNumIn.

## R-19 Message Type Default (FR-030 to FR-032, owner item 8)

**What FIXT 1.1 says.** Within MsgTypeGrp, "for each Message Type where the default application version is different from the Session Default Application Version", RefMsgType(372), RefApplVerID(1130) and DefaultVerIndicator(1410). It has precedence over the Session Default, and Explicit has precedence over it. 372 and MsgDirection(385) are "from the point of view of the sender of the Logon message", and both are required when NoMsgTypes is above 0. "FIX Message Processors must maintain state regarding the Default Application Version."

**What the engines do.** None implements it. QuickFIX C++ and Go ignore the group; QuickFIX/J's `FIXT11.xml` declares it and nothing reads it. So this feature goes beyond them, and no live cell can witness it. W-25 is raw frames.

**Decisions, none needing the owner, each following a ruling already made or the standard's text.**

- **Inbound only.** The owner's ruling was to read it from Logon and add the level to resolution. fixpp declaring defaults in its own Logon, and a peer's `385=R` entries changing what fixpp sends, are an L-row (FR-024).
- **`385=S` only.** From the peer's point of view, S is what it sends, the only frames this feature resolves. An absent 385 departs from the standard's "required", so a validating session refuses it, `1/385`, and with validation off the entry does not count (data-model E-10). The dictionary cannot carry this: Orchestra's member is optional, so `required='Y'` in `FIXT11.xml` would split L from T. No engine has a behaviour here; leniency would be an owner item plus an L-row.
- **`1410=Y` required.** The standard: the indicator says the version "is the default". An entry without it declares support only.
- **The scan reads what the group declares.** Measured 2026-11-03: `384=0`, `384=-1` and a second block after a non-member all pass the gate and reach Active on a validating session, on the edited `FIXT11.xml` and on L, because `consume_group` returns at a zero count and a member is a valid Logon tag wherever it stands. So the scan takes 384's value and stops at the first non-member, one rule validating or not (data-model E-10).
- **A bad or unserviceable 1130 takes the 1137 block's ending.** Ruling 1, and one rule: data-model E-10 states it. A gate Reject naming 1130 or 372 disconnects through S2, so one input has one ending but for the ruled consume split. Validation off: nothing refuses, FR-011.
- **A cap, because the peer controls the count.** 256 counted entries, 8-byte MsgTypes, in data-model E-10. A counted entry has 385=S and 1410=Y, before de-duplication, because a raw count would refuse a 312-entry advert with no 1410, which passes the gate today (measured), and FIX Latest's closed domain has 181 codes (raw XML), so distinct entries could not reach it on L. The longest vendored MsgType is 2 bytes (raw XML, QuickFIX files and Orchestra). Both limits go in FR-024's L-row. W-24 seeds such Logons.
- **Per connection.** Cleared and rewritten at the record site on every Logon that reaches it, so FR-027 covers it: a reconnect never uses the previous peer's defaults.
- **1406 and 1131 ignored,** as 1156 and 1129 are today (B-row).
- **Reject's 1130, "recommended" by the standard,** is not added: today's Rejects do not carry it for a 1137-resolved message either.

**FR-001 and `version_profile`.** The resolver is `resolve_application_version(profile, value)`, and `version_profile` is a 4-byte, statically asserted struct carried by value, so it cannot hold a map. Without a change `dict::reify` would answer the session default on a Message Type Default frame. **Decision:** when the view's byte is set, `reify` returns it and reads no profile (data-model E-5). That also removes the past-header split between routing and `reify`. Rejected: growing `version_profile`, which every `reify` caller and `negotiated_version_profile()` copy; a `reify` overload taking a map, which leaves C with nothing to pass.

**The dictionary.** `dictionaries/FIXT11.xml` is a verbatim QuickFIX C++ copy with no group on Logon, so a validating session refuses such a Logon on 384. **Owner:** add the group and the three missing fields, nothing else; `dictionaries/README.md`'s "verbatim" gets a stated exception and its refresh recipe re-applies it. Measured against adopting QuickFIX/J's whole file, the group's fields appended last: 51 diff lines in four generated files, against 490 in five. What the edit trips, in tests and goldens, is R-17's.

## R-20 An overlay's header Length+Data pair on every route (FR-012, owner item 7)

**What QuickFIX does.** C++ `extractField` treats a tag as Data when `IsDataField(field, pSessionDD, pAppDD)`, either dictionary. J's `parseHeader` passes the session dictionary to `extractField`. So both honour the pair on a routed frame.

**Decision.** At build, a route table for an S with a header pair is the routed dictionary's table with S's pairs registered in it (data-model E-1, E-3). A route remains one table and one `dict_hooks::for_table_view`.

**Rejected: composing two dictionaries into one `dict_hooks`.** `offset_table.hpp`'s nested cache keys on `dict_hooks::opaque_dict()`, on the condition that `for_table_view` and `none()` are the only producers, a function of the dictionary pointer; and a clone or `reify` re-parses from the owner's table, so it would lose the second dictionary and split differently from the view. Merging keeps fixpp#426's and fixpp#456's guarantees, since the pair is registered before the seal.

**Scope.** A pair with either tag in S's header. A body pair S's overlay adds is not composed: FR-002 says it applies to S's own version (US2 scenario 2), and J's behaviour agrees. C++'s "either dictionary" reaches S's body pairs too, but C++'s S is the transport dictionary, whose bodies are admin.

**Collision.** `set_length_pair_data_tag` displaces: applied last, S's pair wins on that table. So a body pair of the routed dictionary sharing a tag with S's header pair is not honoured on that session. Deterministic, and an operator's own overlay caused it. Disclosed, with a W-3 cell.

**Cost.** One extra table per (S with a header pair, other dictionary), built in `Engine::start()`. Zero on every vendored dictionary. `for_table_view` installs the pair callback on those tables only, because `has_nonstandard_pair()` is then true.
