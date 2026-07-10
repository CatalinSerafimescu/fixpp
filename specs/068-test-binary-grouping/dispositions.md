# Disposition Ledger — 068-test-binary-grouping

The FR-011 audit trail: every test `.cpp` in every processed module is recorded
here as `grouped:<bucket>` or `standalone:<reason>`. Per-module the sum of rows
MUST equal the module's `.cpp` count.

## Census signal-set (FR-002 / Research §D3) — forces **standalone**

A `.cpp` stays standalone if it matches any of these (classify by **mechanism**,
not filename):

- **allocation-counting** — in-TU global-`operator new` counter, `mallocnesia`
  `LD_PRELOAD` gate, `alloc_guard`. Grep: `operator new` / `set_new_handler` /
  global `alloc_count`. *(A **local** `std::pmr::memory_resource` subclass with a
  per-instance counter passed explicitly is isolation-safe → groupable.)*
- **OOM-injection via global mechanism** — process-wide new-handler / injection
  toggle. *(Local failing-`pmr` passed explicitly → groupable.)*
- **TSan-specific target** / any test carrying a **heterogeneous** per-test
  `ENVIRONMENT` / `TSAN_OPTIONS` / suppression file. *(A homogeneous
  `ENVIRONMENT` shared by the whole bucket may ride the grouped binary — D3.)*
- **top-level `abort()`/`_exit()`** death (NOT gtest fork-based `EXPECT_DEATH`,
  which groups — D3), or link-mode override.
- **genuinely-concurrent / global-singleton-freshness** — spawns
  `std::thread`/`std::jthread`/`std::async`, or mutates a function-local
  `static`/process-global registry read by other `TEST`s. No reliable grep;
  manual-review flag.
- **per-target `target_compile_definitions` variants** of one `.cpp` (e.g.
  `_wide` vs `_portable`).
- **exact-set completeness gate** with a precise `-L` feature label.
- **`ctest -R <target-name>` by name** — see the `-R` policy below.
- **label-heterogeneous** — the sole groupable member of its label class (a
  bucket-of-one yields no disk win → standalone; D4 label-homogeneity).

**Bucket key (D4):** partition the groupable set by `(sorted link-libs, sorted
labels)`; each partition → one `gtest_discover_tests` binary.

## `-R`-by-name policy (SC-004 / Scenario-3 reconciliation)

Grouping renames ctest entries from the target name (`dictionary_lookup_test`)
to per-case `Suite.Case` (`DictionaryLookupFixture.…`). Every documented
`ctest -R <target-name>` / `-R '^<module>_'` / `-R '<module>|…'` idiom in a
**merged-feature** quickstart/tasks doc therefore stops resolving. A literal
reading of SC-004 ("every `-R` resolves to the same set") is **unachievable
under any grouping** — a module-prefix idiom like `-R '^dictionary_'` can only
be satisfied by grouping nothing.

**Policy (user decision 2026-07-10):** preserve every `ctest -L <label>`
selection (labels re-applied at case granularity via `gtest_discover_tests
PROPERTIES LABELS`); for each historical `-R`-by-name idiom that grouping
breaks, record the **equivalent `-L` replacement** here — the "equivalent
selection documented as its replacement" branch of Scenario-3. A test is kept
standalone for `-R` reasons only when a *live* procedure (active tooling, not a
merged-feature snapshot) selects it by exact target name.

---

## Module: `dictionary` (25 `.cpp`) — PILOT (US1)

### Grouped

**Bucket `dictionary_pure_tests`** — 15 `.cpp`, label `dictionary`, link
`fixpp_dictionary` + `pugixml::pugixml` (union for `round_trip` /
`reused_tag_census` raw-XML scans) + gtest:

