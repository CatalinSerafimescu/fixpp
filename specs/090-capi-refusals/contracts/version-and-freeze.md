# Contract: the version contract — C-ABI **1.6 → 1.7**, MINOR marked **BREAKING**

**Track**: C-ABI. **Authority**: `[const §X.7]`, the **pre-first-release** breaking-change clause.
**Requirements**: FR-014, FR-015, FR-016, FR-017, FR-018.
**Criteria**: SC-010, SC-011, SC-012, SC-013.

---

## 1. The contract in one sentence

> **`FIXPP_C_ABI_VERSION_MINOR` moves 6 → 7 — a MINOR bump, marked BREAKING, with NO constitutional
> amendment** — and that combination is `[const §X.7]`'s prescribed procedure, not a deviation from
> it.

⚠️ **The procedure and the definition come from two different documents, and swapping them is an
error.** `.specify/api-contract.md` §11 supplies the **definition** of a C-ABI breaking change —
*"Making a call to a Stable-from-v1.0 C-ABI symbol fail where it used to succeed"* — and then routes
the **procedure** away from its own consequences: *"`[const §X.7]` also uses the C-ABI effects below
to define a C-ABI breaking change before fixpp's first public release; in that period the consequence
is §X.7's (a MINOR bump marked BREAKING, no amendment), not (a) and (b) below."* **Citing §11 for
the procedure would be wrong.**

⚠️ **The premise — that the first public release has not happened — is MEASURED, not assumed.**
`[const §X.7]` names its own test: *"`gh release list --exclude-drafts` shows whether it has
happened."* **Re-derive it, and keep the control**, because *an empty listing and a broken invocation
both print nothing*:

```
gh repo view --json nameWithOwner -q .nameWithOwner
gh release list --exclude-drafts                      # the repository under change
gh release list --exclude-drafts --repo cli/cli       # POSITIVE CONTROL: proves the invocation can print rows
```

**One bump carries all three refusals, and they ride ONE PR.** The ground for one PR is
`[const §X.7]` **obligation 3**, not convenience: it requires every in-repository consumer updated
*"in the same PR"* as the breaking change, and with three breaking changes sharing one consumer
inventory a split would either duplicate that inventory or leave one PR's consumers unupdated at its
own merge. The bump, the version comment, the freeze re-baselines and the owner-document amendment
are **shared infrastructure**; splitting duplicates them or makes a later PR depend on an unmerged
earlier one.

---

## 2. `[const §X.7]`'s four obligations, and where each is discharged

Quoted verbatim.

| # | obligation | discharged |
|---|---|---|
| 1 | *"bumps `FIXPP_C_ABI_VERSION_MINOR`, not MAJOR, so the error-code downgrade frame of §4 stays continuous"* | §3 |
| 2 | *"is marked **BREAKING** in the documentation of each affected declaration (in the version comment of `version.h` where no declaration carries the change), **in the PR description**, and in the behaviors-and-limitations delta"* | §4 — **three limbs**, and the middle one is the one that gets dropped |
| 3 | *"updates every in-repository consumer (the Python binding, tests, examples, interop harnesses) in the same PR"* | §6 |
| 4 | *"remains subject to §1 review and to all four Appendix A controls"* | the design authority is the `[const §X.1]` Gate A; ⚠️ **it RAN and did NOT converge** — `gate-a-waived` on two reasons. The other three controls are all discharged, in three different modes: `/speckit-analyze` ✅ **DISCHARGED 2026-09-20**, run through the canonical `spec-analyzer` executor and **PINNED to the artifacts at `505adafa`** (**1 finding (F1, multi-site), 0 CRITICAL, 100 % coverage**; F1 **remediated at `e9833610`, not deferred**) — ⚠️ **a pin, not an unconditional discharge: it does not cover a later artifact state, which is why `tasks.md`'s T084 re-runs it**; the user's `/plan` sign-off ✅ **DISCHARGED 2026-09-20** (pinned to `plan.md` at `d12d2270`); and `/clarify` discharged **in substance, by hand**, not by running the skill — recorded as **adapted, not as run**. ⚠️ **This cell said BOTH of the first two were owed until 2026-09-20** — true when written, falsified by the sign-off, and missed by a repair sweep whose grep pattern was too narrow to reach this phrasing; that history is kept because it is why the sweep that reached it exists. ⚠️ **All four discharged is still NOT a clean pass** — Gate A's is a two-reason WAIVER |

