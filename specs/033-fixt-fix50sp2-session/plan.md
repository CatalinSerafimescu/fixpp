# Implementation Plan: FIXT.1.1 / FIX 5.0 SP2 Session Establishment

**Branch**: `033-fixt-fix50sp2-session` | **Date**: 2026-06-12 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/033-fixt-fix50sp2-session/spec.md`

## Summary

Enable fixpp to establish a **FIXT.1.1** transport session carrying a separately-negotiated
application version (primarily **FIX.5.0 SP2**, but the mechanism is version-general per /clarify),
decoupling the wire `BeginString(8)` (transport) from the application dictionary (`DefaultApplVerID(1137)`).
This un-defers the FIXT half of catalogue **S-020**, lands **S-025** (`DefaultApplVerID`) and **S-022**
(`Username`/`Password`), and flips the `deferred:fixt-routing` interop axis to live.

**Key enabling discovery (Phase-0 seam map):** the **dictionary/version layer already models the
FIXT split** — `version_profile` (`include/fixpp/dict/version_profile.hpp:71-79`) carries
`{session_version session, application_version default_appl, bool has_per_message_override}`;
`dict::reify` (`include/fixpp/dict/reify.hpp:111-126`) already dual-dispatches FIXT-admin
(35=A/0/1/2/3/4/5) vs application (ApplVerID(1128) or `profile.default_appl`); `version_registry`
(`include/fixpp/dict/version_registry.hpp:22-66`) already maps `application_version → Dictionary const*`;
the XML data ships (`dictionaries/FIXT11.xml` + `FIX50SP2.xml`), and `xml_loader` already loads the
`FIXT` type as `session_version::vt11` (`src/dictionary/xml_loader.cpp:148`). **No new
ApplVerID→dictionary mapping is invented** — the work is in the **session layer**: thread the
negotiated application version through Logon build/parse and wire the profile into resolution.

**Technical approach (grounded in both reference engines — research.md R1):**
1. **Config**: add `SessionConfig::default_appl_ver_id` (optional). When set together with
   `begin_string == "FIXT.1.1"`, the session is a FIXT session; when unset, FIX.4.x behaviour is
   byte-identical (FR-009/SC-002).
2. **Outbound Logon** (`build_logon`, `admin_messages.cpp:76-179`): emit `DefaultApplVerID(1137)` after
   `108` on **every** outbound Logon (initiator emit `session.cpp:752`; acceptor reply `session.cpp:~1937`)
   — each side advertises its own default (matches QFcpp `Session.cpp:674/701`). Optionally emit
   `Username(553)`/`Password(554)` when configured.
3. **Inbound Logon** (`interpret_logon`, `admin_messages.cpp:183-319`, a dict-free byte scanner): read
   `1137` (+ optional `553`/`554`); return them to the caller. When `BeginString==FIXT.1.1` and `1137`
   is absent → session-level `Reject(35=3)` `SessionRejectReason=RequiredTagMissing(1)`, no
   establishment (FR-004) — reusing the existing missing-`98` reject pattern (`session.cpp:2370+`).
4. **Negotiated version state**: store the peer's declared `DefaultApplVerID` in strand-confined session
   state; resolve it to an `application_version` via `version_profile::resolve_application_version`
   (`version_profile.hpp:111`). If unserviceable (no dictionary for that ApplVerID) → refuse
   establishment (FR-004a).
5. **Negotiated version exposure (NOT a new routing gate)**: the session delivers inbound application
   messages to `fromApp` as dict-free wire views **today, for every version** (`parse_and_dispatch_`,
   `session.cpp:238-265`; no `dict::reify` in `src/session/` — research.md R4). So FR-005 is realized by
   **recording + exposing** the negotiated `application_version`; wherever an app message is reified
   (outside the session), the negotiated `version_profile` (`session=vt11`, `default_appl=`negotiated) is
   used. Admin frames keep the FIXT.1.1 session layer. No session-layer reify/validation gate is added →
   app-message handling is byte-for-byte the FIX.4.x path (zero regression). Per-message `ApplVerID(1128)`
   is **tolerated, not routed** (S-026 deferred, FR-010).
6. **Credentials**: surface parsed `553`/`554` to the existing CompID/authz seam (`session.cpp:1849-1916`);
   **no new validation policy** (FR-008); leave the seam ready for a config-gated future validation
   feature (FR-008a). Redact `554` in logs/transcripts/goldens (FR-011).
7. **Interop**: un-defer `HP-fixt11-fix50sp2-cells`; add a FIX.5.0SP2 cell family and a representative
   FIXT.1.1-carrying-FIX.4.4 cell family, both roles × both engines (SC-004/SC-006).

## Technical Context

**Language/Version**: C++23 (Clang; asio awaitables, `std::expected`) — [const §II]
**Primary Dependencies**: existing dict/version layer (`version_profile`/`version_registry`/`reify`/
`xml_loader`) + the 005 session-establishment path (`build_logon`/`interpret_logon` in
`src/session/admin_messages.cpp`; the Logon emit/parse arms in `src/session/session.cpp`). No new
third-party deps.
**Storage**: `MessageStore` interface UNCHANGED (no seqnum/persistence change; FIXT is orthogonal to the
029/025 store spine). The negotiated application version is in-memory session state, not persisted.
**Testing**: GoogleTest; ASan/UBSan/TSan; coverage llvm-cov. New unit suites for FIXT Logon
build/parse (1137/553/554), the missing-1137 reject, version-resolution + unserviceable-version refuse,
the FIX.4.x byte-identical regression guard, and 554 redaction. New live interop cells (5.0SP2 + 4.4-over-
FIXT, both roles, QFcpp + QFJ) in the parent `phase-9-harness`. — [const §VII, §VII.6, §IX]
**Target Platform**: Linux/Clang Tier-1 (sanitizer matrix); live cells run vs QFcpp v1.16.0 / QFJ 3.0.1
in the parent `phase-9-harness`.
**Project Type**: single C++ library (`fixpp`) + tests + interop-harness extension.
**Performance Goals**: no hot-path regression; FIXT path adds a few optional fields to Logon
build/parse (cold establishment path) and one profile construction per inbound app message routed
through the existing resolver. FIX.4.x path untouched.
**Constraints**: `noexcept`/`expected_t` preserved; FIX.4.x wire output byte-identical when FIXT not
configured (SC-002); `Password(554)` never emitted clear-text into any persisted artifact (FR-011); no
new include into the `session.hpp` awaitable closure ([const §XV.9], confirm at verify).
**Scale/Scope**: medium feature spanning config + admin-message build/parse + session establishment
arms + interop harness. Net-new public surface: one `SessionConfig` field + optional Logon wire fields
(1137/553/554) emitted for FIXT sessions only. NO new error slot (reuses RequiredTagMissing=1), NO
codegen change (dictionaries already ship), NO new C-ABI export, NO new MessageStore surface.

## Constitution Check

*GATE: must pass before Phase 0 (passed) and re-checked after Phase 1.*

| Article | Gate | Status |
|---------|------|--------|
| **II** Language | C++23/Clang, no new deps | ✅ PASS |
| **VI** Spec coverage | Flips **S-020** (FIXT half: backlog/deferred → done), lands **S-025** (`DefaultApplVerID`) and **S-022** (`Username`/`Password`) backlog → done; **S-026** stays deferred (tolerate-only, FR-010). `spec.md` carries Normative References (`[FIX-SL §4.2.1/§4.3.7/§4.3]`, FIXT.1.1 §5) per §VI.5. Whether 553/554 + 1137 need their own catalogue rows (vs amending S-020/S-022/S-025) decided at /tasks Polish — see §VI delta below. | ⚠ RESOLVED (delta specified) |
| **VII** Testing/TDD | RED-first witnesses per user story (research.md R8): W1 FIXT Logon round-trip (8=FIXT.1.1 + 1137 emitted/parsed both roles) ⇒ Active; W2 missing-1137 ⇒ Reject(35=3,373=1) no-establish; W3 unserviceable ApplVerID ⇒ refuse (FR-004a); W4 FIX.4.x byte-identical regression guard (no 1137 emitted, wire unchanged); W5 4.4-over-FIXT establishes (version-general, SC-006); W6 553/554 emit+parse+surface; W7 554 redaction in transcript/log. | ✅ planned |
| **VII.6** Interop | un-defer `HP-fixt11-fix50sp2-cells`; live 5.0SP2 + 4.4-over-FIXT cells both roles × QFcpp/QFJ; goldens banked; manifest flipped from `deferred:fixt-routing` (SC-004) | ✅ planned |
| **VIII.5** Allocator | Logon build appends a few optional fields (existing builder arena); inbound parse is the existing dict-free scanner; profile construction is a 4-byte value. Confirm no new heap on the establishment path; reuse existing no-alloc witnesses. | ⚠ confirm at verify |
| **IX.1** Coverage | ≥95/85 on the new branches: 1137 emit (FIXT vs FIX.4.x), 1137 parse + missing→reject, version resolve + unserviceable→refuse, 553/554 emit/parse, redaction | ✅ planned |
| **IX.2** Sanitizers | ASan/UBSan/TSan on the FIXT establishment unit suites + interop ctest | ✅ planned |
| **X** ABI | NEW public surface: `SessionConfig::default_appl_ver_id` (+ optional credential config fields) and NEW optional Logon wire fields (1137/553/554) emitted **for FIXT sessions only** — additive; FIX.4.x ABI + wire byte-identical (FR-009). NO new error slot (reuses 373=1), NO codegen, NO new C-ABI export, NO MessageStore change. Header changes → source rebuild. | ✅ additive (FIX.4.x byte-identical) |
| **XI.4** Threading | negotiated-version state is strand-confined session state set in the inbound Logon handler; no new concurrency surface | ✅ PASS |
| **XIV.2** Pluggable ≤5 pure-virtual | `MessageStore` untouched; the `Application` callback interface unchanged (app messages still delivered via existing fromApp; this feature selects the dictionary, not the callback shape) | ✅ PASS |
| **XV.9** Banned (`std::mutex` in awaitable hdr) | no new mutex; confirm no new include drags one into the awaitable corpus at verify | ⚠ N/A (confirm at verify) |
| **XVI.3/4** /clarify before /plan | Session 2026-06-12 — 5 decisions recorded (S-026 deferred; missing-1137→Reject(35=3); any-ApplVerID general design; live = 5.0SP2 + 4.4-over-FIXT; creds parse+surface only + future validation knob). Reference-engine grounded (QFcpp `Session.cpp` reject + dual-advertise). | ✅ PASS |
| **XVII.1** Gate A before /tasks | Gate A runs after this plan, before `/speckit-tasks` — recorded in `## Gate A` below. | ⏳ pending |

