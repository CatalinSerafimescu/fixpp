# Phase 0 Research — 004-wire-codec

**Anchor:** `.specify/2b-wire.md` v0.2 (Gate A round 1 converged). The design doc already resolved the hard architectural questions during Phase-2 Gate A (Appendix C: 3 root causes + Codex 3 P1/5 P2/2 P3 + Opus 4 P1/4 P2/2 P3). This Phase-0 record consolidates those decisions in `Decision / Rationale / Alternatives` form and resolves the remaining Technical-Context unknowns. No `NEEDS CLARIFICATION` remains (the 3 `/clarify` answers fixed the scope unknowns).

---

### D-1 — Five primitives + shared `View` flyweight base

- **Decision:** Implement `Framer`, `Parser<Mode>`, `OffsetTable`, `Writer`, `Validator` and the `View` base exactly per `[2b §4.1–§4.8]`. Views never own bytes; lifetime is the originating frame buffer's.
- **Rationale:** W-011 (zero-copy) + `[const §XV.1]` (no per-message heap) + `[SYN §3.1 Q2]` (flyweight + caller-owned buffer). Locked at Phase-2 Gate A.
- **Alternatives:** owning message objects (rejected — allocation per message, `[const §XV.1]`); single monolithic parser without an offset table (rejected — `[const §XV.7]` bans linear-find-only).

### D-2 — Hybrid offset table via compile-time `Parser<access_mode>`

- **Decision:** `enum class access_mode { Iter, Index }`; `Parser<Index>` builds the offset table eagerly, `Parser<Iter>` skips it (zero-alloc streaming). Mode is per-translation-unit; no runtime branch on the hot path (`[2b §6.3]`).
- **Rationale:** `[SYN §3.1 Q1]` decided hybrid; codegen/session use Index, tap/logger use Iter. Template specialization gives one symbol set per TU, zero dispatch cost.
- **Alternatives:** runtime `if (mode==Iter)` (rejected — hot-path branch); Index-only (rejected — forces offset-table cost on streaming consumers).

### D-3 — Three-arena PMR pinning

- **Decision:** Per-message arena (`SessionConfig::message_arena`, reset after `fromApp`) holds OffsetTable `entry[]` + hash overlay + lazy group sub-indices + validator scratch + writer group bookkeeping. Framer carry lives in the **session-lifetime** `SessionConfig::framer_carry_arena` (NOT per-message — carry spans messages). Writer scratch is a constructor parameter.
- **Rationale:** `[const §VIII.5]` zero `new`/`delete` parse→`fromApp`; `[2b §6.6]`/`[2b §8]`. The carry-arena lifetime correction was Opus adversarial NEW P1 (v0.1 wrongly put carry in the per-message arena).
- **Alternatives:** single arena (rejected — carry would be invalidated by per-message reset); global heap (rejected — `[const §VIII.5]`/`[const §XV.1]`).

### D-4 — `Validator` runtime-virtual, exactly 5 pure-virtual, full per-version default

