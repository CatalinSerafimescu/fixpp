# Behaviors & Limitations — re-assessment worklist (2026-07-08)

**Working review doc, not a spec source-of-truth.** Produced by a full one-by-one re-read of
every remaining B-*/L- entry in `behaviors-and-limitations.md` after the self-declared-closed
entries were moved to `behaviors-and-limitations-closed.md`. The re-assessment axis for BOTH
behaviors and limitations: **did later work change the ground truth?** (for an L: was it silently
discharged; for a B: did the behavior change / is the claim now false).

The entries below are the ones that are **neither cleanly discharged-and-moved nor 100% certain
still-true** — hand these to Codex/Fable for source verification. Everything not listed here was
judged clearly still-accurate and left untouched. Line numbers are against the POST-move live file.

For each: verbatim claim (abbrev), why flagged, what to verify + where, recommended disposition.
Codex should RETURN a verdict per item (keep-as-is / rewrite / move-to-closed), with the source
evidence — not silently edit.

---

## Category B — STALE: a later feature almost certainly changed the ground truth (high confidence)

These read as CURRENT but a later shipped feature says otherwise, in this same file. Not
self-declared-closed (so not auto-moved), but the claim as written is stale.

### B-1 — B-004-1 (live line ~82)
- **Claim:** "the `wire::Validator` … is NOT invoked on the session inbound path … `dictionary_driven_validator` has **zero production callers** … out-of-order header/body fields are accepted on the live session path." Carries a `[RATIFY RESOLVED]`/UNWIRED framing.
- **Why flagged:** 041 (**B-041-1**, live ~line 1494) wired `dictionary_driven_validator` into the live inbound path behind opt-in `SessionConfig::validate_inbound_messages` (default `false`), and B-041-1 explicitly states it "supersedes the 'UNWIRED / [RATIFY]' status of B-004-1 / B-005-7 under opt-in." So "zero production callers" is stale: there is now a caller (under opt-in).
- **Verify:** read `src/session/session.cpp` `on_inbound_frame` + `Dictionary::as_table_view()` wiring; confirm the opt-in caller exists and default-off still accepts out-of-order.
- **Recommended disposition:** REWRITE (keep — still true *by default*): note the 041 opt-in wiring; drop the absolute "zero callers." Apply the same edit to **B-005-7** if it carries the same UNWIRED framing.

### B-2 — B-007-2 (live line ~148)
- **Claim:** "There is NO active engine-level null-`clock` rejection: the `clock_not_set` gate … is **UNWIRED** in the shipped runtime `Engine` … `validate_engine_config()` … has **zero production callers** (test-only)."
- **Why flagged:** 041 (**B-041-2**, live ~line 1496) changed `Engine::start()` to `[[nodiscard]] expected_t<void>` and now calls `validate_engine_config()` unconditionally, returning `clock_not_set` on a null clock; B-041-2 explicitly "supersedes B-007-2 'UNWIRED' status." The claim as written is now **false**.
- **Verify:** read `src/session/engine.cpp` `start()` + `engine.hpp`; confirm the gate is wired and unconditional.
- **Recommended disposition:** REWRITE-as-superseded (the unwired behavior no longer exists) OR MOVE to closed. Lean REWRITE into a short "now wired by 041" behavior note, since operators still benefit from knowing the gate exists.

### B-3 — L-050-4 (live line ~1612)
- **Claim:** "the published `[2i §4.3]` session/app C-ABI error block is **DEFERRED**; the reachable `session_*`/`app_*` send/open arms map to `FIXPP_ERR_UNKNOWN` … **L-049-2 stays open** … awaits a dedicated `[2i §4.3]` amendment."
- **Why flagged:** 051 (**B-051-3**, live ~line 1620s) published the `[1400,1499]` session/app/message-construction error block and explicitly states it "**Discharges L-050-4 + L-049-2** (session/app arms)." L-050-4's own text still reads as deferred/awaiting — contradicted by B-051-3 in the same file.
- **Verify:** read `include/fix/c_api/error.h` (codes 1400–1405) + `src/capi/error.cpp` `translate()` + B-051-3; confirm the five arms now surface named codes.
- **Recommended disposition:** MOVE to closed (discharged by 051) — or rewrite to "discharged by 051 (B-051-3)." Note: L-049-2 itself already says "Feature B publishes the `FIXPP_ERR_SESSION_*` block"; re-check whether L-049-2 (live ~1596) is also now stale (session arms discharged, log/otel still deferred → L-051-1).

---

## Category C — DOUBTFUL: needs a source check to decide (genuine uncertainty)

### C-1 — L-044-1 (live line ~1545)
- **Claim:** "`reject_policy` is file-recognized but not file-selectable … the underlying `RejectPolicy` enum (owned by feature 005) is **forward-declared only with no enumerators defined in this checkout** — no canonical token can be mapped." Status: "resolved when feature 005 lands the `RejectPolicy` enum enumerators."
- **Why flagged:** a factual claim about the current source. Feature 005 shipped long ago; the enum may now have enumerators (making both this limitation and its "step-2" framing stale), or it may genuinely still be an empty forward-decl.
- **Verify:** `grep -rn 'enum .*RejectPolicy\|RejectPolicy' include/ src/` — does `RejectPolicy` have enumerators? Is there a string→token mapper for `reject_policy` in `src/config/scalar_mappers.cpp`?
- **Recommended disposition:** if enumerators now exist → rewrite/move (discharged); if still an empty forward-decl → keep as-is (accurate).

