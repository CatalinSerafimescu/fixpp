# Phase 0 Research: v44 all-families typed codegen coverage

All items resolved from the measure-first spike (`research/G19-fix-fpml-iso20022/remaining-work/v44-all-families-measure-spike.md`) + direct source inspection of the 067 emitter. No open NEEDS CLARIFICATION.

## R1 — Message selection: allowlist → coverage-mode predicate

**Decision**: Replace the hardcoded `kOfficial33` allowlist in `emit_builders.cpp` with a coverage-mode predicate:
- `official` mode: keep the `kOfficial33` allowlist → **byte-identical** to today's output.
- `all` mode (default): emit for a message iff it is `msgcat='app'` **and** not in the N-002/N-003 exclusion set (`BE, BF, BW, BX, BY`). In the vendored FIX44 dictionary only **BE and BF exist** (BW/BX/BY are FIX 5.0, absent — their exclusion is a harmless no-op), so the effective FIX44 exclusion is **{BE, BF}** → **83 in-scope** (85 app − 2).

**Rationale**: `msgcat` is authoritative in the dictionary (`<message … msgcat='app'|'admin'>`), so the **8 `msgcat='admin'` messages** — the 7 session types (Heartbeat, TestRequest, ResendRequest, Reject, SequenceReset, Logout, Logon) **+ XMLnonFIX `35=n`** — are auto-excluded, never emitted; no separate session list. Only N-002/N-003 (which are `msgcat='app'`) need an explicit small exclusion set. This keeps the selection dictionary-derived, not a hand-maintained ~83-element list that would go stale.

**Alternatives considered**: A second hardcoded ~83 allowlist (rejected — duplicates dictionary knowledge, re-creates drift risk the completeness pin guards). Category-by-MsgType heuristic (rejected — `msgcat` already encodes it).

## R2 — msgcat must be threaded into the IR

**Decision**: Add a boolean `is_application` (or a small `MsgCategory` enum) to `MessageIR` in `ir.hpp`, populated in `ir.cpp` by reading the `<message msgcat=…>` attribute.

**Rationale**: Grep confirms the IR does **not** currently carry msgcat; the emitter relies solely on `kOfficial33`. A one-field IR addition is the minimal change enabling R1's predicate. `ir.cpp` already parses `<message>` attributes (name, msgtype), so this is a localized addition.

**Alternatives**: Re-parse the XML in the emitter (rejected — the IR is the single model; emitters must not re-read source).

## R3 — Coverage selection wiring (CMake → tool)

**Decision**: Add a CMake **`CACHE STRING`** `FIXPP_CODEGEN_V44_FAMILIES` (NOT `option()`, which is boolean) with values `all` (default) / `official`, constrained by `set_property(CACHE FIXPP_CODEGEN_V44_FAMILIES PROPERTY STRINGS all official)` and a **configure-time fatal error** (`message(FATAL_ERROR …)`) on any other value — failing before codegen runs. Plumbed in `cmake/Codegen.cmake` as a `--families <mode>` CLI argument to `fixpp-codegen` (parsed in `main.cpp`, carried to `emit_builders`). The env-var toggle used in the spike is NOT shipped.

**Rationale**: A CLI flag + CMake `CACHE STRING` is the idiomatic, reproducible control (matches how the driver already passes `--xml/--out`); it is visible in the build graph and honors the "build option, not a monolith" spike recommendation. `option()` is boolean and cannot carry the `all|official` string domain, so a `CACHE STRING` + `STRINGS` property + fail-closed validation is required. Default `all` delivers families in the stock artifact (Clarifications: full-family default); `official` is the opt-DOWN bounding compile cost to today's baseline.

**Alternatives**: Env var (rejected — invisible to the build graph, non-reproducible). `option()` boolean (rejected — cannot express the `all|official` string domain). Always-all, no control (rejected — forecloses the cost opt-out the spike measured a need for; +19 s/TU).

