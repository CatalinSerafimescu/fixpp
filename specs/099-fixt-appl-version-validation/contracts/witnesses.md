# Contract: witnesses and mutants

Issued at Gate A round 1 as the FR × shape × site × mutant matrix. Round 2 ties W-4, W-5 and W-9 to data-model E-7's cell ids and gives each mutant the code it changes and the cell that goes RED on that alone. **A witness with no way to report non-zero is a defect here.**

Unless a row says otherwise, a witness is a real `Session` driven with raw frames. **Shapes** are the spec's T, A, L. **RED-first**: W-1 to W-10 and W-19 run against `c0a828ef`'s behaviour before the code; a cell that is green on that tree says so in its row. E-7's "today" table is that run's expected output for W-4, W-5 and W-9, from research R-18. **Mutants** run from a separate worktree, reused and batched by binary, whole binary, unfiltered, `strings`-checked fresh, and must turn the named cell RED.

Sites are research R-7's. Dispatch sites are D-1 to D-7. Fields: 3115 is FIX Latest only and float-typed, so a valid frame uses `3115=1.5` (measured: `3115=x` draws 373=5, not 373=2). Pick a FIX 4.4-only pair by a recomputed set difference over the vendored XML, in the test.

## Matrix

| Id | Covers | Shapes, role | Site | Cells | Comparand | Mutants |
|---|---|---|---|---|---|---|
| W-1 | FR-001, 002, 003, 005, 006; SC-001; US1 | {T, A, L}; FIX44 S with FIXT11 in the engine; acceptor | scan, gate, D-1, D-2 in `on_inbound_active_`; Logon arms | {Logon, Heartbeat, TestRequest, NOS, NOS 1128=9, 1128=6, 1128=10}; each asserts acceptance, fromApp and fromAdmin counts, Active. **370:** on T with FIX42 and FIX50SP2 registered, `…\|1128=4\|370=…` and `…\|370=…\|1128=4` both delivered; with `1128=9` both 373=2, 371=370 | the spec's "today" table: RED on `c0a828ef` where it marks a cell wrong; literal expected values | M-1, M-4, M-9, M-46, M-58 |
| W-2 | FR-002, 006; SC-002; US1 | T, A | gate | a field only version B defines, for FIX50SP2, FIX Latest, FIX 4.4: Reject when resolved to A, accepted when resolved to B. Spurious-hit arm: a frame on a non-own slot rejected for a field only S's table has, so a route that always accepts cannot pass. **FR-024's limit:** a Logon with `384=1\|372=D\|1130=10`. On T and A: Reject 373=2, 371=384, stays, because `FIXT11.xml` declares no 384 (R-18). On L: established, then a NOS with 3115 and no 1128: Reject 373=2, 371=3115 | the same fault on a twin session whose only version is that one: same 373, 371 (US1 scenario 5) | M-1 |
| W-3 | FR-002, 007, 008, 012; SC-004; US2 | A with overlay; L with overlay; T with overlay | gate, scan, the peek | (a) merged FIX50SP2 S, plain base in engine: field accepted on own version, Required promotion enforced, rejected on another. (b) merged FIX Latest S, no FIX50SP2 registered, resolved to FIX50SP2. (c) T, overlay header scalar on an application message: accepted, routed to another table; closed-enum tag, valid and invalid; int tag, valid and invalid. (d) A, overlay header tag on Logon and Heartbeat through FIXT11: accepted. (e) own-version overlay, header pair whose Data embeds `SOH 1128=10 SOH`, and one with non-tag bytes: selection stays S, Data whole, no fault. **(f) the disclosed limit,** the same pair on a frame routed to another table, Data `SOH 34=7`, NextNumIn 2: ResendRequest, 7=2, NextNumIn kept, no fromApp, because the scan reads 34 as 7. The same with non-tag bytes in the Data: `dispose_unparseable_`'s Reject, 373=0, NextNumIn moved. Both literals are read from source and from research R-6; a run that disagrees stops for a decision. **(g) the trailer limit:** a tag an overlay adds to S's trailer, on a frame routed to another table: 373=2, 371=that tag | (c), (d): the merged dictionary's own answers, research R-6's table. (e): `resolved_application_version()` equals S's own, Data length equals the count. (f): those literals, and (e) as the contrast. (g): that literal | M-2, M-12a, M-12b, M-12c, M-22 |
| W-4 | FR-009, 025, 026; SC-003; US3 | {T, A, L}; acceptor | Active arm: A1, A2, A3 | **Walks E-7's AC row: `AC-a` to `AC-g`, every input of each column, on every shape.** Each asserts exact 373, 371, 372, 45; fromApp and fromAdmin counts; NextNumIn; state. A control frame in the same run delivers, counting 1. **Admin, FIXT11's enum limit:** `Heartbeat 1128=10`: 373=5, 371=1128 on T and A, delivered on L (R-18). **N-7:** on T, `1128=4` with a pair only FIX 5.0 SP2 declares, Data `SOH 34=7`: 45 and NextNumIn are the first 34's, 373=5, 371=1128. The same with non-tag bytes: `dispose_unparseable_`'s fault ending, its literal disclosed. Recompute: 1128 declared in no message body of the vendored files, Orchestra included | E-7's "after" table, literal; its "today" table RED-first | M-3, M-23, M-24, M-25, M-53, M-54, M-55, M-57 |
| W-5 | FR-010, 011, 027; SC-003; US3 | {T, A, L} × {acceptor, initiator} | `on_inbound_not_connected_`, `on_inbound_logon_sent_`: S2, S3, the scan | **Walks E-7's `NC-e` to `NC-i` and `LS-e` to `LS-i`, every input, every shape.** Non-validating twins: as E-7's validation-off paragraph. The double faults and the consume split are asserted as written, so a change to either is RED. **`141=Y` with `1137=4`, validating initiator:** Reject, Disconnected, and `peek_outbound()` stays 2, where the reset unit would make it 1. **Control:** a Logon with `1137=9` and an undeclared 5999: `R 2/5999 A`, stays. **Reconnect**, non-validating initiator: first reply `1137=9`, second with none: `negotiated_version_profile()` still reads as today, and an application frame in Active uses S, not the first connection's table. **Scan:** an engine FIXT11 whose overlay declares a Logon Length+Data pair, S FIX50SP2, the Data embedding `SOH 34=7` and `SOH 141=Y`, each in both arms: NextNumIn is the header's, and no reset. **Interpret:** the same pair, the Data embedding `SOH 1137=6 SOH`, both arms: negotiated stays the real one. Second 1137 in the frame: `interpret_logon`'s reading, as at `c0a828ef` with validation off | E-7, literal. R-18's log for the non-validating twins and the "today" half | M-10, M-27, M-49, M-61, M-62, M-6b, M-6d |
| W-6 | FR-011, 013; clarification; E-5 | {T, A, L} | gate off; D-2 | every W-4 input delivered, with S's view when no dictionary answers; `resolved_application_version()` set when it resolved; admin frame with no transport dictionary uses S; no refusal at `open()` or `register_session`. **`D…A` with Logon fields establishes,** pinned as disclosed. **`off-pair`:** T, a malformed pair only the route's table declares: `dispose_unparseable_`'s Reject, where `c0a828ef` delivered: the one cell not equal to that tree, stated. **E-5's splits:** past-header 1128, empty `1128=`, reconnect with no 1137: the accessor against `dict::reify` | literal endings, from R-18's log for every cell but `off-pair` | M-28, M-29 |
| W-7 | FR-005, 012; US4; "Resend and PossDup" | T for application frames, A for admin frames; validation on and off | **each dispatch site:** D-2, D-4, D-5 for an application frame; D-1, D-3, D-5, D-6, D-7 for an admin frame. Scan in `on_inbound_active_` and `on_inbound_logout_sent_` | application cells, one per site: a custom pair in the body whose Data embeds SOH, `34=7` and `1128=<another version>`, a repeating group, the accessor. Admin cells, one per site, on A with an engine FIXT11 whose overlay declares a pair on that admin message, so S's table splits it wrongly: Heartbeat (D-1), SequenceReset in reset mode (D-3), an out-of-sequence admin frame with `validate_sequence_numbers` off (D-5), a GapFill (D-6), Logout (D-7): the view reads the value whole, `MsgSeqNum` the header's, `resolved_application_version()` `Unknown`. Gate cell: a required field appearing only inside a custom pair's Data draws 373=1. LogoutSent scan: on T, a body pair the route declares, Data `SOH 35=5 SOH`: not taken as the reply | MsgSeqNum and Data bytes against the frame builder's; the group count against the frame; the accessor against the resolved slot | M-5a, M-5b, M-5c, M-5d1 to M-5d5, M-6a, M-6c, M-13, M-17 |
| W-8 | FR-003, 004; US3 scenario 5 | validating: T, L, A with and without FIXT11, FIX44 with and without, partial FIXT11 (admin message removed), FIXT11 whose Logon lacks 1137, `fix44_dictionary_with_1137()`; non-validating twins | both callers: direct `open()`, `Engine::register_session` | refused or opens; state `never_opened` after a direct refusal; no registry entry after a registration refusal; a refused registration on an Engine leaves other entries running. **One clause each, at each caller that can reach it:** `begin_string` FIXT.1.1 with no default, at both; slot miss for the default, at both; null registry, at direct `open()` only. **Log line:** a non-validating bad config on an Engine, with a logger set: the role loop's record, its text from `format_registry.cpp` | literal matrix | M-7a to M-7d, M-14, M-29, M-30a, M-30b, M-56 |
| W-9 | FR-005, 029; R-8; US3 scenario 8 | T, A, L; both establishment arms; LogoutSent | S1, `on_inbound_logout_sent_` | **Walks E-7's `NC-a` to `NC-d`, `LS-a` to `LS-d`, `LO-a` to `LO-g`, every input, every shape.** Named cells: `D…A` and `0…A` with Logon fields: Disconnected, not Active. `D…5` in LogoutSent: confirms the Logout, as `c0a828ef` | E-7, literal; R-18's log for the validation-off twin, which has the same ending but for those two frames | M-15, M-50, M-51 |
| W-10 | FR-001, 014; E-5 | {T, A, L}; FIX.4.4 session | D-2 | C++ and C: 1128 present, negotiated, admin, non-FIXT, unresolved. A clone. A clone of a clone. A `dict::reify` handle's `view()` | on every delivered cell of W-1, `dict::reify(...).version().application`, an independent computation | M-11, M-31, M-32 |
| W-11 | FR-015 | any FIXT | send path | header 1128 on an application-supplied frame reaches the wire before the first body field. **The L-row's limit:** a `toApp` view on T reads no application group | the bytes the peer receives. Green on `c0a828ef`, stated: `kSendHeaderTags` already holds 1128, so only the mutant can show it fails | M-33 |
| W-12 | FR-014, 016; NFR-004; SC-006; US5 | T, A from C | `capi_099_fixt_live_test`: real TCP peer | setters; add-dictionary; Active; a routed Reject and an accept; `fixpp_session_open` returning `FIXPP_ERR_THREAD_CONFIG` with the builder unconsumed; `fixpp_msg_get_resolved_appl_ver_id` in the callback and on a clone. **The getter's negative cells, as `fixpp_msg_get_msg_type`:** null `value_out`, null `len_out`, null message: `FIXPP_ERR_NULL_HANDLE`; destroyed handle, wrong-type handle, outbound message: `FIXPP_ERR_INVALID_HANDLE`; unresolved: `FIXPP_ERR_TAG_NOT_FOUND`. **The freeze:** `bash tools/check_capi_freeze.sh` **fails after the header edits and before the manifest refresh**, with the log kept, and passes after it. `tools/check_capi_reentrancy.sh` passes | `version_test` 1.14; `tests/abi/golden/fixpp_capi_symbols.txt`; literal error codes | M-34, M-35, M-36, M-60 |
| W-13 | FR-014, 016; SC-006; US5 | T from Python | `bindings/python/tests/test_099_*`, raw TCP peer as 098's `_Peer` | the W-12 arms, each calling its new function by name; `session_open` raising `fixpp.SessionError` with its `.name`. A dropped SWIG function fails here and in `EXPECTED_PUBLIC_SURFACE` | `EXPECTED_PUBLIC_SURFACE`; literal exception class and name | M-34, M-36 |
| W-14 | FR-017, 018; SC-006; US5; Gate A Codex 8 | A, T from TOML | `tests/config/test_099_*`, then a real Engine | `extra_paths` reaches the engine in order, front is `path`, session validates; bad path, non-array, non-string, each a diagnostic at the key; absent key: bundle identical. **Overlay cell:** `dictionary.path`, `extra_paths` and a session or default `dialect_overlay`: the session's own-version overlay survives and a non-own extra stays plain, so the overlay field is rejected on a frame routed to the extra. Refusal arm: a validating FIXT TOML with no transport dictionary loads, and `register_session` returns `invalid_session_config` | a bundle dump from the same file without the key; L-098-1's rule, literal | M-38, M-39, M-40 |
| W-15 | NFR-003; SC-007 | an Engine: N sessions on M distinct dictionaries, two of them sharing one overlay S, plus K non-FIXT sessions in a separate pass | `Engine::start`, `open()` | FIXT pass: `as_table_view_call_count` delta equals M after `start()` and is still M after every open. Non-FIXT pass in the same process, for the heap comparand: its delta is K, one per open, which is why the passes are separate. Heap counted around the opens: an added FIXT session adds, beyond what a non-FIXT session with a minimal dictionary adds, less than one copy of the smallest application dictionary it routes to. TSan arm, `worker_threads` above one: every session sees the same entry per slot, no race. All-FIX.4.4 engine: no set. C route, the limit SC-007 carves: the delta is M plus one per C session; heap is reported, not asserted | M, computed in the test from the distinct `Dictionary*`; the non-FIXT twin's delta and the copy's bytes, measured in the same test | M-8, M-8b, M-41 |
| W-16 | NFR-002 | T, validated and not, on a non-own slot | a real `Session`, D-2 | modelled on `tests/perf/test_session_recovery_alloc_guard.cpp`; zero allocations in the marked region under the interceptor | zero, with the interceptor's own write evidence, as that file requires | M-21 |
| W-17 | FR-007; NFR-001; SC-005; Gate A Codex 6 | FIX.4.4; every vendored dictionary | `as_table_view`, both forms | FIX.4.4 strict validation still rejects 1128. Existing suite unmodified on non-FIXT sessions. For every vendored file, every store `table_view_test_access` reads is equal between `as_table_view()` and `as_table_view(fixt_framing)`, except the two framing stores. Those are empty for `vt11` and FIX Latest in both forms. The no-argument form hashed over a canonical dump of those same stores, against a literal recorded by running the recipe at `c0a828ef` | `as_table_view()` from the same `Dictionary`; the literal | M-42, M-43, M-48 |
| W-18 | NFR-005 | see below | live | see below | see below | see below |
| W-19 | FR-005, 012 | {T, A, L} | every scan site | garbled frame, malformed tag, Length/Data mismatch, third field not 35: the same ending, counters and sequence state as today; a frame the peek cannot read is among them. No input here carries a pair only a route's table declares: that is W-6's `off-pair` | **per shape:** faults and garbles are disposed of before the gate, so T, A and L can each be baselined at `c0a828ef` with validation off. A cannot establish there with validation on. Literal values, the log in the verify record | M-16 |
| W-20 | FR-028; E-8 | own executable, under `[const §VII.8]`; FIXT11 plus a minimal application dictionary, because a real one's copy is hundreds of thousands of allocations (research R-3) and a sweep is quadratic | `fixt_route_set::build`, `Engine::start`, `open()`, `fixpp_engine_create` | **A fail-at-N global `operator new`**, armed around the call, not `pugi::set_memory_management_functions`: `as_table_view()` reads a finished handle and pugixml never runs, so that hook would never fire. **Positive control, in each of the four arms:** the hook's fired count is non-zero and the sweep saw both outcomes, or the arm fails, because an unfired hook reads green. **Build arm:** for each N from 1 until a call completes with the hook unfired: `out_of_memory` for each fired N, and a usable set on the last. **Engine arm:** N up to the build's own count, measured in the build arm: `start()` returns `out_of_memory`, no loop runs, and a second `start()` succeeds. **Direct arm:** `open()` returns it with `never_opened`, and a retry opens. **C arm:** a builder with two dictionaries, sweeping `fixpp_engine_create` likewise: each fired N returns `FIXPP_ERR_CAPI_CONFIG_INVALID`, and a retry on the same builder opens a FIXT session, so both dictionaries are still in it. A version-mismatch failure returns before the copy and could not tell a copy from a move. The failure is never read as a miss | literal error; the role loop's counter stays zero | M-44, M-52 |
| W-21 | FR-012; Gate A root cause 6 | unit | the peek, `is_fixt_standard_header_tag`, `is_fixt_admin_msgtype` | the standard set against `FIXT11.xml`'s `<header>` by an independent raw-XML walk, as `fixt_header_merge_test.cpp` does: exact, both ways, with 370 as the one named exception. The same walk over the vendored FIX 4.x files: their union minus `FIXT11.xml`'s is exactly {370}, so a new such tag fails the test. `A` is admin, `n` is application. A body-less application frame with 93, 89, then 1128: the peek stops at 93. FIX44 S: the peek reads header 1128. FIX Latest's header equals `FIXT11.xml`'s | the raw XML; `dict::reify`'s dispatch on the same MsgTypes | M-45, M-46, M-22, M-58 |
| W-22 | NFR-001; `[const §VIII.3]` | FIX.4.4; FIXT for information | `on_inbound_frame_bench`, `validator_bench` | paired, B41's method | the merge base, `[const §VIII.2]` | M-47 |
| W-23 | FR-019 to FR-024; research R-3, R-16 | records | `spec/`, `brain/`, comments | every condition of research R-16, by recipe: a row reads open only if in the live B&L file; `coverage-index.md` §5.3.4 no longer maps to S-026; `value_or(engine_anchor)` gone from the dictionary axis of the two named comments; each B-row and L-row quickstart.md lists present in the live B&L file (FR-022); each brain page FR-023 names mentions 099; "does NOT block" and "non-blocking" gone from `engine.hpp`'s `start()` comments; `fixpp.i`'s `engine_start` row no longer says "returns immediately" | each grep run first on the pre-edit tree, where it must report the old state | a restored row or phrase: the recipe reports it |
| W-24 | `[const §VII.7]`; FR-012 | ASan, UBSan, libFuzzer | `peek_fixt_route`, the FIXT arms, the validator's envelope branch | **`fuzz_session_recovery_admin_parse`, a FIXT arm** selected by a preamble bit: a validating session over a route set on FIXT11 and a small application dictionary, fed `on_inbound_frame`. No crash. On a fault-free frame, the peek's MsgType equals the scan's `fault_ref_msg_type`, or the harness traps. **`fuzz_wire_validator`, an envelope arm:** the same input validated with an envelope table. **Seeds:** research R-9's and R-18's inputs, in `tests/fuzz/corpus/session_recovery_admin_parse/` and `wire_validator/`, replayed by `ctest -L fuzz`. **Positive control:** replaying those seeds, a counter shows the FIXT arm reached the peek and a seeded Logon reached Active; zero fails the run, so the arm cannot be silently bypassed. Ten minutes per target at verify, `--fuzz-duration=600` | the trap; the counter | M-59 |