| `.cpp` | decision | odr_action |
|---|---|---|
| `ref_shape_test` | grouped:pure | none |
| `xml_loader_test` | grouped:pure | none |
| `round_trip_test` | grouped:pure (needs pugixml) | none |
| `determinism_test` | grouped:pure | none |
| `negative_paths_test` | grouped:pure | none |
| `parser_error_test` | grouped:pure | none |
| `lookup_test` | grouped:pure | none |
| `version_profile_test` | grouped:pure | none |
| `field_traits_test` | grouped:pure | none |
| `version_registry_test` | grouped:pure | none |
| `table_view_test` | grouped:pure | none |
| `defect_a_group_context_test` | grouped:pure | none |
| `reused_tag_census_test` | grouped:pure (needs pugixml) | none |
| `collision_membership_guards_test` | grouped:pure | none |
| `oom_injection_test` | grouped:pure (local failing-`pmr`, no global) | none |

**Bucket `dictionary_reify_tests`** — 4 `.cpp`, label `dictionary`, link
`fixpp_dictionary` + gtest, include `_codegen/include`, depends
`fixpp_codegen_generate`:

| `.cpp` | decision | odr_action |
|---|---|---|
| `reify_test` | grouped:reify | none (helpers in anon-ns) |
| `reify_move_test` | grouped:reify | none |
| `reify_dispatch_test` | grouped:reify (local null-`pmr` OOM) | none (helpers in anon-ns) |
| `reify_oom_test` | grouped:reify (local failing/counting-`pmr`) | none (helpers in anon-ns) |

### Standalone (6)

| `.cpp` | reason |
|---|---|
| `pmr_allocation_test` | allocation-counting (in-TU global `operator new`) |
| `concurrent_readers_test` | genuinely concurrent (`std::thread`) |
| `group_context_lookup_alloc_gate_test` | alloc gate + `mallocnesia` add_test keyed by `$<TARGET_FILE:…>` (live name-selection) |
| `reify_cross_strand_test` | TSan target (`LABELS dictionary;tsan`) + threads |
| `reify_membership_identity_test` | label-heterogeneous (`066;dictionary;us1`) — sole groupable member of its label class → standalone (D4) |
| `reify_membership_copy_oom_test` | allocation-counting (global `operator new`) + `066;dictionary;us1` |

**Sum:** 15 + 4 grouped + 6 standalone = **25** ✓ (100% dispositioned).

### `-R` replacements documented (SC-004 / Scenario-3)

| historical idiom (doc) | breaks under grouping? | equivalent replacement |
|---|---|---|
| `-R '^dictionary_'` (002 quickstart:27) | yes (prefix — breaks under any grouping) | `-L dictionary` |
| `-R 'dictionary\|wire\|codegen\|…'` (063 quickstart:20, tasks:26) | yes (lowercase substring vs `Dictionary…` suites) | `-L dictionary` (+ existing `-L`/`-R` for wire/codegen) |
| `-R 'dictionary_(lookup\|negative\|xml_loader)'` (064 quickstart:28, tasks:97) | yes | `-L dictionary` |
| `-R determinism_test` (003 quickstart:78) | yes (`Determinism.*`) | `-L dictionary` (or `-R Determinism`) |
| `-R reify_dispatch` (057 quickstart:42) | yes (`ReifyDispatch*`) | `-L dictionary` (or `-R ReifyDispatch`) |
| `-R 'determinism\|build_graph'` (057 quickstart:50, tasks:97) | determinism yes; `build_graph` is a codegen-module test (unaffected) | `-L dictionary` for determinism |
| `-R '^dictionary_concurrent_readers_test$'` (002 quickstart:78) | **no** — stays standalone, name preserved | — (unchanged) |

## Module: `session` (161 `.cpp`) — US2

Largest module (~160 test binaries/preset); HEAVY standalone set per plan —
classified conservatively by MECHANISM (not filename). Two whole-binary
`add_test` buckets formed from the empty-label subset that neither spawns an
`asio::thread_pool`/`std::thread` nor is selected by exact target name; every
uniquely-labeled feature witness (the module carries ~76 distinct compound
LABELS strings — 010/012/013/014/015/016/019/020/021/022/023/024/025/026/
027/028/029/033/034/036/037/038/040/041/043/046/059/061/065/066/067/…),
every thread_pool/std::thread-spawning test, every global-alloc-counting /
OOM / TSan-specific / name-selected test stays standalone (D3/D4). Note:
`conformance/` (6 `tc_*.cpp`) has its own `CMakeLists.txt` — separate module,
out of scope here.

