# Contract: `fixpp_msg_clone` — refuse a failed dict-backed re-parse (fixpp#458)

**Track**: C-ABI (§1–§8) **and** `[C++ track]` (§9, labelled).
**Marking**: **BREAKING**, under `[const §X.7]`, on **two** limbs — see §1.
**Header**: `include/fix/c_api/message.h`. **Definition**: `src/capi/message_write.cpp`.
**Requirements**: FR-005, FR-006, FR-007, FR-008 (C-ABI); FR-009, FR-010 (`[C++ track]`).
**Criteria**: SC-004, SC-005 (C-ABI); SC-009 (`[C++ track]`).
**Ledger rows**: B-458-1 (C-ABI), B-458-2 (`[C++ track]`).
**Model**: [`../data-model.md`](../data-model.md) §2.2 (S-2), §3.3 (T-3), §4 EC-3/EC-4/EC-5, §4.1, §4.2.

```c
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_clone(const fixpp_msg_t* src, fixpp_msg_t** clone_out);
```

**The signature does not change.**

---

## 1. Why this is the breaking change — TWO limbs, not one

> **Limb 1 — the refusal.** Cloning a **dict-backed** source whose re-parse of the copied frame
> fails returns `FIXPP_ERR_OK` today, with a live clone. After this change it returns a **non-OK**
> code and no clone.

> **Limb 2 — the abort.** A **non-`std::bad_alloc`** exception raised inside clone's construction
> **returns** `FIXPP_ERR_CAPI_CONFIG_INVALID` today, from the body's blanket `catch (...)`. After
> this change it **terminates the process after a fatal log**.

⚠️ **Limb 2 is declared, not inferred.** A return becoming a process abort is a behaviour change on
a C-ABI symbol **whatever its reachability**, so B-458-1 states it explicitly. ⚠️ **And its trigger
set is NOT enumerated** — whether such an exception can be produced on clone's path at all is
**undecided** and registered as such in the design note's NOT MEASURED register. The reachability is
**declined, not asserted**; the behaviour change is declared anyway. A limb declared unreachable on
the strength of an `// LCOV_EXCL_LINE` comment would be a classification nothing enforces, which is
the defect class this feature exists to remove.

### 1.1 The defect, in the tree's own words

The fallback arm in `fixpp_msg_clone`'s body already concedes it:

> *"⚠️ **fixpp#458** — this fallback FAILS OPEN. It is taken whenever the dict-backed re-parse
> fails, for ANY reason: the clone then reports OK from a dict-backed source with NO dictionary …"*

and it concedes its own history in the same comment: *"⚠️ The routes below are NOT exhaustive, and
the history is the reason to say so."* `src/dictionary/reify.cpp` carries the same retraction at its
sibling site. **This contract does not re-argue a point the tree already concedes; it fixes the
behaviour the comment leaves standing.**

⭐ **The in-tree precedent is the engine's.** Two other `Parser<Index>` consumers already **fail
closed** on a failed dict-backed parse — `Session::parse_and_dispatch_` (*"parse error — skip"*) and
the session group-validation path (`return std::nullopt;`). Only the two owning-copy paths do not.
**This is not a new policy; it is the existing policy applied where it was skipped.**

---

## 2. Pre-conditions

Unchanged. In order, as the body runs them:

| # | check | on failure |
|---|---|---|
| 0 | `*clone_out` is set to `NULL` first, unconditionally, when `clone_out != nullptr` | — |
| 1 | `src != nullptr && clone_out != nullptr` | `FIXPP_ERR_NULL_HANDLE` |
| 2 | `src`'s `tag_` is not `FIXPP_HANDLE_TAG_DEAD` | `FIXPP_ERR_INVALID_HANDLE` |
| 3 | `src` has a wire view — an **outbound accumulator** handle has none and is out of v1.0 scope (L-051-2) | `FIXPP_ERR_INVALID_HANDLE` |

---

## 3. Post-conditions

### 3.1 On refusal — dict-backed source, re-parse failed (**NEW**)

| observable | value |
|---|---|
| return code | `translate(parsed.error())` — see §4. **Exactly** that code, never merely "non-OK" |
| `*clone_out` | **`NULL`** |
| the **source** handle | **unchanged and still usable** — no clone was constructed, nothing was consumed |

### 3.2 On refusal — `std::bad_alloc` during clone construction (**preserved, narrowed**)

