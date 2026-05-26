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
7. **`error::tls_*`** variant family — **the 15-variant set re-emitted from `[2g §6.6]` verbatim** + the `tls_pin_empty_at_open` amendment from `/clarify` Q2 (16 variants total per FR-025; Codex P2-1 + NEW-P1-4 close). The `tls_handshake_failed` GROUPING variant is the C-ABI coalescing scheme's anchor — it carries a diagnostic-field sub-reason for non-DoS-cap rejections. `verify_peer` short-circuits on the **first violation hit in a documented 10-step evaluation order** (`/clarify` Q3 → FR-020a; canonical list at `contracts/security_profile.hpp:150-165`): (1) per-cert DER size → (2) RSA key lower bound (≥2048) → (3) RSA key upper bound (≤ `max_rsa_key_bits`) → (4) ECDSA curve (P-256/P-384) → (5) chain depth → (6) SAN cardinality → (7) X.509 version (v2/v3) → (8) expiration vs `effective_clock` → (9) Pinning under `mtls_pinned` (scan `cfg.pinset_snapshot` for `leaf.sha256`) → (10) `CipherPolicy::is_allowed`. (Amended round 3 hand-edit 2026-05-23 — 8-step prose aligned with the 10-step canonical list.)

**Catalogue rows owned**: T-006 (TLS 1.2), T-007 (TLS 1.3), T-008 (CipherPolicy / SecurityProfile bound to the OpenSSL adapter shape), T-011 (Pinset rotation), T-013 (cert_source plugin). **Cross-cuts owned with 2h-transport**: T-039 (validation predicate; OpenSSL handshake hook lives in 2h), T-040 (cert_source consumes operator-distributed secrets). **Cross-cut into session/ Phase-4**: T-041 (peer_identity value supplied here; CompID binding logic in session feature).

**Branch base**: `011-tls-policy` rooted on post-PR-#83-merge `main` of the library submodule (`25cf09d`). No carry-overs from 005 / 009 / 010 work — the session-layer slice is fully merged and this slice opens a **new module** under `include/fixpp/tls/` (no prior code; the `tls/` directory is being established by this feature). At Gate B convergence the slice merges to `main` and bumps the parent-repo submodule pointer per the standard pattern (see post-010 `parent 1e6dc17` + close-out commit).

**Gate A required**: yes. This feature opens a new module surface (`include/fixpp/tls/`) and a new public type cluster — `[const §XII.9]` demands all four mandatory controls (`/clarify`, `/analyze`, Codex Gate A, user `/plan` sign-off) for security-affecting features. **Not Gate-A-inherited from anywhere** — 2g-tls.md is a Phase-2 design-doc Gate A (already converged), but the Phase-4 feature Gate A is independent and reviews this `specs/011-tls-policy/` bundle. See `[const §XVII.1]`.

## Technical Context

**Language/Version:** C++23 (`[const §II.1]`). Coroutines (`asio::awaitable<T>`), `std::expected` (via `core::expected_t`), `std::pmr`, `std::span`, `std::variant`, `std::array`, `std::shared_mutex`, `std::atomic<std::shared_ptr<T>>` where available (with the `[2f §6.5]` fallback split per platform), `[[clang::lifetimebound]]`, `[[nodiscard]]`, `[[deprecated]]`, `static_assert`, deducing `this`.

**Primary Dependencies:** **One new Conan row to vet** — **OpenSSL 3.x** (`OPENSSL_FOUND` already exercised by build-system probes for the 2h-transport future feature, but this slice is the first to materially link). Reuses everything 005 / 006 / 007 / 008 already depend on: `fixpp::core` (`expected_t`, `error`, `trap_throw`, `Clock` / `mock_clock`, `cancellable_dispatch`, `session_executor`), `fixpp::sync::async_mutex` (referenced but NOT consumed — Pinset uses `shared_mutex`, not async_mutex, because Pinset rotation is fully synchronous; `[2g §6.5]` is normative here). GoogleTest 1.17.0 + Google Benchmark 1.9.5 pinned. No new GoogleMock surface introduced.

**Storage:** N/A. `file_cert_source` reads from the filesystem at cold-path load time only; `cert_source` itself is filesystem-agnostic (the file-path is one impl shape; HSM/KMS/vault are others). No state persisted by this feature.

**Testing:** GoogleTest + GoogleMock (C++), TDD red-green-refactor (`[const §VII.1]` / `[const §VII.3]`). Deterministic time via `fixpp::core::mock_clock` (cert-expiration tests). PEM/DER test fixtures generated under `tests/tls/fixtures/` (CA + leaf chains; rotated-pair set; encrypted-PEM with known passphrase; X.509 v1 negative-fixture; SAN-over-cap negative-fixture; DER-over-cap negative-fixture; RSA-1024 negative-fixture; RSA-16384 negative-fixture). Sanitizer matrix per `[const §IX.2]`: ASan + UBSan + **TSan** + GCC release sanity — TSan is critical because Pinset's reader/writer separation is the central concurrency seam this feature ships.

**Target Platform:** Same Tier-1 matrix as 005–010: Linux Clang Debug + Release + ASan + UBSan + TSan + Coverage; GCC Release sanity. Windows Tier 2 manual/nightly (no coverage step — `[const §IX.1]`). No C-ABI surface added by 011 — `[const §IX.5]` abidiff N/A (2i is where the C ABI surfaces; this feature publishes the **C++ source-of-truth** and the per-doc-prefix `FIXPP_ERR_TLS_*` coalescing-group rule).

