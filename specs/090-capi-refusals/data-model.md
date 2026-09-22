# Data Model: Three C-ABI calls that must refuse (090)

**Design authority**: `.specify/447-458-452-capi-refusals.md` at **v0.10**. Everything below is
derived from it; nothing here re-decides anything it decided, and nothing here re-derives a
population it derived. Where it states a **condition** and a **re-derivation recipe** instead of a
count, this file carries the condition and the recipe — not a number.

⚠️ **Gate A on this bundle RAN and did NOT converge.** It is `gate-a-waived` on two reasons.
Both Appendix A controls this line once recorded as owed are now ✅ **DISCHARGED 2026-09-20, and each is PINNED**: the user's `/plan` sign-off to `plan.md` at `d12d2270` (authoritative statement: `plan.md`'s Constitution Check row 5), and `/speckit-analyze` to the artifacts at `505adafa`, where a run through the canonical `spec-analyzer` executor returned **1 finding (F1, multi-site), 0 CRITICAL, 100 % coverage** and the finding was **remediated at `e9833610`, not deferred** (authoritative statement: `plan.md`'s Constitution Check row 4). ⚠️ **Neither discharge covers a LATER artifact state** — the artifacts have moved since and implementation will move them again, which is why `tasks.md`'s T084 survives as a re-run obligation. ⚠️ **This line read "both are OWED", and then "`/speckit-analyze` remains OWED"; each reading was true when written and was falsified by a later event** — this bundle's recurring defect shape, not a typo. Nothing in this file
should be read as a Gate A pass.

---

## 0. What this document is, and what it deliberately is not

**This feature adds no database, no persisted record, no new user-facing data type and no new
serialised shape.** All three changes make existing calls **refuse** in states where they currently
succeed. A CRUD-shaped entity catalogue would therefore be padding, and padding in this bundle is
how a false claim gets a home.

What the feature genuinely has a *model* of, and what this file carries:

| § | modelled thing |
|---|---|
| §1 | the **invariant** `fixpp_msg_remove_tag` breaks, and the two distinct failure modes it breaks it in |
| §2 | the **state** each of the three refusals is keyed on |
| §3 | the **state transitions** that exist — the builder's open/closed life, and the engine's register-before-start ordering |
| §4 | the **error codes** produced, each stated as a *condition*, never as a producer enumeration |

⚠️ **§4's shape is prescribed, not stylistic.** The design note's §6.5 disposition is that a clause
binding an error code to a **producer set** is exactly the clause this PR is amending, because such
a clause is false the moment a producer is added or moved. So the codes below are bound to the
**condition that produces them**. A reader wanting "who returns this code" must derive it; this file
refuses to publish a list that would rot.

**Track labelling is load-bearing.** §0a of the design note names *"fold all five changes under
`C-ABI 1.7 BREAKING`"* as the single most likely way to get this work wrong. Every row below that
belongs to the **C++ track** is labelled **`[C++ track]`**: governed by `[const §XVII.1]`, **not** by
`[const §X.7]` — it is not a C-ABI symbol, it consumes no error-code slot, it moves no version
macro, and it is absent from `tools/capi_freeze.sha256`'s manifest. `[const §X.7]` is cited for those
rows **to record that it is NOT engaged**, which is the disposition
`.specify/456-table-view-seal.md` set for a C++-only break.

---

## 1. I-1 — the index invariant `fixpp_msg_remove_tag` breaks

### 1.1 The two collaborating shapes

Read them at source; both are in `src/capi/capi_internal.hpp` and `src/capi/message_write.cpp`.

- The outbound accumulator owns a top-level `std::pmr::vector<AccumulatorEntry>` named `entries`.
  `AccumulatorEntry`'s own comment gives its tag field two meanings — *"scalar: field tag; group:
  NoXXX count tag"*.
- `fixpp_group_builder` holds a `std::uint32_t group_field_index`, documented as *"index of the
  group `AccumulatorEntry` — in `accumulator->entries` (top-level) or in the parent entry's
  `instance.fields` (nested)"*. `resolve_group` dereferences it by subscript.

So an open builder's identity is an **index**, not a pointer and not a handle to the entry.

### 1.2 The invariant, stated positively

The accumulator's own code states it, at `upsert_pair`: a write lands *"without moving any existing
entry, because open group builders and entries hold indices into these vectors."*

`fixpp_msg_remove_tag` predates and violates that sentence. Its body binds `auto& entries =
h->accumulator->entries;`, `std::ranges::find_if`s on `e.tag == tag`, and calls `entries.erase(it)`
— and it **never consults `open_builders`**. Its only guards are a null handle and
`check_outbound_msg`.

⚠️ **Reallocation and erasure are different events, and the distinction is why no comment in the tree
covers this.** A `std::vector` reallocation moves the buffer and **preserves** every index; an erase
shifts the tail and does **not**. The accumulator's *"hold **INDICES** re-resolved per call (stable
under vector reallocation)"* comment is therefore **true as written** — it promises stability under
*reallocation*. **No comment anywhere claims stability under erasure.** A finding phrased *"the
comments are wrong"* is falsifiable and will be falsified; the finding is the invariant, not the
prose.

