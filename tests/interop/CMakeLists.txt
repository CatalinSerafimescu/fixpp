# tests/interop/CMakeLists.txt — 016-interop-harness (T002)
#
# Session-layer interop gate. This subtree IS the test deliverable (FR-026):
# happy/ drivers + goldens, thorny/ corpus, parity/ witnesses, support/ machinery.
# Orchestration (counterparty spawn, capture proxy, badge emit) lives in the
# gitignored parent ../phase-9-harness/ — NOT here (contract:
# parent-harness-gate-contract.md).
find_package(GTest CONFIG REQUIRED)

# OpenSSL/ZLIB linkage helper for TLS-logon cells. fixpp_transport exposes asio::ssl
# PRIVATEly, so executables linking the static libs must repeat the OpenSSL link
# (same pattern as tests/transport/CMakeLists.txt).
find_package(OpenSSL 3.0 REQUIRED)
find_package(ZLIB REQUIRED)
find_library(FIXPP_INTEROP_OPENSSL_CRYPTO_LIB NAMES crypto libcrypto
    PATHS "${openssl_LIB_DIRS_RELEASE}" NO_DEFAULT_PATH)
find_library(FIXPP_INTEROP_OPENSSL_SSL_LIB NAMES ssl libssl
    PATHS "${openssl_LIB_DIRS_RELEASE}" NO_DEFAULT_PATH)
find_library(FIXPP_INTEROP_ZLIB_LIB NAMES z libz
    PATHS "${zlib_LIB_DIRS_RELEASE}" NO_DEFAULT_PATH)
if(NOT FIXPP_INTEROP_OPENSSL_CRYPTO_LIB OR FIXPP_INTEROP_OPENSSL_CRYPTO_LIB STREQUAL "FIXPP_INTEROP_OPENSSL_CRYPTO_LIB-NOTFOUND")
    set(FIXPP_INTEROP_OPENSSL_CRYPTO_LIB OpenSSL::Crypto)
endif()
if(NOT FIXPP_INTEROP_OPENSSL_SSL_LIB OR FIXPP_INTEROP_OPENSSL_SSL_LIB STREQUAL "FIXPP_INTEROP_OPENSSL_SSL_LIB-NOTFOUND")
    set(FIXPP_INTEROP_OPENSSL_SSL_LIB OpenSSL::SSL)
endif()
if(NOT FIXPP_INTEROP_ZLIB_LIB OR FIXPP_INTEROP_ZLIB_LIB STREQUAL "FIXPP_INTEROP_ZLIB_LIB-NOTFOUND")
    set(FIXPP_INTEROP_ZLIB_LIB ZLIB::ZLIB)
endif()

# ── interop_support — golden diff + descriptor + fixture machinery (T004/T005/T006) ──
# golden_diff.cpp is standard-library-only; the fixture (.cpp) brings up an Engine
# and so links the session/transport/tls stack.
add_library(interop_support STATIC
    support/golden_diff.cpp
    support/interop_fixture.cpp
)
target_include_directories(interop_support PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"            # consumers: #include "support/..."
)
target_link_libraries(interop_support PUBLIC
    fixpp_session
    fixpp_tls
    fixpp_transport
    GTest::gtest
    # fixpp_transport/_tls expose asio::ssl (OpenSSL) PRIVATEly, so every consumer
    # executable must repeat the OpenSSL/ZLIB link. PUBLIC ⇒ inherited transitively.
    "${FIXPP_INTEROP_OPENSSL_SSL_LIB}"
    "${FIXPP_INTEROP_OPENSSL_CRYPTO_LIB}"
    "${FIXPP_INTEROP_ZLIB_LIB}"
)
target_compile_definitions(interop_support PUBLIC
    FIXPP_TLS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/tls/fixtures"
)

# ── Aggregation targets the quickstart references (cmake --build --target interop_*) ──
# Empty at Phase 1 ("builds an empty suite"); per-phase tasks attach drivers via
# add_dependencies(interop_<group> <test_target>).
add_custom_target(interop_happy)
add_custom_target(interop_thorny)
add_custom_target(interop_parity)

