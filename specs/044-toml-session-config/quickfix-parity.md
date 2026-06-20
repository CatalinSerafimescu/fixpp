# QuickFIX-cpp Vocabulary Parity (SC-004)

**Feature**: `044-toml-session-config` · **Step**: 1

This table documents how every QuickFIX-cpp session-configuration key maps
to its 044 TOML equivalent, or records an explicit out-of-scope / deferred
rationale.  It is the human-readable companion to the machine-checked
`test_quickfix_parity_table` (T030 / SC-004).

## Sources

- **QF key oracle**: `reference-engines/quickfix-cpp/include/quickfix/SessionSettings.h`
  (lines 36–239, `const char[]` definitions in namespace `FIX`).
- **QF factory consumption**: `reference-engines/quickfix-cpp/src/C++/SessionFactory.cpp`.
- **044 loader key spellings**: `src/config/scalar_mappers.cpp` and
  `src/config/selector_resolver.cpp`.
- **Design decisions**: `specs/044-toml-session-config/research.md` (D-10).

The `[default]` + `[[session]]` deep-merge (D-8) provides the direct equivalent
of QuickFIX `[DEFAULT]`/`[SESSION]` section inheritance.  This is validated by
the T031 multi-session test (`test_load_multisession_defaults`).

---

## Disposition key

| Symbol | Meaning |
|--------|---------|
| **Mapped** | Direct 044 equivalent; 044 key accepted by the loader |
| **Mapped (TLS selectors)** | Maps through `cert_source` / `security_profile` selectors |
| **Out-of-scope: Schedule** | Session *scheduling*, not establishment — not a parity gap |
| **Out-of-scope: Server infra** | Engine / listener binding — not per-session TOML |
| **Out-of-scope: Socket tuning** | OS-level socket knob — not per-session TOML |
| **Out-of-scope: DB store** | MySQL / PostgreSQL / ODBC store adapters — step-1 stores are `{file, memory}` |
| **Out-of-scope: Log sink** | Observability = step-2 (FR-009) |
| **Out-of-scope: DD flags** | DataDictionary validation flags — not per-session TOML in step-1 |
| **Out-of-scope: HTTP** | HTTP management interface — engine-infra |
| **Deferred** | Has a 044 analogue but not yet wired via TOML in step-1 |
| **Gap** | No 044 equivalent; see findings below |

---

## Session identity

| QF key (`SessionSettings.h` line) | 044 key | Disposition | Notes |
|------------------------------------|---------|-------------|-------|
| `BeginString` (36) | `begin_string` | **Mapped** | `scalar_mappers.cpp:239` — `SessionConfig::begin_string` |
| `SenderCompID` (37) | `sender_comp_id` | **Mapped** | `scalar_mappers.cpp:231` — `SessionConfig::sender_comp_id` |
| `TargetCompID` (38) | `target_comp_id` | **Mapped** | `scalar_mappers.cpp:235` — `SessionConfig::target_comp_id` |
| `SessionQualifier` (39) | — | **Gap** | fixpp sessions are identified by `sender_comp_id + target_comp_id + begin_string`; no disambiguation qualifier in v1.0 |
| `DefaultApplVerID` (40) | `default_appl_ver_id` | **Mapped** | `scalar_mappers.cpp:561`; required when `begin_string="FIXT.1.1"`, omitted for FIX 4.x |
| `ConnectionType` (41) | `role` | **Mapped** | `scalar_mappers.cpp:281`; `"initiator"` / `"acceptor"` ↔ QF `ConnectionType` |

---

## Protocol flags

| QF key (line) | 044 key | Disposition | Notes |
|----------------|---------|-------------|-------|
| `UseDataDictionary` (42) | — | Out-of-scope: DD flags | 044 enables DD validation implicitly when the `dictionary` selector is present; no separate toggle needed |
| `SendResetSeqNumFlag` (43) | `reset_seqnum_policy` | **Mapped** | `scalar_mappers.cpp:475`; `bilateral_strict` / `bilateral_lenient` / `unilateral` span the QF yes/no space |
| `SendRedundantResendRequests` (44) | — | **Gap** | No 044 knob for suppressing duplicate ResendRequest re-sends |
| `SendNextExpectedMsgSeqNum` (45) | `enable_next_expected_msg_seq_num` | **Mapped** | `scalar_mappers.cpp:457`; controls 789/NextExpectedMsgSeqNum in Logon (FIX 5.0+) |

