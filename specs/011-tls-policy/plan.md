# Implementation Plan — 011-tls-policy

**Branch**: `011-tls-policy` | **Date**: 2026-05-23 | **Spec**: [spec.md](spec.md)

**Design anchors**: this slice is **bound by `.specify/2g-tls.md` v0.4** (Gate A round 3 converged via Phase A post-cap pass, 2026-05-09). The design doc is the load-bearing upstream — every design decision in this plan resolves to a 2g §-anchor, not to fresh derivation. Sibling upstreams `architecture.md` v0.2 (§4.6 tls/ surface, §5.6 mid-session-mutable carve-out, §6 plugin pattern), `constitution.md` v0.2 (Article XII Security & TLS — the binding security spine; Article XIV pluggable interfaces; Article XV banned patterns), and the signed-off `.specify/2a-decimal.md` v0.3 (`trap_throw`), `.specify/2b-wire.md` v0.2 (`[[clang::lifetimebound]]` precedent), `.specify/2c-codegen.md` v1.3 (`owning_message_t<>` PMR string precedent), `.specify/2d-threading.md` v0.4 (`EngineConfig` / `SessionConfig` / `session_executor` / `cancellable_dispatch` / TLS strand-safety / effective-clock single rule), `.specify/2e-msgstore.md` v0.4 (writer-mutex precedent), `.specify/2f-async-mutex.md` v1.5 (cancellation-group precedent). On conflict with 2g-tls.md v0.4 the design doc wins; a divergence is a plan defect.

**Pipeline state**: `/speckit-specify` → `/speckit-clarify` (5 Qs resolved 2026-05-23) → **`/speckit-plan` (this doc)** → next per `[const §XVII.1]` / `[pipeline.md step 4]` is **Phase-4 Gate A** (NOT the design-doc Phase-2 Gate A — that converged round 3 on 2g-tls.md v0.4 already). Then `/speckit-tasks` → `/speckit-analyze` → `/speckit-checklist` → `/speckit-checklist-audit` → `/speckit-implement` → `/simplify` → `/speckit-verify` → Gate B.

## Summary

Ship the **TLS-policy core** for fixpp v1.0 — the public C++ surface that 2h-transport will consume to build the OpenSSL `SSL_CTX` and wire the `SSL_VERIFY_PEER` callback, and that the session-FSM Phase-4 feature will consume to perform the T-041 CompID-to-TLS-identity binding. This slice locks the **interface contract**, not the wire layer; FR-026/FR-027 enumerate the negative-ownership boundary explicitly (2h owns `SSL_CTX` + handshake + `SSL_VERIFY_PEER`; 2i owns the C ABI; 2j owns control-plane reload; 2k owns OTel record format; session Phase-4 owns CompID binding).

Five public types land in `include/fixpp/tls/`:

1. **`fixpp::tls::cert_source`** (abstract, 2 pure-virtuals: `load_credentials` awaitable + `load_trust_anchors`) — the pluggable credential-source interface; ships under the `[const §XIV.2]` ≤5 cap with a wide margin so HSM/KMS/vault/in-memory user-side impls remain implementable as a single class.
2. **`fixpp::tls::file_cert_source`** — the v1.0 reference impl, PEM/DER over a `cert_source::Config`, optional password callback for encrypted PEM, factory `make_file_cert_source(Config, std::pmr::memory_resource*) -> expected_t<std::shared_ptr<cert_source>>`.
3. **`fixpp::tls::Pinset`** — the FIXS-RC1 §5 add-then-remove rotation container, keyed on **SHA-256-of-leaf-DER fingerprints** (32 bytes; `/clarify` Q4), lock-free reader path on the handshake hot path (zero alloc per FR-007), `shared_mutex` writer path on add/remove (rare ops). `pin_view` returned by `find` carries a `shared_ptr<const pin_snapshot>` so `pin_view` outlives any concurrent `remove`. **Per-counterparty granularity recommended** (`/clarify` Q5) — one Pinset shared via `shared_ptr` across all SessionConfigs targeting the same counterparty.
4. **`fixpp::tls::CipherPolicy`** — compile-time allow-list per `[const §XII.3]` / `[const §XII.4]`; banned tokens (RC4/DES/3DES/MD5/CBC/SHA-1/1024-bit-RSA/0-RTT/TLS≤1.1/SSL) refused via `static_assert`; runtime-string predicate `is_allowed(string_view) constexpr noexcept` for the 2i C-ABI bridge.
5. **`fixpp::tls::SecurityProfile`** — enum (`mtls_ca` = 1, `mtls_pinned` = 2, `one_way_ca` `[[deprecated]]`) plus the `SslCtxConfig` value-type the 2h adapter consumes. The mapping table **MUST encode TLS 1.3 preferred + TLS 1.2 fallback** (`/clarify` Q1: `SSL_CTX_set_min_proto_version = TLS1_2_VERSION`, `set_max = TLS1_3_VERSION`). `make_ssl_ctx_config(SecurityProfile, …, Pinset*, …) -> expected_t<SslCtxConfig>` is the entry point; it refuses with `tls_pin_empty_at_open` when `mtls_pinned` is paired with a non-null empty pinset (`/clarify` Q2 — new error variant).

