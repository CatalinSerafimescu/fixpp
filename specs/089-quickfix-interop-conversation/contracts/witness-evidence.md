# Contract: run evidence and the witness completeness gate

**Producers**: the harness shim (cell rows) and the fixpp-side cells (witness rows).
**Consumers**: the schema checks, and row 4c's breadth follow-on, which cites a witness per catalogue row.

## Why this contract exists

Today `cell_results.yaml` requires `{id, config, kind, status, matrix_disposition, spec_ref}` and nothing
else (`cell_results_schema_check_test.py:23`). **A hand-edited `status: pass` satisfies all nine tests.**
There is no timestamp, no run id, no artifact pointer, no counterparty version. Whatever else this
feature delivers, evidence that cannot distinguish a real run from a typed word is not evidence.

## Level 1 — cell rows

New identity and evidence fields per [data-model.md](../data-model.md) §5.

> ⚠️ **WITHDRAWN at Gate A round 1: *"Existing fields unchanged."*** That was a constraint this contract
> imposed on itself, and it was jointly unsatisfiable with the obligations below. It stays withdrawn — but
> the edits it was withdrawn to permit are **narrower than round 1 assumed**; see E-1a.

### ⛔ Two artifacts, and they must not be one

⚠️ **One artifact cannot be both a committed CI-checked manifest and a machine-local run ledger.** This is
not a preference; the tree refuses it. `cell_results_schema_check_test.py` is registered as a **ctest**
(`tests/interop/CMakeLists.txt:459`) and is provisioned in **three** CI tiers — `tier1.yml`, `tier2.yml`,
`tier3-libcxx.yml` — on GitHub-hosted runners that have never executed an interop cell and hold **no run
artifacts at all**; it resolves a **committed** manifest relative to its own file; and that manifest carries
**59** `status: pass` rows today, none of which has an 089 run artifact. So *"the check opens every
referenced artifact"* fails on every hosted runner for every 089 row, and an unconditional
`REQUIRED_FIELDS` extension breaks all 59 rows, colliding with FR-020. Every restatement that keeps them as
one artifact fails the same way.

| Artifact | Checked by | Asserts | Opens anything? |
|---|---|---|---|
| `cell_results.yaml` — committed expected inventory | the shipped ctest, three CI tiers | structure; new fields **conditional on `kind: conversation`**; every `pass` names a ledger entry | **no** |
| the **run ledger** — a `runs:` section of `witness_evidence.yaml`, committed (data-model §11) | the same ctest | one **`kind: conformance`** entry with `authoritative: true` per `(cell_id, config)`; the set of those slots equals the 32-slot inventory exactly; `terminal_state`, `witness_count`, `evidence_relpath`, `evidence_digest`; **no absolute paths**. ⚠️ `validator-positive-control` rows are present and are **excluded from that equality** (data-model §11 § *THE TWO DISCRIMINATORS*) | **no** |
| the **validation pairs** — a `validation_pairs:` section of the same `witness_evidence.yaml`, committed (data-model §10) | the same ctest (**E-7a**) | the set of `kind: conformance` pairs equals the **16-pair inventory exactly**; every `off_run_id`/`on_run_id` resolves to a row of the `runs:` ledger | **no** |
| the **run artifact** — machine-local, never committed | the **promotion step** (FR-014b), on the machine that ran the cell | both streams' `hello` **and `terminal`**, the join keys, the completeness gate, `evidence_digest` | **yes — this is the only reader** |