**Result**: PASS to proceed to Phase 0/1. Additive public surface (one config field + FIXT-only optional
wire fields); FIX.4.x wire/ABI byte-identical (SC-002/FR-009); reuses the existing version layer +
reject path; no new error slot/codegen/C-ABI/store surface. Two items carry confirm-at-verify flags
(VIII.5 alloc, XV.9 include-edge) and one design crux is resolved in research.md R2 (session↔registry
wiring).

**Exact §VI delta (applied at Polish, ratified at Gate A):**
- `spec/feature-catalogue.md`: flip **S-020** Notes (FIXT.1.1/5.0SP2 half no longer deferred — cite 033);
  flip **S-025** (`DefaultApplVerID(1137)`) backlog → done (033); flip **S-022** (`Username`/`Password`)
  backlog → done (033). Confirm whether 553/554 and 1137 warrant distinct rows or amendments at /tasks.
  **S-026** (`ApplVerID(1128)` per-message) stays `backlog`/deferred with a 033 note: inbound 1128
  tolerated, per-message routing a follow-on.
- `spec/coverage-index.md`: add FIXT establishment entries mapping FR-001..FR-012 ↔ the new unit suites
  + the live cells.
- `spec/behaviors-and-limitations.md`: add B-033-* (FIXT.1.1/5.0SP2 establishment; transport/app
  decoupling) and an L-033-* for the deferred per-message 1128 routing + the future credential-validation
  knob (FR-008a forward obligation).