| observable | value |
|---|---|
| return code | **exactly** `FIXPP_ERR_CAPI_CONFIG_INVALID` (10) |
| `*clone_out` | **`NULL`** |

⚠️ **This arm is a REGRESSION obligation, not a new witness.** It is what
`CloneMembershipCopyOom.TableViewCopyOomYieldsCapiConfigInvalid` already asserts (*"NOT
`std::terminate`"*, in its own comment). The contract's job here is to **keep that pin green under
the narrowed `catch`**.

### 3.3 On abort — any other exception (**NEW behaviour, no return value**)

The process terminates after a fatal log, from **clone's own outer `catch (...)`** in
`src/capi/message_write.cpp`, matching the shipped `std::fputs(…, stderr)` + `std::abort()` idiom
that `src/capi/session.cpp` already carries at `fixpp_session_send` and
`fixpp_session_acceptor_bound_endpoint`. There is **no return code**.

### 3.4 On success

**Byte-for-byte unchanged from today** for both a healthy dict-backed source and a dict-free source:
`FIXPP_ERR_OK`, `*clone_out` live, session-independent (no liveness token), reads THREAD_SAFE.

---

## 4. The error code on limb 1 — a CONDITION plus a function, not a closed list

> The code is **`translate()`'s current image of the `core::error` the failed dict-backed re-parse
> produced**.

`src/capi/error.cpp`'s `translate()` is already **total** over `fixpp::core::error` (*"Total switch,
no default (see file header)"*), enforced by `-Wswitch` and audited against
`expected_error_map.csv`. Today `parsed.error()` is **discarded** at this site and never inspected —
that discard **is** the design lever, and removing it is the fix.

The arms that exist today, so a witness can assert an **exact** code — **re-derive from `translate()`
rather than trusting this table**:

| `OffsetTable` failure route | code |
|---|---|
| `fail(core::error::out_of_memory)` — the failing-allocator route | `FIXPP_ERR_UNKNOWN` (2) |
| `wire_offset_table_full` (the raised-cap route), `wire_tag_out_of_range` | `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` (101) |
| `wire_invalid_field_format` | `FIXPP_ERR_WIRE_INVALID_FRAME` (100) |

⚠️ **The OOM route's `FIXPP_ERR_UNKNOWN` is documented v1.0 behaviour, not a gap this change
opens.** **L-049-2** in the live `spec/behaviors-and-limitations.md` already records that
*"`out_of_memory` has no cross-cutting OOM code and a `switch(error)` cannot be
call-site-dependent"*, narrowed by 051 to *"log/otel + OOM"*. B-458-1 **references** that limitation;
it does not restate it.

⚠️ **`FIXPP_ERR_DICT_OOM` (202) is rejected** as the OOM code — its name scopes it to dictionary
load/reify, and using it here would make `fixpp_msg_clone` report a dictionary-subsystem failure for
a **wire-parse** allocation failure.

---

## 5. The claim this fix makes, in its narrowest true form

> **The dict-backed re-parse either succeeds, or the operation refuses. No path produces a handle
> that reports success while having silently become dict-free.**

⚠️ **What it does NOT claim, stated because "covered" is the claim that has to be earned:**

- **It does not claim every tag in the resulting view is indexed.** The probe-cap degradation —
  `OffsetTable::build`'s `skip_insert = true` arm under *"DoS bound: leave this occ un-indexed"* —
  never assigns `status_`, so `build_status()` stays ok, **`parse` succeeds**, and this fallback is
  **never entered**. It is a degradation of a *successful* dict-backed parse: a different defect on
  a different path. **Out of scope, and must not be claimed as covered.** **Re-derive:** read that
  arm and check whether it assigns `status_`.
- **It says nothing about a handle minted from a dict-free source.** Such a handle never had a
  dictionary to lose (§9.2).

---

## 6. What is explicitly UNCHANGED

- **The signature, export macro and reentrancy class** — `requires-session-lock` on the source.
- **Pre-conditions 1–3** (§2), including the outbound-accumulator refusal (L-051-2).
- **Success on a dict-free source.** No dict-backed attempt is made, so there is nothing to fail —
  the clone is dict-free as minted, exactly as today.
- **Success on a healthy dict-backed source**, including the 066 membership propagation
  (`clone->owned_tv_` seated from `h->view->membership_copy()`).
- **`FIXPP_ERR_CAPI_CONFIG_INVALID` on `std::bad_alloc`** — preserved, and pinned (§3.2).
- **`tests/abi/golden/fixpp_capi_symbols.txt`**, `include/fix/c_api/error.h`,
  `src/capi/error.cpp`'s `introducing_minor()`, `tools/abi_history/error_codes_v1.txt` — all
  untouched (§7).
- ⚠️ **`[2i §5.2]`'s construction-time whitelist is NOT amended, and `fixpp_msg_clone` is NOT
  reclassified.** See §8 — an earlier reclassification proposal is **withdrawn**.

---

## 7. No code is minted — and the cost argument, because "cheaper" is a claim

Minting a new enumerator would require, all in one PR: a re-baseline of
`include/fix/c_api/error.h`'s freeze hash (it is in `tools/capi_freeze.sha256`'s manifest); an
`expected_error_map.csv` oracle update; an append-only line in
`tools/abi_history/error_codes_v1.txt` carrying introducing-minor **7**; and a `return 7;` arm in
`src/capi/error.cpp`'s `introducing_minor()` — whose own comment warns against the wrong fix: *"A
literal scalar-bump to 4 would silently downgrade EVERY existing 0.2/0.3 code at consumer_minor=3"*.

