# Quickstart: validating the three C-ABI refusals (090)

⚠️ **This is a VALIDATION guide, not an implementation guide.** It contains no function bodies, no
test suites and no migration recipes. It says, per refusal: what must be true before you can
observe it, what to build, what to run, and **what outcome proves the thing actually works** — with
the wrong-reason failure named beside it. Some sibling bundles' quickstarts are written as worked C
examples; this one deliberately is not, because what this feature ships is *refusals*, and a refusal
is validated by observation, not by an example call.

**Design authority**: `.specify/447-458-452-capi-refusals.md` **v0.10**.
**Surfaces**: [`contracts/`](./contracts/). **Invariants and states**: [`data-model.md`](./data-model.md).
This file **references** them and does not duplicate them.

⚠️ **Gate A RAN and did NOT converge** — `gate-a-waived` on two reasons. `/speckit-analyze` and the
user's `/plan` sign-off are **OWED**. Nothing below asserts a Gate A pass.

⚠️ **`[const §XVII.7]` resource gate:** local builds are resource-heavy; an AI agent MUST surface an
`AskUserQuestion` and get approval **before** running any build command in this file.

---

## 0. The three rules every scenario below obeys

Read these once; they are why each scenario names more than one command.

1. ⚠️ **A clean sanitizer run is NOT the instrument for the `#447` scenarios.** The outbound
   accumulator carves from a per-message monotonic arena, so an overrun lands **inside a live
   allocation**; ASan does not annotate a `std::pmr::vector`; UBSan's `bounds` covers fixed-size
   arrays only; and libstdc++/libc++ hardening is off in every preset. **A green ASan run is the
   expected output of an instrument that could not report otherwise.** The instrument is the
   **committed byte string**. Derivation: [`data-model.md`](./data-model.md) §1.6.
2. ⚠️ **A forced-MISS arm cannot catch a spurious HIT.** Every refusal below is also produced by
   some *other* condition — a null handle, a dead handle, a post-`fixpp_engine_start`
   `fixpp_session_open`, a marshalling rejection. **Assert the exact code from the call under test,
   in a state where no confusable producer is live, and pair it with a control showing that the
   injected condition is what produced it.**
3. ⚠️ **Where a two-state observation is specified, BOTH states must be seen.** A single green is
   consistent with the wrong sequence. This applies to the freeze gate (V8) and to every mutation
   listed below.

---

## 1. Prerequisites

