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

## Module: `interop` (31 `.cpp`) — US2

Session-layer interop gate (016-interop-harness). Uses the module's own
`fixpp_add_interop_test(NAME... GROUP... LABEL... SOURCES...)` helper, which
already accepts multi-value `SOURCES` — grouped buckets are built by passing
multiple `.cpp` to one call (no hand-rolled `add_executable`), which
preserves the `interop_<happy|thorny|parity>` aggregation-target wiring
(`add_dependencies`) the helper performs per NAME. `interop_cell_results_schema_check`
is a `pytest` invocation over `cell_results_schema_check_test.py` (US4 T028),
not a `.cpp`/gtest binary — outside grouping scope, unchanged (mirrors how
the `session` ledger notes `conformance/` as a separate module).

**Key discriminator (conservative, per orchestrator note + "when unsure →
standalone"):** every TLS-linked cell (`happy/*`, TLS `thorny/*` 021/022,
`test_business_message_interop.cpp`) builds a real
`fixpp::transport::TransportFactory` (`make_interop_tls_factory`) and calls
`Engine::start()`, which binds/connects a **real TCP socket** (localhost,
env-selected or OS-assigned port) — kept standalone even where two TLS cells
share an identical LABEL (`hp_fix44_testrequest_echo_test` /
`hp_fix44_reject_invalid_admin_test`, both `"interop;016;interop-happy;us1"`).
Non-TLS `parity/*` and non-TLS `thorny/*` cells use `ParityAcceptorFixture`
(`parity/parity_support.hpp`) or an equivalent local fixture (`qfj-626`) that
overrides `transport_send` with an in-process capture lambda and pumps a
private `asio::io_context` via `ioc.run_for()+ioc.restart()` on the calling
thread only — no real socket, no `std::thread`/`thread_pool` anywhere in the
module (grepped) — isolation-safe, same pattern as the already-grouped
`session_pure_tests`/`session_store_tests` precedent.

### ODR pre-check (§3)