### ODR pre-check (§3)

No renames were needed. Grep census across both bucket candidate sets found:
no own `int main(`; no duplicate `TEST(Suite,Name)`. Two apparent file-scope
class-name collisions (`OutboundCapture` × 7, `OrderingStore` × 2) were
verified by namespace-bound analysis to be **already** anonymous-namespace-
scoped per-TU (internal linkage) — false positives from a naive column-0
class/struct grep that does not track brace/namespace nesting. All free
helper functions in the two buckets are `static`. Zero FR-012 renames.

### Grouped

**Bucket `session_pure_tests`** — 41 `.cpp`, no LABELS, link `fixpp_session` +
`fixpp_mock_clock` + `fixpp_transport` + `fixpp_tls` + OpenSSL/ZLIB (union of
member needs) + gtest, include `tests/` + `src/`, compile-defs `FIXPP_TEST_HOOKS`
+ `FIXPP_TEST_BINARY_DIR`/`FIXPP_TEST_SOURCE_DIR` + `FIXPP_TLS_FIXTURE_DIR`
(union of member needs — harmless no-ops for members that don't reference
them), `ENVIRONMENT`/`TIMEOUT 120` identical to every member's pre-existing
`add_threading_test` macro defaults (FR-005 preserved exactly). Single-
threaded `co_spawn(ioc,…)`+`ioc.run()` FSM/builder/handshake/reconnect/
compid-binding/store-recovery witnesses — no `asio::thread_pool`/`std::thread`.

| `.cpp` | decision | odr_action |
|---|---|---|
| `admin_builder_distinct_now_test` | grouped:pure | none |
| `admin_emit_mixed_path_test` | grouped:pure | none |
| `cancellation_two_phase_test` | grouped:pure | none |
| `cfg_lifetime_safety_test` | grouped:pure | none |
| `coverage_adversarial_test` | grouped:pure | none |
| `durable_before_transmit_test` | grouped:pure | none |
| `fix_time_roundtrip_test` | grouped:pure | none |
| `fsm_matrix_witness_test` | grouped:pure | none |
| `fsm_transition_matrix_test` | grouped:pure | none |
| `heartbeat_testrequest_test` | grouped:pure | none |
| `initiator_transport_throw_test` | grouped:pure | none |
| `logon_handshake_test` | grouped:pure | none |
| `logon_received_observability_test` | grouped:pure | none |
| `send_path_test` | grouped:pure | none |
| `sending_time_test` | grouped:pure | none |
| `seqnum_gap_fatal_test` | grouped:pure | none |
| `seqnum_t_handoff_test` | grouped:pure | none |
| `session_reject_test` | grouped:pure | none |
| `session_send_invalid_state_test` | grouped:pure | none |
| `test_backpressure_drop_oldest_banned` | grouped:pure | none |
| `test_cancellation_fromapp_to_close` | grouped:pure | none |
| `test_cancellation_parse_to_fromapp` | grouped:pure | none |
| `test_compid_binding_default_deny` | grouped:pure | none |
| `test_compid_binding_mtls_fail_closed` | grouped:pure | none |
| `test_compid_binding_principal_extraction` | grouped:pure | none |
| `test_compid_binding_symmetric` | grouped:pure | none |
| `test_heartbeat_cadence_8cell` | grouped:pure | none |
| `test_inbound_sequence_reset` | grouped:pure | none |
| `test_live_outbound_serialized` | grouped:pure | none |
| `test_logout_timeout` | grouped:pure | none |
| `test_reconnect_happy_path` | grouped:pure | none |
| `test_recovery_admin_span_gapfill` | grouped:pure | none |
| `test_recovery_store_horizon` | grouped:pure | none |
| `test_reload_credentials_in_flight` | grouped:pure | none |
| `test_reset_seqnum_policy_matrix` | grouped:pure | none |
| `test_seqnum_drain_on_close` | grouped:pure | none |
| `test_session_event_ring_overflow` | grouped:pure | none |
| `test_session_invariant_counter_witness` | grouped:pure | none |
| `test_session_layering` | grouped:pure | none |
| `test_session_open_rejects_unset_security_profile` | grouped:pure | none |
| `test_version_registry_missing_routes_to_dict_layer` | grouped:pure | none |

