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
    int              source_line; // SessionSettings.h line number
};

// All session-establishment const char[] keys from SessionSettings.h (36–239).
// Grouped for readability; the completeness test uses the full flat set.
// clang-format off
const std::vector<QfKey> kQfAllKeys = {
    // ── Session identity ──────────────────────────────────────────────────────
    { "BeginString",                      36  }, // SessionSettings.h:36
    { "SenderCompID",                     37  }, // SessionSettings.h:37
    { "TargetCompID",                     38  }, // SessionSettings.h:38
    { "SessionQualifier",                 39  }, // SessionSettings.h:39
    { "DefaultApplVerID",                 40  }, // SessionSettings.h:40
    { "ConnectionType",                   41  }, // SessionSettings.h:41

    // ── Protocol flags ────────────────────────────────────────────────────────
    { "UseDataDictionary",                42  }, // SessionSettings.h:42
    { "SendResetSeqNumFlag",              43  }, // SessionSettings.h:43
    { "SendRedundantResendRequests",      44  }, // SessionSettings.h:44
    { "SendNextExpectedMsgSeqNum",        45  }, // SessionSettings.h:45

    // ── Dictionary paths ──────────────────────────────────────────────────────
    { "DataDictionary",                   46  }, // SessionSettings.h:46
    { "TransportDataDictionary",          47  }, // SessionSettings.h:47
    { "AppDataDictionary",                48  }, // SessionSettings.h:48

    // ── Time / clock ──────────────────────────────────────────────────────────
    { "UseLocalTime",                     49  }, // SessionSettings.h:49
    { "StartTime",                        50  }, // SessionSettings.h:50
    { "EndTime",                          51  }, // SessionSettings.h:51
    { "StartDay",                         52  }, // SessionSettings.h:52
    { "EndDay",                           53  }, // SessionSettings.h:53
    { "NonStopSession",                   54  }, // SessionSettings.h:54
    { "LogonTime",                        55  }, // SessionSettings.h:55
    { "LogoutTime",                       56  }, // SessionSettings.h:56
    { "LogonDay",                         57  }, // SessionSettings.h:57
    { "LogoutDay",                        58  }, // SessionSettings.h:58

    // ── Validation ────────────────────────────────────────────────────────────
    { "CheckCompID",                      59  }, // SessionSettings.h:59
    { "CheckLatency",                     60  }, // SessionSettings.h:60
    { "MaxLatency",                       61  }, // SessionSettings.h:61
    { "HeartBtInt",                       62  }, // SessionSettings.h:62

    // ── Socket / network ─────────────────────────────────────────────────────
    { "SocketAcceptPort",                 63  }, // SessionSettings.h:63
    { "SocketReuseAddress",               64  }, // SessionSettings.h:64
    { "SocketConnectHost",                65  }, // SessionSettings.h:65
    { "SocketConnectPort",                66  }, // SessionSettings.h:66
    { "SocketConnectSourceHost",          67  }, // SessionSettings.h:67
    { "SocketConnectSourcePort",          68  }, // SessionSettings.h:68
    { "SocketNodelay",                    69  }, // SessionSettings.h:69
    { "SocketSendBufferSize",             70  }, // SessionSettings.h:70
    { "SocketReceiveBufferSize",          71  }, // SessionSettings.h:71
    { "HostSelectionPolicy",              72  }, // SessionSettings.h:72
    { "HostSelectionPolicyPriorityStartOverInterval", 73 }, // SessionSettings.h:73
    { "ReconnectInterval",                74  }, // SessionSettings.h:74

    // ── Wire validation flags ─────────────────────────────────────────────────
    { "ValidateLengthAndChecksum",        75  }, // SessionSettings.h:75
    { "ValidateFieldsOutOfOrder",         76  }, // SessionSettings.h:76
    { "ValidateFieldsHaveValues",         77  }, // SessionSettings.h:77
    { "ValidateUserDefinedFields",        78  }, // SessionSettings.h:78
    { "AllowUnknownMsgFields",            79  }, // SessionSettings.h:79
    { "PreserveMessageFieldsOrder",       80  }, // SessionSettings.h:80

    // ── Timeouts ─────────────────────────────────────────────────────────────
    { "LogonTimeout",                     81  }, // SessionSettings.h:81
    { "LogoutTimeout",                    82  }, // SessionSettings.h:82

    // ── Store backends ────────────────────────────────────────────────────────
    { "FileStorePath",                    83  }, // SessionSettings.h:83
    { "MySQLStoreUseConnectionPool",      84  }, // SessionSettings.h:84
    { "MySQLStoreDatabase",               85  }, // SessionSettings.h:85
    { "MySQLStoreUser",                   86  }, // SessionSettings.h:86
    { "MySQLStorePassword",               87  }, // SessionSettings.h:87
    { "MySQLStoreHost",                   88  }, // SessionSettings.h:88
    { "MySQLStorePort",                   89  }, // SessionSettings.h:89
    { "PostgreSQLStoreUseConnectionPool", 90  }, // SessionSettings.h:90
    { "PostgreSQLStoreDatabase",          91  }, // SessionSettings.h:91
    { "PostgreSQLStoreUser",              92  }, // SessionSettings.h:92
    { "PostgreSQLStorePassword",          93  }, // SessionSettings.h:93
    { "PostgreSQLStoreHost",              94  }, // SessionSettings.h:94
    { "PostgreSQLStorePort",              95  }, // SessionSettings.h:95
    { "OdbcStoreUser",                    96  }, // SessionSettings.h:96
    { "OdbcStorePassword",                97  }, // SessionSettings.h:97
    { "OdbcStoreConnectionString",        98  }, // SessionSettings.h:98

    // ── Log sinks ─────────────────────────────────────────────────────────────
    { "FileLogPath",                      99  }, // SessionSettings.h:99
    { "FileLogBackupPath",                100 }, // SessionSettings.h:100
    { "ScreenLogShowIncoming",            101 }, // SessionSettings.h:101
    { "ScreenLogShowOutgoing",            102 }, // SessionSettings.h:102
    { "ScreenLogShowEvents",              103 }, // SessionSettings.h:103
    { "MySQLLogUseConnectionPool",        104 }, // SessionSettings.h:104
    { "MySQLLogDatabase",                 105 }, // SessionSettings.h:105
    { "MySQLLogUser",                     106 }, // SessionSettings.h:106
    { "MySQLLogPassword",                 107 }, // SessionSettings.h:107
    { "MySQLLogHost",                     108 }, // SessionSettings.h:108
    { "MySQLLogPort",                     109 }, // SessionSettings.h:109
    { "MySQLLogIncomingTable",            110 }, // SessionSettings.h:110
    { "MySQLLogOutgoingTable",            111 }, // SessionSettings.h:111
    { "MySQLLogEventTable",               112 }, // SessionSettings.h:112
    { "PostgreSQLLogUseConnectionPool",   113 }, // SessionSettings.h:113
    { "PostgreSQLLogDatabase",            114 }, // SessionSettings.h:114
    { "PostgreSQLLogUser",                115 }, // SessionSettings.h:115
    { "PostgreSQLLogPassword",            116 }, // SessionSettings.h:116
    { "PostgreSQLLogHost",                117 }, // SessionSettings.h:117
    { "PostgreSQLLogPort",                118 }, // SessionSettings.h:118
    { "PostgreSQLLogIncomingTable",       119 }, // SessionSettings.h:119
    { "PostgreSQLLogOutgoingTable",       120 }, // SessionSettings.h:120
    { "PostgreSQLLogEventTable",          121 }, // SessionSettings.h:121
    { "OdbcLogUser",                      122 }, // SessionSettings.h:122
    { "OdbcLogPassword",                  123 }, // SessionSettings.h:123
    { "OdbcLogConnectionString",          124 }, // SessionSettings.h:124
    { "OdbcLogIncomingTable",             125 }, // SessionSettings.h:125
    { "OdbcLogOutgoingTable",             126 }, // SessionSettings.h:126
    { "OdbcLogEventTable",                127 }, // SessionSettings.h:127

    // ── Reset / refresh ───────────────────────────────────────────────────────
    { "ResetOnLogon",                     128 }, // SessionSettings.h:128
    { "ResetOnLogout",                    129 }, // SessionSettings.h:129
    { "ResetOnDisconnect",                130 }, // SessionSettings.h:130
    { "RefreshOnLogon",                   131 }, // SessionSettings.h:131

    // ── Timestamp ─────────────────────────────────────────────────────────────
    { "MillisecondsInTimeStamp",          132 }, // SessionSettings.h:132
    { "TimestampPrecision",               133 }, // SessionSettings.h:133

    // ── HTTP ─────────────────────────────────────────────────────────────────
    { "HttpAcceptPort",                   134 }, // SessionSettings.h:134

    // ── Persistence ──────────────────────────────────────────────────────────
    { "PersistMessages",                  135 }, // SessionSettings.h:135

    // ── TLS / SSL ─────────────────────────────────────────────────────────────
    { "ServerCertificateFile",            136 }, // SessionSettings.h:136
    { "ServerCertificateKeyFile",         137 }, // SessionSettings.h:137
    { "ClientCertificateFile",            138 }, // SessionSettings.h:138
    { "ClientCertificateKeyFile",         139 }, // SessionSettings.h:139
    { "CertificationAuthoritiesFile",     140 }, // SessionSettings.h:140
    { "CertificationAuthoritiesDirectory",141 }, // SessionSettings.h:141
    { "CertificateRevocationListFile",    142 }, // SessionSettings.h:142
    { "CertificateRevocationListDirectory",143}, // SessionSettings.h:143
    { "CertificateVerifyLevel",           144 }, // SessionSettings.h:144
    { "AllowedRemoteAddresses",           145 }, // SessionSettings.h:145
    { "SSLProtocol",                      190 }, // SessionSettings.h:190
    { "SSLCipherSuite",                   223 }, // SessionSettings.h:223
    { "TLSCipherSuites",                  239 }, // SessionSettings.h:239
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
    Disposition      disposition;
    // For Mapped rows: the concrete 044 loader key accepted by scalar_mappers.cpp
    // or selector_resolver.cpp.  Empty for all non-Mapped dispositions.
    std::string_view key_044;
    // Human-readable rationale (required for non-Mapped rows, recommended for all).
    std::string_view notes;
};