---

## Dictionary paths

| QF key (line) | 044 key | Disposition | Notes |
|----------------|---------|-------------|-------|
| `DataDictionary` (46) | `dictionary` | **Mapped** | `selector_resolver.cpp:298`; `[dictionary] kind="path" path="…"` mirrors QF `DataDictionary=FIX44.xml` |
| `TransportDataDictionary` (47) | — | Out-of-scope: DD flags | FIXT transport DD; 044 step-1 has a single `dictionary` selector; per-layer separation deferred |
| `AppDataDictionary` (48) | — | Out-of-scope: DD flags | FIXT per-ApplVerID app DD; same rationale |

---

## Session scheduling (all out-of-scope)

These keys control *when* a session is active, not *how* it is established.
044 has no session-schedule window concept — out-of-scope for this step.

| QF key (line) | Disposition |
|----------------|-------------|
| `UseLocalTime` (49) | Out-of-scope: Schedule |
| `StartTime` (50) | Out-of-scope: Schedule |
| `EndTime` (51) | Out-of-scope: Schedule |
| `StartDay` (52) | Out-of-scope: Schedule |
| `EndDay` (53) | Out-of-scope: Schedule |
| `NonStopSession` (54) | Out-of-scope: Schedule (044 sessions are non-stop by default) |
| `LogonTime` (55) | Out-of-scope: Schedule |
| `LogoutTime` (56) | Out-of-scope: Schedule |
| `LogonDay` (57) | Out-of-scope: Schedule |
| `LogoutDay` (58) | Out-of-scope: Schedule |

---

## Validation and heartbeat

| QF key (line) | 044 key | Disposition | Notes |
|----------------|---------|-------------|-------|
| `CheckCompID` (59) | `check_comp_id` | **Mapped** | `scalar_mappers.cpp:460` |
| `CheckLatency` (60) | — | **Gap** | QF boolean enable; 044 implicitly enables the check when `sending_time_threshold` is set; no bare boolean knob |
| `MaxLatency` (61) | `sending_time_threshold` | **Mapped** | `scalar_mappers.cpp:411`; QF = integer seconds, 044 = duration string (e.g. `"120s"`); same semantic |
| `HeartBtInt` (62) | `heartbeat_interval` | **Mapped** | `scalar_mappers.cpp:390` |

---

## Socket / network

| QF key (line) | 044 key | Disposition | Notes |
|----------------|---------|-------------|-------|
| `SocketAcceptPort` (63) | — | Out-of-scope: Server infra | Acceptor bind port; engine/listener level |
| `SocketReuseAddress` (64) | — | Out-of-scope: Socket tuning | SO_REUSEADDR |
| `SocketConnectHost` (65) | `transport.host` | **Mapped** | `selector_resolver.cpp`; maps to `reconnect_endpoint.host` |
| `SocketConnectPort` (66) | `transport.port` | **Mapped** | `selector_resolver.cpp`; maps to `reconnect_endpoint.port` |
| `SocketConnectSourceHost` (67) | — | Out-of-scope: Socket tuning | Bind-before-connect source address |
| `SocketConnectSourcePort` (68) | — | Out-of-scope: Socket tuning | Bind-before-connect source port |
| `SocketNodelay` (69) | — | Out-of-scope: Socket tuning | TCP_NODELAY |
| `SocketSendBufferSize` (70) | — | Out-of-scope: Socket tuning | SO_SNDBUF |
| `SocketReceiveBufferSize` (71) | — | Out-of-scope: Socket tuning | SO_RCVBUF |
| `HostSelectionPolicy` (72) | — | Out-of-scope: Server infra | Multi-host failover; v1.0 has single endpoint per session |
| `HostSelectionPolicyPriorityStartOverInterval` (73) | — | Out-of-scope: Server infra | Companion to HostSelectionPolicy |
| `ReconnectInterval` (74) | — | **Deferred** | Reconnect timing; 044 has `reconnect_policy` selector but integer-key wiring deferred |

