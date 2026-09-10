# Quickstart — validating 089 end to end

**Purpose**: prove the feature works against a live QuickFIX peer, and prove each assertion can fail.
Not an implementation guide — see [contracts/](./contracts/) for interfaces and
[data-model.md](./data-model.md) for record shapes.

---

## ⛔ Step 0 — disk preflight. Every time. Before every build.

```bash
ci/disk-preflight.sh <preset>
```

**Do not skip this and do not substitute `df -h .`.** ⛔ **No readings are printed here** — they move, they
have already been falsified inside this bundle twice, and a stale number in a disk section re-arms the
exact instrument the section warns about. Run them:

```bash
df -k /                    # inside the WSL2 VHD — bounds what may be RESIDENT
df -k /mnt/e               # the Windows host drive backing it — bounds what the VHD may still GROW
du -sh build/*/            # per-tree occupancy, for sequencing and reclaim
df -k /mnt/wsl/fixppbuild  # a DIFFERENT device: ccache + the persisted evidence root. Outside both predicates
```

The **internal** reading is routinely the larger and more comfortable, and it is the one that will let you
start a build that cannot finish. **Both** bind, on different quantities, and the gate evaluates them
**independently** — see [contracts/disk-preflight.md](./contracts/disk-preflight.md).

⚠️ Part of the gap is already materialised on the host but free inside the VHD, so some writes may cost the
host nothing. **That is a bound, not a measurement** — do not spend it, do not compute it, and do not let
it into a threshold. R-1 measures the real host delta.

If the verdict is `reclaim-first` — which is offered **only** when the failing predicate is `internal`:

```bash
du -sh build/*/                   # re-derive the order NOW; it moves
rm -rf build/<cheapest useful preset>
```

⚠️ **No fixed order is written here.** The order that used to be was headed by `linux-clang-ubsan` purely
because that tree is currently unpopulated — and this feature's own matrix populates it. Re-derive, then
delete.

⛔ **Never `build/linux-clang-debug/`** — standing user rule, 2026-09-10. It is also the `normal`
configuration's build tree.

⚠️ **Every tree above is also a matrix configuration** (FR-021). Delete one only after its 8 cells have run
**and their evidence has been persisted outside the build tree**; otherwise the reclaim destroys a run the
matrix still owes.

⚠️ **A `host` predicate failure never yields `reclaim-first`.** Deleting inside WSL frees blocks for reuse
within the VHD and returns nothing to `E:\`, so reclaiming would change neither reading and you would
loop. That case is `stop`.

If the verdict is `stop` even after reclaim: **stop and report.** Do not start a partial build, and do
not record any resulting cell as a `pass` or a `skip`.

---

## Step 1 — prerequisites

| Need | Check | Note |
|---|---|---|
| Counterparty image | pull **by digest** | ⚠️ not `:latest` — FR-016b. The published image predates this feature and must be rebuilt first |
| QuickFIX-cpp counterparty binary + `libquickfix.so` | extracted from the image | needs `LD_LIBRARY_PATH` |
| QuickFIX-J shaded jar | extracted from the image | ⚠️ `interop-smoke.yml` does **not** extract it today — the QF-J cells need it |
| TLS fixtures | `phase-9-harness/tls/gen-interop-certs.sh` | generated at run time from fixpp's committed CA; never baked into the image |
| FIX 4.4 dictionary, peer side | `INTEROP_QFCPP_SPEC_DIR` for C++; **classpath** for Java | ⚠️ the two resolve differently — see research R-5 |

---

## Step 2 — the shortest real signal

Run **one** cell, one config, and read the result off the artifact rather than off an exit code:

```bash
python3 ../phase-9-harness/tools/run_interop_cell.py <cell-id> \
    --config normal --build-root "$PWD/build" --out /tmp/cell.yaml