A code slot **does exist**, so this is a **cost** argument, not a feasibility one. Reusing
`translate()` also keeps this feature off every file a concurrent error-code feature would touch.

---

## 8. The exception boundary — nested, inside clone's own body

⚠️ **Read this section before touching the `catch`.** The shape is prescribed, and a flat narrowing
would introduce a *new* defect.

Today the body opens its `try` with the comment `// Construction-time thunk: catch→translate.` and
closes with a blanket `catch (...) { return FIXPP_ERR_CAPI_CONFIG_INVALID; }`. **That comment records
a classification the C-ABI owner document does not make**, and it is deleted by this change.

**The prescribed shape — OUTER and INNER, not three peers:**

| position | handler | outcome |
|---|---|---|
| **OUTER**, in clone's own body | `catch (...)` | fatal log to `stderr`, then `std::abort()` — matching `src/capi/session.cpp`'s shipped idiom verbatim in shape and message form |
| **INNER**, wrapped by the outer | `catch (std::bad_alloc const&)` around clone's construction | return `FIXPP_ERR_CAPI_CONFIG_INVALID` — **preserving the shipped pin** (§3.2) |
| **INNER**, the `if (parsed)` arm that today falls back | *(not an exception path)* | return `translate(parsed.error())` — this is the §1 limb-1 fix |
| anything else | falls past the inner handler into the outer | aborts, **in the same function** |

⚠️ **Why the nesting is not cosmetic.** Today's blanket `catch (...)` guarantees that **no exception
crosses `extern "C"`** from clone. Narrowing it to `catch (std::bad_alloc const&)` **with nothing
outside** would let a `std::logic_error` or a foreign exception leave an `extern "C"` function:
undefined behaviour for a C consumer, an ordinary propagating exception for a C++ test caller. **The
outer catch is what makes the abort outcome exist at all.**

⚠️ **There is no shared construct to delegate to.** The owner document's two `guarded_call_*`
flavours have **no source-level implementation** — re-derive: `grep -rn "guarded_call" src/ include/
tests/ bench/` against a different-pattern control positive on the same corpus. What implements the
steady side is a **hand-copied idiom, written per function and absent wherever nobody wrote it**, and
`src/capi/message_write.cpp` contains **zero `abort()` calls** while carrying comments that assert
the policy. Filed as **fixpp#487**; **out of this feature's scope** for every symbol other than
clone.

⚠️ **Clone STAYS a steady-state symbol and the whitelist stays closed.** Moving it onto the
construction-time whitelist was proposed and **rejected**: that whitelist's criterion is *semantic*
— *"used only by entry points whose invocation is the explicit C-ABI mirror of a constructor that
may throw **on bad config**"* — and **an exhausted heap during a runtime cross-strand copy is not
invalid configuration**. Publishing the blanket catch as policy would additionally advertise *every*
exception, including a `std::logic_error` or a foreign one, as a **retryable configuration result**.