---

## Wire validation flags

| QF key (line) | 044 key | Disposition | Notes |
|----------------|---------|-------------|-------|
| `ValidateLengthAndChecksum` (75) | — | **Gap** | fixpp always validates FIX framing (fail-closed); no disable knob |
| `ValidateFieldsOutOfOrder` (76) | — | Out-of-scope: DD flags | DD-level constructor flag; not per-session TOML in step-1 |
| `ValidateFieldsHaveValues` (77) | — | Out-of-scope: DD flags | DD-level flag |
| `ValidateUserDefinedFields` (78) | — | Out-of-scope: DD flags | DD-level flag |
| `AllowUnknownMsgFields` (79) | — | Out-of-scope: DD flags | DD-level flag |
| `PreserveMessageFieldsOrder` (80) | — | Out-of-scope: DD flags | DD output-ordering flag |

---

## Timeouts

| QF key (line) | 044 key | Disposition | Notes |
|----------------|---------|-------------|-------|
| `LogonTimeout` (81) | — | **Gap** | fixpp drives Logon retry via `reconnect_policy` timers, not a named scalar |
| `LogoutTimeout` (82) | `logout_disconnect_timeout_ms` | **Mapped** | `scalar_mappers.cpp:423`; QF = integer seconds, 044 = integer ms |

---

## Store backends

| QF key (line) | 044 key | Disposition | Notes |
|----------------|---------|-------------|-------|
| `FileStorePath` (83) | `store.directory` | **Mapped** | `selector_resolver.cpp:165`; `[store] kind="file" directory="…"` |
| `MySQLStoreUseConnectionPool` (84) | — | Out-of-scope: DB store | Step-1 stores: `{file, memory}` only |
| `MySQLStoreDatabase` (85) | — | Out-of-scope: DB store | |
| `MySQLStoreUser` (86) | — | Out-of-scope: DB store | |
| `MySQLStorePassword` (87) | — | Out-of-scope: DB store | |
| `MySQLStoreHost` (88) | — | Out-of-scope: DB store | |
| `MySQLStorePort` (89) | — | Out-of-scope: DB store | |
| `PostgreSQLStoreUseConnectionPool` (90) | — | Out-of-scope: DB store | |
| `PostgreSQLStoreDatabase` (91) | — | Out-of-scope: DB store | |
| `PostgreSQLStoreUser` (92) | — | Out-of-scope: DB store | |
| `PostgreSQLStorePassword` (93) | — | Out-of-scope: DB store | |
| `PostgreSQLStoreHost` (94) | — | Out-of-scope: DB store | |
| `PostgreSQLStorePort` (95) | — | Out-of-scope: DB store | |
| `OdbcStoreUser` (96) | — | Out-of-scope: DB store | |
| `OdbcStorePassword` (97) | — | Out-of-scope: DB store | |
| `OdbcStoreConnectionString` (98) | — | Out-of-scope: DB store | |

---

## Log sinks (all out-of-scope: observability = step-2, FR-009)

