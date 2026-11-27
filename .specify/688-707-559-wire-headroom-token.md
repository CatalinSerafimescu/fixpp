# fixpp#688, #707, #559 site 1 — the wire parser's unknown-field view, the writer's passkey and its scratch resource (lane W1 = B103 + B112)

> **Status: v0.3, issue-mode design note, 2026-11-27. Gate A round 2 applied (round 1 before it);
> round 2 review pending.** No code and no test is written yet. Branch
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
  - `src/` has none, and the C ABI has none (`git grep -n -i unknown -- include/fix src/capi`):
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
| A | A status on `unknown_fields_view` (`read_failed()`, as `group_view` has), set on exhaustion. | **Fallback**, if Gate A rejects C. The application learns the list failed but still cannot read it, where QuickFIX would have given it every field. It keeps the materialised list `[2b §4.8]` rejects. |
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

- **The view holds its context by value:**
  - the remaining entries, as a pointer pair into the table's `entries()` storage;
  - the frame's base pointer;
  - the classify function and its opaque dictionary, copied out of the view's `dict_hooks`;
  - `msg_type()`, as a `string_view`;
  - the generation token.

  Nothing in it points at the `MessageView` object itself. A moved-from `MessageView` keeps its arena
  storage (the defaulted move carries the `pmr::vector` buffers), so a view outlives a move exactly as
  far as today's view does.
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
- **The first unknown entry is found when the view is built**, so `begin()` and `empty()` are O(1)
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
- **Side effect, stated as a property.** `unknown_fields()` no longer writes any member, so the
  `mutable` state it shared across `const` calls is gone. Threads calling it on one const view no
  longer write shared state. This note claims nothing wider about sharing a `MessageView` across
  threads.
- **The public constructor `unknown_fields_view(std::span<kv const>, detail::generation_token)` goes.**
  It is not in the shape oracle. Its only user builds a view by hand
  (`tests/wire/unknown_fields_test.cpp`, the iterator-order cell); that cell moves to the parser path.
  `kv` stays a public nested type.

**Cost** (`[const §VIII.3]`, a perf change, with a bench in the same PR):
- Today the first call classifies every entry once and stores the result; later calls are O(1).
- After D-688-1, building the view scans as far as the first unknown entry, and every full iteration
  classifies each remaining entry again. A frame with no unknown fields is scanned whole on each call.
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
    call and becomes a whole-table scan.
- **How the new cases are timed.** CI cannot time them against the base:
  - `tools/bench_compare.py` compares only the benchmark names both trees have;
  - `[const §VIII.2a]` excludes a candidate-only row from timing for that PR;
  - the base has no such case.

  The new cases use only `unknown_fields()`, `begin()`, `end()`, `empty()` and a range-`for` that
  binds `auto const&`, and all of these compile on both trees. So the PR's bench source is copied into
  a base worktree and both trees run A-B-A-B (min per tree), locally. The evidence file records the
  command and the numbers. The rows both trees share stay CI's paired comparison, as usual.
- **What a slowdown needs (R-W1-4 as amended twice).** Nothing is accepted in advance.
  - **The shapes expected to regress:** repeated `unknown_fields()` calls and repeated iteration of
    one view, and `unknown_fields().empty()` on a frame with no unknown fields. The single-pass arm is
    reported beside them, so the report shows both sides.
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
  - **An extra hard rule:** a regression past the band on the per-message dispatch path, which is
    CI's paired rows, blocks the merge and goes to the owner.

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
  - Size pins: `git grep -n -E 'sizeof\((fixpp::)?(wire::)?(MessageView|unknown_fields_view)' -- .`
    finds design text only. The codegen flyweight pin is `sizeof(Msg) == sizeof(MessageView const*)`,
    a pointer, so it is untouched.
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
   - A FIX 5.0 cell: load `dictionaries/FIX50SP2.xml`, parse a NewOrderSingle with the standard header
     and one undefined tag, and assert that only the undefined tag is yielded. Run it on the base first
     and record the RED. Until that run, the RED is a reading, not a measurement.
   - `DictBoundUnknownSplit` and `EmptyDictAllTagsUnknown` stay green. A hand-built table defines every
     tag it is told anything about (the comment on `table_view::mark_defined`), so 35 and 34 are
     defined and 5000 is not.
4. **Zero allocation.** Each instrument names what makes it report non-zero.
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
   - New: a view iterated after its `Parser` is destroyed, with the dictionary alive, still classifies.
     This extends the existing post-`Parser`-destroy cell from the first call to iteration.
6. **Mutants.**
   - Reintroduce a materialised list over the parse resource: cell 1's zero-upstream assertion goes RED.
   - Drop the classification (yield every entry): `DictBoundUnknownSplit` goes RED.
   - Drop the framing exemption: the framing-tag cell goes RED.
   - `tag_defined` back to `field_valid_for`: the predicate cells of item 3 go RED.
   - Skip group-contained entries, and deduplicate equal tags: item 5's group and duplicate cells go
     RED.
   - `operator*` returns `kv const&` from a `kv` cached in the iterator: the `same_as` assert goes
     RED (§A.4, the `CACHED_REF` variant, on clang and gcc).
   - Restore `[[clang::lifetimebound]]` on `begin()` and `end()`: item 5's temporary-view cell
     stops compiling on clang (§A.4, the `KEEP_LB` variant).