No `int main(` in any `.cpp`. No duplicate `TEST`/`TEST_F`/`TEST_P`
`Suite.Name` across the module. `thorny/{framing,recovery,reject}/*.cpp` each
declare a local `using Thorny*Fixture = fixpp::interop::parity::ParityAcceptorFixture;`
type alias — a compile-time-only alias with no linkable symbol, safe to
repeat verbatim across TUs linked into the same bucket (not an ODR
violation). Free non-`static` helpers found in bucket sources
(`any_reject_value_incorrect` in `inbound_sequencereset_arms_test.cpp`;
`frame_from_body`/`any_frame_contains`/`frame_has` in
`fix_tc_coverage_gaps_test.cpp`) are wrapped in an anonymous namespace
(internal linkage) — verified no name collides with any other bucket member
(`qfj-626`'s helpers are already `static`). Zero FR-012 renames.

### Grouped

**Bucket `interop_parity_us3_tests`** — 4 `.cpp`, label
`"interop;016;interop-parity;us3"`, link `interop_support` + `fixpp_mock_clock`
+ gtest (no TLS):

| `.cpp` | decision | odr_action |
|---|---|---|
| `parity/resend_abort_on_failing_write_test.cpp` | grouped:parity_us3 | none |
| `parity/inbound_sequencereset_arms_test.cpp` | grouped:parity_us3 | none (anon-ns helper) |
| `parity/replay_subsumes_reorder_queue_test.cpp` | grouped:parity_us3 | none |
| `parity/fix_tc_coverage_gaps_test.cpp` | grouped:parity_us3 | none (anon-ns helpers) |

**Bucket `interop_thorny_recovery_us2_tests`** — 3 `.cpp`, label
`"interop;016;interop-thorny;us2;recovery"`, link `interop_support` +
`fixpp_mock_clock` + gtest (no TLS):

| `.cpp` | decision | odr_action |
|---|---|---|
| `thorny/recovery/qfj-750-logout-seqnum-mismatch_test.cpp` | grouped:thorny_recovery_us2 | none |
| `thorny/recovery/qfj-271-sequencereset-large-gapfill_test.cpp` | grouped:thorny_recovery_us2 | none |
| `thorny/recovery/qfj-626-resend-recomputes-checksum_test.cpp` | grouped:thorny_recovery_us2 | none (local static helpers) |

**Bucket `interop_thorny_framing_us2_tests`** — 2 `.cpp`, label
`"interop;016;interop-thorny;us2;framing"`, link `interop_support` +
`fixpp_mock_clock` + gtest (no TLS):

| `.cpp` | decision | odr_action |
|---|---|---|
| `thorny/framing/qfj-603-unsupported-beginstring_test.cpp` | grouped:thorny_framing_us2 | none |
| `thorny/framing/qfj-721-non-logon-first-message_test.cpp` | grouped:thorny_framing_us2 | none |

### Standalone (22)

| `.cpp` | reason |
|---|---|
| `support_smoke_test.cpp` | label-heterogeneous (sole `"interop;016;interop-support"`) |
| `happy/hp_fix44_logon_hb_logout_test.cpp` | real-socket TLS cell (label-heterogeneous, sole `us1;smoke`) |
| `happy/hp_fixt50sp2_logon_hb_logout_test.cpp` | real-socket TLS cell (label-heterogeneous, sole `033;us3;fixt;fix50sp2`) |
| `happy/hp_down_peer_stop_watchdog_test.cpp` | real-socket TLS cell (label-heterogeneous, sole `us1;down-peer;watchdog`) |
| `happy/hp_fix44_testrequest_echo_test.cpp` | real-socket TLS cell — conservative standalone despite shared label `"interop;016;interop-happy;us1"` with the next row (bind/connect real localhost port; orchestrator note + "when unsure → standalone") |
| `happy/hp_fix44_reject_invalid_admin_test.cpp` | real-socket TLS cell — same shared-label pair, same conservative reason |
| `happy/hp_fix44_seqnum_recovery_test.cpp` | real-socket TLS cell (label-heterogeneous, sole `us1;us3`) |
| `happy/hp_fix44_recovery_outbound_answer_test.cpp` | real-socket TLS cell (label-heterogeneous, sole `018;us3`) |
| `happy/hp_fix44_idle_heartbeat_cadence_test.cpp` | real-socket TLS cell (label-heterogeneous, sole `018;us2`) |
| `happy/hp_fix44_disconnect_reconnect_noreset_test.cpp` | real-socket TLS cell (label-heterogeneous, sole `us1;reconnect`) |
| `happy/hp_fix44_reset_on_logon_test.cpp` (024) | real-socket TLS cell (label-heterogeneous, sole `024;...;reset-on-logon`) |
| `happy/hp_fix44_received_reset_test.cpp` (030) | real-socket TLS cell (label-heterogeneous, sole `030;...;received-reset`) |
| `happy/hp_fix44_nanos_sendingtime_test.cpp` (026) | real-socket TLS cell (label-heterogeneous, sole `026;...;nanos`) |
| `thorny/reject/qfj-557-generatereject-advances-seqnum_test.cpp` | label-heterogeneous — sole member of `"...;reject"` (D4 bucket-of-one) |
| `thorny/recovery/021-poss-dup-replay-survives_test.cpp` | real-socket TLS cell (label-heterogeneous, sole `021;us1;...;poss-dup`) |
| `thorny/recovery/021-poss-dup-malformed-dup-rejected_test.cpp` | real-socket TLS cell (label-heterogeneous, sole `021;us2;...;poss-dup`) |
| `thorny/framing/022-allow-pos-dup-strip-send_test.cpp` | real-socket TLS cell (label-heterogeneous, sole `022;...;allow-pos-dup`) |
| `thorny/recovery/022-poss-resend-deliver_test.cpp` | real-socket TLS cell (label-heterogeneous, sole `022;...;poss-resend`) |
| `test_business_message_interop.cpp` | real-socket TLS cell + codegen-gated + explicit `TIMEOUT 30` (label-heterogeneous, sole `020;us2;us3;business-message`) |
| `happy/hp_fix44_next_expected_test.cpp` (027) | real-socket TLS cell (label-heterogeneous, sole `027;...;next-expected-msgseqnum`) |
| `happy/hp_fix44_validation_compat_test.cpp` (028) | real-socket TLS cell (label-heterogeneous, sole `028;...;validation-compat;...`) |
| `happy/hp_fix44_restart_resume_test.cpp` (029) | real-socket TLS cell (label-heterogeneous, sole `029;...;restart-resume`) |

**Sum:** 4 + 3 + 2 grouped (9) + 22 standalone = **31** ✓ (100% dispositioned).

### `-R`/`-L` selectability (SC-004 / Scenario-3)

Every `ctest -L <feature>` selector is preserved: the 3 new bucket targets
carry the exact same LABEL string every member previously carried
individually (D4 requires bucket label-homogeneity), so `-L interop`,
`-L 016`, `-L interop-parity`, `-L interop-thorny`, `-L us2`, `-L us3`,
`-L recovery`, `-L framing` all select the same logical case set as before
(spot-checked via `ctest -L interop`: 26 entries, 100% pass, `Label Time
Summary` shows `016 → 12 tests`, `us3 → 5 tests`, `recovery → 4 tests`,
`framing → 2 tests` — all consistent with the pre-grouping per-cell counts).
No historical `-R`-by-target-name idiom targeted any of the 9 grouped names.

## Module: `capi` (28 `.cpp`/`.c`) — US2

C-ABI test suite (050/051/052/062/065/066 layers). Key discriminator: the
C-ABI `fixpp_engine` (`src/capi/capi_internal.hpp`) owns
`std::vector<std::thread> workers_` — calling `fixpp_engine_start()` on a
validly-configured engine spawns **real background worker threads**
(genuinely concurrent, forces standalone). Grepped every `.cpp` for
`fixpp_engine_start`; every file that calls it (successfully) stays
standalone. `capi_occupancy_negative` / `capi_reentrancy_negative` are bash
script invocations (`tools/test_capi_{occupancy,reentrancy}_negative.sh`),
not `.cpp`/gtest binaries — outside grouping scope, unchanged.

### ODR pre-check (§3)

No `int main(` in the bucket's 6 `.cpp` (the 2 `.c` files DO have their own
`main()` but were never gtest binaries — they don't link `GTest::gtest_main`
at all, so they were never groupable candidates in the first place). No
duplicate `TEST`/`TEST_F` `Suite.Name`. `error_surface_test.cpp`'s
`load_csv()`/`symbol_to_code()` and `thunk_split_test.cpp`'s
`abort_trap_handler`/`g_abort_jmp`/`g_abort_caught`/`ScopedAbortTrap`/
`open_unstarted_session` are all wrapped in an anonymous namespace (internal
linkage) — verified via bracket-matched `namespace { … }` spans, not name
collisions with any other bucket member. Zero FR-012 renames.

### Grouped

