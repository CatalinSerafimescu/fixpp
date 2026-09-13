// tests/config/test_quickfix_parity_table.cpp
//
// T030 — QuickFIX-cpp vocabulary parity table (SC-004).
//
// Purpose: assert that every QuickFIX-cpp session-establishment setting key
// (enumerated from the cloned reference source — NOT from memory) has an
// explicit disposition in the 044 loader: either a concrete 044 key, or a
// documented out-of-scope / deferred rationale.
//
// Source authority: reference-engines/quickfix-cpp/include/quickfix/SessionSettings.h
// (keys at lines 36–239; const char[] definitions in namespace FIX).
// SessionFactory.cpp (session-establishment consumption) also consulted for
// which keys are live establishment keys vs. ancillary.
//
// Completeness assertion: kParity must cover kQfEstablishmentKeys *exactly*
// (no missing row, no extra row) — matching the anti-pattern warning in
// [[feedback_completeness_gate_exact_set_not_subset]].
//
// Mapping check: every Mapped row's 044 target must appear in
// kLoaderAccepted044Keys (source: scalar_mappers.cpp / selector_resolver.cpp)
// to catch typos or invented keys.

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {

// ── QuickFIX key source citations ────────────────────────────────────────────
// Every key below cites the specific line in:
//   reference-engines/quickfix-cpp/include/quickfix/SessionSettings.h
// where its const char[] definition appears.

struct QfKey {
    std::string_view name;
    int source_line;  // SessionSettings.h line number
};

// All session-establishment const char[] keys from SessionSettings.h (36–239).
// Grouped for readability; the completeness test uses the full flat set.
// clang-format off
const std::vector<QfKey> kQfAllKeys = {
    // ── Session identity ──────────────────────────────────────────────────────
    { .name="BeginString",                      .source_line=36  }, // SessionSettings.h:36
    { .name="SenderCompID",                     .source_line=37  }, // SessionSettings.h:37
    { .name="TargetCompID",                     .source_line=38  }, // SessionSettings.h:38
    { .name="SessionQualifier",                 .source_line=39  }, // SessionSettings.h:39
    { .name="DefaultApplVerID",                 .source_line=40  }, // SessionSettings.h:40
    { .name="ConnectionType",                   .source_line=41  }, // SessionSettings.h:41

    // ── Protocol flags ────────────────────────────────────────────────────────
    { .name="UseDataDictionary",                .source_line=42  }, // SessionSettings.h:42
    { .name="SendResetSeqNumFlag",              .source_line=43  }, // SessionSettings.h:43
    { .name="SendRedundantResendRequests",      .source_line=44  }, // SessionSettings.h:44
    { .name="SendNextExpectedMsgSeqNum",        .source_line=45  }, // SessionSettings.h:45

    // ── Dictionary paths ──────────────────────────────────────────────────────
    { .name="DataDictionary",                   .source_line=46  }, // SessionSettings.h:46
    { .name="TransportDataDictionary",          .source_line=47  }, // SessionSettings.h:47
    { .name="AppDataDictionary",                .source_line=48  }, // SessionSettings.h:48

    // ── Time / clock ──────────────────────────────────────────────────────────
    { .name="UseLocalTime",                     .source_line=49  }, // SessionSettings.h:49
    { .name="StartTime",                        .source_line=50  }, // SessionSettings.h:50
    { .name="EndTime",                          .source_line=51  }, // SessionSettings.h:51
    { .name="StartDay",                         .source_line=52  }, // SessionSettings.h:52
    { .name="EndDay",                           .source_line=53  }, // SessionSettings.h:53
    { .name="NonStopSession",                   .source_line=54  }, // SessionSettings.h:54
    { .name="LogonTime",                        .source_line=55  }, // SessionSettings.h:55
    { .name="LogoutTime",                       .source_line=56  }, // SessionSettings.h:56
    { .name="LogonDay",                         .source_line=57  }, // SessionSettings.h:57
    { .name="LogoutDay",                        .source_line=58  }, // SessionSettings.h:58

    // ── Validation ────────────────────────────────────────────────────────────
    { .name="CheckCompID",                      .source_line=59  }, // SessionSettings.h:59
    { .name="CheckLatency",                     .source_line=60  }, // SessionSettings.h:60
    { .name="MaxLatency",                       .source_line=61  }, // SessionSettings.h:61
    { .name="HeartBtInt",                       .source_line=62  }, // SessionSettings.h:62

    // ── Socket / network ─────────────────────────────────────────────────────
    { .name="SocketAcceptPort",                 .source_line=63  }, // SessionSettings.h:63
    { .name="SocketReuseAddress",               .source_line=64  }, // SessionSettings.h:64
    { .name="SocketConnectHost",                .source_line=65  }, // SessionSettings.h:65
    { .name="SocketConnectPort",                .source_line=66  }, // SessionSettings.h:66
    { .name="SocketConnectSourceHost",          .source_line=67  }, // SessionSettings.h:67
    { .name="SocketConnectSourcePort",          .source_line=68  }, // SessionSettings.h:68
    { .name="SocketNodelay",                    .source_line=69  }, // SessionSettings.h:69
    { .name="SocketSendBufferSize",             .source_line=70  }, // SessionSettings.h:70
    { .name="SocketReceiveBufferSize",          .source_line=71  }, // SessionSettings.h:71
    { .name="HostSelectionPolicy",              .source_line=72  }, // SessionSettings.h:72
    { .name="HostSelectionPolicyPriorityStartOverInterval", .source_line=73 }, // SessionSettings.h:73
    { .name="ReconnectInterval",                .source_line=74  }, // SessionSettings.h:74

    // ── Wire validation flags ─────────────────────────────────────────────────
    { .name="ValidateLengthAndChecksum",        .source_line=75  }, // SessionSettings.h:75
    { .name="ValidateFieldsOutOfOrder",         .source_line=76  }, // SessionSettings.h:76
    { .name="ValidateFieldsHaveValues",         .source_line=77  }, // SessionSettings.h:77
    { .name="ValidateUserDefinedFields",        .source_line=78  }, // SessionSettings.h:78
    { .name="AllowUnknownMsgFields",            .source_line=79  }, // SessionSettings.h:79
    { .name="PreserveMessageFieldsOrder",       .source_line=80  }, // SessionSettings.h:80

    // ── Timeouts ─────────────────────────────────────────────────────────────
    { .name="LogonTimeout",                     .source_line=81  }, // SessionSettings.h:81
    { .name="LogoutTimeout",                    .source_line=82  }, // SessionSettings.h:82

    // ── Store backends ────────────────────────────────────────────────────────
    { .name="FileStorePath",                    .source_line=83  }, // SessionSettings.h:83
    { .name="MySQLStoreUseConnectionPool",      .source_line=84  }, // SessionSettings.h:84
    { .name="MySQLStoreDatabase",               .source_line=85  }, // SessionSettings.h:85
    { .name="MySQLStoreUser",                   .source_line=86  }, // SessionSettings.h:86
    { .name="MySQLStorePassword",               .source_line=87  }, // SessionSettings.h:87
    { .name="MySQLStoreHost",                   .source_line=88  }, // SessionSettings.h:88
    { .name="MySQLStorePort",                   .source_line=89  }, // SessionSettings.h:89
    { .name="PostgreSQLStoreUseConnectionPool", .source_line=90  }, // SessionSettings.h:90
    { .name="PostgreSQLStoreDatabase",          .source_line=91  }, // SessionSettings.h:91
    { .name="PostgreSQLStoreUser",              .source_line=92  }, // SessionSettings.h:92
    { .name="PostgreSQLStorePassword",          .source_line=93  }, // SessionSettings.h:93
    { .name="PostgreSQLStoreHost",              .source_line=94  }, // SessionSettings.h:94
    { .name="PostgreSQLStorePort",              .source_line=95  }, // SessionSettings.h:95
    { .name="OdbcStoreUser",                    .source_line=96  }, // SessionSettings.h:96
    { .name="OdbcStorePassword",                .source_line=97  }, // SessionSettings.h:97
    { .name="OdbcStoreConnectionString",        .source_line=98  }, // SessionSettings.h:98

    // ── Log sinks ─────────────────────────────────────────────────────────────
    { .name="FileLogPath",                      .source_line=99  }, // SessionSettings.h:99
    { .name="FileLogBackupPath",                .source_line=100 }, // SessionSettings.h:100
    { .name="ScreenLogShowIncoming",            .source_line=101 }, // SessionSettings.h:101
    { .name="ScreenLogShowOutgoing",            .source_line=102 }, // SessionSettings.h:102
    { .name="ScreenLogShowEvents",              .source_line=103 }, // SessionSettings.h:103
    { .name="MySQLLogUseConnectionPool",        .source_line=104 }, // SessionSettings.h:104
    { .name="MySQLLogDatabase",                 .source_line=105 }, // SessionSettings.h:105
    { .name="MySQLLogUser",                     .source_line=106 }, // SessionSettings.h:106
    { .name="MySQLLogPassword",                 .source_line=107 }, // SessionSettings.h:107
    { .name="MySQLLogHost",                     .source_line=108 }, // SessionSettings.h:108
    { .name="MySQLLogPort",                     .source_line=109 }, // SessionSettings.h:109
    { .name="MySQLLogIncomingTable",            .source_line=110 }, // SessionSettings.h:110
    { .name="MySQLLogOutgoingTable",            .source_line=111 }, // SessionSettings.h:111
    { .name="MySQLLogEventTable",               .source_line=112 }, // SessionSettings.h:112
    { .name="PostgreSQLLogUseConnectionPool",   .source_line=113 }, // SessionSettings.h:113
    { .name="PostgreSQLLogDatabase",            .source_line=114 }, // SessionSettings.h:114
    { .name="PostgreSQLLogUser",                .source_line=115 }, // SessionSettings.h:115
    { .name="PostgreSQLLogPassword",            .source_line=116 }, // SessionSettings.h:116
    { .name="PostgreSQLLogHost",                .source_line=117 }, // SessionSettings.h:117
    { .name="PostgreSQLLogPort",                .source_line=118 }, // SessionSettings.h:118
    { .name="PostgreSQLLogIncomingTable",       .source_line=119 }, // SessionSettings.h:119
    { .name="PostgreSQLLogOutgoingTable",       .source_line=120 }, // SessionSettings.h:120
    { .name="PostgreSQLLogEventTable",          .source_line=121 }, // SessionSettings.h:121
    { .name="OdbcLogUser",                      .source_line=122 }, // SessionSettings.h:122
    { .name="OdbcLogPassword",                  .source_line=123 }, // SessionSettings.h:123
    { .name="OdbcLogConnectionString",          .source_line=124 }, // SessionSettings.h:124
    { .name="OdbcLogIncomingTable",             .source_line=125 }, // SessionSettings.h:125
    { .name="OdbcLogOutgoingTable",             .source_line=126 }, // SessionSettings.h:126
    { .name="OdbcLogEventTable",                .source_line=127 }, // SessionSettings.h:127

    // ── Reset / refresh ───────────────────────────────────────────────────────
    { .name="ResetOnLogon",                     .source_line=128 }, // SessionSettings.h:128
    { .name="ResetOnLogout",                    .source_line=129 }, // SessionSettings.h:129
    { .name="ResetOnDisconnect",                .source_line=130 }, // SessionSettings.h:130
    { .name="RefreshOnLogon",                   .source_line=131 }, // SessionSettings.h:131

    // ── Timestamp ─────────────────────────────────────────────────────────────
    { .name="MillisecondsInTimeStamp",          .source_line=132 }, // SessionSettings.h:132
    { .name="TimestampPrecision",               .source_line=133 }, // SessionSettings.h:133

    // ── HTTP ─────────────────────────────────────────────────────────────────
    { .name="HttpAcceptPort",                   .source_line=134 }, // SessionSettings.h:134

    // ── Persistence ──────────────────────────────────────────────────────────
    { .name="PersistMessages",                  .source_line=135 }, // SessionSettings.h:135

    // ── TLS / SSL ─────────────────────────────────────────────────────────────
    { .name="ServerCertificateFile",            .source_line=136 }, // SessionSettings.h:136
    { .name="ServerCertificateKeyFile",         .source_line=137 }, // SessionSettings.h:137
    { .name="ClientCertificateFile",            .source_line=138 }, // SessionSettings.h:138
    { .name="ClientCertificateKeyFile",         .source_line=139 }, // SessionSettings.h:139
    { .name="CertificationAuthoritiesFile",     .source_line=140 }, // SessionSettings.h:140
    { .name="CertificationAuthoritiesDirectory",.source_line=141 }, // SessionSettings.h:141
    { .name="CertificateRevocationListFile",    .source_line=142 }, // SessionSettings.h:142
    { .name="CertificateRevocationListDirectory",.source_line=143}, // SessionSettings.h:143
    { .name="CertificateVerifyLevel",           .source_line=144 }, // SessionSettings.h:144
    { .name="AllowedRemoteAddresses",           .source_line=145 }, // SessionSettings.h:145
    { .name="SSLProtocol",                      .source_line=190 }, // SessionSettings.h:190
    { .name="SSLCipherSuite",                   .source_line=223 }, // SessionSettings.h:223
    { .name="TLSCipherSuites",                  .source_line=239 }, // SessionSettings.h:239
};
// clang-format on

// ── Disposition taxonomy ──────────────────────────────────────────────────────
enum class Disposition {
    // The QF key maps to a concrete 044 loader key.
    Mapped,
    // Session scheduling — not establishment; out-of-scope for config loader.
    OutOfScope_Schedule,
    // Server acceptor binding — engine / infra level; not per-session TOML.
    OutOfScope_ServerInfra,
    // Socket tuning — OS-level knobs; no per-session TOML equivalent.
    OutOfScope_SocketTuning,
    // DB store backend — 044 step-1 store backends are {file, memory} only;
    // MySQL/PostgreSQL/ODBC store adapters are out-of-scope.
    OutOfScope_DbStoreBackend,
    // Log sink configuration — observability = 044 step-2 (FR-009).
    OutOfScope_LogSink,
    // DataDictionary validation flags — driven by the DataDictionary object;
    // no per-session TOML equivalent in step 1.
    OutOfScope_DdValidationFlags,
    // HTTP management interface — engine-infra, not per-session TOML.
    OutOfScope_Http,
    // TLS configuration — mapped collectively via cert_source + security_profile selectors.
    Mapped_TlsViaSelectors,
    // Deferred to step-2 or later.
    Deferred,
    // No 044 equivalent exists; genuine parity gap (see report item 6).
    Gap,
};

struct ParityRow {
    std::string_view qf_key;
    Disposition disposition;
    // For Mapped rows: the concrete 044 loader key accepted by scalar_mappers.cpp
    // or selector_resolver.cpp.  Empty for all non-Mapped dispositions.
    std::string_view key_044;
    // Human-readable rationale (required for non-Mapped rows, recommended for all).
    std::string_view notes;
};

// ── The parity table ──────────────────────────────────────────────────────────
// Source for 044 key spellings: src/config/scalar_mappers.cpp
// and src/config/selector_resolver.cpp.
// clang-format off
const std::vector<ParityRow> kParity = {
    // ── Session identity ──────────────────────────────────────────────────────
    { .qf_key="BeginString",     .disposition=Disposition::Mapped,      .key_044="begin_string",
      .notes="scalar_mappers.cpp, map_scalars — SessionConfig::begin_string" },
    { .qf_key="SenderCompID",    .disposition=Disposition::Mapped,      .key_044="sender_comp_id",
      .notes="scalar_mappers.cpp, map_scalars — SessionConfig::sender_comp_id" },
    { .qf_key="TargetCompID",    .disposition=Disposition::Mapped,      .key_044="target_comp_id",
      .notes="scalar_mappers.cpp, map_scalars — SessionConfig::target_comp_id" },
    { .qf_key="SessionQualifier",.disposition=Disposition::Gap,         .key_044="",
      .notes="No 044 equivalent; fixpp sessions are uniquely identified by "
      "sender_comp_id + target_comp_id + begin_string. The QF qualifier "
      "disambiguates multiple sessions to the same counterparty — a use case "
      "not yet addressed in the v1.0 session model." },
    { .qf_key="DefaultApplVerID",.disposition=Disposition::Mapped,      .key_044="default_appl_ver_id",
      .notes="scalar_mappers.cpp, map_scalars — SessionConfig::default_appl_ver_id "
      "(required for FIXT.1.1; omitted for FIX 4.x)" },
    { .qf_key="ConnectionType",  .disposition=Disposition::Mapped,      .key_044="role",
      .notes="scalar_mappers.cpp, map_scalars — SessionConfig::role "
      "(\"initiator\"/\"acceptor\" ↔ QF ConnectionType)" },

    // ── Protocol flags ────────────────────────────────────────────────────────
    { .qf_key="UseDataDictionary", .disposition=Disposition::OutOfScope_DdValidationFlags, .key_044="",
      .notes="044 always uses the DataDictionary when the 'dictionary' selector is "
      "present; there is no separate enable/disable toggle — 'dictionary' "
      "present ↔ enabled, absent ↔ disabled." },
    { .qf_key="SendResetSeqNumFlag", .disposition=Disposition::Mapped,  .key_044="reset_seqnum_policy",
      .notes="scalar_mappers.cpp, map_scalars — reset_seqnum_policy enum controls the "
      "ResetSeqNumFlag (141) handshake strategy; 'bilateral_strict' / "
      "'bilateral_lenient' / 'unilateral' span the QF yes/no space." },
    { .qf_key="SendRedundantResendRequests", .disposition=Disposition::Gap, .key_044="",
      .notes="No 044 equivalent. QF allows suppressing duplicate ResendRequest re-sends "
      "when the peer has not advanced. fixpp does not expose this knob in v1.0." },
    { .qf_key="SendNextExpectedMsgSeqNum", .disposition=Disposition::Mapped, .key_044="enable_next_expected_msg_seq_num",
      .notes="scalar_mappers.cpp, map_scalars — SessionConfig::enable_next_expected_msg_seq_num; "
      "controls 789/NextExpectedMsgSeqNum in Logon (FIX 5.0+)" },

    // ── Dictionary paths ──────────────────────────────────────────────────────
    { .qf_key="DataDictionary",           .disposition=Disposition::Mapped, .key_044="dictionary",
      .notes="selector_resolver.cpp, resolve_engine_dictionary — 'dictionary' selector, kind=\"path\"; "
      "the DataDictionary= path maps to [dictionary] kind=\"path\" path=\"…\". "
      "By-version resolution is deferred (OQ-1 option A)." },
    { .qf_key="TransportDataDictionary",  .disposition=Disposition::OutOfScope_DdValidationFlags, .key_044="",
      .notes="FIXT.1.1 transport dictionary separate from the app dictionary. "
      "044 step-1 does not distinguish transport vs. app dictionaries — "
      "the single 'dictionary' selector covers the combined role. "
      "Deferred to step-2 if per-layer dicts are needed." },
    { .qf_key="AppDataDictionary",        .disposition=Disposition::OutOfScope_DdValidationFlags, .key_044="",
      .notes="FIXT.1.1 per-ApplVerID app dictionary. Same rationale as "
      "TransportDataDictionary above." },

    // ── Time / clock (scheduling — out of scope) ──────────────────────────────
    { .qf_key="UseLocalTime",    .disposition=Disposition::OutOfScope_Schedule, .key_044="",
      .notes="Session scheduling: whether StartTime/EndTime are in local time. "
      "Not applicable — 044 has no session-schedule window." },
    { .qf_key="StartTime",       .disposition=Disposition::OutOfScope_Schedule, .key_044="",
      .notes="Session scheduling: UTC time-of-day the session window opens. "
      "444 step-1 has no session-schedule concept." },
    { .qf_key="EndTime",         .disposition=Disposition::OutOfScope_Schedule, .key_044="",
      .notes="Session scheduling: UTC time-of-day the session window closes." },
    { .qf_key="StartDay",        .disposition=Disposition::OutOfScope_Schedule, .key_044="",
      .notes="Session scheduling: day-of-week the session window starts." },
    { .qf_key="EndDay",          .disposition=Disposition::OutOfScope_Schedule, .key_044="",
      .notes="Session scheduling: day-of-week the session window ends." },
    { .qf_key="NonStopSession",  .disposition=Disposition::OutOfScope_Schedule, .key_044="",
      .notes="Session scheduling: disables the time-window gate (always active). "
      "044 sessions are non-stop by default; no toggle needed." },
    { .qf_key="LogonTime",       .disposition=Disposition::OutOfScope_Schedule, .key_044="",
      .notes="Session scheduling: sub-window for Logon initiation." },
    { .qf_key="LogoutTime",      .disposition=Disposition::OutOfScope_Schedule, .key_044="",
      .notes="Session scheduling: sub-window for Logout." },
    { .qf_key="LogonDay",        .disposition=Disposition::OutOfScope_Schedule, .key_044="",
      .notes="Session scheduling: day-of-week the Logon window applies." },
    { .qf_key="LogoutDay",       .disposition=Disposition::OutOfScope_Schedule, .key_044="",
      .notes="Session scheduling: day-of-week the Logout window applies." },

    // ── Validation ────────────────────────────────────────────────────────────
    { .qf_key="CheckCompID",      .disposition=Disposition::Mapped,     .key_044="check_comp_id",
      .notes="scalar_mappers.cpp, map_scalars — SessionConfig::check_comp_id" },
    { .qf_key="CheckLatency",     .disposition=Disposition::Gap,        .key_044="",
      .notes="No 044 equivalent for the boolean enable/disable of latency checking; "
      "fixpp uses 'sending_time_threshold' (a Duration) which when set implicitly "
      "enables the check. The QF boolean has no direct mapping." },
    { .qf_key="MaxLatency",       .disposition=Disposition::Mapped,     .key_044="sending_time_threshold",
      .notes="scalar_mappers.cpp, map_scalars — SessionConfig::sending_time_threshold "
      "(a Duration; when set, enables the SendingTime latency guard). "
      "NOTE: the semantics differ slightly — QF MaxLatency is an integer "
      "(seconds); 044 sending_time_threshold is a duration string (e.g. \"120s\"). "
      "No loss of behavior; the guard fires equivalently." },
    { .qf_key="HeartBtInt",       .disposition=Disposition::Mapped,     .key_044="heartbeat_interval",
      .notes="scalar_mappers.cpp, map_scalars — SessionConfig::heartbeat_interval" },

    // ── Socket / network ─────────────────────────────────────────────────────
    { .qf_key="SocketAcceptPort",     .disposition=Disposition::OutOfScope_ServerInfra, .key_044="",
      .notes="Acceptor bind port: engine/infra level, not per-session TOML. "
      "fixpp acceptors are configured at the network layer independently." },
    { .qf_key="SocketReuseAddress",   .disposition=Disposition::OutOfScope_SocketTuning, .key_044="",
      .notes="OS socket option: SO_REUSEADDR. Engine/infra knob; no per-session TOML." },
    { .qf_key="SocketConnectHost",    .disposition=Disposition::Mapped,  .key_044="transport.host",
      .notes="scalar_mappers.cpp, map_structured_members — reconnect_endpoint.host; "
      "maps to 'transport.host' in the [transport] selector table." },
    { .qf_key="SocketConnectPort",    .disposition=Disposition::Mapped,  .key_044="transport.port",
      .notes="scalar_mappers.cpp, map_structured_members — reconnect_endpoint.port; "
      "maps to 'transport.port' in the [transport] selector table." },
    { .qf_key="SocketConnectSourceHost", .disposition=Disposition::OutOfScope_SocketTuning, .key_044="",
      .notes="OS-level bind-before-connect source address; no per-session TOML." },
    { .qf_key="SocketConnectSourcePort", .disposition=Disposition::OutOfScope_SocketTuning, .key_044="",
      .notes="OS-level bind-before-connect source port; no per-session TOML." },
    { .qf_key="SocketNodelay",        .disposition=Disposition::OutOfScope_SocketTuning, .key_044="",
      .notes="TCP_NODELAY socket option; engine/infra tuning, not per-session TOML." },
    { .qf_key="SocketSendBufferSize", .disposition=Disposition::OutOfScope_SocketTuning, .key_044="",
      .notes="SO_SNDBUF socket option; engine/infra tuning, not per-session TOML." },
    { .qf_key="SocketReceiveBufferSize", .disposition=Disposition::OutOfScope_SocketTuning, .key_044="",
      .notes="SO_RCVBUF socket option; engine/infra tuning, not per-session TOML." },
    { .qf_key="HostSelectionPolicy",  .disposition=Disposition::OutOfScope_ServerInfra, .key_044="",
      .notes="Multi-host failover policy (ROUND_ROBIN / RANDOM / PRIORITY); "
      "fixpp v1.0 has a single reconnect endpoint per session. "
      "Multi-host failover is not yet modeled." },
    { .qf_key="HostSelectionPolicyPriorityStartOverInterval", .disposition=Disposition::OutOfScope_ServerInfra, .key_044="",
      .notes="Companion to HostSelectionPolicy (priority restart interval); "
      "same rationale." },
    { .qf_key="ReconnectInterval",    .disposition=Disposition::Deferred, .key_044="",
      .notes="Reconnect retry interval. fixpp v1.0 reconnect policy has its own "
      "reconnect_policy selector with similar semantics; an explicit integer "
      "key is not yet wired. Deferred to step-2." },

    // ── Wire validation flags ─────────────────────────────────────────────────
    { .qf_key="ValidateLengthAndChecksum", .disposition=Disposition::Gap, .key_044="",
      .notes="No 044 equivalent for disabling the framer checksum/length validation; "
      "fixpp always validates the FIX framing length and checksum (fail-closed)." },
    { .qf_key="ValidateFieldsOutOfOrder",  .disposition=Disposition::OutOfScope_DdValidationFlags, .key_044="",
      .notes="DataDictionary-level validation flag (passed to DD constructor). "
      "044 validate_inbound_messages enables DD-driven validation globally; "
      "per-flag DD construction options are not exposed in step 1." },
    { .qf_key="ValidateFieldsHaveValues",  .disposition=Disposition::OutOfScope_DdValidationFlags, .key_044="",
      .notes="DataDictionary-level validation flag; same rationale as above." },
    { .qf_key="ValidateUserDefinedFields", .disposition=Disposition::OutOfScope_DdValidationFlags, .key_044="",
      .notes="DataDictionary-level validation flag; same rationale as above." },
    { .qf_key="AllowUnknownMsgFields",     .disposition=Disposition::OutOfScope_DdValidationFlags, .key_044="",
      .notes="DataDictionary-level validation flag; same rationale as above." },
    { .qf_key="PreserveMessageFieldsOrder",.disposition=Disposition::OutOfScope_DdValidationFlags, .key_044="",
      .notes="DataDictionary flag for preserving field order in output; "
      "no equivalent in 044 step-1." },

    // ── Timeouts ─────────────────────────────────────────────────────────────
    { .qf_key="LogonTimeout",     .disposition=Disposition::Gap, .key_044="",
      .notes="No exact 044 equivalent; fixpp drives Logon retry/timeout via the "
      "reconnect_policy timers, not a named LogonTimeout scalar." },
    { .qf_key="LogoutTimeout",    .disposition=Disposition::Mapped, .key_044="logout_disconnect_timeout_ms",
      .notes="scalar_mappers.cpp, map_scalars — SessionConfig::logout_disconnect_timeout_ms "
      "(integer ms; QF LogoutTimeout is integer seconds — different units, "
      "same semantic: how long to wait for peer Logout ACK before hard-close)." },

    // ── Store backends ────────────────────────────────────────────────────────
    { .qf_key="FileStorePath",                    .disposition=Disposition::Mapped, .key_044="store.directory",
      .notes="selector_resolver.cpp, resolve_engine_store — 'store' selector, kind=\"file\"; "
      "FileStorePath= maps to [store] kind=\"file\" directory=\"…\"." },
    { .qf_key="MySQLStoreUseConnectionPool",      .disposition=Disposition::OutOfScope_DbStoreBackend, .key_044="",
      .notes="MySQL store backend; 044 step-1 stores: {file, memory} only. "
      "MySQL adapter is out-of-scope." },
    { .qf_key="MySQLStoreDatabase",               .disposition=Disposition::OutOfScope_DbStoreBackend, .key_044="",
      .notes="MySQL store backend; same rationale." },
    { .qf_key="MySQLStoreUser",                   .disposition=Disposition::OutOfScope_DbStoreBackend, .key_044="",
      .notes="MySQL store backend; same rationale." },
    { .qf_key="MySQLStorePassword",               .disposition=Disposition::OutOfScope_DbStoreBackend, .key_044="",
      .notes="MySQL store backend; same rationale." },
    { .qf_key="MySQLStoreHost",                   .disposition=Disposition::OutOfScope_DbStoreBackend, .key_044="",
      .notes="MySQL store backend; same rationale." },
    { .qf_key="MySQLStorePort",                   .disposition=Disposition::OutOfScope_DbStoreBackend, .key_044="",
      .notes="MySQL store backend; same rationale." },
    { .qf_key="PostgreSQLStoreUseConnectionPool", .disposition=Disposition::OutOfScope_DbStoreBackend, .key_044="",
      .notes="PostgreSQL store backend; 044 step-1 stores: {file, memory} only." },
    { .qf_key="PostgreSQLStoreDatabase",          .disposition=Disposition::OutOfScope_DbStoreBackend, .key_044="",
      .notes="PostgreSQL store backend; same rationale." },
    { .qf_key="PostgreSQLStoreUser",              .disposition=Disposition::OutOfScope_DbStoreBackend, .key_044="",
      .notes="PostgreSQL store backend; same rationale." },
    { .qf_key="PostgreSQLStorePassword",          .disposition=Disposition::OutOfScope_DbStoreBackend, .key_044="",
      .notes="PostgreSQL store backend; same rationale." },
    { .qf_key="PostgreSQLStoreHost",              .disposition=Disposition::OutOfScope_DbStoreBackend, .key_044="",
      .notes="PostgreSQL store backend; same rationale." },
    { .qf_key="PostgreSQLStorePort",              .disposition=Disposition::OutOfScope_DbStoreBackend, .key_044="",
      .notes="PostgreSQL store backend; same rationale." },
    { .qf_key="OdbcStoreUser",                    .disposition=Disposition::OutOfScope_DbStoreBackend, .key_044="",
      .notes="ODBC store backend; 044 step-1 stores: {file, memory} only." },
    { .qf_key="OdbcStorePassword",                .disposition=Disposition::OutOfScope_DbStoreBackend, .key_044="",
      .notes="ODBC store backend; same rationale." },
    { .qf_key="OdbcStoreConnectionString",        .disposition=Disposition::OutOfScope_DbStoreBackend, .key_044="",
      .notes="ODBC store backend; same rationale." },

    // ── Log sinks ─────────────────────────────────────────────────────────────
    { .qf_key="FileLogPath",                    .disposition=Disposition::OutOfScope_LogSink, .key_044="",
      .notes="File log sink; observability = 044 step-2 (FR-009). "
      "Out-of-scope for step-1 config loader." },
    { .qf_key="FileLogBackupPath",              .disposition=Disposition::OutOfScope_LogSink, .key_044="",
      .notes="File log sink (backup path); same rationale." },
    { .qf_key="ScreenLogShowIncoming",          .disposition=Disposition::OutOfScope_LogSink, .key_044="",
      .notes="Screen/console log sink; same rationale." },
    { .qf_key="ScreenLogShowOutgoing",          .disposition=Disposition::OutOfScope_LogSink, .key_044="",
      .notes="Screen/console log sink; same rationale." },
    { .qf_key="ScreenLogShowEvents",            .disposition=Disposition::OutOfScope_LogSink, .key_044="",
      .notes="Screen/console log sink; same rationale." },
    { .qf_key="MySQLLogUseConnectionPool",      .disposition=Disposition::OutOfScope_LogSink, .key_044="",
      .notes="MySQL log sink; same rationale." },
    { .qf_key="MySQLLogDatabase",               .disposition=Disposition::OutOfScope_LogSink, .key_044="",
      .notes="MySQL log sink; same rationale." },
    { .qf_key="MySQLLogUser",                   .disposition=Disposition::OutOfScope_LogSink, .key_044="",
      .notes="MySQL log sink; same rationale." },
    { .qf_key="MySQLLogPassword",               .disposition=Disposition::OutOfScope_LogSink, .key_044="",
      .notes="MySQL log sink; same rationale." },
    { .qf_key="MySQLLogHost",                   .disposition=Disposition::OutOfScope_LogSink, .key_044="",
      .notes="MySQL log sink; same rationale." },
    { .qf_key="MySQLLogPort",                   .disposition=Disposition::OutOfScope_LogSink, .key_044="",
      .notes="MySQL log sink; same rationale." },
    { .qf_key="MySQLLogIncomingTable",          .disposition=Disposition::OutOfScope_LogSink, .key_044="",
      .notes="MySQL log sink; same rationale." },
    { .qf_key="MySQLLogOutgoingTable",          .disposition=Disposition::OutOfScope_LogSink, .key_044="",
      .notes="MySQL log sink; same rationale." },
    { .qf_key="MySQLLogEventTable",             .disposition=Disposition::OutOfScope_LogSink, .key_044="",
      .notes="MySQL log sink; same rationale." },
    { .qf_key="PostgreSQLLogUseConnectionPool", .disposition=Disposition::OutOfScope_LogSink, .key_044="",
      .notes="PostgreSQL log sink; same rationale." },
    { .qf_key="PostgreSQLLogDatabase",          .disposition=Disposition::OutOfScope_LogSink, .key_044="",
      .notes="PostgreSQL log sink; same rationale." },
    { .qf_key="PostgreSQLLogUser",              .disposition=Disposition::OutOfScope_LogSink, .key_044="",
      .notes="PostgreSQL log sink; same rationale." },
    { .qf_key="PostgreSQLLogPassword",          .disposition=Disposition::OutOfScope_LogSink, .key_044="",
      .notes="PostgreSQL log sink; same rationale." },
    { .qf_key="PostgreSQLLogHost",              .disposition=Disposition::OutOfScope_LogSink, .key_044="",
      .notes="PostgreSQL log sink; same rationale." },
    { .qf_key="PostgreSQLLogPort",              .disposition=Disposition::OutOfScope_LogSink, .key_044="",
      .notes="PostgreSQL log sink; same rationale." },
    { .qf_key="PostgreSQLLogIncomingTable",     .disposition=Disposition::OutOfScope_LogSink, .key_044="",
      .notes="PostgreSQL log sink; same rationale." },
    { .qf_key="PostgreSQLLogOutgoingTable",     .disposition=Disposition::OutOfScope_LogSink, .key_044="",
      .notes="PostgreSQL log sink; same rationale." },
    { .qf_key="PostgreSQLLogEventTable",        .disposition=Disposition::OutOfScope_LogSink, .key_044="",
      .notes="PostgreSQL log sink; same rationale." },
    { .qf_key="OdbcLogUser",                    .disposition=Disposition::OutOfScope_LogSink, .key_044="",
      .notes="ODBC log sink; same rationale." },
    { .qf_key="OdbcLogPassword",                .disposition=Disposition::OutOfScope_LogSink, .key_044="",
      .notes="ODBC log sink; same rationale." },
    { .qf_key="OdbcLogConnectionString",        .disposition=Disposition::OutOfScope_LogSink, .key_044="",
      .notes="ODBC log sink; same rationale." },
    { .qf_key="OdbcLogIncomingTable",           .disposition=Disposition::OutOfScope_LogSink, .key_044="",
      .notes="ODBC log sink; same rationale." },
    { .qf_key="OdbcLogOutgoingTable",           .disposition=Disposition::OutOfScope_LogSink, .key_044="",
      .notes="ODBC log sink; same rationale." },
    { .qf_key="OdbcLogEventTable",              .disposition=Disposition::OutOfScope_LogSink, .key_044="",
      .notes="ODBC log sink; same rationale." },

    // ── Reset / refresh ───────────────────────────────────────────────────────
    { .qf_key="ResetOnLogon",      .disposition=Disposition::Mapped, .key_044="reset_on_logon",
      .notes="scalar_mappers.cpp, map_scalars — SessionConfig::reset_on_logon" },
    { .qf_key="ResetOnLogout",     .disposition=Disposition::Mapped, .key_044="reset_on_logout",
      .notes="scalar_mappers.cpp, map_scalars — SessionConfig::reset_on_logout" },
    { .qf_key="ResetOnDisconnect", .disposition=Disposition::Mapped, .key_044="reset_on_disconnect",
      .notes="scalar_mappers.cpp, map_scalars — SessionConfig::reset_on_disconnect" },
    { .qf_key="RefreshOnLogon",    .disposition=Disposition::Mapped, .key_044="refresh_on_logon",
      .notes="scalar_mappers.cpp, map_scalars — SessionConfig::refresh_on_logon" },

    // ── Timestamp ─────────────────────────────────────────────────────────────
    { .qf_key="MillisecondsInTimeStamp", .disposition=Disposition::Mapped, .key_044="sending_time_precision",
      .notes="scalar_mappers.cpp, map_scalars — SessionConfig::sending_time_precision "
      "(QF MillisecondsInTimeStamp=Y ↔ precision=\"millis\"; "
      "044 additionally supports \"micros\" and \"nanos\")." },
    { .qf_key="TimestampPrecision",      .disposition=Disposition::Mapped, .key_044="sending_time_precision",
      .notes="scalar_mappers.cpp, map_scalars — Same 044 key as MillisecondsInTimeStamp; "
      "TimestampPrecision is the QF successor key (integer: 0=seconds, "
      "3=millis, 6=micros, 9=nanos) — both map to sending_time_precision." },

    // ── HTTP ─────────────────────────────────────────────────────────────────
    { .qf_key="HttpAcceptPort",    .disposition=Disposition::OutOfScope_Http, .key_044="",
      .notes="HTTP management interface (QuickFIX monitoring port); "
      "engine-infra, not per-session TOML. Out-of-scope." },

    // ── Persistence ──────────────────────────────────────────────────────────
    { .qf_key="PersistMessages",   .disposition=Disposition::Mapped, .key_044="store",
      .notes="Indirectly: 'store=memory' implies non-persistent (yields_persistent_store()=false), "
      "mirroring QF PersistMessages=N. 'store=file' gives persistence. "
      "selector_resolver.cpp, resolve_engine_store's store.directory branch." },

    // ── TLS / SSL ─────────────────────────────────────────────────────────────
    { .qf_key="ServerCertificateFile",             .disposition=Disposition::Mapped_TlsViaSelectors, .key_044="cert_source.cert_file",
      .notes="selector_resolver.cpp, resolve_engine_cert_source — cert_source selector, kind=\"file\", "
      "cert_file= path (the server-side leaf certificate)." },
    { .qf_key="ServerCertificateKeyFile",          .disposition=Disposition::Mapped_TlsViaSelectors, .key_044="cert_source.key_file",
      .notes="selector_resolver.cpp, resolve_engine_cert_source — cert_source selector, key_file= path." },
    { .qf_key="ClientCertificateFile",             .disposition=Disposition::Mapped_TlsViaSelectors, .key_044="cert_source.cert_file",
      .notes="selector_resolver.cpp, resolve_engine_cert_source — same cert_source cert_file; QF uses "
      "separate server/client keys, 044 uses a single role-independent cert_source "
      "(the role is determined by security_profile.kind)." },
    { .qf_key="ClientCertificateKeyFile",          .disposition=Disposition::Mapped_TlsViaSelectors, .key_044="cert_source.key_file",
      .notes="selector_resolver.cpp, resolve_engine_cert_source — same cert_source key_file." },
    { .qf_key="CertificationAuthoritiesFile",      .disposition=Disposition::Mapped_TlsViaSelectors, .key_044="cert_source.ca_file",
      .notes="selector_resolver.cpp, resolve_engine_cert_source — cert_source selector, ca_file= path." },
    { .qf_key="CertificationAuthoritiesDirectory", .disposition=Disposition::OutOfScope_SocketTuning, .key_044="",
      .notes="No 044 equivalent; 044 cert_source accepts a single CA bundle file. "
      "A CA directory is not supported in step-1." },
    { .qf_key="CertificateRevocationListFile",     .disposition=Disposition::Gap, .key_044="",
      .notes="No 044 equivalent; fixpp does not currently support CRL-based revocation "
      "checking in step-1 (OCSP / CRL not wired)." },
    { .qf_key="CertificateRevocationListDirectory",.disposition=Disposition::Gap, .key_044="",
      .notes="No 044 equivalent; same rationale as CertificateRevocationListFile." },
    { .qf_key="CertificateVerifyLevel",            .disposition=Disposition::Mapped_TlsViaSelectors, .key_044="security_profile.kind",
      .notes="selector_resolver.cpp, parse_security_profile — security_profile.kind encodes the "
      "verification strategy: "
      "mtls_ca=mutual TLS with CA verification (CertificateVerifyLevel=2), "
      "one_way_ca=server-auth only (CertificateVerifyLevel=1), "
      "insecure_plain_tcp=no TLS (CertificateVerifyLevel=0)." },
    { .qf_key="AllowedRemoteAddresses",            .disposition=Disposition::Gap, .key_044="",
      .notes="No 044 equivalent; fixpp does not expose a per-session IP allowlist in v1.0." },
    { .qf_key="SSLProtocol",                       .disposition=Disposition::Gap, .key_044="",
      .notes="No 044 equivalent; fixpp defers TLS protocol selection to the "
      "system OpenSSL defaults. An explicit protocol string is not configurable "
      "via TOML in step-1." },
    { .qf_key="SSLCipherSuite",                    .disposition=Disposition::Gap, .key_044="",
      .notes="No 044 equivalent; cipher suite selection is not exposed in step-1 TOML." },
    { .qf_key="TLSCipherSuites",                   .disposition=Disposition::Gap, .key_044="",
      .notes="No 044 equivalent; TLS 1.3 cipher suite selection is not exposed in step-1 TOML." },
};
// clang-format on

// ── Loader-accepted 044 keys (sources: scalar_mappers.cpp + selector_resolver.cpp) ──
// This set is the union of every TOML key the loader recognises as a session-
// level key (scalars) or top-level selector key (selector names + their sub-keys).
// These are checked against Mapped rows in kParity.
const std::set<std::string_view> kLoaderAccepted044Keys = {
    // Scalars — scalar_mappers.cpp
    "begin_string",
    "sender_comp_id",
    "target_comp_id",
    "username",
    "password",
    "role",
    "mode",
    "locks",
    "heartbeat_interval",
    "test_request_threshold",
    "sending_time_threshold",
    "logout_disconnect_timeout_ms",
    "reset_on_logon",
    "reset_on_logout",
    "reset_on_disconnect",
    "refresh_on_logon",
    "redeliver_poss_dup",
    "allow_pos_dup",
    "enable_next_expected_msg_seq_num",
    "check_comp_id",
    "validate_sequence_numbers",
    "validate_inbound_messages",
    "reset_seqnum_policy",
    "sending_time_precision",
    "app_backpressure",
    "reject_policy",
    "default_appl_ver_id",
    // Structured members — scalar_mappers.cpp
    "security_profile",
    "security_profile.kind",
    "reconnect_policy",
    "transport",
    "transport.kind",
    "transport.host",
    "transport.port",
    // Selectors — selector_resolver.cpp
    "clock",
    "clock.kind",
    "store",
    "store.kind",
    "store.directory",
    "cert_source",
    "cert_source.kind",
    "cert_source.cert_file",
    "cert_source.key_file",
    "cert_source.ca_file",
    "dictionary",
    "dictionary.kind",
    "dictionary.path",
};

}  // anonymous namespace