## R4 — The read/reify path is already universal (write-side-only feature)

**Decision**: Do NOT add any read-side codegen. `emit_messages.cpp` and `emit_reify.cpp` already loop **all** `ir.messages` with no `is_official` filter (verified), so `v44/Messages.hpp` typed read classes and `dict::reify` dispatch already exist for every message. 069 adds only the write side (`build_`/`validate_`/`Args`) + verification.

**Rationale**: Confirmed by source inspection. This narrows scope and means the differential round-trip harness (R5) can parse each built message back through the already-present typed/runtime read path immediately.

**Consequence**: Spec FR-001's "typed read-back" is satisfied by the pre-existing reify path; 069 does not regenerate it. Recorded so `/tasks` does not create redundant read-emitter work.

## R5 — Verification: differential round-trip at breadth (non-tautological)

**Decision**: A new table-driven harness seeds each in-scope message's `Args`, calls its generated `build_<Msg>`, then parses the produced bytes back through the **independent runtime-XML path** (`Dictionary::as_table_view` / the shipped inbound parse), asserting each seeded field reads back to its exact value. Every one of the 83 in-scope messages is covered. This is anchored externally (R6) so builder+parser cannot be co-wrong.

**Rationale**: Direct application of the "trust the generator, verify the generator, sample the output" model and the logged coverage-push lesson (a pure build→parse loop is tautological; a mass positive-fixture push enshrines emitter bugs). The runtime path is an *independent* reader from the write emitter, so agreement is meaningful; the external golden (R6) breaks the residual tautology.

**Alternatives**: Per-message hand golden for all 83 (rejected — 83 hand fixtures is the wall this whole direction avoids, and enshrines bugs). Build→parse only (rejected — tautological).

## R6 — External anchor: exemplar-per-family goldens

**Decision**: Capture QuickFIX-authored golden wire bytes for a **fixed, enumerated exemplar set** — one newly-covered message per newly-covered family class **plus ≥1 group-heavy/nested case** — asserting the generated bytes match. The exact required MsgTypes + seed names are enumerated in contract **C4** (`TradeCaptureReport 35=AE` [nested/group-heavy], `PositionReport 35=AP`, `CollateralInquiry 35=BB`, `SecurityList 35=y`, `Confirmation 35=AK`, `RegistrationInstructions 35=o`, `ListStatus 35=N`, `BusinessMessageReject 35=j`) + reuse of the existing 061/067 exemplars. Not all 83.

**Rationale**: Reuses the existing reference-engine golden-capture approach (`golden/*.fix`, the `BM-*`/067 precedent). A fixed one-per-family-class anchor (with a nested case) breaks the round-trip tautology without 83-way external parity (which stays optional hardening per SC-006). The list is fixed (not "one per family group") so implementation cannot satisfy FR-010 with a too-small/too-easy subset.

**Alternatives**: External golden for all 83 (rejected — reference-engine capture cost; optional hardening only). No external anchor (rejected — leaves the round-trip tautological, FR-010 violation).

## R7 — Constitution: Article XVIII §7 amendment (folded)

**Decision**: Fold an Article XVIII §7 amendment into 069 (user-approved 2026-07-11). The amendment reclassifies the **v44 `msgcat='app'` typed codegen** (83 in-scope) as delivered by this feature, absorbing the pending D7 §XVIII.7 staleness (which already listed C/R families inconsistently). **XMLnonFIX (A-034) is NOT reclassified — it is `msgcat='admin'`, never emitted.** Gate A reviews the amendment; user signs off (Article XX §2). Version bump **v0.4 → v0.5**. The exact §7 replacement text, Sync Impact Report, §5 disposition, and per-catalogue-row reconciliation are in `plan.md` `## Constitution Amendment Payload`.

