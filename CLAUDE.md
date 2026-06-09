<!-- SPECKIT START -->
**Active feature:** `028-validation-compat-toggles` — G3 validation-compat knobs (`CheckCompID` + `ValidateSequenceNumbers`), plan at `specs/028-validation-compat-toggles/plan.md` (Phase 1 done; Gate A next). Two additive default-`true` `SessionConfig` bools relaxing the steady-state inbound CompID match + sequence validation (QuickFIX-compat; steady-state-only, Logon establishment untouched). MaxLatency already covered by `sending_time_threshold` (NOT in this slice). Predecessor `027-next-expected-msgseqnum` MERGED 2026-06-07 (PR #108 squash `6aecf64`, gate-b-done). After 028: **025-refresh-on-logon** G3 capstone (parked on T034 inbound-store-persistence, [[project_025_refresh_on_logon_bundling_plan]]) → G4. Per [[project_release_interop_quickfix_fix8]].

**This file stays a thin pointer — shipped-feature history is tracked elsewhere, not duplicated here:**
- **Authoritative per-feature status + evidence:** `spec/feature-catalogue.md` (status / evidence_pr / tests per row) + `spec/coverage-index.md`.
- **Per-feature Gate A/B convergence + sign-off:** parent `research/G19-fix-fpml-iso20022/phases/phase-4/session/<feature>.md` (+ the `phases/phase-4.md` status dashboard / `phases/phase-4/cross-module-decisions.md`).
- **Behaviours & limitations (operator-facing):** `spec/behaviors-and-limitations.md` (B-* / L-* rows).
- **Cross-session state + lessons:** project memory `project_*_merged` / `feedback_*` notes (index in `MEMORY.md`).

**Live deferred work** (everything else in the old registry is DISCHARGED or tracked in the catalogue / B&L above):
- **015 down-peer cause #2** — a mid-connect `transport::async_connect` is not promptly cancelled by `cancellation_type::total` (~30 s teardown latency on a never-established initiator). Real, bounded, Gate-A-class concurrency change; tracked in `spec/behaviors-and-limitations.md` (L2). Cause #1 (busy-spin) fixed in 016.
- **Deferred session-recovery (catalogue row 400)** — upgrade 010 F4 cells "2"/"4" (ResendRequest / SequenceReset) from Reject→Process; largely shipped via 013 + S-023; residual = traceability-marker confirm at the v1.0 gate.
<!-- SPECKIT END -->