| QF key (line) | Disposition |
|----------------|-------------|
| `FileLogPath` (99) | Out-of-scope: Log sink |
| `FileLogBackupPath` (100) | Out-of-scope: Log sink |
| `ScreenLogShowIncoming` (101) | Out-of-scope: Log sink |
| `ScreenLogShowOutgoing` (102) | Out-of-scope: Log sink |
| `ScreenLogShowEvents` (103) | Out-of-scope: Log sink |
| `MySQLLogUseConnectionPool` (104) | Out-of-scope: Log sink |
| `MySQLLogDatabase` (105) | Out-of-scope: Log sink |
| `MySQLLogUser` (106) | Out-of-scope: Log sink |
| `MySQLLogPassword` (107) | Out-of-scope: Log sink |
| `MySQLLogHost` (108) | Out-of-scope: Log sink |
| `MySQLLogPort` (109) | Out-of-scope: Log sink |
| `MySQLLogIncomingTable` (110) | Out-of-scope: Log sink |
| `MySQLLogOutgoingTable` (111) | Out-of-scope: Log sink |
| `MySQLLogEventTable` (112) | Out-of-scope: Log sink |
| `PostgreSQLLogUseConnectionPool` (113) | Out-of-scope: Log sink |
| `PostgreSQLLogDatabase` (114) | Out-of-scope: Log sink |
| `PostgreSQLLogUser` (115) | Out-of-scope: Log sink |
| `PostgreSQLLogPassword` (116) | Out-of-scope: Log sink |
| `PostgreSQLLogHost` (117) | Out-of-scope: Log sink |
| `PostgreSQLLogPort` (118) | Out-of-scope: Log sink |
| `PostgreSQLLogIncomingTable` (119) | Out-of-scope: Log sink |
| `PostgreSQLLogOutgoingTable` (120) | Out-of-scope: Log sink |
| `PostgreSQLLogEventTable` (121) | Out-of-scope: Log sink |
| `OdbcLogUser` (122) | Out-of-scope: Log sink |
| `OdbcLogPassword` (123) | Out-of-scope: Log sink |
| `OdbcLogConnectionString` (124) | Out-of-scope: Log sink |
| `OdbcLogIncomingTable` (125) | Out-of-scope: Log sink |
| `OdbcLogOutgoingTable` (126) | Out-of-scope: Log sink |
| `OdbcLogEventTable` (127) | Out-of-scope: Log sink |

---

## Reset / refresh

| QF key (line) | 044 key | Disposition | Notes |
|----------------|---------|-------------|-------|
| `ResetOnLogon` (128) | `reset_on_logon` | **Mapped** | `scalar_mappers.cpp:439` |
| `ResetOnLogout` (129) | `reset_on_logout` | **Mapped** | `scalar_mappers.cpp:442` |
| `ResetOnDisconnect` (130) | `reset_on_disconnect` | **Mapped** | `scalar_mappers.cpp:445` |
| `RefreshOnLogon` (131) | `refresh_on_logon` | **Mapped** | `scalar_mappers.cpp:448` |

---

## Timestamp / precision

| QF key (line) | 044 key | Disposition | Notes |
|----------------|---------|-------------|-------|
| `MillisecondsInTimeStamp` (132) | `sending_time_precision` | **Mapped** | `scalar_mappers.cpp:498`; `Y` ↔ `"millis"` |
| `TimestampPrecision` (133) | `sending_time_precision` | **Mapped** | `scalar_mappers.cpp:498`; QF successor key (integer 0/3/6/9); 044 uses named enum `"seconds"` / `"millis"` / `"micros"` / `"nanos"` |

---

## HTTP management

| QF key (line) | Disposition | Notes |
|----------------|-------------|-------|
| `HttpAcceptPort` (134) | Out-of-scope: HTTP | Engine-infra monitoring port; not per-session TOML |

---

## Persistence

| QF key (line) | 044 key | Disposition | Notes |
|----------------|---------|-------------|-------|
| `PersistMessages` (135) | `store` | **Mapped** | Indirectly: `store=memory` → `yields_persistent_store()=false` (QF `PersistMessages=N`); `store=file` → persistent. `selector_resolver.cpp:159`. |

---

## TLS / SSL

044 maps QF's individual TLS keys to two selectors: `cert_source` (certificate
material) and `security_profile` (verification mode).

| QF key (line) | 044 key | Disposition | Notes |
|----------------|---------|-------------|-------|
| `ServerCertificateFile` (136) | `cert_source.cert_file` | **Mapped (TLS selectors)** | `selector_resolver.cpp:261` |
| `ServerCertificateKeyFile` (137) | `cert_source.key_file` | **Mapped (TLS selectors)** | `selector_resolver.cpp:262` |
| `ClientCertificateFile` (138) | `cert_source.cert_file` | **Mapped (TLS selectors)** | Same `cert_file`; role determined by `security_profile.kind` |
| `ClientCertificateKeyFile` (139) | `cert_source.key_file` | **Mapped (TLS selectors)** | Same `cert_source.key_file` |
| `CertificationAuthoritiesFile` (140) | `cert_source.ca_file` | **Mapped (TLS selectors)** | `selector_resolver.cpp:263` |
| `CertificationAuthoritiesDirectory` (141) | — | Out-of-scope: Socket tuning | 044 `cert_source` accepts a single CA bundle file; CA directory not supported in step-1 |
| `CertificateRevocationListFile` (142) | — | **Gap** | CRL-based revocation not wired in step-1 |
| `CertificateRevocationListDirectory` (143) | — | **Gap** | Same rationale |
| `CertificateVerifyLevel` (144) | `security_profile.kind` | **Mapped (TLS selectors)** | `mtls_ca` = mutual TLS (level 2), `one_way_ca` = server-auth (level 1), `insecure_plain_tcp` = no TLS (level 0) |
| `AllowedRemoteAddresses` (145) | — | **Gap** | Per-session IP allowlist not exposed in v1.0 |
| `SSLProtocol` (190) | — | **Gap** | TLS protocol selection deferred to OpenSSL defaults in step-1 |
| `SSLCipherSuite` (223) | — | **Gap** | TLS 1.2 cipher suite selection not exposed in step-1 |
| `TLSCipherSuites` (239) | — | **Gap** | TLS 1.3 cipher suite selection not exposed in step-1 |

