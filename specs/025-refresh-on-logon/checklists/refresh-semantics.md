# Refresh-Semantics Requirements Checklist: RefreshOnLogon — per-logon re-hydrate

**Purpose**: Validate that the requirements governing the per-logon re-hydrate mechanics — the store-wins direction (up/down), the `force`-param bypass of the 029 one-shot latch, the cold-open-vs-reconnect observability boundary, and the ordering vs the Logon-gate / 789 sampling — are complete, clear, consistent, and measurable. This is the feature's primary behaviour surface.
**Created**: 2026-06-09
**Feature**: [spec.md](../spec.md) · [contracts/refresh-knob.md](../contracts/refresh-knob.md) · [data-model.md](../data-model.md)

## Store-wins direction (up AND down)

- [x] CHK001 Is the store-wins semantic specified as **unconditional** — the manager counters are set to the store's values whether that moves them up or down — and is the advance-only (`max(store, live)`) alternative explicitly forbidden, not merely omitted? [Clarity, FR-003 / INV-RoL-4 / C4.1] — PASS: FR-003 states "store-wins / unconditional" and explicitly says "The system MUST NOT clamp the refresh to advance-only (`max(store, live)`)"; INV-RoL-4 and C4.1 mirror this.
- [x] CHK002 Is the **store-wins DOWN** case stated as a first-class requirement (a standby following a primary's reset-to-1 backward), distinct from the UP case, so a witness must move a counter *below* the live value? [Coverage, FR-003 / SC-002 / W2 / C4.2] — PASS: FR-003 explicitly calls out "whether that moves them up or down"; SC-002 mandates the below-live outcome; W2 is dedicated to the DOWN case distinguishing from advance-only; C4.2 states "a store set below live moves it DOWN (W2)".
- [x] CHK003 Is the requirement that **both** counters (inbound and outbound) are re-read and overwritten stated, rather than leaving it ambiguous whether refresh covers one direction only? [Completeness, FR-002 / W1 / data-model Entities] — PASS: FR-002 explicitly says "re-read BOTH persisted counters (inbound and outbound)"; data-model Entities lists "Persisted counters — `next_seqnum(direction, false)` for inbound + outbound"; W1 asserts "both manager counters = store's higher values".
- [x] CHK004 Is the store-wins source precisely identified (the persisted `next_seqnum(direction, increment=false)` reads, written into the manager via `SeqnumManager::hydrate(in,out)`), so the "store" the requirement reads from is unambiguous? [Clarity, data-model Entities / C4.1] — PASS: data-model Entities states "`store_->next_seqnum(direction, /*increment=*/false)` for inbound + outbound; the store-wins source the re-hydrate reads"; C4.1 confirms the write target is `SeqnumManager::hydrate(in,out)`.

## `force`-param latch bypass

- [x] CHK005 Is the mechanism by which the re-hydrate re-runs specified as **bypassing the 029 one-shot `hydrated_` latch (INV-H3)** via a `force` flag, rather than described only as "re-reads each logon" with the latch interaction left implicit? [Clarity, FR-002 / C2.2 / data-model C3] — PASS: FR-002 says "bypasses the 029 one-shot latch (INV-H3)"; C2.2 states "When `force == true`: the `hydrated_` early-return is bypassed"; data-model C3 names the exact code change "becomes `if (hydrated_ && !force) co_return ok`".
- [x] CHK006 Is the requirement that `force == false` is **byte-identical to 029** (the `hydrated_` early-return path unchanged) stated as the contract for the default path? [Consistency, C2.1 / INV-RoL-1 / FR-004] — PASS: C2.1 states "When `force == false`: byte-identical to 029"; INV-RoL-1 states "`refresh_on_logon == false` ⇒ every path is byte-identical to 029"; FR-004 mandates "seqnum hydration MUST be byte-identical to current 029 behaviour".
- [x] CHK007 Is the benign post-state of the latch after a forced call addressed (the `hydrated_` latch left set is harmless because subsequent forced calls bypass it anyway), so a reviewer is not left to wonder whether the latch must be cleared? [Completeness, C2.6] — PASS: C2.6 explicitly addresses this: "The `hydrated_` latch is left set after a forced call; this is benign (subsequent forced calls ignore it; a subsequent non-forced call short-circuits as already-hydrated, which is the intended cold-path behavior)." The non-persistent path edge case is also addressed.
- [x] CHK008 Is the three-way conjunction that gates an actual re-hydrate (`refresh_on_logon == true` AND `policy != bilateral_strict` AND `store_is_persistent_ == true`) stated as a single, complete predicate, with the responsibility split between call-site and `ensure_hydrated_` made explicit? [Completeness, data-model Force-trigger truth table / C3.1–C3.3] — PASS: data-model Force-trigger truth table states all three conditions; the responsibility split is made explicit ("The last is enforced inside `ensure_hydrated_` at `:576`; the first two at the call site."); C3 gives the exact call-site expression.

## Cold-open vs reconnect observability boundary

- [x] CHK009 Is it stated unambiguously that the knob's **only observable effect is on the 2nd-and-subsequent logon** — the cold-open first logon is *always* the unchanged 029 one-shot (where `force` is moot because `hydrated_` is still false)? [Clarity, FR-002 / data-model truth table note] — PASS: FR-002 says "each 2nd-and-subsequent logon event (the cold-open hydrate is the unchanged 029 one-shot — the knob's only observable effect is the re-hydrate on reconnect)"; data-model truth table note states "Cold open is always the 029 one-shot (`force=false` there is moot — `hydrated_` is still `false` on the first call, so the early-return is not taken regardless of `force`)."
- [x] CHK010 Is the cold-open-vs-reconnect distinction reflected consistently across spec, truth table, and witnesses (e.g. W5a is a **2nd-logon** witness, W5b is a **cold-open** witness), so the two are not conflated in any artifact? [Consistency, FR-002 / W5a / W5b / SC-005] — PASS: the distinction is consistent: FR-002 and SC-005 explicitly call W5a a "2nd-and-subsequent" / "2nd-logon" scenario; W5b is labeled "cold-open" and "L-029-3 inherited-gap witness"; data-model truth table rows separate cold-open and 2nd+ logon columns; tasks T030/T031 reproduce the same split.