---

## 3. Obligation 1 — the pin population, tiered by what catches a miss

### Tier 1 — a build, a test or a CI gate catches it

| # | pin | note |
|---|---|---|
| 1 | `include/fix/c_api/version.h` — `#define FIXPP_C_ABI_VERSION_MINOR 6 /* 1.6: the Length+Data setters (fixpp#428) */` | ⚠️ **the trailing comment is part of the pin** and must be **re-authored**, not merely renumbered — naming these three issues |
| 2 | `tests/capi/version_test.cpp` — ⚠️ **two cells, not one**, and the **CONDITION** is what survives if a third appears: *a cell whose ASSERTION or whose TEST NAME encodes the version literal*. Today that is `CapiVersion.CApiVersionIsExactly_1_6_0` (its **name** plus its major/minor/patch expectations) and `CapiVersion.CompositeMacroValue`'s second assertion against the composite literal. The file's other cells compare against the **macros** and move automatically | ⚠️ **none of these fails at BUILD time** — `grep -c "static_assert" tests/capi/version_test.cpp` → **0**. They fail at **ctest** time. ⚠️ A sweep that fixes one and misses the other is the failure this row exists to prevent |
| 3 | `tools/capi_freeze.sha256` | §5 — **the coupling a reviewer bites on** |
| 4 | `src/capi/error.cpp`'s `introducing_minor()` | **no change** — no code is minted. Recorded so the absence is a **decision**, not an omission |
| 5 | `tools/abi_history/error_codes_v1.txt` | **no change**, same reason |
| 6 | `tests/abi/golden/fixpp_capi_symbols.txt` | **no change** — no symbol added, removed or re-signed. **Check, do not claim**: re-run `.github/workflows/abi-golden.yml`'s own `nm --defined-only --extern-only` diff step |

⚠️ **A renamed gtest can leave a stale `ci/expected-preset-conditional-tests.txt` entry** — which
reds a CI script-pins check. Check that file **before pushing**, because the rename in row 2 is
mandatory.

### Tier 2 — prose no gate catches

⚠️ **`src/capi/version.cpp`'s header comment says the macros are `(1/5/0)` and is ALREADY STALE at
6** — direct in-tree evidence that the prose pins are not maintained. **Do not treat the absence of a
gate failure there as evidence the prose pins are current.** Also `version.h`'s own narrative (*"MINOR
is PRESERVED at 5 across the freeze"*, *"Future MINORs within major 1 (1.6, 1.7, ...) just
continue"*).

⚠️ **Do NOT re-date fixpp#428's markings.** `include/fix/c_api/message.h`'s existing `(1.6,
BREAKING)` / `Since 1.6` mentions and the `C-ABI 1.6` banners in `src/capi/message_write.cpp` and
`tests/capi/message_write_test.cpp` correctly date **#428**. Re-dating them to 1.7 would **falsify
history**. They stay — but any *other* edit in `message.h` still re-triggers its freeze hash (§5).

### Tier 3 — verified non-pins, left alone

- The Python binding **moves automatically**: `bindings/python/fixpp.i` re-exports the macro, and
  `bindings/python/tests/wheel/test_import_surface.py` asserts only that the **name**
  `"C_ABI_VERSION_MINOR"` is exported, with **no value assertion**.
- `CMakeLists.txt`'s project `VERSION` is a **separate version space**; no `SOVERSION` pins the C ABI.
- `tests/capi/error_live_test.cpp`'s hardcoded `consumer_minor` is a **downgrade-mechanism fixture**,
  not a current-version pin. **Leave it.**

**Declared exclusions from the sweep**, stated so the sweep is auditable: `specs/` (closed bundles
narrating past bumps — none pins the current value and none is consumed by a build or a gate) and
`build/`. ⚠️ **A bare `\b1\.6\b` regex false-positives on reconnect schedules, variance percentages
and tag pairs**, which is why the sweep is **per-artefact rather than one regex**.

---

## 4. Obligation 2 — the BREAKING marking, in THREE places

### 4.1 Limb 1 — each affected declaration

| header | declaration | marking |
|---|---|---|
| `include/fix/c_api/message.h` | `fixpp_msg_remove_tag` | **BREAKING** — [`msg-remove-tag.md`](./msg-remove-tag.md) §6 |
| `include/fix/c_api/message.h` | `fixpp_msg_clone` | **BREAKING** on **two** limbs — [`msg-clone.md`](./msg-clone.md) §1 |
| `include/fix/c_api/session.h` | `fixpp_session_config_set_comp_ids` | **BREAKING** — [`session-config-byte-floor.md`](./session-config-byte-floor.md) |
| `include/fix/c_api/session.h` | `fixpp_session_config_set_begin_string` | **BREAKING** — same |
| `include/fix/c_api/version.h` | the `FIXPP_C_ABI_VERSION_MINOR` version comment | `[const §X.7]` names this for the case where *"no declaration carries the change"* |
| `include/fix/c_api/message.h` | the index-bounds population | ⚠️ **NOT marked BREAKING** — [`msg-index-bounds.md`](./msg-index-bounds.md) §1 |

**The in-tree marking precedent is fixpp#428's**, in `message.h`: `-- (1.6, BREAKING) value holds
SOH (0x01) and ...` and `Since 1.6 (BREAKING), ...`. ⚠️ **Follow that spelling; do not invent a
second one.**

### 4.2 ⚠️ Limb 2 — the PR DESCRIPTION, which is the limb that gets dropped

`[const §X.7]` requires the marking *"in the PR description"*. **It is checkable at Gate B, and this
repository has already lost a gate to a marking that was present in **bold** but not as a heading.**

> **The PR body MUST carry, as its own `##` HEADING, a BREAKING section naming all three changes in
> fixpp#428's in-tree spelling** — one line each for `fixpp_msg_remove_tag`'s builder-open refusal,
> `fixpp_msg_clone`'s failed-re-parse refusal **and its abort limb**, and the two session-config
> setters' byte floor.