### C-2 — L-024-2 (live line ~936)
- **Claim:** Status "**RESOLVED — unit+wire proven; live close-out pending (T021)**." Body: the 032 fix shipped (initiator outbound restore-to-2 on peer `141=Y` echo); the live interop cells (`RL-*-init`) are "expected to flip … once the live cell is run (T021/SC-003 live close-out **PENDING**)."
- **Why flagged:** self-labels RESOLVED but with a pending live close-out — so it's neither fully closed (T021 open) nor a plain live limitation. B-032-1 (live ~line 1230s) carries the same "T021/SC-003 deferred" note.
- **Verify:** is T021 still pending? Check `tests/interop/.../cell_results.yaml` for `RL-*-init` disposition (still `deferred:initiator-141echo-outbound-rebase`?) and parent `REMAINING-WORK.md` / the Item-1 live-golden (G4) workstream.
- **Recommended disposition:** if still pending (part of deferred live-golden/G4) → keep as-is (accurate); if the cells were run/flipped → mark fully resolved and move to closed.

---

## Category D — Optional cleanup (low priority; NOT stale, just inconsistent with convention)

### D-1 — B-044-1 (live line ~1541)
- **Note:** leads "**RESOLVED (T039, PR #140)**" and reads as a historical bug record, but its body documents the current `src/config/toml_include.hpp` shim (asserts→catchable exception) — which IS live behavior. Inconsistent with the B-* "current behavior" convention.
- **Disposition:** optional — rewrite as a forward behavior statement ("the config loader routes tomlplusplus through an ODR shim that converts internal `TOML_ASSERT` aborts into catchable `parse_error` diagnostics"), or leave. **Keep** — not stale.

---

## Move-hygiene follow-ups (already-executed move; verify no loss)

- **L-005-3** (moved to closed) embedded a live by-design residual: "a too-low seqnum *without* PossDup stays session-fatal by design." Confirm this residual is still captured by a KEPT entry (candidate: **B-013-1** / the too-low rule in `session.cpp`) before relying solely on the archive. If not captured anywhere live, add a one-line note to a kept 005/013 entry.
- **Live→archive by-ID references** (resolve to the archive, not broken, but worth a glance): **L-021-2** cited by B-022-1; **L-050-1** cited by B-052-1 and L-050-5. Optionally add "(see closed archive)" to those citations.

---

## Record — entries MOVED to `behaviors-and-limitations-closed.md` (step 2, done 2026-07-08)

18 self-declared-closed entries + 1 closed-waiver note:
B-005-1, L-005-2, L-005-3, L-005-4, L-014-2, L-014-3, L-015-2, L-016-1, L-019-3, L-021-2,
L-024-1, L-033-5, L-050-1, L-050-z, L-053-1, L-054-2, L-062-1, L-062-2, + the "053
`msg_get_string` argout codec waiver CLOSED" note. Lossless: 310 formal ids → 292 live + 18 archive.

---

## OUTCOMES (applied 2026-07-08 — Codex gpt-5.5 + Fable claude-fable-5 verification)

**Codex (5 factual items):**
- B-004-1 — STALE → **rewritten** (dropped "zero callers"; noted 041 opt-in wiring). B-005-7 needed NO change (already carries the [RATIFY RESOLVED]/041 opt-in framing).
- B-007-2 — FALSE → **moved to closed** (gate wired unconditionally by 041).
- L-050-4 — DISCHARGED by 051 → **moved to closed**.
- L-049-2 — partially stale → **rewritten/narrowed** (session/app arms discharged by 051; log/otel + out_of_memory still UNKNOWN).
- L-044-1 — still TRUE → **kept**.
- L-024-2 — T021 done 2026-06-12 → **moved to closed**; B-032-1 parenthetical **rewritten** to LIVE-CLOSED; `specs/032/tasks.md:61` T021 checkbox **ticked**.

**Fable (6 reachability waivers):**
- L-063-2 — CLAIM BROKEN (reachable on any group-bearing dict) → **rewritten** (reframed as reachable GA C-ABI silent-wrong-value defect; wrong FIX44 MassQuote example replaced with the real ExecutionReport/NoLegs example) + **GitHub issue #179** opened.
- L-063-4 — DOUBTFUL (overstated rationale + unenforced for user dicts) → **rewritten** (corrected "wire ambiguous" claim; added user/dialect-dict caveat) + hardening tracked in **#180**.
- L-062-3 — DOUBTFUL (typed-path safety rests on unenforced disjointness) → **rewritten** (added the disjointness caveat) + **#180**.
- L-004-5, L-025-2, L-041-3 — HOLDS → **kept**.

**GitHub issues opened:** #179 (L-063-2 membership-aware C-ABI follow-up), #180 (dictionary-census hardening assertions + optional load-time guard).

This worklist is now fully discharged.