## Ordering vs the Logon-gate, 789, and reset paths

- [x] CHK011 Is the ordering of the forced re-hydrate relative to the Logon establishment FSM specified — the outbound hydrate feeds the Logon body `34=`, and the call sits at the established 029 positions (initiator `:658`, acceptor `:1738`) — so the refreshed counter is sampled into the Logon, not after? [Clarity, C3.1 / C3.2 / data-model C4–C5] — PASS: C3.1 states the hydrate precedes `peek_outbound()` (`:699`); C3.2 states "The hydrate precedes `check_inbound` and the reply `peek_outbound()` (`:1951`)."; both anchors confirmed in live session.cpp (`:659` hydrate before `:699` peek; `:1739` hydrate before `:1951` peek).
- [x] CHK012 Is the interaction with `reset_on_logon` (024) specified as requiring **no special-casing** — the 029 ordering (outbound hydrate → durable reset → reset-flag decision) already overwrites the hydrate to `{1,1}` — rather than left as an open composition question? [Consistency, FR-008 / C5.1 / Edge Cases] — PASS: C5.1 states "no special-casing. The 029 ordering (outbound hydrate → durable reset at `:673` → reset-flag decision at `:713`) already yields body `34=1`"; FR-008 confirms "no special-casing is required"; Edge Cases section verifies the ordering.
- [x] CHK013 Is the 789 NextExpectedMsgSeqNum (027) interaction stated as unaffected (the `next_inbound_unsafe()` advertisement is sampled after the hydrate, so it reflects the refreshed value), rather than silently assumed? [Coverage, C5.4] — PASS: C5.4 explicitly addresses it: "789 NextExpectedMsgSeqNum (027): unaffected — `next_inbound_unsafe()` is sampled after the (possibly refreshed) hydrate, so an advertised `789` reflects the refreshed inbound, consistent." The store-wins-DOWN case and why no third site is added are also covered.
- [x] CHK014 Is the no-heap / allocation discipline on the per-logon re-hydrate path captured as a requirement (the refresh path allocates zero on a ready-awaitable store), so it is a measurable gate and not an afterthought? [Measurability, data-model W8 / [const §VIII.5]] — PASS: data-model W8 specifies "zero allocations on the re-hydrate apply step (`SeqnumManager::hydrate()`), the same proxy 029 W8 uses"; plan.md Constitution Check article VIII.5 confirms "the re-hydrate reads are counter-only (no frame body, no new container); reuses 029's non-allocating hydrate path; no-heap witness on the re-hydrate apply step (`SeqnumManager::hydrate()`) proxy, matching 029"; tasks T040 is the dedicated witness.

## Notes

- This checklist tests the requirements for the re-hydrate **mechanics**; the no-op / compatibility paths live in `compat-noop.md` and the wire/interop invariance + reset-precedence in `interop-wire.md`.
- The genuine probes here are **CHK002** (store-wins DOWN, the distinguishing case from advance-only) and **CHK005/CHK009** (the latch-bypass mechanism and the cold-open-vs-reconnect observability boundary). An anchor that does not resolve on these is a real gap, not a style nit.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 14 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | **14** |

### SPEC-FIXED items
None.

### DD-DECIDED items
None.

### WAIVED items
None.

Anchors spot-verified: FR-002, FR-003, FR-004, FR-008, INV-RoL-1, INV-RoL-4, C2.1, C2.2, C2.6, C3.1, C3.2, C4.1, C4.2, C5.1, C5.4, SC-001, SC-002, SC-005, W1, W2, W5a, W5b, data-model Force-trigger truth table — all resolve in Gate-A-converged plan/spec/data-model/contracts (submodule head `fb33a65`, session.cpp unchanged from 029 merge `0b9c8b8`). Line anchors `:564-566` (hydrated_ early-return), `:576` (store_is_persistent_ skip), `:601` (apply_inbound_seed withhold), `:658`/`:659` (initiator ensure_hydrated_ call), `:699` (peek_outbound), `:713-715` (bilateral_strict arm), `:1738`/`:1739` (acceptor ensure_hydrated_ call), `:1925` (received-141 reset) — all confirmed in live source.