# ── fixpp_add_interop_test — used by US1/US2/US3 phase tasks ──────────────────
# fixpp_add_interop_test(NAME <exe> GROUP <happy|thorny|parity> LABEL <ctest-label>
#                        SOURCES <src...> [TLS])
# TLS ⇒ link OpenSSL/ZLIB + define the fixture dir for tls-logon cells.
function(fixpp_add_interop_test)
    set(options TLS)
    set(oneValue NAME GROUP)
    # LABEL is multiValue: a quoted "a;b;c" passed to a oneValue keyword collapses
    # to just "a" (CMake flattens the list), so the ctest -L interop-happy / -parity
    # workflow (quickstart) would match nothing. multiValue preserves the full list.
    set(multiValue SOURCES LABEL EXTRA_LIBS)
    cmake_parse_arguments(IT "${options}" "${oneValue}" "${multiValue}" ${ARGN})

    add_executable(${IT_NAME} ${IT_SOURCES})
    target_link_libraries(${IT_NAME} PRIVATE
        interop_support
        ${IT_EXTRA_LIBS}   # e.g. fixpp_mock_clock for the parity witnesses
        GTest::gtest
        GTest::gtest_main
    )
    target_include_directories(${IT_NAME} PRIVATE
        "${CMAKE_SOURCE_DIR}/src"      # concrete transport/listener headers (tests-only)
        "${CMAKE_SOURCE_DIR}/tests"    # support/minimal_dictionary.hpp, loopback fixtures
    )
    # FIXPP_TEST_HOOKS exposes Session::seqnum_mgr_test_access() so cells can
    # assert sequence-number deltas (FR-007) — the same per-target test macro
    # tests/session/ uses. Test-only ([const §XV.9]).
    target_compile_definitions(${IT_NAME} PRIVATE FIXPP_TEST_HOOKS)
    if(IT_TLS)
        # Static-link ORDER matters: libfixpp_tls.a references OpenSSL symbols
        # (certificate.cpp: EVP_*/X509_*/ASN1_*/BIO_*) and must precede libcrypto
        # on the link line. interop_support re-exports the fixpp libs transitively,
        # which CMake places AFTER the directly-linked OpenSSL libs → undefined
        # refs. Listing the fixpp libs as DIRECT deps here, immediately before
        # OpenSSL, fixes the order (mirrors tests/transport/CMakeLists.txt).
        target_link_libraries(${IT_NAME} PRIVATE
            fixpp_session
            fixpp_transport
            fixpp_tls
            "${FIXPP_INTEROP_OPENSSL_SSL_LIB}"
            "${FIXPP_INTEROP_OPENSSL_CRYPTO_LIB}"
            "${FIXPP_INTEROP_ZLIB_LIB}"
        )
        target_compile_definitions(${IT_NAME} PRIVATE
            FIXPP_TLS_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/tls/fixtures"
        )
    endif()
    add_test(NAME ${IT_NAME} COMMAND ${IT_NAME})
    set_tests_properties(${IT_NAME} PROPERTIES LABELS "${IT_LABEL}")
    add_dependencies(interop_${IT_GROUP} ${IT_NAME})
endfunction()

# ── support smoke (T002 Phase-1 checkpoint: the suite configures + builds + runs) ──
# Runtime-validates the support layer: golden parse/diff + descriptor + probe token.
fixpp_add_interop_test(
    NAME interop_support_smoke_test
    GROUP happy
    LABEL "interop;016;interop-support"
    SOURCES support_smoke_test.cpp
)

