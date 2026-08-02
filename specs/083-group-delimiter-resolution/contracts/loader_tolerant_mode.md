# Contract — Load disposition for an unresolvable group delimiter

**Feature**: `083-group-delimiter-resolution` | Satisfies FR-006, FR-006a, FR-006b, FR-006c, FR-006d, FR-012a, FR-017, SC-005, SC-011, SC-014
*(header traceability corrected 2026-07-31, Gate A round 2 — the body has carried FR-006c (C-6.1b), FR-006d (C-6.1a) and FR-012a / SC-014 (C-7.2) since round 1, and `plan.md`'s contract tree already described this file as the "FR-006/006a/006c/006d load disposition" while this line did not. SC-011 is W-4/W-5/W-6's criterion and SC-005 is W-7's; both were discharged here and unlisted since round 0.)*

Resolves the 2026-07-30 clarification: **reject by default, with an explicit opt-in tolerant mode.**

## Current behaviour (the defect)

A group whose declaration yields no first member records delimiter `0` and is **silently dropped** from the runtime dictionary. No diagnostic. Three FIX 5.0 SP2 groups are in this state today, taking six nested children with them, so the runtime registers 502 groups where the code generator emits 505 — the engine's own strict validation rejects messages its own generator produced.

This silent drop is the outlier in a loader that otherwise fails closed on every structural violation: root element not `<fix>`, missing major/minor, missing `<fields>`, missing or duplicate field `number`, unknown `type`, and a `<group>` with no matching `<field>` declaration all throw.

## Contract

**C-6.1 — Default: fail closed.** An unresolvable group delimiter, after the recursive resolution of `group_ctx_delims.md` C-1.2, rejects the load with a diagnostic **naming the offending group**. Mirrors the existing disposition rather than inventing one.

**C-6.1a — What "unresolvable" means, at one granularity.** *(added Gate A round 1, 2026-07-30; spec FR-006d.)* C-6.1 fires on **a group declaration whose first member cannot be resolved even after C-1.2's recursion**. It does **not** fire on a group that produces zero contexts because no message expansion reaches it — see `group_ctx_delims.md` C-3.6; that group's diagnostic is informational. `group_ctx_delims.md` C-1.4's per-*context* wording is subordinate to this clause and reads as "for a context that is produced at all". Before this reconciliation the two predicates disagreed: read at context granularity no predicate ever fired and the silent drop survived inside the requirement meant to kill it; read at declaration granularity every message-unreachable group in every shipped dictionary tripped fail-closed, potentially loading nothing.

**C-6.1b — Exception type is per loader; behaviour is symmetric.** *(added Gate A round 1, 2026-07-30; spec FR-006c, research.md D-7.)* `XmlLoader` throws `xml_parse_error`; `OrchestraLoader` throws `orchestra_parse_error`, which derives `xml_parse_error` (`include/fixpp/dict/error.hpp:98`) so either catch still works. This is not cosmetic: both loaders' fuzz harnesses enumerate a documented exception set and rethrow anything outside it as an invariant violation, and the Orchestra harness catches the **derived** type (`tests/fuzz/fuzz_orchestra_loader.cpp:52-80`). A base `xml_parse_error` from `OrchestraLoader` would fall through to that harness's terminal `catch (const std::exception&) { throw; }` and crash the fuzzer on every input containing an unresolvable group. Throwing the per-loader type lands inside both existing sets with **no harness widening** and adds **no `core::error` enum slot**, so the `test_020_error_completeness.cpp` slot pin is not engaged — the route 072 took for `group_delimiter_collision_error` (`include/fixpp/dict/error.hpp:23-26`). `include/fixpp/dict/orchestra_loader.hpp:33-37`'s documented exception list is updated in the same change.

**C-6.2 — Opt-in tolerant mode.** An explicit caller opt-in downgrades C-6.1 to warn-and-skip: a diagnostic is emitted, the group stays unregistered, the load succeeds. For callers loading untrusted or partial third-party dictionaries.

**C-6.3 — The default is fail-closed.** Tolerant mode is never implicit, never inferred, never the fallback on a parse difficulty.

**C-6.4 — Symmetry.** Both loaders expose the same option with the same semantics (FR-005).

**C-6.5 — No hot-path effect.** This is load-time only. It introduces no branch on the parse or validate path.

**C-6.6 — Source compatibility.** The opt-in must not break existing callers of the public C++ load entry points. Express it as a defaulted parameter or an options object defaulting to fail-closed — never as a new required argument. This is a C++ surface change only; no C ABI symbol is involved, so Article X's frozen-surface rule is not engaged, but existing source callers must still compile unchanged.

## Sequencing precondition — a task, not an assumption

**C-7.1** — Before C-6.1 is enabled, **all ten shipped dictionaries MUST be confirmed to load under the default.**

This is ordered deliberately. After the recursive resolution the three known offenders resolve — but whether some group *unreachable from message expansion* still resolves nothing is **not measured**. If one does, the fail-closed default would refuse a dictionary that loads today, and CI would be the first to find out.

**Answer space, defined** *(added Gate A round 1, 2026-07-30)* — previously this check would have produced a number without settling the question, because C-6.1 and C-1.4 disagreed about what was being tested. Under C-6.1a the check classifies each surviving zero-delimiter group into exactly one of:

| class | disposition |
|---|---|
| reachable from message expansion, first member unresolvable after C-1.2 | **C-6.1 fires.** Record it; decide explicitly whether it is a dictionary defect to report or a case for tolerant mode |
| declared but reachable from no message expansion (zero contexts) | **C-6.1 does NOT fire** (C-6.1a / C-3.6). Informational diagnostic only |

Two acceptable outcomes, one unacceptable:

- all ten load → enable C-6.1 as specified;
- some group is genuinely unresolvable **in the first class above** → record it, and decide explicitly whether it is a dictionary defect to report or a case for tolerant mode. **Not** a reason to quietly weaken the default;
- ✗ enabling C-6.1 without running this check.

*(There is a **third** precondition in this family, `group_ctx_delims.md` **C-7.3**: confirm all ten shipped dictionaries satisfy the FR-023 Entity-2 completeness invariant before that check is enabled. It lives with the clause it guards, but it is the same shape and the same Phase-3 gate as C-7.1 and C-7.2 — this feature adds three new fail-closed load paths, and none of them may meet the shipped set for the first time in CI.)*

**C-7.2 — Second Phase-3 precondition, alongside C-7.1** *(added Gate A round 1, 2026-07-30; spec FR-012a / SC-014).* The same deletion that C-7.1 guards also perturbs 072's load-time **nested-vs-parent delimiter collision guard** (`src/dictionary/xml_loader.cpp:1016-1017`, `src/dictionary/orchestra_loader.cpp:895-896`), which reads the `first_field_tag` this feature re-derives. Before Phase 3 exits: re-census nested/parent delimiter collisions under the **post-fix** delimiters across all ten dictionaries (`tests/dictionary/reused_tag_census_test.cpp::NestedGroupDelimiterCensus`), and re-derive `spec/behaviors-and-limitations.md` L-063-4's "real-dict-unreachable" disposition — its supporting audit was taken against the pre-fix delimiters this feature changes in 335 contexts. Also state the write order: the global projection must be complete before `finalize()`'s guard reads it (research.md D-10). Without this, the census can go **vacuous without failing** — it still passes and stops pinning.

## Witnesses (FR-006b — both dispositions, or the gate is half-tested)

| # | Given | Mode | Then |
|---|---|---|---|
| W-4 | a dictionary with an unresolvable group | default | load rejected, diagnostic names that group |
| W-5 | same dictionary | tolerant | load succeeds, diagnostic emitted, group unregistered |
| W-6 | all ten shipped dictionaries | default | all load — C-7.1 |
| W-7 | FIX 5.0 SP2 | default | 505 groups registered, matching codegen; the delta from 502 is accounted for by the three named groups, not asserted as a bare number (FR-017) |

Both dispositions are documented for operators. A fail-closed path with no rejection witness, or a tolerant mode with no skip witness, leaves half the contract unproven.