- **Decision:** `Validator` is a runtime-virtual interface with exactly 5 pure-virtual (`validate`, `validate_field`, `required_fields`, `field_valid_for`, `group_first_field`). Default `dictionary_driven_validator` holds `dict::table_view` by value, validates **every dictionary-known field present** unconditionally, for all four versions (v42/v44/v50sp2/vt11) — `/clarify` Q2.
- **Rationale:** `[const §XIV.2]` ≤5 cap satisfied directly; runtime substitution restores the constitutional plugin pattern; per-accessor validation was unsound (Root cause #2). Full per-version depth per spec FR-010/SC-005.
- **Alternatives:** concept-bound template `Validator<DictTableView>` (rejected at Phase-2 Gate A — sidesteps the plugin cap, forces N×M template instantiation); structural-only or interface-only default (rejected at `/clarify` Q2).

### D-5 — Mandatory byte-sum-mod-256 CheckSum, digit-only BodyLength

- **Decision:** CheckSum = unsigned byte-sum mod 256 of `[start_of_8 .. start_of_10)`, 3 zero-padded ASCII digits; verified by `Framer` before any parser sees a frame; no production bypass. BodyLength rendered digit-only with a `memmove` backpatch at `commit()` (no space padding).
- **Rationale:** `[FIX50SP2 §3]`/W-005 (byte-sum, *not* XOR — Codex P1 #2); `[FIX50SP2 §3.3]` `Length` forbids padding (Codex P1 #3). Digit-strict counterparties reject `9=   123|`.
- **Alternatives:** XOR checksum (rejected — non-conformant, regression-tested by seam #12); checksum-bypass switch (rejected — `[2b §2]`, Codex P1 #1 deleted `verify_checksum`); space-padded BodyLength (rejected — non-conformant).

### D-6 — Lifetime enforcement: `[[clang::lifetimebound]]` + debug generation token

- **Decision:** Every view-returning accessor/ctor carries `[[clang::lifetimebound]]`; debug builds embed `detail::generation_token` and trap in `View::check_alive()` on use-after-buffer-reuse; release strips the token (`[[no_unique_address]]`). MSVC gap accepted (`[const §IX.4]`-adjacent / `[2b §6.4]`).
- **Rationale:** Root cause #3; `[arch §5.5]` lifetime model; `[SYN §3.1 Q2]`. Cross-strand escape is via `MessageView::reify` (owned by 2c) — documented, not re-implemented here.
- **Alternatives:** runtime refcounting on views (rejected — allocation + hot-path cost); no enforcement (rejected — silent UB).

### D-7 — Eager/lazy offset-table footprint spike is an in-PR deliverable (`/clarify` Q3)

- **Decision:** `bench/wire/offset_table_footprint_bench.cpp` (seam #6) measures raw `entry[]` + hash overlay separately, in occurrence space, over Logon / NewOrderSingle / NewOrderList×{1,10,100} / MDIR×{10,100,1000} / SecurityList×{1000,3000,5000}. Result recorded as a decision artifact closing `[arch §11 row 1]` / `[2b §10 Q1]` (spec SC-008).
- **Rationale:** Hybrid is DECIDED; the spike *confirms* sizing before it hardens across 2c/2d/2e/2i. `/clarify` Q3 = required in 004.
- **Alternatives:** defer to follow-up (rejected at `/clarify` Q3 — leaves sizing assumptions unvalidated while downstream modules build on them).

### D-8 — Debug generation-counter cost spike (`[2b §10 Q3]`)

- **Decision:** Micro-bench `View::check_alive()` overhead on a 200-tag read loop during implementation; if > 2× release, fall back to per-N-access sampling. Recorded in the verify/bench record.
- **Rationale:** `[2b §10 Q3]`, owned by 2b. Keeps debug builds usable.
- **Alternatives:** unconditional check regardless of cost (kept as default unless spike shows >2×); no debug check (rejected — D-6).

### D-9 — Cross-doc Q2 (HALO on parse→dispatch) — confirm at 2d, not a 004 blocker

- **Decision:** Record `[2b §10 Q2]` as a 2d-owned threading-contract spike; 004 is the producer side. No 004 task gated on it.
- **Rationale:** `[const §II.4]` (no compiler-version pin; PMR fallback handles HALO gaps). Wire's parse path does not depend on HALO firing.
- **Alternatives:** block 004 on the HALO measurement (rejected — out of wire's ownership; `[2b §11]`).

### D-10 — Cross-doc Q4 (arena reset cadence) — confirm at 2d

- **Decision:** Wire assumes **per-message** reset (`[2b §6.6]`/`[2b §8]`); if 2d picks per-batch, `[2b §6.6]` needs an update there, not here. 004 implements against per-message.
- **Rationale:** Per-message bounds peak per-session memory tightest; it is wire's stated preference and the safe default.
- **Alternatives:** assume per-batch (rejected — higher peak; not wire's call to change).

### D-11 — Cross-doc Q5 (Iter-mode Length+Data dialect) — confirm at 2c

- **Decision:** `field_iterator` uses a static `constexpr` table of FIX-standard Length+Data pairs (`[2b §4.3]`). Dialect-introduced new BLOB pairs are out of Iter-mode scope for v1.0 (tap/logger don't read BLOBs); revisit at 2c if a real dialect needs it.
- **Rationale:** Iter mode is dict-free by design; static table is exhaustive against FIX 5.0 SP2.
- **Alternatives:** thread the runtime dict into Iter mode (rejected — defeats the zero-alloc dict-free streaming contract).

### D-12 — Cross-doc Q6 (MessageStore raw-frame model) — confirm at 2e

- **Decision:** Wire exposes `frame_view::bytes()` for the raw-frame journal path; the lossy-traits `static_assert` lives at the typed-payload persistence sink (2e/2j), not at wire. Confirm canonical raw-frame model at 2e.
- **Rationale:** `[2b §7.4]` two-sink distinction (Opus NEW P1); mirrors 2a v0.3 §7.1 signed-off rule.
- **Alternatives:** wire owns the assert (rejected — wire is the producer, not the persistence sink).

### D-13 — No C-ABI surface; abidiff N/A this PR

- **Decision:** Wire emits no `extern "C"` symbols; no wire type in `<fix/c_api.h>` (`[2b §5]`, `[const §X.2]`). `[const §IX.5]` abidiff is explicitly N/A. The 13 `wire_*` `core::error` variants' C-ABI coalescing (`FIXPP_ERR_WIRE_*`) + `abi_history` audit entry are deferred to 2i under the same time-bounded waiver shape used by 002/003.
- **Rationale:** `fixpp_msg_t` accessors are 2i-owned; wire is the C++ surface 2i wraps.
- **Alternatives:** expose wire views through C ABI (rejected — `[const §X.2]` no C++ leakage).

### D-14 — Parser-vs-hffix parity measured but not a this-PR blocker

- **Decision:** `bench/wire/parser_bench.cpp` records parse/sec vs `hffix` into `bench/REPORT.md`; the `[2b §6.6]` ceilings are the this-PR Tier-1 gate. hffix parity is a v1.0 release-candidate gate (`[const §VIII.4]`).
- **Rationale:** `[const §VIII.4]` makes parity a v1.0 (not per-PR) gate; per-PR gate is the ±5% regression budget against the `[2b §6.6]` table.
- **Alternatives:** block this PR on hffix parity (rejected — premature; release-gate scope).

### D-15 — Cutover executed in this PR (`/clarify` Q1)

- **Decision:** Replace the 003-shipped frozen `include/fixpp/wire/message_view_contract.hpp` body with a thin re-export of the real `parser.hpp` `MessageView<Index>`; rewire `dict/reify.hpp`/`field_traits.hpp` (003) and the 001 wire FLOAT accessor onto it; ship the previously 2b-gated tests green (`tests/wire/cutover_2b_gated_test.cpp`). Zero references to the frozen stub remain (spec SC-006).
- **Rationale:** `/clarify` Q1 = cutover in 004; closes 003's R6 deferral and 001's wire-FLOAT deferral in one verifiable place; preserves 003's include path (file kept, body replaced).
- **Alternatives:** wire-only with deferred cutover (rejected at `/clarify` Q1 — leaves stub debt and an unverifiable unblock claim).

---

**All NEEDS CLARIFICATION resolved.** Scope unknowns closed by `/clarify` 2026-05-16 (D-15/D-4/D-7). Cross-doc items (D-9..D-12) are confirmations owned by 2d/2c/2e, explicitly *not* 004 blockers per `[2b §11]`. Proceed to Phase 1.