⚠️ **A heading, not bold text.** ⚠️ **Do not invent a second spelling** — #428's is the in-tree
precedent that §4.1 already binds the headers to, and the body must match it.

### 4.3 Limb 3 — the behaviors-and-limitations delta

**Four BEHAVIOUR rows, all in the LIVE `spec/behaviors-and-limitations.md`, and NO limitation row
**for the three refusals** — plus one limitation row apiece for the two out-of-scope residuals below
(owner decision, 2026-09-21).

| row | track | content |
|---|---|---|
| **B-447-1** | C-ABI, BREAKING | `fixpp_msg_remove_tag` refuses with `FIXPP_ERR_INVALID_HANDLE` while any group builder is open, and erases nothing. Names the two failure modes, the one-line migration, **and the two refused-but-safe classes** — the guard's **true width** |
| **B-458-1** | C-ABI, BREAKING | `fixpp_msg_clone` refuses a failed dict-backed re-parse. Lists the three codes and **references L-049-2** for the OOM route surfacing as `FIXPP_ERR_UNKNOWN` — reference, not restate. ⚠️ **Also declares the abort limb**, and states plainly that **its trigger set is not enumerated** |
| **B-458-2** | `[C++ track]`, BREAKING for a direct C++ caller | the reify factory materialises eagerly and returns `unexpected` on the **one** new failure class. States **exactly what is NOT covered** — the dict-free degrade and the frames-to-nothing arm. **Declares no test rewrite.** A separate row from B-458-1 because the two halves ride different tracks |
| **B-452-1** | C-ABI, BREAKING | the two setters and `Session::open` refuse any byte `< 0x20` or `'='` in CompIDs, BeginString and SupportedMsgTypes' RefMsgType |

⚠️ **`L-452-1` is DELETED, not narrowed.** A limitation nothing can observe is removed from the
delta, not softened. See [`session-config-byte-floor.md`](./session-config-byte-floor.md) §7.

⚠️ **The live file is the authority.** Resolved rows are moved out to the closed file and stay
citable, so a repo-wide grep across the pair reports resolved limitations as open. **Read the live
file; do not grep across the pair.**

