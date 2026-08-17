# #209 — make the `bench` job a real gate

Companion issue: **#263** (`XmlLoader::load` regressed 60–90 % on main, unseen). This record decides
the **shape** of the gate; #263 owns the regression itself.

> **Revision note (r2).** The first draft of this record proposed hardening the existing
> baseline-vs-current ±5 % comparison and *seeding* CI-provenance baselines from this PR's own run.
> Codex adversarial review returned **P1=2 P2=6 P3=1** and both P1s were correct. §2b is the finding
> that killed it: **10 of the 14 allowlisted baselines contain no `cpu_time` field at all** — they are
> hand-authored analysis records, not machine comparands — so the proposed integrity cells would have
> fired on a *correct* tree. §3 is the pivot that resolves it. Review:
> `research/reviews/codex_ci209_bench_gate_r1_review.md`.

---

## 1. What is actually broken

The brief names three defects. There are **four**, and the fourth makes a subset-fix vacuous:

| # | defect | site |
|---|---|---|
| 1 | the job runs `placeholder_bench`, not the real benchmarks | `tier1.yml` "Run benchmark" |
| 2 | the compare step is `continue-on-error: true` | `tier1.yml` "Compare vs baseline (SOFT — exits 0 always)" |
| 3 | `bench` is absent from `tier1-required`'s `needs:` | `tier1.yml` `tier1-required` |
| **4** | **the "Run benchmark" step is ALSO `continue-on-error: true`** | `tier1.yml` "Run benchmark" |

`tools/bench_compare.py` additionally `return 0`s unconditionally by construction. The soft
disposition is therefore expressed in **three** places; removing any subset leaves the job green
regardless of outcome, and the `needs:` entry vacuous — the observe-but-never-assert shape this repo
has a recorded lesson about (`feedback_ci_gate_observes_not_asserts_witness_skips_into_green`, cited
at `ci-script-pins`).

### 1a. The `compile_time_bench` trap — resolved, and my first enumeration was WRONG

The question: would the wired-in job execute `compile_time_bench.sh`, whose flat 3 s ceiling `v44`
breaches at ~4.5 s and always has?

**No — because this job invokes no CTest at all.** That is the load-bearing reason, and it is the
only one that holds.

⚠️ **The reason r1 gave was false and is corrected here.** r1 claimed "exactly four `add_test`
registrations, none of the ~28 Google-Benchmark executables among them." There are four *textual*
`add_test(` call sites, but `bench/tls/CMakeLists.txt:8-20` defines one inside the function
`fixpp_add_tls_bench`, which is invoked **three times** (`:23`, `:26`, `:31`). The configured suite
therefore registers **six** CTest entries, and **three of them ARE Google-Benchmark executables**:

| configured CTest entry | kind |
|---|---|
| `compile_time_bench` | compile-resource script |
| `vlatest_builders_compile_bench` | compile-resource script |
| `bench_threading_regression` | cmake-driven regression check |
| `bench_pinset_snapshot_acquire` | **Google-Benchmark binary** (via `fixpp_add_tls_bench`) |
| `bench_pinset_find` | **Google-Benchmark binary** (via `fixpp_add_tls_bench`) |
| `bench_verify_peer_in_envelope` | **Google-Benchmark binary** (via `fixpp_add_tls_bench`) |

r1 conflated source call sites with configured registrations. Recorded rather than silently amended,
because the error is exactly the class this repo has a memory for — a census that counts one spelling
cannot see the others. **The narrower conclusion survives and is what the design rests on: the job
runs binaries by path and never invokes `ctest`, so no label set can reach `compile_time_bench`.**

### 1b. `v44` is not the only pre-breached ceiling

Splitting the compile-time ceiling out is not a `v44` accommodation. `bench/REPORT.md:103-114` records
the `[2b §6.6]` **≤ 200 ns** `Validator::validate` ceiling currently measured at **253–434 ns** (the
568–1265 ns figures at `:51-75` are the historical pre-fix set — r1 quoted those as current, also
corrected). A blanket "turn every recorded ceiling hard" reddens on that too. **Ceilings are a
different axis from regression-vs-baseline** and are out of scope here (§6).

---

## 2. Why the obvious fix is not available

### 2a. Provenance — every baseline was recorded on the dev host

