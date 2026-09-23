# #495 / #493 / #486 — a shared reify table, copies that keep the source's caps, and an honest validator `noexcept`

> **Status: v0.5 — Gate A CLOSED at round 3 by owner-approved amendment (2026-09-23); post-Gate-A
> `/analyze` applied (text only, no design change).** Rounds (P1/P2/P3): R1 Codex 3/4/3, Opus 1/5/8;
> R2 Codex 0/5/4, Opus 0/4/11; R3 Codex 0/0/2, Opus 0/1/4 → amended per owner (no re-review).
>
> Batch branch `fix/495-493-486-dict-reify-copy`, cut from `origin/main` `3f200360`. This note is the
> design authority for all three issues and **replaces a Spec-Kit bundle**. It triggers Gate A under
> `[const §XVII.1]` (public C++ API §9, parser §2.2, C ABI §7).
>
> **Conventions.** Claims about code name functions or files, never line numbers. Where a count would
> rot, the note states the **condition** and a **re-derivation recipe**, not the result. Every
> negative grep carries a positive control. Every normative or B&L sentence names the test that
> witnesses it (§12); a sentence with no witness is narrowed until it has one.
>
> **Owner decisions (2026-09-23), binding.** Each is stated once, in the section named.
> - **O-1** — #495 takes the owned route (option (a)), narrowed by R-A: the shipped `Session`
>   dispatch path shares; a borrowed `Parser{tv}` keeps the deep copy. Factory signature and
>   `emit_dispatch.cpp` untouched (066 Decision 4 stands). Accepted trade-off: a handle pins the shared
>   **table** (never the `Dictionary`, §3.4). → §2, §3.
> - **O-2** — #493: every re-parse inherits the source's full `OffsetTable::Config`. → §4.
> - **O-3** — #486: conditional `noexcept`, pinned two-sided. `L-456-2` stays open, amended. → §5.
> - **O-4** — D-1c: the reify impl is allocated from `mr`. → §2.5.
> - **R-A** — the owned route is not public C++ API (`detail` convention). → §2.2, §2.3, §3.1.
> - **R-B** — the snapshot owns its table in its own control block; supersedes v0.1's O-5 and its
>   `L-495-2`. → §6.
> - **R-C** — G2 flips from "exactly one" to "zero matches". → §6.4.
> - **R-D** — the C loader uses `new_delete_resource()`; C-ABI MINOR 7 → 8, BREAKING. → §7.
> - **Q-6 (2026-09-23)** — NO: `fixpp_engine_destroy` does not release retained session shells'
>   `dict_` / `tv_` in this batch; the C-route text narrows instead (§3.4, T-13, B-495-1). The
>   retained-shell cost is filed separately as fixpp#501.
> - **Q-7 (2026-09-23)** — Gate A closes after round 3 by "amend, no re-review" (Opus option (a)).
> - **R-E (2026-09-23)** — `detail/` headers and nested `<module>::detail` namespaces are Internal
>   (clients must not use them) but ARE installed, because public headers include them. The
>   api-contract Internal class names nested `detail` namespaces and tags such as `owned_route_key`
>   explicitly, which grounds R-A in the contract rather than in precedent. → §2.2, §12.
> - **R-F (2026-09-23)** — pre-release, `FIXPP_VERSION_*` and the CMake `project()` `VERSION` (0.0.1)
>   are not bumped for C++ layout breaks; both the C-ABI and the library version reset to 1.0.0 at
>   v1.0. → §7.2, §9, §12.
> - **Rejected by the owner:** (b) refusing reify/clone on the borrowed route; (c) amending NFR-003-3
>   only; v0.1's O-5 (keep the alias, document `L-495-2`).
>
> **Fable consult** `b13-reify-handle-pins-dictionary-resource` (trigger 1, `probable`) proposed (i′)
> and argued `table_view` is self-contained. Below `beyond doubt` it is supporting analysis only; the
> authority for §6 is R-B, and the consult's load-bearing facts carry recipes in §6.1.

---

## 0. Scope

### 0.1 Issues

| Issue | Defect | Disposition |
|---|---|---|
| fixpp#495 | Every `dict::reify()` of a dict-backed view deep-copies the membership table: hundreds of µs against NFR-003-3's 1.2 µs | D-1: owned route shares; borrowed route keeps the copy, scoped out |
| fixpp#493 | Clone and the reify factory re-parse under the **default** caps (`L-458-2`) | D-2: inherit the source's `Config` at every re-parse site |
| fixpp#486 | `dictionary_driven_validator`'s `noexcept` constructor moves a `table_view`; on MSVC that allocates, so OOM terminates | D-3: conditional `noexcept` |
| ride-along | Sharing through the snapshot's alias would pin the `Dictionary` and its load resource | D-4 |
| ride-along | A C `Dictionary`'s storage lives in the host's default resource at load (the two paths that reach it: §7.1) | D-5 |

One note, because the three issues edit the same statements: #493's two dict-backed sites are the
two #495 rewrites, and D-4/D-5 exist because #495's sharing changes what a handle pins.

### 0.2 Constitution triggers

- `[const §XVII.1]`: public C++ API (§9), parser (new `Parser` constructor), C ABI (D-5).
- `[const §X.1]` / `[const §X.6]` / `[const §X.7]`: the C ABI changes (§7). §X.6's four Appendix A
  controls are discharged as `.specify/447-458-452-capi-refusals.md` did in issue mode: Codex Gate A
  (this loop); `/clarify` by hand (the owner rulings are its record); `/analyze` over this note, run
  by the spec-analyzer on 2026-09-23 before implementation (all findings text-only, applied in v0.5);
  owner `/plan` sign-off = the owner's Gate A approval of 2026-09-23 (Q-7).
- Appendix A categories this change touches, beyond the C ABI: **threading** (a table shared by
  refcount and read from several threads, §3.3); **error semantics** (a copy's refusal set changes
  under #493, §4; the validator's exception specification, §5); **codegen/loader** (the C
  dictionary loader's resource, §7). No wire format, session FSM or security surface changes.
  - **#493 on the C ABI is an internal clone-seam widening with no public C producer.** No C surface
    can raise or lower a cap: `grep -rn "max_offset_entries\|max_group_entries_per_instance\|OffsetTable::Config" src include`
    finds only `offset_table.cpp`'s enforcement and `parser.hpp`'s C++ overloads (positive control:
    the grep does find those). A failure that becomes a success is not breaking under §X.7 in any
    case. T-6 keeps the seam's remaining refusal witnessed.
- `[const §VIII.2]`: any bench row past +5% needs non-author approval (§11).
- `[const §VII.7]`: no new scanning code. The new `Parser` constructor builds the same `dict_hooks`
  as the existing one; the `tests/fuzz/fuzz_wire_*` harnesses already cover the scanners it feeds —
  recipe: `grep -n "Parser<access_mode::Index> p{tv}" tests/fuzz/fuzz_wire_parser.cpp` shows the
  dict-backed `Parser` construction the harness drives. No new harness is planned.

### 0.3 Where the source contradicts the brief

| # | Brief premise | What the source shows | Consequence |
|---|---|---|---|
| C-1 | The owner token goes into `dict_hooks` | `dict_hooks` is copied into `wire::entry_context`, whose size `tests/codegen/flyweight_shape_test.cpp` pins (every generated flyweight must match); `OffsetTable` stores `hooks_`, fail-loud tests tune caps to `sizeof(OffsetTable)`, and nested sub-tables are `mr->allocate(sizeof(OffsetTable))` | The token lives on `MessageView` (§2.1). Pins: `grep -rnE 'sizeof\((fixpp::)?(wire::)?(entry_context\|dict_hooks\|MessageView\|OffsetTable)\b' include src tests bench` |
| C-2 | The owned route "restores no allocation outside `mr`" | `owning_message_handle_from_frame` begins with a global `new owning_message_handle::impl{mr}` on every route | Sharing alone cannot meet the clause; D-1c (§2.5) |
| C-3 | `static_assert(is_nothrow_constructible_v<V, table_view&&> == is_nothrow_move_constructible_v<table_view>)` pins #486 | The trait also counts the caller-side move into the by-value parameter, so the equality holds on the unfixed tree on every toolchain | §5 isolates the constructor's own specification |
| C-4 | A handle pins "the shared table" | For the C ABI and any C++ `Session` given `SessionConfig::dict_snapshot`, `inbound_tv_` aliases into a `dictionary_snapshot` whose `source_` holds the `Dictionary` | D-4 (§6) |
| C-5 | Pinning only extends a lifetime | A `Dictionary` deallocates into its **load** resource on whatever thread drops it last | D-4 removes this for handles and clones; D-5 for C dictionaries (§7.1) |

---

## 1. Background

### 1.1 #495 — where the time goes

- **Reify.** `detail::owning_message_handle_from_frame` (`src/dictionary/reify.cpp`) seats
  `impl::owned_tv_` (`std::optional<table_view>`) with `view.membership_copy()` when
  `view.is_dict_backed()`, then re-parses the copied frame with `Parser<Index>{*owned_tv_}`.
- **Clone.** `fixpp_msg_clone` (`src/capi/message_write.cpp`) does the same into
  `fixpp_msg::owned_tv_` (`src/capi/capi_internal.hpp`).
- `membership_copy()` copy-constructs the whole `table_view`, every hash container of every message.
  #494's Gate B measured it dominating the call (parent repo,
  `decisions/speckit/090-capi-refusals-verify.md` § *Gate B G-1*). 066 Decision 4 chose the copy
  because a handle outlives its dispatch window and the factory signature was frozen.

**Every view the Session hands an application is `parse_and_dispatch_`'s parser output.**
`Session::parse_and_dispatch_` (`src/session/session.cpp`) builds `pd_parser` over `inbound_tv_`, and
every `MessageView`-bearing `Application` callback (`fromApp`, `fromAdmin`, `toApp`, `toAdmin`) runs
inside one of its calls. Re-derive with `grep -n "parse_and_dispatch_(" src/session/session.cpp`, and
list production `Parser` constructions with `grep -rnE "Parser<[^>]*>[^(;]*[{(]" src` (it also prints
comment lines; read the hits). The C ABI reaches the same parser: `CapiApplication::fromApp` / `toApp`
(`src/capi/engine.cpp`) wrap the Session's view in a stack `fixpp_msg`. `fixpp_session::tv_` is not an
owner object for any view.

### 1.2 #493 — the re-parse sites

`OffsetTable::Config` (`include/fixpp/wire/offset_table.hpp`) holds `max_offset_entries` and
`max_group_entries_per_instance` in the private `cfg_`, with no reader. `OffsetTable::build` enforces
the entry cap; the group cap is enforced lazily, on group reads.

**Census condition:** a re-parse site is a copy site (`grep -rn "membership_copy()" src`) or that
site's dict-free fallback (the `MessageView` construction in the neighbouring `else`). At `3f200360`
this gives four sites, all on the default `Config`; the condition binds, not the table.

| # | Site | Route | Today |
|---|---|---|---|
| S1 | `owning_message_handle_from_frame` | dict-backed | `parser.parse(frame, mr)` |
| S2 | `fixpp_msg_clone` | dict-backed | `clone_parser.parse(fv, clone_mr)` |
| S3 | `owning_message_handle_from_frame` | dict-free fallback | `view_cache_.emplace(frame, mr)` |
| S4 | `fixpp_msg_clone` | dict-free fallback | `make_unique<MessageView<Index>>(fv, clone_mr)` |

