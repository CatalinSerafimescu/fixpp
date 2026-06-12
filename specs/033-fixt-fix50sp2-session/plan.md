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
   `108` on **every** outbound Logon (initiator emit `session.cpp:752`; acceptor reply `session.cpp:~2023`)
   — each side advertises its own default (matches QFcpp `Session.cpp:674/701`). Optionally emit
   `Username(553)`/`Password(554)` when configured.
3. **Inbound Logon** (`interpret_logon`, `admin_messages.cpp:183-319`, a dict-free byte scanner): read
   `1137` (+ optional `553`/`554`); return them to the caller. When `BeginString==FIXT.1.1` and `1137`
   is absent → session-level `Reject(35=3)` `SessionRejectReason=RequiredTagMissing(1)`, no
   establishment (FR-004) — reusing the existing missing-`98` reject pattern (`session.cpp:2370+`).
4. **Negotiated version state + serviceability**: store the peer's declared `DefaultApplVerID` in
   strand-confined session state; resolve it to an `application_version` via
   `version_profile::resolve_application_version` (`version_profile.hpp:111`, wire→C++). Test
   serviceability against the engine-built `version_registry` (threaded to the Session — `EngineConfig::dictionaries`
   → `build_version_registry`, `engine_config.hpp:211`): `registry.get(resolved)` returning
   `dict_no_dictionary_for_application_version` (or an unparseable `1137`) ⇒ unserviceable ⇒
   `Reject(35=3, 371=1137, 373=ValueIsIncorrect(5))` then no Active (FR-004a — distinct from the
   missing-tag `RequiredTagMissing(1)`; research R2).
5. **Negotiated version exposure (NOT a new routing gate)**: the session delivers inbound application
   messages to `fromApp` as dict-free wire views **today, for every version** (`parse_and_dispatch_`,
   `session.cpp:238-265`; no `dict::reify` in `src/session/` — research.md R4). So FR-005 is realized by
   **recording + exposing** the negotiated `application_version` via a NEW
   `Session::negotiated_version_profile() const → dict::version_profile` accessor, reachable from a
   `fromApp` handler through the EXISTING `Engine::lookup(SessionId)→shared_ptr<Session>` spine
   (`engine.hpp:294`). Wherever an app message is reified (outside the session), the negotiated
   `version_profile` (`session=vt11`, `default_appl=`negotiated) is used. Admin frames keep the FIXT.1.1
   session layer. No session-layer reify/validation gate is added → app-message handling is byte-for-byte
   the FIX.4.x path (zero regression). Per-message `ApplVerID(1128)` is **tolerated, not routed** (S-026
   deferred, FR-010). SC-006's W5 asserts the accessor returns the per-cell version (New-1, discriminating).