---

## [default] / [SESSION] parity

The `[default]` top-level TOML table is deep-merged under each `[[session]]`
before validation, with per-session keys overriding.  This is the direct
equivalent of QuickFIX's `[DEFAULT]`/`[SESSION]` two-level config model.
Validated by T031 (`test_load_multisession_defaults`).

---

## Parity summary

| Category | Mapped | Out-of-scope | Deferred | Gaps |
|----------|--------|--------------|----------|------|
| Session identity | 5 | 0 | 0 | 1 (SessionQualifier) |
| Protocol flags | 2 | 1 | 0 | 1 (SendRedundantResendRequests) |
| Dictionary | 1 | 2 | 0 | 0 |
| Schedule | 0 | 10 | 0 | 0 |
| Validation + heartbeat | 2 | 0 | 0 | 1 (CheckLatency) |
| Socket / network | 2 | 8 | 1 | 0 |
| Wire validation flags | 0 | 6 | 0 | 1 (ValidateLengthAndChecksum) |
| Timeouts | 1 | 0 | 0 | 1 (LogonTimeout) |
| Store backends | 1 | 15 | 0 | 0 |
| Log sinks | 0 | 29 | 0 | 0 |
| Reset / refresh | 4 | 0 | 0 | 0 |
| Timestamp | 2 | 0 | 0 | 0 |
| HTTP | 0 | 1 | 0 | 0 |
| Persistence | 1 | 0 | 0 | 0 |
| TLS / SSL | 5 | 1 | 0 | 5 (CRL×2, AllowedRemoteAddresses, SSLProtocol, SSLCipherSuite, TLSCipherSuites) |

**Total QuickFIX keys enumerated: 101**
**Mapped: 26 · Out-of-scope: 73 · Deferred: 1 · Gaps: 11**

---

## Gap findings (item 6 — genuine parity gaps)

These are QuickFIX establishment settings with no 044 equivalent that arguably
could have one.  They are P3 / v1.1 candidates, not v1.0 blockers.

1. **`SessionQualifier`** — session disambiguation qualifier for multiple sessions
   to the same counterparty.  fixpp v1.0 does not model this.
2. **`SendRedundantResendRequests`** — suppress duplicate ResendRequests when
   peer has not advanced.  No equivalent knob.
3. **`CheckLatency`** — boolean enable/disable for SendingTime latency checking;
   044 implicitly enables via `sending_time_threshold` presence, but has no
   explicit boolean toggle.
4. **`ValidateLengthAndChecksum`** — disable FIX framing validation.  fixpp is
   fail-closed (always validates); providing a disable knob is a deliberate
   design difference.
5. **`LogonTimeout`** — named Logon timeout scalar; fixpp uses `reconnect_policy`
   timers instead.
6. **`CertificateRevocationListFile`** — CRL-based certificate revocation;
   not wired in step-1.
7. **`CertificateRevocationListDirectory`** — same as above.
8. **`AllowedRemoteAddresses`** — per-session IP allowlist.
9. **`SSLProtocol`** — explicit TLS protocol version selection.
10. **`SSLCipherSuite`** — TLS 1.2 cipher suite string.
11. **`TLSCipherSuites`** — TLS 1.3 cipher suite string.
