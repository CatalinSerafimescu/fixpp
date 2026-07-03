<!-- SPECKIT START -->
**Active work: `058-async-mutex-hardening`** — Cluster-4 async_mutex hardening (AM-P1..P3 + test-validity gaps). Plan: `specs/058-async-mutex-hardening/plan.md` (spec/research/data-model/contracts alongside). Phase-0 verified (Opus+Codex concur); awaiting user `/plan` sign-off → Gate A (dual Fable+Codex, first Fable spend). See [[project_async_mutex_cluster4_hardening]]. The **C-ABI surface is GA-frozen at `1.5.0`**; the Python-bindings workstream (PY-001..005) is COMPLETE and all follow-ups CLOSED.

**This file is a THIN POINTER.** Per-feature history is NOT duplicated here — it lives in the authoritative trackers:
- **Per-feature status + evidence:** `spec/feature-catalogue.md` (status / evidence_pr / tests per row) + `spec/coverage-index.md`.
- **Per-feature Gate A/B convergence + sign-off:** parent `research/G19-fix-fpml-iso20022/phases/phase-4/<module>/<feature>.md` (+ `phases/phase-4.md` status dashboard / `phases/phase-4/cross-module-decisions.md`).
- **Behaviours & limitations (operator-facing):** `spec/behaviors-and-limitations.md` (B-* / L-* rows).
- **Cross-session state + lessons:** project memory `project_*` / `feedback_*` notes (index in `MEMORY.md`; older lifecycle close-outs in `MEMORY-archive.md`).

**Recently merged (newest first — full detail in the phase-4 lifecycle docs + memory):**
- **`057-behavioral-reify-unblock`** — PR #161 (squash `5a7a944`), 2026-07-02, gate-a/b-done, 0 waivers. Live `dict::reify()`/`reify_as<Msg>()` typed owning handles (app single/multi-char + FIXT-admin) via a build-tree dispatch-bridge; lifts R6 / L-003-1; NO type-erasure (`as<Msg>()` stays T059-deferred); zero new public/wire/error/C-ABI surface (NFR-003-8 held). `phase-4/dictionary/057-behavioral-reify-unblock.md`. See [[project_057_behavioral_reify_unblock]].
- **`056-python-wheel-packaging`** (PY-005) — PR #158 (`e9d3763`), 2026-07-01. One `cp310-abi3` wheel + Tier-1 `python-wheel` gate; closes the PY-001..005 arc. [[project_056_python_wheel_packaging_gatea]].
- **#160 `cabi-1.5-ga-freeze`** — PR #160 (`61edae6`). C-ABI MAJOR 0→1, MINOR preserved → **`1.5.0`** (the `0→1` GA freeze). [[project_cabi_ga_freeze_1_5_0]] / [[feedback_ga_major_bump_preserve_minor_downgrade_frame]].
- **#159 `python-bindings-hardening`** — PR #159 (`723da02`). 4th `python-bindings` UBSan Tier-1 leg (closes L-054-2) + `msg_get_string` non-UTF-8 decode witness.
- **Earlier:** 053–055 (Python PY-001..004) · 049–052 (C-ABI Features A–C + Python-readiness) · 046 (libc++ atomic_shared_ptr) · 035–045 (Fable F-f tail + config/transport). See `phases/phase-4.md` + `spec/feature-catalogue.md`.

**Live deferred work** (everything else is DISCHARGED or tracked in the catalogue / B&L above):
- **Deferred session-recovery (catalogue row 400)** — upgrade 010 F4 cells "2"/"4" (ResendRequest / SequenceReset) Reject→Process; largely shipped via 013 + S-023; residual = traceability-marker confirm at the v1.0 gate.
- **Next candidates** (value-ordered, per parent `REMAINING-WORK.md`): live-interop golden-capture (Tier-1 Item-1, incl. the 025 demo cell) + C-103 chunked-resend (Tier-2, P3) → **G4**. Per [[project_release_interop_quickfix_fix8]].
<!-- SPECKIT END -->