7. **Fuzz** (`[const §VII.7]`, parser-adjacent). `tests/fuzz/fuzz_wire_parser.cpp` gains a property
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
| `SeqnumManager::freeze_key` | A passkey, fixed by 104. The trait TU keeps it as the shape's regression row. |
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
- **`SeqnumManager::freeze_key` was fixed by 104.** The trait TU keeps it as the shape's regression row.

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
4. **MSVC.** Build `windows-msvc-debug` with `-k 0` on the touched targets (the wire and dictionary
   buckets, `fixpp_wire`, `fixpp_dict`), and run the per-lane trait cell of item 1. MSVC's
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
  `git grep -n -E 'sizeof\((fixpp::)?(wire::)?Writer' -- .` finds none.

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
| the parser | **yes** | D-688-1 (`MessageView::unknown_fields`) |
| wire format or codegen layout | no | Bytes on the wire do not change, and the emitted flyweights hold a pointer. |
| concurrency, session FSM, store, security | no | D-688-1 removes a `mutable` write; it adds no synchronisation. |
| a new `.specify/` design document | **yes** | this note |

**The four Appendix A controls apply, with no reading of `[const §X.6]` needed.**
- Appendix A's "Wire format / parser" row ("offset-table semantics, framing rules, validator changes")
  fires on D-688-1, as this table's parser row says.
- Their state, in the shape of `.specify/563-564-callback-send.md` §0a:
  - **Codex Gate A:** this loop. Round 1 is applied in v0.2 and round 2 in v0.3 ("Gate A").
  - **`/clarify`:** by hand. The rulings record is its record (R-W1-1 to R-W1-4).
  - **`/analyze`:** by the spec-analyzer over the converged note, after Gate A and before
    implementation. Pending.
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

Re-run each recipe at implementation and disposition every hit.

- **Amend in place, as 100 did:**
  - `.specify/2b-wire.md`: §4.5 (the constructor); §6.6 and §8, where the writer-scratch row goes,
    leaving two arenas plus the validator's caller-supplied scratch; seam #13; and §4.8, which gets an
    "as built since W1" line (the design itself is unchanged);
  - 093's contract C-8 L-5 and C-3 I-5: the `unknown_fields()` clause.
- **Flag in brain `components/wire.md`'s "Superseded records, flagged in place"**, frozen and not
  edited:
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
    and `wire_alloc_guard_test.cpp`'s arena comment (§3).
- **brain `wire.md`.** The "Every admitted frame parses … `unknown_fields()` that cannot terminate"
  section gets a "⚠️ Superseded in part by W1" line: the catch is gone because nothing allocates. The
  locked-decisions entry for `[2b §4.8]` gets W1's predicate as built.
- **brain `failure-classes.md` class 24.** Its scanning heuristic becomes §2.2's recipe and condition.
  Its instance list gains `group_writer_token` (fixed) and `snapshot_key`, and the form gains the
  implicit-lifetime clause and the `make_shared` variant. That edit is the owner-approved form of
  104's anti-pattern candidate (close-out row 15); it lands only on that approval.

## 7. Rulings applied (no open design question)

v0.1's Q-1 and Q-2 are answered. The rulings record (`decisions/speckit/pr-w1-688-707-559-owner-rulings.md`
in the parent) holds each one; all four are orchestrator rulings under the owner's 2026-11-27
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

No technical question is left open. One owner act may be needed later, and it is conditional: the
PR-thread ratification of an over-band bench row, if the measurement produces one. `[2b §4.8]`, R-W1-3 and the correctness rule settle #688. The
measured compile matrix settles #707's shapes. The code settles #559.

## Gate A

- Round 1 applied 2026-11-27: Codex P1=3 P2=7 P3=2; Opus post-judging P1=1 P2=9 P3=7; rewrite addresses root causes #1 (the classification predicate is `field_valid_for` where `[2b §4.8]` specifies "not defined by the dictionary"), #2 (re-derive recipes that did not reproduce every item the note names), #3 (perf evidence planned through an instrument that cannot time it). Reviews: `research/G19-fix-fpml-iso20022/research/reviews/codex_688_707_559_1_wire_headroom_token_review.md`, `research/G19-fix-fpml-iso20022/research/reviews/opus_688_707_559_1_wire_headroom_token_triage.md` (paths from the parent repository's root).

- Round 2 applied 2026-11-27: Codex P1=1 P2=2 P3=1; Opus post-judging P1=1 P2=3 P3=6; rewrite addresses root causes #1 (an acceptance written ahead of its measurement and outside the PR thread), #2 (the by-value iterator is a public contract change whose attributes, witness and disclosure were not derived from the type), #3 (instruments not shown to be able to report non-zero). Reviews: `research/G19-fix-fpml-iso20022/research/reviews/codex_688_707_559_2_wire_headroom_token_review.md`, `research/G19-fix-fpml-iso20022/research/reviews/opus_688_707_559_2_wire_headroom_token_triage.md` (paths from the parent repository's root).

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

The PR's version of this TU keeps only the passkeys, with `SeqnumManager::freeze_key` added as the
shape's regression row. `snapshot_key`'s pins go in `tests/dictionary/dictionary_snapshot_test.cpp`,
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