| # | Obligation |
|---|---|
| E-1 | `status: pass` **without** a ledger entry whose `terminal_state` is `completed` and whose `witness_count` equals the census figure for that slot ⇒ schema check **FAILS**. The check itself **opens nothing** |
| E-1a | Manifest row identity is **`(cell_id, config)`** — 32 rows, retries never committed — and the shipped `id` field is retained, derived as `"<cell_id>@<config>"`. ⚠️ **`test_ids_unique` therefore does NOT need replacing**; round 1's claim that it did rested on 8 ids serving 32 rows, which the manifest/ledger split removes |
| E-1b | **Corroboration runs at promotion time, where the artifacts are** (FR-014b): the named promotion command opens both streams, requires a `hello` **and** a `terminal` on each, checks the join keys against the row, evaluates the completeness gate, persists the bundle under `$FIXPP_INTEROP_EVIDENCE_ROOT` and records `evidence_digest`. Nothing else may write a `status: pass` conversation row |
| E-1c | Exactly **one `kind: conformance` run with `authoritative: true` per `(cell_id, config)`**; retries carry `authoritative: false` and enter no gate, and `validator-positive-control` runs occupy **no** slot at all (data-model §11 § *THE TWO DISCRIMINATORS*), so neither can make a slot look doubly-claimed. ⚠️ This does not fail loudly on its own: the completeness projection drops `run_id`, so duplicate rows *collapse* and a retry is invisible — a failed run and a retried pass could otherwise both sit in the record with nothing saying which governs |
| E-2 | `counterparty_version` is sourced from the **hello record**, never from a config file — a config says what was *asked for*, not what *ran* |
| E-3 | `counterparty_digest` is the digest actually used, not a tag |
| E-4 | Each of the **4** configs emits its **own** row; folding one into another is a violation |
| E-5 | An `ENOSPC`-killed run is recorded as **`error:enospc`** — never `pass`, `skip`, `n/a` or `fail`. The shipped status vocabulary has no slot for it, so the vocabulary is extended; leaving it closed forces the implementer onto `fail` and makes an infrastructure abort indistinguishable from a fidelity defect |
| E-6 | The run's **arm attestation** (`has_validator`, data-model §1a) MUST equal `arm == "validation-on"` |
| E-7 | ⛔ A **validation pair** (data-model §10) MUST name **two DISTINCT runs with OPPOSITE arms**, stated **positionally** because a run carries one `cell_id` while the pair carries a pair: `off_run_id ≠ on_run_id`; the run named by `off_run_id` carries **`cell_pair[0]`** and recorded `has_validator: false`; the run named by `on_run_id` carries **`cell_pair[1]`** and recorded `has_validator: true`; and **both** carry the pair's `config`, `script_digest` and `kind`. Both runs resolve against the `runs:` ledger — including `kind: validator-positive-control` rows, which the ledger records (data-model §11). **Constructed and evaluated by the named promotion command** (`promote_interop_evidence.py`, FR-014b); a pair violating any part ⇒ **promotion RED**. ⚠️ **This is E-6's sibling, not E-6.** E-6 checks *one run's* attestation against *its own row*; nothing in it ranges over a pair's two referenced runs, so a degenerate pair reading both arms from one execution satisfies E-6 and reports `identical` across all 32 slots — a set equalling itself ⛔ **AND both referenced runs MUST be `authoritative: true`; for a `conformance` pair each MUST be the run selected for its `(cell_id, config)` slot.** ⚠️ Otherwise two **superseded retries** satisfy every predicate here and all 16 slots can be backed by runs the ledger says govern nothing — E-1c, E-7 and E-7a all green. ⛔ **AND `accepted_off`/`accepted_on`/`dispositions` MUST be EXTRACTED from the two referenced run artifacts and `verdict` COMPUTED from them** (exact set equality; `conformance ⇒ identical`, `validator-positive-control ⇒ diverged` matching `expected_verdict`) — a carried result lets a producer hard-code `identical` and pass this gate, E-7a and FR-010a's divergence probe at once |
| E-7a | ⛔ **The pairs must EXIST, and the set must be complete.** The `validation_pairs:` section of the committed `witness_evidence.yaml` MUST carry exactly the **16-pair `kind: conformance` inventory**, the equality **scoped `kind: conformance ∧ authoritative: true`** exactly as §11 scopes the slot equality — set **equality**, not containment — where **16 is derived, not counted**: the 32 conformance slots quotiented by the arm axis (`cell_id ≡ (combo_id, arm)`) = 4 combos × 4 configs. Control pairs are additional and are excluded from that equality. Checked by the **committed schema-check ctest**, which opens nothing (both operands are in the committed file). ⚠️ **This obligation exists because ZERO pairs was GREEN**: E-7 constrains a pair's *shape*, nothing constrained its *existence*, and no W-*/E-* obligation elsewhere mentions pairs at all — so an implementation emitting none passed every gate while SC-004 and FR-010/FR-010a ranged over an entity that legally did not exist ⛔ **AND exactly ONE `authoritative: true` `kind: conformance` pair per `(cell_pair, config)`.** ⚠️ Set equality keyed on the slot is satisfied when a slot is claimed **twice** — the duplicates collapse, so an `identical` and a `diverged` pair for one slot both pass with no rule saying which governs. Same collapse E-1c closes for runs; the 15-of-16 fixture cannot catch it (that forces a *miss*, this is a *spurious hit*) |
| **E-7b** | ⛔ **AT LEAST ONE `kind: validator-positive-control` PAIR MUST EXIST**, carrying `expected_verdict: diverged` **and** `verdict: diverged`, whose `off_run_id` / `on_run_id` resolve to two distinct `kind: validator-positive-control` rows of the `runs:` ledger with **opposite** `has_validator`. Checked by the **committed schema-check ctest**, which opens nothing — §11 puts control runs in the ledger (*"the `runs:` ledger holds **every** run — `conformance` and `validator-positive-control`, authoritative and superseded"*), and E-7 already resolves references against it, so both operands are in the committed file. ⚠️ **This obligation exists because ZERO CONTROL pairs was GREEN.** E-7a excludes control pairs from its equality **by design**, the ctest's other operands do not mention them, and E-7 is vacuous over a `kind` the producer emitted none of — so an artifact with 16 conformance pairs and no control pair at all passed every standing gate, while data-model §10 makes the control pair the **precondition of admissibility** for all 16: *"`identical` is not by itself evidence… The pair is admissible only alongside FR-010a's divergence probe."* FR-010a and SC-004a are a requirement and a success criterion, which this file's round-2 acceptance rule scores as **absent**, and `quickstart.md` Step 4 is a one-time demonstration, not a re-evaluated gate. ⚠️ **Anti-vacuity — ask what ELSE satisfies "a control pair exists".** A degenerate one: a pair naming one run twice, or two runs of the *same* arm. E-7 is `kind`-agnostic and rejects both — but **E-7 fires at promotion while E-7b lives in the committed check**, so leaning on E-7 here would reproduce exactly the host split **E-7c** exists to close. E-7b therefore **restates distinctness and opposite `has_validator` in its own normative text above**, and is satisfiable only by a pair that is well-formed *in the committed artifact* |
| **E-7c** | ⛔ **EVERY PAIR'S TWO REFERENCED RUNS MUST *STILL* BE `authoritative: true`, AND EACH REFERENCE OF A `conformance` PAIR MUST *STILL* BE THE RUN SELECTED FOR ITS `(cell_id, config)` SLOT** — the same two predicates E-7 evaluates at construction, **re-evaluated by the committed schema-check ctest on every CI run**. ⚠️ **This exists because `authoritative` is MUTABLE after a pair is written.** A retry that lands later supersedes a referenced run; E-7 runs **once**, when the promotion command constructs the pair, and does not re-run — so the exact failure mode E-7's predicate closes re-enters through the temporal door and nothing reports it. The ctest's pre-existing *"every `off_run_id`/`on_run_id` resolves to a `runs:` row"* does not catch it: a **superseded** row satisfies resolution. ⚠️ Both operands are in the committed file — the ledger and the pairs are sections of the same `witness_evidence.yaml`, and *the run selected for its slot* is already well-defined there by E-1c / W-3c — so this is not a new mechanism, it is the **same predicate at the host that re-evaluates it** |

