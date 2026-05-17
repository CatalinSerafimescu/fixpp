<!-- SPECKIT START -->
**Active feature:** `004-wire-codec` — the `fixpp::wire` module: `Framer` / `Parser<Mode>` / `OffsetTable` / `Writer` / `Validator` + the `View` flyweight base (third Phase 4 feature; W-001..W-014 + OSS-006/008/013, the critical-path 2b unblocker).

For technologies, project structure, build/test commands, and gate status, read the current plan: [`specs/004-wire-codec/plan.md`](specs/004-wire-codec/plan.md). Design anchor: `.specify/2b-wire.md` **v0.2 (Gate A r1 converged)** — on conflict the design doc wins.

Three `/clarify` decisions, user-answered 2026-05-16: **Q1** cutover-in-004 — this PR removes the 003 frozen `wire::MessageView` stub, rewires 001 (FLOAT accessor) + 003 (`dict::reify` round-trip) onto the real surface, ships those 2b-gated tests GREEN. **Q2** full per-version default `dictionary_driven_validator` (exactly 5 pure-virtual; v42/v44/v50sp2/vt11; required+type+enum+group). **Q3** the `[arch §11 row 1]` eager/lazy offset-table footprint spike is an in-PR decision artifact (SC-008).

Companion artifacts in the same directory:
- [`spec.md`](specs/004-wire-codec/spec.md) — feature spec (anchored to `.specify/2b-wire.md` v0.2; carries /clarify Q1/Q2/Q3)
- [`research.md`](specs/004-wire-codec/research.md) — Phase 0 D-1..D-15 (D-9..D-12 = cross-doc confirmations @2c/2d/2e, NOT 004 blockers)
- [`data-model.md`](specs/004-wire-codec/data-model.md) — E1..E10 entities, three-arena lifetime model, 13 `wire_*` error slots 30–42, PMR accounting
- [`contracts/`](specs/004-wire-codec/contracts/) — `[2b §4]` shape oracles: `view`/`framer`/`parser`/`offset_table`/`writer`/`validator`/`group_view`/`unknown_fields`/`wire_errors`.hpp
- [`quickstart.md`](specs/004-wire-codec/quickstart.md) — build / test / fuzz / bench / footprint-spike / `/speckit-verify` / `/gate-a` / `/gate-b` / cutover sanity

Previous feature (merged): [`003-dictionary-codegen`](specs/003-dictionary-codegen/plan.md) — `fixpp-codegen` + per-version typed messages + `dict::reify` bridge. Gate A + Gate B converged; PR #67 on `main`. Earlier: [`002-dictionary-xml-loader`](specs/002-dictionary-xml-loader/plan.md) (PR #66), [`001-core-decimal`](specs/001-core-decimal/plan.md) (Gate A/B converged 2026-05-12/13).
<!-- SPECKIT END -->