// ── T030 tests ───────────────────────────────────────────────────────────────

// Test 1: kParity covers kQfAllKeys exactly (no missing, no extra rows).
TEST(QuickFixParityTable, CoverageExact) {
    // Build sets from oracle and parity table.
    std::set<std::string_view> oracle;
    for (const auto& k : kQfAllKeys) {
        oracle.insert(k.name);
    }

    std::set<std::string_view> covered;
    for (const auto& row : kParity) {
        covered.insert(row.qf_key);
    }

    // Collect missing and extra with explicit diff (not subset assertion).
    std::vector<std::string_view> missing;
    for (const auto& k : oracle) {
        if (!covered.contains(k)) {
            missing.push_back(k);
        }
    }

    std::vector<std::string_view> extra;
    for (const auto& k : covered) {
        if (!oracle.contains(k)) {
            extra.push_back(k);
        }
    }

    if (!missing.empty() || !extra.empty()) {
        std::string msg = "Parity table coverage mismatch:\n";
        if (!missing.empty()) {
            msg += "  Missing rows (QF keys with no parity disposition):\n";
            for (auto k : missing) {
                msg += "    - " + std::string{k} + "\n";
            }
        }
        if (!extra.empty()) {
            msg += "  Extra rows (parity rows not in QF key oracle):\n";
            for (auto k : extra) {
                msg += "    - " + std::string{k} + "\n";
            }
        }
        FAIL() << msg;
    }
}