6. **Credentials**: surface parsed `553`/`554` as a `logon_credentials` value to a NEW default-accept
   `CompIdAuthorizationPolicy::authorize_logon(asserted_compid, logon_credentials)` seam fired on the
   establishment path **independently of mTLS** — the existing `authorize(peer_identity, compid)` seam
   (`session.cpp:1849-1916`) is mTLS-gated and takes no credentials (research R6). **No new validation
   policy** (FR-008); FR-008a's future config-gated validation knob attaches to `authorize_logon`. Redact
   `554` in logs/transcripts/goldens via a shared tag-554 redactor (FR-011).
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
build/parse (cold establishment path) only. The inbound app-message steady-state path is unchanged —
the session delivers dict-free wire views to `fromApp` and never reifies (research R4); a profile is
constructed only when a downstream reify call-site reads `negotiated_version_profile()`. FIX.4.x path
untouched.
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
| **VI** Spec coverage | Flips **S-020** (FIXT half: backlog/deferred → done), lands **S-025** (`DefaultApplVerID`) and **S-022** (`Username`/`Password`) backlog → done; **S-026** stays deferred (tolerate-only, FR-010). `spec.md` carries a `## Normative References` section with the four exact catalogue refs: S-020 `[FIX-SL §4.2.1] The FIX session profile`, S-022 `[FIX-SL §4.3] Establishing a FIX connection`, S-025 `[FIX-SL §4.3.7] Specifying application version`, S-026 `[FIX-SL §5.3.5] Explicit application version per message` — per §VI.5 (added Gate A round 1; the earlier vague "FIXT.1.1 §5" wording dropped). Whether 553/554 + 1137 need their own catalogue rows (vs amending S-020/S-022/S-025) decided at /tasks Polish — see §VI delta below. | ✅ PASS (Normative References present) |
| **VII** Testing/TDD | RED-first witnesses per user story (research.md R8): W1 FIXT Logon round-trip (8=FIXT.1.1 + 1137 emitted/parsed both roles) ⇒ Active; W2 missing-1137 ⇒ Reject(35=3,373=1) no-establish; W3 unserviceable ApplVerID ⇒ refuse (FR-004a); W4 FIX.4.x byte-identical regression guard (no 1137 emitted, wire unchanged); W5 4.4-over-FIXT establishes (version-general, SC-006); W6 553/554 emit+parse+surface; W7 554 redaction in transcript/log. | ✅ planned |
| **VII.6** Interop | un-defer `HP-fixt11-fix50sp2-cells`; live 5.0SP2 + 4.4-over-FIXT cells both roles × QFcpp/QFJ; goldens banked; manifest flipped from `deferred:fixt-routing` (SC-004) | ✅ planned |
| **VII.7** Fuzz (parser-touching) | `interpret_logon` gains `case 1137:/553:/554:` scanner arms → parser-touching per §VII item 7 ("new parser-touching code without a fuzz harness is a Gate B blocker"). The inbound admin-parse path is already driven by `tests/fuzz/fuzz_session_recovery_admin_parse.cpp`, so this is a **seed/corpus extension** (the 027 T026 pattern), NOT a new harness: add FIXT Logon variants (`1137` present/missing/malformed + optional `553`/`554`). Tracked as tasks.md **T036**; verify via `/speckit-verify` fuzz smoke ≥10 min. *(Added analyze D1 — the row was omitted in the round-1 plan.)* | ✅ planned (T036) |
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
include/fixpp/session/session_config.hpp   # NEW: default_appl_ver_id = std::optional<dict::application_version> (+ optional credential config); FIXT predicate
include/fixpp/dict/version_profile.hpp     # NEW: inverse render helper application_version → wire 1137 string (ABSENT today; res. R3 / data-model E3)
include/fixpp/session/compid_authorization_policy.hpp  # NEW: authorize_logon(asserted_compid, logon_credentials) default-accept seam + logon_credentials value (redacting password) — res. R6 / FR-008/008a
src/session/admin_messages.cpp             # build_logon: emit 1137 (via render helper) (+553/554) after 108; interpret_logon: read 1137/553/554 + return struct
include/fixpp/session/admin_messages.hpp   # interpret_logon return-struct extension (heartbt + appl_ver_id + creds)
src/session/session.cpp                    # initiator Logon emit (:752) + acceptor reply (:~2023) thread app version; inbound arm: store negotiated version, missing-1137 → Reject 373=1 (mirror :2370+), unserviceable-version → Reject 371=1137/373=5, surface creds to authorize_logon (independent of the :1849+ mTLS-gated authorize)
include/fixpp/session/session.hpp          # NEW: strand-confined negotiated-application-version member + version_registry const& handle; NEW negotiated_version_profile() const accessor
include/fixpp/dict/version_registry.hpp / engine.hpp  # thread the engine-built version_registry (EngineConfig::dictionaries → build_version_registry, engine_config.hpp:211) to each Session (engine-lifetime ref) — res. R2
tests/session/test_fixt_logon_establishment.cpp   # NEW: W1/W2/W3/W4/W5 (build/parse; W2 missing→373=1; W3 unserviceable→371=1137/373=5 frame; W4 4.4 byte-identical; W5 negotiated_version_profile().default_appl per version)
tests/session/test_fixt_credentials.cpp           # NEW: W6/W7 (553/554 emit+parse+surface via authorize_logon; 554 redaction)
tests/interop/happy/hp_fixt_fix50sp2_test.cpp     # NEW: live cells (5.0SP2 + 4.4-over-FIXT, both roles) — confirm exact path at /tasks
phase-9-harness/...                        # register the 8 cells (HP-fixt50sp2-{qfcpp,qfj}-{init,acc} + HP-fixt44-{qfcpp,qfj}-{init,acc}); counterparty FIXT11.xml transport + FIX50SP2.xml/FIX44.xml app, DefaultApplVerID=9/6; tools/run_interop_cell.py shared tag-554 redactor; flip manifest off deferred:fixt-routing
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