**Why E-1b exists.** Requiring six more fields to be **present** does not close the threat this contract
states for itself — a hand-edited `status: pass`. Six hand-editable strings are as easy to type as one.
Corroboration means *someone* opens the stream and finds it agrees; the question round 1 got wrong was
**who**, and the answer cannot be a ctest three CI tiers run without artifacts.

**Why a `hello` is not enough.** `readback-jsonl.md` fixes the hello as the **first** line, emitted before
any message is processed. A counterparty that starts, writes its hello and conversates not at all supplies
every field a hello-only corroboration inspects. The **terminal record** (data-model §12) is required on
**both** streams for exactly this reason.

**Proof obligations for Level 1** — each shown RED against its own diagnostic, not merely a non-zero exit:

| Fixture | Must show |
|---|---|
| A validation pair whose `off_run_id`/`on_run_id` name **`authoritative: false` retries**, every other E-7 predicate satisfied | **E-7 RED.** ⚠️ *Forced miss cannot catch this*: the pair is well-formed — the defect is that the runs govern nothing |
| A validation pair whose `verdict` **disagrees with its own `accepted_off`/`accepted_on`** (equal sets, `verdict: diverged`; or unequal sets, `verdict: identical`) | **E-7 RED** — this arms the **second** half of the rule only: `verdict = f(accepted_*)`. ⚠️ **FORCED MISS on the derivation of `verdict`**, and it is labelled that way deliberately: it perturbs three **carried** fields and checks their mutual consistency, so a producer that fabricates self-consistent sets and **never opens a run artifact** passes it perfectly. The spurious hit it cannot catch has its own arm, next row |
| ⭐ **A validation pair whose carried `accepted_off` / `accepted_on` / `dispositions` / `verdict` are MUTUALLY SELF-CONSISTENT but DISAGREE WITH THE TWO REFERENCED RUN ARTIFACTS** — the artifacts show the validation-on arm rejected step `B-05`; the pair records both sets equal and `verdict: identical` | **E-7 RED at promotion.** ⚠️ **This is the arm for the EXTRACTION half** of the rule (*"`accepted_off`, `accepted_on` AND `dispositions` MUST BE EXTRACTED FROM THE TWO REFERENCED RUN ARTIFACTS"*, data-model §10). ⚠️ **spurious hit**: a fabricated pair is internally consistent, so it reports **the same shape a measured one does** and the verdict-consistency fixture above cannot see it. ⚠️ Note the asymmetry that makes this bite — the consistency half is checkable in the committed file and is therefore cheap and permanent, while the extraction half is checkable **only at promotion, on the machine that still holds the artifacts**. Arming the cheap half alone is the direction that fails toward green |
| **Two `authoritative: true` `kind: conformance` pairs for ONE `(cell_pair, config)`**, with opposite verdicts, every other gate satisfied | **E-7a RED.** ⚠️ **spurious hit** — set equality collapses the duplicate and reports complete |
| `pass`, no evidence fields | E-1 RED |
| `pass`, naming a ledger entry that does not exist | E-1 RED |
| **a run whose stream carries a `hello` and NO `terminal` record** | **E-1b RED at promotion** — *the pre-conversation hello does not corroborate a pass*. This is the fixture that separates announcing from conversating |
| a promoted bundle whose stream's `hello` carries a different `run_id` from the row | E-1b RED — *because the stream disagrees* |
| **a stream whose `hello` or `terminal` OMITS any join key** (`run_id`, `cell_id`, `config`, `script_digest`) | **E-1b RED at promotion.** ⚠️ This arm exists because the mismatch arm above cannot catch it: a *different* value disagrees loudly, an *absent* one leaves the comparator with nothing to compare and the join succeeds against nothing. Promotion MUST treat absent-key as failure, never as "not applicable". The gtest that cannot source a key aborts before the conversation starts |
| a ledger `witness_count` differing from the census figure for that slot | E-1 RED |
| **two authoritative runs for one `(cell_id, config)`** | **E-1c RED** — and shown *not* to be caught by the completeness gate alone, which collapses them |
| a `validation-on` row whose run recorded `has_validator: false` | E-6 RED |
| **a validation pair whose `off_run_id` == `on_run_id`** | **E-7 RED at promotion** — ⚠️ **and shown NOT to be caught by FR-010a's divergence probe**, which forces a *divergence*: the degenerate pair reports the same `identical` verdict a correct one does, so this is a **spurious HIT** and a forced-miss arm cannot see it. Asserting the divergence probe stays GREEN on this fixture is half the arm |
| **a validation pair naming two distinct runs that recorded the SAME `has_validator`** | **E-7 RED at promotion** — the distinctness half alone is not the rule; two different runs of the *same* arm are as degenerate as one run read twice |
| ⭐ **an OTHERWISE-COMPLETE artifact carrying 16 valid `kind: conformance` pairs and NO `kind: validator-positive-control` pair at all** | **E-7b RED** in the committed schema check — ⚠️ **and assert E-7a stays GREEN on this fixture**, which is the whole point: E-7a excludes control pairs from its equality by design, so it reports complete over an artifact whose 16 conformance `identical` verdicts are inadmissible. ⚠️ **Forced miss is the CORRECT polarity here** — the defect *is* an absence — but force it against the **otherwise-complete** artifact, or the arm proves only that a broken bundle fails |
| ⭐ **a pair, valid when written, one of whose referenced runs is LATER SUPERSEDED by a retry** — the ledger row is demoted to `authoritative: false` after the pair was constructed and E-7 passed | **E-7c RED** in the committed schema check — ⚠️ **and assert the committed check's PRE-EXISTING reference-resolution clause stays GREEN on the same artifact**: *"every `off_run_id`/`on_run_id` resolves to a `runs:` row"* is satisfied, because a **superseded row still resolves**. That contrast is the arm, and it is assertable on one committed artifact. ⛔ **Do not phrase it as "E-7 stays GREEN"** — E-7 carries the same `authoritative: true` predicate and would go RED here too; E-7's green was a fact about the state *when it fired*, not a property of the fixture, so asserting it would name a contrast that instantiates nothing |
| **a validation pair one of whose referenced runs disagrees with the pair on `config`, `script_digest` or `kind`** | **E-7 RED at promotion.** ⚠️ Three fixtures discriminate this checker, not seven — one per *class* of predicate (identity, arm-opposition, agreement-with-the-referenced-run); a fixture per clause would re-prove the same branch |
| **a `witness_evidence.yaml` whose `validation_pairs:` section is EMPTY**, every other gate satisfied | **E-7a RED.** ⚠️ This is the arm that closes *zero pairs is green*; it must be shown RED against the **otherwise-complete** artifact, or it proves only that a broken bundle fails |
| **a `validation_pairs:` section carrying 15 of the 16 conformance pairs** | **E-7a RED** — equality, not containment. ⚠️ The empty fixture alone cannot discriminate an existence check from a completeness check |
| an `ENOSPC` abort recorded as `fail` | E-5 RED |
| **the 59 pre-existing `pass` rows, unmodified** | the schema check stays **GREEN** — the control that proves the conditional-field rule did not break FR-020 |