# ── US1 happy-path drivers (T010, T016) ───────────────────────────────────────
# All-TLS baseline (FR-025): every cell links OpenSSL/ZLIB (TLS option). Live
# matrix cells skip-with-reason when the counterparty is absent (FR-023); the
# down-peer watchdog (T016) runs green locally (no counterparty — a deliberate
# never-accepting peer). Drivers include happy/hp_support.hpp (resolved relative
# to the source dir) + support/* (via the tests/interop include dir).
fixpp_add_interop_test(
    NAME interop_hp_fix44_logon_hb_logout_test
    GROUP happy
    LABEL "interop;016;interop-happy;us1;smoke"
    SOURCES happy/hp_fix44_logon_hb_logout_test.cpp
    TLS
)
fixpp_add_interop_test(
    NAME interop_hp_down_peer_stop_watchdog_test
    GROUP happy
    LABEL "interop;016;interop-happy;us1;down-peer;watchdog"
    SOURCES happy/hp_down_peer_stop_watchdog_test.cpp
    TLS
)
fixpp_add_interop_test(
    NAME interop_hp_fix44_testrequest_echo_test
    GROUP happy
    LABEL "interop;016;interop-happy;us1"
    SOURCES happy/hp_fix44_testrequest_echo_test.cpp
    TLS
)
fixpp_add_interop_test(
    NAME interop_hp_fix44_reject_invalid_admin_test
    GROUP happy
    LABEL "interop;016;interop-happy;us1"
    SOURCES happy/hp_fix44_reject_invalid_admin_test.cpp
    TLS
)
fixpp_add_interop_test(
    NAME interop_hp_fix44_seqnum_recovery_test
    GROUP happy
    LABEL "interop;016;interop-happy;us1;us3"
    SOURCES happy/hp_fix44_seqnum_recovery_test.cpp
    TLS
)
fixpp_add_interop_test(
    NAME interop_hp_fix44_recovery_outbound_answer_test
    GROUP happy
    LABEL "interop;018;interop-happy;us3"
    SOURCES happy/hp_fix44_recovery_outbound_answer_test.cpp
    TLS
)
fixpp_add_interop_test(
    NAME interop_hp_fix44_idle_heartbeat_cadence_test
    GROUP happy
    LABEL "interop;018;interop-happy;us2"
    SOURCES happy/hp_fix44_idle_heartbeat_cadence_test.cpp
    TLS
)
fixpp_add_interop_test(
    NAME interop_hp_fix44_disconnect_reconnect_noreset_test
    GROUP happy
    LABEL "interop;016;interop-happy;us1;reconnect"
    SOURCES happy/hp_fix44_disconnect_reconnect_noreset_test.cpp
    TLS
)

# ── US3 parity GAP-closure witnesses (T023-T025) ──────────────────────────────
# Standalone (no counterparty, no live transport): drive crafted admin frames
# through Session::on_inbound_frame() over the sync transport_send_ path and
# observe fixpp's recovery behavior. Run green locally (interop-parity label).
fixpp_add_interop_test(
    NAME interop_parity_resend_abort_on_failing_write_test
    GROUP parity
    LABEL "interop;016;interop-parity;us3"
    SOURCES parity/resend_abort_on_failing_write_test.cpp
    EXTRA_LIBS fixpp_mock_clock
)
fixpp_add_interop_test(
    NAME interop_parity_inbound_sequencereset_arms_test
    GROUP parity
    LABEL "interop;016;interop-parity;us3"
    SOURCES parity/inbound_sequencereset_arms_test.cpp
    EXTRA_LIBS fixpp_mock_clock
)
fixpp_add_interop_test(
    NAME interop_parity_replay_subsumes_reorder_queue_test
    GROUP parity
    LABEL "interop;016;interop-parity;us3"
    SOURCES parity/replay_subsumes_reorder_queue_test.cpp
    EXTRA_LIBS fixpp_mock_clock
)

# ── 024 live ResetOnLogon interop cells — T016 (initiator) + T017 (acceptor) ──
# Both cells value-parameterized over counterparty × role in one file.
# Cells skip-with-reason when no counterparty (FR-023); CI always green.
# Parent harness must: (T016) run QFcpp/QFJ acceptor with ResetOnLogon tolerant;
#   (T017) run QFcpp/QFJ initiator with ResetOnLogon=Y so it sends 141=Y + 34=1.
# Anchors: tasks.md T016/T017; contracts/reset-knobs.md C6.1/C6.2; FR-010/SC-005.
fixpp_add_interop_test(
    NAME interop_024_reset_on_logon_test
    GROUP happy
    LABEL "interop;024;interop-happy;polish;reset;reset-on-logon"
    SOURCES happy/hp_fix44_reset_on_logon_test.cpp
    TLS
)