// Test 2: every row has a non-empty notes field.
TEST(QuickFixParityTable, AllRowsHaveNotes) {
    for (const auto& row : kParity) {
        EXPECT_FALSE(row.notes.empty()) << "Row for QF key '" << row.qf_key << "' has empty notes";
    }
}

// Test 3: every Mapped / Mapped_TlsViaSelectors row's 044 key is accepted
//         by the loader (kLoaderAccepted044Keys).  Catches invented or
//         mistyped 044 keys.
TEST(QuickFixParityTable, MappedKeysExistInLoader) {
    for (const auto& row : kParity) {
        bool is_mapped = (row.disposition == Disposition::Mapped ||
                          row.disposition == Disposition::Mapped_TlsViaSelectors);
        if (!is_mapped) {
            continue;
        }
        EXPECT_FALSE(row.key_044.empty())
            << "Row for QF key '" << row.qf_key << "' is Mapped but has empty key_044";
        EXPECT_TRUE(kLoaderAccepted044Keys.count(row.key_044))
            << "Row for QF key '" << row.qf_key << "' claims 044 key '" << row.key_044
            << "' which is NOT in kLoaderAccepted044Keys";
    }
}

// Test 4: non-Mapped rows have empty key_044 (no phantom mapping).
TEST(QuickFixParityTable, NonMappedRowsHaveNoKey044) {
    for (const auto& row : kParity) {
        bool is_mapped = (row.disposition == Disposition::Mapped ||
                          row.disposition == Disposition::Mapped_TlsViaSelectors);
        if (is_mapped) {
            continue;
        }
        EXPECT_TRUE(row.key_044.empty()) << "Non-Mapped row for QF key '" << row.qf_key
                                         << "' unexpectedly has key_044 = '" << row.key_044 << "'";
    }
}