- Round 1 applied 2026-06-12: Codex P1=3 P2=5 P3=1; Opus post-judging P1=3 P2=6 P3=2; rewrite addresses root causes RC1 (Normative References + false-attestation), RC2 (pin exposure/serviceability/credential/type mechanisms to existing surfaces), RC3 (name redaction + live-cell sites). Reviews: research/reviews/codex_033-fixt-fix50sp2-session_gate_a_review.md, research/reviews/opus_033-fixt-fix50sp2-session_gate_a_adversarial_review.md.
- Round 2 applied 2026-06-12: Codex P1=0 P2=2 P3=1; Opus post-judging P1=0 P2=2 P3=1; rewrite addresses F1 (scope the 1128-no-switch guarantee to the dict-free session layer — has_per_message_override gates nothing in reify, so the fix is doc-only, NOT a flag flip or new reify mode), F2 (narrow C2 to build_logon establishment frames), F3 (grep-sweep 2 stale "existing authorization path" → authorize_logon). Reviews: research/reviews/codex_033-fixt-fix50sp2-session_gate_a_2_review.md, research/reviews/opus_033-fixt-fix50sp2-session_gate_a_2_adversarial_review.md.

### Round 2 — disagreements

- **F1 remedy correction (Codex's *first* suggested fix was a no-op — NOT applied literally).** Codex's diagnosis (exposed `has_per_message_override=true` vs the "1128 never switches" guarantee) is upheld, but its literal remedy "set `has_per_message_override=false`" was rejected: source verification (`src/dictionary/version_profile.cpp::resolve_application_version` + `src/dictionary/reify.cpp:215-223`) confirms the flag is read by **zero** resolution lines — `1128`/`1137` is honored whenever present regardless of the flag. Flipping it gates nothing. The applied fix is doc-only: the no-switch guarantee (FR-010 / INV-FIXT-3 / C9) is re-scoped to the **session layer** (which delivers app messages dict-free and never reifies → never selects a dictionary, so never switches one); the per-message-override claim was dropped from the exposed-profile shorthand and the `has_per_message_override` field re-described as a documentation-only descriptor not consulted by resolution. Preserves data-model E1 "reused unchanged"; the HEAVY default-only-reify path (new dict surface, contradicts E1) was explicitly rejected.
- **F3 scope (sweep needle).** Only `spec.md:22` and `spec.md:137` carried the stale "existing authorization path" phrasing. The three remaining `authorize(peer_identity, compid)` references (`spec.md:28`, FR-008 at `spec.md:104`, `plan.md:58`) are load-bearing **contrast** refs (they name the existing mTLS-gated seam to justify the NEW `authorize_logon`) and were intentionally left untouched. Final sweep needle = the literal stale phrase "authorization path", not "authoriz".

### Round 1 — disagreements

- No Codex finding was overturned (no `Disagree`). Codex P2#6 (`default_appl_ver_id` type/render) was a factual **correction**, not a disagreement: Codex's two examples were backwards. The wire `ApplVerID` values for `v44`→`"6"` and `v50sp2`→`"9"` actually **coincide** with intuition and do NOT prove the divergence; the real divergences are enum `v40`→wire `"2"` and enum `v50`→wire `"7"`. The conclusion (pin the type as `std::optional<dict::application_version>` + add the absent inverse render helper) is adopted with the corrected examples (research R3 / data-model E3): emit tests use `v50sp2`→`1137=9`, `v44`→`1137=6`, `v50`→`1137=7` (divergent, the discriminating case), invalid/`Unknown`→fail before Logon.
- Opus New-2 (P3, NOT a defect): BeginString length-agnostic emit, distinct SessionId keying per family, and the permissive `interpret_logon default: break` scanner all HOLD for FIXT — recorded as confirmed-safe in research R9; NOT over-corrected.