## Project Structure

### Documentation (this feature)

```text
specs/033-fixt-fix50sp2-session/
├── plan.md              # this file
├── spec.md              # /speckit-specify + /speckit-clarify output
├── research.md          # Phase 0 — reference-engine oracle + version-layer wiring decisions
├── data-model.md        # Phase 1 — version_profile / negotiated-version state / FIXT Logon entities
├── quickstart.md        # Phase 1 — how to configure + validate (units + live cells)
├── contracts/
│   └── fixt-logon-establishment.md   # FIXT Logon build/parse + reject + version-resolution contract
├── checklists/
│   └── requirements.md  # spec-quality checklist (GREEN)
└── tasks.md             # Phase 2 — /speckit-tasks output (NOT created here)
```

### Source Code (repository root = library submodule)

```text
include/fixpp/session/session_config.hpp   # NEW: default_appl_ver_id (+ optional credential config); FIXT predicate
src/session/admin_messages.cpp             # build_logon: emit 1137 (+553/554) after 108; interpret_logon: read 1137/553/554 + return struct
include/fixpp/session/admin_messages.hpp   # interpret_logon return-struct extension (heartbt + appl_ver_id + creds)
src/session/session.cpp                    # initiator Logon emit (:752) + acceptor reply (:~1937) thread app version; inbound arm: store negotiated version, missing-1137 reject (mirror :2370+), unserviceable-version refuse, surface creds to authz (:1849+), build app version_profile for routing
include/fixpp/session/session.hpp          # NEW: strand-confined negotiated-application-version member
src/dictionary/...                         # (likely no change — dictionaries ship; confirm registry wiring per research R2)
tests/session/test_fixt_logon_establishment.cpp   # NEW: W1/W2/W3/W4/W5 (build/parse/reject/refuse/4.4-over-FIXT/regression)
tests/session/test_fixt_credentials.cpp           # NEW: W6/W7 (553/554 emit+parse+surface; 554 redaction)
tests/interop/happy/hp_fixt_fix50sp2_test.cpp     # NEW: live cells (5.0SP2 + 4.4-over-FIXT, both roles) — confirm exact path at /tasks
phase-9-harness/...                        # un-defer + register FIXT cells; counterparty FIXT/5.0SP2 + FIXT/4.4 configs
```

**Structure Decision**: extend the existing 005 establishment path + the shipped version layer in place;
one new SessionConfig field + additive FIXT-only Logon fields. New unit suites for FIXT establishment +
credentials; new interop cell family. No new modules beyond test files.

## Complexity Tracking

> No constitution violations requiring justification. The feature is broad but additive: it leans on the
> already-FIXT-aware dictionary/version layer (the costly part is pre-built) and confines new behaviour to
> the establishment path behind a config gate, keeping FIX.4.x byte-identical. The one genuine design
> crux — how the session resolves the negotiated application dictionary (session↔`version_registry`
> wiring) — is resolved in research.md R2 and confirmed at early implement before the broad test build.

## Gate A

*(Runs after this plan, before `/speckit-tasks` — [const §XVII.1]. Record convergence + sign-off here.)*

- _Pending._