S1/S2 refuse a raised-cap source (`B-458-2` / `B-458-1`, recorded as `L-458-2`). S3/S4 degrade
silently: the copy's `OffsetTable` build fails and every read reports absent, and
`MessageWrite.CloneDictFreeOversizedSourceStillReturnsOk` asserts only `FIXPP_ERR_OK`, so it is green
over an **empty** clone — a live defect of the same class.

### 1.3 #486

`explicit dictionary_driven_validator(fixpp::dict::table_view dict) noexcept : dict_{std::move(dict)} {}`
(`include/fixpp/wire/validator.hpp`). On MSVC the member move allocates (the issue comment), so
`bad_alloc` becomes `std::terminate`; libstdc++ and libc++ move nothrow. `table_view.hpp`'s ⛔ note
forbids restoring an explicit `noexcept` on `table_view`'s own move constructor; **this change does not
touch `table_view`**. Production callers: `grep -rn "dictionary_driven_validator>(" src` (at
`3f200360`, `Session::open`'s `make_unique<dictionary_driven_validator>(*inbound_tv_)`). The by-value
parameter is copy-initialised at the call site, outside the `noexcept`, so that site can already
throw `bad_alloc`; after the fix an MSVC failure in the member move reaches the same handler.

---

## 2. D-1 — #495, the owned route

### 2.1 The owner token lives on `MessageView`

`MessageView<Index>` gains `std::shared_ptr<const fixpp::dict::table_view> const* dict_owner_ = nullptr;`
(`MessageView<Iter>` gets an empty `[[no_unique_address]]` member instead: both copy sites take
`MessageView<Index>`, so an Iter view never reads an owner) — a borrowed pointer to a `shared_ptr` **object**; `nullptr` means borrowed or dict-free. `Parser`
sets it after constructing the view (`MessageView` gains the declaration `template <access_mode>
friend class Parser;` — it has none today — so no public constructor changes). The defaulted move carries it; copy is already deleted.

**Why a pointer, not a `shared_ptr`:** a by-value `shared_ptr` costs an atomic increment/decrement per
inbound message, on a cache line shared by sessions sharing a snapshot; the pointer costs a store. The
price is a lifetime dependency on the owner object, confined by §3.1. `sizeof(MessageView<Index>)`
and `sizeof(MessageView<Iter>)` grow by one pointer; C-1's grep finds no pin on either (re-run before
implementing).

### 2.2 `Parser`'s owning constructor, behind a `detail` tag (R-A)

```cpp
namespace fixpp::wire::detail {
// Tag for the owned route. Constructible anywhere; `detail` is the "not an entry point" signal.
struct owned_route_key { explicit owned_route_key() = default; };

inline fixpp::dict::table_view const& checked_owner(
    std::shared_ptr<const fixpp::dict::table_view> const& owner) noexcept {
    assert(owner);  // precondition: non-null; checked BEFORE the dereference
    return *owner;
}
}  // namespace fixpp::wire::detail

template <class SP>
Parser(detail::owned_route_key, SP&& owner) noexcept
    requires(std::is_lvalue_reference_v<SP&&> &&
             std::same_as<std::remove_cvref_t<SP>, std::shared_ptr<const fixpp::dict::table_view>>)
    : hooks_{dict_hooks::for_table_view(detail::checked_owner(owner))},
      owner_{std::addressof(owner)} {}

template <class SP>
Parser(detail::owned_route_key, SP&&) noexcept
    requires(!std::is_lvalue_reference_v<SP&&>)
= delete;  // diagnostic only; see below
// + private: std::shared_ptr<const fixpp::dict::table_view> const* owner_ = nullptr;
```

`parse(frame, mr)` and `parse(frame, mr, cfg)` set `mv.dict_owner_ = owner_`; `parse_iter(frame)` stores
nothing. `Parser<Iter>` keeps `owner_`, so the owned-route constructor compiles in both modes.

- **Why "not public API" holds (R-A basis).** `owned_route_key` is a **tag**, not a passkey:
  `dict::detail::snapshot_key` (`dictionary_snapshot.hpp`) has a private constructor and one friend,
  while this key is constructible by anyone, so tests and benches can name it. What makes it non-API
  is the repo's `detail` convention. `.specify/api-contract.md` §3.3 classifies `fixpp::detail::*`
  and `<module>/detail/` headers as Internal, and after R-E names nested `<module>::detail`
  namespaces and tags such as `owned_route_key` there explicitly (they are installed but not for
  clients). The in-tree precedent agrees:
  `fixpp::dict::detail::owning_message_handle_from_frame` in the installed `reify.hpp`, and the
  existing `namespace detail` blocks in `wire/parser.hpp` and `wire/view.hpp`. `explicit` forces the
  spelling `detail::owned_route_key{}` at every call.
- **Lvalue only** (witness T-7). `owner_` stores the argument's address, so a temporary would dangle. The
  forwarding reference plus the `is_lvalue_reference_v` conjunct rejects both const and non-const
  rvalues. The deleted overload is **redundant** with that conjunct (each alone rejects rvalues,
  measured on clang 22 and GCC) and is kept only for its clearer "deleted function" diagnostic.
- **Exact type (`same_as`, witness T-7).** A `shared_ptr<table_view>` lvalue binds `SP&&` directly. Without the
  constraint the error sits in the member initialiser, outside the immediate context, so
  `is_constructible_v` would report `true` for a construction that does not compile.
- The existing borrowed constructor takes one argument, so the two never compete. Null owner is a
  violated precondition of a `detail` entry point; every production caller holds a non-null owner by
  construction (§2.4, §2.6).
- **Unchanged:** `Parser{tv}`, `Parser()`, and every existing call site.

### 2.3 `shared_membership()` — private, reached through a `detail` accessor

A private `MessageView` member, replacing `membership_copy()` at the two copy sites, reached only
through:

```cpp
namespace fixpp::wire::detail {
struct message_view_membership_access {  // befriended by MessageView
    template <access_mode M>
    [[nodiscard]] static std::shared_ptr<const fixpp::dict::table_view>
    shared_membership(MessageView<M> const& v);
};
}
```

It returns:
1. **Owned:** `*dict_owner_` when `dict_owner_ != nullptr && dict_owner_->get() == hooks_.opaque_dict()`
   — a refcount increment, no allocation.
2. **Borrowed and dict-backed:**
   `std::make_shared<const fixpp::dict::table_view>(*static_cast<fixpp::dict::table_view const*>(hooks_.opaque_dict()))`,
   copied **in place from the table reference**. The control block and table share one allocation,
   which comes first; the copy's own allocations come last; there is no `table_view` move (so no MSVC
   move cost). May throw `bad_alloc`, like `membership_copy()` (its FQ-1 note).
3. **Dict-free:** `nullptr`.

