# Contract: a DEFINED refusal for an out-of-range group/instance index (D-2b)

**Track**: C-ABI. **Marking**: ⚠️ **NOT marked BREAKING** — see §1.
**Header**: `include/fix/c_api/message.h`. **Definition**: `src/capi/message_write.cpp`.
**Requirement**: FR-004. **Criterion**: SC-003.
**Model**: [`../data-model.md`](../data-model.md) §1.5 (the two index families), §1.6 (why no
sanitizer sees it), §4 EC-2.

---

## 1. Why this file exists separately, and why it is NOT BREAKING

The discriminator that makes the other three changes breaking is *"a call that used to return
`FIXPP_ERR_OK` and now refuses."* **This surface fails that test**, and the design authority states
the disposition in its own words for these declarations:

> ⚠️ **Not marked BREAKING** — the arm it replaces is **undefined behaviour, not a documented
> success**.

An out-of-range subscript today is silent UB. Nothing that is **defined** changes. It is kept in its
own file so that no reader inherits the BREAKING marking from a neighbouring section.

⚠️ **This is defence in depth, not the fix.** After `fixpp_msg_remove_tag` refuses while a builder is
open (see [`msg-remove-tag.md`](./msg-remove-tag.md)), **no shipped path reaches an out-of-range
index**, so this check is expected to be **unreachable from the C ABI**. That is a reason to state
its status honestly at the site, not a reason to omit it: `[const §IX.1]` permits three
dispositions and this one is **assessed**, with the assessment written where the code is.

---

## 2. The condition, stated as REACHABILITY — not as two function bodies

> **Every raw subscript of `group_field_index` or `instance_index` reachable from a C-ABI entry
> point is unchecked today**, across **both** index families.

`resolve_group` and `resolve_instance` are both `noexcept`, carry **zero asserts**, and return raw
pointers — so there is **no error channel and no debug trap**. An out-of-range **or** a
stale-but-in-range index is silent UB, and both of `remove_tag`'s failure modes land here.

