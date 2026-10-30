> **Work in progress — sandbox project — NOT for production use.**

# fixpp

[![Tier 1 — Linux](https://github.com/CatalinSerafimescu/fixpp/actions/workflows/tier1.yml/badge.svg?branch=main)](https://github.com/CatalinSerafimescu/fixpp/actions/workflows/tier1.yml)
[![Tier 2 — Windows](https://github.com/CatalinSerafimescu/fixpp/actions/workflows/tier2.yml/badge.svg?branch=main)](https://github.com/CatalinSerafimescu/fixpp/actions/workflows/tier2.yml)
[![Tier 3 — Linux libc++](https://github.com/CatalinSerafimescu/fixpp/actions/workflows/tier3-libcxx.yml/badge.svg?branch=main)](https://github.com/CatalinSerafimescu/fixpp/actions/workflows/tier3-libcxx.yml)

[![Coverage](https://codecov.io/gh/CatalinSerafimescu/fixpp/branch/main/graph/badge.svg)](https://codecov.io/gh/CatalinSerafimescu/fixpp)
[![Docs](https://img.shields.io/badge/docs-fixpp-blue)](https://catalinserafimescu.github.io/fixpp/)
[![License: AGPL-3.0](https://img.shields.io/badge/license-AGPL--3.0-blue)](LICENSE)
[![Commercial license available](https://img.shields.io/badge/commercial-available-blue)](LICENSE-COMMERCIAL.md)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue)](https://en.cppreference.com/w/cpp/23)

A modern C++23 FIX protocol library. Zero-copy parsing, lock-free hot paths, ABI-agnostic C interface and Python bindings (SWIG); a gRPC+iceoryx2 service wrapper is planned after v1.0. Targets FIX 4.0 through 5.0SP2/FIXT.1.1 and FIX Latest, test-driven, under the per-PR coverage rule of constitution Article IX §1.

## Status

Early development, working toward v1.0. APIs are not stable yet, and breaking changes happen without notice.

The tables below summarise what is covered. The records behind them are authoritative:

- [**Feature catalogue**](spec/feature-catalogue.md) — every planned feature, with its status, PR and tests.
- [**Behaviours and limitations**](spec/behaviors-and-limitations.md) — what a user or operator needs to know, including every known limitation that is still open.
- [Changelog](CHANGELOG.md) and the [documentation site](https://catalinserafimescu.github.io/fixpp/).

Legend: ✅ done · ◐ partly done · ⏳ planned for v1.0 · ⏭ after v1.0

## FIX versions

| Version | Session layer | Runtime dictionary (XML) | Typed messages (generated) |
|---|---|---|---|
| FIX 4.0, 4.1 | ✅ | ✅ | — |
| FIX 4.2 | ✅ | ✅ | ✅ |
| FIX 4.3 | ✅ | ✅ | — |
| FIX 4.4 | ✅ | ✅ | ✅ |
| FIX 5.0, 5.0SP1 | ✅ over FIXT.1.1 | ✅ | — |
| FIX 5.0SP2 | ✅ over FIXT.1.1 | ✅ | ✅ |
| FIX Latest (Orchestra, EP303) | ✅ over FIXT.1.1, ApplVerID 10 | ✅ (native Orchestra reader) | ✅ |
| FIXT.1.1 | ✅ | ✅ | ✅ (session messages) |

"Typed messages" means builders, validators and typed read accessors generated from that version's dictionary (the catalogue names the few exceptions). A version without them is fully usable through the runtime dictionary and the field-level API.

## FIX specification coverage

| Area | Status | What it covers |
|---|---|---|
| Tag=value encoding | ✅ | Header and trailer, BodyLength and CheckSum, repeating and nested groups, Length+Data fields, every field data type, pipelined framing |
| Session layer | ◐ | Logon, Logout, Heartbeat, TestRequest, ResendRequest, SequenceReset (gap fill and reset), Reject; sequence numbers, PossDup/PossResend, recovery, reset policies, NextExpectedMsgSeqNum, MaxMessageSize, CompID and SendingTime checks. ⏳ Per-message ApplVerID, in-session ResetSeqNumFlag, session scheduling, third-party addressing, LFIXT modes |
| Application messages | ✅ | Every message in the catalogue: order management, market data, allocations and post-trade, account and collateral, registration and IOI, user and network management, and the FIX Latest additions |
| Data dictionaries | ◐ | The nine QuickFIX-format dictionaries, FIXT.1.1 and the FIX Latest Orchestra repository, loaded at runtime. ⏳ Custom dictionary extension (user-defined fields and messages) |
| Transport and FIXS | ◐ | TCP initiator and acceptor, reconnect with back-off, TLS 1.2/1.3 with mutual TLS, certificate and CA pinning, cipher enforcement. ⏳ PSK authentication |
| FIX session test cases | ◐ | Conformance tests for part of the official FIX session test suite. ⏳ The remaining test cases, including the outbound queue on reconnect |
| FIXP, SOFH, binary encodings (SBE, FAST, …) | ⏭ | Planned after v1.0 |

## Beyond the specification

| Feature | Status | Notes |
|---|---|---|
| Dynamic dictionary loading | ✅ | A QuickFIX-format XML loader and a native FIX Orchestra loader, for any supported version at runtime; dictionary-driven inbound validation (strict mode is opt-in) |
| Generated typed tier | ✅ | Builders, validators and typed read accessors generated from the dictionaries, installed with the library for use through `find_package(fixpp)` |
| Zero-copy parser and serializer | ✅ | Offset-table parsing into views over the receive buffer, with allocator-aware (`std::pmr`) arenas |
| Application callbacks | ✅ | QuickFIX-style `onCreate`/`onLogon`/`onLogout`, `fromAdmin`/`fromApp`, `toAdmin`/`toApp` |
| Message stores | ✅ | In-memory and file-based stores, with persistence across restarts |
| Configuration | ✅ | Native TOML configuration files for sessions and logging |
| C ABI | ✅ | Opaque handles, numeric error codes, sessions, message read and build (groups included), dictionary loading; versioned under a strict compatibility policy |
| Python bindings | ✅ | SWIG bindings over the C ABI; a pip-installable abi3 wheel for Linux x86_64 |
| Logging | ✅ | Zero-allocation asynchronous logger with pluggable sinks and a file sink |
| OpenTelemetry | ◐ | Tracer and meter provider wrappers, an OTLP log sink, session span helpers. ⏳ Engine-wide signals and the export pipeline |
| Interoperability | ✅ | Live conversation cells against QuickFIX C++ and QuickFIX/J |
| Platforms | ◐ | Linux (Clang, GCC, libc++) and Windows (MSVC). ⏳ macOS |
| Service wrapper (gRPC control plane, iceoryx2 data plane) | ⏭ | Planned after v1.0 |

## License

`fixpp` is dual-licensed:

- **Open source**: [GNU Affero General Public License v3.0](LICENSE) — you may use, modify, and distribute under AGPL-3.0 terms. Any network-facing service built on `fixpp` must release its source under a compatible license.
- **Commercial**: A separate commercial license is available for organizations that cannot comply with AGPL-3.0. See [LICENSE-COMMERCIAL.md](LICENSE-COMMERCIAL.md) for details.

## Contributing

Not open for external contributions yet. Check back after v1.0.