Mechanical sweep of all 25 `bench/baselines/**/*.json`: every non-empty file reports `num_cpus` **8**
or **10**, or omits the key. Four `wire/*` files are `build_type: debug` — their own note in
`bench/REPORT.md`: *"debug timing is NOT the ceiling gate"*, ~10–25× release.

#263 supplies the decisive corroboration, and it is about the dev host comparing to **itself**:
*"`vt11`, which 082 provably does not touch, moved −35 % between two measurement sessions."* If
same-host/cross-session is invalid at 35 %, cross-host is not rescued by widening a band. This repo
has separately measured **27–43 %** spread across CI runners on heavy lanes
(`feedback_taskset_to_n_cpus_does_not_emulate_an_n_vcpu_runner`), where an unpaired A/B read
1.02×/1.04× and the **paired same-VM** A/B of the same change read **2.10×/1.79×**.

### 2b. ⚠️ THE FINDING THAT DECIDES THE DESIGN — most baselines are not Google-Benchmark JSON

Provenance is not the binding constraint. **Schema is.** Derived by counting `cpu_time` fields per
row, not by reading names:

| class | files | what they are |
|---|---:|---|
| genuine Google-Benchmark JSON (`cpu_time` on every row) | **12** | comparable in principle |
| non-empty but **zero `cpu_time` on every row** | **10** | hand-authored analysis records |
| `benchmarks: []` | **3** | nothing to compare |

The 10 are not stale measurements. They are **documentation in JSON clothing**, with field names that
say so outright:

| file | actual row fields |
|---|---|
| `wire/validator_bench.json` | `ceiling_ns`, `ceiling_source`, `ceiling_tolerance_pct`, `measured_release_ns_mean_10rep` |
| `dictionary/table_view_footprint_bench.json` | `metric`, `measured_B`, `pre_change_B`, **`_timing_is_meaningless_here`** |
| `wire/fix42_group_parse_bench.json` | `fixture`, `role`, `measured_release_ns_median`, **`_why_no_before`** |
| `capi/capi_commit_group_bench.json` | `pre083_median_ns`, `seed_median_ns`, `delta_vs_pre083_pct`, `verdict` |
| `wire/{framer,offset_table,parser,writer}_bench.json` | `ceiling_ns`, `ceiling_source`, `measured_debug_ns` |

Mapped onto the 14 binaries r1 proposed to gate: **2 genuine** (`decimal_baseline.json`,
`dictionary/xml_loader.json`), **2 empty**, **10 non-comparands**. r1's integrity cells would have
fired on a correct tree for 10 of 14 rows.

*(Codex counted 11 rather than 10 in this class; the sweep above yields 10 — capi, table_view_footprint,
fix42_group_parse, framer, offset_table, parser, typed_read_group, validate_group, validator, writer.
The discrepancy does not change the conclusion.)*

These files are **tracked records cited by `bench/REPORT.md`.** They must not be deleted or
overwritten to make a gate's life easier.

### 2c. And seeding from CI is circular

r1's escape — seed CI-provenance baselines from this PR's own run — does not work, per Codex F2:

- seeding from the **candidate** makes the candidate its own comparand; a regression introduced by
  the PR is canonized as the new normal;
- seeding from **`main`** canonizes **#263's known 60–90 % regression** as the baseline.

There is no non-circular seed available while the known regression is unfixed.

### 2d. Article VIII §2 is NOT violated, and this needed checking

> **§VIII.2** — *"Regression budget: ±5 % vs `bench/baselines/` per profile. Intentional perf changes
> update the baseline in the same PR with rationale in the PR body."*

Read in full, this fixes a **review discipline** and a **re-baselining rule**. It does not say CI shall
`exit 1` at 5 %, and says nothing about enforcement mechanism. **No amendment is required.** Recorded
because "the constitution pins 5 %" would otherwise look like a blocker and it is not. Codex
independently verified this at `.specify/constitution.md:192-200` and added the fair caveat that the
obligation *cannot be deferred indefinitely* — §6 is the answer to that.

---

## 3. The pivot: compare the candidate against ITS OWN merge-base, on one runner

Every problem in §2 is a property of the *comparand*, not of the band. Replace the comparand.

Build the **merge-base** and the **candidate** in the same job, on the same VM, with the same
compiler, and run the benches head-to-head. Then:

- no checked-in baseline is needed for the hard signal — §2b and §2c stop mattering;
- no provenance fields to get right — same machine by construction, so Codex F7 (an
  `ubuntu-24.04` label pins neither CPU model nor image revision nor compiler snapshot) is moot;