⚠️ **The condition is WIDER than "the two resolvers", and the gap is a named site.** A check confined
to the resolver bodies would leave `fixpp_entry_set_data`'s **own** `GroupInstance& inst =
group->instances[e->instance_index];` undefined — in a declaration this feature says gains a defined
refusal. **A claim wider than its mechanism is the defect class this feature is written against.**

### 2.1 The three reachability classes — classes are the claim, members are the reading

| class | what it is | why it is its own class |
|---|---|---|
| **(1) resolver bodies** | `resolve_group`'s subscripts of `entries`, of `instances` and of a parent instance's `fields`; `resolve_instance`'s subscript of `instances` | the obvious ones |
| **(2) a direct subscript inside a C-ABI entry point** | `fixpp_entry_set_data`'s `group->instances[e->instance_index]`, which **no resolver covers** | class (1) does not reach it |
| **(3) an immediate dereference of a resolver result** | `builder_context`'s `resolve_group(b->parent->builder)->tag`; `resolve_group`'s own recursion; `resolve_instance`'s call to `resolve_group` | the moment a resolver can report failure, each of these becomes a **new** null-dereference site unless the failure is propagated through it |

⚠️ **Class (3) is a required PROPAGATION point, not a third subscript.** **A bounds check that
creates a null dereference beside the one it removes is not defence in depth.**

### 2.2 Re-derivation recipe — run it, do not trust any list, including this one

```
grep -n "resolve_group\|resolve_instance\|instances\[\|entries\[\|fields\[" src/capi/message_write.cpp
```

Then read each hit and classify it as *resolver body* / *direct subscript in a C-ABI entry point* /
*immediate dereference of a resolver result* / *unrelated*. **A new site appears as a hit in the
second or third class.** ⚠️ **No number is written here**: successive revisions of the design note
wrote "three", then measured four, and then established that the count was never the question — the
**reachability class** is.

---

## 3. The declaration population, stated as a class

> **Every exported entry point that reaches a resolver, plus the exported setters that fan out from
> the non-exported `entry_set_bytes_impl`.**

⚠️ **Counting call sites UNDER-COUNTS declarations.** `entry_set_bytes_impl` is `static` — not
exported — and fans out to four exported setters, so one call site carries four declarations.

**Re-derivation recipe, not a result:**

```
grep -n "resolve_group\|resolve_instance" src/capi/message_write.cpp
# control — a DIFFERENT pattern, positive on the SAME corpus:
grep -n "check_builder\|check_entry"      src/capi/message_write.cpp
```

Classify each hit as **definition** / **internal or recursive use** / **outer call site**, and for
every outer call site that is `static`, follow it to the exported functions that reach it. **A new
resolver consumer appears as a new outer call site.**

**All of these declarations live in `include/fix/c_api/message.h`**, which the version contract
already re-baselines — so **the freeze population does not grow on this account**. See
[`version-and-freeze.md`](./version-and-freeze.md).

---

## 4. Pre-conditions, post-conditions, and the observable

**Pre-conditions are unchanged** on every affected entry point: the existing null / `check_builder`
/ `check_entry` / `check_dict` chain runs first, exactly as today. `check_builder` already refuses a
**closed** builder with `FIXPP_ERR_INVALID_HANDLE` before any resolution happens.

**The new post-condition** applies when, and only when, a resolution would subscript out of range:

| observable | value |
|---|---|
| return code | **exactly** `FIXPP_ERR_INVALID_HANDLE` (4) |
| the accumulator | **nothing written** |
| the builder | **still usable** |
| class (3) sites | the failure **propagates**; no null is dereferenced |

**The code is `FIXPP_ERR_INVALID_HANDLE` for the same reason `remove_tag`'s is** — an index that
does not name a live entry is not a usable handle, and the C ABI already answers that question with
this code. **No enumerator is minted.**

---

## 5. The implementation shape is deliberately LEFT OPEN — with one binding obligation

⚠️ **`resolve_group` and `resolve_instance` are `noexcept` and return raw pointers**, so the refusal
**cannot be a return value from them**. It must be a checked precondition at their callers, or the
functions must change signature.

**The design authority leaves that choice to implementation — but binds it:**

> **Whichever shape is picked MUST discharge all three reachability classes of §2.1.**

In particular, a **null-returning resolver discharges class (3) only if `builder_context` and the two
internal uses are rewritten with it.** Picking a shape that covers classes (1) and (2) and leaves
(3) dereferencing a null is a regression dressed as a hardening.

---

## 6. Declaration obligations

Each affected declaration in `include/fix/c_api/message.h` gains `FIXPP_ERR_INVALID_HANDLE` on an
out-of-range resolver index, **documented as an assessed-unreachable defence-in-depth return** per
`[const §IX.1]`.

⚠️ **No BREAKING marking on these declarations** (§1). ⚠️ **A byte edit to `message.h` all the same**,
which re-triggers that header's freeze hash — see [`version-and-freeze.md`](./version-and-freeze.md).

---

## 7. What is explicitly UNCHANGED

- **Every signature** in the affected population; no export added, removed or re-signed.
- **Every pre-existing pre-condition and its code** (§4).
- **All defined behaviour.** Only the undefined arm gains a definition.
- **The `instance_index` family's stability under `fixpp_msg_remove_tag`** — nothing erases from a
  group's `instances`, so that family is append-only-stable. It is bounds-checked here because the
  check is index-provenance-agnostic, **not** because `remove_tag` shifts it.
- **`src/wire/body_builder.cpp`'s distinct sibling `resolve_group` / `resolve_instance`** — same
  shape, append-only, no `remove_tag` analogue, **not in this population**. Do not conflate them.
- **`include/fix/c_api/error.h`** and `tests/abi/golden/fixpp_capi_symbols.txt`.