// Test 5: schedule keys are all classified as OutOfScope_Schedule.
TEST(QuickFixParityTable, ScheduleKeysAreOutOfScope) {
    const std::set<std::string_view> schedule_keys = {
        "StartTime",    "EndTime",   "StartDay",   "EndDay",   "NonStopSession",
        "UseLocalTime", "LogonTime", "LogoutTime", "LogonDay", "LogoutDay",
    };
    for (const auto& row : kParity) {
        if (schedule_keys.contains(row.qf_key)) {
            EXPECT_EQ(row.disposition, Disposition::OutOfScope_Schedule)
                << "Schedule key '" << row.qf_key << "' must be OutOfScope_Schedule";
        }
    }
}

// Test 6: Gap rows are explicitly enumerated (discriminating count check).
// If a new Gap is added or removed, this test surfaces it for review.
TEST(QuickFixParityTable, GapRowCount) {
    int gaps = 0;
    for (const auto& row : kParity) {
        if (row.disposition == Disposition::Gap) {
            ++gaps;
        }
    }
    // Known gaps (see report item 6 in the brief):
    //   SessionQualifier, SendRedundantResendRequests, CheckLatency,
    //   ValidateLengthAndChecksum, LogonTimeout,
    //   CertificateRevocationListFile, CertificateRevocationListDirectory,
    //   AllowedRemoteAddresses, SSLProtocol, SSLCipherSuite, TLSCipherSuites
    // Total: 11 gaps.
    EXPECT_EQ(gaps, 11) << "Unexpected number of Gap rows; update the count when gaps are "
                           "resolved or new ones are identified";
}