- it is the **paired same-VM instrument** this repo's own measurement says is the only valid one;
- it gates what a merge gate should gate: **the delta this PR introduces.**

**A-B-A, not A-B.** The candidate is measured, then the base, then the candidate again. The
**A-vs-A delta is this run's own noise floor**, printed every run. If the two A measurements disagree
by more than the band, the run declares itself **uninformative** rather than passing — a pass-order
effect is precisely what made the unpaired measurement lie, and a paired design that does not check
for one has only moved the assumption.

**What this does NOT catch, stated plainly: #263 itself.** `main` is already regressed, so
base-vs-candidate sees base == regressed and passes. Tier 2 catches *new* regressions. #263 remains
#263's to fix.

---

## 4. The design — three tiers

### Tier 1 — execution + schema. HARD. No comparand needed.

Every allowlisted binary must build, run, exit 0, and emit Google-Benchmark JSON that survives
validation. **This tier needs no baseline at all**, which is why it can be hard today for all 14
binaries including the 10 whose baselines are non-comparands.

| cell | condition |
|---|---|
| T1-1 | results file missing, unparseable, or empty |
| T1-2 | `benchmarks: []` |
| T1-3 | an allowlisted binary missing from / not executable in the build tree |
| T1-4 | a row with a missing or non-string `name` |
| T1-5 | `cpu_time`/`real_time` absent, non-numeric, NaN, infinite, zero or negative |
| T1-6 | duplicate `(name, run_type, aggregate_name)` identity — row multiplicity changed |
| T1-7 | `time_unit` not uniform across rows, or changed vs the previous tier |
| T1-8 | any row with Google-Benchmark's `error_occurred` set |

T1-6 is Codex F5 and it matters for real inputs: `dictionary/xml_loader.json` carries **21 rows with
6 duplicate names**, `threading_baselines.json` **56 rows with 16 duplicates**. A `{name: value}` dict
— which is what the pre-#209 comparator built — silently collapses those, so set equality cannot see
a lost repetition.

⚠️ **These are *clock-independent predicates*, not "zero-flake"** (Codex F5, and the wording is his
correction). T1-1 can genuinely fire from a runner timeout or an OOM kill. That is infrastructure
failure, handled by normal CI retry — not a tree defect, and not something the predicate itself
introduces.

### Tier 2 — paired base-vs-candidate. HARD. Provisional ±50 % sentinel.

Per §3. Applies to a **named subset** of the allowlist, `xml_loader_bench` included — it is #263's
own bench and the one with a demonstrated capacity to regress unnoticed.

**The band is 50 %, and it is explicitly provisional.** Derivation: above this repo's worst recorded
unpaired cross-runner spread (43 %), below #263's observed signal (60–90 %). Pairing removes the
cross-runner confound that produced the 43 %, so 50 % is probably far looser than necessary — but it
is the only number defensible without data. The **A-vs-A delta printed every run is the evidence
trail** for tightening it, and no tightening happens without those samples.

### Tier 3 — checked-in baselines. INFORMATIONAL, and honest about what it can't do.

`bench/ci-suite.txt` gains a third column giving each row's comparand disposition:

| disposition | meaning | rows |
|---|---|---|
| `gb-json` | genuine Google-Benchmark baseline; ±5 % delta reported per §VIII.2 | 2 |
| `none` | no machine comparand exists (hand-authored record, or empty) | 12 |

A `none` row is **never silently skipped** — it prints its disqualifier by name. A skipped row is how
a gate reads green on nothing.

### The allowlist — `bench/ci-suite.txt` is the ONE source of truth

⚠️ **An earlier revision of this record carried its own copy of the table and the
two drifted immediately** — the record said 14 binaries in one paragraph and
listed 19 in the next, and classified three rows `none` that the manifest called
`gb-json` (Codex round 2, F7). A second hand-maintained copy of a list is exactly
what the manifest's own header warns against, so the table is gone. The manifest
carries the per-row reason inline; this section carries only the rule and the
census, both machine-checkable against it.