**Bucket `capi_pure_tests`** — 6 `.cpp`, no LABELS (unset before AND after —
none of the 6 previously carried any label, so `-L capi` selection is
unaffected), link `fixpp_capi` + gtest, include `FIXPP_CAPI_FEATURE_B_INCLUDES`
(harmless additive for the 4 members that don't need it), compile-defs
`FIXPP_CAPI_DATA_DIR` + `FIXPP_TEST_HOOKS` + `FIXPP_CAPI_LIB` + `FIXPP_CAPI_GOLDEN`
(union — each a no-op for the members that don't reference it):

| `.cpp` | decision | odr_action |
|---|---|---|
| `capi_smoke_test.cpp` | grouped:pure | none |
| `version_test.cpp` | grouped:pure | none |
| `error_surface_test.cpp` | grouped:pure | none (anon-ns helpers) |
| `config_builders_test.cpp` | grouped:pure ("Pure builder unit tests — no engine, no event loop" per file header; never calls `fixpp_engine_start`) | none |
| `thunk_split_test.cpp` | grouped:pure (in-process SIGABRT trap via sigaction/sigsetjmp+siglongjmp, explicitly NOT gtest fork `EXPECT_DEATH` but scoped/restored per-`TEST` via `ScopedAbortTrap` RAII — same cross-test isolation argument the procedure grants fork-based death tests; only constructs an UNSTARTED engine/session) | none (anon-ns helpers) |
| `abi_symbol_golden_test.cpp` | grouped:pure (read-only `nm`-vs-golden-file + error-enum-boundary check; no global mutable state; no `-L` selector to break) | none |

### Standalone (22)

| `.cpp`/`.c` | reason |
|---|---|
| `capi_version_smoke.c` | pure-C compile-smoke gate — not a gtest binary (no `GTest::gtest_main` link, has its own `main()`) |
| `handles_compile_test.c` | pure-C compile-smoke gate — same reason |
| `error_block_test.cpp` | calls `fixpp_engine_start()` (real engine worker threads) |
| `lifecycle_test.cpp` | calls `fixpp_engine_start()` (real engine worker threads) |
| `lifecycle_negative_test.cpp` | calls `fixpp_engine_start()` successfully in 3 of its REJECTION arms (real engine worker threads) |
| `send_recv_test.cpp` | real two-C-ABI-engine loopback + explicit drain **thread** + ASan-conditional `WILL_FAIL` split into 2 `--gtest_filter`-selected ctest entries (live-name selection + heterogeneous ASan-only property) |
| `recv_alloc_guard_test.cpp` | `$<TARGET_FILE:capi_recv_alloc_guard_test>` live name-selection (mallocnesia sidecar) |
| `error_live_test.cpp` | calls `fixpp_engine_start()`; label-heterogeneous sole `capi;050` |
| `message_read_test.cpp` | `$<TARGET_FILE:capi_message_read_test>` live name-selection (mallocnesia sidecar) |
| `message_write_test.cpp` | `$<TARGET_FILE:capi_message_write_test>` live name-selection (mallocnesia sidecar); calls `fixpp_engine_start()` |
| `msg_clone_cross_strand_test.cpp` | calls `fixpp_engine_start()` (real engine worker threads; cross-strand concurrency witness) |
| `toapp_callback_test.cpp` | calls `fixpp_engine_start()` (real engine worker threads) |
| `toapp_alloc_guard_test.cpp` | `$<TARGET_FILE:capi_toapp_alloc_guard_test>` live name-selection (mallocnesia sidecar) |
| `public_roundtrip_test.cpp` | calls `fixpp_engine_start()` (real two-engine TCP loopback round-trip) |
| `message_field_iteration_test.cpp` | `$<TARGET_FILE:capi_message_field_iteration_test>` live name-selection (mallocnesia sidecar) + explicit `TIMEOUT 120` (heterogeneous property) |
| `dictionary_load_test.cpp` | genuinely concurrent (`std::thread` — "sequential + concurrent double-destroy (TSan)" per file header) |
| `dict066_group_loopback_smoke_test.cpp` | calls `fixpp_engine_start()`; label-heterogeneous sole `066;setup;scaffold;capi` |
| `dict066_group_membership_red_test.cpp` | calls `fixpp_engine_start()`; label-heterogeneous sole `066;us1;red;capi` |
| `dict066_nested_membership_red_test.cpp` | calls `fixpp_engine_start()`; label-heterogeneous sole `065;us1;red;capi`; explicit `TIMEOUT 60` |
| `dict066_scalar_as_group_test.cpp` | calls `fixpp_engine_start()`; label-heterogeneous sole `066;us2;capi` |
| `dict066_clone_identity_test.cpp` | calls `fixpp_engine_start()`; shares label `066;us1;capi` with the next row but real-engine-thread concern dominates |
| `dict066_clone_membership_copy_oom_test.cpp` | **global** `operator new`/`operator new[]` override (process-wide allocation-counting seam) — forces standalone regardless of the shared `066;us1;capi` label (D3 global-alloc-counting rule) |

**Sum:** 6 grouped + 22 standalone = **28** ✓ (100% dispositioned).

### `-R`/`-L` selectability (SC-004 / Scenario-3)

`ctest -R '^capi'` selects 31 entries both before (28 `.cpp`/`.c`-derived +
1 occupancy + 1 reentrancy + up to 5 mallocnesia sidecars, gated on
`libmallocnesia.so`) and after (23 executables — 6 folded into
`capi_pure_tests` + 22 unaffected standalone/script/sidecar entries — same
31 total once the 6 collapse to 1). `ctest -L capi` is unaffected: none of
the 6 grouped `.cpp` previously carried the `capi` label (verified — the
bucket carries none either), so the pre-existing `-L capi` set (20 entries,
spot-checked in the post-grouping run) is untouched. No historical
`-R`-by-target-name idiom targeted any of the 6 grouped names (`capi_smoke`,
`capi_version`, `capi_error_surface`, `capi_config_builders`,
`capi_thunk_split`, `capi_abi_symbol_golden` were never referenced by name in
a quickstart/tasks `-R` selector).

## Module: `config` (9 `.cpp`) — US2

044-toml-session-config / 045-config-logging TOML loader tests. Every `.cpp`
carries exactly one of two LABELS strings — `"config;044"` (5 members) or
`"config;045"` (4 members) — a clean D4 partition. No thread/global-alloc/
`int main(`/duplicate-`Suite.Name` anywhere in the module (grepped all 9).
`-R '^config'` matches nothing (the module's target names never started with
`config_` before this change) — `-L config` is the only selector that ever
worked here, per the orchestrator's module note; unaffected by grouping
(still 044 → 1 entry, 045 → 1 entry, both label-preserved).

### ODR pre-check (§3)

No `int main(` in any `.cpp`. No duplicate `TEST`/`TEST_F`/`TEST_P`
`Suite.Name` across the 9. Every file wraps its content in an anonymous
namespace; the only free functions found textually outside that span
(`test_load_logger_negative.cpp`: `neg_fixture`, `full_load`,
`parse_logger_inline`) are already declared `static` (internal linkage).
Zero FR-012 renames.

### Grouped

**Bucket `config_044_tests`** — 5 `.cpp`, label `"config;044"`, link
`fixpp::config_toml` + `fixpp::session` + `fixpp::core` + `fixpp::tls` +
`fixpp::dictionary` + `fixpp::transport` + OpenSSL/ZLIB + gtest (union —
`test_quickfix_parity_table.cpp` needs only gtest per its own "Pure data
test — no loader link needed" comment; the extra libs are a harmless no-op
for it):

| `.cpp` | decision | odr_action |
|---|---|---|
| `test_load_happy_path.cpp` | grouped:044 | none |
| `test_load_negative_battery.cpp` | grouped:044 | none |
| `test_load_selectors.cpp` | grouped:044 | none |
| `test_load_multisession_defaults.cpp` | grouped:044 | none |
| `test_quickfix_parity_table.cpp` | grouped:044 | none |

**Bucket `config_045_tests`** — 4 `.cpp`, label `"config;045"`, link the
044-bucket set + `fixpp::log` + `tomlplusplus::tomlplusplus` (+
`fixpp::log_otlp`/`FIXPP_CONFIG_HAS_OTLP` when `TARGET fixpp::log_otlp`
exists) + gtest, include `src/config` (private headers; needed by 3 of the 4
white-box members, harmless additive for `test_load_deferred_surface.cpp`):

| `.cpp` | decision | odr_action |
|---|---|---|
| `test_load_logger.cpp` | grouped:045 | none |
| `test_load_logger_negative.cpp` | grouped:045 | none (3 free helpers already `static`) |
| `test_load_logger_overrides.cpp` | grouped:045 | none |
| `test_load_deferred_surface.cpp` | grouped:045 | none |

### Standalone (0)

None — the entire module groups into 2 buckets.

**Sum:** 5 + 4 grouped = **9** ✓ (100% dispositioned).

### `-R`/`-L` selectability (SC-004 / Scenario-3)

`ctest -L config` selects 3 entries post-grouping (`config_044_tests`,
`config_045_tests`, plus the pre-existing unrelated
`transport_asio_plain_transport_config` — labeled `"config"` for a different
reason, untouched by this module); `-L 044` and `-L 045` each select exactly
1 entry, matching the pre-grouping 5-entry/4-entry sets collapsed into their
respective buckets. No historical `-R`-by-target-name idiom targeted any of
the 9 grouped names (`-R '^config'` was already documented as matching
nothing per the module's own note).

## Module: `sync` (46 `.cpp`) — US2

async_mutex concurrency-primitive test suite. `linux-clang-tsan` is this
module's **mandatory non-waivable gate** — the discriminator here is
genuinely-concurrent-vs-single-threaded, NOT label (the `add_sync_test`
helper sets no LABELS at all). Under TSan's default halt-on-first-race, one
race in a grouped process would abort before every OTHER test in that bucket
runs, silently blinding the gate for the rest — the same "binary-wide
WILL_FAIL masks a real finding" hazard the capi module's `send_recv` split
already defends against. So: **spawns a real OS thread → standalone,
regardless of link-deps/ENV homogeneity** (grepped
`std::thread|std::jthread|std::async|thread_pool` across all 46; every hit
stays standalone). Single-threaded-coroutine seams (driven entirely by
`ioc.run()`/`co_spawn` on the calling thread — the `session_pure_tests`/
`interop` `ParityAcceptorFixture` precedent) are the groupable set.

The shared `SYNC_FIXTURES_DIR`/`SYNC_GREP_GATE_SCRIPT` `ENVIRONMENT` the
`add_sync_test` helper sets on every registration is homogeneous across the
whole family (orchestrator note) and rides the grouped bucket via
`set_tests_properties(... ENVIRONMENT ...)`; almost none of the 29 grouped
`.cpp` actually reference those two env vars (only the CI-grep-gate driver
does, and it stays standalone for its own *extended*, heterogeneous
`ENVIRONMENT`), so it is a harmless no-op for the rest.

Verified per the advisor's flagged hazard: `async_mutex`'s `waiter_pool_storage_`
(`include/fixpp/core/sync/async_mutex.hpp:355`) is a **per-instance** member
(`std::array<waiter_pool_slot, waiter_pool_capacity_>`), not a process-global/
`static` pool — each test constructs its own `async_mutex mtx;`, so no
cross-test pool-state leakage is possible inside the grouped bucket.

### ODR pre-check (§3)

No `int main(` anywhere. No duplicate `TEST`/`TEST_F`/`TEST_P` `Suite.Name`
across all 46 `.cpp` (grouped + standalone). All 29 grouped `.cpp` wrap their
content in an anonymous namespace; the sole file-scope symbol found outside
that span is `fixpp_consumer_contract_link_probe()` in
`test_consumer_contract_compile.cpp` (non-`static`, `[[maybe_unused]]`,
never executed — a deliberate link-completeness probe) — verified unique
(grepped) across the bucket, so no rename needed (FR-012 only requires a
rename on an actual name collision). `g_constinit_mutex` in the same file is
inside its anonymous namespace (internal linkage). Zero FR-012 renames.

### Grouped

**Bucket `sync_pure_tests`** — 29 `.cpp`, no LABELS, link `fixpp_sync` +
gtest, include `tests/` + `SYNC_FIXTURES_DIR`, `ENVIRONMENT`
`SYNC_FIXTURES_DIR=...;SYNC_GREP_GATE_SCRIPT=...` (the `add_sync_test`
baseline, unioned onto the bucket):

| `.cpp` | decision | odr_action |
|---|---|---|
| `test_uncontended_latency.cpp` | grouped:pure | none |
| `test_contended_latency.cpp` | grouped:pure | none |
| `test_fifo_fairness.cpp` | grouped:pure | none |
| `test_fifo_across_cycles.cpp` | grouped:pure | none |
| `test_contention_stress.cpp` | grouped:pure | none |
| `test_executor_compat.cpp` | grouped:pure | none |
| `test_dispatch_vs_post.cpp` | grouped:pure | none |
| `test_cross_strand_acquire.cpp` | grouped:pure | none |
| `test_guard_destructive_move.cpp` | grouped:pure | none |
| `test_unlock_reaper_splice.cpp` | grouped:pure | none |
| `test_residual_cancel_graceful.cpp` | grouped:pure | none |
| `test_tsan_clean.cpp` | grouped:pure (coroutine-only; "TSan-clean" is a PRESET property, not a per-test toggle) | none |
| `test_asan_clean.cpp` | grouped:pure (same as above, ASan) | none |
| `test_halo_firing.cpp` | grouped:pure | none |
| `test_pmr_fallback.cpp` | grouped:pure | none |
| `test_slot_allocator_storage.cpp` | grouped:pure | none |
| `test_cancel_and_drain.cpp` | grouped:pure | none |
| `test_cancel_and_drain_concurrent.cpp` | grouped:pure (name notwithstanding — coroutine-interleaved, no OS thread) | none |
| `test_drain_latch_holder_lifecycle.cpp` | grouped:pure | none |
| `test_in_flight_acquirer_coverage.cpp` | grouped:pure | none |
| `test_drain_awaitable_cancellation.cpp` | grouped:pure | none |
| `test_drain_strand_local_reap.cpp` | grouped:pure | none |
| `test_drain_immediate_destroy_after_reap.cpp` | grouped:pure | none |
| `test_drain_reentrant_during_active.cpp` | grouped:pure | none |
| `test_drain_onstrand_cancel_during_reap.cpp` | grouped:pure | none |
| `test_drain_predrain_holder.cpp` | grouped:pure | none |
| `test_async_mutex_layout_golden.cpp` | grouped:pure (pure `sizeof`/`alignof` static assertions) | none |
| `test_consumer_contract_compile.cpp` | grouped:pure (compile-only static_assert/SFINAE TU) | none (`fixpp_consumer_contract_link_probe` verified unique) |
| `test_atomic_shared_ptr_primitive.cpp` | grouped:pure (single-threaded correctness of the primitive type) | none |

### Standalone (17)

| `.cpp` | reason |
|---|---|
| `test_async_mutex_aba_interleave.cpp` | zero-`fixpp_sync`-linkage ODR isolation (`FIXPP_ASYNC_MUTEX_TEST_SEAM` alters class layout) + genuinely concurrent + documented-legitimate pre-fix TSan race |
| `test_async_mutex_terminal_cas_recursive_unlock.cpp` | zero-linkage ODR isolation + genuinely concurrent + `TIMEOUT 60` |
| `test_async_mutex_acquire_livelock.cpp` | zero-linkage ODR isolation + genuinely concurrent + `TIMEOUT 60` |
| `test_async_mutex_chain_walk_cas_loss.cpp` | zero-linkage ODR isolation + genuinely concurrent + `TIMEOUT 60` |
| `test_pool_exhaustion_reuse.cpp` | zero-linkage ODR isolation (per-target `FIXPP_ASYNC_MUTEX_TEST_SEAM` compile-def) |
| `test_am_p3_impossible_state_traps.cpp` | zero-linkage ODR isolation + fork-based `EXPECT_DEATH` |
| `test_async_mutex_mt_hammer.cpp` | genuinely concurrent (OS-thread hammer) + heterogeneous `TIMEOUT` (120/300 under coverage) + conditional compile-def |
| `test_result_write_race.cpp` | genuinely concurrent (`std::thread`) |
| `test_cancellation_mid_wait.cpp` | genuinely concurrent (`std::thread`) |
| `test_race_cancel_pre_drain.cpp` | genuinely concurrent (`std::thread`) |
| `test_race_multi_cancel.cpp` | genuinely concurrent (`std::thread`) |
| `test_race_cancel_during_resume.cpp` | genuinely concurrent (`std::thread`) |
| `test_destructor_release_death.cpp` | fork-based `EXPECT_DEATH` + release-linkage override |
| `test_drain_destroy_inflight_mt.cpp` | genuinely concurrent (cross-executor `std::thread`) |
| `test_arm64_weak_memory.cpp` | genuinely concurrent (`std::thread`) + heterogeneous `TIMEOUT 120` |
| `test_atomic_shared_ptr_concurrency.cpp` | genuinely concurrent (`std::thread`) |
| `test_no_std_mutex_ci_gate.cpp` | heterogeneous `ENVIRONMENT` (extends the baseline with `SYNC_GATE_INC`/`SYNC_ASIO_INC`, unique to this seam) |

**Sum:** 29 grouped + 17 standalone = **46** ✓ (100% dispositioned). Two
bash-script `add_test` entries (`check_no_std_mutex_corpus`,
`census_no_raw_atomic_shared_ptr` + its mutation twin) plus the
`FIXPP_LIBCXX_LANE`-gated `check_no_std_mutex_corpus_libcxx`/
`check_libcxx_active` are not `.cpp`/gtest binaries — outside grouping scope,
unchanged.

### `-R`/`-L` selectability (SC-004 / Scenario-3)

`ctest -R '^sync_'` selects 19 entries post-grouping (1 grouped bucket + 17
standalone `sync_*`-prefixed tests, keyed exactly as before, + the
pre-existing unrelated `sync_alloc_guard_test_mallocnesia`) versus 46
`sync_*` + 1 unrelated pre-grouping — same case SET, fewer *entries*, per
the procedure's whole-binary-`add_test` design. No LABELS existed on any
`add_sync_test` registration before or after, so no `-L` selector is
affected. No historical `-R`-by-target-name idiom targeted any of the 29
grouped names (the module's own header documents `-R '^sync_'` as the only
supported selector, and that prefix is preserved verbatim by every
standalone entry; the grouped bucket's own name, `sync_pure_tests`, also
matches `^sync_`).

## Module: `transport` (20 `.cpp`) — US2 — 0 grouped (audited, no viable bucket)

**Expectation-gap note:** the module brief predicted "rich per-test labels
— keep buckets strictly label-homogeneous; likely several small
label-buckets." The full audit found **zero** duplicate-label pairs — see
the census below — so the outcome is 0 grouped / 20 standalone, not an
omission. `CMakeLists.txt` is **unchanged** (no CMake edit this module);
this section documents the full per-file audit and the ctest baseline run.

### Census

17 of the 20 `.cpp` carry a `LABELS` property via `set_tests_properties`;
every one of those 17 strings is **unique** (verified: `sort | uniq -c` over
the `LABELS "..."` lines emits count `1` for each of the 17 — no two
`transport_*` tests share a label class, so every labeled test is a D4
bucket-of-one → standalone regardless of mechanism).

The remaining 3 `.cpp` carry **no** `LABELS` at all (a shared empty-label
class, the only possible D4 partition in this module):
`transport_smoke_test.cpp`, `test_transport_factory_paths.cpp`,
`test_asio_tls_transport_error_paths.cpp`. Grepped all 3 for socket/thread
usage:

- `test_transport_factory_paths.cpp` and `test_asio_tls_transport_error_paths.cpp`
  both bind real `asio::ip::tcp::acceptor`s on `127.0.0.1:0` and
  `async_connect()` real client sockets (`MakeAcceptedAdoptsRealAcceptedSocketAndHandshakes`,
  `ConnectRefusedMapsToTransportConnectRefused`,
  `HandshakeTimeoutMapsToTransportHandshakeTimeout`, etc.) — real-socket
  live-I/O tests, forced standalone per the orchestrator's explicit
  "Socket/port/thread tests stay standalone" module note (same conservative
  rule applied to interop's TLS live cells).
- `transport_smoke_test.cpp` (pure `static_assert` slot-range check, no
  socket/thread) is the lone survivor of the no-label class — a bucket-of-1
  yields no disk win (D4), so it stays standalone too. No CMake change
  needed for it.

Grepped all 20 `.cpp` for `std::thread|std::jthread|std::async|thread_pool`:
2 hits (`test_listener_acceptor.cpp`, `test_transport_factory_cert_source_publish_acquire.cpp`)
— both already forced standalone by unique-label D4 (and the latter also
carries a heterogeneous `ENVIRONMENT` `TSAN_OPTIONS=...` override — the
procedure's explicit "heterogeneous per-test ... TSAN_OPTIONS ..." STANDALONE
criterion). `test_tls_validation_failed_taxonomy.cpp` links `GTest::gtest`
**without** `GTest::gtest_main` (i.e. brings its own `main()`) — reinforcing
that it could never have joined a `gtest_main`-linked bucket even absent its
unique label. No duplicate `TEST`/`TEST_F`/`TEST_P` `Suite.Name`; no global
`operator new`/`set_new_handler` anywhere in the module.

### Grouped

None.

### Standalone (20)

| `.cpp` | reason |
|---|---|
| `transport_smoke_test.cpp` | no-label class, but the sole survivor after the other 2 no-label members were disqualified by the real-socket rule — bucket-of-1, no win (D4) |
| `test_tls_handshake_pinset_rotation.cpp` | label-heterogeneous, sole `012;us1;seam7;tls` |
| `test_durable_before_transmit_ordering.cpp` | label-heterogeneous, sole `012;us1;seam8` |
| `test_compid_tls_identity_binding.cpp` | label-heterogeneous, sole `012;us1;seam9;tls` |
| `test_inflight_exclusivity.cpp` | label-heterogeneous, sole `012;us1;seam15;gate-b-witness;rc-e` |
| `test_reconnect_policy_schedule.cpp` | label-heterogeneous, sole `012;us2;seam13;reconnect` |
| `test_backpressure.cpp` | label-heterogeneous, sole `012;us4;seam10;mock` |
| `test_listener_acceptor.cpp` | label-heterogeneous, sole `012;us3;seam14;gate-b-witness;rc-a`; also genuinely concurrent (`std::thread`) and real-socket loopback |
| `test_cancellation_propagation.cpp` | label-heterogeneous, sole `012;us1;seam5` |
| `test_listener_accept_error_cancel.cpp` | label-heterogeneous, sole `012;us3;seam14;accept-error;cancel` |
| `test_transport_factory_paths.cpp` | no-label class, but real-socket (`tcp::acceptor`/`async_connect` on `127.0.0.1`) — forced standalone per module's socket/port rule |
| `test_load_credentials_seam13_witness.cpp` | label-heterogeneous, sole `012;gate-b-witness;seam13;tls` |
| `test_close_truncated_mapping.cpp` | label-heterogeneous, sole `012;gate-b-witness;rc-d;sc006` |
| `test_tls_validation_failed_taxonomy.cpp` | label-heterogeneous, sole `013;us3;tls;taxonomy;gate-b-witness`; explicit `TIMEOUT 60`; own `main()` (no `GTest::gtest_main` link) |
| `test_verify_peer_pmr_oom.cpp` | label-heterogeneous, sole `012;gate-b-witness;rc-c;pmr-oom;tls` |
| `test_asio_tls_transport_error_paths.cpp` | no-label class, but real-socket (`tcp::acceptor`/`async_connect` on `127.0.0.1`) — forced standalone per module's socket/port rule |
| `test_asio_plain_transport.cpp` | label-heterogeneous, sole `043;us1;sc001;plain`; explicit `TIMEOUT 60`; real-socket loopback (live-I/O witness) |
| `test_asio_plain_transport_config.cpp` | label-heterogeneous, sole `043;us1;sc008;plain;config`; explicit `TIMEOUT 60`; real-socket loopback |
| `test_transport_factory_cert_source_publish_acquire.cpp` | label-heterogeneous, sole `046;nfr017;publish_acquire`; genuinely concurrent (`std::thread` writer/reader); heterogeneous `ENVIRONMENT` `TSAN_OPTIONS=...`; explicit `TIMEOUT 30` |
| `test_transport_forced_fallback_link.cpp` | label-heterogeneous, sole `046;nfr017;fallback_link` |

**Sum:** 0 grouped + 20 standalone = **20** ✓ (100% dispositioned).

### `-R`/`-L` selectability (SC-004 / Scenario-3)

No change — `CMakeLists.txt` is byte-identical to pre-068. `ctest -L
transport` baseline run (post-068, unchanged file): 20/20 pass, matching the
20 `.cpp` census 1:1 (`Label Time Summary` shows every `012`/`013`/`043`/`046`
sub-label at its pre-existing count). No `-R`-by-target-name idiom applies
(nothing changed).

## Module: `core` (25 `.cpp`) — US2

Two `gtest_discover_tests` binaries (`fixpp_core_tests`, `fixpp_capi_tests`)
predate 068 and already grouped their member `.cpp` at the *per-case*
granularity — the pattern superseded by the procedure's whole-binary
`add_test` (per-case discovery = 5.77× serial ctest regression, per
IMPLEMENTATION-PROCEDURE.md). Converted both to whole-binary `add_test(NAME
<t> COMMAND <t>)`; removed `include(GoogleTest)` (dead after the conversion —
nothing else in this file used `gtest_discover_tests`). Neither target
carried any LABELS before the conversion, so none were lost. The pre-existing
standalone `core_smoke_test.cpp` (also no LABELS, identical link-deps —
`fixpp_core` + gtest — to `fixpp_core_tests`) was folded into the
`fixpp_core_tests` bucket as the "remaining standalone-but-groupable pure
core test" per the module brief; its separate `add_executable`/
`gtest_discover_tests` registration was removed.

### ODR pre-check (§3)

No `int main(` in any of the 8 `fixpp_core_tests` sources or the 3
`fixpp_capi_tests` sources. No duplicate `TEST(Suite,Name)` across either
bucket (verified: `CoreSmoke`, `DecimalParse`, `DecimalFormat`,
`DecimalCompare`, `DecimalRoundtrip`, `DecimalCrossTraits`, `DecimalAlias`,
`DecimalPredicates`/`DecimalTrapThrow`/`DecimalToPod`/`DecimalFromPod` suites
in `fixpp_core_tests`; `DecimalCABILayout`/`DecimalCABIChecked`,
`DecimalCAPIErrorPaths`, `DecimalReservedTolerance` in `fixpp_capi_tests` —
all unique prefixes, no overlap). `decimal_cross_traits_test.cpp` opens
`namespace fixpp::core::test { struct decimal_wide {...}; }` and injects an
explicit `decimal_traits<test::decimal_wide>` specialization at namespace
scope (not `static`/anon-ns) — safe because `decimal_wide` is a type defined
only in this TU (no other bucket member names or specializes it), so no ODR
collision. `decimal_parse_test.cpp`'s `parse()`,
`decimal_format_test.cpp`'s `fmt()`, and
`decimal_roundtrip_property_test.cpp`'s `check_roundtrip()` are all inside
anonymous namespaces (internal linkage) — no collision. Shared header
`tests/support/mock_decimal_traits.hpp` (used only by
`decimal_cross_traits_test.cpp` in this module) is `#pragma once` +
header-only. Zero FR-012 renames.

### Grouped

**Bucket `fixpp_core_tests`** — 8 `.cpp` (converted `gtest_discover_tests` →
whole-binary `add_test`; `core_smoke_test.cpp` folded in), no LABELS, link
`fixpp_core` + gtest, include `tests/support` (needed by
`decimal_cross_traits_test.cpp`; harmless additive for the other 7):

| `.cpp` | decision | odr_action |
|---|---|---|
| `core_smoke_test` | grouped:pure (folded in from standalone `gtest_discover_tests`) | none |
| `decimal_parse_test` | grouped:pure (gtest_discover→add_test) | none (anon-ns `parse()`) |
| `decimal_format_test` | grouped:pure (gtest_discover→add_test) | none (anon-ns `fmt()`) |
| `decimal_compare_test` | grouped:pure (gtest_discover→add_test) | none |
| `decimal_roundtrip_property_test` | grouped:pure (gtest_discover→add_test) | none (anon-ns `check_roundtrip()`) |
| `decimal_cross_traits_test` | grouped:pure (gtest_discover→add_test) | none (TU-local `decimal_wide` specialization) |
| `decimal_alias_test` | grouped:pure (gtest_discover→add_test) | none |
| `decimal_predicates_test` | grouped:pure (gtest_discover→add_test) | none |

**Bucket `fixpp_capi_tests`** — 3 `.cpp` (converted `gtest_discover_tests` →
whole-binary `add_test`), no LABELS, link `fixpp_capi` + gtest:

| `.cpp` | decision | odr_action |
|---|---|---|
| `decimal_capi_layout_test` | grouped:pure (gtest_discover→add_test) | none |
| `decimal_capi_error_test` | grouped:pure (gtest_discover→add_test) | none |
| `decimal_reserved_tolerance_test` | grouped:pure (gtest_discover→add_test) | none |

### Standalone (14)

| `.cpp` | reason |
|---|---|
| `test_session_executor_round_trip` (`threading_session_executor_round_trip`) | threading_* seam — kept standalone per module brief (isolation-sensitive TSan-suppressed seam family) |
| `test_mock_clock_determinism` (`threading_mock_clock_determinism`) | threading_* seam, per-test `TSAN_OPTIONS` ENV via `add_threading_core_test` |
| `test_sleep_cancel_race` (`threading_sleep_cancel_race`) | threading_* seam (TSan+ASan), per-test `TSAN_OPTIONS` ENV |
| `test_engine_shutdown_order` (`threading_engine_shutdown_order`) | threading_* seam, per-test `TSAN_OPTIONS` ENV |
| `test_third_party_clock_conformance` (`threading_third_party_clock_conformance`) | threading_* seam, per-test `TSAN_OPTIONS` ENV |
| `test_trace_context_resume` (`threading_trace_context_resume`) | threading_* seam, per-test `TSAN_OPTIONS` ENV |
| `test_session_local_lifetime` (`threading_session_local_lifetime`) | threading_* seam (TSan), per-test `TSAN_OPTIONS` ENV |
| `test_session_executor_accessor_survives_erasure` (`threading_session_executor_accessor_survives_erasure`) | threading_* seam, per-test `TSAN_OPTIONS` ENV |
| `test_trace_context_engine_fallback` (`threading_trace_context_engine_fallback`) | threading_* seam, per-test `TSAN_OPTIONS` ENV |
| `test_017_error_completeness` (`error_017_completeness`) | exact-set completeness gate |
| `test_019_error_completeness` (`error_019_completeness`) | exact-set completeness gate, label-heterogeneous (`019;foundational;error_slots`) |
| `test_020_error_completeness` (`error_020_completeness`) | exact-set completeness gate, label-heterogeneous (`020;foundational;error_slots`) |
| `decimal_compare_diff_oracle_test` | live `ctest -R decimal_compare_diff_oracle` name-selection idiom (own executable, not `gtest_discover_tests`, by design per its own header comment) |
| `decimal_mul_u64_wide_test` | per-`-D` compile variants of one `.cpp` (`decimal_mul_u64_wide` / `decimal_mul_u64_wide_portable`, `FIXPP_DECIMAL_FORCE_PORTABLE_MUL`) — counted once (1 `.cpp` file, 2 targets) |

**Sum:** 8 + 3 grouped (11) + 14 standalone = **25** ✓ (100% dispositioned).

### `-R`/`-L` selectability (SC-004 / Scenario-3)

Neither `fixpp_core_tests` nor `fixpp_capi_tests` (nor the folded-in
`core_smoke_test`) ever carried a `LABELS` property, so no `-L` selector is
affected. `gtest_discover_tests` previously registered these 11 `.cpp` as
84 + 30 = 114 individually-addressable `ctest -R <Suite.Case>` entries (spot-
verified via `--gtest_list_tests`: `fixpp_core_tests` lists 84 cases,
`fixpp_capi_tests` 30) — under whole-binary `add_test` those per-case `-R`
selectors no longer resolve as separate ctest entries (the same expected
Scenario-3 tradeoff as every other grouped bucket in this ledger); the
equivalent replacement is running the 2 named binaries directly
(`ctest -R '^fixpp_core_tests$'` / `^fixpp_capi_tests$'`) or via
`--gtest_filter` against the binary. No pre-existing `-L`/`-R`-by-target-name
idiom in any quickstart/tasks doc targeted individual cases inside these two
binaries (they were always referenced as `fixpp_core_tests`/
`fixpp_capi_tests` at the module level). `threading_*`'s 9 standalone targets,
the 3 completeness gates, and the 2 `decimal_mul_u64_wide*`/
`decimal_compare_diff_oracle` live-name idioms are all unaffected (unchanged
target names/properties).