**Repository**: the library submodule. All commands assume cwd is that root.
**Branch**: read it from git — `git rev-parse --abbrev-ref HEAD`. ⚠️ **Do NOT read a branch from
`check-prerequisites.sh`'s `BRANCH` field**: it is derived from a tracked pin and never from git
(fixpp#490). ⚠️ **The warning is NOT stale just because the script now resolves a plausible feature
directory**: #490 is **mitigated, not fixed** — the pin currently **happens** to point here, so the
resolution looks right **by coincidence** while the mechanism is **unrepaired**. A defect made to
resolve correctly by accident is more dangerous than one that resolves wrongly: nothing looks wrong,
and the next actor on a bundle-less branch gets the original behaviour back with no warning.

**Toolchain**: the Phase 3 inherited one — Conan profile + Clang + Ninja + CMake, per
`[const §II.1]` / `[const §III.1]` / `[const §III.2]`.

```bash
# configure + build, tier-1 entry point
conan install . --build=missing --profile=linux-clang-debug
cmake --preset linux-clang-debug
cmake --build --preset linux-clang-debug
```

**Sanitizer presets**, where a scenario names one: `linux-clang-asan`, `linux-clang-ubsan`,
`linux-clang-tsan` — each with its matching Conan profile under `conan/profiles/`.

⚠️ **One scenario needs a preset-conditional artefact to stay consistent.** A renamed gtest can
leave a stale `ci/expected-preset-conditional-tests.txt` entry, which reds a CI script-pins check.
**Check that file before pushing** — V7 renames a cell.

**Where the relevant suites live** (ctest names, verified against the `add_test(NAME …)`
declarations):

| ctest name | holds |
|---|---|
| `capi_message_write` | the outbound accumulator / group-builder surface (`#447`, the index-bounds surface) |
| `capi_pure_tests` | the version cells and the thunk-split witness (`version_test.cpp`, `thunk_split_test.cpp`) |
| `capi_dict066_clone_membership_copy_oom` | the clone allocation-failure pin (`#458` arm A) |
| `dictionary_reify_tests` | the `[C++ track]` reify-factory surface (`#458`'s C++ half) |
| `session_fixt_credentials` | the credential-floor naming precedent the `#452` cells mirror |

---

## 2. Scenario index

| # | proves | requirement / criterion | contract |
|---|---|---|---|
| [V1](#v1) | `remove_tag` refuses mode (a) — positional shift | FR-001, SC-001 | [`msg-remove-tag.md`](./contracts/msg-remove-tag.md) |
| [V2](#v2) | `remove_tag` refuses mode (b) — the group is the target | FR-002 | same |
| [V3](#v3) | `remove_tag` with **no** builder open is untouched | FR-003, SC-002 | same |
| [V4](#v4) | an out-of-range index is a **defined** refusal | FR-004, SC-003 | [`msg-index-bounds.md`](./contracts/msg-index-bounds.md) |
| [V5](#v5) | `clone` refuses a failed dict-backed re-parse, with the **exact** code | FR-005–007, SC-004/005 | [`msg-clone.md`](./contracts/msg-clone.md) |
| [V6](#v6) | clone's exception boundary has the right **polarity** | FR-008 | [`msg-clone.md`](./contracts/msg-clone.md) §8 |
| [V7](#v7) | `[C++ track]` the reify factory refuses — and the retained arms survive | FR-009/010, SC-009 | [`msg-clone.md`](./contracts/msg-clone.md) §9 |
| [V8](#v8) | the config byte floor refuses and emits nothing | FR-011/012/013, SC-006/007 | [`session-config-byte-floor.md`](./contracts/session-config-byte-floor.md) |
| [V9](#v9) | the Python binding exposes the same refusal | SC-008 | same |
| [V10](#v10) | the version, the pins and the freeze gate | FR-014–017, SC-010/011/012/013 | [`version-and-freeze.md`](./contracts/version-and-freeze.md) |

---

<a id="v1"></a>
## V1 — `fixpp_msg_remove_tag` refuses failure mode (a): positional shift

**Pre-conditions.** An outbound message handle; a live session (the liveness token must not be
expired, or `check_outbound_msg` refuses first and you measure that instead).

**The sequence — lifted from the issue's own measured probe, two builders.** A scalar, then group
builder A opened with an entry, then group builder B opened, filled and **closed**; the variant then
calls `remove_tag` on the scalar before finishing A.

```bash
cmake --build --preset linux-clang-debug --target capi_message_write_test
ctest --preset linux-clang-debug -R '^capi_message_write$' --output-on-failure
```

**Expected observable — GREEN, after the fix.** All five of these, not a subset:

1. `fixpp_msg_remove_tag` returns **exactly** `FIXPP_ERR_INVALID_HANDLE`;
2. the scalar is **still present** in the committed frame;
3. both builders are **still usable** after the refused call;
4. both `fixpp_msg_group_end` calls and `fixpp_msg_commit` **succeed**;
5. the **complete** committed byte string equals the control run's, byte for byte.

⚠️ **The pre-fix RED is NOT "the payload differs".** In the two-builder arrangement, A's shifted
index lands on B's entry, so the later write overwrites B's field and leaves A's instance empty —
and `commit`'s **group grammar then refuses the message**. The observable on the unfixed tree is a
**commit refusal with an empty payload**, not a silently different one. A witness written to assert
payload divergence would not reproduce the one arrangement anybody has actually run.

⚠️ **Wrong-reason green:** an arena layout in which the shifted index happens to land on a benign
entry. **Defend by asserting the full committed byte string** (assertion 5), never a spot check.

⚠️ **Assertions 1 and 3 are what distinguish this fix from the rejected re-indexing option** — a
payload-only assertion is satisfied by re-indexing too.

**A second arrangement — one builder, erased entry positioned so the shifted index lands past the
end.** ⚠️ **Its RED is not observable as a crash** (§0 rule 1); assert the **full committed byte
string** against the pre-`remove_tag` expectation. **This arm's RED has not been executed**, and is
registered as such in the design authority's NOT MEASURED register.

---

<a id="v2"></a>
## V2 — `fixpp_msg_remove_tag` refuses failure mode (b): the group entry is the target

**Pre-conditions.** Same as V1, but `remove_tag` is passed the **NoXXX count tag of the open
group** — the tag on which a group `AccumulatorEntry` is matched.

```bash
ctest --preset linux-clang-debug -R '^capi_message_write$' --output-on-failure
```

**Expected observable.** **Exactly** `FIXPP_ERR_INVALID_HANDLE`, **and** the group entry
**unchanged** — its count, its instances and their fields all survive the refused call — **and**
`commit` still produces the pre-call frame.

⚠️ **THE MUTATION THAT MAKES THIS SCENARIO WORTH RUNNING — a two-state observation.** Replace the
guard's `!open_builders.empty()` predicate with a **position test** and re-run:

| arm | required outcome |
|---|---|
| V2 | goes **RED** |
| V1 | stays **GREEN** |

**A guard that passes V1 and fails V2 is the defect this scenario exists to catch.** Run the
mutation, observe both arms, then revert it.

---

<a id="v3"></a>
## V3 — the positive baseline: no builder open, nothing changes

**Without this scenario, "refuse always" passes V1 and V2.**

```bash
ctest --preset linux-clang-debug -R '^capi_message_write$' --output-on-failure
```

**Expected observable.** `fixpp_msg_remove_tag` with **no** builder open still returns
`FIXPP_ERR_OK` and still erases; an **absent** tag still returns `FIXPP_ERR_OK`. The shipped cells
that exercise this — including the one that uses `remove_tag` as **setup** rather than as the
behaviour under test — **must stay green**.

⚠️ **The setup-users are the subtle half.** A cell that calls `remove_tag` merely to arrange a
fixture is still broken by the refusal if a builder happens to be open at that point. Scope the
sweep to the guard's **true width** — both refused-but-safe classes in
[`msg-remove-tag.md`](./contracts/msg-remove-tag.md) §4 — not to a narrower reading.

---

<a id="v4"></a>
## V4 — an out-of-range index is a DEFINED refusal, not UB

**Pre-conditions.** An `fixpp_entry` whose `instance_index` is out of range for its resolved group —
reaching the subscript inside **`fixpp_entry_set_data`'s own body**, which no resolver covers.

```bash
ctest --preset linux-clang-debug -R '^capi_message_write$' --output-on-failure
```

**Expected observable.** **Exactly** `FIXPP_ERR_INVALID_HANDLE`, **nothing written**, and the
builder **still usable**.

⚠️ **The RED on the unfixed tree is NOT a crash** (§0 rule 1). It is **the absence of a defined
refusal**: the call returns something other than `FIXPP_ERR_INVALID_HANDLE` while the committed
payload is corrupt. **Assert the full committed byte string.**

⚠️ **THE MUTATION — a two-state observation.** Implement the bounds check in the **two resolver
bodies only** and re-run:

| arm | required outcome |
|---|---|
| V4 (the direct subscript) | stays **RED** |
| the resolver-path cells | go **GREEN** |

**A fix that passes V1, V2 and the resolver cells but fails V4 is exactly the mechanism gap this
scenario exists to catch.**

⚠️ **The propagation arm.** With a resolver able to report failure, exercise a **nested** builder so
that `builder_context`'s immediate dereference of a resolver result runs on the failing path. **A
null dereference there is the new UB the check would have introduced.** This arm's RED is
**registered as NOT MEASURED** in the design authority.

---

<a id="v5"></a>
## V5 — `fixpp_msg_clone` refuses a failed dict-backed re-parse

⭐ **Prefer the raised-cap route over an allocator injection.** It is **allocator-free and
sanitizer-safe**, and needs no `operator new` games — the clone-only injection technique is a
TU-local global `operator new` override gated on a witness macro and **disabled under ASan/TSan/MSan**,
i.e. it produces no evidence in three lanes of the matrix.

**The mechanism.** Clone calls the **2-argument** `Parser::parse(frame, mr)`, which takes the
**default-cap** overload. Parse the *source* with the **3-argument** overload at a **raised cap**,
over a frame with more entries than the default cap admits. The clone's re-parse then fails **on
frame shape alone**.

```bash
cmake --build --preset linux-clang-debug --target capi_message_write_test
ctest --preset linux-clang-debug -R '^capi_message_write$' --output-on-failure
```

**Expected observable.** `fixpp_msg_clone` returns **exactly** `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` and
`*clone_out` is **`NULL`**. On the unfixed tree it returns `FIXPP_ERR_OK` with a silently dict-free
clone.

⚠️ **Assert the EXACT code, never `!= FIXPP_ERR_OK`.** A refusal is also what a **null** handle
(`FIXPP_ERR_NULL_HANDLE`) and a **dead** handle (`FIXPP_ERR_INVALID_HANDLE`) produce.

⚠️ **The spurious-hit control.** Clone the **same oversized source** from a **dict-free** handle —
it **must still return `FIXPP_ERR_OK`**, because no dict-backed attempt is made and nothing can
fail. Without this arm, "refuse whenever the source is big" passes.

**The other two codes** (SC-005) come from the other `translate()` arms — derive them from
`translate()` rather than from a list, per
[`msg-clone.md`](./contracts/msg-clone.md) §4.

**Counter-witness note.** The shipped happy-path cell that asserts a clone stays dict-backed and
that a forged field does not appear is this scenario's positive twin; **its negative is what this
feature adds.**

⚠️ **Not validated here, and must not be claimed:** the probe-cap degradation. Because that arm
leaves `status_` untouched, `parse` **succeeds** and this refusal is never entered. See
[`msg-clone.md`](./contracts/msg-clone.md) §5.

---

<a id="v6"></a>
## V6 — clone's exception boundary: the POLARITY, two arms

⚠️ **The seam here is a polarity, not a classification.** The thunk-split witness covers a
construction entry point and a steady entry point and **does not mention clone** — under this design
that silence is **correct**, not a gap.

```bash
cmake --build --preset linux-clang-debug --target capi_dict066_clone_membership_copy_oom_test
ctest --preset linux-clang-debug -R '^capi_dict066_clone_membership_copy_oom$' --output-on-failure
ctest --preset linux-clang-debug -R '^capi_pure_tests$' --output-on-failure
```

⚠️ **The second command is NOT where arm B lives.** `capi_pure_tests` holds the thunk-split witness,
which is named here **only as the abort-trap SHAPE precedent** — it does not contain a clone arm and
**should not**, because clone is not a construction thunk
([`msg-clone.md`](./contracts/msg-clone.md) §8). **Arm B's cell lands with the clone suite.**

**Arm A — `std::bad_alloc` inside clone's construction RETURNS.** Expected: **exactly**
`FIXPP_ERR_CAPI_CONFIG_INVALID`, `*clone_out` **`NULL`**. ⚠️ **This is a REGRESSION arm, not a new
witness** — the shipped OOM pin already covers it, and the job here is to keep it **green under the
narrowed `catch`**.

**Arm B — a NON-allocation exception inside the same window ABORTS.** Expected: `SIGABRT`, in the
in-process abort-trap shape the thunk-split witness already uses for its steady arm, with the abort
originating **from clone's own outer `catch (...)`**.

⚠️ **Both arms or neither.** A one-sided witness asserting only arm A passes against the shipped
blanket catch, the narrowed catch, and every intermediate. A one-sided witness asserting only arm B
passes against "abort everything" — **which reds arm A's shipped pin**. **The two arms name the
COUNT, not a direction.**

⚠️ **THE CONTROL — widen the INNER handler, not the outer.** Restore the single blanket catch (i.e.
widen `catch (std::bad_alloc const&)` back to `catch (...)`) and re-run:

| arm | required outcome under the control |
|---|---|
| arm B | its `SIGABRT` **disappears** and becomes a `FIXPP_ERR_CAPI_CONFIG_INVALID` **return** |

**Widening the OUTER catch changes nothing** — it is already `catch (...)`.

⚠️ **Arm B needs a DIFFERENT injection seam from arm A, and this bundle does not name one.** Arm A's
inherited injection throws `std::bad_alloc` **by construction** and therefore cannot produce arm B's
precondition. The `Parser::parse` call inside clone's `try` is **not** a candidate and never was —
both overloads are declared `noexcept`, so a throw there calls `std::terminate` and can never reach
either handler. **Whether ANY injectable non-`bad_alloc` seam exists is undecided and the set may be
empty**, registered in the design authority's NOT MEASURED register.

⇒ **The MUTATION is the EXPECTED discharge, not a fallback:** delete the **inner**
`catch (std::bad_alloc const&)` handler and assert that arm A goes **from return to abort**. ⚠️ That
mutation only works **once the outer `catch (...)` exists** — against a flat narrowing it produces an
**escape**, not an abort, and would report an outcome it cannot observe. It is the **cheapest single
signal** that the boundary is actually in place.

---

<a id="v7"></a>
## V7 — `[C++ track]` the reify factory refuses — and the retained arms survive

⚠️ **`[const §X.7]` is NOT engaged here.** This is public C++ surface under `[const §XVII.1]`: no
error-code slot, no version macro, not in the byte-freeze manifest.

```bash
cmake --build --preset linux-clang-debug --target dictionary_reify_tests
ctest --preset linux-clang-debug -R '^dictionary_reify_tests$' --output-on-failure
```

**Reach the factory directly**, through the same entry point the existing cells use, with the
repository's failing-PMR test resource.

**Expected observables — FIVE arms, and the count is the point:**

| arm | source | expected |
|---|---|---|
| (i-a) | dict-free, healthy allocator | **succeeds**; `build_status()` ok; not dict-backed |
| (i-b) | dict-free, **failing** allocator | ⭐ **still succeeds** — live handle, `build_status()` reports `out_of_memory`, the field reads absent. **SHIPPED AND PINNED; the obligation is to keep it green, not to write it** |
| (ii) | dict-backed, parses cleanly | **succeeds**; the view is dict-backed |
| (iii) | dict-backed, **re-parse fails** | **refuses** — returns `unexpected` carrying **the wire error the failed parse produced** (not the pre-existing deep-copy sentinel, not a generic one), and **no handle is constructed**. ⚠️ **The only new refusal this half adds, anywhere** |
| (iv) | a span that **frames to nothing**, including zero bytes | ⭐ **still succeeds** — a live handle over a default-constructed empty view. **SHIPPED, MANY TIMES OVER; keep green, do not write** |

⚠️ **Without (i-a), (i-b) and (iv), "refuse whenever anything goes wrong" would pass — and would red
every shipped dict-free reify in the suite.** **(i-b) is exactly the arm that distinguishes this
design from the rejected refuse-everything option**; **(iv) is exactly the arm that distinguishes it
from a withdrawn earlier draft** which would have turned a frames-to-nothing span into a refusal
carrying an unchosen enumerator.

⚠️ **The spurious-hit control.** A failing allocator can also fail the **deep copy**, which returns
the **pre-existing** sentinel through an arm that already worked. Assert the **exact** error on arm
(iii), and add a control calibrated to fail the deep copy that must still yield that pre-existing
sentinel — otherwise this scenario measures the arm that was never broken.

⚠️ **Calibration obligation.** Eager materialisation moves *where* the allocations happen. **Derive
the failing-allocator ordinal by instrumenting the candidate build — do not copy the existing
constant.** If the ordinal moved, **RECALIBRATE the shipped cell; never rewrite it to assert a
refusal** (that is the rejected option). This re-verification is **registered as NOT MEASURED** in
the design authority.

---

<a id="v8"></a>
## V8 — the session-config byte floor refuses, and nothing reaches the wire

```bash
cmake --build --preset linux-clang-debug
ctest --preset linux-clang-debug -R '^capi_pure_tests$|^session_fixt_credentials$' --output-on-failure
```

**Mirror the shipped naming contract** for this family — `…_ReturnsInvalidConfig_NoWireEmit`.
⚠️ **The suffix IS the requirement: assert the error AND assert no wire emission.** A cell that
checks only the return code misses the point of the issue.

**The coverage grid — three dimensions, and each is load-bearing:**

| dimension | members |
|---|---|
| **field** | `sender_comp_id`, `target_comp_id`, `begin_string`, `supported_msg_types[].msg_type` |
| **byte class** | SOH (`\x01`), `=`, another control byte `< 0x20` |
| **role** | initiator **and** acceptor — CompIDs and BeginString are emitted by **both**, and the repository's role-symmetry precedent is an existing `{Initiator,Acceptor}` pair |

**At the C-ABI setters** (SC-006): SOH and `=` into `fixpp_session_config_set_comp_ids` and
`fixpp_session_config_set_begin_string` → **exactly** `FIXPP_ERR_CAPI_CONFIG_INVALID`, **nothing
stored**. ⚠️ For `set_comp_ids`, a bad `target` must not leave a new `sender` stored.

**At session open** (SC-007, `[C++ track]`): a session configured with such a value **never emits a
frame**; opening it fails.

⚠️ **THE SPURIOUS-HIT CONTROL, and it is mandatory here.** `FIXPP_ERR_CAPI_CONFIG_INVALID` is
**already** produced by the setters' own null/empty guard, by a **dead** engine handle, and by a
`fixpp_session_open` called **after** `fixpp_engine_start`. ⇒ **Assert the code from the call under
test, with a live handle, in a pre-start state, and pair it with a value that differs from an
accepted one ONLY by the injected byte.** See [`data-model.md`](./data-model.md) §2.3.

⚠️ **Positive baselines that MUST stay green**, so the guard is not simply "refuse everything": the
shipped cells asserting that configured credentials **are** emitted on an outbound logon, that a
credential-free logon establishes unaffected, and that a credential-free logon's stored bytes are
identical to the wire.

---

<a id="v9"></a>
## V9 — the same refusal through the Python binding

```bash
# after building the binding / wheel per its own recipe
pytest bindings/python/tests/ -k config
pytest bindings/python/tests/wheel/ -k config
```

**Expected observable.** A configured CompID containing **SOH (`\x01`)** — *not* NUL — is
**rejected**. On the unfixed tree it currently returns OK.

⚠️ **The control, and the way this scenario fails silently.** The existing
`test_config_str_rejects_embedded_nul` must **keep raising from the typemap**. **If the SOH cell and
the NUL cell become indistinguishable in their failure text, the SOH cell is measuring marshalling
again** — not the new refusal.

⚠️ **The cell lands in BOTH `bindings/python/tests/` and `bindings/python/tests/wheel/`**, because
obligation 3 requires **every** in-repository consumer updated in the same PR.

---

<a id="v10"></a>
## V10 — the version, the pins, and the freeze gate in BOTH states

```bash
ctest --preset linux-clang-debug -R '^capi_pure_tests$' --output-on-failure
bash tools/check_capi_freeze.sh
```

**Expected observables:**

1. **The version reports 1.7.0** (SC-010). ⚠️ **The two hard version cells fail at ctest time, not
   at build time** — there is no `static_assert` in that file, so a build that succeeds proves
   nothing about them.
2. **A cell is RENAMED**, because its *name* carries the old version. ⚠️ **Check
   `ci/expected-preset-conditional-tests.txt` before pushing** — a renamed gtest can leave a stale
   entry there and red a CI script-pins check.
3. **The error-code enumeration, the append-only code history and the exported-symbol golden are
   UNCHANGED** (SC-011) — measurable proof that nothing was minted. **Check, do not claim**: re-run
   the `nm --defined-only --extern-only` diff the ABI-golden workflow performs.
4. ⚠️ **THE FREEZE GATE IS OBSERVED IN BOTH STATES** (SC-012) — this is the two-state rule at its
   sharpest:

| when | `bash tools/check_capi_freeze.sh` |
|---|---|
| after the header **edits**, before the re-baseline | **FAILS** |
| after the **re-baseline** | **PASSES** |

**A green result alone is consistent with a re-baseline applied BEFORE the edits**, which is a green
gate over unchanged hashes. **Both states must be seen.**

⚠️ **Three headers re-baseline, for two different reasons** — the version header because **its macro
moves**, the other two because **obligation 2 edits their comment bytes**. A comment edit is a byte
edit and the manifest hashes bytes. Writing *"the bump re-baselines three headers"* is wrong. See
[`version-and-freeze.md`](./contracts/version-and-freeze.md) §5.

5. **The PR body carries a `##` HEADING naming all three changes as BREAKING** (SC-013), in the
   in-tree spelling this repository already uses. ⚠️ **A heading, not bold text** — this repository
   has already lost a gate to exactly that. **Run the gate's own check locally before opening the
   PR.**

---

## 3. What this guide deliberately does NOT validate

Stated, because *"nothing to validate here"* is itself a claim.

- **The probe-cap degradation.** Out of scope, on the condition stated in
  [`msg-clone.md`](./contracts/msg-clone.md) §5. **Must not be claimed as covered.**
- **RefMsgType(372) at the two reject builders.** A **checked negative** on the condition in
  [`session-config-byte-floor.md`](./contracts/session-config-byte-floor.md) §8.2 — and **what a
  counterparty parser does with a surviving `=` is not measured and not claimed.**
- **Any performance claim.** Neither the per-call cost of eager materialisation nor the byte scan's
  cost on the commit path is measured here; the instrument for both is a **paired base-vs-candidate
  run on one runner**, A-B-A-B, min-per-tree.
- **The sanitizer matrix as a witness for `#447`** (§0 rule 1).
- **Whether the steady/construction split gains the source-level construct it prescribes.** A
  separate issue; this feature owns only that the owner document stops claiming a gate that does not
  exist.
