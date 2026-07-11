# Phase 0 Research: v44 all-families typed codegen coverage

All items resolved from the measure-first spike (`../../remaining-work/v44-all-families-measure-spike.md`) + direct source inspection of the 067 emitter. No open NEEDS CLARIFICATION.

## R1 — Message selection: allowlist → coverage-mode predicate

**Decision**: Replace the hardcoded `kOfficial33` allowlist in `emit_builders.cpp` with a coverage-mode predicate:
- `official` mode: keep the `kOfficial33` allowlist → **byte-identical** to today's output.
- `all` mode (default): emit for a message iff it is `msgcat='app'` **and** not in the N-002/N-003 exclusion set (`BE, BF, BW, BX, BY`).

**Rationale**: `msgcat` is authoritative in the dictionary (`<message … msgcat='app'|'admin'>`), so the 7 session/admin types (Heartbeat, TestRequest, ResendRequest, Reject, SequenceReset, Logout, Logon) are auto-excluded — no separate session list. Only N-002/N-003 (which are `msgcat='app'`) need an explicit small exclusion set. This keeps the selection dictionary-derived, not a hand-maintained ~81-element list that would go stale.

**Alternatives considered**: A second hardcoded ~81 allowlist (rejected — duplicates dictionary knowledge, re-creates drift risk the completeness pin guards). Category-by-MsgType heuristic (rejected — `msgcat` already encodes it).

## R2 — msgcat must be threaded into the IR

**Decision**: Add a boolean `is_application` (or a small `MsgCategory` enum) to `MessageIR` in `ir.hpp`, populated in `ir.cpp` by reading the `<message msgcat=…>` attribute.

**Rationale**: Grep confirms the IR does **not** currently carry msgcat; the emitter relies solely on `kOfficial33`. A one-field IR addition is the minimal change enabling R1's predicate. `ir.cpp` already parses `<message>` attributes (name, msgtype), so this is a localized addition.

**Alternatives**: Re-parse the XML in the emitter (rejected — the IR is the single model; emitters must not re-read source).

## R3 — Coverage selection wiring (CMake → tool)

**Decision**: Add a CMake cache option `FIXPP_CODEGEN_V44_FAMILIES` with values `all` (default) / `official`, plumbed in `cmake/Codegen.cmake` as a `--families <mode>` CLI argument to `fixpp-codegen` (parsed in `main.cpp`, carried to `emit_builders`). The env-var toggle used in the spike is NOT shipped.

**Rationale**: A CLI flag + CMake cache option is the idiomatic, reproducible control (matches how the driver already passes `--xml/--out`); it is visible in the build graph and honors the "build option, not a monolith" spike recommendation. Default `all` delivers families in the stock artifact (Clarifications: full-family default); `official` is the opt-DOWN bounding compile cost to today's baseline.

**Alternatives**: Env var (rejected — invisible to the build graph, non-reproducible). Always-all, no option (rejected — forecloses the cost opt-out the spike measured a need for; +19 s/TU).

## R4 — The read/reify path is already universal (write-side-only feature)

**Decision**: Do NOT add any read-side codegen. `emit_messages.cpp` and `emit_reify.cpp` already loop **all** `ir.messages` with no `is_official` filter (verified), so `v44/Messages.hpp` typed read classes and `dict::reify` dispatch already exist for every message. 069 adds only the write side (`build_`/`validate_`/`Args`) + verification.

**Rationale**: Confirmed by source inspection. This narrows scope and means the differential round-trip harness (R5) can parse each built message back through the already-present typed/runtime read path immediately.

**Consequence**: Spec FR-001's "typed read-back" is satisfied by the pre-existing reify path; 069 does not regenerate it. Recorded so `/tasks` does not create redundant read-emitter work.

## R5 — Verification: differential round-trip at breadth (non-tautological)

**Decision**: A new table-driven harness seeds each in-scope message's `Args`, calls its generated `build_<Msg>`, then parses the produced bytes back through the **independent runtime-XML path** (`Dictionary::as_table_view` / the shipped inbound parse), asserting each seeded field reads back to its exact value. Every one of the ~81 messages is covered. This is anchored externally (R6) so builder+parser cannot be co-wrong.

**Rationale**: Direct application of the "trust the generator, verify the generator, sample the output" model and the logged coverage-push lesson (a pure build→parse loop is tautological; a mass positive-fixture push enshrines emitter bugs). The runtime path is an *independent* reader from the write emitter, so agreement is meaningful; the external golden (R6) breaks the residual tautology.

**Alternatives**: Per-message hand golden for all 81 (rejected — 81 hand fixtures is the wall this whole direction avoids, and enshrines bugs). Build→parse only (rejected — tautological).

## R6 — External anchor: exemplar-per-family goldens

**Decision**: Capture QuickFIX-authored golden wire bytes for a small **exemplar-per-family** subset (one representative per major family group: e.g. TradeCaptureReport for post-trade, CollateralInquiry for collateral, PositionReport for positions, SecurityList for reference-data, plus reuse of the existing 061/067 exemplars), asserting the generated bytes match. Not all 81.

**Rationale**: Reuses the existing reference-engine golden-capture approach (`golden/*.fix`, the `BM-*`/067 precedent). One external anchor per family group is enough to break the round-trip tautology without 81-way external parity (which stays optional hardening per SC-006).

**Alternatives**: External golden for all 81 (rejected — reference-engine capture cost; optional hardening only). No external anchor (rejected — leaves the round-trip tautological, FR-010 violation).

## R7 — Constitution: Article XVIII §7 amendment (folded)

**Decision**: Fold an Article XVIII §7 amendment into 069 (user-approved 2026-07-11). The amendment reclassifies all-v44-application typed codegen as delivered by this feature, absorbing the pending D7 §XVIII.7 staleness (which already listed C/R families inconsistently). Gate A reviews the amendment; user signs off (Article XX §2). Version bump to constitution v0.5.

**Rationale**: 069's new messages are the §7-deferred A-014..A-034 + C/R/P families; landing them pre-v1.0-tag conflicts with §5/§7, so Article XX §1 requires amend-then-proceed. Folding the amendment into the feature branch matches the 035/043 precedent (Gate-A-folded amendments). "Defer past the v1.0 tag" and "narrow scope" were offered to the user and declined.

**Alternatives**: Standalone `Constitution: amend §XVIII.7` PR (heavier; the folded form is the established precedent). Defer 069 (declined by user).

## R8 — Enum-domain validation out of scope (carried limitation)

**Decision**: Generated `validate_<Msg>` enforces required-field presence + type conformance only (exactly matching 067). Enum value-domain stays unbacked; record `L-069-*` in `behaviors-and-limitations.md`.

**Rationale**: Clarifications 2026-07-11. No enum value tables exist in the model; adding them is a separate dictionary/IR sub-feature. Consistent with 067's shipped validator behavior — no regression, no silent scope creep.

**Alternatives**: Build a minimal enum surface now (declined at /clarify — separate scope).

## Build-cost summary (spike-measured, carried into plan)

| Axis | Measured | Feature disposition |
|---|---|---|
| Builders | 33 → 86 (spike incl. N-002/N-003); **81 in-scope** | default `all`; `official` opt-down restores 33 |
| Header | 2.20× (1.72 → 3.80 MB) | disk +17 MB across presets — trivial |
| Compile / TU | +19 s (2.57×) | bounded to ~5 TUs; `official` opt-down for cost-sensitive builds |
| Object/binary | 1.00× unused | grows only with builders CALLED (harness TU) |
| CI matrix | ~+11 min if monolithic × 7 presets | acceptable; per-family header split deferred unless TU count grows |