## Level 2 — witness rows

**Three identities, named separately (FR-015c). Conflating them is what made the previous single "key"
both unsatisfiable and blind:**

| # | Identity | Value |
|---|---|---|
| 1 | **observed-row uniqueness** | `(run_id, cell_id, config, arm, script_step_id, direction, occurrence)` |
| 2 | **stable completeness key** | **`(cell_id, script_step_id, direction, occurrence)`** — `cell_id ≡ (combo_id, arm)`, so the **role × flavour axis is retained** |
| 3 | **cross-config projection `π`** | an observed row reduced to identity 2 — drops `run_id` and `config` |

| # | Obligation |
|---|---|
| W-1 | One row per identity 1, carrying `combo_id`, `cell_id`, `config`, `run_id`, `arm`, `authoritative` **and `kind`** as fields. ⚠️ `kind` is on the row because W-3a/W-3b/W-3c filter on it; without it π cannot exclude a control run's rows and the 100-key equality is unsatisfiable whenever a control has run |
| W-2 | The per-run expected set is **derived from the conversation script**, never hand-listed |
| W-2a | The script-derived set is asserted **exactly equal to `library/tests/interop/conversation/census.yaml`**, which is a **manual, mechanical transcription** of the declarative census in `spec.md` § *Conversation census* — the business step table, its declared inapplicable combinations and its expansion rule, totalling **100** completeness keys — that figure is a **pointer to § *Conversation census*'s arithmetic**, not an independent count — enumerated over **business steps × applicable combos × declared occurrences × 2 arms**, in exactly the unit of identity 2 (FR-015d). ⚠️ **W-2a consumes that table**; it MUST NOT re-derive the population from the script file, or it agrees by construction and is not a second opinion. ⚠️ **STATED LIMIT — the transcription step is MANUAL and nothing checks it.** `census.yaml` is what the gate reads; `spec.md` § *Conversation census* is its **source**, not its operand. Mutate a row of the spec table and **no gate reddens**, because no check opens `spec.md`. That limit is accepted deliberately: the alternative — generating `census.yaml` from the script — is the tautology `plan.md` § *SC-009a's two operands* forbids, and hard-coding `100` into `census.yaml` is forbidden by `spec.md` § *Conversation census* (the figure is derived in one place and a pointer everywhere else). **When `spec.md` § *Conversation census* changes, `census.yaml` is re-transcribed in the same edit.** |
| W-3 | Exact-set equality: a missing witness **FAILS** the gate |
| W-3a | **π(`kind: conformance` authoritative rows of config `c`) = π(same, config `c'`)** for every ordered pair of the four configs, **and** each equals the census's 100 keys. ⚠️ **Scoped to `conformance`**: a `validator-positive-control` run's rows are not census keys — unscoped, the first control run breaks this equality on the config it ran under. ⚠️ **Across `config`, never across `arm`** — `arm` is inside `cell_id`, so a cross-arm equality is unsatisfiable by construction and is not what SC-011 asks for |
| W-3b | The set of `(cell_id, config)` slots carrying a **`kind: conformance`** run with `authoritative: true` **equals the 32-slot inventory exactly**, not merely is contained in it. ⚠️ `validator-positive-control` runs occupy **no** slot and are excluded from this set — unscoped, a control run is a 33rd slot and this equality cannot be satisfied (data-model §11 § *THE TWO DISCRIMINATORS*) |
| W-3c | **Exactly one `kind: conformance` run with `authoritative: true` per slot**; rows with `authoritative: false` enter no gate, and `validator-positive-control` rows claim no slot to be one-per |
| W-3d | The `kind: conformance` **validation-pair** set equals the **16-pair inventory exactly** — the ledger-side statement of **E-7a**, kept here so a reader of the W-* table is not left with the 32-slot rule and no pair rule. 16 = the 32 conformance slots quotiented by the arm axis |
| W-4 | Rows **accumulate** across configs; a later config MUST NOT overwrite an earlier one's |
| W-5 | The gate **also** runs after the last configuration, over the union — as a second reading, never as the only one |
| W-6 | `skip` may not be produced by a missing readback record — that is `fail` |
| W-7 | An observed occurrence count differing from the **census-declared** count for that `(step, combo)` is its own class, **`occurrence_count_mismatch`**, distinct from a field-level `missing`. ⚠️ The two sides compute the ordinal independently and it never travels on the wire, so under the ResendRequest/GapFill steps FR-008 mandates the counters can legitimately diverge; reconciling against the peer's count would yield a false RED or an off-by-one pairing. Reconciling against the **declared** count keeps the no-injection rule intact |

