# fixpp#688, #707, #559 site 1 — the wire parser's unknown-field view, the writer's passkey and its scratch resource (lane W1 = B103 + B112)

> **Status: v0.1, issue-mode design note, 2026-11-27. Gate A pending.** No code and no test is
> written yet. Branch `wire-688-707-559-headroom-token`, cut from `origin/main` at `b93b8a28`.
>
> **Rulings this note applies** (owner, 2026-11-27): the official FIX documents decide first; then
> QuickFIX's behaviour where copying it has no downside, with QuickFIX C++ as the drop-in target;
> correctness before everything else; every deviation is recorded in the B&L.
>
> *Read* means read from source at `b93b8a28`; *run* names a command or a compile a reader can
> repeat. The probes are inlined in §A so Gate A can re-run them. Claims about code name a function,
> member or file, never a line number. Re-derive any enumeration with the command next to it.

**Decisions.**

| Id | Issue | Decision |
|---|---|---|
| D-688-1 | #688 | `unknown_fields()` becomes the filtered view `[2b §4.8]` specifies: it walks the offset table and allocates nothing, so there is no exhaustion to report. |
| D-707-1 | #707 | `group_writer_token` gets 104's shape (a user-provided private copy constructor). `Writer::open_group` builds the `group_writer` itself, because `std::in_place` copies the key outside `Writer`. |
| D-707-2 | #707 | The census the issue asks for finds `dict::detail::snapshot_key` of the same shape. It gets a user-provided private default constructor and a user-provided **public** copy constructor, because `std::make_shared` copies it. |
| D-559-1 | #559 site 1 | Delete `Writer::scratch_mr_` and the constructor's `scratch_mr` parameter. The writer keeps no group bookkeeping, so there is nothing to put on it. |

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

### 1.2 FIX documents and QuickFIX

