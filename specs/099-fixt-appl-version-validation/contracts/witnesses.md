# Contract: witnesses and mutants

Re-issued at Gate A round 1 as the FR × shape × site × mutant matrix. **Every row names its mutant and its comparand.** A witness with no way to report non-zero is a defect here.

Unless a row says otherwise, a witness is a real `Session` driven with raw frames. **Shapes** are the spec's T, A, L. **RED-first**: W-1 to W-10 and W-19 run against `c0a828ef`'s behaviour before the code; a cell that is green on that tree says so in its row. **Mutants** run from a separate worktree, whole binary, unfiltered, `strings`-checked fresh, and must turn the named witness RED.

Sites are research R-7's. Dispatch sites are D-1 to D-7. Fields: 3115 is FIX Latest only and float-typed, so a valid frame uses `3115=1.5` (measured: `3115=x` draws 373=5, not 373=2). Pick a FIX 4.4-only pair by a recomputed set difference over the vendored XML, in the test.

## Matrix

| Id | Covers | Shapes, role | Site | Cells | Comparand | Mutants |
|---|---|---|---|---|---|---|
| W-1 | FR-001, 002, 003, 005, 006; SC-001; US1 | {T, A, L}; FIX44 S with FIXT11 in the engine; acceptor | scan, gate, D-1, D-2 in `on_inbound_active_`; Logon arms | {Logon, Heartbeat, TestRequest, NOS, NOS 1128=9, 1128=6, 1128=10}; each asserts acceptance, fromApp and fromAdmin counts, Active | the spec's "today" table: RED on `c0a828ef` where it marks a cell wrong; literal expected values | M-1, M-4, M-9, M-46 |
| W-2 | FR-002, 006; SC-002; US1 | T, A | gate | a field only version B defines, for FIX50SP2, FIX Latest, FIX 4.4: Reject when resolved to A, accepted when resolved to B. Spurious-hit arm: a frame on a non-own slot rejected for a field only S's table has, so a route that always accepts cannot pass | the same fault on a twin session whose only version is that one: same 373, 371 (US1 scenario 5) | M-1, M-4 |
| W-3 | FR-002, 007, 008, 012; SC-004; US2 | A with overlay; L with overlay; T with overlay | gate, scan, the peek | (a) merged FIX50SP2 S, plain base in engine: field accepted on own version, Required promotion enforced, rejected on another. (b) merged FIX Latest S, no FIX50SP2 registered, resolved to FIX50SP2. (c) T, overlay header scalar on an application message: accepted, routed to another table; closed-enum tag, valid and invalid; int tag, valid and invalid. (d) A, overlay header tag on Logon and Heartbeat through FIXT11: accepted. (e) own-version overlay, header pair whose Data embeds `SOH 1128=10 SOH`, and one with non-tag bytes: selection stays S, Data whole, no fault | (c), (d): the merged dictionary's own answers, research R-6's table. (e): `resolved_application_version()` equals S's own, Data length equals the count | M-2, M-12, M-12a, M-12b, M-12c, M-22 |
| W-4 | FR-009, 025, 026; SC-003; US3 | {T, A, L}; acceptor | Active arm's gate and pre-gate | every application row of research R-9, including the delivered controls. Each asserts exact 373 and 371, 372 the third field's value, fromApp count 0, a control frame in the same run counting 1, NextNumIn moved as a validation Reject, Active after. Recompute: 1128 declared in no message body of the vendored files, Orchestra included | research R-9's table, literal | M-3, M-23, M-24, M-25 |
| W-5 | FR-010, 011, 027; SC-003; US3 | {T, A, L} × {acceptor, initiator} | `on_inbound_not_connected_`, `on_inbound_logon_sent_`: scan, pre-gate, gate, interpret | 1137 absent, empty, `99`, `abc`, `4`: same ending in every cell. Non-validating twins: as today. Initiator reconnect, non-validating: first reply `1137=9`, second with none: `negotiated_version_profile()` still reads as today, and an application frame uses S, not the first connection's table. Interpret cell: an engine FIXT11 whose overlay declares a Logon Length+Data pair, S FIX50SP2, the Data embedding `SOH 1137=6 SOH`: negotiated stays the real one. Second 1137 in the frame: the disposition and `interpret_logon` read the same one | research R-9's Logon table; today's cells in `test_fixt_logon_establishment.cpp` for the non-validating twins | M-10, M-26, M-27, M-6d |
| W-6 | FR-011, 013; clarification | {T, A, L} | gate off; D-2 | every W-4 input delivered, with S's view when no dictionary answers; `resolved_application_version()` set when it resolved; admin frame with no transport dictionary uses S; no refusal at `open()` or `register_session` | literal endings, each confirmed by running the same frames at `c0a828ef` with validation off, the log in the verify record | M-28, M-29 |
| W-7 | FR-005, 012; US4; "Resend and PossDup" | T for application frames, A for admin frames; validation on and off | **each dispatch site:** D-2, D-4, D-5 for an application frame; D-1, D-3, D-5, D-6, D-7 for an admin frame. Scan in `on_inbound_logout_sent_` | application cells: a custom pair in the body whose Data embeds SOH, `34=7` and `1128=<another version>`, a repeating group, the accessor. Admin cells, A with an engine FIXT11 whose overlay declares a pair on each of those admin messages, so S's table splits it wrongly: the view reads the value whole, `MsgSeqNum` the header's, `resolved_application_version()` `Unknown`. Gate cell: a required field appearing only inside a custom pair's Data draws 373=1 | MsgSeqNum and Data bytes against the frame builder's; the group count against the frame; the accessor against the resolved slot | M-5a, M-5b, M-5c, M-5d, M-6a, M-6c, M-13, M-17 |
| W-8 | FR-003, 004; US3 scenario 5 | validating: T, L, A with and without FIXT11, FIX44 with and without, partial FIXT11 (admin message removed), FIXT11 whose Logon lacks 1137, `fix44_dictionary_with_1137()`; non-validating twins | both callers: direct `open()`, `Engine::register_session` | refused or opens; state `never_opened` after a direct refusal; no registry entry after a registration refusal; a refused registration on an Engine leaves other entries running | literal matrix | M-7, M-14, M-30 |
| W-9 | FR-005; R-8 | T, A, L; both establishment arms | `on_inbound_not_connected_`, `on_inbound_logon_sent_` | application frame, with and without 1128, valid and invalid: one ending, Disconnected, no Reject | the validation-off ending today, which is the same frame's literal | M-15 |
| W-10 | FR-001, 014; E-5 | {T, A, L}; FIX.4.4 session | D-2 | C++ and C: 1128 present, negotiated, admin, non-FIXT, unresolved. A clone. A clone of a clone. A `dict::reify` handle's `view()` | on every delivered cell of W-1, `dict::reify(...).version().application`, an independent computation | M-11, M-31, M-32 |
| W-11 | FR-015 | any FIXT | send path | header 1128 on an application-supplied frame reaches the wire before the first body field | the bytes the peer receives. Green on `c0a828ef`, stated: `kSendHeaderTags` already holds 1128, so only the mutant can show it fails | M-33 |
| W-12 | FR-014, 016; NFR-004; SC-006; US5 | T, A from C | `capi_099_fixt_live_test`: real TCP peer | setters; add-dictionary; Active; a routed Reject and an accept; the handle errors of `contracts/surface.md`; a failed `fixpp_engine_create` leaves the list usable; `fixpp_session_open` returning `FIXPP_ERR_THREAD_CONFIG` with the builder unconsumed; `fixpp_msg_get_resolved_appl_ver_id` in the callback and on a clone | `version_test` 1.14; `tests/abi/golden/fixpp_capi_symbols.txt`; literal error codes | M-34, M-35, M-36 |
| W-13 | FR-014, 016; SC-006; US5 | T from Python | `bindings/python/tests/test_099_*`, raw TCP peer as 098's `_Peer` | the W-12 arms; `session_open` raising `fixpp.SessionError` with its `.name` | `EXPECTED_PUBLIC_SURFACE`; literal exception class and name | M-34, M-36, M-37 |
| W-14 | FR-017, 018; SC-006; US5; Gate A Codex 8 | A, T from TOML | `tests/config/test_099_*`, then a real Engine | `extra_paths` reaches the engine in order, front is `path`, session validates; bad path, non-array, non-string, each a diagnostic at the key; absent key: bundle identical. **Overlay cell:** `dictionary.path`, `extra_paths` and a session or default `dialect_overlay`: the session's own-version overlay survives and a non-own extra stays plain, so the overlay field is rejected on a frame routed to the extra. Refusal arm: a validating FIXT TOML with no transport dictionary loads, and `register_session` returns `invalid_session_config` | a bundle dump from the same file without the key; L-098-1's rule, literal | M-38, M-39, M-40 |
| W-15 | NFR-003; SC-007 | an Engine: N sessions on M distinct dictionaries, two of them sharing one overlay S | `Engine::start`, `open()` | `as_table_view_call_count` delta equals M after `start()` and is still M after every open. Heap counted around the opens: an added session adds, beyond what a non-FIXT session with a minimal dictionary adds in the same run, less than one copy of the smallest application dictionary it routes to. TSan arm, `worker_threads` above one: every session sees the same entry per slot, no race. All-FIX.4.4 engine: no set. C route: the snapshot's extra walk per session is reported, not asserted | M, computed in the test from the distinct `Dictionary*`; the non-FIXT twin's delta and the copy's bytes, measured in the same test | M-8, M-8b, M-41 |
| W-16 | NFR-002 | T, validated and not, on a non-own slot | a real `Session`, D-2 | modelled on `tests/perf/test_session_recovery_alloc_guard.cpp`; zero allocations in the marked region under the interceptor | zero, with the interceptor's own write evidence, as that file requires | M-21 |
| W-17 | FR-007; NFR-001; SC-005; Gate A Codex 6 | FIX.4.4; every vendored dictionary | `as_table_view`, both forms | FIX.4.4 strict validation still rejects 1128. Existing suite unmodified on non-FIXT sessions. For every vendored file, every store `table_view_test_access` reads is equal between `as_table_view()` and `as_table_view(fixt_framing)`, except the two framing stores. Those are empty for `vt11` and FIX Latest in both forms. The no-argument form hashed over a canonical dump of those same stores, against a literal recorded by running the recipe at `c0a828ef` | `as_table_view()` from the same `Dictionary`; the literal | M-42, M-43, M-48 |
| W-18 | NFR-005 | see below | live | see below | see below | see below |
| W-19 | FR-005, 012 | {T, A, L} | every scan site | garbled frame, malformed tag, Length/Data mismatch, third field not 35: the same ending, counters and sequence state as today; a frame the peek cannot read is among them | **per shape:** faults and garbles are disposed of before the gate, so T, A and L can each be baselined at `c0a828ef` with validation off. A cannot establish there with validation on. Literal values, the log in the verify record | M-16 |
| W-20 | FR-028 | Engine and direct | `Engine::start`, `open()` | a failing allocator at each allocation of the build, as `overlay_pugi_oom_test.cpp` does: `start()` returns `out_of_memory` and no loop runs; direct `open()` returns it with `never_opened`, and a retry opens. The failure is never read as a miss | literal error; the role loop's counter stays zero | M-44 |
| W-21 | FR-012; Gate A root cause 6 | unit | the peek, `is_fixt_standard_header_tag`, `is_fixt_admin_msgtype` | the standard set against `FIXT11.xml`'s `<header>` by an independent raw-XML walk, as `fixt_header_merge_test.cpp` does: exact, both ways. `A` is admin, `n` is application. A body-less application frame with 93, 89, then 1128: the peek stops at 93. FIX44 S: the peek reads header 1128 | the raw XML; `dict::reify`'s dispatch on the same MsgTypes | M-45, M-46, M-22 |
| W-22 | NFR-001; `[const §VIII.3]` | FIX.4.4; FIXT for information | `on_inbound_frame_bench`, `validator_bench` | paired, B41's method | the merge base, `[const §VIII.2]` | M-47 |
| W-23 | FR-019 to FR-024; research R-16 | records | `spec/`, `brain/`, comments | every condition of research R-16, by recipe: a row reads open only if in the live B&L file; `coverage-index.md` §5.3.4 no longer maps to S-026; `value_or(engine_anchor)` gone from the dictionary axis of the two named comments; each B-row and L-row quickstart.md lists present in the live B&L file (FR-022); each brain page FR-023 names mentions 099 | each grep run first on the pre-edit tree, where it must report the old state | a restored row or phrase: the recipe reports it |

