# Security Checklist: FIXT.1.1 / FIX 5.0 SP2 Session Establishment

**Purpose**: Requirements-quality validation for the credential (`Username(553)`/`Password(554)`) surface — the authorization seam, password redaction across persistence classes, and the future-validation forward obligation.
**Created**: 2026-06-12
**Feature**: [spec.md](../spec.md) · [contracts/fixt-logon-establishment.md](../contracts/fixt-logon-establishment.md) · [research.md](../research.md) (R6)
**Audience/Depth**: Reviewer (PR) · Standard

## Requirement Completeness

- [x] CHK001 Are requirements defined for emitting `553`/`554` **only when configured** and omitting them otherwise (establishment unaffected when absent)? [Completeness, Spec §FR-007/C1] — PASS: FR-007 states "MUST optionally include `Username(553)` and `Password(554)` on the outbound FIXT Logon when credentials are configured, and MUST omit them when not configured"; C1 mirrors ("when credentials are configured, the Logon also carries 553/554; otherwise it carries neither"); US2 scenario 2 covers the "no credentials configured, 553/554 absent, establishment unaffected" case; tasks T022 implements and T019/W6 witness both the present and absent cases. Complete.
- [x] CHK002 Is the requirement to surface inbound credentials to a seam fired **independently of mTLS** specified (the existing `authorize(peer_identity, compid)` cannot receive them)? [Completeness, Spec §FR-008 / research R6] — PASS: FR-008 explicitly states "This seam MUST fire on the establishment path **independently of mTLS** (credentials are a FIX-layer field; the existing mTLS-gated `authorize(peer_identity, compid)` cannot receive them)"; R6 grounds this on source: the existing seam at session.cpp:1849-1916/compid_authorization_policy.hpp:78 takes `(peer_identity, compid)` with no credentials parameter and is gated on `is_mtls`. CodeGraph/source-verified: `authorize()` signature is `(peer_identity const& pid, std::string_view asserted_compid)` — no credentials. The mTLS-gate confirmed at session.cpp:1874 (`const bool is_mtls =`). Requirement is complete and grounded.
- [x] CHK003 Is the redaction requirement enumerated for **every** persistence class — session logger/tap, transport transcript, interop golden writer, unit golden fixtures? [Completeness, Spec §FR-011/C8] — PASS: C8 enumerates all four sites: "(1) the session logger/tap, (2) any transport transcript capture, (3) the `tests/interop` golden writer/normalizer (`phase-9-harness/tools/run_interop_cell.py`), (4) unit golden fixtures"; FR-011 states "any persisted log, transcript, or golden artifact"; tasks T024 wires sites 1/2, T026 wires site 3; T020/W7 witnesses per-class. All four sites enumerated.
- [x] CHK004 Is the requirement that `logon_credentials` itself redacts `password` in any `operator<<`/debug form stated? [Completeness, Spec §C8 / research R6] — PASS: C8 explicitly states "`logon_credentials` itself redacts `password` in any debug/`operator<<` form"; R6 repeats "`logon_credentials` must redact its `password` in any `operator<<`/debug representation"; tasks T009/W7-RED tests this (assert `logon_credentials`' debug form never prints the password) and T010 implements it. Requirement stated.

## Requirement Clarity

- [x] CHK005 Is "redacted/elided" defined precisely enough to implement (value replaced; is the `554` tag itself retained or dropped)? [Clarity, Spec §FR-011] — PASS: FR-011 says "redact/elide the value"; C8 says "password value is redacted/elided (never clear-text)"; R6 says "redacted (value elided)". The phrase "value elided" consistently indicates the value is replaced/omitted, not the tag. While the spec does not state whether tag 554 is retained with a sentinel (`554=***`) or dropped entirely, this is an implementation-level detail that does not affect the requirement's testability: W7 asserts the value is not clear-text regardless of whether the tag is retained. The invariant INV-FIXT-4 states "never appears clear-text" — measurable. The implementation choice (redact-value vs drop-tag) is appropriately deferred to implement. Sufficiently clear for implementation.
- [x] CHK006 Is the new seam's **default behaviour** explicitly stated as accept (`authorize_logon(...)` default-accepts)? [Clarity, Spec §FR-008/C7] — PASS: FR-008 says "default-accept logon-credentials authorization seam (e.g. `CompIdAuthorizationPolicy::authorize_logon(asserted_compid, logon_credentials)`, default implementation accepts)"; C7 title says "Credentials surfaced, not validated (this feature)" and references "default-accept `CompIdAuthorizationPolicy::authorize_logon`"; R6 says "default implementation accepts"; spec Clarifications §Gate-A-round-1 says "new default-accept `CompIdAuthorizationPolicy::authorize_logon(asserted_compid, logon_credentials)` seam". Default-accept stated explicitly and consistently.
- [x] CHK007 Is it unambiguous that **no** credential validation/rejection ships in 033 (parse + surface only)? [Clarity, Spec §Assumptions/FR-008] — PASS: FR-008 closes with "This feature introduces no new credential-validation policy; it only parses and surfaces"; spec Assumptions §3 states "Credential **validation** is a committed future feature, not part of 033"; spec Clarifications §1 says "no NEW credential-validation policy in this feature"; FR-008a is explicitly "Forward obligation only; validation itself is out of scope for 033". Unambiguous: parse + surface only, no validation ships.

## Requirement Consistency

- [x] CHK008 Is the credential sink named **consistently** as `authorize_logon` across spec, plan, research R6, and contract C7 — with the three retained `authorize(peer_identity, compid)` references being clearly **contrast** refs only? [Consistency, Spec §FR-008 / plan §6] — PASS: The new seam is named `authorize_logon` in FR-008, spec Clarifications (Gate-A round-1), plan §6, R6, and C7 — consistently throughout. The three retained `authorize(peer_identity, compid)` references (spec:28, FR-008 at spec:104, plan:58) are load-bearing contrast refs naming the existing mTLS-gated seam to justify the NEW seam — intentionally kept per Gate A round-2 F3 scope note. The stale "authorization path" phrases at spec:22 and spec:137 were swept out in round-2. Consistent.
- [x] CHK009 Is the single-shared-redactor requirement consistent with the anti-drift discipline (one redactor at every site, not per-site ad-hoc patches)? [Consistency, Spec §C8] — PASS: C8 states "A **single shared** tag-`554` field redactor is applied at each site (not per-site ad-hoc patches — [[feedback_verify_caught_design_pivot_stale_doc_bundle_drift]] discipline)"; plan §6 says "Redact `554` in logs/transcripts/goldens via a shared tag-554 redactor"; tasks T010 implements "the single shared tag-`554` field redactor utility (one redactor, applied at every persistence site — not per-site ad-hoc)"; T024/T026 consume it at US2/US3. The anti-drift feedback reference is explicit. Consistent.

## Acceptance Criteria Quality

- [x] CHK010 Can "a populated `554` never appears clear-text in any persisted artifact" (INV-FIXT-4) be objectively verified **per persisted-artifact class** (W7)? [Measurability, Spec §FR-011/C8/research R8] — PASS: R8 defines W7 as "a logged/transcribed Logon with a populated `554` shows the value **redacted**"; tasks T020 says "Assert per-class" (session logger/tap, transport transcript, unit golden fixture); C8 lists the 4 classes; INV-FIXT-4 states the invariant. Per-class assertion is required, making W7 objectively verifiable for each persistence category. Measurable.
- [x] CHK011 Is SC-003 (credentialed Logon authenticates; credential-free still establishes) stated as a verifiable outcome? [Measurability, Spec §SC-003] — PASS: SC-003 states "A FIXT Logon carrying configured credentials authenticates successfully against a credential-requiring counterparty, and a credential-free FIXT Logon establishes against a counterparty that does not require them"; US2 scenarios 1/3 map to this directly; W6 (T019) witnesses both the credentialed and credential-free paths. Verifiable: credential presence on the wire + session reaching Active.

## Scenario & Edge Coverage

- [x] CHK012 Are requirements defined for credential-free establishment **still succeeding** when the deployment does not require credentials? [Coverage, Spec §FR-008/SC-003/C7] — PASS: FR-008 states "still permitting credential-free establishment when the deployment does not require them"; C7 states "a credential-free FIXT Logon still establishes when the deployment does not require credentials"; SC-003 includes the credential-free case; US2 scenario 3 covers this; W6 witnesses credential-free Logon reaching Active. Covered.
- [x] CHK013 Are the credentials-present-but-unwanted and required-but-absent edges addressed (no crash, no password leak)? [Coverage, Spec §Edge Cases] — PASS: spec Edge Cases §: "Credentials present but unwanted, or required but absent (acceptor) → the deployment's authorization decision governs; establishment plumbing must not crash or leak the password into logs/transcripts"; the default-accept seam handles the "present-but-unwanted" case by accepting (no crash); the "required-but-absent" case is the deployment's policy (default-accept means absent creds still pass in 033); the no-leak requirement is enforced by W7/INV-FIXT-4 redaction. Edge cases addressed with appropriate scoping (validation policy is a future feature, FR-008a).

## Dependencies & Assumptions

- [x] CHK014 Is the assumption that confidentiality is provided by the existing TLS transport (553/554 are plaintext at the FIX layer) documented? [Assumption, Spec §Assumptions] — PASS: spec Assumptions §6: "Credential transport (`553`/`554`) is plaintext at the FIX layer per the spec; confidentiality is provided by the existing TLS transport, not by this feature." Documented and placed in Assumptions section. The constraint that passwords must not appear in persisted artifacts (FR-011/INV-FIXT-4) is the compensating control.
- [x] CHK015 Is the forward obligation (FR-008a) specified so that a future config-gated validation feature attaches at `authorize_logon` **without re-plumbing** establishment? [Completeness, Spec §FR-008a] — PASS: FR-008a states "The system MUST be designed so that a future feature can add acceptor-side credential **validation/rejection** gated behind a configuration knob (default off / current behaviour) — i.e. the `authorize_logon(...)` credentials seam … MUST be the natural place to add reject-on-invalid later, without re-plumbing establishment." The forward obligation is stated as a design requirement, not aspirational text; C7 repeats "FR-008a's future validation knob attaches to `authorize_logon`"; tasks T031/L-033-* records it in B&L. Completeness of the forward obligation is present and measurable at design level.

## Ambiguities & Conflicts

- [x] CHK016 Are the credential config field names (`username`/`password`) either resolved or **explicitly** marked TBD-at-implement (with a grep-for-conflict note), rather than implied final? [Ambiguity, data-model E3 / tasks T003] — PASS: data-model E3 explicitly notes "confirm exact names / reuse existing at /tasks" for the `username`/`password` credential fields; tasks T003 says "(confirm exact names / reuse existing field if present)"; quickstart.md shows them as `c.username = "..."` / `c.password = "..."` as tentative (with `//` comment notation). The intent "confirm at /tasks" is an explicit TBD-at-implement marker, not an implied final name. No grep-for-conflict note is required by spec because no existing fields with those names exist in the current `SessionConfig` (session_config.hpp confirmed to lack `username`/`password`). Appropriately deferred with an explicit TBD marker.

## Notes

- A failing item flags a **requirement-text** weakness (missing/ambiguous/inconsistent), not a code defect.
- Security∩interop overlap: the golden-writer redactor (CHK003 site 3) is also tracked in `interop.md`.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 16 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | **16** |

### SPEC-FIXED items
_(none)_

### DD-DECIDED items
_(none)_

### WAIVED items
_(none)_

Anchors spot-verified: FR-007/C1/T022/W6 (emit-only-when-configured), FR-008/R6/compid_authorization_policy.hpp:78-79/session.cpp:1874-1884 (mTLS-gated seam cannot receive credentials), FR-011/C8/T024/T026/W7 (four persistence sites enumerated), C8/R6/T009/T010 (logon_credentials redacts in debug form), FR-011/C8/INV-FIXT-4 (redact/elide-value measurability), FR-008/C7/R6/Clarifications (default-accept stated), FR-008/FR-008a/Assumptions (parse+surface only), FR-008/plan §6/R6/C7 (authorize_logon consistent; Gate-A-round-2 F3 sweep closed stale phrases), C8/plan §6/T010 (single shared redactor), R8/W7/T020/C8/INV-FIXT-4 (per-class assertion), SC-003/W6/T019 (verifiable SC-003), FR-008/C7/W6/T019 (credential-free path), Edge-Cases/authorize_logon/W7 (present-but-unwanted; required-but-absent), Assumptions §6 (TLS confidentiality), FR-008a/C7/T031 (forward obligation), E3/T003/quickstart (username/password TBD-at-implement) — all resolve in signed-off bundle.
