> **Work in progress — sandbox project — NOT for production use.**

# fixpp

[![CI: Tier 1](https://github.com/CatalinSerafimescu/fixpp/actions/workflows/tier1.yml/badge.svg?branch=main)](https://github.com/CatalinSerafimescu/fixpp/actions/workflows/tier1.yml)
[![Coverage](https://codecov.io/gh/CatalinSerafimescu/fixpp/branch/main/graph/badge.svg)](https://codecov.io/gh/CatalinSerafimescu/fixpp)
[![License: AGPL-3.0](https://img.shields.io/badge/license-AGPL--3.0-blue)](LICENSE)
[![Commercial license available](https://img.shields.io/badge/commercial-available-blue)](LICENSE-COMMERCIAL.md)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue)](https://en.cppreference.com/w/cpp/23)

A modern C++23 FIX protocol library. Zero-copy parsing, lock-free hot paths, ABI-agnostic C interface, Python bindings (SWIG), and a gRPC+iceoryx2 service wrapper. Targets FIX 4.0 through 5.0SP2/FIXT.1.1 with ≥90% test coverage via TDD.

## Status

Early development. No public API yet. Breaking changes happen without notice.

## License

`fixpp` is dual-licensed:

- **Open source**: [GNU Affero General Public License v3.0](LICENSE) — you may use, modify, and distribute under AGPL-3.0 terms. Any network-facing service built on `fixpp` must release its source under a compatible license.
- **Commercial**: A separate commercial license is available for organizations that cannot comply with AGPL-3.0. See [LICENSE-COMMERCIAL.md](LICENSE-COMMERCIAL.md) for details.

## Contributing

Not open for external contributions yet. Check back after v1.0.