**Bucket `session_store_tests`** — 6 `.cpp`, no LABELS, link `fixpp_session` +
`fixpp_mock_clock` (union) + gtest, include `tests/`, compile-def
`FIXPP_TEST_HOOKS` (union), `ENVIRONMENT` identical to `fixpp_add_store_test`'s
default (no `TIMEOUT` — none of the 6 had one; not introduced). Store-family
witnesses that construct no `asio::thread_pool`: three reconcile fault-double
table-driven witnesses, the toApp store-collision provenance witness, the
Path-B compile-time guard (build-is-the-test static_asserts), the SeqnumManager
unit test.

| `.cpp` | decision | odr_action |
|---|---|---|
| `seqnum_manager_test` | grouped:store | none |
| `test_quickfix_compat_path_b_guard` | grouped:store | none |
| `test_send_toapp_store_collision` | grouped:store | none |
| `test_store_fail_reconcile_breadth` | grouped:store | none |
| `test_store_fail_reconcile_outofset` | grouped:store | none |
| `test_store_fail_reconcile_readfail` | grouped:store | none |

### Standalone (114)

| `.cpp` | reason |
|---|---|
| `engine_acceptor_failclosed_test` | label-heterogeneous (LABELS "015;us1;live_tls;acceptor;failclosed") |
| `engine_acceptor_test` | label-heterogeneous (LABELS "015;us1;live_tls;acceptor") |
| `engine_connect_test` | label-heterogeneous (LABELS "015;us2;live_tls;connect") |
| `engine_firstframe_test` | label-heterogeneous (LABELS "015;us1;live_tls;firstframe;window") |
| `engine_harness_compile_smoke_test` | label-heterogeneous (LABELS "015;foundational;harness;compile_smoke") |
| `engine_lifecycle_test` | label-heterogeneous (LABELS "015;us3;live_tls;lifecycle") |
| `engine_readpump_test` | label-heterogeneous (LABELS "015;us2;live_tls;readpump") |
| `engine_seam_removal_test` | label-heterogeneous (LABELS "015;us4;compid;seam_removal") |
| `engine_session_id_test` | label-heterogeneous (LABELS "015;foundational;session_id") |
| `interpret_logon_overflow_test` | label-heterogeneous (LABELS "040;sc001;overflow-hardening;foundational") |
| `logout_exchange_test` | genuinely concurrent (asio::thread_pool/std::thread) |
| `reconnect_policy_witness_test` | label-heterogeneous (LABELS "016;us-foundational;reconnect;down-peer;watchdog") |
| `scan_first_frame_ids_overflow_test` | label-heterogeneous (LABELS "040;us2;scan_first_frame_ids;overflow") |
| `scan_frame_header_overflow_test` | label-heterogeneous (LABELS "040;us1;scan_frame_header;overflow") |
| `security_profile_insecure_plain_tcp_deprecated_negative` | compile-negative harness: add_test COMMAND is `cmake --build ... --target session_security_profile_insecure_plain_deprecated_negative` with WILL_FAIL TRUE (asserts the target FAILS to compile); label-heterogeneous (LABELS "043;us2;sc005;compile_negative;friction") |
| `session_smoke_test` | manual-review: kept standalone (raw add_executable, no TIMEOUT/ENV set; avoids introducing property drift into a bucket) |
| `test_017_session_config_amendment` | manual-review: kept standalone (raw add_executable, no TIMEOUT/ENV set; avoids introducing property drift into a bucket) |
| `test_019_application_compile_smoke` | label-heterogeneous (LABELS "019;foundational;compile_smoke;application") |
| `test_019_g2_enablement_witness` | label-heterogeneous (LABELS "019;phase7;g2;witness;live_tls") |
| `test_019_msgtype_classifier` | label-heterogeneous (LABELS "019;foundational;classifier") |
| `test_066_admin_no_group_test` | label-heterogeneous (LABELS "066;us3") |
| `test_066_arena_fit_test` | label-heterogeneous (LABELS "066;us3") |
| `test_066_group_membership_red_test` | label-heterogeneous (LABELS "066;us1;red") |
| `test_066_group_scaffold_smoke_test` | label-heterogeneous (LABELS "066;setup;scaffold") |
| `test_066_scalar_as_group_test` | label-heterogeneous (LABELS "066;us2") |
| `test_066_validator_on_grouped_test` | label-heterogeneous (LABELS "066;us1") |
| `test_067_builder_failclosed` | label-heterogeneous (LABELS "067;us1;failclosed") |
| `test_067_builder_roundtrip` | label-heterogeneous (LABELS "067;us1;roundtrip") |
| `test_067_builder_shape_oracle` | label-heterogeneous (LABELS "067;us2;shape_oracle;golden;D;8;9;E;AS") |
| `test_067_builder_validate` | label-heterogeneous (LABELS "067;us3;validate") |
| `test_067_completeness` | label-heterogeneous (LABELS "067;us1;completeness") |
| `test_acceptor_logon_sending_time` | label-heterogeneous (LABELS "038;s019;acceptor-logon-sendingtime") |
| `test_admin_emit_toadmin_coverage` | label-heterogeneous (LABELS "036;us1;us2;us3;bmr;toadmin_coverage;toapp") |
| `test_application_business_reject` | label-heterogeneous (LABELS "019;us1;business_reject;application") |
| `test_application_engine_send` | genuinely concurrent (asio::thread_pool/std::thread); label-heterogeneous (LABELS "019;gate_b;engine_send;application;live_tls") |
| `test_application_inbound` | label-heterogeneous (LABELS "019;us1;inbound;application") |
| `test_application_lifecycle` | label-heterogeneous (LABELS "019;us3;lifecycle;application") |
| `test_application_outbound` | label-heterogeneous (LABELS "019;us2;outbound;application") |
| `test_application_strand` | label-heterogeneous (LABELS "019;phase6;strand;drain;application") |
| `test_application_throw` | label-heterogeneous (LABELS "019;phase6;throw;application") |
| `test_business_messages_build` | genuinely concurrent (asio::thread_pool/std::thread); label-heterogeneous (LABELS "020;us1;build;builder") |
| `test_business_messages_read` | label-heterogeneous (LABELS "020;us1;read;flyweight") |
| `test_business_messages_roundtrip` | genuinely concurrent (asio::thread_pool/std::thread); label-heterogeneous (LABELS "020;us1;roundtrip;send_path;inv1;inv5;inv7;inv8") |
| `test_clock_injection_corpus` | genuinely concurrent (asio::thread_pool/std::thread) |
| `test_compid_binding_seam` | label-heterogeneous (LABELS "014;us2;compid;seam") |
| `test_credential_store_redaction` | genuinely concurrent (asio::thread_pool/std::thread); $<TARGET_FILE:> name-selected by credential_store_redaction_mallocnesia sidecar |
| `test_credentials_rotated_emit` | label-heterogeneous (LABELS "014;us3;live_tls;credentials_rotated") |
| `test_direct_executor_reentrancy` | genuinely concurrent (asio::thread_pool/std::thread) |
| `test_engine_clock_gate` | label-heterogeneous (LABELS "041;t017;t018;us3;clock-gate;session") |
| `test_engine_reader_snapshot_publish_acquire` | genuinely concurrent (asio::thread_pool/std::thread); label-heterogeneous (LABELS "session;015;046;nfr017;publish_acquire") |
| `test_engine_session_strand` | genuinely concurrent (asio::thread_pool/std::thread); label-heterogeneous (LABELS "023;session;control_strand;tsan") |
| `test_executor_compat` | genuinely concurrent (asio::thread_pool/std::thread) |
| `test_exemplar_build_failclosed` | label-heterogeneous (LABELS "061;us2;failclosed;D;8;9;E;AS") |
| `test_exemplar_read` | label-heterogeneous (LABELS "061;us3;read") |
| `test_exemplar_roundtrip` | label-heterogeneous (LABELS "061;us2;roundtrip;golden;E") |
| `test_file_store_cancellation` | genuinely concurrent (asio::thread_pool/std::thread) |
| `test_file_store_compid_validation` | genuinely concurrent (asio::thread_pool/std::thread) |
| `test_file_store_concurrent_tsan` | genuinely concurrent (asio::thread_pool/std::thread) |
| `test_file_store_coverage_uplift` | genuinely concurrent (asio::thread_pool/std::thread) |
| `test_file_store_crash_survival` | genuinely concurrent (asio::thread_pool/std::thread); heterogeneous TSan ENV override (die_after_fork=0, fork+thread_pool child) |
| `test_file_store_flush_for_session_close` | genuinely concurrent (asio::thread_pool/std::thread) |
| `test_file_store_offload_thread` | genuinely concurrent (asio::thread_pool/std::thread) |
| `test_file_store_torn_write` | genuinely concurrent (asio::thread_pool/std::thread) |
| `test_fixt_credentials` | label-heterogeneous (LABELS "033;s022;fixt-credentials;foundational") |
| `test_fixt_logon_establishment` | label-heterogeneous (LABELS "033;s020;s025;fixt-logon-establishment;foundational") |
| `test_heartbeat_under_mock_clock` | genuinely concurrent (asio::thread_pool/std::thread) |
| `test_inbound_poss_dup_tolerance` | label-heterogeneous (LABELS "021;us1;possdup;tolerance;inv1;inv2;inv5") |
| `test_inbound_poss_dup_validation` | label-heterogeneous (LABELS "021;us2;possdup;validation;armc;armd;arme") |
| `test_inbound_poss_resend` | label-heterogeneous (LABELS "022;us1;possresend") |
| `test_interpret_logon_encrypt_method` | label-heterogeneous (LABELS "043;fr009;xii7;encrypt-method") |
| `test_live_identity_binding` | label-heterogeneous (LABELS "014;us2;live_tls;identity_binding") |
| `test_memory_store_capacity` | genuinely concurrent (asio::thread_pool/std::thread) |
| `test_memory_store_reset_during_retrieve` | genuinely concurrent (asio::thread_pool/std::thread) |
| `test_memory_store_round_trip` | genuinely concurrent (asio::thread_pool/std::thread) |
| `test_memory_store_zero_allocator_calls` | genuinely concurrent (asio::thread_pool/std::thread) |
| `test_next_expected_msgseqnum` | label-heterogeneous (LABELS "027;s031;next-expected-msgseqnum;foundational") |
| `test_outbound_store_post_commit` | genuinely concurrent (asio::thread_pool/std::thread) |
| `test_persistent_seqnum_hydrate` | label-heterogeneous (LABELS "029;s042;persistent-seqnum-hydrate;setup") |
| `test_quickfix_compat_cfg_loader` | genuinely concurrent (asio::thread_pool/std::thread) |
| `test_reconnect_backoff_cap` | label-heterogeneous (LABELS "014;us1;reconnect") |
| `test_reconnect_cancel_mid_handshake` | label-heterogeneous (LABELS "014;us1;reconnect;cancel") |
| `test_reconnect_live_happy_path` | label-heterogeneous (LABELS "014;us1;live_tls;reconnect") |
| `test_refresh_on_logon` | label-heterogeneous (LABELS "025;s018;refresh-on-logon;setup") |
| `test_resend_reply_possdup` | label-heterogeneous (LABELS "037;us1;possdup;gapfill") |
| `test_reset_on_lifecycle` | label-heterogeneous (LABELS "024;s017;reset-knobs") |
| `test_retrieve_visitor` | genuinely concurrent (asio::thread_pool/std::thread) |
| `test_retrieve_with_gaps` | genuinely concurrent (asio::thread_pool/std::thread) |
| `test_send_allow_pos_dup_strip` | label-heterogeneous (LABELS "022;us2;allowposdup;strip") |
| `test_sending_time_precision` | label-heterogeneous (LABELS "026;s039;sending-time-precision") |
| `test_session_forced_fallback_link` | label-heterogeneous (LABELS "session;046;nfr017;fallback_link") |
| `test_session_fsm_via_mock_transport` | label-heterogeneous (LABELS "session;012;us4;seam6;mock") |
| `test_session_no_implicit_insecure_plain_tcp` | label-heterogeneous (LABELS "043;us2;sc002;no_implicit_default") |
| `test_session_plaintext_authz` | label-heterogeneous (LABELS "043;t008;us1;plaintext;authz") |
| `test_session_plaintext_factory_mismatch` | label-heterogeneous (LABELS "043;t021;t022;us3;sc003;fr008;mismatch") |
| `test_session_plaintext_reconnect` | label-heterogeneous (LABELS "043;us1;plaintext;reconnect") |
| `test_session_plaintext_roundtrip` | label-heterogeneous (LABELS "043;t007;us1;plaintext;roundtrip;live_plain") |
| `test_store_cancellation_contract` | genuinely concurrent (asio::thread_pool/std::thread) |
| `test_store_fail_closed_persistent` | genuinely concurrent (asio::thread_pool/std::thread) |
| `test_store_fail_open_volatile` | genuinely concurrent (asio::thread_pool/std::thread) |
| `test_store_fail_reconcile` | genuinely concurrent (asio::thread_pool/std::thread) |
| `test_store_fifo_fair` | genuinely concurrent (asio::thread_pool/std::thread) |
| `test_store_pmr_poison_retrieve` | genuinely concurrent (asio::thread_pool/std::thread) |
| `test_store_reset` | genuinely concurrent (asio::thread_pool/std::thread) |
| `test_store_reset_crash_cut` | genuinely concurrent (asio::thread_pool/std::thread); heterogeneous TSan ENV override (die_after_fork=0, fork+thread_pool child) |
| `test_store_seqnum_out_of_order` | genuinely concurrent (asio::thread_pool/std::thread) |
| `test_store_shutdown_ordering` | genuinely concurrent (asio::thread_pool/std::thread) |
| `test_strand_serialisation` | genuinely concurrent (asio::thread_pool/std::thread) |
| `test_test_request_id_cross_session_race` | genuinely concurrent (asio::thread_pool/std::thread) |
| `test_trace_context_accessors` | manual-review: kept standalone (raw add_executable, no TIMEOUT/ENV set; avoids introducing property drift into a bucket) |
| `test_validate_gate_default_off` | label-heterogeneous (LABELS "041;t015;t016;us2;validation-gate;default-off;session") |
| `test_validate_gate_inbound` | label-heterogeneous (LABELS "041;t012;t014;validation-gate;inbound;foundational") |
| `test_validate_gate_logon_arm` | label-heterogeneous (LABELS "041;t013;t014;validation-gate;logon-arm;foundational") |
| `test_validation_compat_toggles` | label-heterogeneous (LABELS "028;s040;s041;validation-compat-toggles;setup") |
| `test_validation_gate_config` | label-heterogeneous (LABELS "041;t003;t004;validation-gate;foundational") |

**Sum:** 41 + 6 grouped + 114 standalone = **161** ✓ (100% dispositioned).

### `-R`/`-L` selectability (SC-004 / Scenario-3)

`ctest -L session` selects 8 tests both before and after (unchanged —
`-L <regex>` substring-matches any label containing "session"; none of the
47 grouped `.cpp` previously carried any label, and the two new bucket
targets carry none either, so the pre-existing 8-test set is untouched).
Every uniquely-labeled feature `-L <feature>` selector (spot-checked: `-L 019`
→ 12, `-L 066` → 15, `-L 034` → 1) is unaffected since those tests stayed
standalone. The one live name-selection idiom in this module —
`$<TARGET_FILE:credential_store_redaction>` consumed by the
`credential_store_redaction_mallocnesia` sidecar — is preserved by keeping
`credential_store_redaction` standalone under its exact original name.
No historical `-R`-by-target-name idiom targeted any of the 47 grouped names
(they were `add_threading_test`/`fixpp_add_store_test` seam registrations, not
named in any quickstart/tasks `-R` selector); nothing to replace.