⭐ **Consequence for the split's own witness:** `tests/capi/thunk_split_test.cpp` covers
`fixpp_engine_create` (construction arm) and `fixpp_session_send` (steady arm) and **does not
mention clone**. Under this contract that silence is **correct**, not a gap. What is owed instead is
a cell proving the boundary's **polarity** — see [`../quickstart.md`](../quickstart.md) V5.

---

## 9. `[C++ track]` — the reify factory half

⚠️ **This section is governed by `[const §XVII.1]` (*"Touches the public C++ API"*).
`[const §X.7]`'s machinery is recorded here as NOT ENGAGED**: these are not C-ABI symbols, they
consume no error-code slot, they move no version macro, and they are **not in
`tools/capi_freeze.sha256`'s manifest**. This is the disposition `.specify/456-table-view-seal.md`
set for a C++-only break. **Ledger row B-458-2 is separate from B-458-1 because the two halves ride
different tracks** — not because they behave differently. Under this design they behave the same.

### 9.1 The changed declarations

| declaration | what changes |
|---|---|
| `fixpp::dict::detail::owning_message_handle_from_frame` (`include/fixpp/dict/reify.hpp`) | Its documented failure set grows by **one** class. Today: *"`std::bad_alloc` -> `dict_reify_oom`"*. It also returns the **wire error from a failed dict-backed re-parse**. ⚠️ **NOT from a failed or empty frame.** **Signature unchanged** — `[[nodiscard]] core::expected_t<owning_message_handle> … noexcept`, the refusal channel that already exists |
| `fixpp::dict::owning_message_handle::view()` | Its contract changes from *lazily re-framed* to *a pre-populated cache*. ⚠️ **NOT to "a cache the factory validated"** — the factory seats an empty view when the span frames to nothing, exactly as `view()` does today, so the observable on that arm is unchanged. Signature, `noexcept` and `[[clang::lifetimebound]]` unchanged |
| `fixpp::dict::reify()` | the same failure-set growth, propagated from the generated dispatch |

⚠️ **No new public method is added.** An earlier three-state status accessor proposal is
**withdrawn** — it could not report the framing observable, and it could not be simultaneously
non-allocating and post-materialisation.

### 9.2 What B-458-2 explicitly does NOT cover — and each is shipped, pinned behaviour

1. **A dict-free source still materialises, still returns, and still reports a failed `OffsetTable`
   build** through the already-public `view().offsets().build_status()`. Pinned by
   `ReifyErrorContract.ViewRebuildOomDegradesNotTerminate` in
   `tests/dictionary/reify_dispatch_test.cpp`, whose fixture builds its source with the
   **two-argument, dict-free** `MessageView` overload and whose comment reads *"a throwing mr at
   first field access must NOT terminate"*. **Refusing here is the rejected option** — it reds that
   cell.
2. **A span that frames to NOTHING** — including a zero-byte span, where `Framer::feed` **returns
   success with an empty span** — still materialises and still returns, with a default-constructed
   empty view. Pinned across `tests/dictionary/reify_dispatch_test.cpp`,
   `tests/codegen/vlatest_dispatch_exclusion_test.cpp` and
   `tests/integration/fixt_cross_vocabulary.cpp`. A refusal on this arm was proposed and
   **withdrawn**; it would have obliged rewrites of shipped cells that no row declared.

⇒ **B-458-2 declares NO test rewrite.** That is the honest disposition now that the arm producing
them is gone.

### 9.3 BREAKING for a direct C++ caller, on ONE class

A direct C++ caller that receives a live `owning_message_handle` today — from a **dict-backed**
source whose re-parse failed — receives `std::unexpected` instead. **That single class is on its own
sufficient** for the marking; it is a source-visible contract change on a public C++ entry point.

### 9.4 Two obligations that follow from eagerness, recorded rather than assumed

- **The materialisation moves into the factory**, so a shipped cell calibrated on a `fail_on_call_n`
  ordinal must be **re-verified**. Eager changes the allocation *site*, not the *sequence*, so the
  ordinal is **expected** to hold — but a structural expectation is not a measurement. Derive it by
  instrumenting the candidate build. ⚠️ **If the ordinal moved, RECALIBRATE the cell — never rewrite
  it to assert a refusal.** That is the rejected option, not this design.
- **The per-call cost of eager materialisation is NOT measured**, and the population of affected
  callers is a different proposition from the per-call cost. The instrument is `[const §VIII.2]`'s
  paired base-vs-candidate run over `dict::reify()`.