**⚠️ W-2 and W-2a are two halves of one clause; neither alone is a check.** A hand-maintained expected
list drifts toward whatever is currently produced, so the gate agrees with reality by construction — the
reason W-2 exists. But script-derivation alone has the mirror defect: **one source drives both production
and expectation**, so deleting a business step removes the witness *and* its expectation, and the gate
stays green over a conversation that no longer covers that message type. W-2a gives the derivation
something that can disagree with it. This is not a retreat to a hand list — the per-run projection stays
script-derived.

**⚠️ W-3a is the clause that makes the config axis visible.** With no `config` on the row and equality
taken over the union, a configuration producing **zero** witnesses leaves the union unchanged and the gate
passes. That is the repository's named failure class — an instrument reporting clean because it could not
report anything else — inside the gate written to catch it.

**⚠️ Identity 2 keeps `cell_id`, and that is not decoration.** The obvious remainder after dropping
`run_id` and `config` — `(arm, script_step_id, direction, occurrence)` — **collapses all four role×flavour
combos into one set**. Under it, a configuration that ran **one** of its four combos projects to the same
set as one that ran all four, and the gate passes over a matrix that ran a quarter of itself. That is
W-3a's own failure, one axis over, inside the clause written to close it.

**⚠️ W-4 exists because of disk.** The four configs run in sequence with reclaim between them
(plan.md § Matrix sequencing). A record rewritten per config keeps only the last one's rows, and the
completeness gate would then pass over the remainder — a green produced by amnesia.