## W-18, a conditional row (research R-13)

The owner's ruling selects one row; both run from `HP-QFcpp-init-fixt11-fix50sp2-app-bn` and `HP-QFj-init-fixt11-fix50sp2-app-bn`, shape T, `validate_inbound_messages = true`, hello arm `validation-on`. Either way, after 5 s not Active, or a peer `OUT 3` or `OUT j`, is a non-zero exit, and `session_test_access::has_validator` true is asserted. An unrunnable cell is a blocker reported to the owner, per NFR-005, not a skip.

| Option | Direction and frame | Assertion | Goes RED when | What it cannot show |
|---|---|---|---|---|
| **A**, arm flip | peer to fixpp: Logon only | Active; fixpp sends no Reject; comparison row as today | M-9 (a Logon routed to an application table). On `c0a828ef`, a T Logon passes: **not RED there** | anything about selection |
| **B**, one peer application frame | peer to fixpp: one 35=BN, a new `peer-to-fixpp` step in `fixt_app_script.yaml`, no 1128, resolved by the negotiated 1137=9, fields valid for FIX50SP2 | fromApp count 1; `resolved_application_version()` is `v50sp2`; the peer's transcript has no `IN 3`; a readback comparison row per field | on `c0a828ef`: Reject 373=2, 371=8. M-1. M-5a | QuickFIX/J's script path is unverified, and a counterparty republish may be needed (research R-13) |