```

Then confirm, in order:

1. **The peer announced itself.** The readback stream's first line is a `hello` carrying `engine_version`,
   `readback_protocol`, and the join keys `run_id` / `script_digest` / `config` / `counterparty_digest`.
   **No hello ⇒ the cell must have FAILED**, not skipped — and the failure text must not contain the token
   `unavailable:`, which the shim greps into a `skip:`.
1a. **BOTH streams carry a `hello` AND end with a `terminal` record.** ⚠️ A `hello` is written *before any
   message is processed*, so a peer that starts, announces itself and conversates not at all satisfies every
   field a hello-only check reads. `terminal_state: completed` on **both** streams is what corroborates a
   pass.
1b. **fixpp's hello attests the arm.** `has_validator` — read from the live session via
   `Session::has_validator_for_test()`, not from the `SessionConfig` the driver constructed — must equal
   `arm == "validation-on"`. ⚠️ *"A production dictionary is loaded"* is **not** this evidence: the
   dictionary is on in **both** arms, so that predicate's population equals its complement.
1c. **Both directions produced BOTH record kinds.** Each side emits a `sent` record for what it built and a
   `readback` record for what it parsed. A direction carrying only one of the two has no comparison — the
   peer's `sent` record is the only source for values the peer generates at run time (`OrderID`, `ExecID`).
2. **`dictionary_enabled` is true** for a cell whose witnesses include repeating groups. Under
   `UseDataDictionary=N` both engines flatten groups, so a group-free readback would compare clean
   against a group-free intent — a false green.
3. **Witnesses exist for both directions**, including the fixpp-acceptor direction that today asserts
   only counters.
4. **Promote the run, then check the manifest.** Corroboration happens where the artifacts are:

```bash
python3 ../phase-9-harness/tools/promote_interop_evidence.py \
    --run-dir <run directory> \
    --evidence-root "${FIXPP_INTEROP_EVIDENCE_ROOT:-/mnt/wsl/fixppbuild/interop-evidence}" \
    --ledger tests/interop/witness_evidence.yaml \
    --manifest tests/interop/cell_results.yaml