Plus the two **out-of-scope residuals** the design authority records — the probe-cap degradation and
RefMsgType(372) at the two reject builders. **They are limitations if they are written down and
defects found at Gate B if they are not** — so each is written down as **its own limitation row** in
the same live ledger. ⚠️ *"No limitation row"* above bounds the rows **for the three refusals**; it
never meant the residuals go unrecorded. ⚠️ **A third residual surfaced at `/simplify` (2026-09-22) and is recorded the same way:** **L-458-2**, a source parsed under a raised `OffsetTable` entry cap cannot be cloned or reified because the copy re-parses under the default cap (C++ track only; tracked as **fixpp#493**).

---

## 5. ⚠️ The freeze coupling — and the causal chain is NOT the version bump

`tools/capi_freeze.sha256` is a `sha256sum -c` manifest over the C-ABI headers, enforced by
`tools/check_capi_freeze.sh` from the tier-1 workflow. **It is byte-level, comments included.**

**Three headers re-baseline, for TWO different reasons:**

| header | why |
|---|---|
| `include/fix/c_api/version.h` | **its macro moves** |
| `include/fix/c_api/session.h` | ⚠️ **because obligation 2 requires editing the doc comments IN THAT FILE** — not because of the bump |
| `include/fix/c_api/message.h` | ⚠️ **same** — and additionally because the index-bounds population's declarations live there |

**A comment edit is a byte edit, and the freeze hashes bytes.** ⚠️ **Writing "the bump re-baselines
three headers" is wrong and contradicts the design authority.** Expect this collision **at the start
of implementation**; do not discover it at the end.

⚠️ **The gate must be observed in BOTH states** (SC-012): **FAILING** after the header edits, and
**PASSING** after the re-baseline. **A green result alone is consistent with a re-baseline applied
before the edits**, which is a green gate over unchanged hashes. See
[`../quickstart.md`](../quickstart.md) V8.

---

## 6. Obligation 3 — every in-repository consumer, in the SAME PR

The obligation is verbatim: *"updates every in-repository consumer (the Python binding, tests,
examples, interop harnesses) in the same PR"*. **This is the binding ground for ONE PR** (§1).

⚠️ **Derive the consumer population; do not read one from a list.** The condition: *anything in this
repository that calls one of the four changed symbols, or that pins a version value the bump moves.*
Two shapes need special care, both recorded by the design authority:

- **A consumer that uses a refused call as SETUP, not as the behaviour under test.** A test that
  calls `fixpp_msg_remove_tag` merely to arrange a fixture is still broken by the refusal if a
  builder happens to be open. The scoping is D-1's **true width** (both refused-but-safe classes),
  not a narrower reading.
- **The Python binding**, which needs both the refusal witness and its **control** — a cell whose
  failure text must stay **distinguishable** from the existing embedded-NUL marshalling rejection,
  or the new cell is measuring marshalling again.

---

## 7. What is explicitly UNCHANGED by the bump

- **`FIXPP_C_ABI_VERSION_MAJOR`** stays 1; **`_PATCH`** stays 0.
- **The error-code downgrade frame.** Obligation 1's whole point: MINOR, not MAJOR, so §4's frame
  **stays continuous**. `introducing_minor()` gains no arm and `error_codes_v1.txt` gains no line,
  because **nothing is minted**.
- **The exported symbol set.** `tests/abi/golden/fixpp_capi_symbols.txt` is expected
  **byte-unchanged**; `fixpp_capi.map` is unchanged.
- **Every other header in the freeze manifest** — only three re-baseline (§5).
- **No constitutional amendment.** That is the pre-first-release clause's explicit consequence.
- **fixpp#428's 1.6 markings and banners** (§3, Tier 2).

---

## 8. The owner-document amendment that rides this PR (FR-019 / FR-020)

The C-ABI owner document contains passages **binding `FIXPP_ERR_CAPI_CONFIG_INVALID` to a producer
set** — a clause false of essentially all of its direct producers as literally written, including
the very symbol that document uses to *define* the steady side.

**The membership CONDITION, which is what survives — the count does not:**

> **Every passage that BINDS the code to a producer set is amended; a passage that merely mentions
> it in a count, a changelog or a hedged list is not.**

⚠️ **The population figure has been wrong more than once, in both directions, and is deliberately
not repeated here.** Derive it from the condition. The amendment is **prose-only**: it **reds no
pin**, **mints nothing**, and **touches no source**. Leaving any subset live means the document
**still contradicts itself after a PR that claimed to fix exactly that**.

**This PR CLOSES fixpp#488** — its own *"Suggested fix"* is the amendment this PR performs. The
**producer RE-POINTING** question is a different issue and stays open, as does whether the
steady/construction split should acquire the source-level construct it prescribes. ⚠️ **Deleting the
assertion that drift is caught is NOT the same as closing the gap** — this PR owns only that the
owner document stops claiming a gate that does not exist.

**Two derived restatements outside that document** are amended alongside it (FR-020), so that no
live passage anywhere contradicts the published declarations after this PR (SC-014).