# ── 026 live nanos-SendingTime interop cells — T018 (initiator) + T019 (acceptor) ──
# Both cells value-parameterized over counterparty × role in one file.
# Cells skip-with-reason when no counterparty (FR-023); CI always green.
# Parent harness must: (T018) configure the counterparty to tolerate a 27-char
#   nanos 52= from fixpp; (T019) configure the counterparty initiator to SEND
#   nanos 52= so fixpp's acceptor exercises the lenient parser.
# Anchors: tasks.md T018/T019; contracts/sendingtime-precision.md C7 (C7.1/C7.2);
#          quickstart.md "Live interop"; FR-007/SC-001/SC-003/SC-004/I-NST-5.
fixpp_add_interop_test(
    NAME interop_026_nanos_sendingtime_test
    GROUP happy
    LABEL "interop;026;interop-happy;polish;sending-time;nanos"
    SOURCES happy/hp_fix44_nanos_sendingtime_test.cpp
    TLS
)

# ── US2 thorny-corpus P1 witnesses (T020-T021) ────────────────────────────────
# Standalone provenance-linked replays of upstream closed-with-fix bugs; reuse the
# parity ParityAcceptorFixture (parity/parity_support.hpp) — no counterparty, no
# live transport. Each asserts fixpp's spec-conformant outcome (FR-018).
# Provenance + bucketing: thorny/CORPUS-INDEX.md.
fixpp_add_interop_test(
    NAME interop_thorny_qfj750_logout_seqnum_mismatch_test
    GROUP thorny
    LABEL "interop;016;interop-thorny;us2;recovery"
    SOURCES thorny/recovery/qfj-750-logout-seqnum-mismatch_test.cpp
    EXTRA_LIBS fixpp_mock_clock
)
fixpp_add_interop_test(
    NAME interop_thorny_qfj603_unsupported_beginstring_test
    GROUP thorny
    LABEL "interop;016;interop-thorny;us2;framing"
    SOURCES thorny/framing/qfj-603-unsupported-beginstring_test.cpp
    EXTRA_LIBS fixpp_mock_clock
)
fixpp_add_interop_test(
    NAME interop_thorny_qfj721_non_logon_first_message_test
    GROUP thorny
    LABEL "interop;016;interop-thorny;us2;framing"
    SOURCES thorny/framing/qfj-721-non-logon-first-message_test.cpp
    EXTRA_LIBS fixpp_mock_clock
)
fixpp_add_interop_test(
    NAME interop_thorny_qfj271_sequencereset_large_gapfill_test
    GROUP thorny
    LABEL "interop;016;interop-thorny;us2;recovery"
    SOURCES thorny/recovery/qfj-271-sequencereset-large-gapfill_test.cpp
    EXTRA_LIBS fixpp_mock_clock
)

# ── Gate-B/r1 thorny witnesses: C-101 (qfj#626) + C-102 (qfj#557) ───────────
fixpp_add_interop_test(
    NAME interop_thorny_qfj626_resend_recomputes_checksum_test
    GROUP thorny
    LABEL "interop;016;interop-thorny;us2;recovery"
    SOURCES thorny/recovery/qfj-626-resend-recomputes-checksum_test.cpp
    EXTRA_LIBS fixpp_mock_clock
)
fixpp_add_interop_test(
    NAME interop_thorny_qfj557_generatereject_advances_seqnum_test
    GROUP thorny
    LABEL "interop;016;interop-thorny;us2;reject"
    SOURCES thorny/reject/qfj-557-generatereject-advances-seqnum_test.cpp
    EXTRA_LIBS fixpp_mock_clock
)

# ── 021 live PossDup interop cells (T006 + T009) — both roles × both engines ──
# Live cells: skip-with-reason when no counterparty (FR-023); gate-bite negative
# tests run green locally (no counterparty required — standalone golden diff).
# Normalization profile: {52,122,10} per quickstart §2 (poss_dup_profile_excluded_tags()).
# Anchors: tasks.md T006/T009; contracts/session-possdup.md C1; quickstart.md §2.
fixpp_add_interop_test(
    NAME interop_021_poss_dup_replay_survives_test
    GROUP thorny
    LABEL "interop;021;interop-thorny;us1;recovery;poss-dup"
    SOURCES thorny/recovery/021-poss-dup-replay-survives_test.cpp
    TLS
)
fixpp_add_interop_test(
    NAME interop_021_poss_dup_malformed_dup_rejected_test
    GROUP thorny
    LABEL "interop;021;interop-thorny;us2;recovery;poss-dup"
    SOURCES thorny/recovery/021-poss-dup-malformed-dup-rejected_test.cpp
    TLS
)