```

   The promotion step opens both streams, requires a `hello` **and** a `terminal` on each, checks the join
   keys, evaluates the completeness gate, copies the bundle under the evidence root and records
   `evidence_digest`. The committed manifest then carries a row naming that ledger entry and **no path**.
   ⚠️ **The schema check opens nothing** — it is a ctest three CI tiers run on hosted runners that hold no
   run artifacts at all, against a committed manifest holding 59 pre-existing `pass` rows. Asking it to
   open `artifact_path` fails everywhere; that is why the manifest and the ledger are two artifacts.

---

## Step 3 — the 32-run matrix, in sequence

⚠️ **Build the interop driver targets ONLY — never `all`.** `run_interop_cell.py` builds nothing; it
expects a pre-built tree and runs one named gtest binary per cell, and the cells name **four** binaries. A
full build of a preset produces hundreds of executables the matrix never opens. Re-derive the lever if you
need to argue about it (`ls build/<preset>/bin | wc -l`, `du -sh build/<preset>/bin`); do not quote a
figure from a document.

⚠️ **Four FULL builds do not fit; four TARGETED builds do** — that is the condition. Derive it, do not read
it: sum `du -sh build/*/`, scaling any unpopulated tree to a populated sibling's object count first, and
compare with `df -k /`. And fitting inside the VHD is necessary, not sufficient: only the already
-materialised pool *may* be reusable (a bound, not a measurement), beyond which every byte costs host growth
against `df -k /mnt/e` alone. Run in sequence, heaviest trees one at a time:

```
for config in normal ubsan asan tsan:      # normal = build/linux-clang-debug, never reclaimed
    ci/disk-preflight.sh <preset>          → must say proceed (BOTH predicates)
    build ONLY the interop driver targets for that config
    run its 8 cells                        (4 role×flavour × 2 validation arms)
    PERSIST results + evidence outside the build tree     ← before reclaim
    reclaim that config                    ← ONLY after its runs are persisted; never `normal`
```

⚠️ **`ubsan` is a matrix arm now, not just the cheapest thing to delete.** It appears at the head of the
reclaim order *and* in the run order; those are two different orderings over the same four trees. Deleting
it before its 8 cells have run destroys an arm the matrix still owes.

⛔ **Do not read `build/linux-clang-ubsan/`'s small size as "the ubsan config is cheap".** That tree is
**unpopulated** — a fraction of its siblings' object count. Reclaiming it frees almost nothing, and a
*full* ubsan build is comparable to the others once scaled. **A size is a claim about a directory, not
about a build.** The targeted build unit is what makes it affordable.

⚠️ **A targeted sanitizer build covers exactly the paths these cells exercise — say so when reporting it.**
It is not repo-wide sanitizer coverage (FR-023b); Tier 1 runs full suites under sanitizers separately.

⚠️ **Persist before reclaim.** Deleting a build tree that still holds the only copy of a run's evidence
loses the run.

⚠️ **Evidence accumulates; it is not rewritten per config.** The completeness gate projects each
configuration's **authoritative** rows onto the completeness key `(cell_id, script_step_id, direction,
occurrence)` and asserts every configuration's set equals every other's **and** equals the census's **100**
keys — **across `config`, never across `arm`** (`arm` is inside `cell_id`, so a cross-arm equality is
unsatisfiable by construction). It *also* runs over the union after the last config. **The union reading
alone is not a check**: with no `config` on the witness row, a configuration producing zero witnesses
leaves the union unchanged and the gate passes. And a key without `cell_id` is blind the same way on the
role×flavour axis.

**TSan is bring-up, not a config flip.** It has never been run on the paired live matrix. A TSan arm that
skips, dies in setup, or produces no witnesses is a **failure** (FR-022), and every config must produce the
**same witness set** as the `normal` arm (SC-011).

**`ubsan` is the arm whose absence made the previous Article IX §2 claim false.** The retired `asan-ubsan`
config mapped to `linux-clang-asan`, which enables ASan only — so no UBSan ever ran on this surface. Run it
early to land the constitutional coverage first — **not** because it is cheaper than the others; under the
targeted build unit every configuration costs about the same.

---

## Step 4 — prove the instruments can fail

A green run proves nothing until each assertion has been shown RED. **Two kinds of arm, and one does not
substitute for the other**:

- a **forced miss** proves the guard *can* fire;
- a **spurious hit** is *an arm that makes the guard report PASS for a reason other than the property it
  claims to measure* (FR-018) — it asks *what else could satisfy the condition the guard is watching*.

⚠️ Six of the arms below were absent from this table while the contracts that mandate them said *"needs a
witness"*, and the arms this bundle labelled *spurious hit* were second flavours of forced miss. The table
is now derived from the contracts' own witness lists, not from memory.

### Forced-miss arms

| Force | Expect | Mandated by |
|---|---|---|
| A wrong field value in one sent message | that witness RED, naming the path and `value_mismatch` | FR-006 |
| Omit a required field | RED, classified `missing` | FR-006 |
| Add a field the sent record does not declare | RED, classified `spurious` — ⚠️ the class a subset comparison cannot see | FR-006 |
| A message the peer should Reject | the cell fails; ⚠️ tolerate QuickFIX-cpp *disconnecting* instead of rejecting — only QF-J 3.0.1 is confirmed to `Reject(35=3)` | FR-017 |
| **Counterparty with the readback channel removed** | **every** affected cell RED — zero pass, zero skip (SC-005a). Jointly exercises FR-016a and FR-016c. ⚠️ This is a forced **miss** — it forces the *absence* of records. It was previously labelled the spurious-hit arm; the proof obligation is unchanged, the label was wrong | FR-016c · FR-018a |
| A peer announcing a `readback_protocol` **older** than the cell requires | cell FAILS — not skip, not pass | C-2 · FR-016a |
| A peer announcing **no hello at all** | cell FAILS. ⚠️ Assert the failure text does **not** contain `unavailable:` — that token is grepped into `skip:` | C-1 · FR-016a · FR-024 |
| A cell with `dictionary_enabled == false` whose witnesses include groups | cell FAILS | data-model §1 |
| Group instances emitted as a bare `NoXxx` count, no members | RED — a count with no member at that path is malformed | C-3 · C-4 |
| Enumeration driven by `getGroups(tag)` per field (QFJ) | RED — the walk **mutates** the message it observes | C-5 |
| A value containing each escape class — `"`, `\`, a byte `< 0x20`, multi-byte UTF-8, and a **non-UTF-8** sequence — from **both** writers | the consumer parses all of them; the non-UTF-8 value arrives as `value_b64` | FR-025 · escaping clause |
| The **cross-language golden fixture** — one consumer, both producers, incl. **tag 1156**, built by invoking each emitter directly on a constructed message (a live cell cannot carry 1156: it is not a FIX 4.4 field) | both producers' output is **byte-identical** — not merely "parses to the same records". Entries sorted by canonical parsed-path order, so neither engine's walk order leaks in. **Tag 1156 is HEADER and appears in NEITHER producer's `fields`** | C-7 · C-6 · R-10 |
| A field entry emitted in the **engine's walk order** rather than sorted by canonical parsed path | C-7 RED — the two producers' bytes diverge | C-7 · § Canonical form |
| An emitter that classifies **tag 1156** by its **own** built-in list (QuickFIX-cpp includes it in `fields`) | C-6 RED — the canonical partition is union-of-both-built-ins ∪ the dictionary `<header>` block, and 1156 is header on both | C-6 |
| The readback file opened in **append** mode across two runs | RED — stale records from run *n−1* must not satisfy run *n* | R-4 |
| A falsified `cell_results.yaml` row — `pass`, no evidence | schema check RED (E-1) | FR-014 |
| `pass` naming a **ledger entry that does not exist** | schema check RED | FR-014 · E-1 |
| A ledger `witness_count` differing from the census figure for that slot | schema check RED | FR-014 · E-1 |
| A promoted bundle whose stream's `hello` carries a **different `run_id`** from the row | promotion RED — *because the stream disagrees*. ⚠️ The fixture that separates presence from corroboration | FR-013b · FR-014 · E-1b |
| A slot with **no** authoritative run | schema check RED — the 32-slot inventory equality, not containment | W-3b |
| A replayed step delivered once where the census declares **two** occurrences | RED as **`occurrence_count_mismatch`**, not as a field-level `missing` | FR-005 · W-7 |
| **The 59 pre-existing `pass` rows, unmodified** | the schema check stays **GREEN** — the control proving the conditional-field rule did not break FR-020 | FR-020 |
| An `ENOSPC` abort recorded as `fail` | schema check RED — it must be `error:enospc` | FR-014a · E-5 |
| Delete one witness | completeness gate RED (W-3) | FR-015b |
| Delete a business step from the script | the census's 100 keys disagree with the script-derived projection ⇒ RED | FR-015d · W-2a |
| Add a script step that produces no witness | census disagrees ⇒ RED, in the other direction | FR-015d · W-2a |
| **Re-derive W-2a's expected population from the script file instead of from the census table** | both arms above go GREEN — the derivation agrees with itself. ⚠️ Assert this: it is the check that the census is a **second opinion** and not a second reading of the same source | FR-015d · W-2a |
| `ci/disk-preflight.sh` arms A-1, A-1a, A-3, A-5, A-5a, A-6 | see [contracts/disk-preflight.md](./contracts/disk-preflight.md) | — |

### Spurious-hit arms — the guard reports PASS for the wrong reason

⛔ **An arm is not written until its OBSERVABLE exists.** Two of the arms below were previously listed with
no observable at all — their forced defect produced **no difference the guard could see**, so they measured
their own setup and stayed green. The middle column now names *what differs*.

| Force | **The observable — what differs** | Expect | Mandated by |
|---|---|---|---|
| Derive the `sent` record's `fields` **from the serialized frame**, **and** mutate the frame after intent capture | A test-only hook rewrites `Account(1)` from `ACCT0001` to `ACCT0009` in the outbound **`B-03`** frame, after stage-1 capture and before transmission — **same length**, `CheckSum(10)` recomputed, `BodyLength(9)` unchanged, dictionary-declared for 35=F, and nothing branches on it. ⚠️ **`B-03`, not `B-01`**: `B-03` has exactly one declared occurrence on all four combos, while `B-01` carries a replay at occurrence `1` on C3/C4 whose stored bytes may be pre- or post-mutation — which would make this arm's expected observable ambiguous | **builder-derived ⇒ RED, `value_mismatch` on path `1`, sent `ACCT0001` / readback `ACCT0009`. Frame-derived ⇒ GREEN.** Assert **both halves**; the second is what makes it discriminating. ⚠️ Without the mutation an unmutated serializer round-trips to the same field set and the two derivations are indistinguishable — that version of this arm *is* the defect | FR-018 · data-model §3 · C-8 |
| Bypass the peer's **typed accessor**, filling `typed_reads` from generic enumeration | **A typed-accessor invocation seam, recorded through the accessor's own return.** Every `typed_reads` entry carries `accessor: "<Message>::<getter>"` **and** `accessor_witness`, a value the counterparty can obtain ONLY by holding the object the generated typed getter returned (e.g. the getter's out-parameter `FieldBase`, reported as its `getTag()`/`getValue()` pair read back off that object). The seam sits **on the call path, through the accessor's result** — never beside it. ⚠️ `fix_type` is retained as evidence of dictionary-backed resolution, but it is explicitly **NOT** accessor provenance: the counterparty already holds bytes + dictionary + script, so every typed *output value* is derivable without invoking the getter, and a bypass emitting identical `path`/`value`/`fix_type` would make the comparator agree for the wrong reason. Only a value routed **through the returned object** distinguishes them. | RED naming the path. ⚠️ **A decimal-spelling observable was tried and REJECTED on verified evidence**: it needed `Price(44) = 100.1000`'s trailing zeros to reach the wire, and fixpp's decimal writer **strips trailing fractional zeros** (`src/core/decimal.cpp`, `to_chars`, step *AC-S4*). Both paths would have read `100.1` and the arm would have been **inert while looking green** | FR-003b · FR-018 |
| **Empty intent vs empty readback** | the comparator is handed `∅` on both sides | it **rejects**; it must not pass on `∅ == ∅` | FR-018 · FR-016c |
| **Run one combo of one config and none of the other three** | that config's projection π is missing 12–13 of the census's 100 keys while the other three are complete | completeness gate RED. ⚠️ **This is the arm that discriminates the completeness key from `(arm, step, direction, occurrence)`** — dropping a whole *config* does not, because a config-blind gate and a combo-blind gate both redden on that one | FR-015c · W-3a · N-1 |
| **Drop one whole configuration** from the matrix | that config contributes zero keys; the union is unchanged | completeness gate RED. ⚠️ Deleting a single witness row passes under a union-only gate, so it cannot tell a correct gate from a config-blind one | FR-015c · W-3a · SC-009 |
| **A second (retry) run for one `(cell_id, config)` slot** | two runs claim one slot; because π drops `run_id` the duplicate rows **collapse**, so the completeness gate alone sees nothing | W-3c RED — and assert the completeness gate alone stays GREEN, which is the point | FR-015c · W-3c |
| **Launch a `validation-on` cell with `validate_inbound_messages` forced false** | fixpp's hello carries `has_validator: false` while the row claims `arm: validation-on` | RED. ⚠️ FR-011's *"a dictionary is loaded"* is true in **both** arms and produces no observable here — the entire US4 deliverable goes vacuously green without this arm | FR-011a · SC-009c |
| **A stream carrying a `hello` and no `terminal` record** | the pre-conversation hello is present and every field it carries checks out; nothing attests a conversation happened | promotion RED (E-1b) | FR-014 · FR-014a · C-10 |
| Seed a message the dictionary should reject; run **both** validation arms as a `validator-positive-control` run | the two arms' accepted-message sets differ, and the validation pair reports `diverged` against `expected_verdict: diverged` | the arms **diverge**, naming the message and the validator's objection. ⚠️ Identical sets are exactly what a **dead validator** produces. ⚠️ This is a **separate run outside the 32-slot inventory** — seeding it into the normal script would fail FR-010 on all 32 by construction | FR-010a · SC-004a |
| Disk gate: threshold **unset or unparseable** with both mounts nearly full | `proceed` is true while the predicates measure nothing | RED (arm A-7) | D-9 |
| Disk gate: host mount **resolves to the build mount** | the host predicate is satisfied by the build reading | RED (arm A-8) | D-10 |

### Controls — arms that must stay GREEN

| Force | Expect | Mandated by |
|---|---|---|
| `ci/disk-preflight.sh` A-2 (both comfortable) | GREEN, with both readings, both required values and their dates printed | D-6 |
| `ci/disk-preflight.sh` A-4 (not WSL, no host mount) | GREEN via **D-4** — proves A-3 is not just "no host mount ⇒ fail" | D-4 |

**Every RED cell must assert its OWN diagnostic, not merely a non-zero exit.** In PR #255 two cells
reddened via a *different* check than the one they were written for and stayed green under a mutation
that reverted the rule they were meant to pin.

⚠️ **Before believing any of these arms, prove the instrument can report non-zero** — run each against the
unmutated tree and confirm it is GREEN there, so a RED is attributable to the mutation and not to the
harness being broken in a way that reddens everything.

---

## What success does NOT mean

This feature closes **zero catalogue rows**. The narrow set — 35=D/8/F/G/9 — is `A-001`, `A-003`,
`A-004`, `A-006`, `A-007`, **all already `done`**. Success is that the fidelity machinery exists, is
proven able to fail, and emits evidence a catalogue flip can stand on. The 10 QuickFIX-route rows close
in the breadth follow-on; the 31 FIX-Latest rows close under row 4b's differential baseline.