## W-18, a conditional row (research R-13)

The owner's ruling selects one row; both run from `HP-QFcpp-init-fixt11-fix50sp2-app-bn` and `HP-QFj-init-fixt11-fix50sp2-app-bn`, shape T, `validate_inbound_messages = true`, hello arm `validation-on`. Either way, after 5 s not Active, or a peer `OUT 3` or `OUT j`, is a non-zero exit, and `session_test_access::has_validator` true is asserted. An unrunnable cell is a blocker reported to the owner, per NFR-005, not a skip.

| Option | Direction and frame | Assertion | Goes RED when | What it cannot show |
|---|---|---|---|---|
| **A**, arm flip | peer to fixpp: Logon only | Active; fixpp sends no Reject; comparison row as today | M-9 (a Logon routed to an application table). On `c0a828ef`, a T Logon passes: **not RED there** | anything about selection |
| **B**, one peer application frame | peer to fixpp: one 35=BN, a new `peer-to-fixpp` step in `fixt_app_script.yaml`, no 1128, resolved by the negotiated 1137=9, fields valid for FIX50SP2 | fromApp count 1; `resolved_application_version()` is `v50sp2`; the peer's transcript has no `IN 3`; a readback comparison row per field | on `c0a828ef`: Reject 373=2, 371=8. M-1. M-5a | QuickFIX/J's script path is unverified, and a counterparty republish may be needed (research R-13) |

