# Contract: `fixpp_msg_remove_tag` — refuse while a builder is open (fixpp#447)

**Track**: C-ABI. **Marking**: **BREAKING**, under `[const §X.7]`.
**Header**: `include/fix/c_api/message.h`. **Definition**: `src/capi/message_write.cpp`.
**Requirements**: FR-001, FR-002, FR-003. **Criteria**: SC-001, SC-002. **Ledger row**: B-447-1.
**Model**: [`../data-model.md`](../data-model.md) §1 (I-1), §2.1 (S-1), §3.1 (T-1), §4 EC-1.

```c
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_remove_tag(fixpp_msg_t* msg, uint16_t tag);
```

**The signature does not change.** No parameter, no return type, no export, no reentrancy class.

---

## 1. Why this is the breaking change

> **A call sequence that returns `FIXPP_ERR_OK` today will return `FIXPP_ERR_INVALID_HANDLE` after
> this change.**

That is the whole of `[const §X.7]`'s test — *"a call that used to succeed and now fails, whatever
the documentation said about it."* There is no argument to be had.

⚠️ **And the breakage is larger than "a new code appears": the declaration's own published guarantee
is NARROWED.** The shipped doc block reads, in full:

> *"Remove a tag from the accumulator.  Idempotent (absent returns OK). Reentrancy:
> requires-session-lock"*

D-1's guard fires **before** the `find_if`, so **absent no longer returns OK when a builder is
open**. The published idempotence sentence becomes conditional. A caller relying on it loses it, and
the declaration must say so rather than leave it to be discovered. (The same idempotence line is
restated in the C-ABI owner document's per-symbol roster; FR-019's amendment covers it.)

---

## 2. Pre-conditions

Unchanged, and read at source rather than described as *"the existing checks"*. In order:

| # | check | on failure |
|---|---|---|
| 1 | `msg != nullptr` | `FIXPP_ERR_NULL_HANDLE` |
| 2 | `check_outbound_msg(msg)` — a composite of three: the handle's `tag_` is not `FIXPP_HANDLE_TAG_DEAD`; the handle's `flavour` **is** outbound (an inbound or clone handle is not a mutation target); the liveness `token` has not expired (session close / engine destroy tombstones it lazily) | `FIXPP_ERR_INVALID_HANDLE` for all three |
| **3 — NEW** | `msg->accumulator->open_builders` is **empty** | `FIXPP_ERR_INVALID_HANDLE` |

⚠️ **Check 3 is keyed on the builder stack being non-empty — NOT on the erased position and NOT on
the erased tag.** One predicate, covering both of I-1's failure modes, defeatable by no tag choice.
A position-keyed guard misses mode (b) when the group entry is last; a tag-keyed guard misses mode
(a) entirely.

⚠️ **Check 3 runs BEFORE the `std::ranges::find_if`.** It does not matter whether the tag is present.

---

## 3. Post-conditions

### 3.1 On refusal — `open_builders` non-empty

| observable | value |
|---|---|
| return code | **exactly** `FIXPP_ERR_INVALID_HANDLE` (4) — not merely "non-OK" |
| the accumulator's `entries` | **unchanged** — nothing erased, nothing shifted |
| every open builder | **still usable**: `fixpp_group_builder_add_entry`, `fixpp_entry_set_*` and `fixpp_msg_group_end` all continue to succeed on it |
| a subsequent `fixpp_msg_commit` | produces the **byte-identical** frame it would have produced had the refused `remove_tag` never been called |

⚠️ **The last two rows are the contract, not colour.** A return code alone does not distinguish a
refusal from a **partial** operation, and a payload assertion alone does not distinguish D-1 from the
**rejected** re-indexing option — which also yields a correct payload. Both must be observable.

### 3.2 On success — `open_builders` empty

**Byte-for-byte unchanged from today.** Present tag → erased, `FIXPP_ERR_OK`. Absent tag → nothing
erased, `FIXPP_ERR_OK` (idempotent). This is FR-003 and SC-002, and it is what stops the fix from
degenerating into "refuse always".

---

## 4. What the caller observes — the two refused-but-safe classes, stated explicitly

⚠️ **B-447-1 requires the guard's TRUE width.** Two call shapes are safe today and are refused
anyway. They are named here so that no caller and no reviewer has to infer them:

| class | why it is safe today | why it is refused anyway |
|---|---|---|
| **absent tag, builder open** | `entries.erase` is never reached, so nothing moves — and the declaration promises `FIXPP_ERR_OK` for it | the guard precedes the find; a tag-sensitive guard would make the outcome depend on the caller's argument |
| **present tag positioned after every live root's group entry** | `vector::erase` shifts only indices **above** the erased position, so no live `group_field_index` moves | the guard is position-insensitive; a position test is mode (a)'s predicate and cannot cover mode (b) |

**The trade, stated as the design took it:** one predicate that over-refuses in two **enumerable,
harmless** ways is a smaller contract surface than two predicates that under-refuse in ways nobody
can enumerate — on a path that **no sanitizer in the matrix can observe** (see
[`../data-model.md`](../data-model.md) §1.6).

**Migration is one line** for every refused call: move the `remove_tag` **before** the
`fixpp_msg_group_begin`, or **after** the matching `fixpp_msg_group_end`.

---

## 5. What is explicitly UNCHANGED

- **The signature, export macro and reentrancy class** — `requires-session-lock`, as published.
- **Every pre-existing failure code and its condition** — §2 checks 1 and 2 are untouched.
- **Success semantics with no builder open**, including idempotence on an absent tag (§3.2).
- **The group builder's own state machine.** D-1 adds no transition to it; it refuses a *different*
  call made while the machine is in OPEN.
- **Nested-group entry vectors.** `remove_tag` never touched a `GroupInstance`'s `fields` and still
  does not.
- **`tests/abi/golden/fixpp_capi_symbols.txt`** — no symbol added, removed or re-signed.
- **`include/fix/c_api/error.h`** — no enumerator minted; `FIXPP_ERR_INVALID_HANDLE` already exists.

---

## 6. Declaration obligations (`[const §X.7]` obligation 2, first limb)

The doc block in `include/fix/c_api/message.h` gains:

1. the **BREAKING** marking in the in-tree spelling already used in that header for fixpp#428 —
   `-- (1.7, BREAKING) …` / `Since 1.7 (BREAKING), …`. ⚠️ **Do not invent a second spelling**, and
   ⚠️ **do not re-date #428's existing `(1.6, BREAKING)` / `Since 1.6` mentions** — they correctly
   date #428, and re-dating them would falsify history;
2. an **error-code list**, which the declaration does not have today at all;
3. the new `FIXPP_ERR_INVALID_HANDLE` condition, stated as *a group builder is open*;
4. the **two refused-but-safe classes** of §4, stated rather than implied.

⚠️ **This is a byte edit to `include/fix/c_api/message.h`**, and the byte-level freeze manifest
hashes comments. See [`version-and-freeze.md`](./version-and-freeze.md) — the re-baseline follows
from **this edit**, not from the version bump.

---

## 7. The error code, and why no code is minted

`FIXPP_ERR_INVALID_HANDLE` follows **the one existing precedent for this exact condition in the C
ABI**: `fixpp_msg_commit`'s `if (!acc.open_builders.empty()) return FIXPP_ERR_INVALID_HANDLE;`,
whose comment reads *"An open (unended) group builder ⇒ not a sealed, committable state"*.

⇒ **Two symbols answer the same question with the same code**, and no new enumerator is minted — so
this feature touches neither `src/capi/error.cpp`'s `introducing_minor()` nor
`tools/abi_history/error_codes_v1.txt` on this account. See
[`msg-clone.md`](./msg-clone.md) §7 for the same disposition stated with its cost argument.

---

## 8. The rejected alternative, recorded so it is not re-proposed

**Re-indexing** — erase, then walk the live index holders and decrement every index above the erased
position — is **rejected**, on two independent grounds:

1. **It cannot repair failure mode (b) at all.** When the erased entry *is* the group, there is no
   correct value to renumber the builder to. Re-indexing would have to detect that case and refuse
   anyway, leaving one call with two behaviours the caller cannot predict without knowing whether its
   own tag argument happened to be a NoXXX tag of an open group.
2. **Its write set is larger than it looks.** `fixpp_msg_group_begin` does **not** refuse while a
   builder is open, so `open_builders` can hold several roots simultaneously and every root above the
   erased position must be adjusted — an off-by-one on every element, on a path invisible to every
   sanitizer in the matrix.

Under `[const §X.7]` **both** options are breaking, so breaking-ness does not discriminate.
**Predictability does**: re-indexing changes the meaning of a *successful* call (indices silently
move under the caller); refusing changes only *which* calls succeed.