Each arm is witnessed by T-8. **Arm 1's identity check** defends one misuse: an owner object reassigned while its old table is
alive — the check misses and arm 2 copies the old table, which is the one the view was parsed
against. It does **not** make a dead or reassigned owner safe (reading a destroyed owner is UB before
the comparison; a dead old table already dangled; a new table at the dead one's address passes, ABA).
All three lie outside §3.1's precondition.

`membership_copy()` stays public and unchanged for its other callers
(`grep -rn membership_copy src include tests`).

### 2.4 The two copy sites

**Reify** (`src/dictionary/reify.cpp`): `impl::owned_tv_` becomes
`std::shared_ptr<const table_view>`, assigned `wire::detail::message_view_membership_access::shared_membership(view)`
when `view.is_dict_backed()`. The re-parse is
`Parser<Index> parser{wire::detail::owned_route_key{}, handle.pimpl_->owned_tv_}`, over the impl's own
member (stable: the impl never relocates; a handle move moves the pointer). The handle's own `view()`
is therefore owned-route, so re-reifying or cloning it shares. The `assert(!owned_tv_.has_value())`
and its "#456 seam 6: seated, not assigned" comment go: #456 bars assigning a `table_view`, and the
table is now behind a pointer.

**Clone** (`src/capi/message_write.cpp` / `capi_internal.hpp`): `fixpp_msg::owned_tv_` becomes
`std::shared_ptr<const fixpp::dict::table_view>`, seated the same way; `clone_parser` is
`Parser<Index>{wire::detail::owned_route_key{}, clone->owned_tv_}` over the heap shell's stable
member, so a clone of a clone shares. `session_tv_` stays separate; non-null still means "outbound".

**Untouched:** the factory signature, `reify.hpp`'s declarations, `emit_dispatch.cpp`, the generated
bridge, `is_dict_backed()`, and the three-way refusal contract of `B-458-1` / `B-458-2`. New code and
comments follow §6.4's G2 spelling rule.

### 2.5 D-1c — the handle's impl moves into `mr` (O-4)

**Today** the owned reify path has two global allocations: the impl's `new` and `membership_copy()`'s
table copy. D-1 removes the second on the owned route; D-1c removes the first. After both, every
allocation on the owned reify path comes from `mr`: the impl, `bytes_`, the `OffsetTable`'s
containers and `unk_items_` (the zero-cap carry and the framer allocate nothing; T-16's probe asserts it on the empty path).

**Change:** the impl is allocated with `std::pmr::polymorphic_allocator<>{mr}.new_object<impl>(mr)`.
`~owning_message_handle` and the move-assignment's release go through one private helper that recovers
the resource from `pimpl_->bytes_.get_allocator().resource()` **before** destroying the impl, then
calls `delete_object`. `mr` must already outlive the handle, since its parsed view lives there, so no
new lifetime obligation arises. A monotonic `mr` reclaims the impl only on `release()`, as for
`bytes_`.

**Effect on OOM tests:** the impl becomes the first `mr` call, so every cell that fails a bounded or
failing `mr` through the factory now lands on a different allocation. T-16 derives that population
and recalibrates it by phase.

### 2.6 Session wiring and the `inbound_tv_` invariant

- `parse_and_dispatch_`'s `pd_parser{*inbound_tv_}` becomes
  `pd_parser{wire::detail::owned_route_key{}, inbound_tv_}`. This one edit puts every view handed to
  an application callback, C++ and C, on the owned route.
- `validate_inbound_`'s `vg_parser` stays borrowed; its view never leaves the function.
- **Invariant:** `inbound_tv_` is the owner object of every dispatched view, so once
  `parse_and_dispatch_` can run it is never reassigned or reset. Enforcement: it is written only in
  `Session::open`, before `state_ = lifecycle::open`; afterwards `open()`'s first check
  (`state_ != lifecycle::never_opened` → `session_already_open`) makes further writes unreachable.
  Write census: `grep -n "inbound_tv_ *=" src/session/session.cpp`. The member comment in
  `include/fixpp/session/session.hpp` states the invariant and names this note.
- **No `assert(!inbound_tv_)` at the seating site:** an `open()` that fails after the assignment
  (e.g. on the security-profile check) leaves `state_` at `never_opened`, and a legitimate retry
  reassigns it while no view exists.

---

## 3. Lifetime and concurrency

### 3.1 The owner object must outlive every view parsed through it

A borrowed-route view depends on its frame, its parse arena and the pointee table. An owned-route view
adds the owner `shared_ptr` **object** at `dict_owner_`, which can die or be reassigned while the
pointee lives on. **Precondition:** the owner object outlives every view parsed through it and is not
reassigned while any such view may be shared. R-A confines it to code that names
`detail::owned_route_key`; `grep -rn "owned_route_key" src` must list only the three holders below
(a one-off recipe, not a gate). Tests and benches carry the precondition themselves.

| Owner object | Why the precondition holds | Views pointing at it |
|---|---|---|
| `Session::inbound_tv_` | Single write in `open()` before dispatch (§2.6); dies with the `Session` | `pd_parser`'s output, live only inside `parse_and_dispatch_`; C inbound handles wrap it |
| `owning_message_handle::impl::owned_tv_` | Seated once by the factory; impl never relocates | `impl::view_cache_` |
| `fixpp_msg::owned_tv_` | Seated once by `fixpp_msg_clone`; shell never relocates | `fixpp_msg::owned_view_` |

A handle or clone copies the `shared_ptr` inside the dispatch window and never reads `dict_owner_`
again.

### 3.2 Stable addresses

`for_table_view` stores `std::addressof(table)`, and the span accessors alias the table's own vectors.
A `shared_ptr`'s pointee never relocates, and owner objects are not reassigned while a view exists
(§3.1), so moving a handle cannot invalidate the table.

### 3.3 Sharing across threads

- **Reads.** N handles on N threads plus the Session strand read one `table_view`. Safe only if every
  accessor the hooks and the validator call is `const` with no hidden writes. `table_view` has no
  `mutable`, `thread_local` or atomic member: `grep -nwE "mutable|thread_local|atomic" include/fixpp/dict/table_view.hpp`
  prints nothing (positive control: the same grep over `include/fixpp/wire/parser.hpp` prints hits).
  A grep cannot stop a future lazy cache; T-17 is the executed witness. Per
  `brain/components/dictionary.md`, the object is not called "immutable": a `const_cast` write
  through a span is defined; the #456 seal governs reachability, not writability.
- **Refcounts.** Copying one `shared_ptr` object from several threads is `const` access, so safe.
- **Destruction.** The last reference may drop on any thread. After D-4 it never owns a `Dictionary`
  (§3.4), so destruction is `~table_view` alone, which deallocates only through default allocators
  (§6.1) and touches no Session state. T-11 drops it on a second thread.

### 3.4 What a handle pins

| Route | `inbound_tv_` is | A live handle or clone pins |
|---|---|---|
| C++ `Session`, no `dict_snapshot` | `make_shared<const table_view>(dictionary->as_table_view())` | the table only |
| C++ `Session` with `dict_snapshot`; every C-ABI session | a copy of the snapshot's own table owner (D-4) | the table only — not the snapshot; the handle does not pin the `Dictionary` (T-19; C++ route T-13). On the C ABI the session shell pins it regardless (next bullets) |
| Borrowed `Parser{tv}` | none | its own deep copy (unchanged) |

- The pinned table is self-contained (§6.1), so handles and clones carry no load-resource obligation.
  On the **C++** route the `Dictionary` dies when the application's and the session's own references
  go, as on `origin/main` (T-13 C++ twin).
- On the **C ABI** the session shell holds the `Dictionary` for the **process lifetime**:
  `fixpp_session_open` sets `fixpp_session::dict_` (and `tv_`) and nothing resets them, and
  `fixpp_engine_destroy` retains its session shells in the dead-shells registry until the process
  exits (closed `L-050-z`, `spec/behaviors-and-limitations-closed.md`). Recipe:
  `grep -n "dict_ = \|dict_\.reset" src/capi/session.cpp src/capi/engine.cpp` (positive control: the
  same grep over `src/capi/message_write.cpp` prints the outbound assignment and reset). This is
  pre-existing and independent of handles, so "a clone never keeps the `Dictionary` alive" is **moot**
  on the C route, not witnessed; the C route's no-alias property rests on `fixpp_session_open` going
  through `shared_dictionary_view` (T-11, T-19). Owner ruling Q-6: not changed here; the retained-shell
  cost is filed separately as fixpp#501.
- The two load-resource paths that remain on the C ABI are closed by D-5; the one statement of them
  is §7.1.
- **Memory per handle:** before, one full table copy on the global heap; after, on the owned route,
  one refcount. A session closed while handles live keeps its table alive until the last handle dies
  — the pin O-1 accepted.

### 3.5 Destruction order

- `impl`: `owned_tv_` stays declared before `view_cache_` (reverse-order destruction destroys the view
  first); the member comment says so.
- `fixpp_msg`: `owned_tv_` stays declared before `owned_frame_` / `owned_view_`, and
  `fixpp_msg_destroy` still resets `owned_view_` first.
- When the handle holds the last reference, the table dies after the view.

---

## 4. D-2 — #493, the source's caps travel with the copy

New public accessor: `[[nodiscard]] Config config() const noexcept { return cfg_; }` on `OffsetTable`,
reached as `view.offsets().config()`. Both copy sites hold an Index view.

| Site | After |
|---|---|
| S1 | `parser.parse((*framed)[0], mr, view.offsets().config())` |
| S2 | `clone_parser.parse(fv, clone_mr, h->view->offsets().config())` |
| S3 | `view_cache_.emplace((*framed)[0], mr, view.offsets().config(), wire::dict_hooks::none())` |
| S4 | `make_unique<MessageView<Index>>(fv, clone_mr, h->view->offsets().config(), dict_hooks::none())` |

- **Both directions travel.** A copy keeps a raised cap **and** a lowered one. The group cap is lazy,
  so a source parsed under a lowered `max_group_entries_per_instance` reads scalars but fails its
  group read; its copy now fails that group read identically, where today it re-parses at the default
  and succeeds. That is O-2's full fidelity, a C++-only narrowing (no C surface sets a cap, §0.2),
  witnessed by T-5.
- **S3/S4 need no new API.** They call the existing four-argument `MessageView` constructor with
  `dict_hooks::none()`, which `Parser{}.parse(frame, mr[, cfg])` already uses for every dict-free
  parse. Calling it directly, not `Parser::parse`, keeps the fallback's "never refuses, degrades in
  place" contract (`B-458-2`'s retained arm).
- **Root group context normalised.** The four-argument constructor seeds the root `group_context`
  with the MsgType; the two-argument one leaves it empty (`parser.hpp`). A copy therefore carries the
  same root context as any `Parser{}`-parsed source; only a source built through the raw two-argument
  constructor sees a difference. It is observable only through `OffsetTable::group_context_for()`,
  whose production consumer is `fixpp_msg_get_group`'s cursor seed (`grep -rn "group_context_for" src`),
  and a dict-free table declines that read whatever the context (`B-220-1`). T-4 witnesses it.

**What can still refuse a copy.** A copy re-parses its source's bytes under its source's caps, so it
fails only where the source's own build failed, or on OOM in the copy's arena. The first needs the raw
dict-backed `MessageView` constructor (the one taking `dict_hooks::for_table_view(...)`), which skips
`build_status()`. It is public, so external C++ callers can reach it; in-tree sites: read the hits of
`grep -rn "for_table_view(" src tests` (positive control: the TU holding
`…MalformedFieldYieldsWireInvalidFrame` appears). `B-458-1` / `B-458-2` stay true as written (their
code lists are `translate()`'s image).

---

## 5. D-3 — #486, a conditional `noexcept` checkable on both sides

```cpp
explicit dictionary_driven_validator(fixpp::dict::table_view dict)
    noexcept(std::is_nothrow_move_constructible_v<fixpp::dict::table_view>)
    : dict_{std::move(dict)} {}
```

**The pin** (C-3), in `tests/wire/validator_domain_test.cpp` (includes `validator.hpp` with
`table_view` complete; built by the MSVC legs):

```cpp
using tv_factory = fixpp::dict::table_view (&)() noexcept;
static_assert(noexcept(fixpp::wire::dictionary_driven_validator{std::declval<tv_factory>()()})
              == std::is_nothrow_move_constructible_v<fixpp::dict::table_view>);
```

The argument is a prvalue from a `noexcept` function, so guaranteed elision initialises the parameter
with no move; the only other call is the implicitly-`noexcept` destructor. The `noexcept` operator
therefore sees only the constructor's own specification.

**Two-sided proof** (recorded in the verify record): MSVC unfixed — `true == false`, fires; MSVC fixed
— `false == false`; Linux fixed — `true == true`; Linux mutation `noexcept(false)` — fires.
**Fallback** if the MSVC leg shows MSVC does not evaluate the prvalue form as the standard requires
(Q-2): a behavioural witness on MSVC — a TU-local failing `operator new`, calibrated by counting a
parameter-only move, failing inside the member move; unfixed `EXPECT_DEATH`, fixed
`EXPECT_THROW(std::bad_alloc)`.

**Unaffected:** SC-007's by-value validator (no virtual edge; it cannot join §2's sharing and this
note does not reopen that), and `table_view`'s inferred move specification.

---

## 6. D-4 — the snapshot owns its table in its own control block (R-B, R-C)

### 6.1 Why this is safe: `table_view` is self-contained

A handle can pin the table without the `Dictionary` only if the table holds nothing pointing back
into the `Dictionary` or its load resource. `table_view`'s data members are standard containers on
default allocators, or scalars; the nested types (`group_ctx_key`, `enum_domain`) hold `std::string`
and `std::vector`; there is no `pmr`, `string_view`, `span` or raw-pointer member. The populators copy
into `std::string` rather than viewing the `Dictionary`'s name pool (`table_view_builder`;
`as_table_view()`'s population calls). `table_view.hpp` already states that `as_table_view()` "may
legally outlive the Dictionary it was built from".

Lead recipe:
```
awk '/^class table_view \{/,/^\};/' include/fixpp/dict/table_view.hpp | grep -vE '^\s*//' \
  | grep -E '^\s{4}[^(]*(pmr|string_view|span<|\*)[^(]*\s[a-z_]+_\s*(=[^;]*)?;'
```
Expect no output; positive control: the same grep over the whole header prints
`valid_tag_set_view::set_` (a separate view type). ⚠️ Single-line pattern: it misses wrapped
declarations such as `valid_` and `required_` (`std::unordered_map<std::string, …>`, read by hand).
T-11 is the executed proof.

### 6.2 The change

In `include/fixpp/dict/dictionary_snapshot.hpp` / `src/dictionary/dictionary_snapshot.cpp`:
- `view_` becomes `std::shared_ptr<const table_view>`, seated with
  `std::make_shared<const table_view>(std::move(tv))`; never null (`make_shared` throws). `view()`
  returns `*view_`.
- **Decision: a public accessor, `[[nodiscard]] std::shared_ptr<const table_view> const& view_owner()
  const noexcept [[clang::lifetimebound]]`.** `shared_dictionary_view(snap)` returns
  `snap->view_owner()` (or `nullptr` for a null `snap`), stays `noexcept`, and remains the call both
  consumers use; its aliasing construction disappears. Befriending the free function instead is
  rejected: G1's assertion (b) counts every `friend` line in the header and requires exactly one
  (R-C keeps G1), and a friend would also reach `source_`. The accessor exposes only the `const`
  pointee the helper already hands out, and `SessionConfig` accepts only a `dict_snapshot`, so there
  is no injection surface for a bare owner; C1's closure (`.specify/215-dictionary-view.md` §3)
  holds. Its const-pointee shape is pinned by T-19(d).
- **Provenance unaffected:** `Session::open`'s C4 check compares `source()` only
  (`grep -n "source()" src/session/session.cpp`).
- **Consumers:** `grep -rn "shared_dictionary_view(" src` (at `3f200360`: `Session::open` and
  `fixpp_session_open`). The call sites do not change.
- `dictionary_snapshot.hpp`'s header comment names this note as superseding 215's alias design.

### 6.3 Cost (config time only, `[const §XV.1]`)

| path | `table_view` moves | allocations |
|---|---:|---:|
| 215 §4, specified (`make_shared<const dictionary_snapshot>`) | 2 | 1 |
| **D-4** | **2** | **2** |

Move 1 initialises the by-value parameter; move 2 now goes into the table's `make_shared` block
instead of the `view_` member (on MSVC each move allocates; config-time, same count). The control
block adds one allocation per snapshot. Nothing pins that count:
`grep -rn make_dictionary_snapshot tests/ bench/ | grep -i 'alloc\|count\|mallocnesia'` is empty
(positive control: without the second filter it prints the snapshot's users).

### 6.4 G2 (R-C) — the one statement of the gate and its spelling rule

`tools/check_dictionary_snapshot_exclusivity.sh`'s G2 is a **spelling lint**: it matches a
parenthesised `shared_ptr<const … table_view>(` whose first argument begins `std::move(` or is an
identifier followed by a comma. The script's own comment concedes it misses east const, brace-init,
deduced return and type aliases. After R-C:
- **Assertion `g2_all_n -eq 0`, log line `G2 matches of the enumerated spellings = 0`.** It claims
  nothing about aliasing constructions in general; the **property** — a handle or clone never keeps
  the `Dictionary` alive — is witnessed behaviourally on the C++ shipped route by T-13's `weak_ptr`
  arm (moot on the C route, where the session shell pins the `Dictionary`, §3.4) and on the helper
  by T-19. G2 is a cheap early tripwire for the spellings it lists.
- **Scope: tree-wide** (`src/ include/ bindings/ tools/ tests/`), decided: it costs nothing today,
  scratch-copy mutants are never committed, and a test needing an alias would need an allowlist.
- The `G2 DEAD` liveness line (`-ge 1`) is deleted (the flipped tree fails it by design); T-18's
  seeded positives replace it. The "(in the factory: N, elsewhere: M)" split and G2's assertion (b)
  collapse into the one count. G1 is unchanged.
- **Spelling rule for this change (code, comments and self-test alike).** G2 does not strip comments
  and has no self-exclusion. Safe: copying or assigning a `shared_ptr`, and `std::make_shared<const
  table_view>(…)` (no `shared_ptr<` token). ⚠️ Avoid `std::shared_ptr<const table_view>(std::move(x))`
  even as a one-argument move: G2 cannot tell it from the alias. The self-test assembles its seed text
  from fragments at run time, **or** G2 gains a `SELF` / `SELF_TEST` exclusion mirroring G1's; the
  implementer picks one and records which.
  **Recorded at implementation:** fragments — the self-test assembles both seeds at run time; G2 has
  no `SELF` / `SELF_TEST` exclusion.
- **215 v0.4's "required helper calls" census does not exist in the shipped script** (PR #262
  replaced it with behavioural pins; `grep -n "shared_dictionary_view" tools/*.sh` finds only a
  comment). Those pins are amended by T-19.

---

## 7. D-5 — the C loader uses `new_delete_resource()` (R-D)

### 7.1 The change

- `fixpp_dict_load_from_xml` (`src/capi/dictionary.cpp`) calls
  `load_any(path, std::pmr::new_delete_resource())` instead of `get_default_resource()`.
- **Why.** A C `Dictionary`'s storage lives in whatever default resource the host had installed at
  load — possibly since replaced, destroyed, or not thread-safe. Two paths reach it: (1) a
  `fixpp_dict_t` never attached to a session is destroyed, and `~Dictionary` deallocates into that
  resource; (2) a session-attached `Dictionary`, held for the process lifetime by the retained session
  shell (§3.4), keeps its **live** storage in a host resource the host may tear down, so any later read
  is a use-after-free. (An outbound `fixpp_msg::dict_` cannot drop the last reference: the session
  shell outlives it.) `new_delete_resource()` is immortal and thread-safe, closing both.
- **Scope of the claim:** this closes the hazard for the **dictionary loader** only. Other
  `get_default_resource()` captures (e.g. `EngineConfig::default_session_resource` /
  `default_message_resource`, evaluated at construction) are outside this note; re-derive the
  population with `grep -rn "get_default_resource()" src include`. Nothing is filed from here.
- **`include/fix/c_api/dict.h`'s doc comment** changes from "Wraps
  fixpp::dict::XmlLoader::load(path, std::pmr::get_default_resource())" to name
  `fixpp::dict::load_any(path, std::pmr::new_delete_resource())` (the old text also mis-named the
  function), adds that a host's `std::pmr::set_default_resource()` does not affect the dictionary's
  storage, and marks **BREAKING (C-ABI 1.8)**. ⚠️ Edit only that comment's words; never reformat
  `include/fix/c_api/*.h`.

### 7.2 Classification — decided: BREAKING, MINOR 7 → 8

- **Deciding clause.** §X.7's breaking-change definition incorporates `.specify/api-contract.md` §11,
  which lists changing a C-ABI symbol's *"documented ownership, lifetime or reentrancy rule"*; `dict.h`
  documents the resource backing the returned `Dictionary`. A host that installed a counting or
  limiting default resource observes the difference. `version.h`'s own rule ("additive changes bump
  MINOR too") makes the bump unconditional either way; the BREAKING marker follows §11.
- **Re-run at implementation** (the sequencing question is closed on these; re-check before editing):
  - positive control for the `gh` checks first: `gh pr list --state merged --limit 1` must print a
    PR (an unreachable API otherwise reads as "nothing found");
  - `gh release list --exclude-drafts` lists nothing (§X.7's pre-release regime applies);
  - `gh pr list --state open --json number,files --jq '.[] | select(any(.files[]; .path == "include/fix/c_api/version.h")) | .number'`
    prints nothing;
  - `git fetch --all --prune`, then
    `git grep -n "define FIXPP_C_ABI_VERSION_MINOR" $(git for-each-ref --format='%(refname)' refs/heads refs/remotes) -- include/fix/c_api/version.h`
    shows no branch already at 8. Positive control: `origin/main` shows 7.
- **Steps:** `FIXPP_C_ABI_VERSION_MINOR` 7 → 8 in `include/fix/c_api/version.h` with a re-authored
  trailing comment; BREAKING markers in `dict.h`, the PR description and `B-495-3`; every in-repo
  consumer updated in the same PR. The pin population follows
  `.specify/447-458-452-capi-refusals.md` §5b's tiers, re-derived for 1.8:
  `git grep -ln "VERSION_MINOR\|0x010700\|1_7_0\|(7U << 8U)" -- . ':!specs'`, then classify each hit
  (most compare against the macro and move automatically). Known hard pins it must surface:
  `tests/capi/version_test.cpp`'s exact-version cell (name and minor) and `CompositeMacroValue`;
  Tier 2 prose pins such as `src/capi/version.cpp`'s header and `version.h`'s narrative. No error
  code is minted, so `introducing_minor()` and `tools/abi_history/error_codes_v1.txt` do not change.
  Classification of two hits that do **not** move: a `FIXPP_VERSION_MINOR` hit is the **library
  track**, not bumped (R-F: pre-release, `FIXPP_VERSION_*` and the CMake `project()` `VERSION` stay at
  0.0.1; both tracks reset to 1.0.0 at v1.0); `include/fix/c_api.h`'s stale "0.2.0" comment is
  pre-existing and out of scope, left as is. Witness for the re-pin: T-21.
- **Freeze manifest** (`tools/capi_freeze.sha256`, gate `tools/check_capi_freeze.sh`; recorded as
  T-21): edit `dict.h`
  and `version.h`; the gate must **fail** on exactly those headers (positive control;
  `include/fix/c_api.h` carries no C-ABI version literal, so it is not among them); replace those two
  manifest lines with `sha256sum include/fix/c_api/dict.h include/fix/c_api/version.h`; the gate must
  **pass** with the same header count as before.

### 7.3 Who else is affected

C++ callers choose their own load resource, and after D-4 no handle or clone pins their `Dictionary`.
The Python binding wraps `dict_load_from_xml` and gets the fix transparently
(`grep -n "dict_load_from_xml" bindings/python/fixpp.i`).

---

## 8. Alternatives rejected

| Alternative | Why rejected |
|---|---|
| Owner token inside `dict_hooks` | Grows size-pinned `entry_context`, cap-tuned `OffsetTable` and every nested sub-table; a copy per group descent (C-1) |
| `MessageView` / `Parser` holding a `shared_ptr` by value | Safe and could be public, but an atomic RMW pair per inbound message (`[const §VIII.5]` spirit); R-A chose the zero-atomic `detail` route. The recorded route if a public owned route is ever wanted |
| A `weak_ptr` token | Still atomics, plus a lock step |
| Token owned by the `Parser` (via `parse()`'s `[[clang::lifetimebound]]`) | At both copy sites the `Parser` is block-local and its view outlives it |
| Thread a dictionary through factory, pimpl, bridge and `dict::reify` (066 mechanism (a)) | Changes the factory signature; 066 rejected it and O-1 keeps that |
| `shared_ptr<const Dictionary>` pin, rebuild the table per handle | `as_table_view()` is a full walk: slower than the copy, and barred off config time (`[const §XV.1]`) |
| `table_view` refcounted internally | Would share the validator's SC-007 by-value copy too, and change #456's sealed type |
| (i) A table-only owner **copied** at `open()` | One resident table per snapshot-route session; (i′) gets the property without the copy |
| (ii) D-5 alone | Closes the C half only; taken **with** D-4 for the outbound pin D-4 does not reach |
| G2 as an AST-based check | Heavier, a new toolchain dependency in a required job, and still a spelling of the property; T-13's `weak_ptr` arm (C++ twin) witnesses the property itself (PR #262's lesson: do not let a selector stand in for a test) |
| A mallocnesia budget of 1 on the owned arm instead of D-1c | Needs a `--max-allocs` passthrough `fixpp_add_mallocnesia_test` lacks, and leaves "no allocation outside `mr`" false |
| #493 via a shared `reparse_like` helper in `wire` | Sites come in two shapes (refusing `Parser::parse`, degrading raw constructor); a one-line accessor is the same single source of truth |
| #493: a new dict-free `MessageView(frame, mr, Config)` constructor | The four-argument constructor with `none()` already is that route |
| #486: `const&` plus copy | A copy allocates more than a move on MSVC; still terminates |
| #486: drop `noexcept` unconditionally | Loses the promise where it is true |
| #486: `is_nothrow_constructible_v<V, table_view&&>` as the pin | Cannot fail on any toolchain (C-3) |

---

## 9. Public API delta (`[const §XVII.1]`)

| Symbol | Header | Change |
|---|---|---|
| `wire::detail::owned_route_key`, `wire::detail::checked_owner` | `include/fixpp/wire/parser.hpp` | **new**, `detail` |
| `template <class SP> Parser<Mode>::Parser(detail::owned_route_key, SP&&)` (+ deleted rvalue overload) | `parser.hpp` | **new**, `detail`-keyed |
| `Parser<Mode>` layout | `parser.hpp` | +1 private pointer (`owner_`): size and layout change; source-compatible; not across the C ABI |
| `wire::detail::message_view_membership_access::shared_membership` | `parser.hpp` | **new**, `detail` |
| `MessageView<Mode>` | `parser.hpp` | +1 private pointer in `MessageView<Index>` only (an empty `[[no_unique_address]]` member in `Iter`: both copy sites take `MessageView<Index>`), one private member function, befriends `Parser` and the accessor; `MessageView<Index>` grows by one pointer, `MessageView<Iter>` does not; no public signature change; not across the C ABI |
| `OffsetTable::Config OffsetTable::config() const noexcept` | `include/fixpp/wire/offset_table.hpp` | **new**, public |
| `dictionary_driven_validator(table_view)` | `include/fixpp/wire/validator.hpp` | `noexcept` → conditional: unchanged on libstdc++/libc++, **narrowed** on MSVC |
| `dictionary_snapshot` layout | `include/fixpp/dict/dictionary_snapshot.hpp` | inline `table_view` → `shared_ptr<const table_view>`: size, layout and destruction change; construction stays passkey-gated; not across the C ABI |
| `dictionary_snapshot::view_owner() const noexcept` | same | **new**, public |
| `shared_dictionary_view` | same | signature unchanged; returns the snapshot's table owner instead of an alias |

**Compatibility of the three layout rows** (`Parser`, `MessageView`, `dictionary_snapshot`):
source-compatible (no public signature changes); **binary-incompatible** for C++ code compiled
against the old headers, since size and layout change. No C++ ABI gate sees this before the first
public release: `.specify/api-contract.md` §4's Tier 2 ABI comparison starts with the release after
the first one, and `.github/workflows/abi-golden.yml` checks the C archive's symbol set only.

Size pins: `grep -rnE 'sizeof\((fixpp::)?(wire::)?(Parser|MessageView)|sizeof\((fixpp::)?(dict::)?dictionary_snapshot' include src tests bench`
— expected empty; re-run before implementing (positive control: replacing the alternation with
`OffsetTable` prints the cap-band pins).

**Behaviour deltas with no C++ signature change:** reify and clone share on the owned route; the reify
impl comes from `mr`; copies inherit the source's caps; a snapshot's table no longer keeps the
snapshot or its `Dictionary` alive.

**C ABI (§7):** `dict.h`'s doc comment, `version.h`'s MINOR and the freeze manifest change; no
prototype, error code or symbol-golden line. The Python binding exposes none of the C++ symbols above.

**Library version (R-F):** the C++ layout breaks above do **not** bump `FIXPP_VERSION_*` or the CMake
`project()` `VERSION` (0.0.1): pre-release, the library track is not bumped, and it resets to 1.0.0 at
v1.0 together with the C-ABI version.

---

## 10. Tests — TDD order; each RED names the mutation that turns it red

**Conventions.** "RED today" = fails on `3f200360`; a test for a not-yet-existing symbol is RED by
failing to compile (record the error). Mutations run in a **scratch copy**, never the working tree.
"Owned-route parse" = `Parser<Index>{wire::detail::owned_route_key{}, sp}` with `sp` kept alive as
§3.1 requires.

**Comparisons (no new `operator==`).** A `Config` compares member-wise (`max_offset_entries`,
`max_group_entries_per_instance`). A `group_context` compares by `msg_type` **content** plus
`depth` and the `parent_path` prefix `[0, depth)` — `msg_type` is a view into each message's own
buffer, so pointer identity would differ between source and copy.

**Registration (`[const §VII.8]`).** Isolation-safe cells join their module's existing grouped
bucket. T-11, T-13, T-17, T-20 and §5's behavioural fallback (if built) are isolation-sensitive
(threads, sanitizer arms, a global default resource, a TU-local `operator new`) and register as
standalone binaries, each carrying the `495` label besides its module label. T-14 is standalone by
construction (mallocnesia). The shared frame builder `make_oversized_frame_for_clone_test` moves from
`tests/capi/message_write_test.cpp` to `tests/support/` so the C++ reify cells (T-2, T-4, T-5) and
the C cells use one definition.

### #486
- **T-1 — §5's `static_assert`.** RED on the MSVC leg, unfixed tree; Linux mutation `noexcept(false)`.
  Both recorded.

### #493 (before #495, so the cap fix is witnessed on today's copy sites)
- **T-2 — `ReifyEagerMaterialization.RaisedCapDictBackedSourceReifiesUnderItsOwnCaps`.** The
  4100-field `make_oversized_frame_for_clone_test` shape, parsed dict-backed at
  `max_offset_entries = 8192`, reified through the factory. Asserts a value;
  `view().offsets().config()` equals the source's (member-wise); `field_value(49) == "SENDERID"`; equal entry
  counts. RED today: `wire_offset_table_full`. Mutation: S1 back to two arguments.
- **T-3 — `MessageWrite.CloneDictBackedReparseCapExceededYieldsWireLimitExceeded`** renamed
  `…RaisedCapSourceClonesUnderItsOwnCaps`: `FIXPP_ERR_OK`, the clone's 49, equal entry counts;
  source-intact assertions stay. RED today: `WIRE_LIMIT_EXCEEDED`. Mutation: S2 back to two arguments.
- **T-4 — dict-free fallbacks.** `MessageWrite.CloneDictFreeOversizedSourceStillReturnsOk` gains
  `fixpp_msg_get_string(clone, 49) == "SENDERID"`; a C++ twin reifies a dict-free raised-cap source
  and reads 49. Both also assert
  that `copy.offsets().group_context_for(t)` equals `source.offsets().group_context_for(t)` (by the
  comparison rule above) for a `Parser{}`-parsed source and any count tag `t`. RED today (empty copy). Mutations: S3, S4 each back
  to two arguments (the context assertion also goes RED).
- **T-5 — the whole `Config`, both directions.**
  - (a) **Raised:** a dict-backed source with raised `max_offset_entries` and raised
    `max_group_entries_per_instance`, carrying one group instance longer than the default per-instance
    cap; clone and reify each read that group. Mutation: inherit only `max_offset_entries` — the
    copy's group read fails.
  - (b) **Lowered** (required: `B-493-1` states this direction): a C++ dict-backed source parsed with
    `max_group_entries_per_instance` below one instance's length. Source: scalar read succeeds, group
    read fails. Reify copy, and a clone through the internal seam the existing raised-cap clone tests
    use: scalar succeeds, group read fails identically. Mutation: re-parse at the default `Config` —
    the copy's group read succeeds.
- **T-6 — `MessageWrite.CloneOfUnbuiltOversizedSourceYieldsWireLimitExceeded`** (regression pin,
  GREEN before and after). After T-3 nothing else witnesses clone → `WIRE_LIMIT_EXCEEDED`
  (`grep -rn WIRE_LIMIT_EXCEEDED tests/capi`: besides T-3, only translate-table and read-path hits).
  Source: the 4100-field frame through the raw dict-backed `MessageView` constructor at the default
  cap, so its own build failed (the lever `…MalformedFieldYieldsWireInvalidFrame` uses). Asserts the
  exact code, `clone_out == NULL`, unchanged source. What only T-6 witnesses: after #493 a clone's
  re-parse still refuses under the source's own (default) caps. Mutation (turns T-6 RED, the clone
  succeeding with `FIXPP_ERR_OK`): the clone re-parses with a `Config` other than the source's, e.g. a
  raised `max_offset_entries`. Mapping the error through the constant `FIXPP_ERR_WIRE_LIMIT_EXCEEDED`
  instead of `translate()` is an **equivalent mutant** for T-6; the `translate()` delegation is
  witnessed by `MessageWrite.CloneDictBackedReparseMalformedFieldYieldsWireInvalidFrame`, which
  expects `FIXPP_ERR_WIRE_INVALID_FRAME` and goes RED under that constant.

### #495
- **T-7 — constructor constraints**, `static_assert`s in a wire test TU. With
  `K = wire::detail::owned_route_key`, `S = shared_ptr<const table_view>`, rows
  `is_constructible_v<Parser<Index>, K, X>` for X = `S&` (true), `S const&` (true), `S&&` (false),
  `S const&&` (false), `shared_ptr<table_view>&` (false). Discriminating mutations (measured in a
  scratch TU on clang 22.1.2 and GCC), each turning the named rows RED:
  - drop `same_as` → the `shared_ptr<table_view>&` row;
  - drop the `is_lvalue_reference_v` conjunct **and** the deleted overload → the `S&&` and
    `S const&&` rows;
  - parameter `SP&`, constraint reduced to `same_as`, no deleted overload → the `S const&&` row.

  Deleting only the overload, or only the conjunct, is an **equivalent mutant** (each alone suffices)
  and is not listed as a witness.
- **T-8 — `shared_membership()` arms** (`MessageViewSharedMembership.*`, through the accessor):
  owned — `s.get() == sp.get()`, `use_count` +1; borrowed — `s.get() != sp.get()` and the same
  `field_valid_for` over the frame's tags; dict-free — `nullptr`; **reassigned owner, old table
  alive** — hold `old = sp`, parse, reassign `sp` to a table built from a **different** dictionary,
  then assert `s.get() != sp.get()`, `s.get() != old.get()`, and a membership only the old dictionary
  has. Mutations: always copy (owned arm RED); drop the identity check (reassigned arm RED:
  `s.get() == sp.get()`).
- **T-9 — reify shares.** Owned-route source, reified: `handle.view().hooks().opaque_dict() == sp.get()`;
  reifying `handle.view()` again yields the same address. RED today. Mutation: arm 1 → arm 2.
- **T-10 — clone shares.** Via `capi_internal.hpp`: `clone->owned_tv_.get() == sp.get()`, and a clone
  of the clone shares. RED today (type mismatch, then address). Mutation: the clone site seats
  `owned_tv_` through arm 2 (a copy) instead of arm 1 — the address equality goes RED.
- **T-11 — a pinned table outlives the `Dictionary` and its load arena** (ASan and TSan).
  - Setup: heap-allocate a `monotonic_buffer_resource` and its buffer (so a late deallocation is a
    heap-use-after-free, not stack-use-after-scope); load the `Dictionary` into it;
    `snap = make_dictionary_snapshot(dict)`, `sp = shared_dictionary_view(snap)`; owned-route parse of
    a NoLegs frame in a separate parse arena; reify into a handle arena that lives to the end; clone.
  - Sequencing: the second thread is started holding the handle and the clone, and blocks on a
    `std::latch`. The main thread destroys everything else in order — source view, frame, parse arena;
    then `sp`, `dict`, `snap`; last the load arena — and then counts the latch down. The second thread
    reads NoLegs, membership-bounded, from handle and clone, drops both there, and the main thread
    joins it.
  - Asserts: both reads succeed; ASan and TSan clean.
  - **RED arm (ASan):** the **alias mutant** — in `shared_dictionary_view`, take
    `table_view const* p = snap->view_owner().get();` before moving `snap`, then return the aliasing
    construction over `std::move(snap)` and `p`. The second thread's drop runs `~Dictionary` into the
    destroyed arena. (This mutant is also T-13's and T-19's.)
- **T-12 — borrowed-route survival** (`GroupMembershipSurvivesSourceDestruction`, `Parser{tv}`),
  unchanged and green: the borrowed route keeps its self-contained copy. A **regression pin by
  design** (GREEN before and after); it has no RED of its own in this change.
- **T-13 — shipped route, real dispatch** (066 Decision 6). Two twins; each asserts on the
  **unmutated** tree first, so a false RED is loud before any mutant runs.
  - **C++ twin.** A `Session` given `SessionConfig::dict_snapshot`; `Application::fromApp` reifies two
    consecutive inbound messages into handles whose `mr` the test owns outside the Session and the
    application. Capture `std::weak_ptr<const Dictionary>` first. Assert both handles'
    `view().hooks().opaque_dict()` are equal (RED today: separate copies). Then drop every anchor the
    test holds — each `SessionConfig` copy, the snapshot, the `Dictionary`, and
    `EngineConfig::dictionaries` if an `Engine` is used (it pins the `Dictionary`) — and destroy the
    Session/Engine. Assert `weak.expired()` while both handles live, then read a group from each.
  - **C twin** (engine loopback). Load via `fixpp_dict_load_from_xml`. The recv callback clones two
    inbound handles; assert their `owned_tv_` compare equal and equal `sess->tv_` (sharing). Then
    destroy the engine, every session config, and the test's `fixpp_dict_t`, and read a group from
    each clone under ASan. **No `weak.expired()` assertion:** expiry is moot on the C route (§3.4,
    Q-6).
  - Mutations: revert `pd_parser` to `{*inbound_tv_}` (address equality RED in both twins); the T-11
    alias mutant (`weak.expired()` RED in the C++ twin).
- **T-14 — the owned route performs no global-heap allocation.**
  - New binary `tests/alloc_guard/test_reify_owned_alloc_guard.cpp`, dual gate per
    `test_dict066_grouped_read_alloc_guard.cpp` (TU-local `operator new` counter compiled out under
    `FIXPP_SANITIZER_REPLACES_NEW`, plus mallocnesia markers).
  - Setup outside the window: owned-route parse; `mr` a `monotonic_buffer_resource` over a stack
    buffer with `null_memory_resource()` upstream (overflow → `dict_reify_oom`, not the heap). Window:
    `dict::reify(view, profile, &mr)`, a field read, the handle's destruction (`sp` still held).
  - `ReifyOwnedAllocGuard.OwnedRouteZeroGlobalHeap`: counter delta 0.
    `ReifyOwnedAllocGuard.BorrowedRouteAllocates`: the same through `Parser{*sp}`, delta > 0 when the
    counter is compiled in.
  - Registration (`tests/alloc_guard/CMakeLists.txt`): one plain `add_test`;
    `fixpp_add_mallocnesia_test(NAME reify_owned_alloc_guard_mallocnesia … ENVIRONMENT GTEST_FILTER=*OwnedRoute*)`;
    `fixpp_add_mallocnesia_test(NAME reify_borrowed_alloc_guard_expected_violation_mallocnesia … EXPECT_VIOLATION ENVIRONMENT GTEST_FILTER=*BorrowedRoute*)`.
    Both `LABELS "495;alloc_guard"`; the helper adds `mallocnesia`. The name must **not** contain
    `positive_control`: `tools/check_mallocnesia_population.py` requires exactly one such label
    member, and `alloc_guard_positive_control_mallocnesia` is it. The checker's `--min-gates` floor
    needs no edit for an added gate.
  - RED: on today's tree, compile failure; with sharing but the impl still `new`ed, the owned gate.
    Mutations, each RED alone: revert D-1c; replace arm 1 with arm 2.
- **T-15 — the inbound parse stays zero-alloc.** `test_dict066_grouped_read_alloc_guard.cpp` mirrors
  `parse_and_dispatch_`'s construction; each test gains an owned-route arm (borrowed arms stay),
  covered by the existing `dict066_grouped_read_alloc_guard_mallocnesia` registration.
  `wire_alloc_guard_test_mallocnesia` covers the dict-free path. ⚠️ No mallocnesia gate covers
  `Session` dispatch itself (`alloc_guard_dispatch` / `alloc_guard_session` are deliberately ungated:
  their windows wrap `co_spawn` / `ioc.run()`); this change stores one more pointer and does not
  widen that gap. Mutation: the owned `parse()` path allocates on the global heap (e.g. it seats the
  view's owner through a `std::make_shared` table copy instead of the owner's address) — the owned
  arm's counter goes RED.
- **T-16 — OOM recalibration by phase.**
  - **Population recipe** (every factory / `dict::reify` / `from_frame` call made under a bounded or
    failing `mr`, not one lever):
    `grep -rlE "failing_pmr|fail_on_call_n|null_memory_resource" tests bench | xargs grep -lE "reify|from_frame"`,
    then read **each call site** in each hit and classify which allocation it fails today and under
    D-1c. Positive control: `tests/dictionary/reify_dispatch_test.cpp` must be listed. Hits that feed
    only `Parser::parse` or go through `owning_<Msg>::from_view` (e.g. `reify_oom_test.cpp`) are
    recorded as out of population, with the call read.
  - **(a) Probe.** `owning_message_handle_from_frame(rmv, wire::MessageView<Index>{}, &r)`, `r` a
    counting `failing_pmr_resource` (`fail_on_call_n = 0`) over an empty default view; the count is
    `I`. Stated precondition: on the empty path `bytes_.assign` of nothing, the zero-cap
    `pmr_carry_buffer`, the `Framer` and the default `view_cache_.emplace()` allocate nothing from
    `mr`. The probe asserts `I == 1` (the impl's `new_object`), which also witnesses the
    precondition. `FIXPP_SKIP_ON_MSVC_DEBUG_ARENA` applies (MSVC debug pmr containers draw proxies
    from `mr` inside the impl constructor). For a real frame, `bytes_` is call `I+1` and the first
    `OffsetTable`-build call `I+2` (a complete frame never appends to the carry).
  - **Recalibrated cells** — each names its phase in its comment, never an ordinal:
    - `ReifyErrorContract.DeepCopyOomYieldsReifyOom`: its zero-buffer
      `monotonic_buffer_resource{null_memory_resource()}` would fail at the **impl** under D-1c and
      stay green while measuring the wrong allocation. It becomes a counting `failing_pmr_resource`
      failing call `I+1` (`bytes_`), expecting `dict_reify_oom`.
    - `ReifyErrorContract.ViewRebuildOomDegradesNotTerminate` fails `I+2` (its "alloc #1 / alloc #2"
      comment is rewritten by phase); `SpuriousHitControl_…` fails `I+1`.
    - **Impl arm** (new): failing call `I` yields `dict_reify_oom`, no handle, no leak under ASan.
    - Mutation: revert D-1c — `I` becomes 0 and the probe's `I == 1` goes RED.
  - **(b) Global-new cells.** `reify_membership_copy_oom_test.cpp` arms `fail_at = t_dict` (premise:
    the copy's K allocations are the last K); `dict066_clone_membership_copy_oom_test.cpp` arms
    `dict_total - 1` (premise: exactly one further global allocation, the `make_unique<MessageView>`).
    Arm 2's in-place spelling keeps both premises **for arm 2**, but the `reify_membership_copy_oom_test`
    calibration can also move because of **D-1c**: the impl now sits in the handle arena's first
    block (a default-constructed `monotonic_buffer_resource` over the counted global heap), so the
    re-parse after the copy may force a chunk refill — a counted global `new` after the copy, landing
    `fail_at = t_dict` in the re-parse. Second premise, stated: the handle arena's post-copy
    allocations fit the chunk already acquired; recalibrate at implementation if the witness below
    shows otherwise. That test's header derivation (the function does only `return handle;` after the
    copy) is stale since #458 D-4 moved the eager re-parse into the factory; fixed at implementation.
    Both cells are libstdc++-only (`FIXPP_OOM_WITNESS_ENABLED` requires `__GLIBCXX__`); other STLs
    compile them out. Witness (libstdc++): a `gdb` backtrace at each armed ordinal lands inside `table_view`'s copy
    constructor. Mutation: the prvalue spelling
    `make_shared<const table_view>(membership_copy())` — the ordinal lands in `__allocate_shared`.
  - `FIXPP_SKIP_ON_MSVC_DEBUG_ARENA` still applies on MSVC debug throughout.
- **T-17 — concurrent readers of one shared table** (TSan). Two handles reified over the same `sp`
  (assert `opaque_dict()` equality first); a `std::barrier` releases two threads, each looping over
  membership-bearing group reads plus `unknown_fields()` / classification reads on its own handle.
  Pass: TSan clean. Mutation: a `mutable` lazily-filled cache in a `const` `table_view` accessor on
  that path — TSan reports a race.

### D-4 / R-C
- **T-18 — G2 seeded positives** (`tools/test_dictionary_snapshot_exclusivity_gate.sh`). The
  self-test copies the scanned directories (`src include bindings tools tests`) to a `mktemp -d`
  directory and runs every case — clean, A5 and seeds — on that copy through the gate's root
  override (`--root DIR`, or `FIXPP_GATE_ROOT`; default `git rev-parse --show-toplevel`). The source
  tree is never edited, so the ctest carries no `RUN_SERIAL` or resource lock. Clean copy: exit 0
  and `G2 matches of the enumerated spellings = 0`. Seeds 1 (`(std::move(`) and 2 (`(identifier,`)
  appended to the copy's `src/dictionary/dictionary_snapshot.cpp`. Each seed: exit 1 **and**
  `G2 FAIL` in the log (G1 runs first, so a bare exit code is not enough). Seeds follow §6.4's
  spelling rule. Mutation: revert the assertion to `-eq 1` — the clean-copy case goes RED.
- **T-19 — amended control-block pins.**
  - (a) `DictionarySnapshot.SharedDictionaryViewAliasesRatherThanCopies`
    (`tests/dictionary/dictionary_snapshot_test.cpp`), renamed for sharing the table owner. Keeps
    `owner.get() == &snap->view()` (identity discriminates; "a different control block" is true of a
    copy too), `use_count() == 1` and the post-reset read; drops the two `owner_before` assertions;
    **adds** the (i′) witness: sample `dict.use_count()` before minting the snapshot; after
    `snap.reset()` with the owner alive, the count returns to the sample. Mutation: T-11's alias
    mutant. ⚠️ The TU's five `static_assert` blocks A1–A5 stay byte-for-byte (the gate self-test's awk
    requires exactly five).
  - (b) `SessionTableViewReuse.OpenAdoptsAConfigSuppliedSnapshotAndWalksZeroTimes`
    (`tests/session/test_session_table_view_reuse.cpp`) samples
    `shared_dictionary_view(snap).use_count()` before and after `open()` (both samples include the
    temporary's reference). Mutation: `open()` copies the table
    (`make_shared<const table_view>(snap->view())`) — the rise disappears.
  - (c) `CapiGroupDelimiterCtx.SessionHandleAliasesDictionarySnapshotControlBlock`
    (`sess->tv_.use_count() > 1`) stands; its name and message drop "alias". It tells a share from a
    copy, not an alias from a share (the registered `SessionConfig::dict_snapshot` also holds the
    snapshot); the property is T-13's.
  - (d) **`view_owner()`'s shape**, in `test_session_table_view_reuse.cpp` (not in A1–A5):
    `static_assert(std::same_as<decltype(std::declval<fixpp::dict::dictionary_snapshot const&>().view_owner()), std::shared_ptr<const fixpp::dict::table_view> const&>);`
    Mutation: the accessor returns `std::shared_ptr<fixpp::dict::table_view>`.

### D-5 / R-D
- **T-20 — `CapiDictionary.LoadDoesNotRetainInstalledDefaultResource`** (`tests/capi/`). Install a
  `memory_resource` tracking bytes currently held, behind an RAII guard restoring the previous
  default; call `fixpp_dict_load_from_xml`; assert held bytes after return are 0 (not a total count:
  a transient `pmr` temporary may allocate and free). Then `fixpp_dict_destroy`, restore; ASan. RED
  today: held bytes > 0. Mutation: revert to `get_default_resource()`. Still RED after the one-line
  fix → the loader has a `get_default_resource()` fallback for retained storage: a finding in the
  loader.
- **T-21 — C-ABI version and freeze pins:** §7.2's pin population updated; its fail-then-pass freeze
  sequence recorded in the verify record. Mutations: `FIXPP_C_ABI_VERSION_MINOR` back to 7 — the
  exact-version cell in `tests/capi/version_test.cpp` goes RED; `dict.h` edited without re-pinning
  its manifest line — `tools/check_capi_freeze.sh` fails on that header.

---

## 11. Bench plan (`bench/dictionary/reify_bench.cpp` only; no bench CMake edit)

Baseline on `3f200360` **before any edit** (owner).

| Row | Route | Frame | Role |
|---|---|---|---|
| `BM_Reify_DictBacked_20tag` (existing) | borrowed `Parser{tv}` | existing, < 20 fields | **the** borrowed no-regression witness: base-vs-head, ≤ +5% (`[const §VIII.2]`) |
| `BM_Reify_Dispatch_20tag` (existing) | returns before dispatch | no MsgType | base-vs-head, no regression |
| `BM_Reify_DictBacked_Owned_20field` (**new**) | owned route | v44 NewOrderSingle, ≥ 20 fields valid for `D` | NFR-003-3's **1.2 µs** ceiling (cannot build at base) |
| `BM_Reify_DictBacked_Borrowed_20field` (**new**) | borrowed, same frame | same | **informational only**: shows the copy's cost on the owned row's frame; not a regression witness |

- The existing borrowed row is an adequate no-regression witness because this change's borrowed-route
  deltas are constant-cost and do not scale with frame length: arm 2's control block shares the
  copy's allocation; D-1c moves the impl into `mr`; the copy scales with dictionary size, not frame.
- ⚠️ **Attributing a reading on that row:** its 4 KiB `reify_buf` arena has a `new_delete` upstream.
  If the impl no longer fits under D-1c the row spills to the heap, and that shows as a D-1c cost.
  Check spill before attributing a +5% reading; a slowdown past +5% goes to non-author approval.
- **New rows' setup checks** (`SkipWithError`): `parsed->offsets().entries().size() >= 20`; one
  untimed reify must succeed and, on the owned row, `handle.view().hooks().opaque_dict()` must equal
  the owner's pointer (proves the timed loop reaches the owned path, not an early return); the stack
  arena has `null_memory_resource()` upstream, so an overflow is a refusal, not a silent heap
  allocation in the timed loop (`k20TagBufSz` is not assumed to fit).
- **Arena margin rule:** the arena is sized at **twice** the bytes one untimed reify draws from a
  counting resource over the same frame, measured in setup; setup fails with `SkipWithError` if the
  measured draw exceeds half the arena.
- **Pre-registered disposition** if `BM_Reify_DictBacked_Owned_20field` reads **above 1.2 µs** on the
  reference machine (this dev box: WSL2, pinned CPU 3, `linux-clang-release`): it escalates to the
  owner for a ruling — either NFR-003-3 is narrowed further or the design is revisited. It is not
  waived by the implementer.
- `bench/ci-suite.txt` gains a comment line (no CMake change) stating what the borrowed route is held
  to: no regression against the merge-base on `BM_Reify_DictBacked_20tag`.
- `bench/wire/parser_bench` also runs base-vs-head (`MessageView` grew, `Parser` stores a pointer);
  it is CI-`paired`.
- **Procedure:** `linux-clang-release`, A-B-A-B, min-per-tree, one machine; raw JSON in the verify
  record. `reify_bench` is `none:` / `no` in `bench/ci-suite.txt` (`grep -n reify_bench bench/ci-suite.txt`),
  so the paired manual run is the only instrument (`[const §VIII.2]`'s partial-coverage clause).

---

## 12. Spec, B&L, catalogue, brain and comment deltas

**NFR-003-3** (`specs/003-dictionary-codegen/spec.md`), amended in place with an *"Amended
(fixpp#495, <date>)"* note — the date is the implementation commit's, filled in then — scoped to
`dict::reify` only:
> ≤ 1.2 µs (20-tag), and no allocation outside `mr`, when `dict::reify` consumes a view delivered by
> the shipped `Session` dispatch path, or the `view()` of a handle derived from one. When it consumes
> a view the caller parsed through a borrowed `Parser{tv}`, `dict::reify` deep-copies the membership
> table and is not held to the ceiling (`L-495-1`); it is held to no regression against the
> merge-base on `BM_Reify_DictBacked_20tag`.

Witnesses: the §11 owned row (ceiling); T-14 (no global allocation); §11's existing borrowed row (no
regression). `fixpp_msg_clone` takes no `mr` and is not covered by NFR-003-3; its sharing is `B-495-1`.

**066 records.** `research.md` Decision 4 and `data-model.md`'s "Reify owning handle owned table_view"
/ "Clone-owned table_view" entities get an in-place *"Superseded in part by
`.specify/495-493-486-dict-reify-copy.md`"* note; the history stays.

**`.specify/215-dictionary-view.md`** header pointer: *"The aliasing design (§3's
`shared_dictionary_view`, §5b's 'third owner of the snapshot's control block', §6 seam 7's G2) is
superseded by `.specify/495-493-486-dict-reify-copy.md` §6 (D-4): the snapshot owns its table in its
own control block, and G2 asserts zero matches of its enumerated spellings."*

**B&L** (`spec/behaviors-and-limitations.md`), new section; each row names its witness:
- **`B-495-1`** — on the shipped dispatch path, `dict::reify` and `fixpp_msg_clone` share the source's
  table by reference count; a live handle or clone keeps that table alive, never the `Dictionary`
  (C++ route). On the C ABI only the sharing is claimed (why: §3.4, Q-6; cost filed as fixpp#501).
  *Witness: T-9, T-10, T-13 (C++ twin: sharing + expiry; C twin: sharing).*
- **`L-495-1`** — a source parsed through a borrowed `Parser{tv}` still deep-copies the table per
  handle, above NFR-003-3's ceiling and scoped out of it. *Witness: T-12; §11 borrowed rows.*
- **`B-495-2`** (D-4) — a held view of a `dictionary_snapshot`'s table does not keep the snapshot or
  its `Dictionary` alive; the table itself lives as long as the view. *Witness: T-19(a), T-11.*
- **`B-495-3`** (D-5), **BREAKING (C-ABI 1.8)** — `fixpp_dict_load_from_xml` allocates from
  `std::pmr::new_delete_resource()`; a host's installed default resource no longer backs a C
  dictionary. *Witness: T-20.*
- **`B-493-1`** — clone and reify re-parse under the source's `OffsetTable::Config`, so a copy keeps
  **both raised and lowered** caps, including a lazy group-read failure under a lowered group cap. A
  raised-cap source now copies, including through the dict-free fallbacks (which returned an empty
  copy); a dict-free copy's root group context matches the parsed-source form;
  `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` from clone is reachable only from a source whose own build failed.
  *Witness: T-2, T-3, T-4, T-5(a)/(b), T-6.*
- **`B-486-1`** — `dictionary_driven_validator`'s constructor is specified
  `noexcept(std::is_nothrow_move_constructible_v<table_view>)`: `noexcept` exactly where `table_view`'s
  move is nothrow, so on a toolchain whose move can throw (MSVC) the constructor does not promise
  `noexcept`. The row claims the specification only. *Witness: T-1.*
- **`L-458-2`** moves to `spec/behaviors-and-limitations-closed.md`, resolved by fixpp#493.
- **`L-456-2`** stays live; its ⚠️ paragraph changes: the validator leaves "who already paid it" (now
  conditional, fixpp#486); `optional::emplace` stays as a generic move path, scoped *"no production
  copy site uses it after fixpp#495"*. Condition: `grep -rnE "optional<.*table_view|owned_tv_\.emplace" src include`
  has no hit outside a comment (`tests/` hits are generic move-path tests, allowed); the snapshot's member move is replaced by its move into the
  `make_shared` block (same count). Borrowed arm 2 adds no move path.

**Catalogue** (`spec/feature-catalogue.md`): dated notes on CA-009 (clone), the wire rows carrying the
066 / 458 notes, W-014 (validator), the 215 row (D-4) and the C-ABI dictionary row (D-5); no new rows.
Rows: `grep -n "066-dict\|#458\|090-capi\|215\|fixpp_dict_load" spec/feature-catalogue.md`.

**Brain:** `brain/components/dictionary.md` — *"The reify handle materialises EAGERLY"* gains the owned
route and table pin; the ⚠️ residuals line drops `L-458-2`; its `.specify/215-dictionary-view.md`
entries flag the alias design (incl. §5b's "third owner") as **superseded in part** by this note.
`brain/components/c-api.md` — the clone entry likewise, and the loader entry for D-5. Each component
index lists this note.

**`.specify/api-contract.md` and `.specify/architecture.md` (R-E, R-F):**
- api-contract §2's Internal tier and its §3.3 "Detail headers …" sentence: `detail/` headers are
  **installed** (public headers include them) and are not for clients; drop "excluded from the
  install set". The Internal class names nested `<module>::detail` namespaces in installed headers
  and `detail` tags such as `wire::detail::owned_route_key` explicitly.
- architecture §9.1's "Detail headers are excluded from the install set" gets the same correction.
- api-contract §4 gains the library-track pre-release clause: before the first public release a C++
  layout or signature break does not bump `FIXPP_VERSION_*` or the CMake `project()` `VERSION`;
  both tracks reset to 1.0.0 at v1.0.

**Superseding header comments** (parent `CLAUDE.md` rule): `impl::owned_tv_`; `fixpp_msg::owned_tv_`;
`membership_copy()`'s "the ONE accessor" comment; `Session::inbound_tv_` (with §2.6's invariant);
`dictionary_snapshot.hpp`'s header.

**Stale prose to rewrite** (it becomes false after D-4). Recipe:
`grep -rn -i "alias\|only as\|exactly ONE production site\|LISTED SPELLINGS" include/fixpp/dict/dictionary_snapshot.hpp src/dictionary/dictionary_snapshot.cpp src/session/session.cpp include/fixpp/session/session.hpp src/capi/session.cpp src/capi/capi_internal.hpp tests/dictionary/dictionary_snapshot_test.cpp tests/session/test_session_table_view_reuse.cpp tests/capi/capi_group_delimiter_ctx_test.cpp tools/check_dictionary_snapshot_exclusivity.sh`
— positive control: it hits every one of these files at `3f200360`. The self-test
(`tools/test_dictionary_snapshot_exclusivity_gate.sh`) has no G2 prose today, so the grep cannot
see it: read its header by hand ("printed liveness counts" loses its G2 half with `G2 DEAD`), and
T-18's new case descriptions are new prose under the same rule. What the grep finds includes:
`dictionary_snapshot.hpp`'s class comment (the aliasing pointer; "exposes the view only as
`table_view const&`", false once `view_owner()` exists) and the helper's "ONE production site"
comment; `dictionary_snapshot.cpp`'s helper comments incl. "Spelled out on purpose … G2 counts" and
its `NOLINTNEXTLINE`, which lose their reason; `session.hpp`'s `inbound_tv_` comment; `session.cpp`'s
`open()` comment; `fixpp_session_open`'s comment; `capi_internal.hpp`'s `session_tv_` / `owned_tv_`
comments; the three tests' names and messages (T-19); the gate script's G2 header ("exactly ONE
production site", "exact-count grep"). All rewritten prose
obeys §6.4's spelling rule.

---

## 13. Verification plan

Builds are owner-approved before they run (`[const §XVII.7]`); check `df -h /mnt/e` first.

1. **`linux-clang-debug`, targeted, by label (`[const §VII.8]`: `ctest -L`, never `-R`).** The cells
   land in these ctest buckets, as registered at implementation (re-derive with
   `ctest --show-only=json-v1`):
   - existing grouped buckets: `dictionary_reify_tests` (T-2, T-4 twin, T-5 reify arms, T-9, T-16(a);
     label `dictionary`), `dictionary_pure_tests` (T-19(a); `dictionary`), `wire_dict_tests` (T-7,
     T-8; `075;wire`), `wire_pure_tests` (T-1; `wire`), `capi_message_write` (T-3, T-4, T-5 clone
     arms, T-6, T-10; `capi`), `capi_pure_tests` (T-19(c), T-21; `capi`), `session_table_view_reuse`
     (T-19(b),(d); `session`), `dict066_grouped_read_alloc_guard` (T-15; `alloc_guard`); the T-16(b)
     standalones `dictionary_reify_membership_copy_oom_test` and
     `capi_dict066_clone_membership_copy_oom` (`066`);
   - new standalone binaries, each labelled `495`: `capi_dict495_pinned_table_lifetime` (T-11;
     `495;capi;tsan` — in `tests/capi/` because it clones through the C ABI),
     `session_reify_shared_dispatch` (T-13 C++; `495;session`), `capi_dict495_clone_shared_table`
     (T-13 C; `495;capi`), `reify_owned_alloc_guard` and its two mallocnesia entries (T-14;
     `495;alloc_guard`), `dictionary_reify_shared_table_concurrency_test` (T-17;
     `495;dictionary;tsan`), `capi_dict495_loader_default_resource` (T-20; `495;capi`);
   - T-18 is the unlabelled `dictionary_snapshot_exclusivity_gate` ctest (it runs
     `tools/test_dictionary_snapshot_exclusivity_gate.sh` on a temp copy), also run directly in
     step 7; `git status` stays clean across the ctest run.
   Run `ctest -L '495|dictionary|capi|session|wire|alloc_guard|066'`.
2. **Mallocnesia, point 1** (owner): after implementation, before `/simplify` and the verify record,
   on the Linux non-sanitizer preset (`build/<preset>/lib/libmallocnesia.so`):
   - `python3 tools/check_mallocnesia_population.py --build-dir build/<preset> --min-gates 20`
     (tier1's invocation; must pass with exactly one `positive_control`);
   - `ctest -L mallocnesia`: T-14's owned gate passes; its `expected_violation` entry is caught;
     `alloc_guard_positive_control_mallocnesia` is caught (interceptor live).
3. **Sanitizers:** ASan, UBSan, TSan, targeted to T-2…T-20. TSan required for T-11 and T-17; the
   alias-mutant RED arms (T-11, T-13) run on ASan.
4. **Bench:** §11, `linux-clang-release`, against the pre-edit baseline.
5. **Coverage:** `linux-clang-coverage`, targeted, over the changed functions in `reify.cpp`,
   `message_write.cpp`, `parser.hpp`, `offset_table.hpp`, `dictionary_snapshot.cpp`,
   `dictionary_snapshot.hpp`, `src/capi/dictionary.cpp`, `session.cpp` and `validator.hpp`; every new
   branch covered, incl. `shared_membership()`'s reassigned-owner arm. **`[const §IX.1]` waiver,
   pre-registered:** `detail::checked_owner`'s `assert(owner)` false branch is a violated precondition
   (a debug abort), not exercised by any cell; covering it needs a death test for a `detail`
   precondition, which this note does not add.
6. **MSVC local** (parent repo `research/G19-fix-fpml-iso20022/msvc-local-build-procedure.md`; toolset
   from `CMakeCache.txt`'s `CMAKE_LINKER`): T-1 fails to compile unfixed and compiles fixed (decides
   Q-2); T-16(a)'s cells run on `windows-msvc-release`. T-16(b)'s OOM witnesses are libstdc++-only
   (`FIXPP_OOM_WITNESS_ENABLED` requires `__GLIBCXX__`, a guard that predates this note): on MSVC
   their binaries link no test case, so an MSVC "passed" carries no information for them.
7. **Gates:** `bash tools/test_dictionary_snapshot_exclusivity_gate.sh` (T-18's seeds RED with
   `G2 FAIL`, clean copy green); `bash tools/check_dictionary_snapshot_exclusivity.sh`
   (`G2 matches of the enumerated spellings = 0`, G1 lines unchanged); `bash tools/check_capi_freeze.sh`
   (§7.2 fail-then-pass); `.claude/scripts/check-comment-claims.py --root <tree> --base origin/main`;
   `check_line_citations.py --shift-audit origin/main..HEAD`.
8. **Mallocnesia, point 2** (owner): re-run step 2 in full, population checker included, after
   `/gate-b`'s fixer rounds, before merge.
9. **Index:** `codegraph sync` in this worktree after each code-changing phase.

---

## 14. Not in scope

Sharing the validator's table (SC-007, §5); unifying `fixpp_msg::session_tv_` with `owned_tv_`;
`L-458-1`; the `BM_Reify_Dispatch_20tag` placement effect (owner-approved in #494); the MSVC cost of
`table_view` moves in `as_table_view()` / `build() &&`; `owning_<Msg>::from_view` / `reify_as`;
making `vg_parser` owned; a mallocnesia gate over `Session` dispatch (T-15); a public owned route for
C++ callers' own parses (§8); other `get_default_resource()` captures (§7.1).

---

## 15. Open questions

- **Q-2.** Does MSVC evaluate §5's prvalue `noexcept` form as the standard requires? The MSVC leg
  (§13 step 6) decides; if not, §5's behavioural fallback stands in.

Closed on evidence in round 2, now decisions: Q-4 (C-ABI MINOR sequencing) → §7.2, with its
re-run-at-implementation checks; Q-5 (G2 scope) → §6.4, tree-wide; `view_owner()` vs a friend →
§6.2. Earlier: Q-1 → O-4; Q-3 → R-A.

---

## Normative References

Included voluntarily (as in `.specify/447-458-452-capi-refusals.md` and
`.specify/456-table-view-seal.md`): `[const §VI.5]` binds the section to `/specify` artifacts only.

**Normative FIX references: NONE.** The changes decide fixpp-owned policy (table ownership, re-parse
caps, an exception specification, a loader's resource); no wire behaviour, grammar, session FSM or
dictionary semantics change, and no `spec/coverage-index.md` entry informs them.

| citation | as used here |
|---|---|
| `[const §XVII.1]` | *"Touches the public C++ API or C ABI"* / *"… parser, or codegen layout"*: Gate A triggers |
| `[const §X.1]` | the C ABI is a versioned contract; Codex Gate A mandatory: D-5 |
| `[const §X.6]` | ABI-affecting features trigger the four Appendix A controls: §0.2 |
| `[const §X.7]` | pre-release breaking C-ABI changes allowed but declared, with a MINOR bump: §7.2 |
| `[const §VIII.2]` | a slowdown > +5% on a paired base-vs-candidate run needs approval: §11 |
| `[const §VIII.5]` | zero `new`/`delete` between parse and `fromApp`: why the token is a pointer (§2.1) |
| `[const §VII.7]` | parser-touching code without a fuzz harness blocks Gate B: §0.2 |
| `[const §XV.1]` | no per-message heap allocation on the hot path: D-4 is config-time (§6.3) |
| `[const §VI.5]` | Normative References required of `/specify` artifacts: why this is voluntary |

**Inherited design contracts:** SC-007's by-value validator; 066 Decisions 4 and 6;
`.specify/215-dictionary-view.md`'s Option C (passkey, provenance check), whose alias mechanism §6
supersedes; `.specify/456-table-view-seal.md`'s no-assignment rule; `.specify/api-contract.md` §3.3
(Internal) and §11 (C-ABI effect list).

---

## Gate A

- Round 1 applied 2026-09-23: Codex P1=3 P2=4 P3=3; Opus post-judging P1=1 P2=5 P3=8; rewrite addresses root causes 1-4 + owner rulings R-A..R-D (Fable consult b13-reify-handle-pins-dictionary-resource). Reviews: `research/G19-fix-fpml-iso20022/research/reviews/codex_495_493_486_1_dict-reify-copy_review.md`, `research/G19-fix-fpml-iso20022/research/reviews/opus_495_493_486_1_dict-reify-copy_triage.md` (parent repo).
- Round 2 applied 2026-09-23: Codex P1=0 P2=5 P3=4; Opus post-judging P1=0 P2=4 P3=11; rewrite addresses root causes A-D + P3s + consolidation. Reviews: `research/G19-fix-fpml-iso20022/research/reviews/codex_495_493_486_2_dict-reify-copy_review.md`, `research/G19-fix-fpml-iso20022/research/reviews/opus_495_493_486_2_dict-reify-copy_triage.md` (parent repo).
- Round 3 2026-09-23: Codex P1=0 P2=0 P3=2; Opus post-judging P1=0 P2=1 P3=4; rewrite cap reached; owner ruled "amend, no re-review" — text-only amendment of N-1 (C expiry assertion dropped, §3.4/B-495-1 narrowed), T-6 mutation, whitespace, N-2, N-3. Reviews: `research/G19-fix-fpml-iso20022/research/reviews/codex_495_493_486_3_dict-reify-copy_review.md`, `research/G19-fix-fpml-iso20022/research/reviews/opus_495_493_486_3_dict-reify-copy_triage.md` (parent repo).
- Post-Gate-A `/analyze` applied 2026-09-23 (text only; no design change).