**Selection rule: single-threaded, CPU-bound, no network, no disk write —
applied PER BINARY, on a reading of that binary's source.** The earlier
directory-level application was wrong and dropped deterministic hot paths for
their siblings' sins; `bench_pinset_snapshot_acquire`, `bench_pinset_find`,
`bench_async_mutex_uncontended`, `fix_time_bench`, `bench_memory_store`,
`fsm_bench`, `seqnum_bench`, `bench_compid_authorize` and
`offset_table_footprint_bench` were all restored on that basis.

Every exclusion now cites a source property rather than a directory:
`bench_async_mutex_contended` (contention is a scheduling measurement),
`bench_threading` (a concurrency benchmark), `bench_file_store` (real disk I/O),
`bench_verify_peer_in_envelope` (real handshake), the loopback-socket transport
benches, `log_spike` (a noise instrument by design), `log_enqueue_bench` (runs a
drain thread — *not*, as an earlier draft said, merely because its baseline is
debug), `heartbeat_bench` (an explicit scope placeholder), `check_alive_bench`
(its measured operation compiles to a no-op in release), and `placeholder_bench`
(measures nothing).

**Census, as it stands (derive, do not trust this line):**

```
grep -c '^bench/\|^bin/' bench/ci-suite.txt   # 23 binaries
awk '$3=="paired"' bench/ci-suite.txt | wc -l  # 5 tier-2 rows
```

23 binaries under tier 1; **5** under tier 2 (`xml_loader_bench`, `framer_bench`,
`parser_bench`, `writer_bench`, `validator_bench`); **6** carry a `gb-json`
tier-3 comparand and the remaining 17 a named `none:` disqualifier.

⚠️ **A `cpu_time` key is not schema validation.** `sync/async_mutex_baselines.json`
has one and is still a hand-authored record — its rows are `ceiling_ns` /
`ceiling_source` / `measured_ns` with no `real_time`, `time_unit` or `run_type`.
It was classified `gb-json` on that mistake and is now `none:`. For the same
reason the r2 claim of "12 genuine Google-Benchmark files" in §2b is **withdrawn**:
that count came from `has("cpu_time")`, which also admits `placeholder.json`
(null timings) and the async-mutex record. The 10-file zero-`cpu_time` count and
the 3 empty files are unaffected and stand.

---

## 5. Acceptance criteria — FIXED BEFORE ANY MEASUREMENT REPORTS

Per `.specify/ci241-coverage-ccache.md` §3, so no band can be fitted to the number it judges.

| # | criterion | if unmet |
|---|---|---|
| **AC-1** | the job prints `nproc`, CPU model, compiler version, and the Google-Benchmark `context` for every binary | **block** |
| **AC-2** | a probe makes the `bench` job **RED in a real CI run**, failing on the cell that names the defect. The probe is a **manifest row naming a binary that is not built** (tier-1 cell T1-3) — deterministic, one line, trivially reverted | **block** |
| **AC-3** | with the probe reverted, the same job is **GREEN** on otherwise-unmodified code | **block** |
| **AC-4** | `ci/test-bench-gate.sh` asserts every T1-1…T1-8 cell **and** the tier-2 comparison, and **each mutant reddens the one cell that names its defect** | **block** |
| **AC-5** | `bench` is in `tier1-required`'s `needs:` **and** its `== success` assertion loop, in the same commit | **block** |
| **AC-6** | `ci/test-tier1-python-policy.sh`'s `EXPECTED_NEEDS`, its M4 mutant literal, `CI_PIN_HARNESSES`, and a new echo-stub mutant for `ci/test-bench-gate.sh` are all updated, and `MUTANTS_DECLARED` matches the number run | **block** |
| **AC-7** | tier 2 prints the **A-vs-A** delta, and a run whose A-vs-A exceeds the band reports **UNINFORMATIVE**, not PASS | **block** |
| **AC-8** | the `bench` job's wall time stays **≤ 34 min** (the `coverage` job, Tier 1's current pole), with build time and benchmark time reported separately | **exceeded ⇒ explicit disposition required before merge, not a silent record** |

AC-2/AC-3 are the both-poles requirement, and AC-2 is proven on a **deterministic** cell — never on
the timing band, whose RED would be unreproducible and would prove nothing about the gate.

⚠️ **AC-2 was rewritten because its r2 form described the abandoned design** (Codex round 2, F6). It
said "blank a checked-in baseline". Under the pivot a checked-in baseline feeds **tier 3, which is
informational** — blanking one produces a printed `NOT COMPARED` line and a green job. The criterion
was unsatisfiable as written, and had it been left there the RED proof would have been attempted,
failed, and read as the gate being broken.