Plus two value types and the error envelope:

6. **`fixpp::tls::Certificate`** + **`fixpp::tls::peer_identity`** — parsed-peer-cert views; `[[clang::lifetimebound]]` at the abstract-base declaration site per `[arch §5.5]` and the `[2b §6.4]` precedent. `peer_identity` is what the session-FSM Phase-4 module consumes for T-041.
7. **`error::tls_*`** variant family — 13 variants minimum (FR-025), `verify_peer` short-circuits on the **first violation hit in a documented evaluation order** (`/clarify` Q3 → FR-020a: per-cert DER size → RSA bounds → ECDSA curve → chain depth → SAN cardinality → X.509 version → expiration vs effective_clock → `CipherPolicy::is_allowed`).

**Catalogue rows owned**: T-006 (TLS 1.2), T-007 (TLS 1.3), T-008 (CipherPolicy / SecurityProfile bound to the OpenSSL adapter shape), T-011 (Pinset rotation), T-013 (cert_source plugin). **Cross-cuts owned with 2h-transport**: T-039 (validation predicate; OpenSSL handshake hook lives in 2h), T-040 (cert_source consumes operator-distributed secrets). **Cross-cut into session/ Phase-4**: T-041 (peer_identity value supplied here; CompID binding logic in session feature).

**Branch base**: `011-tls-policy` rooted on post-PR-#83-merge `main` of the library submodule (`25cf09d`). No carry-overs from 005 / 009 / 010 work — the session-layer slice is fully merged and this slice opens a **new module** under `include/fixpp/tls/` (no prior code; the `tls/` directory is being established by this feature). At Gate B convergence the slice merges to `main` and bumps the parent-repo submodule pointer per the standard pattern (see post-010 `parent 1e6dc17` + close-out commit).

**Gate A required**: yes. This feature opens a new module surface (`include/fixpp/tls/`) and a new public type cluster — `[const §XII.9]` demands all four mandatory controls (`/clarify`, `/analyze`, Codex Gate A, user `/plan` sign-off) for security-affecting features. **Not Gate-A-inherited from anywhere** — 2g-tls.md is a Phase-2 design-doc Gate A (already converged), but the Phase-4 feature Gate A is independent and reviews this `specs/011-tls-policy/` bundle. See `[const §XVII.1]`.

## Technical Context

**Language/Version:** C++23 (`[const §II.1]`). Coroutines (`asio::awaitable<T>`), `std::expected` (via `core::expected_t`), `std::pmr`, `std::span`, `std::variant`, `std::array`, `std::shared_mutex`, `std::atomic<std::shared_ptr<T>>` where available (with the `[2f §6.5]` fallback split per platform), `[[clang::lifetimebound]]`, `[[nodiscard]]`, `[[deprecated]]`, `static_assert`, deducing `this`.

**Primary Dependencies:** **One new Conan row to vet** — **OpenSSL 3.x** (`OPENSSL_FOUND` already exercised by build-system probes for the 2h-transport future feature, but this slice is the first to materially link). Reuses everything 005 / 006 / 007 / 008 already depend on: `fixpp::core` (`expected_t`, `error`, `trap_throw`, `Clock` / `mock_clock`, `cancellable_dispatch`, `session_executor`), `fixpp::sync::async_mutex` (referenced but NOT consumed — Pinset uses `shared_mutex`, not async_mutex, because Pinset rotation is fully synchronous; `[2g §6.5]` is normative here). GoogleTest 1.17.0 + Google Benchmark 1.9.5 pinned. No new GoogleMock surface introduced.

