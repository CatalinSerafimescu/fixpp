# fixpp Constitution

> **Status:** user-signed-off v0.1 (2026-05-10) — Phase 2 Gate A converged (Codex review + Claude Sonnet review + Codex adversarial pass, all 18 issues resolved); see `decisions/constitution.md`.
> **Authority:** This document is project-wide non-negotiables. Every `/specify`, `/plan`, ADR, and PR must satisfy it. Conflicts are resolved by amending the constitution first (Article XX) — never by silently violating an article.
> **Citation form:** other documents cite articles as `[const §Roman.arabic]` (e.g., `[const §VIII.3]`).

---

## Article I — Identity & Mission

1. **`fixpp` is a modern C++23 implementation of the FIX protocol.** Session layer + application layer for FIX 4.0 through 5.0SP2 + FIXT.1.1. v1.0 ships 100% of the official spec for those versions, with the following codegen-vs-runtime split:
   - **Codegen scope (per `[2c §1.3]`):** FIX 4.2, FIX 4.4, FIX 5.0 SP2, FIXT.1.1. Typed-message classes, `constexpr` field metadata, per-message validators, `dict::reify` runtime-dispatch all generated under per-version namespaces (`fixpp::v42`, `fixpp::v44`, `fixpp::v50sp2`, `fixpp::vt11`).
   - **Runtime-XML scope:** FIX 4.0, FIX 4.1, FIX 4.2, FIX 4.3, FIX 4.4, FIX 5.0, FIX 5.0 SP1, FIX 5.0 SP2, FIXT.1.1. `dict::XmlLoader` accepts QuickFIX-XML for any of these; runtime `Dictionary` works for field/required/group/length-pair lookups; users access fields through the runtime tag-keyed accessor.

   The runtime-XML-only versions (4.0 / 4.1 / 4.3 / 5.0 / 5.0 SP1) ship without a typed-message namespace in v1.0. Per-version codegen for those versions is deferred to post-v1.0 best-effort per Article XVIII §6.

   FIX Latest, FIXP, SBE, FAST, SOFH, JSON, GPB, and FIX MMT are post-1.0 milestones (Article XVIII).
2. **Primary distribution: in-process C++23 library** (static `.a`/`.lib` or shared `.so`/`.dll`). The C ABI is *adjacent* — it exists for non-C++ consumers, language bindings, and the out-of-process service mode, not as the primary surface.
3. **The Master Feature Catalogue** (`spec/feature-catalogue.md` in this repo) is the single coverage tracker. v1.0 cannot ship until every `OFFICIAL` row is `done` or explicitly `dropped` with user-signed rationale.
4. **No silent omissions.** Every normative section of every supported FIX spec must produce at least one catalogue row, traceable through `spec/coverage-index.md`.

---

## Article II — Language, Compilers, Platforms

1. **Language standard: C++23.** No fallback to earlier standards. Modules are not required (toolchain support is uneven); free use of concepts, coroutines, ranges, `std::expected`, `std::flat_map`, `std::pmr`, deducing `this`.
2. **Compiler matrix:**
   - **Clang** — primary development compiler, Linux. Sanitizers + fuzzing run here.
   - **GCC** — Linux CI sanity build only.
   - **MSVC** — Windows; `clang-cl` is **prohibited**.
3. **Platforms:**
   - **Linux is the primary development environment.** Day-to-day work, sanitizers, fuzzing, perf profiling, and e-book authoring all run on Linux.
   - **Windows** builds run **manual / on-demand / nightly via Tier 2 CI** (Article IX), not on every PR.