**Project Type:** C++23 library, **NEW module** `tls/` — first feature to ship `include/fixpp/tls/*.hpp` + `src/tls/*.cpp` + `tests/tls/test_*.cpp` + `bench/tls/bench_*.cpp`. Module layer per `tools/check_layers.py` + `[arch §4.6]` (the `tls` surface inventory — round-2 NEW-P2-2 close): `tls/` sits at the same architectural tier as `session/` (both consume `core/`; `session/` will consume `tls/`'s `peer_identity` at the T-041 binding boundary, post-this-feature). The check-layers config MUST be updated to register the new module BEFORE `/speckit-implement` per `[[feedback_gate_b_check_layers_post_fixer]]`.

**Performance Goals:**

- **Hot-path lookup (`Pinset::find` / `Pinset::snapshot_acquire`):** Platform-conditional ceiling per `[2g §6.3]`:
  - libstdc++ ≥ 16 / libc++ on x86_64 with `cmpxchg16b` (lock-free `atomic<shared_ptr>`): **≤ 30 ns p99** snapshot_acquire, **≤ 130 ns p99** full find (snapshot + linear scan 16).
  - libstdc++ ≤ 15 (mutex-fallback `atomic<shared_ptr>`): **≤ 100 ns p99** snapshot_acquire, **≤ 200 ns p99** full find.
  CI fails on > 5% regression vs the applicable ceiling per `[const §VIII.2]`. Bench records the platform tuple (`compiler / stdlib / stdlib-version / cpu-supports-cmpxchg16b`) and selects the matching ceiling at runtime.
- **Cold-path load (`file_cert_source::load_credentials`):** ≤ 50 ms soft per call (file I/O + parse + chain build). No `[const §VIII.5]` applicability — cold path.
- **Cold-path validate (`verify_peer`):** ≤ 1 ms p99 for an in-envelope chain (depth ≤ 4, RSA-2048 or ECDSA-P-256, ≤ 8 SAN entries). DoS-reject paths (RSA-16384 / DER-too-large / SAN-too-many) MUST short-circuit ≤ 50 µs p99 — the bound is the whole point of the cap.
- **Cipher allow-list compile-time refusal:** `static_assert` cost is build-time only; no runtime cost.

**Constraints:**

- **Zero global `new`/`delete` on the handshake-read hot path** (`Pinset::find` / `Pinset::snapshot_acquire` / `verify_peer`) per `[const §VIII.5]` / `[const §XV.1]`. Verified via `tests/tls/test_tls_handshake_alloc_guard.cpp` (`[2g §9 seam #7]`) + mallocnesia LD_PRELOAD dual gate (lessons from `[[feedback_tracking_pmr_resource_false_pass]]` — counting_resource alone is not sufficient; mallocnesia per `[[reference_mallocnesia_path]]` MUST be wired).
- **Cold-path PMR allocation** permitted for `file_cert_source::load_credentials` (file I/O + parse + chain build) and for `peer_identity` SAN-list owning vectors. PMR throws routed through `[2a §4.2]` `trap_throw` and surface as `error::tls_cert_load_failed` (per `[2g §6.6]`) or `error::tls_pinset_alloc_failed` (warm-path Pinset::add snapshot-clone failures). No exception escapes the public surface during the session-handling window (`[arch §5.3]`).
- **`make_file_cert_source` construction-time throws** permitted per `[arch §5.3]` carve-out (engine bootstrap before any session is open); the factory wraps that boundary in `expected_t<...>` so non-construction-time callers (2i C ABI, future hot-reload) never see a thrown exception.
- **ASIO native cancellation slots end-to-end** on `cert_source::load_credentials` per `[const §XI.2]` / `[SYN §3.2 Q6a]`; cancellation completes with `expected_t::unexpected{tls_load_cancelled}` (FR-021). Recipe lifted verbatim from `[2d §6.5]` `cancellable_dispatch`.
- **`Pinset` mutex choice:** `std::shared_mutex` (NOT `std::mutex`, NOT `fixpp::sync::async_mutex`). Rationale at `[2g §6.5.2]` — cite-and-stop per NEW-P2-6 close (round-2 NEW-P2-1 close: the round-1 (a)/(b)/(c) summary block is dropped per NEW-P2-6's "the v0.1 anti-pattern of three-rationales-in-three-sites that 2g §6.5.2 explicitly closed must not be re-introduced").
- **Per-session strand serialisation** (`[const §XI.4]`) for the coroutine that calls `cert_source::load_credentials`; the cancellation slot is read inside the coroutine before any work dispatch.
- **No C-ABI surface emitted** by 011 (`[const §X.2]` nm check inherited). 2i bridges; 011 publishes the C++ source-of-truth + per-doc-prefix `FIXPP_ERR_TLS_*` coalescing-group naming rule.
- **Pluggable interface cap**: `cert_source` ships **2 pure-virtual methods** — well under `[const §XIV.2]`'s ≤ 5. Design-doc justification is not needed (only required for > 5).
- **No `dlopen` plugin loading** per `[const §XIV.4]`. Cert-source impls are compile-time linked (the file default; user-side HSM/KMS impls compiled into the user binary linking against fixpp).

**Scale/Scope:** Estimated edit footprint (new code, no pre-existing `tls/` directory):

- **Headers**: 7 new public headers under `include/fixpp/tls/` — `cert_source.hpp`, `pinset.hpp`, `cipher_policy.hpp`, `security_profile.hpp`, `certificate.hpp`, `peer_identity.hpp`, `tls_errors.hpp` (the last folds the `error::tls_*` enum variants into `include/fixpp/core/error.hpp` — single point of truth — and `tls_errors.hpp` re-exports the per-doc-prefix grouping under `fixpp::tls::errors` for ergonomic at-site use). Public surface: ~700 lines total across the 7 headers + ~200 lines of `[[clang::lifetimebound]]` / `[[nodiscard]]` / `static_assert` annotations.
- **`include/fixpp/core/error.hpp`** appended with **16 new `error::tls_*` enum variants** at the next free slots (the 15 `[2g §6.6]` variants per FR-025 + the `tls_pin_empty_at_open` `/clarify` Q2 amendment; slot allocation must respect prior 005/008/009/010 pinning per `[[project_2e_design_doc_only_seqnum_handoff]]` — slots already locked at 49/51/52/54/55/57/58/60/63/77; 011 takes the next free contiguous range, NOT renumber).
- **Implementation**: ~5 new `.cpp` files under `src/tls/` — `file_cert_source.cpp`, `pinset.cpp`, `security_profile.cpp` (the `make_ssl_ctx_config` factory), `certificate.cpp` (DER parse), `verify_peer.cpp` (the validation predicate). Total ~1.2k–1.5k lines. The two factories (`make_file_cert_source` + `make_ssl_ctx_config`) live in dedicated `.cpp` files so 2h-transport links against the symbols cleanly.
- **Tests**: the test list IS the 18 binding seams from `[2g §9]` (NEW-P1-3 close) + one `/clarify`-Q2-driven additional test (`tls_pin_empty_at_open`) + two FR-aligned contract witnesses (per-counterparty sharing; lifetimebound smoke). The total test binary count is therefore: 14 named `tests/tls/test_*.cpp` files (seams #1, #2, #7, #8, #9, #10, #11, #12, #13, #14, #15, #16, #17, #18 + the Q2-driven empty-pinset test + per-counterparty + lifetimebound) + 1 fuzz target (`tests/fuzz/fuzz_file_cert_source.cpp` — seam #6) + 1 conformance test (`tests/conformance/test_fixs_rotation.cpp` — seam #3). PEM/DER fixture authoring is a sub-task (one-time, manual + `openssl` CLI; checked in as binary fixture).
- **Bench**: 3 new bench binaries (matching the 3 latency-row entries in `[2g §6.3]` benched via `[2g §9 seams #4, #5]`) — `bench_pinset_snapshot_acquire.cpp` (seam #4 — snapshot_acquire ceiling) + `bench_pinset_find.cpp` (seam #5 — full find ceiling; Codex P2-4 + NEW-P1-3 close) + `bench_verify_peer_in_envelope.cpp` (the ≤ 1 ms p99 in-envelope handshake-validation budget).
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
| IX.4 | Fuzz on parser-touching surfaces | `file_cert_source` parses PEM/DER (parser-touching); `tests/fuzz/fuzz_file_cert_source.cpp` per `[2g §9 seam #6]` runs under ASan + UBSan invariants (NEW-P2-3 close) | ✓ |
| X.2 | No `extern "C"` symbol emitted | `nm` check inherited; 011 emits no C ABI (2i bridges) | ✓ |
| XI.2 | ASIO native cancellation slots | `cert_source::load_credentials` honours per FR-021; recipe from `[2d §6.5]` | ✓ |
| XI.3 | `fixpp::sync::async_mutex` for serialised mutation in coroutine context | Pinset writer is NOT coroutine context — synchronous from operator's control-plane thread; uses `std::shared_mutex` per `[2g §6.5.2]` (consolidated rationale — cite-and-stop per NEW-P2-6 close). No coroutine-suspending lock anywhere in this feature. | ✓ (rationale recorded at `[2g §6.5.2]`) |
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
│   │   └── error.hpp                       # APPENDED — 16 new `error::tls_*` variants at next free slots (matches the line-71 narrative + tls_errors.hpp slots 78..93 — round-2 NEW-P1-1 close)
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
│   └── verify_peer.cpp                     # verify_peer(SslCtxConfig const&, std::span<const Certificate> peer_chain) noexcept -> expected_t<peer_identity> per [2g §4.5] lines 700-701 (round-2 P1-3 propagation close — ONE peer_chain parameter; the leaf is peer_chain[0]; cfg.clock + cfg.pinset_snapshot reach the predicate via SslCtxConfig).
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
│   │
│   # ── Test list re-emitted from [2g §9]'s 18 BINDING TEST SEAMS verbatim.
│   # ── (NEW-P1-3 close: the plan.md test list is consumed from the design-doc
│   # ── inheritance contract by NAME, not re-derived from the FRs. Each seam
│   # ── below maps to a numbered seam in [2g §9] lines 1093-1127 + the one
│   # ── /clarify-Q2-driven additional test for tls_pin_empty_at_open.
│   #
│   ├── test_pinset_add_then_remove_deterministic.cpp      # [2g §9 seam #1] — Pinset add-then-remove ordering, hook-driven non-flaky deterministic witness.
│   ├── test_pinset_add_then_remove_stress.cpp             # [2g §9 seam #2] — TSan stress: thread A add/remove loop; thread B find loop; verify no UAF / data race under TSan + ASan.
│   # [2g §9 seam #3] — conformance corpus FIXS rotation lives at tests/conformance/test_fixs_rotation.cpp (see below).
│   # [2g §9 seam #4] — bench_pinset_snapshot_acquire lives at bench/tls/ (see below).
│   # [2g §9 seam #5] — bench_pinset_find lives at bench/tls/ (see below; the Codex-P2-4 + NEW-P1-3 gap).
│   # [2g §9 seam #6] — fuzz harness lives at tests/fuzz/ (see below; the NEW-P1-3 + NEW-P2-3 gap).
│   ├── test_tls_handshake_alloc_guard.cpp                 # [2g §9 seam #7] — alloc-guard on the handshake-hot path (mallocnesia + counting_resource dual-gate) on Pinset::snapshot + find + verify_peer.
│   ├── test_hsm_async_signer_mock.cpp                     # [2g §9 seam #8] — HSM-style async_signer_ref mock; verify signing routes off the session strand via cancellable_dispatch per the §6.4 recipe.
│   ├── test_pinset_single_thread_ordering.cpp             # [2g §9 seam #9] — Pinset single-thread add/add/remove + duplicate-add + remove-absent + pin_view::found() correctness.
│   ├── test_cipher_allow_list_static_assert.cpp           # [2g §9 seam #10] — Codex P2-2 + NEW-P1-3 close: CMake `try_compile` NEGATIVE-COMPILE test that attempts to declare a CipherPolicy variant including "ECDHE-RSA-AES128-CBC-SHA256" (CBC banned per [const §XII.3]); verify the build FAILS with the expected static_assert message. Companion positive try_compile confirms the published allow-list arrays compile clean. Lives in tests/tls/ with companion tests/tls/CMakeLists.txt declaring both negative- and positive-compile targets.
│   ├── test_security_profile_mapping.cpp                  # [2g §9 seam #11] — SecurityProfile-to-OpenSSL-mode row-by-row mapping per [2g §4.5.1] (NEW-P1-3 sub-bullet close: each row of the §4.5.1 4-row table verified incl. the acceptor/initiator role split for mtls_pinned and mtls_ca rows + the unset / mtls_pinned-with-null-Pinset / one_way_ca-with-non-null-Pinset / null-clock rejections). Additionally verifies the SecurityProfile::one_way_ca [[deprecated]] diagnostic emission (NEW-P2-2 close) via a try_compile witness with -Wdeprecated-declarations -Werror.
│   ├── test_tls_error_variants.cpp                        # [2g §9 seam #12] — exercise every error::tls_* variant from [2g §6.6] (15 variants + tls_pin_empty_at_open amendment = 16 total) via synthesised failing inputs; assert returned expected_t::unexpected carries the expected variant + diagnostic sub-reason for `tls_handshake_failed`.
│   ├── test_load_credentials_cancellation.cpp             # [2g §9 seam #13] — FR-021 ASIO cancellation seam; recipe per [2g §6.4] / [2d §6.5] verbatim under both `per_session_strand` and `direct_executor` modes; injects probe at recipe step 4 (cancellable_dispatch) and asserts the dispatch is observed and reaped.
│   ├── test_verify_peer_t039.cpp                          # [2g §9 seam #14] — T-039 cert-parameter validation: each rejection criterion (RSA < 2048, RSA > 8192, ECDSA non-P-256/P-384, X.509 v1, expired vs cfg.clock, not-yet-valid, SHA-1 sig, banned sig_alg, DER > 16 KiB, SAN > 64) returns expected_t::unexpected of the right variant. Includes the FR-020a short-circuit-first-violation-hit witness per /clarify Q3.
│   ├── test_pinset_rotation_does_not_affect_in_flight.cpp # [2g §9 seam #15] — mid-session rotation does NOT affect in-flight handshake; verify the handshake captures Pinset::snapshot() ONCE before pause per [2g §6.5.1] BINDING CONTRACT; concurrent add/remove on a separate thread; in-flight handshake observes pre-rotation snapshot; next handshake observes post-rotation.
│   ├── test_pin_view_lifetime_under_rotation.cpp          # [2g §9 seam #16] — RC#1 close: pin_view lifetime under concurrent rotation; thread A holds pin_view from find(); thread B issues add+remove repeatedly; assert under ASan + TSan that pin_view::value dereference remains valid.
│   ├── test_make_file_cert_source_factory.cpp             # [2g §9 seam #17] — RC#2 close: make_file_cert_source factory parity. (a) factory returns expected_t<shared_ptr<cert_source>> with NO exception thrown; (b) parse-failure surfaces tls_cert_parse_failed; (c) load-failure surfaces tls_cert_load_failed; (d) success returns a shared_ptr whose load_credentials() / load_trust_anchors() mirror the throwing-constructor `file_cert_source` directly. Companion positive-compile test that 2i's C-ABI bridge stub calls `make_file_cert_source` (not the throwing constructor) through the factory's expected_t<...> shape.
│   ├── test_pinset_snapshot_outlives_pinset.cpp           # [2g §9 seam #18] — round-3 P1-1 close: post-`~Pinset()` snapshot lifetime under the Pinset::Config::mr outlives-snapshot contract; construct Pinset against scope-outer monotonic_buffer_resource; add 2 pins, snapshot once, drop Pinset; exercise read-only access on snapshot's pin::subject_dn / san_dns; drop reader and verify ~vector<pin> runs through the still-live MR. Companion negative test where MR's scope is INSIDE Pinset scope (contract violation) MUST surface ASan UAF.
│   #
│   # ── /clarify-Q2-driven additional test (the one legitimately new test
│   # ── since 2g §9 signed off; binds to FR-025 + US3 AC5).
│   ├── test_security_profile_empty_pinset.cpp             # /clarify Q2 — make_ssl_ctx_config(mtls_pinned, ..., empty_pinset) returns unexpect{tls_pin_empty_at_open}.
│   #
│   # ── Additional contract-binding witnesses (FR-aligned; do not displace [2g §9] seams).
│   ├── test_pinset_per_counterparty_sharing.cpp           # FR-009a / /clarify Q5 — two sessions sharing one shared_ptr<Pinset> see rotation atomically across both on the next handshake.
│   └── test_certificate_lifetimebound.cpp                 # FR-017 — `-Wdangling` smoke test against the abstract base declaration site (the [arch §5.5] / [2b §6.4] precedent).
│
├── tests/perf/                                           # the alloc-guard seam #7 lives under tests/tls/ above
│
├── tests/fuzz/                                           # [const §IX.4] / [2g §9 seam #6] NEW-P2-3 close
│   └── fuzz_file_cert_source.cpp                          # libFuzzer-driven random PEM/DER inputs feeding file_cert_source construction; ASan + UBSan invariants; verify no crash / UAF / UB on adversarial inputs.
│
├── tests/conformance/                                    # NEW
│   └── test_fixs_rotation.cpp                             # [2g §9 seam #3] — recorded FIXS rotation scenario driven end-to-end against a MockTransport replaying canned handshake bytes for both old and new certs.
│
├── bench/tls/                                            # NEW
│   ├── bench_pinset_snapshot_acquire.cpp                  # [2g §6.3] / [2g §9 seam #4] — platform-conditional latency ceiling (≤ 30 ns lock-free floor / ≤ 100 ns libstdc++ ≤ 15 fallback) on Pinset::snapshot().
│   ├── bench_pinset_find.cpp                              # [2g §9 seam #5] — Codex P2-4 + NEW-P1-3 close: latency regression on Pinset::find(sha256) at max_pins = 16; ≤ 130 ns p99 on lock-free floor / ≤ 200 ns p99 on libstdc++ ≤ 15 fallback. SC-007's "≤ 130 ns p99 full find" is gated here.
│   └── bench_verify_peer_in_envelope.cpp                  # ≤ 1 ms p99 in-envelope handshake-validation budget (the [2g §6.3] verify_peer row).
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

**No entries.** No constitution violations need justification. The mutex-type choice (`std::shared_mutex` over `fixpp::sync::async_mutex`) is rationale-recorded in the Constitution Check above (Article XI.3 row) and traces to `[2g §6.5.2]` (the CONSOLIDATED rationale per NEW-P2-6 — cite `[2g §6.5.2]` and STOP; do not re-derive the rationale at other sites) which is Gate-A-signed-off — it is not a complexity exception, it is the design-doc-mandated choice for synchronous writer paths.

## Gate A

### Round 1 applied 2026-05-23 (amended at round 2)

- Round 1 applied 2026-05-23: Codex P1=4 P2=4 P3=2; Opus post-judging P1=8 P2=6 P3=3; rewrite addresses root causes [1: bundle paraphrased 2g v0.4 → re-emitted verbatim from 2g §4/§6/§9; 2: plan.md test list re-emitted from `[2g §9]` 18 binding seams; 3: error envelope transcribed verbatim from 2g §6.6 incl. `tls_handshake_failed` coalescing group]. Reviews: `research/reviews/codex_011-tls-policy_gate_a_review.md`, `research/reviews/opus_011-tls-policy_gate_a_adversarial_review.md`. Adversarial-review Codex pass STALLED — rescue-only for round 1 per `[[feedback_gate_a_codex_dual_pass]]` round-3 fallback.
- **(amended at round 2 — round-2 Codex P2-2 close)** The round-1 disposition block below claimed seven findings were "addressed" that had only landed partially — Codex P1-1 (`cert_source` / `file_cert_source` propagation incomplete in spec.md / research.md / quickstart.md), Codex P1-2 (Pinset `add(Certificate)` propagation missed research.md D-5 + the /clarify Q4 bullet at spec.md:15), Codex P1-3 (verify_peer 10-step list missed in spec.md FR-020a + Assumptions + plan.md:155 signature), Codex P2-1 (research.md D-10 still cited the non-existent `tls_cert_chain_too_deep` variant), Codex P2-3 + NEW-P2-1 (PMR-failure seams collapsed into seam #12 — closure narrowed not addressed), NEW-P3-1 (the cipher_policy.hpp 0RTT restore touched the file and introduced fresh paraphrasing in kx_groups + sig_algs + banned_tokens). Each of those carried the "addressed (via X)" prose in the round-1 disposition block below; the round-2 row records the actual addressal.

### Round 1 — per-finding disposition

The rewrite addresses every Confirm / Escalate finding in the Opus adversarial review. Findings by class:

- **Codex P1-1 — `cert_source` / `file_cert_source` divergence**: addressed (cert_source.hpp re-emits 2g §4.1 verbatim incl. `expected_t<span<const Certificate>>` return; data-model E-1a restores `private_key_path`; data-model E-2 restores `chain` as `std::span<const Certificate>` view per 2g §4.1 lines 251-255).
- **Codex P1-2 — Pinset rewrites add(Certificate) → add(fingerprint)**: addressed (pinset.hpp re-emits 2g §4.3 verbatim; `add(Certificate const&)` consumes the cert for SHA-256 + subject_dn + SAN list at add() time; data-model E-5 + E-6 align).
- **Codex P1-3 — verify_peer step order + signature**: addressed (security_profile.hpp re-emits 2g §4.5 signature `verify_peer(SslCtxConfig const&, std::span<const Certificate> peer_chain)`; data-model E-14 + spec FR-020a align on the §6.5.1-conformant order; pinning step consumes the captured `cfg.pinset_snapshot` per NEW-P1-1).
- **Codex P1-4 — SecurityProfile enumerator count**: addressed (spec FR-013 rewritten to four enumerators including `unset = 0` sentinel; data-model E-10 + security_profile.hpp contract align with 2g §4.5 lines 650-656).
- **Codex P2-1 — TLS error envelope**: addressed (tls_errors.hpp + data-model E-15 + spec FR-025 all re-emit the 15-variant set from 2g §6.6 verbatim + the `tls_pin_empty_at_open` Q2 amendment; the `tls_handshake_failed` C-ABI coalescing group is restored as the grouping variant with diagnostic-field sub-reasons).
- **Codex P2-2 — compile-fail witness**: addressed (plan.md test list seam #10 explicitly names the `try_compile` NEGATIVE-COMPILE fixture).
- **Codex P2-3 — PMR-throw test**: addressed (test list seam #6 — fuzz_file_cert_source.cpp + the `tls_pinset_alloc_failed` variant restored to E-15; the PMR-throw witness is part of seam #12 `test_tls_error_variants.cpp`).
- **Codex P2-4 — `Pinset::find` bench**: addressed (plan.md bench list adds `bench_pinset_find.cpp` per seam #5).
- **Codex P3-1 — checklist future tense**: addressed below in `## Gate A — checklist editorial`.
- **Codex P3-2 — quickstart.md architecture link path**: addressed (quickstart.md Reference section now cites `../../.specify/architecture.md`).
- **NEW-P1-1 — verify_peer violates §6.5.1 snapshot-once-at-handshake-start**: addressed (security_profile.hpp adds `SslCtxConfig::pinset_snapshot` field; verify_peer's pinning step scans this captured snapshot, NEVER calls `cfg.pinset->find/contains/snapshot`; data-model E-11 + E-14 record the contract).
- **NEW-P1-2 — make_ssl_ctx_config parameter order**: addressed (signature `(profile, cs, clock, pinset = nullptr, mr = nullptr)` per 2g §4.5 lines 681-686 in security_profile.hpp + data-model E-11; the spurious `validation_caps` parameter is removed — DoS caps reach `verify_peer` via `cs->config()` per 2g §4.2 line 320-326; quickstart.md Scenarios A+B+C call shapes updated).
- **NEW-P1-3 — `[2g §9]` 18 binding seams not faithfully consumed**: addressed (plan.md Project Structure test list re-emitted from 2g §9 verbatim by seam name; seams #5 / #6 / #10 / #18 + the §4.5.1 row-by-row mtls_pinned/initiator-vs-acceptor split for seam #11 explicitly named).
- **NEW-P1-4 — pin_snapshot type changed `pmr::vector<pin>` → `array<fingerprint>`**: addressed (data-model E-7 restores `using pin_snapshot = std::pmr::vector<pin>;` per 2g §4.3 line 421; pin's diagnostic fields restored per E-6; Pinset::Config::mr lifetime contract restored at E-5).
- **NEW-P2-1 — Pinset::add snapshot-clone PMR-throw test**: addressed (the `tls_pinset_alloc_failed` variant is in the E-15 envelope and is exercised by seam #12).
- **NEW-P2-2 — `one_way_ca [[deprecated]]` diagnostic-emission test**: addressed (seam #11 in plan.md test list now explicitly states it includes a try_compile witness with `-Wdeprecated-declarations -Werror` for the one_way_ca attribute placement).
- **NEW-P2-3 — `[const §IX.4]` fuzz requirement**: addressed (Constitution Check table now includes a IX.4 row; test list adds `tests/fuzz/fuzz_file_cert_source.cpp` per seam #6).
- **NEW-P2-4 — `Certificate::not_before/not_after` absolute-vs-relative ambiguity**: addressed (certificate.hpp + data-model E-12 add explicit "X.509-envelope ABSOLUTE wall-clock times — parsed from DER, NOT captured against cfg.clock" prose).
- **NEW-P2-5 — `cert_source::load_credentials` cancellation recipe not republished**: addressed (cert_source.hpp `§6.4 Cancellation contract — load_credentials recipe` section publishes the recipe body shape verbatim per 2g §6.4 lines 903-944).
- **NEW-P2-6 — `[const §XV.9]` rationale consolidation**: addressed (pinset.hpp now publishes the consolidated rationale once with a header-level comment; data-model E-5 + plan.md Constitution Check + research.md D-8 are pruned to a single cite to `[2g §6.5.2]` with NO re-derivation).
- **NEW-P3-1 — `/clarify` Q1 0-RTT compile-time refusal token dropped**: addressed (cipher_policy.hpp + data-model E-9 restore `"0RTT"` to `banned_tokens` per 2g §4.4 line 603; the token list now has 15 entries).
- **NEW-P3-2 — `/clarify` Q5 per-counterparty pattern missing from data-model E-5**: addressed (data-model E-5 prose now explicitly includes the per-counterparty granularity recommendation per FR-009a / `/clarify` Q5).
- **NEW-P3-3 — `[const §VI.5]` exact-anchor cite quality**: addressed (the `[2g §6.5]` cites that should be at `[2g §6.5.2]` are updated where the consolidated-rationale anchor is being cited; other cites verified to resolve verbatim).

### Round 1 — disagreements

No disagreements applied. The Opus adversarial review marked no Codex findings as "Disagree+reason" — every Codex P1/P2/P3 was Confirm (sometimes Confirm-and-extend) — so this section records "none" for round 1.

### Round 1 — checklist editorial (Codex P3-1)

The `checklists/requirements.md` file will be updated at the next pipeline cycle (step 7 `/speckit-checklist` re-emission post-Gate A) to reflect the completed `/clarify` pass (the future-tense prose at `:16` and `:35-38` is stale relative to spec.md `## Clarifications Session 2026-05-23`). Recorded here as procedurally-deferred editorial rather than blocking Gate A round 2.

### Round 2 applied 2026-05-23

- Round 2 applied 2026-05-23: Codex P1=4 P2=3 P3=0; Opus post-judging P1=6 P2=5 P3=2; rewrite addresses [the 6 P1: cipher_policy regression revert to 2g §4.4 verbatim (kx_groups 3 entries `X25519`/`secp256r1`/`secp384r1`; sig_algs 4 entries `ECDSA+SHA256`/`ECDSA+SHA384`/`RSA-PSS+SHA256`/`RSA-PSS+SHA384`; banned_tokens 12 entries with `SHA1` no-dash); cert_source / file_cert_source propagation to spec.md FR-004 + research.md D-7 + D-10 row + quickstart.md Scenario A struct literal; Pinset `add(Certificate const&)` propagation to research.md D-5; verify_peer 10-step list reconciliation to spec.md FR-020a + Assumptions paragraph; variant-count consistency 13→16 at plan.md project-structure tree; /clarify Q4 amendment marker on spec.md:15 preserving the SHA-256 decision while clarifying add(Certificate) ergonomics] + the 5 P2 [chain-depth variant-name drift research.md D-10 → `tls_handshake_failed` + sub-reason `"chain_too_deep"`; ## Gate A round-1 row marked `(amended at round 2)` per Codex P2-2; PMR-failure seams P2-3 carried as procedurally-deferred (the named seams remain a /speckit-tasks decision rather than a Gate-A blocker per the round-2 review's "lean toward adding the two named seams" framing — see the disposition below); rationale-consolidation summaries (a)/(b)/(c) at plan.md:62 dropped to cite-and-stop; `[arch §4.6]` cite added for tls/ module layer position] + 1 of the 2 P3 [system_clock_source name verified against `include/fixpp/core/system_clock_source.hpp` line 40 — type IS named `system_clock_source` in 007; the Opus review's NEW-P3-1 was based on a wrong premise (assumed type was `core::system_clock`) and explicitly allowed the rewriter to verify against 007; no edit needed; the second P3 was the same item, also no-op]. Reviews: `research/reviews/codex_011-tls-policy_gate_a_2_review.md`, `research/reviews/opus_011-tls-policy_gate_a_2_adversarial_review.md`. Adversarial-review Codex pass STILL STALLED — rescue-only continued for round 2 per `[[feedback_gate_a_codex_dual_pass]]` dual-pass-with-fallback. **rewrites=2/2 (AT CAP).**

### Round 2 — per-finding disposition

The rewrite addresses every Confirm finding in the round-2 Opus adversarial review. Findings by class:

- **Codex round-2 P1-1 — `cert_source` / `file_cert_source` propagation incomplete**: addressed (spec.md FR-004 changed `cert_source::Config` → `file_cert_source::Config` with E-1 / E-1a cite; research.md D-7 restores `load_trust_anchors`'s `core::expected_t<std::span<const Certificate>> [[clang::lifetimebound]]` signature per `[2g §4.1]` line 288; research.md D-10 row 1 changed `cert_source::Config::max_rsa_key_bits` → `file_cert_source::Config::max_rsa_key_bits`; quickstart.md Scenario A struct literal restored to `file_cert_source::Config` with `private_key_path` field added per `[2g §4.2]` line 319 + `password_callback` renamed to `password_cb` per `[2g §4.2]` line 320 / data-model E-1a).
- **Codex round-2 P1-2 — Pinset API split**: addressed (research.md D-5 rewritten so `add` consumes a `Certificate const&` and only `remove` / `find` key on the bare SHA-256 fingerprint per `[2g §4.3]` lines 486-495; spec.md:15 /clarify Q4 bullet amended with `(Amended round 2)` marker — the /clarify Q4 decision itself is preserved (lookup is still SHA-256-of-leaf-DER) and only the API ergonomics layer on top is clarified).
- **Codex round-2 P1-3 — `verify_peer` step-count drift**: addressed (spec.md FR-020a + Assumptions paragraph extended from the v0.1 8-step list to the canonical 10-step list matching data-model E-14 + `contracts/security_profile.hpp` lines 156-165 — pinning at step 9 + cipher at step 10; plan.md project-structure `verify_peer.cpp` comment rewritten to the `(SslCtxConfig const&, std::span<const Certificate> peer_chain) noexcept -> expected_t<peer_identity>` signature per `[2g §4.5]` lines 700-701).
- **Codex round-2 P1-4 — `CipherPolicy` 3-axis drift from 2g §4.4 verbatim (REGRESSION introduced by round-1 NEW-P3-1 close)**: addressed (`contracts/cipher_policy.hpp` lines 22-93 regenerated verbatim from `[2g §4.4]` lines 549-616: kx_groups restored to `X25519` / `secp256r1` / `secp384r1` — 3 entries; sig_algs restored to `ECDSA+SHA256` / `ECDSA+SHA384` / `RSA-PSS+SHA256` / `RSA-PSS+SHA384` — 4 entries (the round-1 fabricated `rsa_pss_rsae_sha512` row is gone); banned_tokens restored to 12 entries with `SHA1` no-dash spelling — the round-1 paraphrased additions `SHA-1` / `RSA-1024` / `TLS-1.0` / `TLS-1.1` / `SSL` are dropped (those are constitutional pins enforced via `[const §XII.3]` allow-list construction, NOT additional banned_tokens entries). Data-model E-9 likewise restored.
- **Codex round-2 P2-1 — chain-depth variant name**: addressed (research.md D-10 row 4 changed `tls_cert_chain_too_deep` → `tls_handshake_failed` with sub-reason `"chain_too_deep"` per `[2g §6.6]` line 995; matches the grouping-variant wording already used in FR-025 / data-model E-15 / contracts/tls_errors.hpp / verify_peer step 5).
- **Codex round-2 P2-2 — plan.md round-1 disposition-block accuracy**: addressed (round-1 row above marked `(amended at round 2)` with an explicit list of the seven findings whose round-1 closure was partial — Codex P1-1 / P1-2 / P1-3 / P2-1 / P2-3 + NEW-P2-1 / NEW-P3-1; this round-2 row records the actual addressal).
- **Codex round-2 P2-3 + Opus round-1 NEW-P2-1 — dedicated PMR-failure witnesses**: closure-narrowed procedurally-deferred to /speckit-tasks. The round-2 review's framing ("lean toward adding the two named seams since the `[2a §4.2] trap_throw` boundary is the actual contract under test") is recorded as guidance for /speckit-tasks; seam #12's `test_tls_error_variants.cpp` exercises the variant returns synthesise-side, and the dedicated cold-path (`test_file_cert_source_pmr_fail.cpp`) + warm-path (`test_pinset_add_pmr_fail.cpp`) seams are noted as candidates for the /speckit-tasks expansion (21 → 23 tests). Not a Gate-A blocker per the review's "/speckit-tasks decision" framing.
- **Opus round-2 NEW-P1-1 — plan.md variant-count internal disagreement (13 vs 16)**: addressed (project-structure tree comment changed `13 new error::tls_* variants` → `16 new error::tls_* variants` matching the line-71 narrative + tls_errors.hpp slots 78..93).
- **Opus round-2 NEW-P1-2 — spec.md /clarify Q4 bullet contradicts restored Pinset API**: addressed (spec.md:15 amended with `(Amended round 2)` marker; the /clarify Q4 decision answer (SHA-256-of-leaf-DER lookup) is preserved verbatim and the API ergonomics on top of that decision are clarified: `add` consumes a `Certificate`; `remove` and `find` take the bare 32-byte fingerprint; pin-rotation prose rewritten to `add(new_leaf_cert)` then `remove(sha256_of_old_leaf)`).
- **Opus round-2 NEW-P2-1 — `[const §XV.9]` rationale still re-derived in plan.md Constitution Check**: addressed (the (a)/(b)/(c) three-line summary at plan.md:62 dropped to a single cite-and-stop sentence per NEW-P2-6 close; the XI.3 row cell text at line 94 tightened to cite `[2g §6.5.2]` (consolidated rationale) instead of the bare `[2g §6.5]` per NEW-P3-3).
- **Opus round-2 NEW-P2-2 — `[arch §4.6]` cite for tls/ module layer position**: addressed (Project Type paragraph appended `[arch §4.6]` for the tls surface inventory; `architecture.md` line 270 hosts `### 4.6 tls`).
- **Opus round-2 NEW-P3-1 — `core::system_clock_source` is an "invented type name"**: NO-OP. The Opus review's NEW-P3-1 was based on the wrong premise that 007 ships `core::system_clock` (no `_source` suffix). Verification against `include/fixpp/core/system_clock_source.hpp:40` (`class system_clock_source final : public Clock`) confirms the type IS named `system_clock_source`. The review explicitly permitted the rewriter to verify against 007: "or whatever the actual 007-shipped name is — orchestrator can confirm at fix time". The quickstart.md:55 type name is correct as-is.
- **Opus round-2 P3-2 (minor doc nit)**: no-op (the round-2 review enumerates only NEW-P3-1 as the P3 content; P3-2 in the summary tally refers to the same finding's count).

### Round 2 — disagreements

- **Opus round-2 NEW-P3-1**: rewriter disagrees with the finding. The Opus review claimed `core::system_clock_source` is "invented" and that 007 ships `core::system_clock`. Verification against `include/fixpp/core/system_clock_source.hpp` line 40 + `src/core/system_clock_source.cpp` + 6 production call sites under `tests/core/` + `tests/alloc_guard/` confirms the type IS named `system_clock_source`. The review's text itself flagged the uncertainty ("or whatever 007 shipped; the rewriter must verify against the actual 007 / `include/fixpp/core/clock.hpp` surface"). Per the verification, no edit applied. Recorded as a Disagree+reason per the Gate A pipeline contract.