## Mutants

Each row: the code it changes, and the cell that goes RED on that change alone. Where E-7 names the cell, the id is E-7's. A mutant that needs a shape states it.

| Id | Code it changes | Cell RED on that alone |
|---|---|---|
| M-1 | selection (E-7): returns `session_route_` on every frame. SC-001 names this mutant | W-1, T, NOS: Reject 373=2, 371=8 |
| M-2 | E-6's slot rule: the registry's dictionary answers before the own-version test | W-3 (a), Required promotion not enforced |
| M-3 | A2 removed: a refusal falls through to `session_route_` | `AC-b`, `1128=4` on A and L, where S passes: delivered. T's `1128=99` draws 2/8 instead. A and L's S also answer 5/1128 on `1128=99`, so that input alone cannot kill it there |
| M-4 | `fixt_route_set::build` calls plain `as_table_view()` | W-1, FIX44 S, `1128=6`: rejected on tag 8 |
| M-5a, 5b, 5c | `parse_and_dispatch_`'s owner at D-2; D-4; D-5 stays `inbound_tv_` | W-7's application cell at that site: Data split |
| M-5d1 to M-5d5 | the same at D-1; D-3; D-5 for admin; D-6; D-7 | W-7's admin cell at that site, on A: S's table splits the pair. T cannot kill them |
| M-6a | `on_inbound_active_`'s scan keeps `inbound_tv_`'s hooks | W-7, application cell: `MsgSeqNum` 7 |
| M-6b | the scan in `on_inbound_not_connected_`; in `on_inbound_logon_sent_`, one edit per run, keeps S's hooks | W-5, Scan cell, that arm: NextNumIn from `34=7`, or a reset from `141=Y` |
| M-6c | `on_inbound_logout_sent_`'s scan keeps `inbound_tv_`'s hooks | W-7, LogoutSent scan: taken as the reply |
| M-6d | `interpret_logon`'s hooks at the acceptor; at the initiator, one edit per run | W-5, Interpret cell, that arm: negotiated reads 6 |
| M-7a | E-9: the no-default clause removed | W-8, no default, at each caller: opens or registers |
| M-7b | E-9: the null-registry clause removed | W-8, null registry, direct `open()` |
| M-7c | E-9: the slot-miss clause removed | W-8, slot miss, at each caller |
| M-7d | E-9: the transport clause removed | W-8, A with no FIXT11 |
| M-8 | a role loop does not call `adopt_routes` | W-15, walks above M after the opens |
| M-8b | a session copies the validators it reads | W-15, heap arm RED, walks unchanged: why it needs both |
| M-9 | selection: admin takes an application slot | W-1, A, Heartbeat rejected |
| M-10 | S3: the initiator's block removed | `LS-e`, `4`: Active |
| M-11 | `parse_and_dispatch_` does not write the accessor | W-10, 1128 present: `Unknown` |
| M-12a, 12b, 12c | `validate(…, envelope)`: membership; enum; type answered by the routed table. Each alone (Gate A Codex 7) | W-3 (c)'s scalar; closed-enum invalid; int invalid |
| M-13 | the peek ignores its stop rule | W-7, `1128=<other>` inside a body pair's Data: routes away |
| M-14 | `defines_fixt_admin` tests the version label | W-8, partial FIXT11: opens |
| M-15 | S1's `application` clause removed | `NC-d`, T, NOS: Reject 2/8, stays |
| M-16 | selection: a peek failure refuses | W-19, a frame the peek cannot read: not today's ending |
| M-17 | `validate_inbound_`'s parser keeps `inbound_tv_`: the #426 hazard | W-7, gate cell: `msg.get` finds the field in the Data |
| M-21 | the peek allocates: a `std::string` of 256 bytes, past every small-string buffer, so it reaches `operator new` | W-16: non-zero |
| M-22 | the peek walks with `dict_hooks::none()` (Gate A Codex 1) | W-3 (e), non-tag bytes: unreadable |
| M-23 | A3's probe removed | `AC-c`, `1128=6`: delivered |
| M-24 | A1 removed | `AC-a`, `0…D`: delivered |
| M-25 | A3's probe runs after the validator | `AC-c`, `1128=abc`: 373=5 |
| M-27 | selection reads `negotiated_appl_version_` | W-5, Reconnect: the first connection's table |
| M-28 | a refusal is acted on with validation off | W-6, `1128=99`: Reject |
| M-29 | E-9's transport clause also runs without validation | W-8, non-validating A with no FIXT11: refused |
| M-30a | `register_session` does not call the check | W-8, A with no FIXT11, at registration |
| M-30b | `register_session` gates on `cfg.is_fixt()` | W-8, no default, at registration: registers |
| M-31 | `fixpp_msg_clone` does not copy the byte | W-10, a clone |
| M-32 | `owning_message_handle_from_frame` does not copy the byte | W-10, `dict::reify`'s `view()` |
| M-33 | 1128 removed from `kSendHeaderTags` | W-11 |
| M-34 | `add_dictionary` stores nothing | W-12, W-13: `fixpp_session_open` refused on a slot miss |
| M-35 | the validation setter is a no-op | W-12: `1128=99` delivered, no Reject |
| M-36 | `fixpp_session_open` returns `FIXPP_ERR_OK` on a refused registration | W-12, W-13, the refusal cell |
| M-38 | an extra overwrites `dictionaries.front()` | W-14, front is `path` |
| M-39 | the overlay merges into every extra | W-14, overlay cell |
| M-40 | extras pushed before `path` | W-14, order |
| M-41 | `Engine::start` does not build the set | W-15: delta 0 after `start()`, N×M after the opens |
| M-42 | the overload merges framing tags into `valid_` (Gate A Codex 6) | W-17, store equality |
| M-43 | plain `as_table_view()` gains the FIX 4.x surface | W-17: a non-FIXT session accepts 1128 |
| M-44 | `build` maps a failure to a miss | W-20, build arm: a route with a hole, and no `out_of_memory` |
| M-45 | the standard set keeps 10, 89, 93 (round 1 N-13) | W-21, 93 then 1128: peek reads on |
| M-46 | admin is `detail::is_admin_msgtype` (round 1 N-14) | W-21, `A`; W-1, a Logon routed as application |
| M-47 | a seeded delay on the non-FIXT path | W-22: proves the paired bench reports |
| M-48 | the overload populates framing for FIX Latest | W-17: answers change through `field_type_of_with_framing` |
| M-49 | S2: a gate Reject naming 1137 does not disconnect | `NC-e`, `LS-e`, absent: stays |
| M-50 | S1 reduced to `!is_fixt_admin_msgtype(hdr.msg_type)`, the last MsgType, with no mismatch clause | `NC-a`, `D…A` with Logon fields: a gate Reject, stays, not Disconnected |
| M-51 | S1's mismatch clause removed | `NC-a`, `0…A` with Logon fields: Reject 2/98, stays |
| M-52 | `fixpp_engine_create` moves the builder's list | W-20, C arm: the retry's session refused on a slot miss |
| M-53 | A1's Reject takes 372 from `hdr.msg_type` | `AC-a`, `D…4`: 372=4 |
| M-54 | the consume is keyed on `hdr.msg_type` | `AC-a`, `D…4`: kept; `4…D`: consumed |
| M-55 | A2's Reject takes 45 from `hdr.msg_seq_num` | W-4, N-7's cell: 45=7 |
| M-56 | the role loop's format string left out of `format_registry.cpp` | W-8, Log line: the record's text |
| M-57 | the mismatch's cold path answers 14 without re-walking | `AC-a`, `0,D`: 373=14 |
| M-58 | 370 left out of the standard set | W-1, 370 before 1128: 373=14 |
| M-59 | the peek takes MsgType from the last 35 | W-24, the harness's trap, on `D…0` in the seeds |
| M-60 | the getter skips `check_inbound_msg` | W-12, outbound message: not `FIXPP_ERR_INVALID_HANDLE` |
| M-61 | S3's initiator block moved to today's record site, behind the reset unit | W-5, `141=Y` with `1137=4`: `peek_outbound()` moves |
| M-62 | S2 disconnects on any gate Reject of a Logon | W-5, the control: Disconnected, not stays |