**Proof obligations for Level 2:**

| Arm | Kind | Must show |
|---|---|---|
| Delete one witness row | forced miss | W-3 RED |
| **Drop one whole configuration** | **spurious hit** — the gate reports PASS for a reason other than completeness | W-3a RED. ⚠️ Single-row deletion passes under a union-only gate, so it cannot discriminate a correct gate from a config-blind one; this arm can |
| Delete a business step from the script | forced miss | W-2a RED — the census disagrees |
| Add a step to the script that produces no witness | forced miss | W-2a RED — in the other direction |
| A witness row missing its `config` field | schema RED | W-1 — the field is required, not optional |
| A witness row missing **`kind`** (or **`authoritative`**) | schema RED | W-1 — ⚠️ W-3a's projection and the 32-slot rules **filter on** these, so a row missing one is silently dropped from the population it should have joined: the gate then measures a smaller set and reports **complete** |
| **Run one combo of one config and none of the other three** | **spurious hit** — the gate reports PASS because the projection lost the role×flavour axis | W-3a RED. ⚠️ This is the arm that discriminates identity 2 from `(arm, script_step_id, direction, occurrence)`; dropping a whole *config* does not, because a config-blind gate and a combo-blind gate both redden on that one |
| **A second (retry) run for one slot** | spurious hit | W-3c RED; and shown **not** caught by W-3/W-3a alone, which collapse the duplicate |
| A slot with no authoritative run at all | forced miss | W-3b RED — the inventory equality, not mere containment |
| A replayed step delivered once where the census declares two occurrences | forced miss | W-7 RED as `occurrence_count_mismatch`, **not** as a field-level `missing` |

## What this contract does NOT do

It does not flip any catalogue row. The narrow message set maps to `A-001`, `A-003`, `A-004`, `A-006`,
`A-007`, all already `done`. This contract makes the **citation mechanism** exist; the breadth follow-on
uses it.