**Storage:** N/A. `file_cert_source` reads from the filesystem at cold-path load time only; `cert_source` itself is filesystem-agnostic (the file-path is one impl shape; HSM/KMS/vault are others). No state persisted by this feature.

**Testing:** GoogleTest + GoogleMock (C++), TDD red-green-refactor (`[const §VII.1]` / `[const §VII.3]`). Deterministic time via `fixpp::core::mock_clock` (cert-expiration tests). PEM/DER test fixtures generated under `tests/tls/fixtures/` (CA + leaf chains; rotated-pair set; encrypted-PEM with known passphrase; X.509 v1 negative-fixture; SAN-over-cap negative-fixture; DER-over-cap negative-fixture; RSA-1024 negative-fixture; RSA-16384 negative-fixture). Sanitizer matrix per `[const §IX.2]`: ASan + UBSan + **TSan** + GCC release sanity — TSan is critical because Pinset's reader/writer separation is the central concurrency seam this feature ships.

**Target Platform:** Same Tier-1 matrix as 005–010: Linux Clang Debug + Release + ASan + UBSan + TSan + Coverage; GCC Release sanity. Windows Tier 2 manual/nightly (no coverage step — `[const §IX.1]`). No C-ABI surface added by 011 — `[const §IX.5]` abidiff N/A (2i is where the C ABI surfaces; this feature publishes the **C++ source-of-truth** and the per-doc-prefix `FIXPP_ERR_TLS_*` coalescing-group rule).