# ── 022 live AllowPosDup + PossResend interop cells (T011 + T012) ─────────────
# Live cells: skip-with-reason when no counterparty (FR-023); gate-bite negative
# tests run green locally (no counterparty required — standalone golden diff).
# T011 (AllowPosDup wire-capture): default allow_pos_dup==false strips caller
#   43/122; counterparty accepts the stripped frame; session stays Active.
#   Normalization profile: {52,10} (admin profile).
#   Category: thorny/framing (send-path strip is a framing-layer concern).
# T012 (PossResend deliver): QF counterparty sends 97=Y business message;
#   fixpp delivers it to fromApp; session stays Active (witness-only, D4).
#   Normalization profile: {52,10} (admin profile).
#   Category: thorny/recovery (in-sequence delivery, not a strip concern).
# Anchors: tasks.md T011/T012; contracts/session-send-possdup.md §C5;
#          quickstart.md "Live interop cells"; 022 FR-010/SC-005.
fixpp_add_interop_test(
    NAME interop_022_allow_pos_dup_strip_send_test
    GROUP thorny
    LABEL "interop;022;interop-thorny;polish;framing;allow-pos-dup"
    SOURCES thorny/framing/022-allow-pos-dup-strip-send_test.cpp
    TLS
)
fixpp_add_interop_test(
    NAME interop_022_poss_resend_deliver_test
    GROUP thorny
    LABEL "interop;022;interop-thorny;polish;recovery;poss-resend"
    SOURCES thorny/recovery/022-poss-resend-deliver_test.cpp
    TLS
)

# ── US2/US3 live business-message interop (020 T012/T014/T016) ───────────────
# Both-role NOS→ExecRpt round-trip vs QuickFIX-J and QuickFIX-cpp.
# All 4 cells skip-with-reason when no counterparty (FR-023); CI always green.
# Needs the generated fixpp::v44 Messages.hpp (codegen 003) for the typed read
# flyweights + fixpp_dictionary for decode_field. Mirror the dictionary reify
# test pattern (add_dependencies + codegen include dir).
if(TARGET fixpp_codegen_generate)
    fixpp_add_interop_test(
        NAME interop_business_message_interop_test
        GROUP happy
        LABEL "interop;020;interop-happy;us2;us3;business-message"
        SOURCES test_business_message_interop.cpp
        TLS
        EXTRA_LIBS fixpp_dictionary
    )
    set(_BM_INTEROP_CODEGEN_INC "${CMAKE_BINARY_DIR}/_codegen/include")
    add_dependencies(interop_business_message_interop_test fixpp_codegen_generate)
    target_include_directories(interop_business_message_interop_test PRIVATE
        "${_BM_INTEROP_CODEGEN_INC}"
    )
    set_tests_properties(interop_business_message_interop_test PROPERTIES
        TIMEOUT 30)
else()
    message(STATUS
        "tests/interop: fixpp_codegen_generate not available; skipping business message interop test")
endif()

# ── US4 cell_result manifest schema-check (T028) ──────────────────────────────
# Validates cell_results.yaml against the parent gate-evaluator's result schema
# (contracts/parent-harness-gate-contract.md): per-cell completeness, skip vs
# deferred disambiguation, corpus P1 block rule. Pure-Python (PyYAML) — no
# instrumented .so, so it runs under every preset (unlike the decimal oracle).
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(
    NAME interop_cell_results_schema_check
    COMMAND ${Python3_EXECUTABLE} -m pytest -xq
            "${CMAKE_CURRENT_SOURCE_DIR}/cell_results_schema_check_test.py"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(interop_cell_results_schema_check PROPERTIES
    LABELS "interop;016;interop-thorny;us4;schema-check"
)