// ── The parity table ──────────────────────────────────────────────────────────
// Source for 044 key spellings: src/config/scalar_mappers.cpp (lines 231–679)
// and src/config/selector_resolver.cpp (lines 51–652).
// clang-format off
const std::vector<ParityRow> kParity = {
    // ── Session identity ──────────────────────────────────────────────────────
    { "BeginString",     Disposition::Mapped,      "begin_string",
      "scalar_mappers.cpp:239 — SessionConfig::begin_string" },
    { "SenderCompID",    Disposition::Mapped,      "sender_comp_id",
      "scalar_mappers.cpp:231 — SessionConfig::sender_comp_id" },
    { "TargetCompID",    Disposition::Mapped,      "target_comp_id",
      "scalar_mappers.cpp:235 — SessionConfig::target_comp_id" },
    { "SessionQualifier",Disposition::Gap,         "",
      "No 044 equivalent; fixpp sessions are uniquely identified by "
      "sender_comp_id + target_comp_id + begin_string. The QF qualifier "
      "disambiguates multiple sessions to the same counterparty — a use case "
      "not yet addressed in the v1.0 session model." },
    { "DefaultApplVerID",Disposition::Mapped,      "default_appl_ver_id",
      "scalar_mappers.cpp:561 — SessionConfig::default_appl_ver_id "
      "(required for FIXT.1.1; omitted for FIX 4.x)" },
    { "ConnectionType",  Disposition::Mapped,      "role",
      "scalar_mappers.cpp:281 — SessionConfig::role "
      "(\"initiator\"/\"acceptor\" ↔ QF ConnectionType)" },

    // ── Protocol flags ────────────────────────────────────────────────────────
    { "UseDataDictionary", Disposition::OutOfScope_DdValidationFlags, "",
      "044 always uses the DataDictionary when the 'dictionary' selector is "
      "present; there is no separate enable/disable toggle — 'dictionary' "
      "present ↔ enabled, absent ↔ disabled." },
    { "SendResetSeqNumFlag", Disposition::Mapped,  "reset_seqnum_policy",
      "scalar_mappers.cpp:475 — reset_seqnum_policy enum controls the "
      "ResetSeqNumFlag (141) handshake strategy; 'bilateral_strict' / "
      "'bilateral_lenient' / 'unilateral' span the QF yes/no space." },
    { "SendRedundantResendRequests", Disposition::Gap, "",
      "No 044 equivalent. QF allows suppressing duplicate ResendRequest re-sends "
      "when the peer has not advanced. fixpp does not expose this knob in v1.0." },
    { "SendNextExpectedMsgSeqNum", Disposition::Mapped, "enable_next_expected_msg_seq_num",
      "scalar_mappers.cpp:457 — SessionConfig::enable_next_expected_msg_seq_num; "
      "controls 789/NextExpectedMsgSeqNum in Logon (FIX 5.0+)" },

    // ── Dictionary paths ──────────────────────────────────────────────────────
    { "DataDictionary",           Disposition::Mapped, "dictionary",
      "selector_resolver.cpp:298 — 'dictionary' selector, kind=\"path\"; "
      "the DataDictionary= path maps to [dictionary] kind=\"path\" path=\"…\". "
      "By-version resolution is deferred (OQ-1 option A)." },
    { "TransportDataDictionary",  Disposition::OutOfScope_DdValidationFlags, "",
      "FIXT.1.1 transport dictionary separate from the app dictionary. "
      "044 step-1 does not distinguish transport vs. app dictionaries — "
      "the single 'dictionary' selector covers the combined role. "
      "Deferred to step-2 if per-layer dicts are needed." },
    { "AppDataDictionary",        Disposition::OutOfScope_DdValidationFlags, "",
      "FIXT.1.1 per-ApplVerID app dictionary. Same rationale as "
      "TransportDataDictionary above." },

    // ── Time / clock (scheduling — out of scope) ──────────────────────────────
    { "UseLocalTime",    Disposition::OutOfScope_Schedule, "",
      "Session scheduling: whether StartTime/EndTime are in local time. "
      "Not applicable — 044 has no session-schedule window." },
    { "StartTime",       Disposition::OutOfScope_Schedule, "",
      "Session scheduling: UTC time-of-day the session window opens. "
      "444 step-1 has no session-schedule concept." },
    { "EndTime",         Disposition::OutOfScope_Schedule, "",
      "Session scheduling: UTC time-of-day the session window closes." },
    { "StartDay",        Disposition::OutOfScope_Schedule, "",
      "Session scheduling: day-of-week the session window starts." },
    { "EndDay",          Disposition::OutOfScope_Schedule, "",
      "Session scheduling: day-of-week the session window ends." },
    { "NonStopSession",  Disposition::OutOfScope_Schedule, "",
      "Session scheduling: disables the time-window gate (always active). "
      "044 sessions are non-stop by default; no toggle needed." },
    { "LogonTime",       Disposition::OutOfScope_Schedule, "",
      "Session scheduling: sub-window for Logon initiation." },
    { "LogoutTime",      Disposition::OutOfScope_Schedule, "",
      "Session scheduling: sub-window for Logout." },
    { "LogonDay",        Disposition::OutOfScope_Schedule, "",
      "Session scheduling: day-of-week the Logon window applies." },
    { "LogoutDay",       Disposition::OutOfScope_Schedule, "",
      "Session scheduling: day-of-week the Logout window applies." },

    // ── Validation ────────────────────────────────────────────────────────────
    { "CheckCompID",      Disposition::Mapped,     "check_comp_id",
      "scalar_mappers.cpp:460 — SessionConfig::check_comp_id" },
    { "CheckLatency",     Disposition::Gap,        "",
      "No 044 equivalent for the boolean enable/disable of latency checking; "
      "fixpp uses 'sending_time_threshold' (a Duration) which when set implicitly "
      "enables the check. The QF boolean has no direct mapping." },
    { "MaxLatency",       Disposition::Mapped,     "sending_time_threshold",
      "scalar_mappers.cpp:411 — SessionConfig::sending_time_threshold "
      "(a Duration; when set, enables the SendingTime latency guard). "
      "NOTE: the semantics differ slightly — QF MaxLatency is an integer "
      "(seconds); 044 sending_time_threshold is a duration string (e.g. \"120s\"). "
      "No loss of behavior; the guard fires equivalently." },
    { "HeartBtInt",       Disposition::Mapped,     "heartbeat_interval",
      "scalar_mappers.cpp:390 — SessionConfig::heartbeat_interval" },

    // ── Socket / network ─────────────────────────────────────────────────────
    { "SocketAcceptPort",     Disposition::OutOfScope_ServerInfra, "",
      "Acceptor bind port: engine/infra level, not per-session TOML. "
      "fixpp acceptors are configured at the network layer independently." },
    { "SocketReuseAddress",   Disposition::OutOfScope_SocketTuning, "",
      "OS socket option: SO_REUSEADDR. Engine/infra knob; no per-session TOML." },
    { "SocketConnectHost",    Disposition::Mapped,  "transport.host",
      "selector_resolver.cpp:628 — reconnect_endpoint.host; "
      "maps to 'transport.host' in the [transport] selector table." },
    { "SocketConnectPort",    Disposition::Mapped,  "transport.port",
      "selector_resolver.cpp:628 — reconnect_endpoint.port; "
      "maps to 'transport.port' in the [transport] selector table." },
    { "SocketConnectSourceHost", Disposition::OutOfScope_SocketTuning, "",
      "OS-level bind-before-connect source address; no per-session TOML." },
    { "SocketConnectSourcePort", Disposition::OutOfScope_SocketTuning, "",
      "OS-level bind-before-connect source port; no per-session TOML." },
    { "SocketNodelay",        Disposition::OutOfScope_SocketTuning, "",
      "TCP_NODELAY socket option; engine/infra tuning, not per-session TOML." },
    { "SocketSendBufferSize", Disposition::OutOfScope_SocketTuning, "",
      "SO_SNDBUF socket option; engine/infra tuning, not per-session TOML." },
    { "SocketReceiveBufferSize", Disposition::OutOfScope_SocketTuning, "",
      "SO_RCVBUF socket option; engine/infra tuning, not per-session TOML." },
    { "HostSelectionPolicy",  Disposition::OutOfScope_ServerInfra, "",
      "Multi-host failover policy (ROUND_ROBIN / RANDOM / PRIORITY); "
      "fixpp v1.0 has a single reconnect endpoint per session. "
      "Multi-host failover is not yet modeled." },
    { "HostSelectionPolicyPriorityStartOverInterval", Disposition::OutOfScope_ServerInfra, "",
      "Companion to HostSelectionPolicy (priority restart interval); "
      "same rationale." },
    { "ReconnectInterval",    Disposition::Deferred, "",
      "Reconnect retry interval. fixpp v1.0 reconnect policy has its own "
      "reconnect_policy selector with similar semantics; an explicit integer "
      "key is not yet wired. Deferred to step-2." },

    // ── Wire validation flags ─────────────────────────────────────────────────
    { "ValidateLengthAndChecksum", Disposition::Gap, "",
      "No 044 equivalent for disabling the framer checksum/length validation; "
      "fixpp always validates the FIX framing length and checksum (fail-closed)." },
    { "ValidateFieldsOutOfOrder",  Disposition::OutOfScope_DdValidationFlags, "",
      "DataDictionary-level validation flag (passed to DD constructor). "
      "044 validate_inbound_messages enables DD-driven validation globally; "
      "per-flag DD construction options are not exposed in step 1." },
    { "ValidateFieldsHaveValues",  Disposition::OutOfScope_DdValidationFlags, "",
      "DataDictionary-level validation flag; same rationale as above." },
    { "ValidateUserDefinedFields", Disposition::OutOfScope_DdValidationFlags, "",
      "DataDictionary-level validation flag; same rationale as above." },
    { "AllowUnknownMsgFields",     Disposition::OutOfScope_DdValidationFlags, "",
      "DataDictionary-level validation flag; same rationale as above." },
    { "PreserveMessageFieldsOrder",Disposition::OutOfScope_DdValidationFlags, "",
      "DataDictionary flag for preserving field order in output; "
      "no equivalent in 044 step-1." },

    // ── Timeouts ─────────────────────────────────────────────────────────────
    { "LogonTimeout",     Disposition::Gap, "",
      "No exact 044 equivalent; fixpp drives Logon retry/timeout via the "
      "reconnect_policy timers, not a named LogonTimeout scalar." },
    { "LogoutTimeout",    Disposition::Mapped, "logout_disconnect_timeout_ms",
      "scalar_mappers.cpp:423 — SessionConfig::logout_disconnect_timeout_ms "
      "(integer ms; QF LogoutTimeout is integer seconds — different units, "
      "same semantic: how long to wait for peer Logout ACK before hard-close)." },

    // ── Store backends ────────────────────────────────────────────────────────
    { "FileStorePath",                    Disposition::Mapped, "store.directory",
      "selector_resolver.cpp:165 — 'store' selector, kind=\"file\"; "
      "FileStorePath= maps to [store] kind=\"file\" directory=\"…\"." },
    { "MySQLStoreUseConnectionPool",      Disposition::OutOfScope_DbStoreBackend, "",
      "MySQL store backend; 044 step-1 stores: {file, memory} only. "
      "MySQL adapter is out-of-scope." },
    { "MySQLStoreDatabase",               Disposition::OutOfScope_DbStoreBackend, "",
      "MySQL store backend; same rationale." },
    { "MySQLStoreUser",                   Disposition::OutOfScope_DbStoreBackend, "",
      "MySQL store backend; same rationale." },
    { "MySQLStorePassword",               Disposition::OutOfScope_DbStoreBackend, "",
      "MySQL store backend; same rationale." },
    { "MySQLStoreHost",                   Disposition::OutOfScope_DbStoreBackend, "",
      "MySQL store backend; same rationale." },
    { "MySQLStorePort",                   Disposition::OutOfScope_DbStoreBackend, "",
      "MySQL store backend; same rationale." },
    { "PostgreSQLStoreUseConnectionPool", Disposition::OutOfScope_DbStoreBackend, "",
      "PostgreSQL store backend; 044 step-1 stores: {file, memory} only." },
    { "PostgreSQLStoreDatabase",          Disposition::OutOfScope_DbStoreBackend, "",
      "PostgreSQL store backend; same rationale." },
    { "PostgreSQLStoreUser",              Disposition::OutOfScope_DbStoreBackend, "",
      "PostgreSQL store backend; same rationale." },
    { "PostgreSQLStorePassword",          Disposition::OutOfScope_DbStoreBackend, "",
      "PostgreSQL store backend; same rationale." },
    { "PostgreSQLStoreHost",              Disposition::OutOfScope_DbStoreBackend, "",
      "PostgreSQL store backend; same rationale." },
    { "PostgreSQLStorePort",              Disposition::OutOfScope_DbStoreBackend, "",
      "PostgreSQL store backend; same rationale." },
    { "OdbcStoreUser",                    Disposition::OutOfScope_DbStoreBackend, "",
      "ODBC store backend; 044 step-1 stores: {file, memory} only." },
    { "OdbcStorePassword",                Disposition::OutOfScope_DbStoreBackend, "",
      "ODBC store backend; same rationale." },
    { "OdbcStoreConnectionString",        Disposition::OutOfScope_DbStoreBackend, "",
      "ODBC store backend; same rationale." },

    // ── Log sinks ─────────────────────────────────────────────────────────────
    { "FileLogPath",                    Disposition::OutOfScope_LogSink, "",
      "File log sink; observability = 044 step-2 (FR-009). "
      "Out-of-scope for step-1 config loader." },
    { "FileLogBackupPath",              Disposition::OutOfScope_LogSink, "",
      "File log sink (backup path); same rationale." },
    { "ScreenLogShowIncoming",          Disposition::OutOfScope_LogSink, "",
      "Screen/console log sink; same rationale." },
    { "ScreenLogShowOutgoing",          Disposition::OutOfScope_LogSink, "",
      "Screen/console log sink; same rationale." },
    { "ScreenLogShowEvents",            Disposition::OutOfScope_LogSink, "",
      "Screen/console log sink; same rationale." },
    { "MySQLLogUseConnectionPool",      Disposition::OutOfScope_LogSink, "",
      "MySQL log sink; same rationale." },
    { "MySQLLogDatabase",               Disposition::OutOfScope_LogSink, "",
      "MySQL log sink; same rationale." },
    { "MySQLLogUser",                   Disposition::OutOfScope_LogSink, "",
      "MySQL log sink; same rationale." },
    { "MySQLLogPassword",               Disposition::OutOfScope_LogSink, "",
      "MySQL log sink; same rationale." },
    { "MySQLLogHost",                   Disposition::OutOfScope_LogSink, "",
      "MySQL log sink; same rationale." },
    { "MySQLLogPort",                   Disposition::OutOfScope_LogSink, "",
      "MySQL log sink; same rationale." },
    { "MySQLLogIncomingTable",          Disposition::OutOfScope_LogSink, "",
      "MySQL log sink; same rationale." },
    { "MySQLLogOutgoingTable",          Disposition::OutOfScope_LogSink, "",
      "MySQL log sink; same rationale." },
    { "MySQLLogEventTable",             Disposition::OutOfScope_LogSink, "",
      "MySQL log sink; same rationale." },
    { "PostgreSQLLogUseConnectionPool", Disposition::OutOfScope_LogSink, "",
      "PostgreSQL log sink; same rationale." },
    { "PostgreSQLLogDatabase",          Disposition::OutOfScope_LogSink, "",
      "PostgreSQL log sink; same rationale." },
    { "PostgreSQLLogUser",              Disposition::OutOfScope_LogSink, "",
      "PostgreSQL log sink; same rationale." },
    { "PostgreSQLLogPassword",          Disposition::OutOfScope_LogSink, "",
      "PostgreSQL log sink; same rationale." },
    { "PostgreSQLLogHost",              Disposition::OutOfScope_LogSink, "",
      "PostgreSQL log sink; same rationale." },
    { "PostgreSQLLogPort",              Disposition::OutOfScope_LogSink, "",
      "PostgreSQL log sink; same rationale." },
    { "PostgreSQLLogIncomingTable",     Disposition::OutOfScope_LogSink, "",
      "PostgreSQL log sink; same rationale." },
    { "PostgreSQLLogOutgoingTable",     Disposition::OutOfScope_LogSink, "",
      "PostgreSQL log sink; same rationale." },
    { "PostgreSQLLogEventTable",        Disposition::OutOfScope_LogSink, "",
      "PostgreSQL log sink; same rationale." },
    { "OdbcLogUser",                    Disposition::OutOfScope_LogSink, "",
      "ODBC log sink; same rationale." },
    { "OdbcLogPassword",                Disposition::OutOfScope_LogSink, "",
      "ODBC log sink; same rationale." },
    { "OdbcLogConnectionString",        Disposition::OutOfScope_LogSink, "",
      "ODBC log sink; same rationale." },
    { "OdbcLogIncomingTable",           Disposition::OutOfScope_LogSink, "",
      "ODBC log sink; same rationale." },
    { "OdbcLogOutgoingTable",           Disposition::OutOfScope_LogSink, "",
      "ODBC log sink; same rationale." },
    { "OdbcLogEventTable",              Disposition::OutOfScope_LogSink, "",
      "ODBC log sink; same rationale." },

    // ── Reset / refresh ───────────────────────────────────────────────────────
    { "ResetOnLogon",      Disposition::Mapped, "reset_on_logon",
      "scalar_mappers.cpp:439 — SessionConfig::reset_on_logon" },
    { "ResetOnLogout",     Disposition::Mapped, "reset_on_logout",
      "scalar_mappers.cpp:442 — SessionConfig::reset_on_logout" },
    { "ResetOnDisconnect", Disposition::Mapped, "reset_on_disconnect",
      "scalar_mappers.cpp:445 — SessionConfig::reset_on_disconnect" },
    { "RefreshOnLogon",    Disposition::Mapped, "refresh_on_logon",
      "scalar_mappers.cpp:448 — SessionConfig::refresh_on_logon" },

    // ── Timestamp ─────────────────────────────────────────────────────────────
    { "MillisecondsInTimeStamp", Disposition::Mapped, "sending_time_precision",
      "scalar_mappers.cpp:498 — SessionConfig::sending_time_precision "
      "(QF MillisecondsInTimeStamp=Y ↔ precision=\"millis\"; "
      "044 additionally supports \"micros\" and \"nanos\")." },
    { "TimestampPrecision",      Disposition::Mapped, "sending_time_precision",
      "scalar_mappers.cpp:498 — Same 044 key as MillisecondsInTimeStamp; "
      "TimestampPrecision is the QF successor key (integer: 0=seconds, "
      "3=millis, 6=micros, 9=nanos) — both map to sending_time_precision." },

    // ── HTTP ─────────────────────────────────────────────────────────────────
    { "HttpAcceptPort",    Disposition::OutOfScope_Http, "",
      "HTTP management interface (QuickFIX monitoring port); "
      "engine-infra, not per-session TOML. Out-of-scope." },

    // ── Persistence ──────────────────────────────────────────────────────────
    { "PersistMessages",   Disposition::Mapped, "store",
      "Indirectly: 'store=memory' implies non-persistent (yields_persistent_store()=false), "
      "mirroring QF PersistMessages=N. 'store=file' gives persistence. "
      "selector_resolver.cpp:159." },

    // ── TLS / SSL ─────────────────────────────────────────────────────────────
    { "ServerCertificateFile",             Disposition::Mapped_TlsViaSelectors, "cert_source.cert_file",
      "selector_resolver.cpp:261 — cert_source selector, kind=\"file\", "
      "cert_file= path (the server-side leaf certificate)." },
    { "ServerCertificateKeyFile",          Disposition::Mapped_TlsViaSelectors, "cert_source.key_file",
      "selector_resolver.cpp:262 — cert_source selector, key_file= path." },
    { "ClientCertificateFile",             Disposition::Mapped_TlsViaSelectors, "cert_source.cert_file",
      "selector_resolver.cpp:261 — same cert_source cert_file; QF uses "
      "separate server/client keys, 044 uses a single role-independent cert_source "
      "(the role is determined by security_profile.kind)." },
    { "ClientCertificateKeyFile",          Disposition::Mapped_TlsViaSelectors, "cert_source.key_file",
      "selector_resolver.cpp:262 — same cert_source key_file." },
    { "CertificationAuthoritiesFile",      Disposition::Mapped_TlsViaSelectors, "cert_source.ca_file",
      "selector_resolver.cpp:263 — cert_source selector, ca_file= path." },
    { "CertificationAuthoritiesDirectory", Disposition::OutOfScope_SocketTuning, "",
      "No 044 equivalent; 044 cert_source accepts a single CA bundle file. "
      "A CA directory is not supported in step-1." },
    { "CertificateRevocationListFile",     Disposition::Gap, "",
      "No 044 equivalent; fixpp does not currently support CRL-based revocation "
      "checking in step-1 (OCSP / CRL not wired)." },
    { "CertificateRevocationListDirectory",Disposition::Gap, "",
      "No 044 equivalent; same rationale as CertificateRevocationListFile." },
    { "CertificateVerifyLevel",            Disposition::Mapped_TlsViaSelectors, "security_profile.kind",
      "selector_resolver.cpp:400 — security_profile.kind encodes the "
      "verification strategy: "
      "mtls_ca=mutual TLS with CA verification (CertificateVerifyLevel=2), "
      "one_way_ca=server-auth only (CertificateVerifyLevel=1), "
      "insecure_plain_tcp=no TLS (CertificateVerifyLevel=0)." },
    { "AllowedRemoteAddresses",            Disposition::Gap, "",
      "No 044 equivalent; fixpp does not expose a per-session IP allowlist in v1.0." },
    { "SSLProtocol",                       Disposition::Gap, "",
      "No 044 equivalent; fixpp defers TLS protocol selection to the "
      "system OpenSSL defaults. An explicit protocol string is not configurable "
      "via TOML in step-1." },
    { "SSLCipherSuite",                    Disposition::Gap, "",
      "No 044 equivalent; cipher suite selection is not exposed in step-1 TOML." },
    { "TLSCipherSuites",                   Disposition::Gap, "",
      "No 044 equivalent; TLS 1.3 cipher suite selection is not exposed in step-1 TOML." },
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

} // anonymous namespace

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
        if (!covered.count(k)) {
            missing.push_back(k);
        }
    }

    std::vector<std::string_view> extra;
    for (const auto& k : covered) {
        if (!oracle.count(k)) {
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
        EXPECT_FALSE(row.notes.empty())
            << "Row for QF key '" << row.qf_key << "' has empty notes";
    }
}