**Project Type:** C++23 library, **NEW module** `tls/` — first feature to ship `include/fixpp/tls/*.hpp` + `src/tls/*.cpp` + `tests/tls/test_*.cpp` + `bench/tls/bench_*.cpp`. Module layer per `tools/check_layers.py`: `tls/` sits at the same architectural tier as `session/` (both consume `core/`; `session/` will consume `tls/`'s `peer_identity` at the T-041 binding boundary, post-this-feature). The check-layers config MUST be updated to register the new module BEFORE `/speckit-implement` per `[[feedback_gate_b_check_layers_post_fixer]]`.

**Performance Goals:**

- **Hot-path lookup (`Pinset::find` / `Pinset::snapshot_acquire`):** Platform-conditional ceiling per `[2g §6.3]`:
  - libstdc++ ≥ 16 / libc++ on x86_64 with `cmpxchg16b` (lock-free `atomic<shared_ptr>`): **≤ 30 ns p99** snapshot_acquire, **≤ 130 ns p99** full find (snapshot + linear scan 16).
  - libstdc++ ≤ 15 (mutex-fallback `atomic<shared_ptr>`): **≤ 100 ns p99** snapshot_acquire, **≤ 200 ns p99** full find.
  CI fails on > 5% regression vs the applicable ceiling per `[const §VIII.2]`. Bench records the platform tuple (`compiler / stdlib / stdlib-version / cpu-supports-cmpxchg16b`) and selects the matching ceiling at runtime.
- **Cold-path load (`file_cert_source::load_credentials`):** ≤ 50 ms soft per call (file I/O + parse + chain build). No `[const §VIII.5]` applicability — cold path.
- **Cold-path validate (`verify_peer`):** ≤ 1 ms p99 for an in-envelope chain (depth ≤ 4, RSA-2048 or ECDSA-P-256, ≤ 8 SAN entries). DoS-reject paths (RSA-16384 / DER-too-large / SAN-too-many) MUST short-circuit ≤ 50 µs p99 — the bound is the whole point of the cap.
- **Cipher allow-list compile-time refusal:** `static_assert` cost is build-time only; no runtime cost.

**Constraints:**

- **Zero global `new`/`delete` on the handshake-read hot path** (`Pinset::find` / `Pinset::snapshot_acquire`) per `[const §VIII.5]` / `[const §XV.1]`. Verified via `tests/perf/test_tls_alloc_guard.cpp` + mallocnesia LD_PRELOAD dual gate (lessons from `[[feedback_tracking_pmr_resource_false_pass]]` — counting_resource alone is not sufficient; mallocnesia per `[[reference_mallocnesia_path]]` MUST be wired).
- **Cold-path PMR allocation** permitted for `file_cert_source::load_credentials` (file I/O + parse + chain build) and for `peer_identity` SAN-list owning vectors. PMR throws routed through `[2a §4.2]` `trap_throw` and surface as `error::tls_load_failed`. No exception escapes the public surface during the session-handling window (`[arch §5.3]`).
- **`make_file_cert_source` construction-time throws** permitted per `[arch §5.3]` carve-out (engine bootstrap before any session is open); the factory wraps that boundary in `expected_t<...>` so non-construction-time callers (2i C ABI, future hot-reload) never see a thrown exception.
- **ASIO native cancellation slots end-to-end** on `cert_source::load_credentials` per `[const §XI.2]` / `[SYN §3.2 Q6a]`; cancellation completes with `expected_t::unexpected{tls_load_cancelled}` (FR-021). Recipe lifted verbatim from `[2d §6.5]` `cancellable_dispatch`.
- **`Pinset` mutex choice:** `std::shared_mutex` (NOT `std::mutex`, NOT `fixpp::sync::async_mutex`). Rationale per `[2g §6.5]`: rotation is fully synchronous (called from operator's control-plane handler, no `co_await` inside the add/remove API); reader path is lock-free via the published `atomic<shared_ptr<const pin_snapshot>>`. `std::mutex` is banned in coroutine context per `[const §XV.9]` — but this is NOT coroutine context for the writer (the writer runs on the operator's thread, not on a coroutine). `async_mutex` is wrong: the writer needs synchronous serialization, not awaitable suspension.
- **Per-session strand serialisation** (`[const §XI.4]`) for the coroutine that calls `cert_source::load_credentials`; the cancellation slot is read inside the coroutine before any work dispatch.
- **No C-ABI surface emitted** by 011 (`[const §X.2]` nm check inherited). 2i bridges; 011 publishes the C++ source-of-truth + per-doc-prefix `FIXPP_ERR_TLS_*` coalescing-group naming rule.
- **Pluggable interface cap**: `cert_source` ships **2 pure-virtual methods** — well under `[const §XIV.2]`'s ≤ 5. Design-doc justification is not needed (only required for > 5).
- **No `dlopen` plugin loading** per `[const §XIV.4]`. Cert-source impls are compile-time linked (the file default; user-side HSM/KMS impls compiled into the user binary linking against fixpp).

**Scale/Scope:** Estimated edit footprint (new code, no pre-existing `tls/` directory):

- **Headers**: 7 new public headers under `include/fixpp/tls/` — `cert_source.hpp`, `pinset.hpp`, `cipher_policy.hpp`, `security_profile.hpp`, `certificate.hpp`, `peer_identity.hpp`, `tls_errors.hpp` (the last folds the `error::tls_*` enum variants into `include/fixpp/core/error.hpp` — single point of truth — and `tls_errors.hpp` re-exports the per-doc-prefix grouping under `fixpp::tls::errors` for ergonomic at-site use). Public surface: ~700 lines total across the 7 headers + ~200 lines of `[[clang::lifetimebound]]` / `[[nodiscard]]` / `static_assert` annotations.
- **`include/fixpp/core/error.hpp`** appended with **13 new `error::tls_*` enum variants** at the next free slots (slot allocation must respect prior 005/008/009/010 pinning per `[[project_2e_design_doc_only_seqnum_handoff]]` — slots already locked at 49/51/52/54/55/57/58/60/63/77; 011 takes the next free contiguous range, NOT renumber).
- **Implementation**: ~5 new `.cpp` files under `src/tls/` — `file_cert_source.cpp`, `pinset.cpp`, `security_profile.cpp` (the `make_ssl_ctx_config` factory), `certificate.cpp` (DER parse), `verify_peer.cpp` (the validation predicate). Total ~1.2k–1.5k lines. The two factories (`make_file_cert_source` + `make_ssl_ctx_config`) live in dedicated `.cpp` files so 2h-transport links against the symbols cleanly.
- **Tests**: ~22 test binaries under `tests/tls/` covering 5 user-story scenarios × ~4 acceptance scenarios + 14 edge cases + perf alloc-guard + ASIO cancellation seam + Pinset add-then-remove deterministic test. PEM/DER fixture authoring is a sub-task (one-time, manual + `openssl` CLI; checked in as binary fixture).
- **Bench**: 2 new bench binaries — `bench_pinset_snapshot_acquire.cpp` (the platform-conditional latency-ceiling enforcement) + `bench_verify_peer_in_envelope.cpp` (the ≤ 1 ms p99 in-envelope handshake-validation budget).
- **Documentation**: `docs/src/tls-quickstart.md` + the `quickstart.md` companion in this spec dir (Phase 1 output). `feature-catalogue.md` + `coverage-index.md` updates per `[[feedback_feature_completeness_gate]]`.
- **`tools/check_layers.py`** config updated to register the new `tls/` module before /speckit-implement.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-checked post-Phase 1.*

| Article | Clause | This feature | Status |
|---|---|---|---|
| II.1 | C++23 | C++23 throughout | ✓ |
| VI | Spec Coverage Discipline | Catalogue rows T-006/007/008/011/013 owned; cross-cuts T-039/040/041 named | ✓ |
| VII.1 / VII.3 | TDD red-green-refactor | Tests authored first per phase per `[[feedback_subagent_phase_verification_two_traps.md]]` | ✓ (procedural; enforced at /tasks + /implement) |
| VIII.5 | Zero `new`/`delete` on hot path | Pinset hot-path lock-free; verified via `[[reference_mallocnesia_path]]` + counting_resource dual gate per `[[feedback_tracking_pmr_resource_false_pass]]` | ✓ |
| VIII.2 | ±5% perf regression gate | Platform-conditional latency ceilings recorded in plan; bench baseline per `[2g §6.3]` | ✓ |
| IX.1 | ≥95% line / ≥85% branch on touched modules | Targeted via test plan; YELLOW carve-out per `[[feedback_codecov_patch_vs_lcov_da_brda_gate]]` if file_cert_source I/O fault paths cannot be reached without injecting filesystem-fault hooks (precedent: PR #73 / #74 / #77 / #82) | ✓ (planned; documented if YELLOW) |
| IX.2 | Sanitizer Tier 1 ASan+UBSan+TSan | All three enforced per /speckit-verify | ✓ |
| X.2 | No `extern "C"` symbol emitted | `nm` check inherited; 011 emits no C ABI (2i bridges) | ✓ |
| XI.2 | ASIO native cancellation slots | `cert_source::load_credentials` honours per FR-021; recipe from `[2d §6.5]` | ✓ |
| XI.3 | `fixpp::sync::async_mutex` for serialised mutation in coroutine context | Pinset writer is NOT coroutine context — synchronous from operator's control-plane thread; uses `std::shared_mutex` per `[2g §6.5]`. No coroutine-suspending lock anywhere in this feature. | ✓ (rationale recorded) |
| XI.4 | Per-session strand | `cert_source::load_credentials` runs on the session strand from the consuming coroutine in 2h-transport's future code; this feature publishes the awaitable shape | ✓ |
| **XII (Security & TLS)** | OpenSSL on Linux + Windows; TLS 1.2 + 1.3 only; compile-time allow-list; `SecurityProfile` explicit; `EncryptMethod(98)≠0` rejection; pluggable `cert_source`; security mandatory controls | **All eight subsections honoured**; FR-014 codifies the TLS 1.3-preferred / 1.2-fallback posture from `/clarify` Q1 | ✓ (central — see Phase-0 research §1) |
| XIV.2 | Pluggable interface ≤ 5 pure-virtuals | `cert_source` ships **2** (`load_credentials`, `load_trust_anchors`); margin is intentional for HSM/KMS extension | ✓ |
| XIV.4 | No `dlopen` plugin loading | Compile-time selection only | ✓ |
| XV.5 | No sync logging on hot path | This feature does not log on the Pinset hot path; `file_cert_source` cold-path logging routes through the standard async logger (T-2k integration) | ✓ |
| XV.9 | No `std::mutex` in coroutine context | Pinset uses `std::shared_mutex` on the synchronous writer path (NOT coroutine context); reader path is lock-free | ✓ |
| XV.11 | Banned TLS versions / ciphers compile-time refusal | `CipherPolicy::tls13_suites` / `tls12_suites` `static_assert` against banned tokens per `[2g §4.4]` / `[2g §6.1]` | ✓ |
| XV.10 | `EncryptMethod(98)≠0` rejection | Owned by wire-validator + session-FSM, NOT by 2g per spec.md FR-026/`[2g §2]`; this feature records the constitutional pin only | ✓ (cross-cut documented) |
| XVI.3 | `/clarify` mandatory for security | Completed 2026-05-23, 5 Qs resolved | ✓ |
| XVII.1 | Phase-4 Gate A blocks `/tasks` | Next gate after this `/plan` | ✓ (procedural — not yet executed) |
| XVII.8 | `/speckit-verify` mandatory; paired-evidence labels | Procedural — `/verify` runs post-/simplify, pre-Gate-B | ✓ (procedural) |

**No constitution violations to track.** No `Complexity Tracking` table entries needed. The choice of `std::shared_mutex` over `async_mutex` is explicitly rationale-recorded above (XI.3 row) rather than treated as a violation — `[const §XV.9]` bans `std::mutex` in **coroutine context**, and Pinset's writer is **not** a coroutine context; the design-doc `[2g §6.5]` is the binding authority and is already Gate-A-signed-off.

## Project Structure

### Documentation (this feature)

```text
specs/011-tls-policy/
├── plan.md              # This file (/speckit-plan output)
├── research.md          # Phase 0 output (/speckit-plan)
├── data-model.md        # Phase 1 output (/speckit-plan)
├── quickstart.md        # Phase 1 output (/speckit-plan)
├── contracts/           # Phase 1 output (/speckit-plan)
│   ├── cert_source.hpp           # cert_source + Config + local_credentials + signer variant
│   ├── pinset.hpp                # Pinset + pin_view + pin_snapshot + Config
│   ├── cipher_policy.hpp         # CipherPolicy compile-time allow-list + is_allowed
│   ├── security_profile.hpp      # SecurityProfile enum + SslCtxConfig + make_ssl_ctx_config
│   ├── certificate.hpp           # Certificate value-type view
│   ├── peer_identity.hpp         # peer_identity value type (T-041 cross-cut handoff to session/)
│   └── tls_errors.hpp            # error::tls_* variant family (re-export shape; defs in core/error.hpp)
├── checklists/
│   └── requirements.md  # Spec-Kit quality checklist (from /speckit-specify; all-pass first iteration)
├── tasks.md             # NOT YET — produced by /speckit-tasks after Phase-4 Gate A
└── spec.md              # /speckit-specify + /speckit-clarify output (already authored)
```

### Source Code (repository root — submodule `library/`)

```text
library/
├── include/fixpp/
│   ├── core/
│   │   └── error.hpp                       # APPENDED — 13 new `error::tls_*` variants at next free slots
│   └── tls/                                # NEW MODULE
│       ├── cert_source.hpp
│       ├── file_cert_source.hpp            # default impl + factory `make_file_cert_source`
│       ├── pinset.hpp
│       ├── cipher_policy.hpp
│       ├── security_profile.hpp            # SecurityProfile enum + SslCtxConfig + make_ssl_ctx_config
│       ├── certificate.hpp
│       ├── peer_identity.hpp
│       └── tls_errors.hpp                  # re-export of fixpp::tls::errors::tls_* aliases for ergonomic at-site use
│
├── src/tls/                                # NEW MODULE
│   ├── file_cert_source.cpp                # PEM/DER parse + chain build + factory body
│   ├── pinset.cpp                          # shared_mutex writer + atomic<shared_ptr> reader publication
│   ├── security_profile.cpp                # make_ssl_ctx_config body + table validation
│   ├── certificate.cpp                     # DER parse helpers (subject DN, issuer DN, SAN extraction, SHA-256-of-DER)
│   └── verify_peer.cpp                     # verify_peer(SslCtxConfig const&, Certificate const&, std::span<const Certificate>, Clock const&) -> expected_t<peer_identity>
│
├── tests/tls/                              # NEW
│   ├── fixtures/                           # PEM/DER fixtures (generated via tests/tls/fixtures/Makefile + openssl CLI)
│   │   ├── ca.pem, ca.key
│   │   ├── leaf_rsa2048.pem, leaf_rsa2048.key
│   │   ├── leaf_rsa1024_negative.pem
│   │   ├── leaf_rsa16384_negative.pem
│   │   ├── leaf_ecdsa_p256.pem, leaf_ecdsa_p256.key
│   │   ├── leaf_ecdsa_p521_negative.pem    # rejected (P-256/P-384 only)
│   │   ├── leaf_x509v1_negative.pem        # rejected (v2/v3 only)
│   │   ├── leaf_san_64.pem                 # at the cap
│   │   ├── leaf_san_65_negative.pem        # over the cap
│   │   ├── leaf_der_16KiB_negative.pem     # at the DER cap
│   │   ├── chain_depth_8.pem               # at the depth cap
│   │   ├── chain_depth_9_negative.pem      # over the depth cap
│   │   ├── leaf_expired.pem                # expired vs mock_clock
│   │   └── leaf_encrypted_pem.pem          # passphrase "test"
│   ├── test_cert_source_plugin_interface.cpp     # US2 scenarios — operator-supplied cert_source observable signing-off-strand
│   ├── test_file_cert_source_load.cpp            # US2 AC4 — factory error path + happy path
│   ├── test_file_cert_source_password.cpp        # encrypted-PEM happy + wrong-password
│   ├── test_load_credentials_cancellation.cpp    # FR-021 ASIO cancellation seam (recipe per [2d §6.5])
│   ├── test_pinset_add_then_remove.cpp           # US1 AC1+AC2 happy path
│   ├── test_pinset_add_then_remove_deterministic.cpp  # [2g §9 seam #1] hook-driven non-flaky test
│   ├── test_pinset_remove_before_add.cpp         # US1 AC3 — operator-induced ordering error surfaced as tls_cert_pin_mismatch
│   ├── test_pinset_cap_enforcement.cpp           # US1 AC4 — add over max_pins refused
│   ├── test_pinset_pin_view_outlives_remove.cpp  # FR-008 — concurrent remove + held pin_view (TSan)
│   ├── test_pinset_per_counterparty_sharing.cpp  # FR-009a — two sessions sharing one Pinset rotate atomically
│   ├── test_cipher_policy_compile_time_refusal.cpp  # FR-011 — `static_assert` static-assertion-emit; this is a `static_assert(!is_allowed(banned), …)`-style positive test
│   ├── test_cipher_policy_is_allowed.cpp         # FR-012 — runtime predicate
│   ├── test_security_profile_adapter.cpp         # FR-014 — SslCtxConfig mapping table row-by-row; TLS 1.3-preferred / 1.2-fallback (Clarify Q1)
│   ├── test_security_profile_empty_pinset.cpp    # FR-025 — make_ssl_ctx_config rejects mtls_pinned + empty Pinset with tls_pin_empty_at_open (Clarify Q2)
│   ├── test_verify_peer_dos_bounds.cpp           # FR-019 + FR-020a — each cap rejection + first-violation-hit ordering (Clarify Q3)
│   ├── test_verify_peer_x509v1_reject.cpp        # FR-020 — X.509 v1 negative
│   ├── test_verify_peer_expiration.cpp           # FR-020 — vs mock_clock via [2d §7.9] effective_clock
│   ├── test_verify_peer_ecdsa_curve_reject.cpp   # FR-020 — P-521 negative (P-256/P-384 only)
│   ├── test_peer_identity_san_owning.cpp         # FR-018 — SAN list ownership + cap
│   └── test_certificate_lifetimebound.cpp        # FR-017 — `-Wdangling` smoke test against the abstract base
│
├── tests/perf/
│   └── test_tls_alloc_guard.cpp                  # FR-007 — Pinset::find hot-path zero-alloc (counting_resource + mallocnesia dual gate)
│
├── bench/tls/                                    # NEW
│   ├── bench_pinset_snapshot_acquire.cpp         # [2g §6.3] platform-conditional latency ceiling
│   └── bench_verify_peer_in_envelope.cpp         # ≤ 1 ms p99 in-envelope handshake-validation budget
│
├── tools/
│   └── check_layers.py                           # APPEND `tls/` module registration BEFORE /speckit-implement
│
├── spec/
│   ├── feature-catalogue.md                      # APPEND 011-tls-policy + flip T-006/007/008/011/013 to `implementing`
│   └── coverage-index.md                         # APPEND 011 ledger; rotate "Active feature" pointer
│
└── docs/src/
    └── tls-quickstart.md                         # operator quickstart (mdBook) — companion to specs/011-tls-policy/quickstart.md
```

**Structure Decision**: this feature establishes the `tls/` module from scratch — no prior code in `include/fixpp/tls/` or `src/tls/`. Layer position: `tls/` consumes `core/` (and indirectly OpenSSL); `session/` will consume `tls/`'s `peer_identity` at the post-this-feature T-041 binding boundary. `tools/check_layers.py` must register `tls/` BEFORE `/speckit-implement` per `[[feedback_gate_b_check_layers_post_fixer]]`.

## Complexity Tracking

**No entries.** No constitution violations need justification. The mutex-type choice (`std::shared_mutex` over `fixpp::sync::async_mutex`) is rationale-recorded in the Constitution Check above (Article XI.3 row) and traces to `[2g §6.5]` which is Gate-A-signed-off — it is not a complexity exception, it is the design-doc-mandated choice for synchronous writer paths.