AC-8 replaces r1's "record, do not block". Codex F9 is right that the old wording permitted a 35–180
minute job (the step's `timeout-minutes: 180`) to satisfy every blocking criterion while materially
extending Tier-1 latency on the required path.

⚠️ **`workflow_dispatch` is the instrument for AC-2/AC-3 pre-label** — Tier 1 skips until both gate
labels land. It runs the workflow file from the dispatched ref and does **NOT** satisfy branch
protection: evidence, not gate signal. That it reaches `bench` at all follows from gate-precheck's
non-PR early-exit setting `proceed=true`; **confirm on a real dispatch rather than relying on that
reasoning.**

### 5a. Merge-base resolution — the silent-vacuity risk in tier 2

`github.event.pull_request.base.sha` is empty on `push:main`. A base SHA that silently resolves to
the candidate would build HEAD twice and **pass forever** — a tier-2-shaped false green. Disposition,
fixed here: tier 2 **requires** a base SHA that differs from HEAD; on `push:main` it uses `HEAD~1`,
and if no distinct base can be resolved it reports **SKIPPED-WITH-REASON and fails the step** rather
than comparing a tree with itself.

---

## 6. What this PR does NOT close, stated plainly

### 6a. ⚠️ The residual reach, stated as a number rather than left implicit

**A 49 % `xml_loader_bench` slowdown passes tier 2 by design**, and an arbitrarily large slowdown in
any of the 18 binaries not marked `paired` passes every timing axis. That is the honest cost of a
provisional band and a small tier-2 set. Codex round 2 (F11) is right that "provisional" becomes
permanent unless the promotion rule is written down now, so:

| promotion | criterion, fixed here | trigger |
|---|---|---|
| tighten the tier-2 band | after **20** `push:main` runs, set the band to `p95(observed A-vs-A and B-vs-B spread) × 2`, floored at 10 % and capped at the current 50 % — it may only ever narrow | a tracked issue filed with this PR |
| tighten the noise band | same sample, `p95 × 1.5`, floor 5 % | same issue |
| add a binary to tier 2 | its A-vs-A spread stays under the noise band across the same 20 runs | same issue |
| tier 3 → hard | requires a non-circular comparand, which requires #263 fixed | blocked on #263 |

The A-vs-A and B-vs-B figures the job prints every run are that evidence trail; the artifact upload
retains them for 14 days.

| left open | why | lands as |
|---|---|---|
| tightening the tier-2 band below 50 % | needs the 20 samples §6a specifies | follow-up issue, criteria fixed in §6a |
| tier 3 going hard at ±5 % | needs a non-circular comparand, which needs #263 fixed first | follow-up, blocked on #263 |
| re-seeding the 10 hand-authored baselines as real Google-Benchmark JSON | they are cited records; converting them is a separate, reviewable act | follow-up |
| the `compile_time_bench` 3 s ceiling and #209's per-class model | #209's own caveat — *"R² = 0.9996 on n = 4, one machine, one compiler should raise suspicion, not confidence"*; and §1b shows it is not the only pre-breached ceiling | #209 stays **open** for items 2/3/4 |
| the `XmlLoader::load` regression | this is the instrument, not the fix | #263 |

**Reach, honestly: tier 2 catches a #263-class regression introduced from now on. It does not catch
#263 itself**, because `main` is already regressed and is the base. Stated here rather than
discovered later.

---

## 7. Items of #209 addressed

| #209 item | disposition |
|---|---|
| 1 — gate inert, no CI job runs it | **fixed** for the runtime benches; compile-time ceiling deliberately **not** wired (§1a/§6) |
| 2 — flat 3 s ceiling not meaningful | **deferred**, #209 stays open |
| 3 — proposed per-class model | **deferred**, and #209's own n=4 caveat is the reason |
| 5 — `FINDINGS.md` cited, never existed | **fixed by re-pointing, not by authoring.** `bench/codegen/compile_time_bench/README.md` is titled *"T046 — Compile-time bench known findings (NFR-003-2)"* and already contains the v50sp2 record; the citation was misspelled, not missing. Three sites: the script ×2 and `bench/codegen/CMakeLists.txt:41`. |
| 6 — decide whether this runs in CI | **decided:** runtime benches yes (per-binary allowlist), compile-time ceiling no, with the exit criteria in §6 |