// Test 3: every Mapped / Mapped_TlsViaSelectors row's 044 key is accepted
//         by the loader (kLoaderAccepted044Keys).  Catches invented or
//         mistyped 044 keys.
TEST(QuickFixParityTable, MappedKeysExistInLoader) {
    for (const auto& row : kParity) {
        bool is_mapped = (row.disposition == Disposition::Mapped
                          || row.disposition == Disposition::Mapped_TlsViaSelectors);
        if (!is_mapped) {
            continue;
        }
        EXPECT_FALSE(row.key_044.empty())
            << "Row for QF key '" << row.qf_key
            << "' is Mapped but has empty key_044";
        EXPECT_TRUE(kLoaderAccepted044Keys.count(row.key_044))
            << "Row for QF key '" << row.qf_key
            << "' claims 044 key '" << row.key_044
            << "' which is NOT in kLoaderAccepted044Keys";
    }
}

// Test 4: non-Mapped rows have empty key_044 (no phantom mapping).
TEST(QuickFixParityTable, NonMappedRowsHaveNoKey044) {
    for (const auto& row : kParity) {
        bool is_mapped = (row.disposition == Disposition::Mapped
                          || row.disposition == Disposition::Mapped_TlsViaSelectors);
        if (is_mapped) {
            continue;
        }
        EXPECT_TRUE(row.key_044.empty())
            << "Non-Mapped row for QF key '" << row.qf_key
            << "' unexpectedly has key_044 = '" << row.key_044 << "'";
    }
}

// Test 5: schedule keys are all classified as OutOfScope_Schedule.
TEST(QuickFixParityTable, ScheduleKeysAreOutOfScope) {
    const std::set<std::string_view> schedule_keys = {
        "StartTime", "EndTime", "StartDay", "EndDay",
        "NonStopSession", "UseLocalTime",
        "LogonTime", "LogoutTime", "LogonDay", "LogoutDay",
    };
    for (const auto& row : kParity) {
        if (schedule_keys.count(row.qf_key)) {
            EXPECT_EQ(row.disposition, Disposition::OutOfScope_Schedule)
                << "Schedule key '" << row.qf_key
                << "' must be OutOfScope_Schedule";
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
    EXPECT_EQ(gaps, 11)
        << "Unexpected number of Gap rows; update the count when gaps are "
           "resolved or new ones are identified";
}