Unused ids, none reused: M-12 (folded into M-12a to M-12c), M-18 to M-20, M-26 (it mutated the pre-gate, which Option 3 removes; under Option 1 it returns), M-37 (a dropped SWIG function is W-13's surface check).

**One half of a requirement with no mutant.** FR-027's "in an establishment arm it MUST NOT read that value" cannot go RED: S1 disconnects every application frame there and a Logon uses the transport route, so no ending depends on the value. It is a design statement, checked at review.

## Coverage, walked 2026-11-03

| Requirement | Witness | Requirement | Witness |
|---|---|---|---|
| FR-001 | W-1, W-10 | FR-015 | W-11 |
| FR-002 | W-1, W-2, W-3 | FR-016 | W-12, W-13 |
| FR-003 | W-1, W-8 | FR-017 | W-14 |
| FR-004 | W-8, W-12, W-13, W-14 | FR-018 | W-14 |
| FR-005 | W-1, W-7, W-9, W-19 | FR-019 to FR-024 | W-23 |
| FR-006 | W-1, W-2 | FR-025, FR-026 | W-4 |
| FR-007 | W-1 (FIX44), W-3, W-17 | FR-027 | W-5 |
| FR-008 | W-3 | FR-028 | W-20 |
| FR-009 | W-4 | FR-029 | W-9 |
| FR-010 | W-5 | NFR-001 | W-17, W-22 |
| FR-011 | W-5, W-6 | NFR-002 | W-16 |
| FR-012 | W-1, W-3, W-7, W-19, W-21, W-24 | NFR-003 | W-15 |
| FR-013 | W-6 | NFR-004 | W-12 |
| FR-014 | W-10, W-12, W-13 | NFR-005 | W-18 |
| | | SC-001 to SC-007 | W-1; W-2; W-4, W-5, W-9; W-3; W-17; W-12 to W-14; W-15 |

Sites: frame scans W-5 (two establishment arms), W-7 (Active, LogoutSent), W-19 (all four). Gates W-4 (Active), W-5, W-9 (establishment). Interpret W-5. Dispatch W-7, one cell per site. E-7's cells: AC W-4; NC and LS `a`–`d` W-9, `e`–`i` W-5; LO W-9.