**Rationale**: 069's new messages are the `msgcat='app'` subset of the §7-deferred A-014..A-034 + C/R/P families (A-034 admin excepted); landing them pre-v1.0-tag conflicts with §5/§7, so Article XX §1 requires amend-then-proceed. §5's no-early-ship bar is dispositioned (not separately amended) because delivered-now scope is no longer "deferred scope being early-shipped." Folding the amendment into the feature branch matches the 035/043 precedent (Gate-A-folded amendments). "Defer past the v1.0 tag" and "narrow scope" were offered to the user and declined.

**Alternatives**: Standalone `Constitution: amend §XVIII.7` PR (heavier; the folded form is the established precedent). Defer 069 (declined by user).

## R8 — Enum-domain validation out of scope (carried limitation)

**Decision**: Generated `validate_<Msg>` enforces required-field presence + type conformance only (exactly matching 067). Enum value-domain stays unbacked; record `L-069-*` in `behaviors-and-limitations.md`.

**Rationale**: Clarifications 2026-07-11. No enum value tables exist in the model; adding them is a separate dictionary/IR sub-feature. Consistent with 067's shipped validator behavior — no regression, no silent scope creep.

**Alternatives**: Build a minimal enum surface now (declined at /clarify — separate scope).

## Build-cost summary (spike-measured, carried into plan)

| Axis | Measured (spike) | Feature disposition |
|---|---|---|
| Builders | 33 → 86 (spike **msgtype-based**: 93 − 7 session, still counting XMLnonFIX + BE/BF); **83 in-scope** (msgcat-based) | default `all`; `official` opt-down restores 33. Real cost ≤ measured (83 ≤ 86). |
| Header | 2.20× (1.72 → 3.80 MB) | disk +17 MB across presets — trivial |
| Compile / TU | +19 s (2.57×) | bounded to ~5 TUs; `official` opt-down for cost-sensitive builds |
| Object/binary | 1.00× unused | grows only with builders CALLED (harness TU) |
| CI matrix | ~+11 min if monolithic × 7 presets | bounded by the CI policy below (not every preset pays `all`) |

## R9 — CI coverage-mode matrix policy (default `all`)

**Decision**: With `FIXPP_CODEGEN_V44_FAMILIES` defaulting to `all`, define an explicit per-preset CI policy rather than letting every preset pay the full compile cost:

- **`all` presets (carry full-family codegen):** the primary Linux clang debug preset **and** the coverage preset — this is where the differential round-trip harness + exemplar goldens + completeness pin run. The clang-debug `all` preset is the **single** preset carrying the full differential harness (satisfies FR-008's "≥1 full-family configuration"; SC-004 mode-count runs here).
- **`official` presets (opt-down to bound cost):** the sanitizer-heavy presets (ASan/UBSan/TSan) and MSVC/gcc-release build `official` — they exercise the emitter machinery and the 33-byte-identity regression without paying the 50-extra-builder compile cost on every sanitizer TU. (`/speckit-verify` Tier-1 still runs the harness on the `all` preset.)
- **FR-008 reconciliation**: exactly one preset (clang-debug `all`) is the mandatory full-family + full-differential configuration; the rest opt down. This makes the default-`all` cost bounded and predictable rather than ×7.
- **Per-family header-split trigger**: header splitting (one header per family instead of one monolithic `Builders.hpp`) becomes mandatory only when the `all`-preset TU count consuming `Builders.hpp` grows beyond the current ~5 (e.g. if a broad consumer starts including it), at which point the +19 s/TU × N cost crosses the CI-budget threshold. Deferred until then.

**Rationale**: The spike recommended default `official`; the bundle reverses to default `all` per the "families present in the stock artifact" clarification, so the cost must be governed by *which presets* run `all`, not by the default alone. This keeps the wide coverage continuously proven (FR-008) while keeping the sanitizer matrix cheap.

**Alternatives**: All presets `all` (rejected — pays the full cost ×7, the exact monolithic-cost the spike flagged). Default `official` + one `all` preset (rejected at /clarify — families must be present in the *stock* artifact).