### 1.3 Two distinct failure modes — not one

Because `find_if` matches on `e.tag`, and a group entry's tag *is* its NoXXX count tag:

| mode | condition | what breaks |
|---|---|---|
| **(a) positional shift** | a **scalar** positioned *before* an open builder's group entry is erased | every later `group_field_index` is now one too high — the builder writes into the wrong entry, or past the end |
| **(b) target erasure** | the erased entry **is** the group the builder points at, because the caller passed the group's NoXXX count tag | the builder's index now names a different entry, or none. There is **no correct value to renumber it to** — the entry it named is gone |

⚠️ **A guard keyed on one mode misses the other.** A guard keyed on the erased **position** does not
fire when the group itself is the target and happens to be the last entry; a guard keyed on the
erased **tag** does not fire for an unrelated scalar before the group. This asymmetry is the whole
reason D-1's predicate is `!open_builders.empty()` — one predicate, keyed on neither position nor
tag, covering both modes and defeatable by no tag choice.

Mode (b) is also why re-indexing (the design note's Option B) is **rejected** rather than merely
not-preferred: it cannot repair mode (b) at all, so it would have to detect that case and refuse
anyway — leaving one call with two behaviours the caller cannot predict without knowing whether its
own tag argument happened to be a NoXXX tag of an open group.

### 1.4 The population condition — why `remove_tag` is the only offender

**The claim:** *`fixpp_msg_remove_tag` is the only non-append-only mutation of the accumulator's
top-level `entries` vector reachable from the C ABI.*

**How it is established, and why that shape matters.** A `std::pmr::vector` can only be resized
through a **non-`const` binding** to it. So the claim rests on the *receiving signature* of every
binding of `entries`, not on a search vocabulary — a vocabulary blind to `swap`, `assign`,
`erase_if`, `std::remove_if` or a mutation through an escaped non-`const` reference is equally blind
however wide the corpus it is run over.

**Re-derivation recipe — run it, do not read a list here:**

```
grep -rn "accumulator->entries\|acc\.entries\|acc_->entries" src/
# then read the receiving signature on the other side of every binding.
# control — a DIFFERENT pattern, positive on the SAME corpus, so the enumeration
# above is a measurement and not a broken invocation:
grep -rn "open_builders" src/
```

The classification that decides it: the `const&` receivers cannot move anything; `upsert_entry` and
`upsert_pair` are **append-or-in-place only** (a linear scan returning an existing entry, else
`emplace_back`); `fixpp_msg_group_begin` appends; `resolve_group`'s subscript reads. Exactly one
binding erases. **A new offender would appear as a non-`const` binding whose callee resizes or
reorders** — that is the condition to test, and it does not rot the way a total does.

⚠️ The **nested** vectors have no `remove_tag` analogue at all: `entry_set_bytes_impl` and the nested
`upsert_pair` site operate on a `GroupInstance`'s own `fields`. Only the **top-level** arm of
`resolve_group` is exposed to this defect.

⚠️ There is a **distinct sibling** `resolve_group` / `resolve_instance` pair in
`src/wire/body_builder.cpp`. Same shape, append-only, no `remove_tag` analogue. Do not conflate them
with the C-ABI resolvers in `src/capi/message_write.cpp`.

### 1.5 The two index FAMILIES — and why only one is shifted by an erase

| family | what it indexes | shifted by `remove_tag`? |
|---|---|---|
| `group_field_index` | the accumulator's top-level `entries`, or a parent instance's `fields` | **yes**, on the top-level arm — this is I-1 |
| `instance_index` | a group's `instances` vector | **no** — nothing erases from `instances`; it is append-only-stable |

⚠️ **"Not shifted by `remove_tag`" is NOT "cannot be out of range", and the table's column header is
the only thing scoping it.** An out-of-range **or a stale-but-in-range** index is silent UB across
**both** families today — both are dereferenced by raw subscript, in `noexcept` functions carrying
zero asserts. So D-1's correctness rests on `group_field_index` alone, while **D-2b's bounds refusal
covers both families**: the check is **index-provenance-agnostic**, and reading this row as
"`instance_index` needs no check" would narrow the mechanism below the claim. See
[`contracts/msg-index-bounds.md`](./contracts/msg-index-bounds.md) §2.

### 1.6 Why no sanitizer in the matrix can observe I-1

This is a property of the **data layout**, so it belongs here rather than in the test plan — and it
is what forces every witness in `quickstart.md` to assert **committed payload bytes** instead of a
clean run.

- The outbound accumulator carves from a per-message `std::pmr::monotonic_buffer_resource` over a
  shell-owned block. An overrun lands **inside a live allocation**. There is no redzone to trip.
- ASan's container-overflow annotation does not apply: libstdc++ does not annotate a vector with a
  non-default allocator, and these are `std::pmr::vector`.
- UBSan `-fsanitize=bounds` covers fixed-size arrays; `operator[]` on a vector is plain pointer
  arithmetic.
- libstdc++/libc++ hardening is **not set by any project flag** — re-derive:
  `grep -rn "_GLIBCXX_ASSERTIONS\|_GLIBCXX_DEBUG\|_LIBCPP_HARDENING" cmake/ CMakeLists.txt
  CMakePresets.json`, with a positive control on the same corpus (`FIXPP_WERROR`) so a zero is a
  measurement and not a broken command. ⚠️ **That zero is not "hardening is off in every preset."**
  Measured this session: libstdc++ enables `_GLIBCXX_ASSERTIONS` **by default at `-O0`** (clang +
  libstdc++, no project define) — a hard out-of-range `vector::operator[]` **aborts** — and does
  **not** at `-O2`, independent of any project flag; `linux-clang-debug`'s `CMAKE_BUILD_TYPE=Debug`
  builds at `-O0`. **The claim that survives is narrower and still holds**: a **stale-but-in-range**
  index (the shape I-1 actually produces) is invisible to `_GLIBCXX_ASSERTIONS` regardless of
  optimization level — the assertion checks the bound, not the value — so the committed byte string
  remains the instrument for THAT arm.

⇒ **A clean ASan/UBSan/TSan run over the defect is not evidence of anything**; it is the expected
output of an instrument that could not report otherwise.

⚠️ The one shipped cell that holds a builder open across a `remove_tag` —
`MessageWrite.ZeroGlobalHeapSetCommitGuard` in `tests/capi/message_write_test.cpp` — **dodges the
defect on purpose** and says so: *"The group is the first top-level entry, so the `remove_tag(11)`
below cannot shift its index (#447)."* Its sibling
`CapiSetData.AppendingWhileAGroupBuilderIsOpenKeepsTheBuilderOnItsGroup` proves the **append** case
safe, which is a different claim.

---

## 2. The state each refusal is keyed on

⚠️ **This section is the reason the feature is not a "return a new code" change.** Each refusal is
keyed on a *state*, and in two of the three cases **the error code alone does not identify the
state** — another condition produces the same code. A witness that asserts only the code measures
nothing. This is the design note's §6 spurious-hit rule, restated as a data fact.

### 2.1 S-1 — a live open builder (`#447`, C-ABI track)

**The state:** `accumulator->open_builders` is non-empty.

**Why that set is both necessary and sufficient as the population of live index holders** — three
structural facts, none of which is a census, so none rots:

1. A **closed** builder is inert. `fixpp_msg_group_end` sets `b->open = false` under the comment
   *"invalidates the builder + its entries (validity ⇒ builder->open)"*, and `check_builder` returns
   `FIXPP_ERR_INVALID_HANDLE` on `!b->open` **before any resolution**. `check_entry` delegates to
   `check_builder`, so an `fixpp_entry` cannot outlive its builder's validity either.
2. A **nested** builder's own `group_field_index` indexes its parent instance's `fields`, which
   `remove_tag` never touches — but `resolve_group` **recurses to the root builder**, whose index
   *is* into `entries`. So every open builder at every depth resolves through exactly one root index
   into `entries`.
3. Every one of those roots is on the `open_builders` stack. `fixpp_msg_group_begin` does **not**
   refuse while a builder is open — it appends another top-level group entry and pushes another root
   — so the stack can hold **several** roots simultaneously.

**The refusal's true width, stated because B-447-1 requires it.** D-1's guard fires **before** the
`find_if`, so two classes that are safe today are refused:

- **absent tag, builder open** — returns `FIXPP_ERR_OK` today, erases nothing, corrupts nothing. The
  declaration's own published contract calls it out: *"Idempotent (absent returns OK)"*. A caller
  relying on that documented idempotence loses it.
- **present tag positioned after every live root's group entry** — `vector::erase` shifts only
  indices **above** the erased position, so no live `group_field_index` moves. Safe today; refused.

Uniformity is chosen over precision deliberately: a narrow guard whose outcome depends on the
caller's tag argument *and* on the accumulator's current layout reintroduces exactly the
unpredictability that rejects Option B, and it is **two** predicates each with its own edge on a path
no sanitizer can observe (§1.6).

**Migration is one line** for every refused class: move the `remove_tag` before `group_begin` or
after `group_end`.

### 2.2 S-2 — clone's source state and its degradable half (`#458`)

**Two different states are in play, and confusing them is how the previous revisions of the design
note produced three successive false claims.**

**(A) The state the C-ABI refusal is keyed on — `fixpp_msg_clone`:**

> the source handle is **dict-backed**, *and* the clone's dict-backed re-parse of the copied frame
> **failed**.

Read it in `fixpp_msg_clone`'s body: the dict-backed arm runs only `if
(h->view->is_dict_backed())`, seats `clone->owned_tv_` from `h->view->membership_copy()`, and parses.
The fallback below it is entered when `!clone_view` — which is **both** the dict-free source (fine,
nothing was lost) **and** the failed dict-backed re-parse (the defect: a handle that reports success
while having silently become dict-free). The shipped comment on that fallback already concedes the
defect and its own history, including that *"The routes below are NOT exhaustive."*

**The narrowest true claim the fix makes:**

> The dict-backed re-parse either succeeds, or the operation refuses. **No path produces a handle
> that reports success while having silently become dict-free.**

**(B) `[C++ track]` The degradable state that is deliberately RETAINED.** A handle minted from a
**dict-free** source never had a dictionary to lose. Its `OffsetTable` can still degrade — the
dict-free two-argument `MessageView(frame_view const&, std::pmr::memory_resource*)` constructor is
`noexcept` and checks nothing, while `OffsetTable::build` catches `std::bad_alloc` and degrades in
place (`entries_.clear(); overlay_.clear(); status_ = fail(core::error::out_of_memory);`). That
degradation is **publicly reported** through `view().offsets().build_status()`, and it is **pinned by
a shipped test** — `ReifyErrorContract.ViewRebuildOomDegradesNotTerminate` in
`tests/dictionary/reify_dispatch_test.cpp`, whose fixture builds its source with the two-argument
overload and whose comment reads *"a throwing mr at first field access must NOT terminate"*.

⚠️ **The claim in (A) says nothing about (B), and must not be widened to.** A dict-free handle does
not *report success while having silently become dict-free*; it reports success while being dict-free
**as minted**, and says so through a public channel.

**The asymmetry to be aware of when reading the two materialisation arms:** the **dict-backed** arm
goes through `Parser<Index>::parse`, whose body *"`if (auto s = mv.offsets().build_status(); !s) {
return … s.error(); }`"* — it **checks the build status and propagates it**, so a dict-backed
materialisation whose table build fails is not a degraded view, it is a failed parse. The
**dict-free** arm checks nothing. That is why only the dict-free side has a degraded outcome at all.

**(C) `[C++ track]` A span that frames to NOTHING is also retained.** `Framer::feed` on a zero-byte
span **succeeds with an empty span**, so a handle minted over it still materialises and still
returns, with a default-constructed empty view. Shipped cells across
`tests/dictionary/reify_dispatch_test.cpp`, `tests/codegen/vlatest_dispatch_exclusion_test.cpp` and
`tests/integration/fixt_cross_vocabulary.cpp` pin this. A refusal here was proposed and **withdrawn**;
re-introducing it reds those cells.

⚠️ **`[const §X.7]` is recorded for (B) and (C) as NOT ENGAGED** — they are public C++ surface,
governed by `[const §XVII.1]`.

### 2.3 S-3 — the session config's lifecycle position, and why it is in this file

**The `#452` refusal itself is keyed on BYTE CONTENT, not on lifecycle.** The two setters refuse a
configured string containing any byte `< 0x20` (SOH `\x01` included) or `'='` (0x3D).

**So why is lifecycle here at all?** Because the code that refusal returns —
`FIXPP_ERR_CAPI_CONFIG_INVALID` — is **already produced by a lifecycle condition on a neighbouring
call**, and a witness that asserts only the code cannot tell the two apart.

Read it in `src/capi/session.cpp`, under the comment *"Register-before-start (FR-004): a
session_open after engine_start is a C-ABI-enforced config error"*: `fixpp_session_open` returns
`FIXPP_ERR_CAPI_CONFIG_INVALID` when `engine->engine_started_` is already true. The same code is
produced by a **dead engine handle**, and by the **pre-existing** null/empty-argument refusals in the
setters themselves.

**The consequence for every `#452` and `#458` witness:** assert the **exact** code **from the call
under test, in a state where no confusable producer is live** — and pair it with a control showing
that the *injected condition* is what produced it. This is the design note's own spurious-hit rule;
`quickstart.md` carries it per scenario.

**The lifecycle ordering itself is unchanged by this feature.** No row below moves it; it is
modelled because it is a confusable state, not because it is edited.

---

## 3. State transitions

Only two genuine state machines are touched. Neither gains a state; both gain an **edge that
refuses**.

### 3.1 T-1 — the group builder's open/closed life (`#447`)

```
        fixpp_msg_group_begin / fixpp_entry_group_begin
  (none) ──────────────────────────────────────────────▶ OPEN   (pushed onto open_builders)
                                                          │
                     fixpp_msg_group_end  (b->open = false)│
                                                          ▼
                                                        CLOSED  (inert: check_builder → INVALID_HANDLE)
```

| transition | today | after D-1 |
|---|---|---|
| `remove_tag` while **any** builder is OPEN | erases; silently invalidates live indices; returns `FIXPP_ERR_OK` | **refuses** with `FIXPP_ERR_INVALID_HANDLE`; erases nothing; **the accumulator is unchanged and every open builder stays usable** |
| `remove_tag` with **no** builder open | erases if present, returns `FIXPP_ERR_OK`; absent → `FIXPP_ERR_OK` | **unchanged** |
| any builder's own transitions (`group_begin`, `group_end`, nesting) | — | **unchanged** — D-1 adds no edge to this machine, it only refuses a call made *during* OPEN |

⚠️ **"The accumulator is unchanged" is an assertable post-condition, not a restatement of the return
code**, and it is the one an Option-B implementation would fail. See `contracts/msg-remove-tag.md`.

### 3.2 T-2 — the engine's register-before-start ordering (context for S-3)

```
  fixpp_engine_create ──▶ CREATED ──fixpp_session_open──▶ (sessions registered)
                             │
                             └── fixpp_engine_start ──▶ STARTED
                                                          │
                          fixpp_session_open here ────────┴──▶ FIXPP_ERR_CAPI_CONFIG_INVALID
```

**Unchanged by this feature.** Modelled because the STARTED arm is a confusable producer of the same
code the `#452` setters now return (§2.3), and because `fixpp_session_register_callback` and its
sibling carry the same *"MUST precede `fixpp_engine_start`"* ordering.

### 3.3 T-3 — `[C++ track]` handle materialisation moves from LAZY to EAGER

Today `owning_message_handle::view()` is memoized and materialises **lazily** on first call; the
factory's own comment documents deferring the fallible work — *"leave `view_cache_` empty (built
lazily on first `view()`)"*. Under D-4 the factory materialises **eagerly** and refuses through its
**existing** `core::expected_t` channel.

| transition | today | after D-4 |
|---|---|---|
| dict-backed source, re-parse **fails** | factory returns a live handle; first `view()` silently yields a dict-free view | factory returns `unexpected` carrying **the wire error the failed parse produced** (not `dict_reify_oom`, not a generic sentinel); **no handle is constructed** |
| dict-backed source, re-parse succeeds | live handle | **unchanged** |
| dict-free source, healthy | live handle | **unchanged** |
| dict-free source, table build degrades | live handle; `build_status()` reports it | **unchanged** (§2.2 B) |
| span that frames to nothing | live handle, empty view | **unchanged** (§2.2 C) |

⚠️ **No accessor is added.** An earlier proposal for a three-state status accessor was **withdrawn**;
a witness asserting one would pass against a design that does not exist.

⚠️ **A calibration consequence, registered rather than assumed.** Eager materialisation moves *where*
the allocations happen without changing the *sequence*, so a shipped cell keyed on a
`fail_on_call_n` ordinal is **expected** to survive — but a structural expectation is not a
measurement. Derive the ordinal by instrumenting the candidate build; if it moved, **recalibrate the
cell, never rewrite it to assert a refusal** (that is the rejected option, not D-4).

---

## 4. The error codes, keyed on the CONDITION that produces them

⚠️ **Read this table as `condition → code`, never as `code → producers`.** A clause binding
`FIXPP_ERR_CAPI_CONFIG_INVALID` to a producer set is precisely what this PR amends in the C-ABI owner
document (FR-019), because such a clause is falsified by every producer added or moved since it was
written. To answer *"who returns this code"*, derive it: `grep -rn "return
FIXPP_ERR_CAPI_CONFIG_INVALID" src/`, then classify each hit by its **enclosing entry point**.

No enumerator is added to `include/fix/c_api/error.h`. Every value below already exists.

| # | condition | code | track |
|---|---|---|---|
| **EC-1** | `fixpp_msg_remove_tag` called while the accumulator's `open_builders` is non-empty | `FIXPP_ERR_INVALID_HANDLE` (4) | C-ABI |
| **EC-2** | a `group_field_index` or `instance_index` dereference reachable from a C-ABI entry point would be **out of range** for the container it subscripts | `FIXPP_ERR_INVALID_HANDLE` (4) | C-ABI |
| **EC-3** | `fixpp_msg_clone` on a **dict-backed** source whose re-parse failed — the code is `translate()`'s image of the `core::error` that failure produced | see §4.1 | C-ABI |
| **EC-4** | `std::bad_alloc` during clone construction | `FIXPP_ERR_CAPI_CONFIG_INVALID` (10) — **preserved**, narrowed | C-ABI |
| **EC-5** | a **non-`std::bad_alloc`** exception on the clone path | **no code** — fatal log, then `std::abort()`. See §4.2 | C-ABI |
| **EC-6** | a configured CompID / BeginString string containing a byte `< 0x20` or `'='`, at `fixpp_session_config_set_comp_ids` / `_set_begin_string` | `FIXPP_ERR_CAPI_CONFIG_INVALID` (10) | C-ABI |
| **EC-7** | the same byte condition on `SessionConfig::sender_comp_id` / `::target_comp_id` / `::begin_string` / `supported_msg_types[].msg_type`, observed at `Session::open` | `core::error::invalid_session_config` | `[C++ track]` |
| **EC-8** | a **dict-backed** re-parse failure inside the reify factory | the **wire error the failed parse produced**, through the factory's existing `core::expected_t` channel | `[C++ track]` |

**EC-2 is not marked BREAKING.** The arm it replaces is **undefined behaviour**, not a documented
success. Nothing that is defined today changes.

**EC-6's code is chosen for one reason and it is not the one a reader expects.** Every sibling
`fixpp_session_config_set_*` in `src/capi/config.cpp` already returns this code, including their own
existing null/empty refusals, and that file's header comment asks for exactly this shape — *"setters
validate cheaply (else defer to open/create)"*. A byte scan is cheap.

⚠️ **`error::invalid_session_config`'s `translate()` arm is NOT re-pointed.** It is **grouped with
`error::clock_not_set`** — `case error::invalid_session_config: case error::clock_not_set: return
FIXPP_ERR_THREAD_CONFIG;` — so re-pointing it would silently move an unrelated error and would be a
broader C-ABI change than the three refusals this feature is scoped to.

⚠️ **There is NO published code asymmetry between EC-6 and EC-7, and no limitation row for one.** A
claim that a C consumer and a C++ caller see different codes for the same byte in the same field was
**deleted**, not narrowed: both engine `co_await …open()` sites discard the result, and
`fixpp_session_open` never calls `Session::open()` at all — it calls `Engine::register_session`. No
supported route lets any consumer observe the two side by side. **Re-derivation recipe:** re-run
`grep -rn "open()" src/ | grep -i "co_await"` and `grep -rn "\->open()\|\.open()" src/`, and read
what each site does with the result. Re-introduce the claim only on exhibiting a caller that both
propagates that error to a boundary **and** puts it through `translate()`.

### 4.1 EC-3's code is a CONDITION plus a function, not a closed list

The C-ABI code for a failed dict-backed re-parse is **`translate(parsed.error())`** — the current
image, under `src/capi/error.cpp`'s `translate()`, of whatever `core::error` the parse reported.
`translate()` is already **total** over `fixpp::core::error` (*"Total switch, no default (see file
header)"*), with `-Wswitch` as the enforcement and `expected_error_map.csv` as the audited oracle.

**Nothing is minted.** The design rejected minting a new enumerator on cost, not feasibility: a new
code would red `include/fix/c_api/error.h`'s freeze hash, need an `expected_error_map.csv` oracle
update, an append-only line in `tools/abi_history/error_codes_v1.txt`, and an arm in
`introducing_minor()` — whose own comment warns that *"A literal scalar-bump to 4 would silently
downgrade EVERY existing 0.2/0.3 code at consumer_minor=3"*.

The arms that exist **today**, recorded so a witness can assert an *exact* code — and **re-derivable
from `translate()` rather than trusted from here**:

| `OffsetTable` failure route | C-ABI code |
|---|---|
| `fail(core::error::out_of_memory)` — the failing-allocator route | `FIXPP_ERR_UNKNOWN` (2) |
| `wire_offset_table_full` (the raised-cap route), `wire_tag_out_of_range` | `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` (101) |
| `wire_invalid_field_format` | `FIXPP_ERR_WIRE_INVALID_FRAME` (100) |

⚠️ **The OOM route's `FIXPP_ERR_UNKNOWN` is documented v1.0 behaviour, not a gap this change opens.**
**L-049-2** in the live `spec/behaviors-and-limitations.md` already records that *"`out_of_memory`
has no cross-cutting OOM code and a `switch(error)` cannot be call-site-dependent"*. The B&L delta
**references** that limitation; it does not restate it.

⚠️ **`FIXPP_ERR_DICT_OOM` (202) is rejected as the OOM code** — its name scopes it to dictionary
load/reify, and reusing it would make `fixpp_msg_clone` report a dictionary-subsystem failure for a
wire-parse allocation failure.

### 4.2 EC-5 — a return code entry that has no return code

Today a `std::logic_error`, or any foreign exception, raised inside `fixpp_msg_clone`'s `try` is
caught by the body's **blanket `catch (...)`** and **returns `FIXPP_ERR_CAPI_CONFIG_INVALID`**. Under
the nested boundary it falls past the inner `catch (std::bad_alloc const&)` into the function's own
**outer `catch (...)`** and **terminates the process after a fatal log**.

**A return becoming a process abort is a behaviour change on a C-ABI symbol whatever its
reachability**, so it is *declared* in B-458-1 rather than left to be inferred from an arm list.

⚠️ **Its trigger set is NOT enumerated, and that is stated rather than hidden.** Whether a
non-`bad_alloc` exception can be produced on clone's construction path at all is **undecided** —
registered in the design note's NOT MEASURED register. A limb declared unreachable on the strength
of an `// LCOV_EXCL_LINE` comment would be a classification nothing enforces, which is the defect
class this whole feature is written against.

⚠️ **This uncertainty does not destabilise the declaration and must not be used to.** The
reachability is **declined, not asserted**; the behaviour change is declared anyway.

---

## 5. What is NOT in this model — stated, because "nothing here" is itself a claim

- **No new error enumerator.** `include/fix/c_api/error.h` gains nothing (§4).
- **No new exported symbol.** `tests/abi/golden/fixpp_capi_symbols.txt` is expected **byte-unchanged**
  — a validation-tightening exports nothing new. **Check, do not claim**: re-run the
  `nm --defined-only --extern-only` diff step that `.github/workflows/abi-golden.yml` performs.
- **No new public C++ method on the reify handle.** The withdrawn status accessor does not exist.
- **No change to the nested `GroupInstance::fields` vectors' mutation shape** (§1.4).
- **No change to the engine lifecycle ordering** (§3.2).
- **No limitation row for the three refusals** in the B&L delta. `L-452-1` is deleted (§4).
  No limitation row covers any of the three refusals; the delta's limitation rows record only **residuals the design leaves unfixed** — read them from the live file rather than from a count here. (FR-016)
- **The probe-cap degradation is OUT OF SCOPE and must not be claimed as covered.**
  `OffsetTable::build`'s probe-cap arm sets `skip_insert = true` under *"DoS bound: leave this occ
  un-indexed"* and **never assigns `status_`** — so `build_status()` stays ok, `Parser::parse`
  **succeeds**, and the clone fallback is never entered. It is a degradation of a *successful*
  dict-backed parse: a different defect on a different path. **Re-derive:** read that arm and check
  whether it assigns `status_`.
- **RefMsgType(372) at the two reject builders is a CHECKED NEGATIVE, not a covered site.** Those two
  values are slices of an **inbound** frame, not config members. The condition they rest on: *the
  value at those sites is produced by a scanner that terminates a non-Data field at SOH.*
  **Re-derive:** read how `scan_frame_header` bounds the value it assigns to `h.msg_type`, and
  confirm tag 35 is not reachable as a Data tag through `dict_hooks::data_tag_for_length`. ⚠️ An `=`
  **can** survive into such a value — fixpp's own scanner does `++i;  // skip '='` before taking
  `vstart`, so it splits at the **first** `=` only. What a **counterparty** parser does with a second
  `=` is **not measured and not claimed**; it is a recorded residual.

⚠️ **The last bullet is also why the `#452` predicate must be named and documented as a POLICY
FLOOR, not as the FIX grammar.** `=` *can* appear in a FIX field value as fixpp parses it. The rule
is a conservative compatibility/security floor inherited from the existing credential guard — which
is why a name such as `contains_forbidden_config_byte` states what is true and
`is_valid_fix_field_value` does not.

---

## Normative references

- `.specify/447-458-452-capi-refusals.md` **v0.10** — the design authority. §1.1/§1.1a (I-1 and the
  reachability classes), §1.2 (the sanitizer argument), §1.3/§1.4 (D-1, D-2, D-2b), §2.1–§2.4b (the
  clone and reify states), §3.1–§3.4 (the `#452` population and D-5a/b/c), §4 (the decision table),
  §5a (the declaration population), §6 (the test seams), §7 (the residuals), §8 (NOT MEASURED).
- `.specify/constitution.md` — `[const §X.7]` (the pre-first-release breaking-change procedure and
  its four obligations), `[const §XVII.1]` (the public-C++-API review trigger governing the
  `[C++ track]` rows), `[const §IX.1]` (the three dispositions, under which EC-2's unreachability is
  **assessed** rather than omitted).
- `.specify/456-table-view-seal.md` — the precedent for citing `[const §X.7]` **to record that it is
  NOT engaged** on a C++-only break.
- `spec/behaviors-and-limitations.md` — the **live** ledger. **L-049-2** is referenced by EC-3.
  Resolved rows live in the closed file; do not grep across the pair.
- `specs/090-capi-refusals/spec.md` — FR-001..FR-020 and SC-001..SC-014.
- `specs/090-capi-refusals/contracts/` — the per-surface contracts this model underlies.