## Mutants

| Id | Mutation | Why it is listed |
|---|---|---|
| M-1 | selection returns `session_route_` | the old behaviour |
| M-2 | the registry's dictionary answers before the own-version rule | loses the overlay |
| M-3 | a refusal falls through to S | kills only where S answers differently. On `1128=99`, A and L's S also answer 5/1128: **only T's cell kills it there**. `1128=4` kills it on A and L, where S passes |
| M-4 | the route set builds with plain `as_table_view()` | FIX44's slot rejects 8 |
| M-5a, 5b, 5c | D-2; D-4; D-5 keep `inbound_tv_` | one per application-delivering site (N-9) |
| M-5d | D-1, D-3, D-5, D-6, D-7 keep `inbound_tv_` on an admin frame | killed on A, where S's table differs from the transport's; T cannot kill it |
| M-6a, 6c, 6d | the scan keeps `inbound_tv_`'s hooks in `on_inbound_active_`; `on_inbound_logout_sent_`; `interpret_logon` keeps S's hooks | |
| M-7 | the transport guard removed from the check | |
| M-8 | a session builds its own set though the Engine handed it one | walks RED |
| M-8b | a session copies the validators it reads | heap RED, walks unchanged: why W-15 needs both arms |
| M-9 | admin frames use an application slot | |
| M-10 | the initiator's disposition removed | |
| M-11 | accessor never written | |
| M-12 | envelope dropped | |
| M-12a, 12b, 12c | envelope membership; enum; type answered by the routed table | Gate A Codex 7: each alone |
| M-13 | the peek reads past the header | killed by a `1128=` inside a body pair's Data, which W-7's cells carry |
| M-14 | `defines_fixt_admin` checks the version label | |
| M-15 | establishment arms validate application frames | |
| M-16 | a peek failure refuses instead of routing to `session_route_` | |
| M-17 | the gate's parser keeps `inbound_tv_` | the #426 hazard: `msg.get` then finds the field in the Data |
| M-21 | the peek allocates a `std::string` | proves W-16 can report |
| M-22 | the peek walks with `dict_hooks::none()` | Gate A Codex 1 |
| M-23 | the past-header probe removed | measured: the frame then passes |
| M-24 | the MsgType mismatch check removed | |
| M-25 | the past-header probe runs after the validator | body `1128=abc` draws 373=5 |
| M-26 | the 1137 disposition runs after the gate | absent stays in the arm: N-1 |
| M-27 | selection reads `negotiated_appl_version_` | the second connection selects from the first's |
| M-28 | a refusal is acted on with validation off | |
| M-29 | the transport guard refuses a non-validating session | |
| M-30 | `register_session` does not call the check | |
| M-31 | `fixpp_msg_clone` does not copy the byte | |
| M-32 | `owning_message_handle_from_frame` does not copy the byte | |
| M-33 | 1128 removed from `kSendHeaderTags` | |
| M-34 | `add_dictionary` stores nothing | |
| M-35 | the validation setter is a no-op | |
| M-36 | `fixpp_session_open` returns `FIXPP_ERR_OK` on a refused registration | |
| M-37 | a SWIG function dropped | |
| M-38 | an extra overwrites `dictionaries.front()` | |
| M-39 | the overlay merges into every extra | |
| M-40 | extras pushed before `path` | |
| M-41 | `Engine::start` does not build the set | |
| M-42 | the overload merges framing tags into `valid_` | Gate A Codex 6 |
| M-43 | plain `as_table_view()` gains the FIX 4.x surface | a non-FIXT session then accepts 1128 |
| M-44 | `build` maps a failure to a miss | a route with a hole |
| M-45 | the standard set keeps 10, 89, 93 | N-13 |
| M-46 | admin is `detail::is_admin_msgtype` | a Logon routes as application: N-14 |
| M-47 | a seeded delay on the non-FIXT path | proves the paired bench reports |
| M-48 | the overload populates framing for FIX Latest | answers change through `field_type_of_with_framing` |

M-18 to M-20 are unused.

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
| FR-009 | W-4 | NFR-001 | W-17, W-22 |
| FR-010 | W-5 | NFR-002 | W-16 |
| FR-011 | W-5, W-6 | NFR-003 | W-15 |
| FR-012 | W-3, W-7, W-19, W-21 | NFR-004 | W-12 |
| FR-013 | W-6 | NFR-005 | W-18 |
| FR-014 | W-10, W-12, W-13 | SC-001 to SC-007 | W-1; W-2; W-4, W-5; W-3; W-17; W-12 to W-14; W-15 |

Sites: frame scans W-5 (two establishment arms), W-7 (Active, Logout-sent), W-19 (all four). Gates W-4 (Active), W-5, W-9 (establishment). Interpret W-5. Dispatch W-7, one cell per site.
