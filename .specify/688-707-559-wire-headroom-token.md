# fixpp#688, #707, #559 site 1 — the wire parser's unknown-field view, the writer's passkey and its scratch resource (lane W1 = B103 + B112)

> **Status: v0.7: scoped Gate A CONVERGED round 5 (P1=0, P2=0; P3s applied) (Codex codex_688_707_559_5…, 2026-11-28). v0.7: scoped round 4 (Codex codex_688_707_559_4…, Opus opus_688_707_559_4…) applied — post-judging P1=0 P2=2 P3=10. v0.6: R-W1-6 delta, D-688-2, the first-unknown index (§1.4a), over v0.5. v0.5: the /analyze findings applied (R-W1-5, 2026-11-28) over v0.4. v0.4: Gate A CONVERGED round 3 (2026-11-27): Codex P1/P2/P3 r1 3/7/2, r2 1/2/1, r3 0/2/1; Opus post-judging r1 1/9/7, r2 1/3/6, r3 0/0/9; 2 rewrites + this P3 pass. Owner-delegated sign-off (2026-11-27, parent decisions/speckit/pr-w1-688-707-559-owner-rulings.md).**
> Issue-mode design note. v0.5 (D-688-1, D-707-1/2, D-559-1) is implemented on the branch, and so is D-688-2 (v0.7). Branch
> `wire-688-707-559-headroom-token`, cut from `origin/main` at `b93b8a28`. What each round changed,
> and what it declined, is in "Gate A".
>
> **Rulings this note applies** (owner, 2026-11-27): the official FIX documents decide first; then
> QuickFIX's behaviour where copying it has no downside, with QuickFIX C++ as the drop-in target;
> correctness before everything else; every deviation is recorded in the B&L. Under the owner's
> delegation of 2026-11-27 the orchestrator ruled R-W1-1 to R-W1-4, each flagged for the owner's review
> at merge (§7; the rulings record is `decisions/speckit/pr-w1-688-707-559-owner-rulings.md` in the
> parent). **R-W1-4 applies as amended twice:** the note names the shapes expected to regress and how
> they are measured, and accepts nothing in advance. An over-band row is decided after the
> measurement, posted in the PR thread with its figures, and the owner's ratification there is a merge
> condition (§1.4, §7).
>
> *Read* means read from source at `b93b8a28`; *run* names a command or a compile a reader can
> repeat. The probes are inlined in §A so Gate A can re-run them. Claims about code name a function,
> member or file, never a line number. Re-derive any enumeration with the command next to it.

**Decisions.**

| Id | Issue | Decision |
|---|---|---|
| D-688-1 | #688 | `unknown_fields()` becomes the filtered view `[2b §4.8]` specifies: it walks the offset table and allocates nothing, so there is no exhaustion to report. It yields the tags the dictionary does not define (`table_view::tag_defined`), not the tags the MsgType does not declare (R-W1-3). |
| D-688-2 | #688 (R-W1-6, v0.6) | `MessageView<Index>` remembers where the first unknown entry is: one `std::uint32_t` index into `entries()`, in existing padding. The first `unknown_fields()` call over a non-empty table stores the index when it is representable; otherwise it leaves the sentinel and rescans on every call; later calls start at a stored index. The design requires both modes' `sizeof` and `alignof` to stay those of the tree before D-688-2, read before and after on each toolchain (§1.4a). |
| D-707-1 | #707 | `group_writer_token` gets 104's shape (a user-provided private copy constructor). `Writer::open_group` builds the `group_writer` itself, because `std::in_place` copies the key outside `Writer`. |
| D-707-2 | #707 | The census the issue asks for finds `dict::detail::snapshot_key` of the same shape, and W1 fixes it (R-W1-2). It gets a user-provided private default constructor and a user-provided **public** copy constructor, because `std::make_shared` copies it. |
| D-559-1 | #559 site 1 | Delete `Writer::scratch_mr_` and the constructor's `scratch_mr` parameter. The writer keeps no group bookkeeping, so there is nothing to put on it. W1 closes #559; its fourth site is fixpp#735 (R-W1-1). |

---

## 1. fixpp#688 — `unknown_fields()` over an exhausted resource returns an empty view

### 1.1 The premise at `b93b8a28` (read)

- `MessageView<Index>::unknown_fields()` (`include/fixpp/wire/parser.hpp`) builds a
  `std::pmr::vector<unknown_fields_view::kv>` (`unk_items_`) from the view's parse resource on its
  first call and caches it, with `unk_items_built_` as the flag. Both members are `mutable`.
- On `std::bad_alloc` it clears the partial list and keeps the flag set. That call and every later one
  return the same empty view (093 FR-015, `B-093-8`).
- `unknown_fields_view` (`include/fixpp/wire/unknown_fields.hpp`) exposes `empty()`, `begin()` and
  `end()` and nothing else. No state tells "no unknown fields" apart from "the list could not be built".
- Inside a session callback the view's parse resource is the per-session buffer B(L). Lazy reads draw on
  its `kCallbackReadHeadroom` term (`src/session/parse_capacity.hpp`), and its upstream is
  `detail::arena_upstream()`: null everywhere except MSVC debug, where it forwards.
- Group reads report this failure since 100: `group_view::read_failed()` and
  `OffsetTable::group_slices_status()`. The raw `OffsetTable::group_slices()` span still cannot be told
  from an absent group, and its status-bearing sibling is the way to tell.
- **The classification is not the one its contracts state.** `unknown_fields()` asks
  `hooks().classify_fn()`, and the classify lambda of `dict_hooks::for_table_view` (defined in
  `parser.hpp`) returns `table_view::field_valid_for(msg_type, tag)`, "declared for msg_type".
  - `unknown_fields.hpp`'s header comment, its shape oracle and `[2b §4.8]` say the view yields
    "ONLY dictionary-MISSING tags". A tag the dictionary defines but the MsgType does not declare is
    the validator's `wire_unexpected_tag`.
  - 2b's Gate A made that split (`research/reviews/opus_2b_wire_adversarial_review.md` in the parent,
    the finding on dictionary-known-but-invalid-for-this-MsgType tags). The validator keeps it through
    `table_view::tag_defined`: `dictionary_driven_validator`'s unexpected-tag step answers
    `wire_unexpected_tag` for a defined tag and `wire_undefined_tag` for the rest.
  - **It is observable on every FIX 5.0 session.** The FIX 5.0 QuickFIX-XML files have an empty
    `<header/>`. So 081 keeps `field_valid_for(msg_type, T)` false for every FIXT framing tag T, and
    the validator accepts those tags through the validator-private `table_view::is_fixt_framing_tag`
    (081 contract `validation-acceptance.md`, "Parser-containment pin";
    `tests/dictionary/fixt_header_merge_test.cpp` T006).
  - `unknown_fields()` exempts only 8, 9 and 10, so on a FIX 5.0 parse it yields the rest of the
    standard header (34, 49, 52, 56, …) as unknown.
  - Those tags are defined: `grep -c "number='49'" dictionaries/FIX50SP2.xml` prints 1. The file
    quotes its attributes with `'`, so a `"`-quoted grep prints 0 for every tag, even one that is
    certainly present (`number="11"`). That is the grep's known negative.
- **What the issue got right:** the condition, the evidence and the RED cell.
- **What it leaves out:** the design it is measured against. `[2b §4.8]` (`.specify/2b-wire.md`) and
  its shape oracle `specs/004-wire-codec/contracts/unknown_fields.hpp` specify a *filtered view over
  the offset table* with "no vector materialization". brain `components/wire.md` lists that among the
  wire layer's locked decisions, against "materialising a vector — which would allocate, violating
  `[const §VIII.5]`". The materialised list arrived in 004's Gate B rounds (`37aa434c`,
  `c16d48bd`).
  - Those rounds' records (`research/reviews/codex_pr68_*` in the parent, finding PR68-02 and its
    carry-overs, and PR68-10) argue *what* it classifies against: a dictionary bound at `Parser`
    construction, and that dictionary's lifetime.
  - None of them weighs storing the list against filtering on the fly, and no decision record does
    either. Re-derive with `grep -n -i -E 'materiali|unk_items|filter' research/reviews/codex_pr68_*
    decisions/speckit/004-wire-codec-*.md` in the parent. `unknown_fields.hpp`'s own header comment still says "no
  vector materialization". The failure #688 describes exists only because of that drift.
- Re-derive the callers: `git grep -n 'unknown_fields()' -- src include tests bench bindings`.
  - `src/` has none, and the C ABI has none (`git grep -n unknown_fields -- include/fix src/capi`
    prints one comment in `src/capi/message_write.cpp` and no call):
    `[2b §5]` keeps every wire type out of `<fix/c_api.h>`.
  - No binding calls it.
  - The callers are tests: `tests/wire/unknown_fields_test.cpp`,
    `tests/session/inbound_frame_dispositions_test.cpp` (Q-33) and
    `tests/dictionary/reify_shared_table_concurrency_test.cpp`.
  - The classify hook has one more reader, that last test, which calls `classify_fn()` directly.
    Re-derive the hook's readers with `git grep -n 'classify_fn()' -- include src tests bench`. In
    `include/` and `src/` the only reader is `unknown_fields()`.

### 1.2 FIX documents and QuickFIX