- **FIX documents.** The FIX documents define no engine API. What they decide about an undefined tag
  (Reject with SessionRejectReason 0, or acceptance as a user-defined field) belongs to the validator
  (`[2b §6.5]` rule 5, 101's `wire_undefined_tag`), and nothing in W1 changes it.
- **QuickFIX.** None of the three engines has an unknown-field accessor. Each builds the whole field map
  before the application sees the message:
  - C++: `Message::setString` appends every field into `FieldMap`, whose storage is a
    `std::vector` under `ALLOCATOR`. `Session::verify` catches `std::exception` and disconnects.
  - J: `AbstractIoHandler` parses the message into `FieldMap`'s `TreeMap`.
  - Go: `doParsing` sizes `msg.fields` from the SOH count in one `make`.
- So in all three, an application that has a message has every field of it. A field list the engine
  could not build surfaces as an exception or a disconnect, never as an empty list.
- The QuickFIX observable is therefore *completeness*, not a status bit, and that is what D-688-1
  delivers. QuickFIX does not split known fields from unknown ones; that split is fixpp's own surface
  and is not a deviation.

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
- **The iterator holds the same context by value**, so `auto it = mv.unknown_fields().begin();` does
  not dangle on the temporary view. That matches today, where the iterator points into the
  `MessageView`'s vector. `operator*` returns `kv` **by value**.
  - Rejected: keeping `kv const&` by storing a `kv` in the iterator. That reference dangles across
    `++`, a hazard today's iterator does not have.
- **Framing tags 8, 9 and 10 stay exempt**, and the classification is `hooks().classify_fn()` exactly as
  today, so the set yielded and its document order do not change.
- **The first unknown entry is found when the view is built**, so `begin()` and `empty()` are O(1)
  after construction. `View`'s base keeps its meaning: the bytes of the first unknown field's value, or
  none. Re-derive that nothing reads it today:
  `git grep -n -E 'unknown_fields\(\)\.(bytes|size)|uf\.(bytes|size)' -- src include tests`.
- **`unk_items_` and `unk_items_built_` are deleted from `MessageView`.** `unk_items_` is declared in
  both modes, so both `MessageView<Index>` and `MessageView<Iter>` shrink. With it goes the MSVC-debug
  hazard of constructing a `pmr::vector` over a null resource, which the member's comment explains.
- **`unknown_fields()` keeps `noexcept`, `[[nodiscard]]`, `[[clang::lifetimebound]]` and its
  `requires(Mode == access_mode::Index)`.** It no longer has a `try`, because nothing in it can throw.
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

  Measure them paired, base against candidate. A slowdown past `[const §VIII.2]`'s +5% needs that
  clause's approval; it is not author-declared.

### 1.5 API, C ABI and layout

- **C ABI: no change.** No C symbol, struct, constant, error code or documented result moves. No C-ABI
  version bump.
- **Public C++: source-incompatible in three places**, each recorded in the B&L (§4):
  - the `unknown_fields_view` constructor from a `kv` span is removed;
  - `iterator::operator*` returns `kv` by value. Code that binds `kv const&` or `auto const&` keeps
    compiling, through lifetime extension. Code that takes `&*it`, or keeps a reference across `++`,
    does not. Re-derive the in-repo uses with
    `git grep -n -E 'unknown_fields\(\)|unknown_fields_view' -- tests src include bench bindings`;
  - `sizeof(unknown_fields_view)` grows, and `sizeof(MessageView<…>)` shrinks in both modes.
- **Layout: rebuild required.** `MessageView` is built by the library (the session's parse path) and
  read inline by application code, so mixing objects built against the old and the new header is an ODR
  violation. That is the same "requires a rebuild" disclosure 093 made for `MessageStore::reset_to`.
  - fixpp has no C++ ABI golden: `.github/workflows/abi-golden.yml` checks the C-ABI symbol set only.
  - Size pins: `git grep -n -E 'sizeof\((fixpp::)?(wire::)?(MessageView|unknown_fields_view)' -- .`
    finds design text only. The codegen flyweight pin is `sizeof(Msg) == sizeof(MessageView const*)`,
    a pointer, so it is untouched.
- **B(L)'s `kParseContainers` counts the `pmr::vector` members `MessageView<Index>` and `OffsetTable`
  construct from the parse resource**, times the view's moves (its recipe, `parse_capacity.hpp`).
  Deleting `unk_items_` changes what that recipe counts. **Re-derive the constant on the MSVC debug
  sandbox by its recipe and set it to the result.** An unmeasured edit is not allowed: a count left high
  is the safe direction, but no longer the recipe's answer.

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
2. **Q-33 is not the RED.** On MSVC debug the base passes it, because the list fits from the heap
   through the forwarding upstream. It is re-pinned:
   - with a null upstream, `unknown_fields()` after `group_slices(453)` yields the frame's `2=` fields;
   - on the forwarding lane, the spill count comes from `group_slices()` alone.
3. **Zero allocation.** Iterate under a global `operator new` counter and a counting resource: no call
   on either.
4. **Unchanged semantics.**
   - `DictBoundUnknownSplit` and `EmptyDictAllTagsUnknown` stay green.
   - New: document order over unknowns interleaved with known fields.
   - New: `empty()` on a frame with none, and `begin() == end()`.
   - New: an iterator taken from a temporary view stays valid while the `MessageView` lives.
   - New: a view iterated after its `Parser` is destroyed, with the dictionary alive, still classifies.
     This extends the existing post-`Parser`-destroy cell from the first call to iteration.
5. **Mutants.**
   - Reintroduce a materialised list over the parse resource: cell 1's zero-upstream assertion goes RED.
   - Drop the classification (yield every entry): `DictBoundUnknownSplit` goes RED.
   - Drop the framing exemption: the framing-tag cell goes RED.
6. **Fuzz** (`[const §VII.7]`, parser-adjacent). `tests/fuzz/fuzz_wire_parser.cpp` gains a property
   check. A differential against a filter that uses the same classifier would be tautological, so the
   check states the properties instead:
   - every yielded tag is one the dictionary does not define, and none is 8, 9 or 10;
   - every such entry of `entries()` is yielded exactly once, in document order.

   It runs over a parse resource with a null upstream and a tracking resource, so an allocation that
   creeps back into the read is reported. The 10-minute PR campaign runs on it.
7. **Bench**: the `bench/wire` case of §1.4, paired.

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

- **Recipe.** List the candidates with `git grep -n -E 'friend (class|struct)' -- include`. Then
  compile one TU that includes each candidate's header and evaluates `std::is_trivially_copyable_v`,
  the `bit_castable` concept and `__builtin_is_implicit_lifetime` on each (§A.2).
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
  - The PR teaches the filter the declaration's form, in the header only, and adds a self-test seed
    showing that a real second mint in `src/` still fails
    (`tools/test_dictionary_snapshot_exclusivity_gate.sh`).

### 2.5 API, C ABI and layout

- **C ABI: no change.** Neither type reaches `<fix/c_api.h>`.
- **Public C++: source-compatible for every legal use.** Only a forgery stops compiling. Sizes do not
  change: both stay empty classes.
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
     error. `static_assert(bit_castable<int>)` is its known positive.
   - The implicit-lifetime assert is guarded by `__has_builtin(__builtin_is_implicit_lifetime)`, or by
     `__cpp_lib_is_implicit_lifetime` for `std::is_implicit_lifetime_v`. Where neither exists, the
     guard leaves it out and says so; it does not pretend to pass.
2. **RED proof.** Put the unfixed header first on the include path: exactly the negative asserts fire, on
   clang and gcc (104's method). **Mutant:** default the copy constructor. The asserts go RED.
3. **Behaviour.** `writer_error_path_test` and `round_trip_property_test` (`open_group`) stay green,
   and so do the snapshot tests.
4. **MSVC.** Build `windows-msvc-debug` with `-k 0` on the touched targets (the wire and dictionary
   buckets, `fixpp_wire`, `fixpp_dict`). MSVC's `std::bit_cast` and `std::expected` are the ones not
   probed in §A.
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
- The documents still describe bookkeeping that was never built:
  - `[2b §4.5]`: "open-group entry-count tracking + offset list for backpatch at close";
  - `[2b §6.6]` and `[2b §8]`: the writer-scratch row of the three-arena table;
  - `specs/004-wire-codec`: data-model, contracts `writer.hpp`, and checklists `nfr.md` CHK003.
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
| c | Keep a `[[deprecated]]` two-argument overload that forwards to the new one. | Rejected. Before the first public release no supported external consumer exists (the reasoning of `[const §X.7]`, which states it for the C ABI). The PR updates every in-repo caller anyway, so a shim serves nobody. |
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
  green with the parameter removed.
- `writer_bench` loses an unused resource from its loop body. That is a perf change in principle, so the
  bench stays in the paired run.

### 3.6 Closing #559

#559's thread names one more site, in a comment: `SessionConfig::message_arena`. It is public, no
production code reads it, and it is out of #481's warning scope. That comment's site is placed in no
batch (`grep -n message_arena phases/phase-4/issue-batches*.md` in the parent finds nothing). After W1,
site 1 is done and sites 2 and 3 were done by B27. **The W1 PR must not close #559 while that site is
unplaced.** See §7, Q-1.

---

## 4. B&L delta

- **Amended: `B-093-8`.** Its "returns an empty view when its arena is exhausted" description is
  superseded. The row keeps its history (the #540 terminate and its reproduction) and gains: since W1,
  `unknown_fields()` allocates nothing, so no resource state changes what it yields. Witnesses: the
  rewritten `WireUnknownFieldsQ32.*` and `InboundFrameDispositionsQ33.UnknownFieldsAtHeadroomExhaustion`.
- **Narrowed, not retired: `L-093-5`.**
  - Its `MessageView::unknown_fields()` bullet is deleted, with an "Amended (W1, #688)" line quoting it.
  - The `OffsetTable::group_slices()` bullet stays live: a raw empty span still cannot be told from an
    absent group, and `group_slices_status()` is the status-bearing form.
  - Its other bullets are unchanged.
- **New: `B-688-1`.**
  - `unknown_fields()` is the filtered view of `[2b §4.8]`: it lists every field the dictionary does
    not define, in document order, framing tags exempt, and allocates nothing.
  - Its cost is a classification per entry per iteration.
  - The dictionary it was parsed against must outlive every iteration, not only the first call.
  - C++ source changes: the `kv`-span constructor is removed, and `iterator::operator*` returns `kv` by
    value. `MessageView`'s layout changes, so a rebuild is required. C ABI: none.
- **New: `B-559-1` (BREAKING, C++ only).** `wire::Writer` is constructed from its destination buffer
  alone. It allocates nothing, with or without groups. C ABI: none.
- **`#707`: no new row.** The keys' restriction is a compile-time property with no runtime observable.
  It is pinned by static asserts and recorded in the headers and in brain class 24's instance list.
  - Re-derive that no B&L row cites either key: `grep -n -E 'group_writer_token|snapshot_key'
    spec/behaviors-and-limitations.md`.
  - If Gate A wants one, it is a B-707-1 stating the two keys' conditions.

## 5. Gate A triggers (`[const §XVII.1]`)

| Trigger | Touched | By |
|---|---|---|
| public C++ API or C ABI | **yes**, the C++ API only | D-688-1 (`unknown_fields_view`, `MessageView` layout); D-559-1 (`Writer`'s constructor); D-707-1/2 (the keys' copy constructors) |
| the parser | **yes** | D-688-1 (`MessageView::unknown_fields`) |
| wire format or codegen layout | no | Bytes on the wire do not change, and the emitted flyweights hold a pointer. |
| concurrency, session FSM, store, security | no | D-688-1 removes a `mutable` write; it adds no synchronisation. |
| a new `.specify/` design document | **yes** | this note |

`[const §X.6]` gives "ABI-affecting features" four controls. Article X's subject is the C ABI (§1), and
no W1 decision changes it. W1 does change C++ layouts (`MessageView`, `unknown_fields_view`, `Writer`).
If Gate A reads §X.6 as reaching a C++ layout change, the four controls apply, and that reading is
Gate A's to make, not this note's.

## 6. Documents to amend or flag

- **Amend in place, as 100 did:**
  - `.specify/2b-wire.md` §4.5 (the constructor), §6.6 and §8 (the writer-scratch row goes; two arenas
    remain plus the validator's caller-supplied scratch), and §4.8 (an "as built since W1" line; the
    design itself is unchanged);
  - 093's contract C-8 L-5 and C-3 I-5: the `unknown_fields()` clause.
- **Flag in brain `components/wire.md`'s "Superseded records, flagged in place"**, frozen and not edited:
  - `specs/004-wire-codec/contracts/writer.hpp` and data-model (`Writer::scratch_mr`);
  - checklists `nfr.md` CHK003's writer-scratch arena;
  - 093's plan OD-18 as the record of the catch D-688-1 replaces.
- **brain `wire.md`.** The "Every admitted frame parses … `unknown_fields()` that cannot terminate"
  section gets a "⚠️ Superseded in part by W1" line: the catch is gone because nothing allocates.
- **Header comments**: `unknown_fields.hpp`'s summary becomes true as written. `parser.hpp`'s comment
  on `unknown_fields()` and on the deleted members goes.
- **brain `failure-classes.md` class 24.** Its scanning heuristic becomes §2.2's trait condition. Its
  instance list gains `group_writer_token` (fixed) and `snapshot_key`, and the form gains the
  implicit-lifetime clause and the `make_shared` variant. That edit is the owner-approved form of
  104's anti-pattern candidate (close-out row 15); it lands only on that approval.

## 7. Open owner questions

- **Q-1 (scope and placement): the `SessionConfig::message_arena` site in #559's comment.** It is
  session configuration, not wire, and is placed in no batch. Two ways out:
  - file it as its own issue and place it; then W1 closes #559;
  - or W1 references #559 without closing it.

  The documents and the code do not settle this; it is placement.
- **Q-2 (scope): `snapshot_key` (D-707-2) folded into W1.** #707's own fix list asks for the census, and
  the class rule says to fix every instance on its first finding, so this note folds it in. Confirm, or
  have it filed separately.

No technical question is left open. `[2b §4.8]` and the correctness rule settle #688; the measured
compile matrix settles #707's shapes; the code settles #559.

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