4. **No compiler-version pinning** for HALO (Heap Allocation eLision Optimization — the compiler eliding a coroutine's heap frame when its lifetime is bounded by the caller) and other optimizations (SYNTHESIS §3.2 Q6). The codebase tolerates HALO not firing on a given compiler revision; PMR fallback paths handle the gap.

---

## Article III — Build & Dependency Toolchain

1. **Build system: CMake ≥ 3.28 + Ninja.** No alternative generators in CI.
2. **Dependency manager: Conan.** Vcpkg is **prohibited**. Every external dep is declared in `conanfile.py` with a pinned version; transitive overrides go in profile files, not in code.
3. **Conan profiles** (under `conan/profiles/`):
   - `linux-clang-debug`, `linux-clang-release`
   - `linux-clang-asan`, `linux-clang-ubsan`, `linux-clang-tsan`
   - `linux-clang-coverage`
   - `linux-gcc-release`
   - `windows-msvc-debug`, `windows-msvc-release`
   - `windows-msvc-asan`
4. **CMake presets reference Conan profiles.** `tool_requires` pins `cmake` and `ninja` versions so the build is reproducible without relying on the host toolchain.
5. **`tools/` is build-only.** Code generators run during configure; no runtime dependency on tooling at user-link time.

---

## Article IV — Distribution Model

1. The C++ library is the primary public surface. It is consumed in-process by C++23 code via Conan.
2. The **C ABI** (`include/fix/c_api.h`) is the legal isolation boundary for AGPL/commercial dual licensing **and** the foundation for language bindings and the service wrapper. It is not the primary API.
3. **Python bindings** ship via SWIG over the C ABI, packaged as a CPython wheel. Linux x86_64 wheel is mandatory for v1.0; Windows wheel is best-effort via Tier 2.
4. **Service wrapper** (`service/`) is opt-in. gRPC is the control-plane transport; iceoryx2 SHM is the optional data-plane (Article XIV §3).
5. **v1.0 release artifacts are built but not published.** Conan packages and Python wheels are attached to GitHub releases; no upload to Conan Center or PyPI in v1. Publishing is gated on production-readiness and the README disclaimer being removed.

---

## Article V — License

1. **`fixpp` library:** AGPL-3.0 + commercial dual. The C ABI is the linkage isolation boundary for commercial users.
2. **E-book** (`book/` in the parent repo): CC-BY-SA 4.0.
3. **No LGPL dependencies.** Viral linkage is incompatible with the dual-license model.
4. **Vendored algorithm code** (e.g., `fixpp::sync::async_mutex` lifted from avast/asio-mutex BSL-1.0) carries upstream attribution at the file level; license compatibility is verified at vendoring time.
5. **Public repo from day one.** The README contains the disclaimer **"Work in progress — sandbox project — NOT for production use."** at the top of the file. The disclaimer must remain visible until publishing is unblocked (Article IV §5).

---

## Article VI — Spec Coverage Discipline (the 100% FIX Rule)

1. Every normative FIX spec section produces **at least one** `OFFICIAL` row in the catalogue (`feature-catalogue.md`).
2. Every OFFICIAL row's `Spec ref` uses canonical format `[DocAbbrev §X.Y.Z] Section title`. Vague refs (`§4`, "FIX spec") are a CI-linting failure.
3. Rows backed by design decisions instead of spec sections carry `[impl] description` or `[constitution] description` and are explicitly noted in `coverage-index.md` as design choices, not spec gaps.
4. **Bidirectional traceability:** `spec/coverage-index.md` maps every spec section → catalogue rows. Every new OFFICIAL row must have a coverage-index entry **before** it lands.
5. Every `/specify` artifact must include a **Normative References** section listing the exact `[DocAbbrev §X.Y.Z] Title` entries from the coverage index that inform the spec.
6. **No PR may close a `done` row without:** (a) a matching `/specify` artifact, (b) verifying tests, (c) Codex Gate B pass, (d) Gate A pass for non-trivial designs.

---

## Article VII — Testing Requirements

1. **Test framework: GoogleTest + GoogleMock** for C++ tests.
2. **Python tests: pytest** against the SWIG bindings.
3. **TDD is mandatory.** Every feature lands as red-green-refactor: failing test first, then implementation. Implementation without a preceding failing test is a Gate B blocker.
4. **No code without a test.** Untested code on `main` is a constitution violation; Codex Gate B prompts for it explicitly.
5. **Conformance corpus:** `tests/conformance/` holds the official FIX session-layer test cases (TC-001..TC-017, sourced from the **FIX Session Layer Test Cases** specification — `FIX-TC` in the coverage index) as executable scenarios. Every PR must pass them in CI.
6. **Interop:** v1.0 includes at least one interop test against an independent FIX implementation (QuickFIX) covering Logon → NewOrderSingle → ExecutionReport → Logout.
7. **Fuzzing (parser-touching modules):** libFuzzer corpus run ≥10 minutes on every PR; longer overnight runs on `main`. New parser-touching code without a fuzz harness is a Gate B blocker.

---

## Article VIII — Performance Budgets & Benchmarks

1. **Bench framework: Google Benchmark.** Every perf-sensitive module has a benchmark in `bench/`.
2. **Regression budget: ±5%** vs `bench/baselines/` per profile. Intentional perf changes update the baseline **in the same PR** with rationale in the PR body.
3. **No perf change merged without a benchmark in the same PR.**
4. **v1.0 perf targets:**
   - Parser: parity-or-better with `hffix` on identical hardware (parse/sec).
   - Session throughput: parity-or-better with QuickFIX on identical hardware (messages/sec, end-to-end).
   - Latency: end-to-end session round-trip p50 and p99 measured and reported in `bench/REPORT.md`; no specific number is constitutional, but regressions vs the v1.0 baseline are blockers.
5. **Allocator policy on the hot path:** zero `new`/`delete` between parse and `fromApp` callback. Arena/PMR is the default; deviations require justification in the relevant `/plan`.
6. **Codex adversarial perf review** (v1.0 release-candidate gate) hunts for benchmark hacks, compiler optimization that elides work, and unrealistic data shapes. Findings are blockers.

---

## Article IX — Coverage, Sanitizers, Static Analysis

1. **Coverage thresholds:**
   - **Per-PR:** ≥90% line, ≥80% branch on **touched modules**.
   - **Global** (once `wire/` and `session/` modules have shipped): ≥90% line.
   - Coverage is measured on Linux/Clang only (`llvm-cov` + `llvm-profdata`). Windows/MSVC builds (Tier 2) do not run a coverage step — coverage thresholds are platform-independent, and the only viable Windows tool (`OpenCppCoverage`, last release 2019) cannot reliably measure modern MSVC output.
2. **Sanitizers — Tier 1 (every PR, Linux/Clang):** ASan, UBSan, TSan must all run and pass.
3. **Sanitizers — Tier 2 (Windows/MSVC, manual/nightly):** ASan only. UBSan is not available under MSVC; equivalent UB coverage is provided by Linux/Clang Tier 1 (Article IX §2), since UBSan findings are language-level and platform-independent.
4. **Static analysis — Tier 1:**
   - `clang-tidy` clean against the project ruleset.
   - `clang-format` check.
   - `cppcheck` clean.
   - `include-what-you-use` clean.
5. **ABI check (from the first tagged C ABI release onward):** C ABI surface is dumped (`abidiff` Linux; structural diff Windows in CI) against the previous tagged ABI. Breaking changes are explicit `MAJOR` bumps; silent breaks are a release-blocker bug.
6. **Two-tier CI** (per `opus_plan.md` Quality Gate):
   - **Tier 1 — every PR (required to merge):** Linux/Clang Debug+Release, Linux/GCC Release sanity, sanitizers, coverage, perf, static analysis, fuzz (parser-touching modules), Python pytest, catalogue consistency check.
   - **Tier 2 — Windows + ABI:** manual / nightly / on-demand. Triggered by the `windows` PR label or nightly schedule.

---

## Article X — ABI Policy

1. **The C ABI in `include/fix/c_api.h` is a versioned contract.** Every change to it is reviewed against the contract; Codex Gate A is mandatory.
2. **No C++ symbol leakage** through the C ABI. CI verifies via `nm` (Linux) and `dumpbin` (Windows): the public C ABI surface contains only `extern "C"` symbols.
3. **Decimal at the C ABI boundary:** PoD `(int64 mantissa, int8 exponent)`. C++ users get full template flexibility via `decimal_traits<T>` (per SYNTHESIS §3.1 Q5); the C ABI picks one shape and freezes it.
4. **Error reporting at the C ABI:** `fixpp_error_t` is a bounded enum with reserved range and explicit forwards-compatibility rules (per SYNTHESIS §3.5 Q19). Out-of-range values are mapped to a documented "unknown error" code on read; unknown values from old consumers are tolerated by the engine. **Operational detail (per `[2i §4.4]` / `[2i §4.5]`):** the engine's translation layer downgrades to `FIXPP_ERR_UNKNOWN` (numeric 2) on the return path based on the consumer's published ABI minor version recorded at `fixpp_engine_create` time per `[2i §4.5]` version-binding protocol; a code introduced after the consumer's minor version is mapped to `FIXPP_ERR_UNKNOWN` before return. The FROM-consumer direction stays opaque pass-through — the engine does not actively reject unknown FROM-consumer values; `FIXPP_ERR_VERSION_MISMATCH` (numeric 5) is reserved for the explicit major-version-mismatch case at engine construction (per `[2i §4.5]`), not for unknown-value-tolerance. **Stability rule:** once a numeric value is published in a tagged C ABI release (`FIXPP_C_ABI_VERSION_MAJOR == 1`), it never changes meaning; new variants append at unused numeric slots within their domain block. Audit trail via `tools/abi_history/error_codes_v1.txt` (checked-in append-only file); CI verifies no re-definitions per the abidiff check `[const §IX.5]` and the occupancy gate `tools/check_capi_occupancy.sh`. Numeric-block layout per `[2i §4.3]`.
5. **Reentrancy contract** is documented per C ABI symbol (thread-safe / single-thread / requires-session-lock). No undocumented reentrancy.
6. **ABI-affecting features trigger all four mandatory controls (Appendix A):** `/clarify`, `/analyze`, Codex Gate A, user `/plan` sign-off.

---

## Article XI — Concurrency & Coroutines

1. **C++20/23 coroutines (`asio::awaitable<T>`) are the session/transport composition primitive.** Logon flow, recv, gap-fill, TLS handshake — all coroutines.
2. **Cancellation: ASIO native cancellation slots end-to-end.** No parallel `stop_token` abstraction. `fixpp_session_close()` from the C ABI signals the cancellation slot.
3. **Awaitable mutex required in coroutine context.** `fixpp::sync::async_mutex` (own implementation, BSL-1.0 algorithm attribution to avast/asio-mutex) is the only allowed mutex shape for coroutines. **Plain `std::mutex` is banned in any header that includes `asio::awaitable<...>`.** Enforced by clang-tidy custom check or grep gate.
4. **Application threading default: per-session strand.** Users who say nothing get callbacks serialised per session, never on the I/O thread. Custom executors are opt-in (per SYNTHESIS §3.2 Q6c).
5. **Hot-path lock policy: per-session policy with hard-coded callsite caps.** Default = mutex. Spin opt-in via session config. Store-write path always uses mutex regardless of policy (SYNTHESIS §3.2 Q8).
6. **Coroutine frame allocation: HALO-first.** PMR fallback per-awaiter where HALO doesn't fire. No global compiler-version pin (Article II §4).
7. **Threading/concurrency-affecting features trigger all four mandatory controls (Appendix A):** `/clarify`, `/analyze`, Codex Gate A, user `/plan` sign-off.

---

## Article XII — Security & TLS

1. **TLS implementation: OpenSSL on both Linux and Windows.** Schannel is **dropped** (locked decision 2026-05-06).
2. **Allowed TLS versions: 1.2 and 1.3 only.** TLS 1.0, TLS 1.1, all SSL versions are **prohibited** at compile time.
3. **Allowed cipher suites are an explicit compile-time allow-list. The engine refuses to load anything outside it (FIXS RC1 alignment).**
   - **TLS 1.3:** `TLS_AES_128_GCM_SHA256`, `TLS_AES_256_GCM_SHA384`, `TLS_CHACHA20_POLY1305_SHA256` (RFC 8446 §9.1 mandatory + recommended set).
   - **TLS 1.2:** ECDHE-(RSA\|ECDSA) with AES-128-GCM, AES-256-GCM, or ChaCha20-Poly1305; SHA-256 or SHA-384 PRF only.
   - **Key exchange groups:** X25519, secp256r1, secp384r1.
   - **Signature algorithms:** ECDSA (P-256, P-384), RSA-PSS (key size ≥ 2048 bits).

   Anything not on these four lists — including TLS 1.3 0-RTT data, static RSA key exchange, CBC-mode suites, SHA-1 signatures, and 1024-bit RSA — is rejected at compile time.
4. **Banned cryptography:** RC4, DES, 3DES, MD5, DH_anon, NULL ciphers, anonymous key exchange, export-grade ciphers. Enforced at compile time.
5. **`Session` construction requires an explicit `SecurityProfile` choice — there is no implicit default.** The profile selects the trust mode:
   - `mtls_ca` — mutual TLS with CA-chain trust on the peer cert. The recommended starting profile for v1.0 deployments.
   - `mtls_pinned` — mutual TLS with leaf-cert pinning (FIXS RC1 strict profile). Required for FIXS-conformant deployments.
   - `one_way_ca` — server-cert TLS only, CA trust; permitted for legacy interop where the counterparty does not present a client cert. Construction emits a compile-time `[[deprecated]]` diagnostic.

   Pinset rotation (multiple valid peer certs per counterparty, FIXS §5) is supported under both `mtls_pinned` and `mtls_ca`.
6. **Certificate pinset rotation API** is a v1.0 feature (multiple valid peer certs per counterparty, FIXS §5).
7. **`EncryptMethod(98)` ≠ 0 is rejected.** Application-layer encryption is deprecated since FIX 4.3; encryption lives at TLS only.
8. **Pluggable `cert_source` interface** with one default impl (file-based PEM/DER) in v1.0; HSM/TPM/cloud-KMS impls are user-side or future bundles (Article XIV).
9. **Security-affecting features trigger all four mandatory controls (Appendix A):** `/clarify`, `/analyze`, Codex Gate A, user `/plan` sign-off.

---

## Article XIII — Observability & Logging

1. **OpenTelemetry instrumentation from v1.0.** Traces, metrics, logs all OTLP-exportable. Prometheus + OTLP dual export is the v1.0 minimum.
2. **Async logging is mandatory.** Synchronous logging on the hot path is a banned pattern (Article XV). The in-process logger is zero-alloc producer, bounded MPSC queue, dedicated drain thread, deferred formatting. Telemetry and log queues are permitted to use `drop-oldest` under bounded-queue overflow; this exception is scoped strictly to non-business signals (logs, metrics, traces) and never applies to FIX application or session messages (Article XV §15).
3. **OTel `trace_id` / `span_id` in every log record.** Each `Session` carries a `trace_context` field; logging on the session strand reads it directly. Code paths outside session scope (e.g., listener accept, control-plane handlers) use the `co_await fixpp::current_trace_context` awaitable backed by a strand-stored context (see `architecture.md`). `thread_local` propagation of trace context is **prohibited** — coroutines may resume on a different thread than they suspended on, and a `thread_local` write made before suspension is not guaranteed visible after resume. Correlation must work at the observability backend without manual stitching.
4. **Same sink interface backs OTel log export and file/stderr sinks.** No double-write paths.
5. **Bench spike mandatory** for the in-house logger vs `quill` before locking the implementation choice.

---

## Article XIV — Pluggable Interfaces

1. **The following are pluggable, each with one default impl in v1.0:**
   - **Transport** (default: ASIO TCP/TLS over OpenSSL).
   - **Control plane** (default: gRPC over Unix socket / named pipe).
   - **Cert source** (default: file-based PEM/DER).
   - **Logger sinks** (default: in-process async logger + OTLP exporter).
   - **MessageStore** (default: in-memory; file-based impl also v1.0).
2. **Interface surfaces are small.** Each pluggable interface defines **≤5 pure-virtual methods**. Bigger surfaces are permitted only with an explicit design-doc justification (one paragraph naming the necessary methods and why each is irreducible). The justification is reviewed at Gate A.
3. **Data-plane SHM via iceoryx2** is opt-in for sidecar mode. The control plane (gRPC) works without it.
4. **Plugin discovery is compile-time only in v1.0.** Dynamic plugin loading (`dlopen`) is post-1.0.

---

## Article XV — Banned Patterns

The following patterns are **prohibited** in `fixpp` source code. Each is rooted in a real failure mode observed in surveyed implementations.

1. **Heap-allocate per message or per field on the hot path.** Use zero-copy views; arena/PMR for the rare materialise cases.
2. **Thread-per-session blocking I/O.** Use ASIO async I/O; multiplex N sessions onto M executor threads.
3. **Coarse global session lock.** Per-session state; lock-free queue between I/O and app thread.
4. **Synchronous disk I/O on every send** (e.g., QuickFIX `FileStore` flush per write). Async journal with background flush; sync-on-failover is opt-in.
5. **Synchronous logging on the hot path.** Async logger only (Article XIII §2).
6. **Runtime-only field validation.** Constexpr field metadata + typed accessors generated from the dictionary; misuse fails to compile, not at runtime.
7. **Forward-only field iteration with linear find** as the only access mode. Offset table is mandatory for typed/random-access path.
8. **`std::multimap` (or any other cache-hostile map) for field storage.** Vector + offset table; SBO for the common case.
9. **`std::mutex` in coroutine context.** Use `fixpp::sync::async_mutex` (Article XI §3).
10. **Application-layer encryption** (`EncryptMethod(98)` ≠ 0). TLS only (Article XII §7).
11. **TLS 1.0 / 1.1 / SSL / RC4 / DES / MD5 / DH_anon / anonymous KX / NULL ciphers / export-grade ciphers.** Compile-time allow-list refusal (Article XII §4).
12. **LGPL dependencies.** Viral linkage is incompatible with dual licensing.
13. **Eager codegen with no runtime dictionary path.** Hybrid mandated: codegen for standard fields (D-008), runtime XML loader for custom (D-007 + D-009).
14. **FAST / SBE / FIXP / SOFH shoehorned into v1.0.** Roadmap-locked to post-1.0 (Article XVIII).
15. **Application-layer message drops on slow consumer.** Backpressure-aware dispatch with two configurable modes: `block` (push back to the producer) or `disconnect-and-recover` (terminate the session and rely on FIX `ResendRequest` semantics on reconnect). `drop-oldest` is **never** permitted on the application or session message path — silent loss desynchronises the sequence-number contract. Telemetry and log queues may use `drop-oldest` under the rules in Article XIII §2.
16. **Custom XML config format** incompatible with the QuickFIX `[DEFAULT]` / `[SESSION]` CFG format. We accept QuickFIX CFG verbatim; TOML is also accepted; new formats require justification.
17. **Vendored OSS based on `master`/`main` when a release tag is older.** Read from the last release tag unless `master` represents a justified upstream improvement (SYNTHESIS §2.1).
18. **Research / decision content** (`research/`, `decisions/`, `book/`) committed into the `fixpp` repo. The `.github/workflows/no-research.yml` guard rejects it.

Each entry is a CI-enforced rule wherever feasible (Article IX §4 covers static analysis; Article XV.18 has its own guard workflow).

---

## Article XVI — Spec Kit Workflow Rules

1. **Full Spec Kit command set, each invoked in a clean context:** `/constitution`, `/specify`, `/clarify`, `/plan`, `/tasks`, `/analyze`, `/checklist`, `/taskstoissues`, `/implement`, `/simplify`.
2. **Clean-context rule:** every Spec Kit command runs as a fresh subagent invocation that loads only the artifacts it needs. No cross-phase context bleeding.
3. **`/clarify` is MANDATORY before `/plan` for any feature that touches:** ABI, threading, error semantics, wire format, codegen, session FSM, or security. (Same trigger set as Codex Gate A — Article XVII.)
4. **`/analyze` is MANDATORY** for the same trigger set as `/clarify`. Drift between constitution ↔ spec ↔ plan ↔ tasks is caught here, before `/implement`.
5. **`/checklist` output is part of CI evidence.** Checklists tied to NFRs and acceptance criteria become the e-book's "how to verify" appendix.
6. **`/implement` is one task at a time, TDD red-green-refactor.** Sonnet executes; Opus reviews increments.
7. **`/simplify` runs on the implementation diff before PR open.** Code-reuse, quality, efficiency findings fixed before review.
8. **Stuck loop:** three failed `/implement` invocations on the same red test (each invocation is a fresh-context attempt at one TDD cycle, per §1 and §6) → escalate to Codex as fallback implementer; if still stuck, `AskUserQuestion`. Codex's PR review for that task must come from a **fresh** Codex session, not the one that wrote the code (independence between author and reviewer is non-negotiable).

---

## Article XVII — Codex Review Gates

1. **Gate A — Design review (pre-implementation, non-trivial designs).** Triggers (any one):
   - Touches the public C++ API or C ABI.
   - Touches concurrency / threading / cancellation / executor model.
   - Touches the wire format, parser, or codegen layout.
   - Touches the session FSM, recovery, or message store contract.
   - Touches the security surface (TLS, cert handling, PSK).
   - Any new design document under `.specify/` (`architecture.md` and sibling design docs) — qualifies by default.

   Trivial features (rename a private helper, add a P2 boilerplate row over an existing module) skip Gate A. **When in doubt, run it.** Blockers from Gate A must be resolved or explicitly waived with rationale before `/tasks` runs.

2. **Gate B — PR review (post-implementation, every PR before merge).** Mandatory regardless of feature size. High-severity findings resolved or waived with rationale in the PR description before merge.

3. **Independence rule.** Codex's review is independent of the implementer. When Codex implements (escalation), Codex's PR review for that PR comes from a separate Codex session.

4. **User invokes Codex.** Neither Sonnet nor Opus auto-invokes Codex; the gates are user-driven (`codex:codex-rescue` agent or local Codex CLI). The PR description links to the Gate A outcome and the Gate B outcome.

5. **Findings triage:** Opus triages; Sonnet fixes accepted items; user signs off feature completion at `/specify` boundaries and at module close.

6. **CI enforcement.** The `.github/workflows/gate-a.yml` workflow inspects every PR's changed-file set against the Appendix A trigger paths (path globs are owned by the workflow itself, not the constitution). If any trigger path is touched, the workflow blocks merge unless the PR carries either a `gate-a-done` label (Codex Gate A passed) or a `gate-a-waived` label with mandatory rationale in the PR body. Trivial diffs auto-waive: comment-only edits, doc fixes, single-line whitespace, dependency-pin bumps without code changes.

7. **Local pre-PR build gate (mandatory, all PRs).** Before opening any PR, the contributor MUST run a local Conan install + CMake configure + build + ctest cycle on at least the `linux-clang-debug` preset, and `pytest bindings/python/tests/` if the change touches `bindings/python/`. The PR description must include a one-line confirmation (`local build: green on linux-clang-debug @ <git-sha>`). PRs without that line, or with a known-red local build, are rejected at review.
   - **Resource gate:** local builds are resource-heavy (Conan fetches + full compile + sanitizer rebuilds). When an AI agent needs to run the local build, it MUST surface an `AskUserQuestion` first; the user approves the build before it runs. The agent never auto-runs `conan install` / `cmake --build` without explicit approval.
   - **All dev work happens locally.** Contributors do not push speculative commits to remote branches "to see what CI says" as a substitute for local testing. CI is verification of green local work, not a remote test runner.
   - **Local toolchain target: Clang 22** (matches the user's local install and the Conan profile pin per Article II §2 / Article III §3). CI provisions Clang 22 via `apt.llvm.org` so local==CI.

---

## Article XVIII — Roadmap Discipline

1. **v1.0 scope is locked:** FIX 4.0–5.0SP2 + FIXT.1.1 session + application layers, FIXS over TLS, the C ABI, Python bindings, the gRPC service wrapper, iceoryx2 SHM data plane (opt-in).
2. **Post-1.0 roadmap (locked):**
   - **v1.1 — SOFH** (Simple Open Framing Header).
   - **v1.2 — FIX Latest application messages** (new MsgTypes A-035..A-065 + EP-level field additions to existing messages, per coverage-index Post-1.0 Gap Registry).
   - **v1.3 — SBE** (Simple Binary Encoding).
   - **v1.4 — FIXP** (FIX Performance Session Layer).
   - **v1.5 — FAST** (FIX Adapted for STreaming).
   - **v1.6 — JSON** (FIX JSON encoding).
   - **v1.7 — GPB** (Google Protocol Buffers FIX encoding).
   - **v1.8 — FIX MMT** (Market Model Typology).
3. **Permanently dropped:** FIXML (XML representation; superseded), FIXatdl (UI/display spec, not a wire protocol).
4. **Roadmap changes are constitution amendments.** Re-ordering, additions, removals all require Article XX.
5. **No early shipping** of post-1.0 protocols into v1.0 to "get them done." Each shipping target is its own Spec Kit cycle, gated by the same Tier 1 quality bar.
6. **Post-v1.0 codegen for runtime-XML-only versions.** FIX 4.0, FIX 4.1, FIX 4.3, FIX 5.0, and FIX 5.0 SP1 ship in v1.0 with runtime-XML support only (no per-version codegen namespace). Per-version codegen for these versions is post-v1.0 best-effort, prioritised at the discretion of the maintainer team based on observed downstream demand. The recommended priority order is: FIX 4.3 first (most-used legacy version in the post-v1 backlog), 5.0 SP1 second, 5.0 third, 4.0 / 4.1 last (vanishingly few production deployments). Each version's codegen is its own minor-version Spec Kit cycle, gated by the same Tier 1 quality bar.
7. **Application-message codegen scope for v1.0.** Application-message rows A-014..A-034 are codegen-deferred to v1.x for the four codegen versions. v1.0's typed-message scope under `fixpp::v42`, `fixpp::v44`, `fixpp::v50sp2` is A-001..A-013 plus the M-/P-/C-/R-/N- families per the catalogue. Runtime-XML access to A-014..A-034 via `view.get(uint16_t tag)` ships in v1.0 across all 9 supported FIX versions; typed accessors for those messages land in v1.x. The deferred set comprises (per `feature-catalogue.md` lines 291–311) BusinessMessageReject (A-014, 35=j), DontKnowTrade (A-015, 35=Q), the ListCancel/Execute/Status family (A-019), the SecurityList family (A-025, 35=v/w/x/y), XMLnonFIX (A-034, 35=n), and similar additional order-management variants; A-024 stays dropped as a duplicate per `[SYN §4.4]`.

---

## Article XIX — Documentation

1. **Library docs:** Doxygen → mdBook bridge under `docs/`. Hand-written getting-started, session cookbook, custom dictionary how-to, C ABI usage, Python tutorial, perf tuning, troubleshooting.
2. **E-book:** mdBook + mdbook-pdf under `book/` in the parent repo. CC-BY-SA 4.0 (Article V §2).
3. **Every code example in either artifact is a runnable file in `examples/` exercised by CI.** Stale examples are a documentation bug.
4. **Cross-link to source** at pinned tags, not at `main`, so references survive history rewrites.
5. **Pages tied to public API surfaces** must be regenerated when the surface changes. Doxygen output drift is a Gate B finding.

---

## Article XX — Amendments

1. **The constitution is amendable but not silently violatable.** Any conflict between this document and a feature spec must be resolved by amending the article first (with rationale committed in the same PR), then proceeding.
2. **Amendment process:**
   - Open a PR titled `Constitution: amend §<article>.<number> — <summary>`.
   - PR description states the change, the rationale, and which catalogue rows / specs are affected.
   - Codex Gate A review on every amendment.
   - User signs off.
3. **`_log.md` records every locked decision** with date and source (which article amendment, which `/specify`, which architecture revision).
4. **Backwards-incompatible amendments** (banned-pattern additions, perf-budget tightening) require a v-major bump and an entry in `CHANGELOG.md`.
5. **Cross-cutting hand-off rules** (e.g., "Sonnet asks before every `git push`", "sudo confirmation required") are amendments to this constitution; they live here, not in ad-hoc memory or `CLAUDE.md` carve-outs. *No such rules defined as of v0.1; future rules of this shape are added below as numbered sub-clauses (5.a, 5.b, …) under this article via the standard amendment process (§2).*

---

## Appendix A — Mandatory triggers reference

**This appendix is the canonical mandatory-trigger reference.** Article-level trigger clauses must match it; conflicts are resolved in favour of this table (Article XX). The following features trigger **all four** mandatory controls: `/clarify`, `/analyze`, Codex Gate A, user `/plan` sign-off.

| Trigger | Examples |
|---|---|
| ABI surface change | new C ABI symbol, signature change, error-code addition |
| Threading / concurrency | new awaitable, new strand discipline, lock policy change |
| Error semantics | new `fixpp_error_t` value, exception-vs-`expected` change |
| Wire format / parser | offset-table semantics, framing rules, validator changes |
| Codegen layout | dictionary loader, multi-version coexistence |
| Session FSM | state additions, recovery semantics, gap-fill rules |
| Security | TLS config, cipher allow-list, cert-source plug-in |

Trivial features (P2 boilerplate over an existing P0 module, doc-only) skip all four. **When in doubt, run them.**

---

## Appendix B — Cross-references

- **`opus_plan.md`** (parent repo) — phase plan, owns the Codex gate workflow descriptions, owns the Quality Gate Tier 1/Tier 2 split.
- **`SYNTHESIS.md`** (parent repo) — Phase 1.5 output; the decisions encoded here are sourced from §1, §2, §3, §5.
- **`spec/coverage-index.md`** (this repo) — bidirectional spec ↔ catalogue traceability index. Article VI §4 binds `/specify` to it.
- **`spec/feature-catalogue.md`** (this repo) — the 100% FIX tracker. Article VI §1 binds it to the spec.
- **`architecture.md`** (this directory, drafted next) — module layering, public namespaces, design patterns. Implements the rules; this constitution sets them.