- **FIX documents.** The FIX documents define no engine API. What they decide about an undefined tag
  (Reject with SessionRejectReason 0, or acceptance as a user-defined field) belongs to the validator
  (`[2b §6.5]` rule 5, 101's `wire_undefined_tag`), and nothing in W1 changes it.
- **QuickFIX.** None of the three engines has an unknown-field accessor. Each builds the whole field map
  before the application sees the message:
  - C++: `Message::setString` appends every field into `FieldMap`, whose storage is a
    `std::vector` under `ALLOCATOR`.
    - `Session::next(const std::string&, …)` builds the `Message` inside a `try` that catches
      `InvalidMessage` only.
    - So an allocation failure leaves `next` as an exception before any application callback runs.
    - `Session::verify`'s `std::exception` catch runs later, on a message already built.
  - J: `AbstractIoHandler` parses the message into `FieldMap`'s `TreeMap`.
  - Go: `doParsing` sizes `msg.fields` from the SOH count in one `make`.
- So in all three, an application that has a message has every field of it. Construction either
  completes with the whole field map or fails before delivery. A field list the engine
  could not build surfaces as an exception or a disconnect, never as an empty list.
- The QuickFIX observable is therefore *completeness*, not a status bit, and that is what D-688-1
  delivers. QuickFIX does not split known fields from unknown ones; that split is fixpp's own surface
  and is not a deviation.
- **QuickFIX does separate the two kinds of tag that R-W1-3 separates.** In `DataDictionary.h`,
  `checkValidTagNumber` refuses a tag no field defines (`InvalidTagNumber`), and `checkIsInMessage`
  refuses a defined tag the message does not declare (`TagNotDefinedForMessage`). D-688-1's
  "unknown" is the first kind.

### 1.3 Options

| | Option | Outcome |
|---|---|---|
| **C** | **The filtered view of `[2b §4.8]`.** The view walks `OffsetTable::entries()` and yields each entry the dictionary does not define, classifying as it goes. Nothing is stored. | **Chosen (D-688-1).** The failure cannot occur, so there is nothing to report. It restores the locked design. |
| A | A status on `unknown_fields_view` (`read_failed()`, as `group_view` has), set on exhaustion. | **Fallback**, if Gate A rejects C, or if the owner declines an over-band bench row (§1.4). The application learns the list failed but still cannot read it, where QuickFIX would have given it every field. It keeps the materialised list `[2b §4.8]` rejects. |
| B | A status-bearing sibling (`unknown_fields_status()`), as `group_slices_status()` is to `group_slices()`. | Rejected. It has A's shortcoming, and it leaves `unknown_fields()` ambiguous for every caller that does not switch. |
| D | Size `kCallbackReadHeadroom` to hold the whole list. | Rejected. One `kv` per field at the entry cap is a large multiple of the term, on every session. It covers only the session's buffer, not a caller's own resource. 093 OD-18 rejected it already. |

**Why 093 chose the catch.** 093 OD-18 (`specs/093-inbound-frame-dispositions/plan.md`) needed the
smallest change inside 093's scope that removed the `std::terminate`: a catch that keeps `noexcept`.
It rejected new result-bearing `try_*` lazy APIs (a public surface with its own versioning, for a
failure 093 did not cause) and a headroom large enough to make exhaustion impossible.
- Neither OD-18 nor research R-10 (`specs/093-inbound-frame-dispositions/research.md`) names
  `[2b §4.8]`'s non-allocating view. Re-derive with `grep -n -E '4\.8|filter'` over that plan, that
  research file and the parent's `decisions/speckit/093-inbound-frame-dispositions-gatea.md`.
- So D-688-1 reverses no recorded rejection. OD-18's reasoning stays
in 093's bundle as history.

### 1.4 D-688-1 — the shape

> ⚠️ **Amended in v0.6 by D-688-2 (§1.4a, R-W1-6).** `MessageView<Index>` now stores one index, where
> the first unknown entry is. Three statements below change, each flagged in place: "The first unknown
> entry is found when the view is built", "Side effect, stated as a property", and the Cost bullet on
> a frame with no unknown fields.

- **The view holds its context by value:**
  - the remaining entries, as a pointer pair into the table's `entries()` storage;
  - the frame's base pointer;
  - the classify function and its opaque dictionary, copied out of the view's `dict_hooks`;
  - `msg_type()`, as a `string_view`;
  - the generation token, in debug builds only (the generation check below).

  Nothing in it points at the `MessageView` object itself. A moved-from `MessageView` keeps its arena
  storage (the defaulted move carries the `pmr::vector` buffers), so a view outlives a move exactly as
  far as today's view does.
- **The generation check (`[2b §6.4]`).** Carrying the token does nothing by itself: an accessor must
  call the check.
  - The iterator's copy of the token is debug-only, under `#ifndef NDEBUG` as `View::gen_` is, so
    neither the view nor the iterator grows in release for it.
  - `unknown_fields()` calls `MessageView`'s own `check_alive()` before its scan, because building
    the view reads `entries()` and the frame to find the first unknown entry. That is the check
    `bytes()` makes today.
  - The iterator's `operator*` and `operator++` call the check, as `group_view`'s accessors do.
  - The view's `begin()`, `end()` and `empty()` read no frame memory after construction, so they
    do not check.
  - The base iterator checks nowhere, so these checks are new (§1.6 items 5 and 6).
- **The dictionary's lifetime condition widens.**
  - `opaque_dict` points at the caller's `table_view` on the borrowed route, or at the table the
    `Parser`'s owner holds on the owned route (fixpp#495).
  - Today classification runs once, on the first call, and iteration then reads only the stored `kv`
    list, so the dictionary must be alive at the first call.
  - After D-688-1 every construction, `++` and `empty()` classifies, so the dictionary must outlive the
    last iteration of every view.
  - The hazard is not new (a view whose dictionary has gone already dangles at today's first call),
    but its window grows from the first call to the last iteration. `unknown_fields()`'s comment and
    `B-688-1` state the condition.
- **The iterator contract** (a public C++ change; the probes are §A.4):
  - **The iterator holds the same context by value**, so `auto it = mv.unknown_fields().begin();` does
    not dangle at runtime on the temporary view: it stays valid while the `MessageView`'s parse
    storage lives. At runtime that matches today, where the iterator points into the `MessageView`'s
    vector. It does not match today at compile time on clang (next bullet).
  - **`operator*` returns `kv` by value.** Rejected: keeping `kv const&` by storing a `kv` in the
    iterator. That reference dangles across `++`, a hazard today's iterator does not have. **Pinned**
    by `static_assert(std::same_as<decltype(*std::declval<unknown_fields_view::iterator const&>()),
    unknown_fields_view::kv>)`, in the grouped wire bucket. It is RED on the base, which returns
    `kv const&`, on clang and gcc (§A.4). The iterator-cached-reference mutant (§1.6 item 6) is the
    design it kills, and no behavioural cell can: range-`for` reads each element before `++`, and
    the storage stays alive, so ASan has nothing to report.
  - **`begin()` and `end()` drop `[[clang::lifetimebound]]`.** Today both carry it on the view.
    After D-688-1 the iterator no longer points into the view, so the attribute's claim becomes false,
    and keeping it makes clang reject the idiom the design makes valid: `auto it =
    mv.unknown_fields().begin();` is "temporary whose address is used as value of local variable
    'it' will be destroyed at the end of the full-expression [-Werror,-Wdangling]" (`-Wdangling` is
    on by default). The hidden `_base` preset in `CMakePresets.json` sets `FIXPP_WERROR` to `ON`,
    `fixpp_apply_werror_to_all_targets` (`cmake/Helpers.cmake`) applies `-Werror` to every target
    that does not set `FIXPP_WERROR_EXEMPT`, and `git grep -n FIXPP_WERROR_EXEMPT -- tests` finds
    only negative-compile probes, none in `tests/wire`. So with the attribute kept, item 5's
    temporary-view cell could not build on any clang lane.
    - **Kept:** `unknown_fields()`'s own `[[clang::lifetimebound]]`. `auto v =
      make_mv().unknown_fields();` over a temporary `MessageView` is still diagnosed (§A.4).
    - **The cost:** clang no longer diagnoses the chain through `begin()` from a temporary
      `MessageView`, as in `auto it = parser.parse(f, &mr)->unknown_fields().begin();`, which does
      dangle. `B-688-1` says so.
    - **Rejected:** keeping the attribute and wrapping item 5's cell in a local
      `#pragma clang diagnostic ignored "-Wdangling"`. That makes every consumer who writes the valid
      idiom suppress a diagnostic the header itself makes false, to keep a diagnosis that the
      attribute gives only incidentally (it fires on the view temporary, valid or not).
    - gcc ignores the attribute (`-Wno-attributes=clang::lifetimebound`, `cmake/Helpers.cmake`) and
      warns on neither shape.
  - **What stops compiling** is listed in §1.5 and `B-688-1`.
- **The predicate is the dictionary-wide `table_view::tag_defined(tag)` (R-W1-3).** The classify
  lambda of `dict_hooks::for_table_view` returns `tag_defined(t)` in place of `field_valid_for(mt, t)`.
  Framing tags 8, 9 and 10 stay exempt, and the document order does not change. **The yielded set
  changes:**
  - A tag the dictionary defines only for another MsgType is no longer yielded. With validation on it
    is `wire_unexpected_tag`, as before. With validation off it is reachable through `get(tag)` and
    field iteration, not through typed accessors and not through `unknown_fields()`. That is 2b
    v0.2's design, now as built.
  - On a FIX 5.0 table the standard header is no longer yielded (§1.1).
  - A dict-free view is unchanged. Its classify function is null, so every non-framing tag is unknown.
  - **Framing-surface tables.** `table_view::add_fixt_framing_tag` calls `mark_defined`, so on a table
    that carries 081's framing surface, `tag_defined` is true for every FIXT framing tag. Those are the
    FIX 5.0 tables, and 099's `fixt_framing` forms for FIX 4.0 to 4.4 on a route table. So on those
    tables a FIXT header tag is not yielded, even where that version's own XML does not define it.
    `B-688-1` says so.
  - **081's invariant still holds; only its stated mechanism goes stale.** The invariant is that
    classification is identical with strict validation on or off. It rests on the parser's table being
    built whatever the validation flag (081 `research.md`, the "Parser-unchanged" rationale). What goes
    stale is the text "via `field_valid_for`" (§6). T006 stays green, because `field_valid_for` itself
    does not change.
- **`classify_fn_t` keeps its signature**, `msg_type` included, and the new lambda ignores it.
  - One function changes. The hook's other users are untouched: the test seam
    (`tests/support/dict_hooks_test_access.hpp`), and the direct call in
    `reify_shared_table_concurrency_test.cpp`. That call asks about a tag the FIX 4.4 table defines, so
    its answer is still true.
  - Rejected: dropping the parameter. That changes the installed `dict_hooks.hpp` and the seam, only
    to save the view's copy of `msg_type`. So the view and its iterator still carry `msg_type`.
- **The first unknown entry is found when the view is built** (v0.6: by the first call's scan, and on a
  later call from the `MessageView`'s stored index, D-688-2), so `begin()` and `empty()` are O(1)
  after construction. `View`'s base keeps its meaning: the bytes of the first unknown field's value, or
  none. The condition is that `View`'s base is not part of `unknown_fields_view`'s contract and no
  test asserts it. A name-based recipe can only support it:
  `git grep -n -E 'unknown_fields\(\)\.(bytes|size)|\buf\.(bytes|size)' -- src include tests`
  prints nothing. Without the `\b`, `uf\.` matches every `buf.size()` in the tree. The `\b` is
  honoured: its known positive, `git grep -n -E '\bbuf\.size' -- src`, prints hits. A view bound to
  any other name escapes the recipe.
- **`unk_items_` and `unk_items_built_` are deleted from `MessageView`.** `unk_items_` is declared in
  both modes, so both `MessageView<Index>` and `MessageView<Iter>` shrink. With it goes the MSVC-debug
  hazard of constructing a `pmr::vector` over a null resource, which the member's comment explains.
- **`unknown_fields()` keeps `noexcept`, `[[nodiscard]]`, `[[clang::lifetimebound]]` and its
  `requires(Mode == access_mode::Index)`.** It no longer has a `try`, because nothing in it can throw.
  `begin()` and `end()` lose their `[[clang::lifetimebound]]` (the iterator contract above).
- **Side effect, stated as a property.** ⚠️ *Withdrawn in v0.6 (D-688-2): the first `unknown_fields()` call over a
  non-empty table stores the index when it is representable; otherwise it leaves the sentinel and rescans; the thread contract is §1.4a's.* The v0.5 text:
  `unknown_fields()` no longer writes any member, so the
  `mutable` state it shared across `const` calls is gone. Threads calling it on one const view no
  longer write shared state. This note claims nothing wider about sharing a `MessageView` across
  threads.
- **The public constructor `unknown_fields_view(std::span<kv const>, detail::generation_token)` goes.**
  It is not in the shape oracle. Its only user builds a view by hand
  (`tests/wire/unknown_fields_test.cpp`, the iterator-order cell); that cell moves to the parser path.
  `kv` stays a public nested type. With the span gone, `unknown_fields.hpp` names no `std::span`, so
  its `<span>` include goes (IWYU, R-W1-5 E2). An includer that used `std::span` through it must
  include `<span>` itself; the Linux `all` builds and the MSVC list of §2.6 item 4 show any that did.

**Cost** (`[const §VIII.3]`, a perf change, with a bench in the same PR):
- Today the first call classifies every entry once and stores the result; later calls are O(1).
- After D-688-1 a classification runs at every construction of the view (the scan to the first
  unknown entry) and at every `++` (the scan to the next one) (R-W1-5 F2).
- After D-688-1, building the view scans as far as the first unknown entry, and every full iteration
  classifies each remaining entry again. A frame with no unknown fields is scanned whole on each call.
  ⚠️ *v0.6 (D-688-2): the scan to the first unknown entry, or the whole-table scan of a frame with
  none, runs on the first call only; later calls start at the stored index. Iteration past the
  first unknown entry still classifies (§1.4a, the expected bench effect).*
- **What one classification does changes; whether it gets cheaper is measured, not claimed.**
  - `field_valid_for` hashes `msg_type` into `valid_` and then probes that MsgType's set, once for
    each entry it classifies. The validator hoists that hash (the "Hot-path hoist" comment on
    `table_view::valid_tags_for`).
  - `tag_defined` drops the `msg_type` lookup and binary-searches the sorted `defined_tags_`, every
    tag the dictionary defines.
  - A hash probe against a logarithmic search is an empirical question. The evidence file states the
    per-entry cost the bench measures, and that figure is the conclusion.
- `unknown_fields()` is not on the session's dispatch path (no caller in `src/`), so the cost falls on
  an application that asks.
- No bench covers it today (`git grep -n unknown_fields -- bench`). The PR adds `bench/wire` cases for
  the shapes where the new design costs more:
  - one pass over a frame of many known fields with unknowns at the end; a single pass will probably
    read faster, because nothing is pushed into a list;
  - repeated iteration of one view, and repeated `unknown_fields()` calls, where today's later calls
    are O(1);
  - `unknown_fields().empty()` on a frame with no unknown fields, which today is O(1) after the first
    call and becomes a whole-table scan. ⚠️ *D-688-1 history. Under D-688-2 (§1.4a) only the first
    call scans the whole table and later calls start at the stored end index, so this case measures
    the removal of the whole-table rescan after the first call.*
- **How the new cases are timed.** CI cannot time them against the base:
  - `tools/bench_compare.py` compares only the benchmark names both trees have;
  - `[const §VIII.2a]` excludes a candidate-only row from timing for that PR;
  - the base has no such case.

  **The host (R-W1-5 C2).** The cases are appended to `bench/wire/parser_bench.cpp`, after its
  existing `BENCHMARK`s; the binary is `bench/wire/parser_bench`, already `paired` in
  `bench/ci-suite.txt`, and `bench/ci-suite.txt` does not change.
  - `[const §VIII.2a]` makes a `paired` entry irreversible, so W1 adds no manifest entry. On CI the
    new rows are candidate-only: excluded from timing for this PR, and still gated on execution and
    schema.
  - They are appended after the existing cases so that the shared rows' code moves as little as
    possible.
  - They run over a table loaded from `dictionaries/FIX44.xml`, as `validator_bench` does, through
    `FIXPP_DICT_DATA_DIR`. Over `parser_bench`'s empty `table_view tv{}` every non-framing tag is
    unknown in both trees, so the no-unknown shape could not exist. So `bench/wire/CMakeLists.txt`
    gives `parser_bench` that compile definition (`fixpp_dictionary` already reaches it through
    `fixpp_wire`'s public link), and the base worktree gets the same CMake edit.

  The new cases use only `unknown_fields()`, `begin()`, `end()`, `empty()` and a range-`for` that
  binds `auto const&`, and all of these compile on both trees. So the PR's bench source is copied into
  a base worktree and both trees run A-B-A-B (min per tree), locally. The evidence file records the
  command and the numbers. The rows both trees share stay CI's paired comparison, as usual.

  **The layout control.** Code layout alone can move a case past the band on identical source. 092's
  evidence file (parent `decisions/speckit/092-garbled-frame-reject-evidence.md`, its "Invalid first
  run, kept as evidence" block) read the scan cases past `[const §VIII.2]`'s band with the base
  worktree at a path of another length, because a source-root path embedded in the binary moved every
  later function. `bench/wire/CMakeLists.txt` passes the source root's `dictionaries` path as
  `FIXPP_DICT_DATA_DIR`, so a bench binary that uses it embeds the root; `strings` on the built
  binary shows whether a given one does. So:
  - the base worktree is temporary, on `/mnt/wsl/fixppbuild`, at a path of the candidate root's
    length, and it is removed after the run;
  - an A-A round (the base source in both trees) runs first and its delta is recorded; the A-B rows
    are judged against that spread, not against zero;
  - the A-A pair's `.text` sections are compared (`objcopy --only-section=.text` on each binary,
    then `cmp`), and the result is recorded, as 092 did.
- **What a slowdown needs (R-W1-4 as amended twice).** Nothing is accepted in advance.
  - **The shapes expected to regress:** repeated `unknown_fields()` calls and repeated iteration of
    one view, and `unknown_fields().empty()` on a frame with no unknown fields. The single-pass arm is
    reported beside them, so the report shows both sides. ⚠️ *D-688-1's list. Under D-688-2 what each
    case is expected to do is §1.4a's "Expected bench effect"; every row is still measured and
    judged by the rule below.*
  - **How they are measured:** the transplanted A-B-A-B run above, min per tree, with its command and
    figures in the evidence file.
  - **The rule.** Each row past `[const §VIII.2]`'s band is decided after the measurement, with its
    figures. `[const §VIII.2]`'s "An accepted regression is never author-declared" paragraph requires
    approval by someone other than the change's author, recorded in the PR thread. So each over-band
    row is posted in the PR thread with its figures and the orchestrator's recommendation, and the
    owner's ratification there is a **merge condition**, the form 106's Gate B gave R20
    (`decisions/speckit/106-logon-guards-write-bounds-gateb.md` in the parent, "the R20 owner flag").
    The delegation's "merge if green" does not cover an over-band row. If no row is over the band, no
    approval is needed.
  - **If the owner declines a row,** the PR does not merge. The row's design is revisited: #688
    returns to Gate A with Option A (`read_failed()` over the materialised list, §1.3) as the named
    alternative, and D-707 and D-559 may then ship split out.
  - **An extra hard rule:** a regression past the band on the per-message dispatch path, which is
    CI's paired rows, blocks the merge and goes to the owner.
  - **How CI's paired rows are read (R-W1-5 B1).** `parser_bench` and `writer_bench` are `paired` in
    `bench/ci-suite.txt`. Their shared rows are read from the bench job's output against
    `[const §VIII.2]`'s +5%; a green sentinel is not compliance. Any shared row above +5% follows the
    PR-thread route above: its figures are posted with the orchestrator's recommendation, and the
    owner's ratification there is a merge condition. CI's paired comparison has no A-A control, so a
    shared row can pass the band on layout alone; such a row is still posted, with the local A-A
    spread beside it.

### 1.4a D-688-2 — the first-unknown index (v0.6, R-W1-6)

**The ruling.** R-W1-6 (owner, option A): the `MessageView` remembers where the first unknown entry
is, computed on the first `unknown_fields()` call and stored without allocation, so later calls start
there instead of rescanning the frame. It is a design choice, not a ratification of any figure
(R-W1-4 still applies; "Expected bench effect" below). Every other clause of D-688-1 stands; the
three §1.4 statements this one changes are flagged there.

**The shape.**
- `MessageView<Index>` gains one private member, `first_unknown_`: an index into `table_.entries()`.
  - Index mode only, as `resolved_application_version_` and `dict_owner_` are:
    `using first_unknown_t = std::conditional_t<Mode == access_mode::Index, std::uint32_t, no_first_unknown_t>;`
    and `[[no_unique_address]] mutable first_unknown_t first_unknown_{not_computed()};`. It is
    `mutable` because `unknown_fields()` is `const`.
  - **`no_first_unknown_t` is a new empty type** (`struct no_first_unknown_t {};`), distinct from
    `no_resolved_version_t`, `no_dict_owner_t` and `empty_t`. Two `[[no_unique_address]]` members of
    the same empty type cannot share an address, so reusing one of those types would give
    `MessageView<Iter>` a byte of its own on clang and gcc (§A.7 compiles both forms).
  - **The initializer is mode-dependent**, in the pattern `hooks_{no_hooks()}` already uses
    (`static constexpr hooks_holder_t no_hooks()`, `include/fixpp/wire/parser.hpp`): a
    `static constexpr first_unknown_t not_computed() noexcept` returns
    `detail::kFirstUnknownNotComputed` for Index and `no_first_unknown_t{}` for Iter.
    - Not `{std::numeric_limits<std::uint32_t>::max()}`: every constructor uses the default member
      initializer, so it is instantiated for `MessageView<Iter>` too, and initialising an empty
      struct from a `std::uint32_t` is ill-formed (§A.7).
    - Not `{}`, the spelling `resolved_application_version_{}` uses: it gives Index the value 0, which
      reads as "computed, first unknown entry at index 0", so the first call would yield the framing
      tag 8. Cell (a)'s first-call count and the existing cells would catch that loudly; the design
      names the form so that no one reaches for it.
  - Declared immediately after `resolved_application_version_` (the layout, below).
- **The encoding: one sentinel, checked in code on both sides.**
  - **"Not computed yet"** is `detail::kFirstUnknownNotComputed`,
    `std::numeric_limits<std::uint32_t>::max()`, the Index value of `not_computed()`.
  - A stored value `v` that is not the sentinel and has `v <= entries().size()` is computed:
    `v < size()` is the first unknown entry's index; **"none"** (the frame has no unknown field) is
    `v == size()`, the end index. That is what `unknown_fields_view::next_unknown` returns when it
    finds nothing (`first == s.end`), so "none" needs no second magic value.
  - **Read:** `detail::first_unknown_is_set(std::uint32_t v, std::size_t size)` is
    `v != kFirstUnknownNotComputed && v <= size`.
    - The `!=` half keeps the sentinel from reading as "computed at index `UINT32_MAX`" on a table of
      `UINT32_MAX` entries or more. That would start the view past every earlier entry and silently
      drop their unknown fields.
    - The `<= size` half makes the moved-from source rescan (below).
  - **Store:** `detail::first_unknown_fits(std::size_t idx)` is `idx < kFirstUnknownNotComputed`. A
    found index that does not fit is not stored. The sentinel stays, and that view rescans on every
    call, which is D-688-1's behaviour, so what it yields is still right.
  - **Where they live:** both are `constexpr` free functions in `fixpp::wire::detail` in
    `include/fixpp/wire/parser.hpp`, beside `kFirstUnknownNotComputed`, with `static_assert`
    witnesses next to them:
    - `!first_unknown_is_set(kFirstUnknownNotComputed, std::size_t{kFirstUnknownNotComputed} + 1U)`:
      the sentinel is not "computed" on a table one entry larger than it. Where `std::size_t` is
      64-bit, it fails the build if the `!=` half is dropped. Where it is 32-bit, the sum wraps to 0
      and the assertion holds either way (reasoned from unsigned arithmetic; no 32-bit toolchain was
      run);
    - `!first_unknown_fits(kFirstUnknownNotComputed)` and
      `first_unknown_fits(kFirstUnknownNotComputed - 1U)`;
    - `first_unknown_is_set(3, 3)` and `!first_unknown_is_set(4, 3)`.
  - **What the witnesses kill, and what has no cell.**
    - CI cannot build a table of 2^32 entries: a direct `Parser` would need `max_offset_entries`
      raised far past its default and a frame of several GiB. So the runtime path past 2^32 entries
      has no executable cell.
    - The `static_assert`s kill a mutation of either predicate at compile time (§A.7 runs both
      mutants). They do not see the call sites. A store that skips `first_unknown_fits` behaves
      identically below 2^32 entries, so no cell can fail on it; a read that tests only the `!=`
      half at the call site is cell (b)'s M4.
    - The call sites are re-derived with
      `git grep -n -E 'first_unknown_(is_set|fits)\(' -- include/fixpp/wire/parser.hpp`, which must
      print the two definitions, their witnesses, and one call of each in `unknown_fields()`.
- **`unknown_fields()`, in order** (the order matters: step 2 comes before step 3, so a moved-from
  source reads its stale index through the guard, and test (b)'s mutants can show):
  1. `check_alive()`, as now, before anything is read.
  2. `ents = table_.entries()`. If `detail::first_unknown_is_set(first_unknown_, ents.size())`, build
     the view through `unknown_fields_view`'s private constructor
     (`unknown_fields_view(scan_state, entry const* first, generation_token)`; `MessageView` is its
     friend) at `ents.data() + first_unknown_`. No classify call is made. Re-entering `scan` at that
     index would classify the cached entry once more, and test (a) below pins that it does not.
  3. Otherwise, if `ents` is empty, return the view of an empty range and write nothing (the shared
     empty view, under the thread contract below). The scan there is O(1) anyway. A default-constructed
     view holds the sentinel, so it reaches this step and is only ever read.
  4. Otherwise run `scan` from `ents.data()` as today. Store the found entry's index `idx` if
     `detail::first_unknown_fits(idx)`, and otherwise leave the sentinel. Return that view.
- **Representation: an index, not an `entry const*`. The ruling's premise does not hold in source.**
  - R-W1-6 says "a copied MessageView copies its OffsetTable". Read at `10e73842`: `MessageView`'s
    copy constructor and copy assignment are `= delete`, and so is its move assignment; only its
    move constructor is `= default` (`include/fixpp/wire/parser.hpp`, the special-member block whose
    comment begins "Move-only (move-CONSTRUCTION only"). `OffsetTable`'s copy is deleted as well, and
    its move is defaulted (`include/fixpp/wire/offset_table.hpp`, "389: COPY IS DELETED"). So no
    copied view exists.
  - **The copy sites re-parse.** `dict::reify`'s factory and `fixpp_msg_clone` build a new
    `MessageView` over their own bytes with `parser.parse(…, view.offsets().config())`
    (`src/dictionary/reify.cpp`, `src/capi/message_write.cpp`), so a copy starts "not computed".
  - **Move construction is the one way a set index travels.** The defaulted move copies the member.
    `std::pmr::vector`'s move constructor copies the allocator and takes the source's buffer, so in
    the destination the index still names the same entry. So would a pointer.
  - **The reasons the index is chosen:**
    - it can be checked against `entries().size()`, which is what makes the moved-from source
      safe. A pointer carried into the moved-from source points into the buffer the destination now
      owns, and nothing in the source can check it;
    - 4 bytes fit existing padding, and 8 do not (the layout, below);
    - it is a function of the entries alone, so it stays right if their storage ever moves.
- **The width: `std::uint32_t`.**
  - `std::uint16_t` cannot hold every index. The session's entry cap is
    `parse_capacity::entry_cap_for(limit)`, `limit / 3 + 1` (`src/session/parse_capacity.hpp`),
    and `default_max_frame_bytes` is 256 KiB (`include/fixpp/wire/framer.hpp`), so the default
    session cap is past 65535. A direct `Parser` user sets `OffsetTable::Config::max_offset_entries`,
    a `std::size_t` with no clamp: the only bound is `OffsetTable::build`'s
    `entries_.size() >= cfg_.max_offset_entries` check.
  - `std::uint32_t` is the width the table already uses for an entry index: `overlay_` is a
    `std::pmr::vector<std::uint32_t>` whose slots hold `static_cast<std::uint32_t>(e) + 1U`
    (`OffsetTable::build`), and `entry::offset` is a `std::uint32_t`.
  - **Why the store is bounded.** Nothing in source keeps `entries().size()` below 2^32:
    `max_offset_entries` has no clamp, and `OffsetTable::build` has no frame-length bound. So the
    encoding checks representability itself, on the read side and on the store side (above), and
    relies on no table condition.
  - **Rejected: a `std::uint32_t` index plus a `bool` "computed" flag.** It still truncates an index
    of 2^32 or more on store, so it still needs the store bound, and it still needs `<= size` for the
    moved-from source. It removes neither check. It adds a member and a second declaration-order
    constraint: the flag must also sit in the gap before `dict_owner_`, or the size grows.
  - **Rejected: a cardinality invariant in `OffsetTable::build`.** It changes the table's rejection
    surface (a new error disposition and a B&L row), it is just as unwitnessable at that size, and it
    is outside D-688-2.
  - **Disclosed for filing:** `OffsetTable::build` already narrows the value offsets and lengths of a
    frame past 4 GiB (§6). D-688-2 does not depend on it.
- **The moved-from source.** After the defaulted move both objects hold the index. The source's
  entries are valid but unspecified; libstdc++, libc++ and the MSVC STL leave a move-constructed-from
  vector empty. Through the `<= size` half of step 2's guard, an index above the source's size reads as "not computed",
  so the source rescans what it holds (an empty table: step 3, no write). The guard is exact while a moved-from vector holds either the
  source's elements or none, and nothing can change the source's entries after the move (the
  invalidation, below).
  - Rejected: a member type whose move constructor resets the source. It works, but it adds a type
    for a state the one comparison already covers.

**Invalidation: the index stays valid for the view's whole life.** It memoises
`next_unknown(entries().data(), …) - entries().data()`, a function of the entries, the classify hook
and its dictionary pointer (both in `hooks_`), `msg_type()` (read from the entries and the frame)
and the dictionary's answers. Each is fixed after construction:
- **`entries()`.** `entries_` is written only in `OffsetTable::build`, which is private and called
  only from `OffsetTable`'s constructors (`git grep -n -E '\bbuild\(' -- src/wire/offset_table.cpp`).
  `entries_` is not `mutable`, and `src/wire` and `include/fixpp/wire` have no `const_cast`
  (`git grep -n const_cast -- src/wire include/fixpp/wire` prints nothing). `group_index_link` is
  written only by `build`'s `emplace_back` (`git grep -n group_index_link -- src include`).
  - The lazy state lives in other members and does not touch `entries_`. The group context is written
    by `set_group_context` from every `group<>()`. `group_index_` gains its rows on a slice read.
    `nested_cache_` holds sub-tables that `build_nested_subview` places in the arena, each an
    `OffsetTable` with its own entries.
  - `table_` is never replaced: `MessageView`'s move assignment is deleted, and `offsets()` returns
    `OffsetTable const&`. There is no in-place re-parse.
- **`hooks_`.** Set only in `MessageView`'s constructor initializers
  (`grep -n 'hooks_ =' include/fixpp/wire/parser.hpp` prints nothing). `Parser::parse` writes
  `dict_owner_` after construction, and `message_view_resolved_access` writes the resolved
  version. Neither touches `hooks_`.
- **The dictionary.** `table_view`'s population surface (`mark_defined`, `add_valid_tag`,
  `add_fixt_framing_tag`) is private since fixpp#456 and reached only through
  `table_view_builder`, so a published table's `tag_defined` answers are fixed. D-688-1's condition,
  that the dictionary outlives every iteration, already covers the index.
- **So nothing resets the index.** There is no copy and no assignment to reset it for, and the move
  is covered by the guard. A test-seam bundle (`dict_hooks_test_access::make`) whose classify answer
  changes between calls would see the index from its first answer. Production bundles come only from
  `dict_hooks::for_table_view` and `none()`.

**The thread contract.**
- **It is documented, and the index sits inside it.** `.specify/2b-wire.md` §6.6, "Threading":
  "`Parser`, `Writer`, `OffsetTable`, `MessageView`, `Validator` are value types … Safe on a
  single thread; not synchronized for concurrent access." Two other places carry it:
  - the live B&L's `B-003-4` (an `owning_<Msg>` is single-strand). The reason it gives, the lazy
    `view()` cache, is no longer the mechanism (`owning_message_handle::impl::view_cache_` is filled
    eagerly since fixpp#458 D-4), but the view's own caches keep the contract true;
  - `include/fixpp/wire/view.hpp`'s generation registry, "Single-thread-per-session".
- **What the existing caches already impose.** `group<>()` calls `set_group_context` on every call,
  which writes the `mutable` `group_ctx_msg_type_`, `group_ctx_parent_path_` and
  `group_ctx_depth_`. A slice read can append to `group_index_`, and `nested_group_slices` to
  `nested_cache_`. So two threads reading one view through `group<>()` already race. The index is
  state of the same kind, under the same contract, and adds no hazard the contract permits.
- **Against v0.5 and against the base.** D-688-1's "Side effect, stated as a property" (that
  `unknown_fields()` writes no member) is withdrawn: the first call over a non-empty table stores the index when it is representable; otherwise it leaves the sentinel and rescans. Against the base (`b93b8a28`), which wrote `unk_items_` and
  `unk_items_built_` on the first call, nothing changes.
- **One place where the library itself shares a view across threads: `kEmpty`.**
  `owning_message_handle::view()` (`src/dictionary/reify.cpp`) returns one function-local
  `static MessageView<Index> const kEmpty` for every null (for example moved-from) handle, on every
  thread. Two threads, each holding its own empty handle, reach one `MessageView`. Step 3's rule
  (nothing is written over an empty table) keeps `unknown_fields()` from writing it, because
  `kEmpty`'s table is default-constructed and empty. Test (e) pins the rule.
  - **Disclosed and filed, not fixed here:** `group<>()` on `kEmpty` already writes
    `set_group_context`'s members (read). That is fixpp#736 (batch B96), "owning_message_handle::view()'s
    shared static empty MessageView is written by const group<>() on every thread". Whether a
    declined slice read on its empty table also memoises a row in `group_index_` was not traced
    here. It predates W1 and is outside it.
- **The C ABI does not reach the index.** No C symbol calls `unknown_fields()`:
  `git grep -n unknown_fields -- src` prints only comments (no call), and
  `git grep -n unknown_fields -- bindings tools` prints nothing. So `B-051-2`'s THREAD_SAFE clone
  reads are untouched.
- **No test reads one `MessageView` from two threads.**
  - `tests/dictionary/reify_shared_table_concurrency_test.cpp` reads two reify handles, `ha` and
    `hb`, each over its own `MessageView`, one handle per thread. With the index, each thread's
    first call writes its own view's index, so the TSan cell stays clean.
  - Only `unknown_fields()` writes the index, so the instrument is the threaded tests that call it:
    `git grep -l -E 'std::(j)?thread' -- tests | xargs grep -l unknown_fields`. Its known positive,
    `reify_shared_table_concurrency_test.cpp`, is the only file it printed at `10e73842`. So no test
    calls `unknown_fields()` on one view from two threads. This note claims nothing about the other
    threaded tests' use of a view.
- **Rejected: `std::atomic<std::uint32_t>`.** It would make this one accessor safe while
  `group<>()` stays unsynchronised, so it buys no contract. And `std::atomic` is neither copyable nor
  movable, so `MessageView`'s defaulted move constructor would become deleted.
- **B&L:** no new row. `B-688-1` gains one sentence citing `[2b §6.6]` (§4).

**The layout. The design requires that `sizeof` and `alignof` of both `MessageView<access_mode::Index>`
and `MessageView<access_mode::Iter>` equal those of the tree before D-688-2, on each toolchain, in
release and in debug.** The re-derive recipe below checks it, before and after. No result is claimed
here ahead of the after-run.
- **Where the room is.** In §A.6's before-run on clang and gcc, `resolved_application_version_` (an
  `application_version`, `std::uint8_t`-based) is followed by `dict_owner_`, a pointer, with 7
  bytes of padding between them, in release and in debug. A `std::uint32_t` declared right after
  `resolved_application_version_` lands in that padding, 4-aligned. Declared anywhere else, for
  example last, it adds 8 bytes. So the declaration order is part of the design.
- **`MessageView<Iter>` on clang and gcc.** The new member is a `[[no_unique_address]]` member of a
  new, distinct empty type (the shape, above), so it needs no byte of its own.
- **MSVC (reasoned, not measured).** x64 MSVC uses the same natural-alignment rule. `hooks_` ends on
  an 8-byte boundary and the 1-byte member is followed by an 8-aligned pointer, so the same gap exists
  in debug and release, whatever the STL's container sizes are.
  - MSVC ignores `[[no_unique_address]]`, and `include/` has no `msvc::no_unique_address`
    (`git grep -n 'msvc::no_unique_address' -- include` prints nothing). So in `MessageView<Iter>`
    the new empty member is one more byte beside the two empty members already at its tail, within the
    tail padding.
  - Measured at the `-k 0` MSVC build of the touched targets: one TU compiled with
    `/d1reportSingleClassLayoutMessageView`, against the same TU before D-688-2 (at `10e73842`). The
    run must read the layout of both specialisations, Index and Iter. If the switch's report does not
    show both, each gets its own TU.
- **The re-derive recipe, run before and after on each toolchain, release (`-DNDEBUG`) and debug.**
  The TU includes `fixpp/wire/parser.hpp` and names `sizeof` and `alignof` of both specialisations
  (§A.6 gives it).
  - clang: `clang++ -std=c++23 -fsyntax-only [-DNDEBUG] -I include -Xclang -fdump-record-layouts-complete <the TU>`.
    Read both `MessageView` records: in Index, `first_unknown_`'s offset lies between
    `resolved_application_version_`'s and `dict_owner_`'s; in both, `sizeof` and `align` equal the
    before-run's.
  - gcc: `template <unsigned long, unsigned long, unsigned long, unsigned long> struct S; S<sizeof(MessageView<access_mode::Index>), alignof(MessageView<access_mode::Index>), sizeof(MessageView<access_mode::Iter>), alignof(MessageView<access_mode::Iter>)> s;`.
    The incomplete-type error prints the four values.
  - MSVC: the bullet above.
  - No `std::optional<MessageView<Index>>` probe: for one standard library, `optional<T>`'s layout is
    a function of `sizeof(T)` and `alignof(T)`, which the recipe reads.
- **Every pin that could fire.** Each recipe runs over `tests include src bindings bench tools perf`
  unless it names its own paths, with `':!.specify/688-707-559-wire-headroom-token.md'` so that no
  recipe matches this note. **The rule:** every hit the disposition below names appears in its
  recipe's printed output, and every file a recipe prints is dispositioned below. A name no recipe
  prints is a recipe defect, fixed before the list is trusted.
  - (P1) `git grep -n -E 'sizeof ?\([^)]*\b(MessageView|unknown_fields_view|OffsetTable)\b' -- …`.
    Known positive: the `sizeof(OffsetTable::entry)` pin in `tests/wire/offset_table_test.cpp`.
  - (P2) multiline, because a `static_assert(` can stand alone on its line and `git grep` has no
    multiline mode:
    `git ls-files -z -- tests include src bindings bench tools perf | xargs -0 perl -0777 -ne 'while (/static_assert\s*\([^;]{0,500}?\b(MessageView|unknown_fields_view)\b/g) { my $l = 1 + (substr($_, 0, $-[0]) =~ tr/\n//); print "$ARGV:$l\n"; }'`.
    It prints `file:line` for each `static_assert` that names either type before its first `;`.
    Known positive: the `unknown_fields_view` pin in `tests/wire/type_shape_pins_test.cpp` whose
    `static_assert(` stands alone on its line, which a single-line `git grep` misses.
  - (P3) `git grep -n -E '(offsetof|alignof) ?\([^)]*(MessageView|OffsetTable|unknown_fields_view)' -- …`.
    Known positive: the `alignof(OffsetTable::entry)` pin in `tests/wire/offset_table_test.cpp`.
  - (P4) `git grep -n kParseContainers -- . ':!.specify/688-707-559-wire-headroom-token.md'`, §1.5's
    command. Known positive: the constant's definition in
    `src/session/parse_capacity.hpp`.
  - (P5) holders by value, in three recipes:
    - (P5a) `git grep -n -E '(optional|expected_t|unique_ptr)<\s*(::)?(std::)?(::)?(fixpp::)?(wire::)?MessageView' -- src include tests tools`.
      Known positive for the leading-`::` allowance: the generated holder's golden in
      `tests/codegen/accessor_resolution_census_test.cpp`
      (`::std::optional<::fixpp::wire::MessageView<…>>`), which the form without `::` misses.
    - (P5b) direct members: `git grep -n -E 'MessageView<[^;]*>\s+[a-z_]+_\s*[;{=]' -- src include tests tools`.
      Known positives: `src/dictionary/reify.cpp`'s `view_cache_`, the same golden's `view_cache_`,
      and `tests/support/copy_site_fixtures.hpp`'s `mv_`.
    - (P5c) the emitter writes its holder's type from pieces (`w.raw(kMessageView)`), so no
      `MessageView` regex reaches it: `git grep -n -F view_cache_ -- tools`. Known positive:
      `tools/codegen/fixpp-codegen/emit_reify.cpp`.

  **What each kind of hit does under D-688-2**, as read at `10e73842`:
  - **The codegen flyweight pin** (`tests/codegen/accessor_resolution_census_test.cpp`,
    `sizeof(NewOrderSingle) == sizeof(MessageView<…> const*)`) is a pointer: unchanged.
  - **`tests/wire/parser_index_test.cpp`**, `is_move_constructible_v` and
    `!is_move_assignable_v`: unchanged. A `std::uint32_t` member keeps the defaulted move
    constructor, and the design relies on both pins.
  - `include/fixpp/wire/message_view_contract.hpp` (`is_base_of_v`),
    `tests/wire/noexcept_trap_test.cpp` (`get_decimal`'s `noexcept`), and
    `tests/wire/type_shape_pins_test.cpp` (`unknown_fields_view`'s constructibility pins):
    unchanged. D-688-2 adds no member and no constructor to `unknown_fields_view`.
  - **Every `sizeof(OffsetTable)` band** (`tests/capi/message_read_failloud_test.cpp`,
    `tests/wire/group_view_failloud_test.cpp`, `tests/wire/nested_group_slices_failloud_test.cpp`,
    `tests/support/group_read_100.hpp`, `tests/wire/test_100_group_read_status.cpp`,
    `build_nested_subview`'s allocation in `src/wire/offset_table.cpp`), and the comments that
    describe them (`include/fixpp/wire/offset_table.hpp`, `tests/wire/nested_group_slices_cache_test.cpp`):
    unchanged.
    - The index lives in `MessageView`, not in `OffsetTable`. That is a requirement: in
      `OffsetTable` it would move every band calibrated to its size, and each nested sub-table would
      carry an index nothing reads.
  - **The `OffsetTable` nested types' sizes and alignments** (`entry`, `Config`, `group_span`,
    `nested_cache_row`), wherever P1 and P3 print them (`tests/`, `tests/support/wire_test_hooks.hpp`,
    `bench/wire/offset_table_footprint_bench.cpp`, `src/session/parse_capacity.hpp`'s formula):
    unchanged. D-688-2 changes no `OffsetTable` type.
  - **`kParseContainers`** (`src/session/parse_capacity.hpp` and the test's own copy in
    `tests/session/inbound_frame_dispositions_test.cpp`): unchanged. No container is added, and the
    member is not in the parse resource. The records P4 prints in `brain/`, `spec/` and
    `specs/093-inbound-frame-dispositions/` cite the constant and change with it only.
  - **The holders** (P5). None has a size pin, and each one's size follows
    `sizeof(MessageView<Index>)`, which the design keeps (the layout, above):
    - `Parser::parse`'s and `parse_iter`'s `core::expected_t<MessageView<…>>` results
      (`include/fixpp/wire/parser.hpp`);
    - `owning_message_handle::impl`'s `std::optional<MessageView>` `view_cache_`
      (`src/dictionary/reify.cpp`, in the caller's `mr`);
    - the C ABI's heap-held view, `fixpp_msg::owned_view_`, a `std::unique_ptr`
      (`src/capi/capi_internal.hpp`), which `fixpp_msg_clone` fills from its local `clone_view`
      (`src/capi/message_write.cpp`);
    - **the generated `owning_<Msg>` holder**: its `mutable std::optional<MV> view_cache_`, emitted by
      `tools/codegen/fixpp-codegen/emit_reify.cpp`, with its golden in
      `tests/codegen/accessor_resolution_census_test.cpp`. It is per-object (no shared static), and
      `view()` fills it lazily. Its move constructor and move assignment reset both the source's and
      the destination's cache to `nullopt`, so a stored index never travels through it;
    - the tests' holders (`tests/support/copy_site_fixtures.hpp`, `tests/support/group_read_100.hpp`,
      `tests/session/test_067_builder_roundtrip.cpp`, `tests/wire/unknown_fields_test.cpp`).
    - No C-ABI struct holds a `MessageView` by value: `<fix/c_api.h>` exposes no wire type (`[2b §5]`).
  - `bindings/` and `perf/`: no hit for any recipe. `tools/`: only P5c's emitter hit (the generated
    holder, above).

**Behaviour: unchanged for every frame.**
- What `unknown_fields()` yields is unchanged. The stored index is the value `scan` finds, a pure
  function of inputs that are fixed for the view's life (the invalidation, above), and the view built
  at it equals the one `scan` builds.
- The every-field and allocation-free properties, and Q-32 (an exhausted arena still yields every
  unknown field), are unchanged. The index is a member of the `MessageView` object, never in the
  parse resource; the view stores no list.
- **The generation check is unchanged.** `unknown_fields()` still calls `check_alive()` first,
  before it reads the entries or the index, and the iterator's checks are as in §1.4.

**Expected bench effect, stated as an expectation and measured, not claimed.** The case names are
`bench/wire/parser_bench.cpp`'s `BM_UnknownFields_*`.
- **`EmptyOnFrameWithNone`:** after the first call, each call is the guard's comparison and the
  view's construction. The whole-table scan goes.
- **`RepeatedCalls`:** the frame is the 20-tag body with three trailing undefined tags. The rescan of
  the known prefix goes. Each call still iterates: the entries after the first unknown one are
  classified, and the checksum is skipped by the framing test. So it falls, and stays above the
  base's figure, because the base iterated a stored list.
- **`RepeatedIterationOfOneView`:** no change expected. The view is built once, outside the timed
  loop, and each `++` classifies the next entry.
- **`SinglePassAfterParse`:** one store more on the first call.
- **The parse path** (`BM_Parser_Index_20tag`, `BM_Parser_Index_200tag`): each `MessageView`
  construction initialises one more member, the move into `core::expected_t` copies it, and code
  layout moves, so these rows are re-measured as well.
- **The procedure** is §1.4's, in full, after D-688-2 is implemented: the A-B-A-B run with the base
  worktree at a path padded to the candidate root's length, the A-A control with its `.text`
  comparison, and every row, the shared parse rows included. The run replaces the earlier figures for
  every row. Each row past `[const §VIII.2]`'s band goes to the PR thread with its figures, and the
  owner's ratification there is a merge condition (R-W1-4, R-W1-6).

**Tests (RED first; each names the mutant it kills).** Cells (a) to (d) are in
`tests/wire/unknown_fields_test.cpp` (the grouped wire bucket). They build the view with the
three-argument `MessageView(frame_view const&, memory_resource*, dict_hooks)` constructor over a
bundle from `tests/support/dict_hooks_test_access.hpp`'s `make`:
- the classify stub counts its calls through the `opaque_dict` it is given, a test-local object
  with a `mutable` counter;
- the group hooks are null, which the frames below do not need.

`tests/wire/unknown_fields_dict_test.cpp` loads a real XML dictionary through `Parser{tv}`, and
`Parser` takes only a `table_view`, so a counting hook cannot go through it.
- **(a) The second call makes no classify call before the first unknown entry.**
  - The frame has at least two defined non-framing tags before its first undefined one, then two
    undefined ones: for example `35=D`, `34=1`, `55=X`, `5000=a`, `5001=b`. The stub answers
    "defined" for tags below 5000.
  - The expected count of the first call is derived in the cell from `offsets().entries()`: the
    non-framing entries up to and including the first undefined one. It is not hard-coded.
  - Then the counter is reset, `unknown_fields()` is called again together with `empty()` and
    `begin()`, and the counter must be **exactly 0**.
  - A full iteration then makes exactly as many calls as there are non-framing entries after the
    first unknown one.
  - RED on the branch before D-688-2: the second call reclassifies the prefix.
  - Mutants:
    - M1, the index never stored: RED;
    - M2, stored but never read (always rescan): RED;
    - M3, re-entering `scan` at the index: 1 call, so RED, because the assertion is exact.
- **(b) Moves; there are no copies.**
  - **Pin the premise:**
    `static_assert(!std::is_copy_constructible_v<MessageView<access_mode::Index>> && !std::is_copy_assignable_v<MessageView<access_mode::Index>>)`,
    in `tests/wire/parser_index_test.cpp` beside the move pins. No such pin exists today
    (`git grep -n -E 'is_copy_(constructible|assignable)_v<\s*(fixpp::wire::)?MessageView' -- tests`
    prints nothing). The index design relies on the deletion. Its RED is a user-provided copy
    constructor on `MessageView`.
  - **Move with the index set, and with it unset.** The source is held as
    `std::optional<core::expected_t<MessageView<Index>>>`. In one arm `unknown_fields()` is called
    first, so the index is set; in the other it is not.
    - `MessageView<Index> b{std::move(**src)}`.
    - `b` yields the source's (tag, value) list from before the move, also after the source is
      destroyed with `src.reset()`. `b` owns the buffer after the move.
  - **The moved-from source**, `**src` after the move, read while `b` is alive and before
    `src.reset()`:
    - **Precondition, in the parent:** `ASSERT_TRUE((**src).offsets().entries().empty())`. M4 shows
      only over an empty moved-from table, so on a standard library that kept the elements the arm
      reports instead of silently not testing M4.
    - **The oracle is the child's exit code, and nothing is iterated:**
      `EXPECT_EXIT({ auto const uf = (**src).unknown_fields(); std::_Exit(uf.empty() && uf.begin() == uf.end() ? 0 : 1); }, ::testing::ExitedWithCode(0), "")`,
      with no `EXPECT` inside. A death-test child's assertions do not reach the parent; only its exit
      status does, and a statement that returns without exiting fails as "did not die". So a count
      checked inside the child would fail toward clean. The death test also keeps a mutant's crash
      inside this cell instead of ending the bucket.
    - **M4, step 2 tests only `first_unknown_ != kFirstUnknownNotComputed`** (the `<= size` half
      dropped at the call site; dropped inside `first_unknown_is_set`, it already fails the build on
      `!first_unknown_is_set(4, 3)`). In the index-set arm the source builds its view at
      `data() + k` over its empty table, whose `data()` is null, with `k > 0` because entry 0 is the
      framing tag 8, and the constructor reads that entry. That is undefined behaviour, so how it
      ends depends on the lane:
      - on the clang UBSan lanes (`linux-clang-ubsan`, `linux-clang-libc++-ubsan`),
        `-fsanitize=undefined` (`cmake/Sanitizers.cmake`) includes clang's pointer-overflow check,
        which flags a non-zero offset applied to a null pointer, and both test presets'
        `UBSAN_OPTIONS=halt_on_error=1` (`CMakePresets.json`) end the child. RED by the
        halted diagnostic. That claim is clang's: gcc's UBSan is not claimed to flag it;
      - elsewhere, RED by the SIGSEGV observed or by exit 1, neither guaranteed.
      - In the index-unset arm the source holds the sentinel, so M4 cannot show there.
    - **M5, an `entry const*` in place of the index:** the source's pointer names `b`'s first unknown
      entry, which is live while `b` is, and its end is its own empty table's. Construction and
      `empty()` read valid memory and find an entry, so the child exits 1: RED deterministically, with
      no undefined behaviour (the comparison is pointer equality, and nothing is iterated). M5 also
      shows in the layout after-run: a pointer does not fit the 7-byte gap, so the size grows by 8.
    - Both mutants show only because step 2 precedes step 3. With the empty-table test first, the
      source would never read its index, and this arm could not fail.
  - **The copy sites are re-parses.** `dict::reify` of a source whose index is set; the source
    frame and arena are then destroyed, and the handle's `view().unknown_fields()` yields the same set
    (`tests/dictionary/`, next to the reify cells). It is a regression cell, not RED-first: reify
    re-parses by construction (`src/dictionary/reify.cpp`), so no D-688-2 mutant can turn it RED. It
    fails only if a later change ever reused the source's storage (ASan). The C-ABI clone is not
    given a cell, because no C symbol reads unknown fields.
- **(c) A frame with no unknown field.** The stub answers "defined" for every tag.
  - The first call classifies every non-framing entry.
  - Every later call: `empty()` is true, and the counter stays at **0**.
  - M6, only an index below the size stored and "none" left as "not computed": RED.
- **(d) The allocation cells stay green, with a second call inside their window.**
  - `WireUnknownFields.ReadingTheUnknownFieldsDrawsNothingFromTheParseResource` already makes two
    `unknown_fields()` calls, each iterated, inside its counted window (its two-pass loop). Once
    D-688-2 is built, its second pass runs the cached path under the counter, with no edit.
  - `WireUnknownFieldsQ32.ExhaustedArenaStillYieldsEveryUnknownFieldWithNoUpstreamRequest` makes one
    call. It gains a second `unknown_fields()` call and iteration inside its counted window, with the
    same count and the same zero upstream requests asserted.
  - M7, the index kept in a container on the parse resource: RED by the parse-resource counter.
- **(e) The shared empty view is never written.** In its own standalone binary,
  `tests/dictionary/reify_shared_empty_view_concurrency_test.cpp`, one cell, added to
  `FIXPP_REIFY_STANDALONE` in `tests/dictionary/CMakeLists.txt` and labelled as
  `reify_shared_table_concurrency_test` is (a TSan target):
  - two null handles (each moved from);
  - **precondition:** `&ha.view() == &hb.view()` (the same `kEmpty`), so the instrument can see a
    race. It reads only the addresses, never `unknown_fields()`;
  - two threads each loop on `h.view().unknown_fields().empty()`.
  - **Why its own process.** M8 writes once: after the first call over an empty table, `0 <= 0` reads
    as computed, and no later call writes. So the process's first `unknown_fields()` call on
    `kEmpty` must be made inside the two threads. If any code in the process made it first on the
    main thread, that write would happen-before both threads, they would only read, and TSan would
    stay clean with M8 live. Each binary in `FIXPP_REIFY_STANDALONE` is one `add_test` process for
    all its cells, so a binary with this one cell keeps the condition by construction. Nothing else
    may be added to it.
  - M8, the index written over an empty table: one thread's first-call write, raced by the other
    thread's first call, which TSan reports on `first_unknown_`. That is RED on the TSan lane only;
    on other lanes the cell is green either way, and that is stated with it.

**Comments that change** (re-derive with
`git grep -n -i -E 'stores? (and allocates )?nothing|nothing is stored|writes no member' -- include/fixpp/wire tests/wire tests/dictionary spec/behaviors-and-limitations.md .specify/2b-wire.md brain/components/wire.md`,
then keep the hits about `unknown_fields`; `parser.hpp`'s "an Iter view stores nothing", about
`resolved_application_version_`, stays true):
- **`include/fixpp/wire/parser.hpp`, the comment above `unknown_fields()`.** "It stores and
  allocates nothing, so no state of the parse resource changes what it yields" becomes: "It allocates
  nothing, so no state of the parse resource changes what it yields. The view stores no list; this
  MessageView stores one index, where the first unknown entry is: the first call over a non-empty
  table stores the index when it is representable; otherwise it leaves the sentinel and rescans, and later calls start at a stored index (D-688-2)." "The view copies its context out of this
  MessageView, not a pointer to it" stays true.
- **`include/fixpp/wire/parser.hpp`, the new member's comment.** It states the encoding (the
  sentinel, "none" as the end index), the two `detail` predicates and why each half exists (the
  moved-from source, the store bound), why the guard is tested before the empty-table rule, the rule
  that nothing is written over an empty table (the shared `kEmpty`), and the thread contract
  (`[2b §6.6]`: one view, one thread at a time).
  - **The placement, as a condition and a recipe only:** "declared right after
    `resolved_application_version_` so that it falls in the padding before `dict_owner_`; re-derive
    with `-Xclang -fdump-record-layouts-complete` (clang) or `/d1reportSingleClassLayoutMessageView`
    (MSVC)". It states no byte count, no offset and no "the size does not change": a comment may
    record a procedure, not a result (the parent's `.claude/CLAUDE.md`).
  - The implementation's added comments are checked with the parent's
    `.claude/scripts/check-comment-claims.py --root <tree> --base origin/main`, and every hit is read.
- **`include/fixpp/wire/unknown_fields.hpp`, the file header.** "It stores nothing: it holds the
  remaining offset-table entries, …" becomes "The view stores no list: it holds the remaining
  offset-table entries, …", and the header adds that the `MessageView` which builds it keeps one index,
  where the first unknown entry is (D-688-2).
- **`include/fixpp/wire/unknown_fields.hpp`, the private constructor's comment.** "The first unknown
  entry is found here, so begin() and empty() are O(1)" becomes "The first unknown entry is found
  before construction, by scan() on a MessageView's first call or from its stored index on a later
  one, so begin() and empty() are O(1)".
- **`tests/wire/unknown_fields_test.cpp`**: the file header's "and stores nothing, so the parse
  resource's state cannot change what it yields", and the Q-32 block's "unknown_fields() stores
  nothing". Both become "allocates nothing".
- **`tests/dictionary/reify_shared_table_concurrency_test.cpp`**: "its unknown fields classify on
  every iteration" stays true and gains "and the view remembers where the first one is (D-688-2)".
- The live B&L `B-688-1` (§4), and `[2b §4.8]`'s as-built line (§6).

### 1.5 API, C ABI and layout

- **C ABI: no change.** No C symbol, struct, constant, error code or documented result moves. No C-ABI
  version bump.
- **Public C++: source-incompatible in three places**, each recorded in the B&L (§4). No shim is kept.
  Removing public C++ API before the first release has precedent in the live B&L: `B-098-4`
  (`SessionConfig::dialect_overlay`) and `B-652-1` (`redact_tag554`). Article IV §1 makes C++ the
  primary surface but states no compatibility promise for it. The three places:
  - the `unknown_fields_view` constructor from a `kv` span is removed;
  - the iterator (§1.4, the iterator contract):
    - `operator*` returns `kv` by value. Code that binds `kv const&` or `auto const&` keeps
      compiling, through lifetime extension.
    - Code that stops compiling or stops being valid: `for (auto& x : mv.unknown_fields())`
      (a non-const lvalue reference cannot bind the prvalue, on clang and gcc, §A.4); `&*it`; a
      reference held across `++`; and the iterator's public `explicit iterator(kv const*)`
      constructor, which goes with the span constructor.
    - `begin()` and `end()` lose `[[clang::lifetimebound]]`, which changes what clang diagnoses,
      not what compiles.
    - Re-derive the in-repo uses with
      `git grep -n -E 'unknown_fields\(\)|unknown_fields_view' -- tests src include bench bindings`;
      for the iterator's constructor,
      `git grep -n -E 'unknown_fields_view::iterator|iterator\{' -- tests src include bench bindings`
      and keep the hits that mention `unknown`: only the header's own `begin()` and `end()`;
  - `sizeof(unknown_fields_view)` grows, and `sizeof(MessageView<…>)` shrinks in both modes.
- **Layout: rebuild required.** `MessageView` is built by the library (the session's parse path) and
  read inline by application code, so mixing objects built against the old and the new header is an ODR
  violation. That is the same "requires a rebuild" disclosure 093 made for `MessageStore::reset_to`.
  - fixpp has no C++ ABI golden: `.github/workflows/abi-golden.yml` checks the C-ABI symbol set only.
  - Size pins, in `B-104-7`'s shape:
    `git grep -n -E 'sizeof ?\([^)]*\b(MessageView|unknown_fields_view|Writer)\b' -- tests include src bench ':!.specify/688-707-559-wire-headroom-token.md'`.
    The pathspec does not reach this note, so the recipe cannot match its own text. A hit that pins
    the size of one of these types moves with the change. The codegen flyweight pin
    (`tests/codegen/accessor_resolution_census_test.cpp`, `sizeof(Msg) == sizeof(MessageView const*)`)
    is a pointer, so it is untouched.
    - **Known positive:** the same pattern with `OffsetTable::entry` in place of the three names
      prints hits, `tests/wire/offset_table_test.cpp` among them, so the pattern can report a pin.
  - **D-688-2 (v0.6):** `MessageView<Index>` gains `first_unknown_`, declared in what §A.6's
    before-run shows is padding. The design requires both modes' `sizeof` and `alignof` to equal the
    before-run's on each toolchain: clang and gcc are read by §1.4a's recipe before and after, and
    MSVC at its `-k 0` build. The whole-repo pin recipes and each hit's disposition are §1.4a's
    "The layout".
- **B(L)'s `kParseContainers` counts the `pmr::vector` members `MessageView<Index>` and `OffsetTable`
  construct from the parse resource**, times the view's moves (its recipe is the comment above it in
  `src/session/parse_capacity.hpp`).
  - Deleting `unk_items_` changes what that recipe counts. **Re-derive the constant on the MSVC debug
    sandbox by its recipe and set it to the result.**
  - An unmeasured edit is not allowed: a count left high is the safe direction, but it is no longer the
    recipe's answer.
  - **Every pin of the value moves with it.** Re-derive them with
    `git grep -n kParseContainers -- . ':!.specify/688-707-559-wire-headroom-token.md'`:
    - `src/session/parse_capacity.hpp`, the constant itself.
    - `expected_parse_buffer` in `tests/session/inbound_frame_dispositions_test.cpp`, the test's own
      copy, kept apart on purpose so that a formula change needs a test edit. Re-run
      `InboundFrameDispositionsSc007.*` after the edit.
    - `B-093-7`'s worked totals in `spec/behaviors-and-limitations.md`, which were measured with the old
      value. Re-read them from those two cells and record them with an "Amended (W1)" line (§4).
    - 093's own records of the value (`specs/093-inbound-frame-dispositions`: plan, research,
      data-model, tasks and checklist `nfr.md`). They are frozen and flagged, not edited (§6).

### 1.6 Tests (RED first)

1. **RED: `WireUnknownFieldsQ32`, rewritten.**
   - Use the existing rig: a monotonic block whose upstream is a tracking resource over
     `null_memory_resource()`, sized by the sweep to the smallest block the parse fits.
   - Assert that `unknown_fields()` yields every unknown field (35, 34 and the 5000s, as the control
     counts them) and that `upstream.allocate_calls()` stays 0.
   - RED on the base: the base yields an empty view. The explicit null upstream makes the cell's result
     the same on every lane, MSVC debug included.
   - The cell's old meaning (the same empty view twice, the process lives) is what D-688-1 removes; its
     name changes with it.
2. **Q-33 is not the RED, and R-W1-3 changes its frame.**
   - On MSVC debug the base passes Q-33, because the list fits from the heap through the forwarding
     upstream.
   - Its filler `2=` is AdvId, which `FIX44.xml` defines (`grep -c "number='2'" dictionaries/FIX44.xml`
     prints 1). So does every other field of its frame.
   - So under `tag_defined` the frame has no unknown field. The forwarding arm's "non-empty view"
     check would go RED, and the frame comment "tag 2 is not a NewOrderSingle field, so each is an
     unknown field" stops being true.
   - It is re-pinned:
     - The `2=` filler stays, because the frame's density is chosen for B(L).
     - The body gains a few fields whose tag `FIX44.xml` does not define. Check a choice with
       `grep -c "number='5000'" dictionaries/FIX44.xml`, which must print 0. The `'`-quoting caveat of
       §1.1 applies: the same grep's `number='2'` row is its known positive.
     - The cell's existing `delivered` assertion is what shows that the added undefined tags do not
       trip a reject on that session before `fromApp`.
     - With a null upstream, `unknown_fields()` after `group_slices(453)` yields exactly those fields,
       in order. The base yields an empty view there, so the cell is RED on the base.
     - On the forwarding lane the cell asserts the same set. The base yields the `2=` fields too, so
       the cell is RED on the base there as well. The spill count comes from `group_slices()` alone.
     - The frame comment is rewritten to say which tags are unknown and why.
3. **The predicate (R-W1-3).** Each new cell is RED on the base.
   - A tag the table defines only for another MsgType is not yielded. As a cross-check, the validator
     still answers `wire_unexpected_tag` on the same frame.
   - Item 3's real-dictionary cells live in a new file, `tests/wire/unknown_fields_dict_test.cpp`, in
     the `wire_dict_tests` bucket, which links `fixpp_dictionary` and defines `FIXPP_DICT_DATA_DIR`
     as the source tree's `dictionaries/` (R-W1-5 C4).
   - A FIX 5.0 cell: load `dictionaries/FIX50SP2.xml`, parse a NewOrderSingle with the standard header
     and one undefined tag, and assert that only the undefined tag is yielded. Run it on the base first
     and record the RED. Until that run, the RED is a reading, not a measurement.
   - `DictBoundUnknownSplit` and `EmptyDictAllTagsUnknown` stay green. A hand-built table defines every
     tag it is told anything about (the comment on `table_view::mark_defined`), so 35 and 34 are
     defined and 5000 is not.
4. **Zero allocation.** Each instrument names what makes it report non-zero.
   - **What it catches beyond item 1 (R-W1-5 A2).** Item 1's counter is the block's upstream, so it
     sees an allocation only once the block is spent. An allocation that fits in what the block has
     left (its alignment slack, or room a sweep step leaves) reaches no upstream. Item 4's counter is
     the parse resource itself, so it sees every allocation `unknown_fields()` makes, whatever its
     size, on a frame not sized at the edge.
   - **The wire cell.** A counter upstream of a block too large to exhaust is never called, so it
     could not report a list that creeps back. The counter is the parse resource itself:
     `fixpp::test_support::pmr_allocation_tracking_resource` over a monotonic block, its
     `allocate_calls()` read after `parse()` (the parse itself allocates), then `unknown_fields()`
     called and iterated over a frame with an undefined tag, and the count must not have moved. **RED
     on the base**, which builds its list in the parse resource.
   - **The heap cell.** The global `operator new` counter has global allocation state, and
     `[const §VII.8]` admits to a grouped executable only tests with none. So it lives in
     `tests/alloc_guard/`, on `wire_alloc_guard_test.cpp`'s rig (mallocnesia, through
     `tools/check_alloc.py`), and not in the grouped wire binary. Its frame gains an undefined tag, and
     the window covers `unknown_fields()` and its iteration.
     - **It is not RED on the base**: the base list draws on the pmr parse resource, which that rig
       backs with a stack block and a null upstream, not on the heap. What it guards is a heap
       allocation creeping into the walk.
     - **Its known positives:** the population's positive control (`planted_alloc_witness.cpp`, run
       by `check_alloc.py --expect-violation`) shows interception is live; and a mutant that builds a
       plain `std::vector` inside `unknown_fields()` must turn the cell RED. That mutant's RED is
       recorded in the evidence file.
5. **Unchanged semantics, and the boundaries.**
   - New: document order over unknowns interleaved with known fields.
   - New: unknown tags inside one group instance and between instances, plus a trailing scalar. The
     cell asserts exact document order and every occurrence.
   - New: duplicate unknown tags through the parser path, each yielded.
   - New: `empty()` on a frame with none, and `begin() == end()`.
   - New: an iterator taken from a temporary view (`auto it = mv.unknown_fields().begin();`) stays
     valid while the `MessageView` lives. It is the witness of the iterator contract's
     `lifetimebound` choice (§1.4). On the base it fails to compile on clang under `-Werror`
     (`-Wdangling`); on gcc it compiles and passes, because the base iterator is valid at runtime.
   - New: the `same_as` assert of §1.4, `operator*` returns `kv` by value. RED on the base.
   - New (R-W1-5 C5): the removed constructors stay removed.
     `static_assert(!std::is_constructible_v<unknown_fields_view, std::span<unknown_fields_view::kv const>, detail::generation_token>)`
     and `static_assert(!std::is_constructible_v<unknown_fields_view::iterator, unknown_fields_view::kv const*>)`,
     both RED on the base. Known positive: `static_assert(std::is_nothrow_default_constructible_v<unknown_fields_view>)`.
   - The static pins of this item and of §2.6 and §3.5 that are RED on the base sit in one sibling TU
     of the grouped wire bucket, apart from the behavioural cells, because a pin that does not compile
     on the base takes its whole TU down with it, and the behavioural cells must show their own RED
     there.
   - New: a view iterated after its `Parser` is destroyed, with the dictionary alive, still classifies.
     This extends the existing post-`Parser`-destroy cell from the first call to iteration.
   - New, debug only: the generation check (§1.4). In `tests/wire/lifetime_trap_test.cpp`, inside its
     `#ifndef NDEBUG` block, beside `ParserViewTrapsAfterArenaRecycle`:
     - parse a frame with one undefined tag, and bind `auto uf = mv.unknown_fields(); auto it =
       uf.begin();`. The view is named so that the base compiles on clang and its RED is behavioural;
     - `ASSERT_NE(it, uf.end())`, then `framer.recycle_pool()`;
     - `EXPECT_DEATH((void)*it, "")` and `EXPECT_DEATH(++it, "")`.
     - The condition that makes each death the check's: `Framer::recycle_pool` bumps the pool
       generation and releases no storage, so nothing but the check can end the process. Re-read
       `recycle_pool` at implementation.
     - RED on the base, whose iterator never checks.
6. **Mutants.**
   - Reintroduce a materialised list over the parse resource: cell 1's zero-upstream assertion goes RED.
   - Drop the classification (yield every entry): `DictBoundUnknownSplit` goes RED (35 and 34 are
     yielded).
   - Drop the framing exemption: `EmptyDictAllTagsUnknown` (its `has_framing` check) and
     `DictBoundUnknownSplit` (exactly one tag yielded) go RED (R-W1-5 C3).
   - `tag_defined` back to `field_valid_for`: the predicate cells of item 3 go RED.
   - Skip group-contained entries, and deduplicate equal tags: item 5's group and duplicate cells go
     RED.
   - `operator*` returns `kv const&` from a `kv` cached in the iterator: the `same_as` assert goes
     RED (§A.4, the `CACHED_REF` variant, on clang and gcc).
   - Restore `[[clang::lifetimebound]]` on `begin()` and `end()`: item 5's temporary-view cell
     stops compiling on clang (§A.4, the `KEEP_LB` variant).
   - Remove the iterator's generation check from `operator*`, or from `operator++`: the matching
     arm of item 5's generation cell goes RED, on a debug lane.
7. **Fuzz** (`[const §VII.7]`, parser-adjacent). The properties' reference is the harness table's own
   `tag_defined`, which the harness calls directly (R-W1-5 B3). So the check says that the view's
   yield agrees with that predicate entry by entry, not that the predicate is right;
   `TagDefined101`'s exact-set cells own the predicate. `tests/fuzz/fuzz_wire_parser.cpp` gains a property
   check. A differential against a filter that uses the same classifier would be tautological, so the
   check states the properties instead:
   - every yielded tag is one for which the dictionary's `tag_defined` is false, and none is 8, 9 or
     10;
   - every such entry of `entries()` is yielded exactly once, in document order.

   **The allocation counter is the parse resource itself.** The harness parses over a 256 KiB stack
   `monotonic_buffer_resource` with a null upstream, and every fuzz frame's parse fits in it, so a
   tracking resource at that arena's upstream would never be called and could not report a list that
   creeps back. So:
   - the parse resource becomes `fixpp::test_support::pmr_allocation_tracking_resource`
     (`tests/support/pmr_allocation_tracking_resource.hpp`) with the monotonic arena as its upstream;
   - its `allocate_calls()` is read after `parse()`, because the parse itself allocates; then
     `unknown_fields()` is called and iterated, and the count must not have moved;
   - **known positive:** the PR's harness run against the base header, with one seed frame that carries
     an undefined tag, reports a non-zero delta, because the base builds its list in the parse
     resource. That run (or the materialised-list mutant of item 6 under the harness) is recorded in
     the evidence file. Without it the property could not report a non-zero result.
   - **The positive must be the counter's, not another check's.** The seed's other tags satisfy the
     classification properties on the base too: MsgType `D`, and only tags the table registers
     under `D` through `add_valid_tag`, plus the one tag defined nowhere. The counter check runs
     before the classification checks and fails with its own message, so the recorded failure names
     the counter.

   **Both sides of the predicate must fire.** The harness's table defines only the group tags 453, 448
   and 447, so the "not yielded because defined" side would fire only on a frame that happens to carry
   one of them. The table gains a few common tags through `table_view_builder::add_valid_tag` (for
   example 34, 35, 49 and 55 under one MsgType); `add_valid_tag` marks the tag defined, so
   `tag_defined` is true for them. The 10-minute PR campaign runs on it.
8. **Bench**: the `bench/wire` cases of §1.4, timed by the transplanted A-B-A-B run, and CI's paired
   rows.
9. **D-688-2 (v0.6):** the cells (a) to (e) of §1.4a, "Tests", RED first, each with its mutant. The
   bench of item 8 is re-run in full after D-688-2 (§1.4a, "Expected bench effect").

---

## 2. fixpp#707 — `group_writer_token` can be forged with `std::bit_cast`; the class census

### 2.1 The premise at `b93b8a28` (read; run)

- `fixpp::wire::group_writer_token` (`include/fixpp/wire/writer.hpp`) is an empty struct. Its default
  constructor is private and defaulted, `Writer` is its friend, and its copy and move are implicit. It
  is trivially copyable.
  - **Run (§A.1):** `std::bit_cast<group_writer_token>(std::array<std::byte, 1>{})` compiles on
    clang 22.1.2 and gcc 13.3.
  - With a forged key, `group_writer`'s public constructor
    `group_writer(group_writer_token, Writer*)` makes a `group_writer` over any `Writer`.
- **What the forgery reaches.** `group_writer::append_field` forwards to `Writer::append_raw` (public),
  and `close_impl` writes nothing. So it gives a consumer nothing it could not already do. The
  restriction the header documents ("only Writer can create a group_writer_token") is still false, and
  the reach is the consumer's own code, never the peer's. Same severity as 104's RC#1.
- **The issue is right on the shape.** One thing it does not say: **104's shape alone does not compile
  here.**
  - `Writer::open_group` returns `core::expected_t<group_writer>{std::in_place, group_writer_token{},
    this}`. `std::expected`'s in-place constructor forwards the key and copies it into `group_writer`'s
    by-value parameter inside the standard library, where `Writer`'s friendship does not reach.
  - **Run (§A.1):** with the private copy constructor, that line fails on both compilers ("no matching
    constructor" / "no matching function").
  - Building the prvalue `group_writer{group_writer_token{}, this}` in `Writer`'s scope and moving it
    into the `expected` compiles. `group_writer` is move-constructible, and guaranteed elision builds
    the key in place.
  - `SeqnumManager::freeze_key` never met this, because `Session` passes it straight to a member
    function.

### 2.2 The class census — what "of this shape" means

`brain/failure-classes.md` class 24 gives the scan as a name heuristic: `*_token`, `*_key`,
`passkey`. A name heuristic misses friend-gated types with other names, so the census is a condition
checked by trait:

> A **passkey** is a class whose only purpose is to be named as a parameter, and whose constructor only
> a friend may call. It is forgeable when it is trivially copyable (`std::bit_cast`). It is also
> forgeable when it is an implicit-lifetime type *and* a non-friend can copy it: implicit object
> creation, for example `std::start_lifetime_as`, gives an lvalue, which a by-value parameter then
> copies.

- **Recipe: the union of two lists, then a compile.** Each hit is dispositioned.
  1. `git grep -n -E 'friend ' -- include`: every friend declaration, of a class or of a function.
     `snapshot_key`'s only friend is a function, so v0.1's `friend (class|struct)` grep could not list
     it, although it was one of that recipe's own two findings.
  2. `git grep -n -E '(_key|_token|_tag|passkey)\b[^;(]*\{' -- include | grep -E 'struct|class'`:
     brain class 24's name heuristic, for a key that befriends nothing.
  3. Compile one TU that includes each candidate's header and evaluates `std::is_trivially_copyable_v`,
     the `bit_castable` concept and `__builtin_is_implicit_lifetime` on each (§A.2).
  - **Self-check.** Lists 1 and 2 together name every type this section dispositions. A recipe that
    misses one of its own findings reports clean because it cannot report anything else.
- **Which hit is a candidate.** A hit is a passkey candidate only if its class is empty (no non-static
  data member) and has no member function but its constructors. By that condition:

| Hits (the class that holds the friend line, or the name-scan type) | Disposition |
|---|---|
| `wire::group_writer_token`, `dict::detail::snapshot_key` | Passkeys, and forgeable (below). |
| `SeqnumManager::freeze_key` | A passkey, fixed by 104. Its pins stay where 104 put them (`tests/session/test_104_reset_on_logout.cpp`), and they are the shape's regression row; W1 does not repeat them. |
| Classes with state and behaviour whose friends are owners, builders, loaders or `*_test_access` seams: `Dictionary`, `table_view`, `MessageView`, `OffsetTable`, `dict_hooks`, `Framer`, `Writer`, `group_writer`, `body_builder` with `group_handle` and `entry_handle`, `async_mutex`, `Session`, `Engine`, `SeqnumManager`, `MemoryStore`, `owning_message_handle` | Not passkeys. Each has data members and member functions. |
| Guarded objects with a private constructor and data: `async_lock_guard`, `table_view`'s `valid_tag_set_view` | Not passkeys. The private constructor guards an object, not a call. |
| `wire::frame_view`, `wire::field_view` | Not passkeys: flyweights (below). |
| Friend operators and functions inside a class with state (`decimal`'s comparisons, `SessionId`'s `operator==`, `logon_credentials`'s `operator<<`, `header_length_pairs`, the engine's accept and connect loops, `detail::async_lock_op`, `detail::owning_message_handle_from_frame`) | Not passkeys: the holding class has state. |
| `MessageView::reserve_tag`, `detail::owned_route_key`, `asio_tls_transport_factory::shared_ctx_tag` | Tags, not passkeys (below). |
| `dict::group_ctx_key` (`table_view.hpp`) | Not a passkey: an owned map key with public members, stored in `table_view`'s group-context store. |
| `wire::detail::generation_token` (`view.hpp`) | Not a passkey: a public 4-byte POD (pool id, generation) that guards a debug trap and is constructible anywhere by design. |
| Comment lines that contain "friend", and the name scan's hit on a comment in `group_view.hpp` | No declaration. |

- **Run at `b93b8a28` (§A.2):**
  - `group_writer_token` and `dict::detail::snapshot_key`
    (`include/fixpp/dict/dictionary_snapshot.hpp`) are passkeys, and both are forgeable.
  - `snapshot_key` gates `dictionary_snapshot`'s public constructor, whose design (215) restricts
    minting a key to `make_dictionary_snapshot`. A forged key builds a `dictionary_snapshot` that pairs
    a `table_view` with a `Dictionary` it was not built from. The reach is again the consumer's own code.
- **`wire::frame_view` and `wire::field_view` also come out trivially copyable, but they are not
  passkeys.**
  - They are by-value flyweights, and trivially copyable is part of their contract.
  - Each already has a public minting seam in its own header (`frame_view_slice_access::make`,
    `field_view_access::make`), so `bit_cast` adds no capability.
  - Whether those seams should be public is a different question from #707's, and W1 does not take it.
- **`MessageView::reserve_tag`, `detail::owned_route_key` and `asio_tls_transport_factory::shared_ctx_tag`
  are tags, not passkeys:**
  - `reserve_tag` is a private nested type, so only the class and its friends can name it;
  - `owned_route_key` is documented as constructible anywhere;
  - `shared_ctx_tag` is public and default-constructible by design.
- **`SeqnumManager::freeze_key` was fixed by 104.** 104's own pins in
  `tests/session/test_104_reset_on_logout.cpp` are the shape's regression row (R-W1-5 A1). A copy in
  the W1 wire TU would duplicate them and pull a session header into `wire_pure_tests`, which links
  `fixpp_wire` alone.

### 2.3 D-707-1 — `group_writer_token`

- Give it a user-provided private copy constructor (`group_writer_token(group_writer_token const&)
  noexcept {}`). The defaulted private default constructor stays. Put the same
  `NOLINTNEXTLINE(modernize-use-equals-default,hicpp-use-equals-default)` and the same reason as
  `freeze_key` on it: a defaulted copy constructor is trivial and would undo the fix.
- `Writer::open_group` returns
  `core::expected_t<group_writer>{group_writer{group_writer_token{}, this}}`.
- The header comment states the key's conditions: its copy is user-provided and private, so it is not
  trivially copyable and only `Writer` can pass one. It names no "other routes" (104 round 2's lesson).
- The key is implicit-lifetime (its defaulted default constructor is trivial), but no non-friend can copy
  it, so implicit object creation yields an lvalue nobody can pass. The trait TU pins that.
- **The moved-from temporary.** The prvalue form materialises a `group_writer`, moves it into the
  `expected` and destroys it at the end of the full-expression. `group_writer`'s defaulted move
  clears neither `owner_` nor `closed_`, so the temporary's destructor runs `close_impl` with the
  owner still set. The comment at `open_group` in `writer.hpp` states that condition: it is harmless
  while `close_impl` writes nothing, and a `close_impl` that writes (the backpatch-at-close design
  that `[2b §4.5]`'s comment still describes, §3.1) must first make a moved-from `group_writer`
  inert. A user's own `std::move(*g)` already has this shape, so the hazard is not new in kind.

### 2.4 D-707-2 — `snapshot_key`

104's shape is not available. `std::make_shared<const dictionary_snapshot>(snapshot_key{}, …)` copies
the key inside `std::construct_at`, outside the friend. **Run (§A.1):** a private copy constructor
fails there on both compilers. So the copy constructor must be public, and the remaining hole must be
closed another way.

| Shape | `bit_cast` | implicit-lifetime (clang 22) | `make_shared` |
|---|---|---|---|
| today: defaulted private default constructor, implicit copy | forges | yes | compiles |
| defaulted private default constructor + user-provided **public** copy constructor | rejected | **yes**: an implicitly created lvalue can be copied | compiles |
| **user-provided private default constructor + user-provided public copy constructor** | rejected | **no** | compiles |

- **Chosen: the last row**, `snapshot_key() noexcept {}` private and
  `snapshot_key(snapshot_key const&) noexcept {}` public.
- **clang-tidy.** Both constructors have empty bodies, which `modernize-use-equals-default` and
  `hicpp-use-equals-default` report, and `.clang-tidy` makes every warning an error for headers under
  `include/fixpp/`. Each carries
  `// NOLINTNEXTLINE(modernize-use-equals-default,hicpp-use-equals-default)` on the line above it,
  with its own reason:
  - the copy constructor, `freeze_key`'s reason: a defaulted copy constructor is trivial, so the key
    would be `bit_cast`-able again;
  - the default constructor: a defaulted one is trivial, so the key would be implicit-lifetime again
    (the table's second row).
  - The suppression sits on the line above, not as a trailing `// NOLINT(…)`. G1's exception
    matches the declaration's exact form (below), which a trailing comment would change.
- 215's description stays true: the key is "nameable and copyable" anywhere and mintable only by the
  factory.
- 104's trio cannot apply whole. `!is_copy_constructible` is false by design here. The pins are:
  - not trivially copyable;
  - not `bit_cast`-able;
  - not default-constructible (215's A5, which already exists);
  - not implicit-lifetime, where the compiler offers the trait.
- **`tools/check_dictionary_snapshot_exclusivity.sh` must change with it.**
  - G1's mint pattern `snapshot_key[[:space:]]*(\{[[:space:]]*\}|\([[:space:]]*\))` filters only the
    `= default` declaration. So the user-provided declaration `snapshot_key() noexcept {}` in the
    header counts as a second production mint, and G1 fails.
  - The PR teaches the filter the declaration's exact form, in the header only (the gate's `HDR`).
  - The self-test (`tools/test_dictionary_snapshot_exclusivity_gate.sh`) gains three seeds:
    - the exact declaration in the header is accepted;
    - an extra mint in the header is rejected, which shows the exception is not wider than the one
      declaration;
    - an extra mint in `src/` is rejected.
  - **The self-test's comment-out step changes too.** `comment_out_static_asserts` rewrites every
    `static_assert` block of the gate's `A5TU` (`tests/dictionary/dictionary_snapshot_test.cpp`), and it
    fails unless it rewrote a fixed total. §2.6 adds `snapshot_key` pins to that file, so the self-test
    would go RED mechanically. Instead, it selects the blocks it means by a marker comment on each, not
    by their total.

### 2.5 API, C ABI and layout

- **C ABI: no change.** Neither type reaches `<fix/c_api.h>`.
- **Public C++: no legal call or object changes; the keys' type properties do.** A forgery stops
  compiling. What consumer code can also observe is a trait query on the type:
  `std::is_trivially_copyable_v` becomes false for both keys, and `std::is_copy_constructible_v`
  becomes false for `group_writer_token` (all four are true on the base, §A.4). A consumer `static_assert`, constraint or `if constexpr` on those traits can fail or take
  another branch, although no consumer can legally hold a value of either key. Sizes do not change:
  both stay empty classes.
- **Calling convention.** A user-provided copy constructor makes the key non-trivial for the purposes of
  calls (Itanium and MSVC both pass it indirectly).
  - `group_writer_token` is taken by value only by `group_writer`'s inline constructor.
  - `snapshot_key` is taken by `dictionary_snapshot`'s out-of-line constructor, called only from
    `make_dictionary_snapshot` inside the library.
  - No consumer-visible exported call changes. Mixing objects built against the old and the new header
    is still an ODR mismatch, so "rebuild required" applies, as for 104's `freeze_key`, which took no
    version bump.

### 2.6 Tests

1. **Static asserts, RED on the base:**
   - `group_writer_token`, in a `tests/wire` file of the grouped wire bucket: not trivially copyable;
     not copy-constructible outside `Writer`; not `bit_cast`-able; not implicit-lifetime-*and*-copyable.
   - `snapshot_key`, in `tests/dictionary/dictionary_snapshot_test.cpp`, already on G1's allowlist: the
     pins of §2.4.
   - The `bit_castable` concept is a template, so a rejected cast makes it false rather than a hard
     error.
   - **Known positives, on every lane.** `static_assert(bit_castable<int>)`. And a same-shape positive:
     a local replica of the base `group_writer_token` (an empty class with a defaulted private default
     constructor and one friend), for which `bit_castable` holds. That shows, on MSVC too, that the
     concept can say yes for an empty friend-gated class.
   - Name every replica without the substring `snapshot_key`. G1's name census greps for that substring
     in every test file but its allowlisted one.
   - **One shared guard header (R-W1-5 C1).** The guard and its `kHasTrait` live in one header,
     `tests/support/implicit_lifetime_trait.hpp`, as `inline constexpr bool`, included by both pin
     TUs: the `tests/wire` passkey TU and `tests/dictionary/dictionary_snapshot_test.cpp`. Its name
     and its whole text contain no `snapshot_key` substring, because G1's name census greps the
     contents of every file under `tests/` but its allowlisted one. Each of the two TUs has its own
     per-lane `kHasTrait` cell, under distinct suite names.
   - **The implicit-lifetime guard has this shape** (§A.3):
     `#if defined(__has_builtin)`, then a nested `#if __has_builtin(__builtin_is_implicit_lifetime)`.
     If that leaves the trait undefined, a second branch tries `defined(__cpp_lib_is_implicit_lifetime)`
     for `std::is_implicit_lifetime_v`.
     - Where `__has_builtin` is not a macro, the inner `#if` sits in a skipped group and is never
       evaluated.
     - A one-level `#if __has_builtin(x)` is not a false branch there. The undefined identifier becomes
       `0`, and `0(x)` is not a valid `#if` expression.
     - The repo has no such guard yet: `git grep -n __has_builtin -- include src tests` prints nothing.
       What the guard finds on MSVC is read from the cell below, not from the build: a `-k 0` build
       compiles cleanly both when the trait is found and the pins pass and when it is not found and
       the pins are left out. §A.5 shows, with a stand-in name, that the nested shape falls through to
       "not found" where the name is not a macro, and that the one-level shape is a preprocessor error.
   - **Inside the guard, known positives:** `static_assert(__builtin_is_implicit_lifetime(int))` and the
     base-shape replica (an implicit-lifetime type), each through the guard's macro.
   - **Where the trait exists.** §A.3's probe found it only on clang 22. Clang 18 and gcc 13, 14 and 15
     offer neither route. Which CI clang lanes run clang 22:
     - The libc++ sanitizer presets name `clang++-22`.
     - The other clang presets name an unversioned `clang++`. In `.github/workflows/tier1.yml`, the
       steps that run `llvm.sh 22` point it at the major that `llvm.sh` installed, and print
       `clang++ --version`.
     - So the pin is evaluated on every Tier 1 clang lane whose `clang++ --version` line reads 22. The
       PR's first CI run is where to read that line.
     - Until then, §A.3's local RED is the record. On gcc the guard leaves it out, and a test comment
       says so; it does not pretend to pass.
   - **Which lanes evaluate the pins is a recorded per-lane fact.** The guard defines `kHasTrait` as
     §A.3 does. One runtime cell beside the pins calls `GTEST_SKIP()` naming the lane (the compiler
     and its version) when `kHasTrait` is false, and succeeds when it is true. The evidence file lists,
     per lane and MSVC included, whether that cell ran or skipped, which is the list of lanes that can
     kill the row-2 mutant.
2. **RED proof.** Put the unfixed header first on the include path: exactly the negative asserts fire, on
   clang and gcc (104's method). **Mutants:**
   - Default the copy constructor of either key. The `bit_cast` asserts go RED, on every lane.
   - For `snapshot_key`, default the **default** constructor: row 2 of §2.4's table. Only the
     implicit-lifetime assert can kill it, so it is RED on the clang 22 lanes. On gcc, and on any lane
     where the guard finds no trait, it survives by construction, the test comment says so, and the
     per-lane cell above skips there (§A.3 shows the RED).
   - For `group_writer_token`, the pin "not implicit-lifetime-*and*-copyable" has the base-shape
     replica as its known positive: for the replica the conjunction holds (§A.3).
3. **Behaviour.** `writer_error_path_test` and `round_trip_property_test` (`open_group`) stay green,
   and so do the snapshot tests.
4. **MSVC.** Build `windows-msvc-debug` with `-k 0` on one target list covering every target W1
   touches (R-W1-5 E1), and run the per-lane trait cells of item 1.
   - **The list:** the libraries `fixpp_wire`, `fixpp_dictionary` and `fixpp_session`
     (`src/session/admin_messages.cpp` and `session.cpp` construct a `Writer`); `wire_pure_tests`
     and `wire_dict_tests`; `dictionary_pure_tests` (`dictionary_snapshot_test.cpp`),
     `dictionary_fixt_header_merge_test`, `dictionary_reify_shared_table_concurrency_test` and, with
      D-688-2, `dictionary_reify_shared_empty_view_concurrency_test` (§1.4a cell (e)) and
      `dictionary_reify_tests` (cell (b)'s reify cell, by the re-derive rule below);
     `session_inbound_frame_dispositions` (Q-33, Sc007); `wire_alloc_guard_test`; and the targets
     that compile `tests/interop/conversation/support/conv_wire.hpp`: `interop_conversation_test`,
     `interop_hp_fixt50sp2_logon_hb_logout_test` and `interop_readback_witness_test`.
   - `parser_bench` and `writer_bench` are not in it: `windows-msvc-debug` inherits `_base`, which
     does not build benches (`FIXPP_BUILD_BENCH` in the sandbox's `CMakeCache.txt` shows it).
   - **Re-derive the list** at implementation: for each file the diff touches under `src/`, `tests/`
     and `bench/`, the target whose `compile_commands.json` entry compiles it, plus every target
     that compiles a file including a touched `tests/` header; confirm each name with
     `cmake --build <tree> --target help`.
   - The `kParseContainers` recipe (§1.5) runs in the same sandbox session. MSVC's
   `std::bit_cast` and `std::expected` are the ones not probed in §A, and the build shows whether
   they compile. Whether the implicit-lifetime pins are evaluated there is the trait cell's answer,
   since a clean build is the outcome of both states.
5. **The exclusivity gate** and its self-test, on the changed header.

---

## 3. fixpp#559 site 1 — `Writer::scratch_mr_` is stored and never read

### 3.1 The premise at `b93b8a28` (read)

- `Writer::Writer(std::span<std::byte>, std::pmr::memory_resource* scratch_mr)`
  (`src/wire/writer.cpp`) stores the pointer in `scratch_mr_`. The member is marked
  `[[maybe_unused]]` with `// see fixpp#559`. Re-derive that nothing reads it:
  `git grep -n scratch_mr_ -- include src` finds the declaration and the initializer only.
- **The writer keeps no group bookkeeping, so there is nothing to put on the resource.**
  - `open_group` writes `NoXxx=count` up front.
  - `group_writer::close_impl` writes nothing ("The count was already written at open_group();
    nothing to backpatch").
  - `group_writer` holds an owner pointer and a flag.
- The documents still describe bookkeeping that was never built. **Recipe**, each hit dispositioned in
  §6:
  `rg -n -i -e 'scratch_mr' -e 'writer[ -]scratch' -e 'three[- ]arena' -e 'three pinned' -e 'group bookkeeping' specs/004-wire-codec .specify/2b-wire.md brain`.
  Two kinds of hit stay. The validator's own `scratch_mr` (`validate(msg, scratch_mr)`, in 2b and
  in 004's spec, data-model, plan, tasks and validator checklist) is a different resource. 2b's Gate A
  history tables are history. The rest:
  - `[2b §4.5]`: its `Writer` signature block, whose comment reads "open-group entry-count tracking +
    offset list for backpatch at close";
  - `[2b §6.6]` and `[2b §8]`: the writer-scratch row of the three-arena table, and seam #13;
  - `specs/004-wire-codec`: every three-arena line of the plan, research D-3, the data-model's
    `Writer` fields and arena rows, contracts `writer.hpp`, tasks, and checklists `nfr.md` CHK001 and
    CHK003.
- **In code**, the phrases without `scratch_mr`
  (`rg -n -i -e 'writer[ -]scratch' -e 'three[- ]arena' -e 'three pinned' -e 'group bookkeeping' -e 'scratch_mr for group' tests src include bench`)
  find:
  - `writer.hpp`'s comments, including the member's;
  - `three_arena_pinning_test.cpp`;
  - `tests/alloc_guard/wire_alloc_guard_test.cpp`'s "Writer scratch/dst" arena.
- Two code hits are false positives: `accessor_smoke_test.cpp`'s "group bookkeeping inside
  OffsetTable" and `group_view_failloud_test.cpp`'s "three arena-exhaustion" modes.
- Every two-argument construction is found by the compiler (§3.3).
- **What the issue got right:** both choices, and that removal is a public API change.
- **One thing it did not know:** `tests/wire/three_arena_pinning_test.cpp` cannot assert the Writer half,
  because the Writer allocates nothing.
  - Its header describes only the per-message and carry arenas; the "three-arena" name comes from
    `[2b §6.6]`, which §6 amends.
  - After D-559-1 there is no Writer half to assert, and the header's model line follows the amended
    `[2b §6.6]`.

### 3.2 Options

| | Option | Outcome |
|---|---|---|
| **b** | **Delete the parameter and the member.** `Writer(std::span<std::byte> dst)`. | **Chosen (D-559-1).** The code matches the contract it states, and nothing is left unused. |
| a | Implement bookkeeping on `scratch_mr`. | Rejected. Nothing needs it. Inventing state (a LIFO stack, a count check) would be a feature #559 did not ask for, and it would add allocations to a path that has none. |
| c | Keep a `[[deprecated]]` two-argument overload that forwards to the new one. | Rejected. Removing public C++ API before the first release, with no shim, has precedent in the live B&L (`B-098-4`, `B-652-1`), and Article IV §1 states no compatibility promise for C++. The PR updates every in-repo caller, so a shim serves nobody. (`[const §X.7]` is about the C ABI and is not the authority here.) |
| d | Keep the parameter, ignore it, drop the member. | Rejected. A documented parameter that does nothing is the defect #559 names, moved one line up. |

### 3.3 D-559-1 — what changes

- `include/fixpp/wire/writer.hpp`:
  - drop the parameter, its `[[clang::lifetimebound]]` and the member;
  - drop the `<memory_resource>` include, which nothing else in the header uses (IWYU, R-W1-5 E2;
    an includer that used `std::pmr` through it must include it itself);
  - the design-summary comment and the constructor comment say `Writer(dst)`.
- `src/wire/writer.cpp`: the constructor.
- **Every caller**, re-derived with `git grep -n -E 'wire::Writer[ ]*[a-z_]*[({]|\bWriter [a-z_]+[({]' --
  src tests bench`. The callers are in `src/session/admin_messages.cpp`, `src/session/session.cpp`'s
  replay builder, tests, benches and `tests/interop/conversation/support/conv_wire.hpp`.
  - Callers that built a `monotonic_buffer_resource` only to pass it drop it.
  - `src/capi` does not construct a `Writer`.
  - Documentation and generators:
    `git grep -n -E 'Writer[ ]*[a-z_]*[{(]|wire::Writer' -- tools examples bindings docs '*.md' ':!specs' ':!.specify'`
    finds no construction outside `specs/` and `.specify/`. The codegen emits `TemplateWriter`, an
    unrelated class, and `body_builder` does not reuse `Writer`. The design-text sites are §6's.

### 3.4 API, C ABI and layout

- **C ABI: no change.** `tests/abi/golden/fixpp_capi_symbols.txt` lists no `Writer` symbol:
  `grep -i writer tests/abi/golden/fixpp_capi_symbols.txt` finds none.
- **Public C++: source-incompatible (BREAKING for C++).** Every two-argument `Writer` construction stops
  compiling. Recorded in the B&L (§4).
- **Layout: rebuild required.** `sizeof(Writer)` shrinks, and the out-of-line constructor's mangled name
  changes (a link error for a stale object, not a silent mismatch). Size pins:
  §1.5's recipe, which covers `Writer`, with its known positive.

### 3.5 Tests

- The change is subtractive, so the RED is the compile.
  - A `static_assert(!std::is_constructible_v<Writer, std::span<std::byte>, std::pmr::memory_resource*>)`
    in the wire bucket is RED on the base.
  - `static_assert(std::is_nothrow_constructible_v<Writer, std::span<std::byte>>)` is its known
    positive.
- The writer tests, the round-trip property test, the alloc-guard writer cell and the writer bench stay
  green with the parameter removed. `tests/alloc_guard/wire_alloc_guard_test.cpp` builds a two-argument
  `Writer` over its `write_scratch` arena. The removed constructor forces that edit at compile time, and
  the arena and its comment go with it.
- `writer_bench` loses an unused resource from its loop body. That is a perf change in principle, so the
  bench stays in the paired run.

### 3.6 Closing #559

#559's thread names one more site, in a comment: `SessionConfig::message_arena`. It is public, no
production code reads it, and it is out of #481's warning scope. **R-W1-1** splits it out as fixpp#735,
placed in B31 (S8) in the parent's `phases/phase-4/issue-batches.md`. PR #720 (B27) did sites 2 and 3,
and W1 does site 1, so **the W1 PR closes #559**, and its body names #735 as the fourth site's home.

---

## 4. B&L delta

- **Amended: `B-004-5`.** It says tags absent from the dictionary are exposed through
  `unknown_fields()` and written back with zero allocation. Until W1 that was not what was built: the
  set was the tags not declared for the MsgType (`field_valid_for`, §1.1), and the read allocated in
  the parse resource. The row gains an "Amended (W1)" line saying so, and that since W1 the row holds
  as written (`B-688-1`).
- **Amended: `B-093-8`.**
  - Its "returns an empty view when its arena is exhausted" description is superseded.
  - The row keeps its history (the #540 terminate and its reproduction) and gains: since W1,
    `unknown_fields()` allocates nothing, so no resource state changes what it yields.
  - Witnesses: the rewritten `WireUnknownFieldsQ32.*` and
    `InboundFrameDispositionsQ33.UnknownFieldsAtHeadroomExhaustion`. The Q-33 witness changes meaning:
    it now asserts that the frame's undefined tags are yielded at headroom exhaustion (§1.6 item 2),
    not that the view is empty.
- **Amended: `B-093-7`.** Its worked totals are re-read from `InboundFrameDispositionsSc007.*` after
  `kParseContainers` is re-derived (§1.5), under an "Amended (W1)" line that quotes the old totals.
- **Narrowed, not retired: `L-093-5`.**
  - Its `MessageView::unknown_fields()` bullet is deleted, with an "Amended (W1, #688)" line quoting it.
  - **It is retitled.** The title says "two reports cannot be told from an absent result"; the two are
    `group_slices()` and `unknown_fields()`. After the deletion only `group_slices()` is left, so
    the title says "one report" (`group_slices()`), and the "Amended (W1, #688)" line quotes the old
    title too.
  - The `OffsetTable::group_slices()` bullet stays live: a raw empty span still cannot be told from an
    absent group, and `group_slices_status()` is the status-bearing form.
  - Its other bullets are unchanged.
- **New: `B-688-1`.**
  - `unknown_fields()` is the filtered view of `[2b §4.8]`. It lists every field whose tag the
    dictionary does not define (`table_view::tag_defined`), in document order, framing tags exempt,
    and allocates nothing. A dict-free view lists every non-framing field.
  - **The set changed in W1 (R-W1-3).** A tag the dictionary defines for another MsgType is no longer
    listed, and on FIX 5.0 dictionaries the standard header is no longer listed. On a table that
    carries the FIXT framing surface (FIX 5.0, and FIX 4.0 to 4.4 on a FIXT route), every FIXT framing
    tag counts as defined. With validation on,
    such a misplaced tag is `wire_unexpected_tag`. With it off, the field is reachable through
    `get(tag)` and field iteration only.
  - Its cost is a classification per entry per iteration.
  - The dictionary it was parsed against must outlive every iteration, not only the first call.
  - C++ source changes: the `kv`-span constructor and the iterator's `kv const*` constructor are
    removed, and `iterator::operator*` returns `kv` by value. So `for (auto& x : …)`, `&*it` and a
    reference held across `++` stop compiling or stop being valid; `kv const&` and `auto const&`
    bindings keep compiling. C ABI: none. The layout is `B-707-1`'s.
  - `begin()` and `end()` no longer carry `[[clang::lifetimebound]]`: an iterator taken from a
    temporary view is valid, and clang no longer diagnoses one taken through a temporary
    `MessageView` (`parse(…)->unknown_fields().begin()`), which dangles. `unknown_fields()` keeps
    its own attribute.
- **Amended in v0.6 (D-688-2): `B-688-1`**, which the W1 implementation has already written into the live
  file:
  - "Nothing is stored, so no state of the parse resource changes what it yields" becomes: "It
    allocates nothing, so no state of the parse resource changes what it yields. The view stores no
    list; the `MessageView` stores one index, where the first unknown entry is: the first call over a
    non-empty table stores the index when it is representable; otherwise it leaves the sentinel and rescans."
  - Its Cost bullet's "a second `unknown_fields()` call classifies again, and `empty()` on a frame
    with no unknown field scans the whole table" becomes: "the first `unknown_fields()` call
    classifies as far as the first unknown entry and stores where it is when that index is
    representable, so a later call makes no classification before it, and `empty()` is O(1) after the
    first call; an index that is not representable leaves the sentinel, and every call rescans. Each iteration still
    classifies every entry after the first unknown one."
  - It gains a threading sentence: "One `MessageView` is read by one thread at a time (`[2b §6.6]`).
    The first `unknown_fields()` call over a non-empty table stores the index when it is representable; otherwise it leaves the sentinel and rescans; a store is a write, as
    `group<>()` writes the view's group context. A null reify handle's shared empty view is never written by `unknown_fields()`."
- **Amended in v0.6 (D-688-2): `B-707-1`.** Its `parser.hpp` bullet gains: "`MessageView<Index>`
  gains `first_unknown_`, an index declared in what was padding before `dict_owner_`;
  `MessageView<Iter>` gains an empty member." The sentence on whether either size changes is written
  only after the after-run, from its result on each toolchain (clang and gcc by §1.4a's recipe, MSVC
  at its `-k 0` build), and not ahead of it. The re-derive recipe stays.
- **New: `B-559-1` (BREAKING, C++ only).** `wire::Writer` is constructed from its destination buffer
  alone. It allocates nothing, with or without groups. C ABI: none. The layout is `B-707-1`'s.
- **New: `B-707-1`, one layout row in `B-104-7`'s shape.**
  - It covers `MessageView` and `unknown_fields_view` (D-688-1), and `Writer` (D-559-1).
  - It states both keys' conditions:
    - `group_writer_token`'s copy constructor is user-provided and private, so it is not trivially
      copyable and only `Writer` can pass one;
    - `snapshot_key`'s copy constructor is public and its default constructor is user-provided and
      private: copyable anywhere, mintable only by `make_dictionary_snapshot`, not trivially copyable,
      and not implicit-lifetime.
  - It says a consumer compiling against these installed headers must rebuild, and that the C ABI is
    unaffected.
  - It says no legal call or object changes for the keys, but their type properties do:
    `is_trivially_copyable` becomes false for both, and `is_copy_constructible` for
    `group_writer_token`, so consumer code that queries those traits can fail or take another branch
    (§2.5).
  - Re-derive its members from the diff of the touched headers, as `B-104-7` does.
  - Witnesses: the `static_assert`s of §2.6.

## 5. Gate A triggers (`[const §XVII.1]`)

| Trigger | Touched | By |
|---|---|---|
| public C++ API or C ABI | **yes**, the C++ API only | D-688-1 (`unknown_fields_view`, `MessageView` layout); D-559-1 (`Writer`'s constructor); D-707-1/2 (the keys' copy constructors) |
| the parser | **yes** | D-688-1 (`MessageView::unknown_fields`); D-688-2 (`MessageView<Index>`'s layout: one member, the first-unknown index) |
| wire format or codegen layout | no | Bytes on the wire do not change, and the emitted flyweights hold a pointer. D-688-2's layout change is the parser's (row above). |
| concurrency, session FSM, store, security | **yes**, concurrency | D-688-1 removes a `mutable` write. D-688-2 adds one back: one `mutable` index; the first `unknown_fields()` call over a non-empty table stores the index when it is representable; otherwise it leaves the sentinel and rescans. `[2b §6.6]`'s single-thread contract is unchanged. Concurrent `unknown_fields()` on one non-empty view becomes a race, as `group<>()` on one view already is. The only library-shared view (`kEmpty`) has an empty table and is never written by `unknown_fields()` (§1.4a, cell (e)). Neither adds synchronisation. |
| a new `.specify/` design document | **yes** | this note |

**The four Appendix A controls apply, with no reading of `[const §X.6]` needed.**
- Appendix A's "Wire format / parser" row ("offset-table semantics, framing rules, validator changes")
  fires on D-688-1, as this table's parser row says.
- Their state, in the shape of `.specify/563-564-callback-send.md` §0a:
  - **Codex Gate A:** this loop. Round 1 is applied in v0.2, round 2 in v0.3, and round 3, the converged round, in v0.4
    ("Gate A").
  - **Codex Gate A, v0.6:** R-W1-6's new member changes the parser's layout, which is Appendix A's
    trigger ("touches the wire format, parser, or codegen layout"). So the D-688-2 delta gets a
    scoped Codex Gate A review before implementation, as the amended clauses of v4.1 to v4.4 did.
    Round 4 is applied in v0.7, and the scoped round 5 converged (P1=0, P2=0), its three P3s applied
    in v0.7 ("Gate A"). The delta is
    §1.4a and the lines it flags in §1.4, §1.5, §1.6, §4, §5, §6 and §7.
  - **`/clarify`:** by hand. The rulings record is its record (R-W1-1 to R-W1-4).
  - **`/analyze`:** by the spec-analyzer over the converged v0.4 note, after Gate A and before
    implementation. Its findings and their dispositions are R-W1-5 in the rulings record, and v0.5
    applies them.
  - **Owner `/plan` sign-off:** the owner's delegation of 2026-11-27. The rulings record states that
    the delegation is the Article X §6 / XII §9 `/plan` sign-off for W1, as for 106 and 107.

## 6. Documents to amend or flag

Each list below comes from a recipe:
- `kParseContainers`: §1.5's recipe.
- The writer scratch: §3.1's recipe.
- 081's texts:
  `git grep -n -E 'field_valid_for|unknown_fields|inbound parser' -- specs/081-strict-validation-residuals tests/dictionary/fixt_header_merge_test.cpp include/fixpp/dict src/dictionary`.
  A hit that only says `field_valid_for` stays byte-identical is still true and stays. A hit that
  says the parser classifies through it is stale.
- The test comments:
  `git grep -n -i -E 'unknown|classif' -- tests/wire/unknown_fields_test.cpp tests/dictionary/reify_shared_table_concurrency_test.cpp`.

- The changed declarations (the iterator's attributes and return type, the passkey's constructors,
  G1's mint pattern):
  `git grep -n -E 'unknown_fields_view|iterator begin\(\)|snapshot_key\(\)|snapshot_key\(snapshot_key|G1_MINT|passkey' -- .specify specs brain include tools ':!.specify/688-707-559-wire-headroom-token.md'`.
  - **Its known positives** are the records below that pin those declarations: `[2b §4.8]`'s
    `begin()` and `[2b §6.4]`'s "Every `View`-derived type"; the `begin()` of
    `specs/004-wire-codec/contracts/unknown_fields.hpp`; 215's `snapshot_key() = default;` and
    `G1_MINT`; `brain/components/dictionary.md`'s "The passkey and the provenance check stand"; and
    `include/fixpp/dict/dictionary_snapshot.hpp`'s "the pairing and the passkey below are unchanged".
    A run that misses one of them is not this recipe.
  - The other hits are dispositioned by kind. Gate A history tables and logs (2b's, 215's,
    `brain/log.md`) are history. Other features' passkeys, tags and attorney patterns (`freeze_key`
    in 104 and brain `session.md`, 057's, 495's `owned_route_key`, `reify.hpp`'s) and other types'
    `begin()` are unrelated. `writer.hpp`'s passkey comment stays true. `brain/failure-classes.md`
    class 24 is below.

Re-run each recipe at implementation and disposition every hit.

- **Amend in place, as 100 did:**
  - `.specify/2b-wire.md`: §4.5 (the constructor); §6.6 and §8, where the writer-scratch row goes,
    leaving two arenas plus the validator's caller-supplied scratch; seam #13; §4.8, which gets an
    "as built since W1" line: `begin()` and `end()` without `[[clang::lifetimebound]]`, the
    iterator's `operator*` returning `kv` by value, and why (§1.4, the iterator contract); and §6.4,
    whose "called from every accessor" gets an "as built since W1" line naming the unknown-field
    view's and iterator's check sites (§1.4, the generation check);
  - 093's contract C-8 L-5 and C-3 I-5: the `unknown_fields()` clause;
  - 093's contract C-7 row 18 (`specs/093-inbound-frame-dispositions/contracts/`), the
    `unknown_fields()` row "returns an empty view on arena exhaustion": an "Amended (W1)" line says
    that since W1 it allocates nothing, so there is no exhaustion to report (R-W1-5 F3).
- **Flag in brain `components/wire.md`'s "Superseded records, flagged in place"**, frozen and not
  edited:
  - `specs/004-wire-codec/contracts/unknown_fields.hpp`, the shape oracle's `[[clang::lifetimebound]]`
    on `begin()` and `end()`;
  - `specs/004-wire-codec`: contracts `writer.hpp`, data-model (`Writer::scratch_mr`), plan, research
    D-3, tasks, and checklists `nfr.md` CHK003's writer-scratch arena;
  - 093's plan OD-18, as the record of the catch D-688-1 replaces;
  - 093's records of the value `kParseContainers` = 10 (plan, research, data-model, tasks, checklist
    `nfr.md`);
  - 081's texts that say the parser classifies through `field_valid_for`. Among them: FR-009
    (`spec.md`); its contract `validation-acceptance.md`, item 5 and the parser-containment
    invariant; `research.md`'s "Parser-unchanged" rationale; the data-model's load-bearing invariant;
    quickstart; tasks T006; and checklist `validation-correctness.md` CHK020. The recipe gives the full
    set. Since W1 the parser classifies by `tag_defined`. The invariant itself, classification
    identical with strict validation on or off, still holds (§1.4).
- **Code comments, amended in the PR:**
  - `table_view::tag_defined`'s comment, which reads "Cold: the validator asks only after a tag
    failed the message's set". It names both readers: the validator, as before, and
    `unknown_fields()`, which asks for every non-framing entry it walks (R-W1-5 F1);
  - the comment above `table_view::is_fixt_framing_tag`, which gives "the inbound parser's
    `unknown_fields()` classification" as a reason to keep the framing tags out of `valid_`. The
    reason narrows to the validator's own accessors;
  - `tests/dictionary/fixt_header_merge_test.cpp` T006's header: what it pins (`field_valid_for` false)
    stays, but not its reason;
  - `unknown_fields.hpp`'s summary, which becomes true as written. Its "borrowed span of (tag,value-slice)
    pairs the parser recorded" sentence goes;
  - `parser.hpp`'s comment on `unknown_fields()` ("not `field_valid_for`-known", "Builds and caches")
    and the deleted members' comment;
  - `tests/wire/unknown_fields_test.cpp`'s header ("unknown = not in dict for this msg_type"), the
    hand-built cell's comment ("The view borrows the…", the cell moves to the parser path), and the
    Q-32 block comment and cell name (§1.6 item 1). The header's first sentence, "ONLY
    dictionary-MISSING tags", becomes true and stays;
  - `reify_shared_table_concurrency_test.cpp`'s "The view caches its group slices and unknown fields
    after the first call", which is no longer true of unknown fields;
  - the Q-33 frame comment (§1.6 item 2);
  - `writer.hpp`'s design summary and constructor comment, `three_arena_pinning_test.cpp`'s model line,
    and `wire_alloc_guard_test.cpp`'s arena comment (§3);
  - `include/fixpp/dict/dictionary_snapshot.hpp`'s supersession header: its "the pairing and the
    passkey below are unchanged" names W1 (D-707-2) for the passkey's shape, as the project rule asks
    of a header whose decision is superseded;
  - the G1 comment in `tools/check_dictionary_snapshot_exclusivity.sh` ("`snapshot_key() = default;`
    is a DECLARATION"), which follows §2.4's filter.
- **brain `wire.md`.** The "Every admitted frame parses … `unknown_fields()` that cannot terminate"
  section gets a "⚠️ Superseded in part by W1" line: the catch is gone because nothing allocates. The
  locked-decisions entry for `[2b §4.8]` gets W1's predicate as built.
- **brain `components/dictionary.md`.** 215's entry, which says "The passkey and the provenance check
  stand", gains: the passkey's shape is superseded by W1 (D-707-2); the pairing, the sole minter and the
  provenance check stand. It names 215's §3 key declaration, its §5a `snapshot_key` row and §6 seam
  7's `G1_MINT`.
  - Its 101 entry ("a sorted `std::vector<std::uint16_t>`, binary-searched … the lookup is cold") is
    flagged in place, not edited: since W1 `unknown_fields()` asks `tag_defined` for every
    non-framing entry it walks, so the lookup is no longer cold for that reader. Whether the sorted
    vector still stands is decided by the bench result (§1.4), not by this note (R-W1-5 F1).
- **brain `failure-classes.md` class 24.** Its scanning heuristic becomes §2.2's recipe and condition.
  Its instance list gains `group_writer_token` (fixed) and `snapshot_key`, and the form gains the
  implicit-lifetime clause and the `make_shared` variant. **The edit is not made in this PR** (R-W1-5
  F4): it is proposed to the owner at close-out (row 15), as anti-pattern candidates always are, and
  lands only on that approval.

- **D-688-2 (v0.6).** Re-run §1.4a's comment recipe at implementation, and disposition every hit.
  - **Code and test comments:** the ones §1.4a lists under "Comments that change".
  - **Amend in place:** `.specify/2b-wire.md` §4.8's "as built since W1" line gains: "`MessageView<Index>`
    remembers where the first unknown entry is, so a later `unknown_fields()` call does not rescan the
    frame (D-688-2)". §6.6's "Threading" needs no edit: it already states the contract the index
    relies on.
  - **brain `components/wire.md`:** the locked-decisions row for `[2b §4.8]` ("as a filtered view")
    gains "the `MessageView` keeps one index, where the first unknown entry is (D-688-2)", and its
    "materialising a vector" rejection stays true: the index allocates nothing.
  - **The live B&L:** `B-688-1` and `B-707-1` (§4).
  - **Disclosed and filed, not edited here:** `group<>()` writes `set_group_context`'s members on
    the shared `kEmpty` of `owning_message_handle::view()` (§1.4a, the thread contract). It is
    fixpp#736 (batch B96).
  - **Disclosed, to be filed (the orchestrator files it): `OffsetTable::build` truncates the value
    offsets and lengths of a frame past 4 GiB, with no error.**
    - `OffsetTable::build` (`src/wire/offset_table.cpp`) narrows each entry's value offset and value
      length to `std::uint32_t` with a bare `static_cast` in its `entries_.emplace_back`. Nothing
      bounds the frame to fit: `frame_view`'s lengths and `Framer::Config::max_frame_bytes` are
      `std::size_t` (`include/fixpp/wire/framer.hpp`), and `OffsetTable::Config::max_offset_entries`
      is an unclamped `std::size_t` (`include/fixpp/wire/offset_table.hpp`).
    - So a frame longer than 4 GiB that reaches a direct C++ `Parser` gets a wrong offset for every
      value that starts at or past byte 2^32, and a wrong length for any value of 4 GiB or more, with
      no error. The frame's length alone decides it: a frame with a few fields, one of them a
      several-GiB value, stays far below the default `max_offset_entries`. Such a frame reaches the
      parser through a `Framer` whose `max_frame_bytes` is raised past its default, or through
      `frame_view_slice_access::make` (`include/fixpp/wire/framer.hpp`), a public seam that mints a
      `frame_view` without the Framer. A Data value cannot be the long one: `build` saturates a
      declared Data length at `0xFFFFFFFF`.
    - The overlay's slot value, `static_cast<std::uint32_t>(e) + 1U`, wraps at a second threshold, a
      table of more than 2^32 − 1 entries, which also needs `max_offset_entries` raised.
    - Re-derive the narrowings with
      `git grep -n -E 'static_cast<std::uint32_t>\((val_start|val_len|e)\)' -- src/wire/offset_table.cpp`.
    - Reach: the session's inbound path cannot reach it, because its frame limit is a
      `std::uint32_t` (`inbound_limit_for` in `src/session/inbound_limit.hpp`, applied by
      `inbound_framer_config` in `src/session/read_pump.hpp`). Other entries that parse a
      caller-supplied frame were not traced.
    - It predates W1 and is outside it. D-688-2 does not rely on it: its encoding checks
      representability itself (§1.4a, the encoding).
    - No fixpp issue covers it (searched 2026-11-28 with
      `gh issue list -R CatalinSerafimescu/fixpp --state all --search <terms>` for "4 GiB",
      "truncat offset_table", "uint32 OffsetTable", "value offset uint32" and "frame larger than 4").
      The dictionary's analogous 32-bit residuals are `L-102-1` (fixpp#676, fixpp#677).

## 7. Rulings applied (no open design question)

v0.1's Q-1 and Q-2 are answered. The rulings record (`decisions/speckit/pr-w1-688-707-559-owner-rulings.md`
in the parent) holds each one; each is an orchestrator ruling under the owner's 2026-11-27
delegation, flagged for the owner's review at merge.

- **R-W1-1 (Q-1):** `SessionConfig::message_arena` is split out as fixpp#735 (B31 / S8), and W1 closes
  #559 with site 1 (§3.6).
- **R-W1-2 (Q-2):** the `snapshot_key` fix is folded into W1, and the exclusivity gate's mint filter
  changes with it (§2.4).
- **R-W1-3 (Gate A round 1):** "unknown" is decided by `tag_defined(tag)`, not
  `field_valid_for(msg_type, tag)`. That widens #688 from "allocates" to "classifies". 081's texts are
  flagged and the comments amended (§6), and `B-688-1` records the changed set (§4).
- **R-W1-4 (Gate A round 1; amended twice in round 2):** §VIII.2 approval comes after the measurement,
  never before. The note names the shapes expected to regress and their instrument, and accepts
  nothing in advance. Each over-band row from the paired run is posted in the PR thread with its
  figures and the orchestrator's recommendation, and the owner's ratification there is a merge
  condition; "merge if green" does not cover it. If no row is over the band, no approval is needed. A
  regression past the band on the dispatch path (CI's paired rows) blocks the merge and goes to the
  owner (§1.4).
- **R-W1-5 (/analyze on v0.4):** applied in v0.5. B1: CI's paired `parser_bench` and `writer_bench`
  rows are read from the job output against +5%, and a shared row above it takes the PR-thread route
  (§1.4). C1: one shared implicit-lifetime guard header (§2.6 item 1). C2: the bench cases' host is
  `parser_bench`, with no manifest change (§1.4). E1: one MSVC target list (§2.6 item 4). F1: the
  `tag_defined` comment and brain `dictionary.md`'s rationale (§6). The LOWs A1, A2, B2, B3, C3, C4,
  C5, E2, F2 and F3 are applied where each names; F4: the class-24 edit is not in this PR (§6).
- **R-W1-6 (owner, 2026-11-28, "unknown_fields() - A"):** applied in v0.6 as D-688-2 (§1.4a). The
  `MessageView` remembers where the first unknown entry is, with no allocation. It ratifies no
  figure: the paired run is repeated in full and R-W1-4's PR-thread route applies to every over-band
  row. One correction to the ruling's text: its reason for an index, "a copied MessageView copies its
  OffsetTable", does not hold, because both copies are deleted. The index stands for the reasons
  §1.4a gives: a moved-from source, the padding, and a bounds check.

No technical question is left open. One owner act may be needed later, and it is conditional: the
PR-thread ratification of an over-band bench row, if the measurement produces one. `[2b §4.8]`, R-W1-3 and the correctness rule settle #688. The
measured compile matrix settles #707's shapes. The code settles #559. If the owner declines an
over-band row, the PR does not merge, and #688 returns to Gate A with Option A as the named
alternative (§1.4).

## Gate A

- v0.7: scoped Gate A CONVERGED round 5 (P1=0, P2=0; P3s applied) (2026-11-28): Codex P1=0 P2=0 P3=3, scoped to the D-688-2 delta; each P3's mechanical edit is applied, with no rewrite. Review: `research/G19-fix-fpml-iso20022/research/reviews/codex_688_707_559_5_wire_headroom_token_review.md` (path from the parent repository's root). What each finding changed is under "Round 5" below.

- v0.7: scoped round 4 (Codex codex_688_707_559_4…, Opus opus_688_707_559_4…) applied — post-judging P1=0 P2=2 P3=10; round 5 above re-checked it. Codex P1=1 P2=2 P3=3 (2026-11-28). Scoped to the D-688-2 delta; the converged rounds below are not reopened. Reviews: `research/G19-fix-fpml-iso20022/research/reviews/codex_688_707_559_4_wire_headroom_token_review.md`, `research/G19-fix-fpml-iso20022/research/reviews/opus_688_707_559_4_wire_headroom_token_triage.md` (paths from the parent repository's root). What each finding changed, and what was declined, is under "Round 4" below.

- v0.6: R-W1-6 delta (2026-11-28). The delta is D-688-2 (§1.4a) and the lines it flags in §1.4,
  §1.5, §1.6, §4, §5, §6 and §7, with §A.6. Its scoped Codex Gate A review is round 4 (v0.7).

- Round 1 applied 2026-11-27: Codex P1=3 P2=7 P3=2; Opus post-judging P1=1 P2=9 P3=7; rewrite addresses root causes #1 (the classification predicate is `field_valid_for` where `[2b §4.8]` specifies "not defined by the dictionary"), #2 (re-derive recipes that did not reproduce every item the note names), #3 (perf evidence planned through an instrument that cannot time it). Reviews: `research/G19-fix-fpml-iso20022/research/reviews/codex_688_707_559_1_wire_headroom_token_review.md`, `research/G19-fix-fpml-iso20022/research/reviews/opus_688_707_559_1_wire_headroom_token_triage.md` (paths from the parent repository's root).

- Round 2 applied 2026-11-27: Codex P1=1 P2=2 P3=1; Opus post-judging P1=1 P2=3 P3=6; rewrite addresses root causes #1 (an acceptance written ahead of its measurement and outside the PR thread), #2 (the by-value iterator is a public contract change whose attributes, witness and disclosure were not derived from the type), #3 (instruments not shown to be able to report non-zero). Reviews: `research/G19-fix-fpml-iso20022/research/reviews/codex_688_707_559_2_wire_headroom_token_review.md`, `research/G19-fix-fpml-iso20022/research/reviews/opus_688_707_559_2_wire_headroom_token_triage.md` (paths from the parent repository's root).

- Round 3 — converged 2026-11-27: Codex P1=0 P2=2 P3=1; Opus post-judging P1=0 P2=0 P3=9; no rewrite: a P3 pass applies each finding's mechanical edit (v0.4). Root cause #1: the supersession sweep was keyed on the decisions' topics, not on the declarations they change. Reviews: `research/G19-fix-fpml-iso20022/research/reviews/codex_688_707_559_1_wire_headroom_token_review.md`, `research/G19-fix-fpml-iso20022/research/reviews/opus_688_707_559_1_wire_headroom_token_triage.md`, `research/G19-fix-fpml-iso20022/research/reviews/codex_688_707_559_2_wire_headroom_token_review.md`, `research/G19-fix-fpml-iso20022/research/reviews/opus_688_707_559_2_wire_headroom_token_triage.md`, `research/G19-fix-fpml-iso20022/research/reviews/codex_688_707_559_3_wire_headroom_token_review.md`, `research/G19-fix-fpml-iso20022/research/reviews/opus_688_707_559_3_wire_headroom_token_triage.md` (paths from the parent repository's root).

### Round 5 (scoped to D-688-2) — how each finding was applied

| Finding | Where v0.7 answers it |
|---|---|
| Codex F-1 (P3) | Every claim that the first call over a non-empty table writes the index now says it stores the index when it is representable, and otherwise leaves the sentinel and rescans: the Decisions row, §1.4's withdrawn side-effect bullet, §1.4a's thread contract and the proposed `unknown_fields()` comment, §4's `B-688-1` amendment (its text, Cost and threading sentences), §5's concurrency row |
| Codex F-2 (P3) | §1.4's bench-case list: the `empty()` case's "becomes a whole-table scan" is marked D-688-1 history, and the case measures the removal of the rescan after the first call; the "shapes expected to regress" list points at §1.4a's "Expected bench effect" |
| Codex F-3 (P3) | §1.4a's P4 is §1.5's command, with the pathspec that excludes this note |

### Round 4 (scoped to D-688-2) — how each finding was applied

| Finding | Where v0.7 answers it |
|---|---|
| Codex F-1 (P1; P2 after judging) | §1.4a the encoding (the two `detail` predicates, both sides checked, their witnesses, what has no cell) and the width (why the store is bounded, two rejections); §6 the >4 GiB disclosure; §A.7 |
| Codex F-3 (P2) | §1.4a cell (b), the moved-from source: parent precondition, exit-code oracle, M4 and M5 re-stated |
| Codex F-2 (P2; P3 after judging) | §1.4a the layout (both specialisations, `alignof`, release and debug) and the shape (`no_first_unknown_t`); §A.6 |
| Codex F-4 (P3) | §1.4a "Every pin that could fire": P2 multiline, P5a to P5c, a known positive each, the rule; the holders (the C ABI's `owned_view_`, the generated `owning_<Msg>`) |
| Codex F-5 (P3, in part) | §A.6's prediction replaced by the pass condition; §1.4a "Where the room is" cites §A.6; §1.5; §4 `B-707-1`; the Decisions row |
| Codex F-6 (P3) | §5 the concurrency row |
| Opus O-1 (P3) | §1.4a the shape, the initializer; §A.7 |
| Opus O-2 (P3) | §1.4a the thread contract and §6: fixpp#736 |
| Opus O-3 (P3) | §1.4a cell (e): its own binary |
| Opus O-4 (P3) | §1.4a cell (d) |
| Opus O-5 (P3) | §1.4a "Comments that change", the new member's comment |
| Opus O-6 (P3) | §1.4a the C ABI |

One correction beyond the fix list: the layout's baseline was "the merge base", which is `b93b8a28`,
before D-688-1 deleted `unk_items_`. D-688-2's sizes are compared with the tree before D-688-2
(§A.6's before-run at `10e73842`).

### Round 4 — disagreements

- **Codex F-1, severity (Codex P1; Opus P2).** The session path cannot reach a table of 2^32 entries
  (`entry_cap_for` over a `std::uint32_t` limit), and a direct `Parser` reaches it only with a frame
  whose table already holds truncated offsets (§6). Neither of Codex's counter-proposals is taken: the
  index plus a `bool` still needs both checks and adds a member, and an entry-count invariant in
  `build` changes the table's rejection surface and is outside D-688-2 (§1.4a, the width).
- **Codex F-2, severity (Codex P2; Opus P3).** On clang and gcc a `[[no_unique_address]]` member of a
  new, distinct empty type adds no byte, so this is a missing measurement, not a defect. Rejected half:
  the `optional<MessageView<Index>>` probe (§1.4a, the recipe).
- **Codex F-3, the counter-proposals.** Neither a test-friend seam nor a resetting wrapper type is
  added: the exit-code oracle makes M5 deterministic with no undefined behaviour, and M4's undefined
  behaviour is claimed only on the lanes that diagnose it.
- **Codex F-5, rejected half.** §A.3 to §A.6 are dated run records (date, commit, toolchains), which
  are provenance, not claims that rot; they stay. Only the results written ahead of a measurement go.

### Round 3 — how each finding was applied

| Finding | Root cause | Where v0.4 answers it |
|---|---|---|
| Codex F-2 (P3 after judging) | #1 | §6: the declaration-keyed recipe and its known positives; `[2b §4.8]` and `[2b §6.4]` amended; 004's `unknown_fields.hpp` contract flagged; brain `dictionary.md`; `dictionary_snapshot.hpp`'s header and the G1 comment |
| Codex F-1 (P3 after judging) | — | §1.4 the generation check; §1.6 item 5 (the death cell) and item 6 (the mutant) |
| Codex F-3 (P3) | — | Status block |
| Opus NEW-1 (P3) | — | §4 `B-004-5` |
| Opus NEW-2 (P3) | — | §2.4 clang-tidy: `NOLINTNEXTLINE` on both constructors |
| Opus NEW-3 (P3) | — | §1.4 the layout control |
| Opus NEW-4 (P3) | — | §1.3 Option A, §1.4 "If the owner declines a row", §7 |
| Opus NEW-5 (P3) | — | §2.3 the moved-from temporary |
| Opus NEW-6 (P3) | — | §1.5 and §3.4: the size recipe in `B-104-7`'s shape, with its known positive |

### Round 3 — disagreements

- **Codex F-1 and F-2, severity only (Codex P2; Opus P3).** The facts are right, and v0.4 applies both
  fixes. For F-1, the base iterator already makes no check, so W1 did not regress, and the trap is a
  debug diagnostic. For F-2, a reader who restored a stale record's shape would be caught loudly: the
  `bit_cast` pins of §2.6 reject 215's defaulted key, and §A.4's `KEEP_LB` fails item 5's cell under
  `-Werror`.

### Round 2 — how each finding was applied

| Finding | Root cause | Where v0.3 answers it |
|---|---|---|
| Codex F-1 (P1) | #1 | Status block, §1.4 "What a slowdown needs", §7 R-W1-4: R-W1-4 as amended twice; over-band rows posted in the PR thread, owner ratification a merge condition |
| Codex F-3 (P2) | #2 | §1.4 iterator contract: the `same_as` pin, RED on the base; §1.6 items 5 and 6 (the cached-reference mutant); §A.4 |
| Opus NEW-1 (P2) | #2 | §1.4 iterator contract: `begin()`/`end()` drop `[[clang::lifetimebound]]`, with the trade and the rejected pragma; §1.6 items 5 and 6; `B-688-1`; §A.4 |
| Opus NEW-3 (P3) | #2 | §1.5 and `B-688-1`: `auto&`, `&*it`, a reference across `++`, the `kv const*` constructor |
| Opus NEW-2 (P2) | #3 | §1.6 item 7: the counter is the parse resource, read after `parse()`, with its known positive, which names the counter; the table gains defined tags |
| Root cause #3, applied beyond NEW-2 | #3 | §1.6 item 4: the wire cell's counter is the parse resource (RED on the base); the heap cell is not RED on the base and names its positive control and a `std::vector` mutant |
| Opus NEW-5 (P3) | #3 | §1.4: the condition, and the recipe with `\b` and its known positive |
| Opus NEW-6 (P3) | #3 | §2.6 items 1, 2 and 4: a per-lane `kHasTrait` cell that skips by name; §A.5 |
| Codex F-2 (P3 after judging) | — | §2.5 and `B-707-1`: the keys' type properties change; §A.4. Severity disagreement below |
| Codex F-4 (P3) | — | §1.4 "Cost": the proven change only; the net cost is measured |
| Opus NEW-4 (P3) | — | §4 `L-093-5`: retitled "one report", the old title quoted |

### Round 2 — disagreements

- **Codex F-2, severity only (Codex P2; Opus P3).** The facts are right, and v0.3 applies Codex's
  wording fix in §2.5 and `B-707-1`. What Opus disputes is the severity. No consumer can legally hold
  a value of either key, because nothing outside the friend can mint one. So only what a trait query
  on the type answers changes; no behaviour, layout or legal call does. It is a disclosure
  correction.

### Round 1 — how each finding was applied

| Finding | Root cause | Where v0.2 answers it |
|---|---|---|
| Codex F-1, Opus NEW-1, NEW-4 | #1 | R-W1-3; §1.1, §1.4 (predicate, cost), §1.6 items 2, 3 and 6, §4 `B-688-1`, §6 (081's texts and the comments) |
| Codex F-8 | #1 | §1.6 items 3 to 6: predicate, group-instance and duplicate cells, mutants; the global counter moves to `tests/alloc_guard/` |
| Codex F-5 | #2 | §1.5 (every pin of `kParseContainers`), §4 `B-093-7`, §6 |
| Codex F-6 | #2 | §2.2: the recipe is the union of the friend and name lists, and every hit is dispositioned, `group_ctx_key` and `generation_token` included |
| Codex F-9 (P3 after judging) | #2 | §3.1 recipe, §3.5, §6 |
| Codex F-4, Opus NEW-2 | #3 | §1.4: transplanted A-B-A-B bench; R-W1-4 in place of an open owner item |
| Codex F-2 | bookkeeping | §3.6, §7: R-W1-1 and R-W1-2 applied; Q-1 and Q-2 deleted |
| Codex F-3 (P2 after judging) | bookkeeping | §5: the controls apply through Appendix A's "Wire format / parser" row; each control's state is recorded |
| Codex F-7 | — | §2.4: three seeds, and a marker-selected comment-out step |
| Codex F-10 (P3 after judging) | — | §4 `B-707-1`, one layout row in `B-104-7`'s shape |
| Codex F-11 | — | Disagreement below. The citation is fixed in §1.5 and §3.2 |
| Codex F-12 | — | §1.2: `Message::setString` and `Session::next` |
| Opus NEW-3 | — | §2.6: known positives inside the guard, the row-2 mutant, the lanes where the trait exists (§A.3) |
| Opus NEW-5 | — | §2.6: the nested `defined(__has_builtin)` guard, a same-shape `bit_castable` positive, and the MSVC build as the guard's test |

**Found while rewriting (not in either review):**
- **Q-33's frame has no undefined tag.** Its filler tag 2 is AdvId, defined in `FIX44.xml`, so R-W1-3
  would empty its view on both lanes. §1.6 item 2 re-pins it.
- **The classify hook has a second reader**, a direct call in
  `reify_shared_table_concurrency_test.cpp`. That corrects the triage's "one reader". Its answer does
  not change (§1.4), and its stale comment is in §6.
- **`tag_defined` is true for every FIXT framing tag on a framing-surface table**, because
  `add_fixt_framing_tag` marks the tag defined. That includes 099's FIX 4.0 to 4.4 route tables. §1.4
  and `B-688-1` state it.

### Round 1 — disagreements

- **Codex F-2, severity only (Codex P1; Opus P3).** The facts are right, and v0.2 applies the fix:
  Q-1 and Q-2 are deleted and R-W1-1 and R-W1-2 are applied. What Opus disputes is the severity. The
  note asked both questions, and the rulings answering them were recorded a minute after the note was
  committed (the note's commit against the rulings record's commit in the parent). It was a ruling
  the note had not absorbed yet, not a design that contradicted a binding ruling.
- **Codex F-11, the counter-proposal (Codex P3, a shim or a new owner ruling).** Not applied. There is
  no shim, no `[[deprecated]]` overload and no new ruling. Removing public C++ API before the first
  release already has precedent in the live B&L (`B-098-4`, `B-652-1`), and Article IV §1 makes C++ the
  primary surface without a compatibility promise. What Opus did confirm is the citation defect: v0.1
  reasoned from `[const §X.7]`, which is C-ABI-only. §1.5 and §3.2 now cite the precedents instead.

---

---

## A. Probes (run 2026-11-27; clang 22.1.2 with libstdc++ 15, gcc 13.3.0 with libstdc++ 13; `-std=c++23 -fsyntax-only`)

### A.1 The fix shapes, standalone

```cpp
#include <array>
#include <bit>
#include <cstddef>
#include <expected>
#include <memory>
#include <type_traits>
template <class T>
concept bit_castable = requires { std::bit_cast<T>(std::array<std::byte, sizeof(T)>{}); };
static_assert(bit_castable<int>);

class Writer;
struct group_writer_token {
private:
    friend class Writer;
    group_writer_token() noexcept = default;
#if defined(TOKEN_FIX)
    group_writer_token(group_writer_token const&) noexcept {}
#endif
};
class group_writer {
public:
    explicit group_writer(group_writer_token, Writer* o) noexcept : owner_{o} {}
    group_writer(group_writer&&) noexcept = default;
    group_writer(group_writer const&) = delete;
private:
    Writer* owner_ = nullptr;
};
class Writer {
public:
    std::expected<group_writer, int> open_group() noexcept {
#if defined(VIA_PRVALUE)
        return std::expected<group_writer, int>{group_writer{group_writer_token{}, this}};
#else
        return std::expected<group_writer, int>{std::in_place, group_writer_token{}, this};
#endif
    }
};
class snap;
std::shared_ptr<const snap> make_snap();
class snapshot_key {
#if defined(SNAP_USER_DEFAULT)
    snapshot_key() noexcept {}
#else
    snapshot_key() = default;
#endif
    friend std::shared_ptr<const snap> make_snap();
#if defined(SNAP_COPY_PUBLIC)
public:
    snapshot_key(snapshot_key const&) noexcept {}
#elif defined(SNAP_COPY_PRIVATE)
    snapshot_key(snapshot_key const&) noexcept {}
#endif
};
class snap { public: snap(snapshot_key, int v) : v_{v} {} int v_; };
std::shared_ptr<const snap> make_snap() { return std::make_shared<const snap>(snapshot_key{}, 1); }
// per variant: static_assert(bit_castable<...>) on the base, !bit_castable / !is_trivially_copyable
// (and, on clang, !__builtin_is_implicit_lifetime) on the fixed shapes
int main() { Writer w; auto g = w.open_group(); (void)g; return make_snap()->v_ - 1; }
```

| Variant | clang 22 | gcc 13 |
|---|---|---|
| base: both keys `bit_cast`-able | compiles | compiles |
| `TOKEN_FIX`, `std::in_place` | **error**: no matching `expected` constructor | **error**: no matching `expected` constructor |
| `TOKEN_FIX` + `VIA_PRVALUE`: not trivially copyable, not copy-constructible, not `bit_cast`-able | compiles | compiles |
| `SNAP_COPY_PRIVATE` | **error**: `construct_at`, private copy constructor | **error**: same |
| `SNAP_COPY_PUBLIC` (defaulted default constructor) | compiles; `!bit_castable` holds; **`__builtin_is_implicit_lifetime` true** | compiles |
| `SNAP_COPY_PUBLIC` + `SNAP_USER_DEFAULT` | compiles; `!bit_castable`, not default-constructible, **not implicit-lifetime** | compiles |
| 104's shape (defaulted private default + user-provided private copy) | implicit-lifetime, not copy-constructible | not copy-constructible |

### A.2 The census TU, against the real headers at `b93b8a28` (`clang++ -std=c++23 -fsyntax-only -I include`)

```cpp
#include <array>
#include <bit>
#include <cstddef>
#include <type_traits>
#include <fixpp/wire/writer.hpp>
#include <fixpp/wire/framer.hpp>
#include <fixpp/wire/field_view.hpp>
#include <fixpp/dict/dictionary_snapshot.hpp>
template <class T>
concept bit_castable = requires { std::bit_cast<T>(std::array<std::byte, sizeof(T)>{}); };
static_assert(bit_castable<int>, "known positive");
#define REPORT(T) \
    static_assert(!bit_castable<T>, #T " is bit_castable"); \
    static_assert(!std::is_trivially_copyable_v<T>, #T " is trivially copyable"); \
    static_assert(!__builtin_is_implicit_lifetime(T), #T " is implicit-lifetime");
REPORT(fixpp::wire::group_writer_token)
REPORT(fixpp::dict::detail::snapshot_key)
REPORT(fixpp::wire::frame_view)
REPORT(fixpp::wire::field_view)
int main() {}
```

Result: all three asserts fire for each of the four types. `frame_view` and `field_view` are
dispositioned in §2.2 (flyweights with public minting seams).

The PR's version of this TU keeps only `group_writer_token`. `SeqnumManager::freeze_key`'s pins stay
in 104's `tests/session/test_104_reset_on_logout.cpp` (§2.2). `snapshot_key`'s pins go in `tests/dictionary/dictionary_snapshot_test.cpp`,
not here. G1's name census (`G1_ALLOW` in the exclusivity gate) admits that one test file, and naming
the key in any other test fails it.

### A.3 The implicit-lifetime guard and its known positives (run 2026-11-27; `-std=c++23`, compile and run)

The source is the guard of §2.6 and four replicas. Each replica is an empty class with one friend:
- `base_key` is the base shape of both keys: a defaulted private default constructor, implicit copy;
- `row2_key` is row 2 of §2.4's table;
- `row3_key` is the chosen `snapshot_key`;
- `gwt_key` is the chosen `group_writer_token`.

`main` returns 0 when the guard found the trait and 1 when it left the asserts out.

```cpp
#include <array>
#include <bit>
#include <cstddef>
#include <type_traits>
#include <version>
template <class T>
concept bit_castable = requires { std::bit_cast<T>(std::array<std::byte, sizeof(T)>{}); };
static_assert(bit_castable<int>);
#if defined(__has_builtin)
#if __has_builtin(__builtin_is_implicit_lifetime)
#define W1_IMPLICIT_LIFETIME(T) __builtin_is_implicit_lifetime(T)
#endif
#endif
#if !defined(W1_IMPLICIT_LIFETIME) && defined(__cpp_lib_is_implicit_lifetime)
#define W1_IMPLICIT_LIFETIME(T) std::is_implicit_lifetime_v<T>
#endif
class F;
class base_key { base_key() = default; friend class F; };
class row2_key { row2_key() = default; friend class F; public: row2_key(row2_key const&) noexcept {} };
class row3_key { row3_key() noexcept {} friend class F; public: row3_key(row3_key const&) noexcept {} };
class gwt_key { gwt_key() noexcept = default; gwt_key(gwt_key const&) noexcept {} friend class F; };
static_assert(bit_castable<base_key>, "same-shape positive");
static_assert(!bit_castable<row2_key> && !bit_castable<row3_key> && !bit_castable<gwt_key>);
static_assert(std::is_copy_constructible_v<base_key> && !std::is_copy_constructible_v<gwt_key>);
#if defined(W1_IMPLICIT_LIFETIME)
static_assert(W1_IMPLICIT_LIFETIME(int), "known positive");
static_assert(W1_IMPLICIT_LIFETIME(base_key), "same-shape positive");
#ifdef EXPECT_ROW2_RED
static_assert(!W1_IMPLICIT_LIFETIME(row2_key), "row-2 mutant");
#endif
static_assert(!W1_IMPLICIT_LIFETIME(row3_key));
#ifdef EXPECT_GWT_RED
static_assert(!(W1_IMPLICIT_LIFETIME(base_key) && std::is_copy_constructible_v<base_key>));
#endif
static_assert(!(W1_IMPLICIT_LIFETIME(gwt_key) && std::is_copy_constructible_v<gwt_key>));
constexpr bool kHasTrait = true;
#else
constexpr bool kHasTrait = false;
#endif
int main() { return kHasTrait ? 0 : 1; }
```

| Compiler | plain | `-DEXPECT_ROW2_RED` | `-DEXPECT_GWT_RED` |
|---|---|---|---|
| clang 22.1.2 | compiles; trait found (exit 0) | **error**: the row-2 assert fires | **error**: the base-shape conjunction assert fires |
| clang 18 | compiles; trait not found (exit 1) | compiles: the assert is left out | not run |
| gcc 13.3, 14, 15 | compiles; trait not found (exit 1) | compiles: the assert is left out | not run |

So the decisive row-2 pin is evaluated only where the trait exists. MSVC is not probed here; the
per-lane trait cell of §2.6 item 1 records what the guard finds there.

### A.4 The iterator contract (run 2026-11-27; `-std=c++23 -fsyntax-only -Werror`)

**Against the real headers at `b93b8a28`** (`-Iinclude`; gcc adds `-Wall -Wextra
-Wno-attributes=clang::lifetimebound`, the form `cmake/Helpers.cmake` applies to GNU C++):

```cpp
#include <fixpp/wire/parser.hpp>
#include <fixpp/wire/unknown_fields.hpp>
#include <concepts>
#include <utility>
int f(fixpp::wire::MessageView<fixpp::wire::access_mode::Index> const& mv) {
    auto it = mv.unknown_fields().begin();
    auto e = mv.unknown_fields().end();
    int n = 0;
    for (; !(it == e); ++it) { n += (*it).tag; }
    return n;
}
#ifdef AUTOREF
int g(fixpp::wire::MessageView<fixpp::wire::access_mode::Index> const& mv) {
    int n = 0; for (auto& x : mv.unknown_fields()) n += x.tag; return n; }
#endif
#ifdef SAMEAS
using fixpp::wire::unknown_fields_view;
static_assert(std::same_as<decltype(*std::declval<unknown_fields_view::iterator const&>()),
                           unknown_fields_view::kv>, "operator* returns kv by value");
#endif
```

| Variant | clang 22.1.2 | gcc 13, 14, 15 |
|---|---|---|
| plain | **error**: `-Wdangling` on `it` and on `e` (the attribute on `begin()`/`end()`) | compiles |
| `-DSAMEAS` | **error**: the `same_as` assert fires (and the `-Wdangling` pair) | **error**: the `same_as` assert fires |
| `-DAUTOREF` | `g` compiles (only `f`'s `-Wdangling` pair) | compiles |

Clang 18 is not run against the real headers: with its default library it has no `std::expected`.

**The key traits on the base** (clang 22, real headers): `static_assert`s that
`is_trivially_copyable_v` and `is_copy_constructible_v` hold for `group_writer_token` and
`dict::detail::snapshot_key` compile; the control, `is_trivially_copyable_v<fixpp::wire::group_writer>`,
fires (a user-provided destructor), so the probe can report a failure.

**A replica of the D-688-1 shape** (iterator holding its context by value, `MV::unknown_fields()`
carrying `[[clang::lifetimebound]]`; the same flags as above):

```cpp
#include <concepts>
#include <cstdint>
#include <utility>
struct kv { std::uint16_t tag; };
struct entry { std::uint16_t tag; };
struct ufv {
  entry const* b_ = nullptr; entry const* e_ = nullptr;
  struct iterator {
    entry const* p_; entry const* e_;
#ifdef CACHED_REF
    kv cur_{};
    kv const& operator*() const noexcept { return cur_; }
#else
    kv operator*() const noexcept { return kv{p_->tag}; }
#endif
    iterator& operator++() noexcept { ++p_; return *this; }
    bool operator==(iterator const& o) const noexcept { return p_ == o.p_; }
  };
#ifdef KEEP_LB
  iterator begin() const noexcept [[clang::lifetimebound]] { return {b_, e_}; }
  iterator end() const noexcept [[clang::lifetimebound]] { return {e_, e_}; }
#else
  iterator begin() const noexcept { return {b_, e_}; }
  iterator end() const noexcept { return {e_, e_}; }
#endif
  bool empty() const noexcept { return b_ == e_; }
};
struct MV { entry es[4]{}; ufv unknown_fields() const noexcept [[clang::lifetimebound]] { return {es, es + 4}; } };
MV make_mv();
int f(MV const& mv) {
  auto it = mv.unknown_fields().begin();
  auto e = mv.unknown_fields().end();
  int n = 0; for (; !(it == e); ++it) n += (*it).tag; return n;
}
#ifdef TMP_VIEW
int h() { auto v = make_mv().unknown_fields(); return v.empty(); }
#endif
#ifdef TMP_MV_ITER
int k() { auto it = make_mv().unknown_fields().begin(); return (*it).tag; }
#endif
#ifdef AUTOREF
int g(MV const& mv) { int n = 0; for (auto& x : mv.unknown_fields()) n += x.tag; return n; }
#endif
static_assert(std::same_as<decltype(*std::declval<ufv::iterator const&>()), kv>, "operator* returns kv by value");
```

| Variant | clang 22.1.2 and clang 18 | gcc 13, 14, 15 |
|---|---|---|
| plain (attribute dropped) | compiles | compiles |
| `-DKEEP_LB` | **error**: `-Wdangling` on `it` and `e` in `f`, the valid idiom | compiles |
| `-DKEEP_LB -DTMP_MV_ITER` (clang 22) | **error**: the same pair, and `k`'s `it` | not run |
| `-DCACHED_REF` | **error**: the `same_as` assert fires | **error**: the same |
| `-DTMP_VIEW` | **error**: `-Wdangling` on `v`, from `unknown_fields()`'s own attribute | compiles |
| `-DTMP_MV_ITER` | compiles: the dangle through `begin()` is not diagnosed (the cost) | compiles |
| `-DAUTOREF` | **error**: a non-const lvalue reference cannot bind a temporary `kv` | **error**: the same |

### A.5 The guard's nested shape where the name is not a macro (run 2026-11-27; compile and run)

`W1_NOT_A_MACRO` stands in for `__has_builtin` on a preprocessor that does not define it. It is a
stand-in for the preprocessor rule, not a probe of MSVC: `#undef __has_builtin` is no stand-in,
because clang 22 and gcc 15 keep answering through it.

```cpp
#ifdef ONE_LEVEL
#if W1_NOT_A_MACRO(__builtin_is_implicit_lifetime)
#define W1_IL 1
#endif
#else
#if defined(W1_NOT_A_MACRO)
#if W1_NOT_A_MACRO(__builtin_is_implicit_lifetime)
#define W1_IL 1
#endif
#endif
#endif
#if defined(W1_IL)
constexpr bool kHasTrait = true;
#else
constexpr bool kHasTrait = false;
#endif
int main() { return kHasTrait ? 0 : 1; }
```

| Variant | clang 22.1.2 | gcc 15 |
|---|---|---|
| nested (the guard of §2.6) | compiles; exit 1 (trait not found) | compiles; exit 1 |
| `-DONE_LEVEL` | **error**: "function-like macro 'W1_NOT_A_MACRO' is not defined" | **error**: "missing binary operator before token '('" |

### A.6 `MessageView`'s layout before D-688-2 (run 2026-11-28; clang 22.1.2 with libstdc++ 15, gcc 13.3.0 with libstdc++ 13; `-std=c++23 -fsyntax-only -I include`)

The Index release and debug rows were read at `10e73842`. The Iter, gcc-Iter and `alignof` rows were
read the same day at `e0b00e52`, whose `src/`, `include/`, `tests/` and `tools/` equal
`10e73842`'s (`git diff --stat 10e73842 e0b00e52 -- src include tests tools` prints nothing).

The TU:

```cpp
#include <fixpp/wire/parser.hpp>
using fixpp::wire::MessageView; using fixpp::wire::access_mode;
std::size_t a = sizeof(MessageView<access_mode::Index>);
std::size_t b = sizeof(MessageView<access_mode::Iter>);
std::size_t c = alignof(MessageView<access_mode::Index>);
std::size_t d = alignof(MessageView<access_mode::Iter>);
#ifdef GCC_SIZES
template <unsigned long, unsigned long, unsigned long, unsigned long> struct S;
S<sizeof(MessageView<access_mode::Index>), alignof(MessageView<access_mode::Index>),
  sizeof(MessageView<access_mode::Iter>), alignof(MessageView<access_mode::Iter>)> s;
#endif
```

On clang, `-Xclang -fdump-record-layouts-complete` prints each record. On gcc, `-DGCC_SIZES` makes
the incomplete-type error print `S<…>`.

| | release (`-DNDEBUG`) | debug |
|---|---|---|
| `sizeof` / `alignof` of `MessageView<Index>`, clang and gcc | 352 / 8 | 368 / 8 |
| `resolved_application_version_` (1 byte), clang | offset 336 | offset 352 |
| `dict_owner_` (a pointer), clang | offset 344 | offset 360 |
| `sizeof` / `alignof` of `MessageView<Iter>`, clang and gcc | 32 / 8 | 40 / 8 |
| Iter's Index-only members (`table_`, `resolved_application_version_`, `dict_owner_`), clang | empty, at offset 0 | empty, at offset 0 |

**The after-run passes when**, on clang and gcc, in release and in debug, `first_unknown_`'s offset
lies between `resolved_application_version_`'s and `dict_owner_`'s, and both modes' `sizeof` and
`alignof` equal this before-run's. MSVC was not run here (§1.4a, the layout).

**The after-run (run 2026-11-28, at `f84caed2`, D-688-2 implemented; the same TU and toolchains; MSVC 14.44.35207 with `/d1reportSingleClassLayoutMessageView`, `-std:c++latest`, debug `/MDd /D_DEBUG` and release `/MD /O2 /DNDEBUG`; the before columns re-read in the same session from `git archive 10e73842 include`).** Raw output: `/mnt/wsl/fixppbuild/evidence/w1-688-707-559/d688-2/` (`layout-*`, `msvc/layout-*`).

| | before (`10e73842`) | after |
|---|---|---|
| clang release, Index: `sizeof` / `align`; `resolved_application_version_`, `first_unknown_`, `dict_owner_` | 352 / 8; 336, none, 344 | 352 / 8; 336, 340, 344 |
| clang debug, Index | 368 / 8; 352, none, 360 | 368 / 8; 352, 356, 360 |
| clang release / debug, Iter: `sizeof` / `align` | 32 / 8, 40 / 8 | 32 / 8, 40 / 8 (`first_unknown_` empty, offset 0) |
| gcc release / debug, `S<Index size, align, Iter size, align>` | `S<352, 8, 32, 8>`, `S<368, 8, 40, 8>` | the same |
| MSVC release, Index / Iter `size` | 352 / 48 | 352 / 48 (`first_unknown_` at 340; Iter's empty member a byte of the tail padding) |
| MSVC debug, Index / Iter `size` | 400 / 56 | 400 / 56 (`first_unknown_` at 388) |

The pass condition above holds on clang and gcc, and on MSVC for `size` (the switch reports no alignment). The M5 mutant (an `entry const*` in place of the index), read by the same recipe, gives Index 360 / 376 on clang and gcc, release / debug.

### A.7 The encoding's predicates and the member's initializer, standalone (run 2026-11-28; clang 22.1.2, gcc 13.3.0; `-std=c++23 -fsyntax-only`)

A mock of the design's pieces, not the real header. Each `-D` switch is a mutant or a rejected form.

```cpp
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
namespace detail {
inline constexpr std::uint32_t kFirstUnknownNotComputed = std::numeric_limits<std::uint32_t>::max();
[[nodiscard]] constexpr bool first_unknown_is_set(std::uint32_t v, std::size_t size) noexcept {
#ifdef MUT_NE
    return v <= size;                                   // the != half dropped
#else
    return v != kFirstUnknownNotComputed && v <= size;
#endif
}
[[nodiscard]] constexpr bool first_unknown_fits(std::size_t idx) noexcept {
#ifdef MUT_FIT
    (void)idx; return true;                             // the store bound dropped
#else
    return idx < kFirstUnknownNotComputed;
#endif
}
static_assert(!first_unknown_is_set(kFirstUnknownNotComputed, std::size_t{kFirstUnknownNotComputed} + 1U));
static_assert(!first_unknown_fits(kFirstUnknownNotComputed));
static_assert(first_unknown_fits(kFirstUnknownNotComputed - 1U));
static_assert(first_unknown_is_set(3, 3));
static_assert(!first_unknown_is_set(4, 3));
}
enum class access_mode { Iter, Index };
template <access_mode Mode> struct MV {
    struct no_resolved_version_t {};
    struct no_first_unknown_t {};
    struct no_dict_owner_t {};
    using first_unknown_t = std::conditional_t<Mode == access_mode::Index, std::uint32_t, no_first_unknown_t>;
    static constexpr first_unknown_t not_computed() noexcept {
        if constexpr (Mode == access_mode::Index) { return detail::kFirstUnknownNotComputed; }
        else { return no_first_unknown_t{}; }
    }
    void* hooks_ = nullptr;
    [[no_unique_address]] std::conditional_t<Mode == access_mode::Index, std::uint8_t, no_resolved_version_t>
        resolved_application_version_{};
#ifdef BAD_INIT        // the uint32_t initializer, instantiated for Iter too
    [[no_unique_address]] mutable first_unknown_t first_unknown_{std::numeric_limits<std::uint32_t>::max()};
#elif defined(SAME_TYPE)  // Iter's member reuses no_resolved_version_t
    [[no_unique_address]] mutable std::conditional_t<Mode == access_mode::Index, std::uint32_t, no_resolved_version_t>
        first_unknown_{};
#else
    [[no_unique_address]] mutable first_unknown_t first_unknown_{not_computed()};
#endif
    [[no_unique_address]] std::conditional_t<Mode == access_mode::Index, void const*, no_dict_owner_t> dict_owner_{};
};
MV<access_mode::Index> a; MV<access_mode::Iter> b;
static_assert(sizeof(MV<access_mode::Index>) == 24);   // the uint32_t sits in the gap before dict_owner_
static_assert(sizeof(MV<access_mode::Iter>) == 8);     // the empty members add no byte
```

With no switch it compiles on both compilers. Each of `-DMUT_NE` (the first `static_assert`),
`-DMUT_FIT` (the second), `-DBAD_INIT` (clang "excess elements in struct initializer", gcc "too many
initializers") and `-DSAME_TYPE` (the Iter size assertion) fails on both. A 32-bit build was not
run: `g++ -m32` has no multilib here.
