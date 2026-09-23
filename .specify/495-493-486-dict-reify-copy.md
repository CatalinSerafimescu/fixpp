# #495 / #493 / #486 — a shared reify table, copies that keep the source's caps, and an honest validator `noexcept`

> **Status: v0.1 — draft, Gate A round 1 pending.**
>
> Batch branch `fix/495-493-486-dict-reify-copy`, cut from `origin/main` `3f200360`. This note is the
> design authority for all three issues and **replaces a Spec-Kit bundle**. It triggers Gate A under
> `[const §XVII.1]` because it touches the public C++ API (§7) and the parser (§2.2).
>
> Claims about code name functions or files, never line numbers (426-428 precedent). Where a count
> would rot, the note gives the **condition** and a **re-derivation recipe** instead.
>
> **Owner decisions (2026-09-23), not open for review:**
> - **O-1 — #495 takes option (a), the owned route.**
>   - NFR-003-3's `dict::reify` ceiling covers the **owned** route: a source view parsed by a `Parser`
>     that holds an owning `shared_ptr<const table_view>`.
>   - A view parsed from a borrowed lvalue, `Parser{tv}`, keeps the deep copy. It is scoped out in
>     NFR-003-3's text and in a new B&L row.
>   - The factory signature `(rmv, view, mr)` and `tools/codegen/fixpp-codegen/emit_dispatch.cpp` stay
>     **untouched**. 066 rejected changing them (`specs/066-dict-backed-inbound-parse/research.md`
>     Decision 4; `plan.md`'s complexity table, "Mechanism (a) … rejected"), and this note keeps that
>     rejection.
>   - **Accepted trade-off.** 066 chose a membership copy "without a `shared_ptr<const Dictionary>`
>     pin". The owned route reverses that: a long-lived handle now **pins** the shared table, and on
>     some routes more than the table (§3.4).
>   - What the owner gets in return:
>     - memory is lower in aggregate, since N handles share one table where today each holds a copy;
>     - the reify path's allocations stay inside `mr`, through D-1c (O-4). Sharing alone does not
>       deliver this (C-2).
> - **O-2 — #493:** every re-parse inherits the source's full `OffsetTable::Config` (both caps),
>   read through a new public `OffsetTable::config()`. This applies at **all four** re-parse sites,
>   including the two dict-free fallbacks. `L-458-2` moves to the closed file.
> - **O-3 — #486:** `wire::dictionary_driven_validator`'s constructor becomes
>   `noexcept(std::is_nothrow_move_constructible_v<fixpp::dict::table_view>)`, pinned by a
>   compile-time check that the Linux lanes discharge on its true branch and the MSVC legs on its
>   false branch. `L-456-2` stays **open**, because it records an STL property; its text is amended.
> - **O-4 — D-1c accepted (owner ruling 2026-09-23, on v0.1's Q-1).** The reify handle's impl is
>   allocated from `mr` (§2.5).
>   - **Rationale:** the handle's parsed view already lives in `mr` (`bytes_`, the `OffsetTable`,
>     `unk_items_`), so `mr` must already outlive the handle. Moving the impl adds **no new lifetime
>     constraint**.
>   - The two fixed-index OOM tests are recalibrated by warm-up counting (§8 T-16).
> - **O-5 — `L-495-2` accepted and documented (owner ruling 2026-09-23, on v0.1's Q-5).**
>   - The obligation: on the C++ `dict_snapshot` route, the `Dictionary`'s load `memory_resource`
>     must outlive every reified handle and clone. It becomes a new B&L row, and the reify and
>     `Session` documentation state it (§10).
>   - The C ABI is unaffected (§3.4).
>   - It is not engineered away.
> - **Rejected by the owner:**
>   - (b), refusing reify or clone on the borrowed route;
>   - (c), only amending NFR-003-3.
>
> ⚠️ **Five premises in the brief did not survive the source.** §0.4 lists them. Each one changes the
> design or makes a test able to fail.

---

## 0. Scope

### 0.1 Issues

| Issue | Defect | Disposition |
|---|---|---|
| fixpp#495 | Every `dict::reify()` of a dict-backed view pays a full `membership_copy()`, measured at hundreds of µs against NFR-003-3's 1.2 µs | O-1: owned route shares; borrowed route keeps the copy, scoped out |
| fixpp#493 | `fixpp_msg_clone` and the reify factory re-parse under the **default** caps, so a source accepted at a raised cap is refused (`L-458-2`) | O-2: inherit the source's `Config` at all four sites |
| fixpp#486 | `dictionary_driven_validator`'s `noexcept` constructor move-constructs a `table_view`. On MSVC that move performs heap allocations (issue comment: measured ~20), so OOM there means `std::terminate` | O-3: conditional `noexcept` |

### 0.2 Why one note

All three touch the same objects:
- the `table_view` a copy site holds (#495);
- how a copy site re-parses (#493);
- how a `table_view` is moved into its long-lived holder (#486).

#493's two dict-backed sites are the same two lines #495 rewrites, so fixing them separately would
mean editing the same statements twice.

### 0.3 Constitution triggers

- `[const §XVII.1]`: this is a public C++ API change (§7), and it touches the parser (a new `Parser`
  constructor).
- `[const §X.1]` / `[const §X.7]`: the C ABI is **not** changed. No header, prototype, error code or
  freeze-manifest line moves. `fixpp_msg_clone` **widens**: a raised-cap source it used to refuse
  with `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` now clones. Nothing that succeeded before now fails, so
  under §X.7 this is not a breaking change and nothing needs declaring. `version.h` is unchanged.
  - **What a C program can observe:** nothing but memory retention.
    - `L-458-2` records that no C consumer can raise a cap. Re-derive with
      `grep -rn "max_offset_entries" src include`. So #493 changes nothing a C program can observe.
    - #495 changes only how long a table is retained (§3.4).
- `[const §VIII.2]`: any bench row past +5% needs approval from someone other than the author (§9).
- `[const §VII.7]`: the parser change adds no scanning code. The new `Parser` constructor builds
  the same `dict_hooks` as the existing one, and the existing `tests/fuzz/fuzz_wire_*` harnesses
  already cover the scanners it feeds. No new harness is planned; Gate A may object.

### 0.4 Where the source contradicts the brief

| # | Brief premise | What the source shows | Consequence here |
|---|---|---|---|
| C-1 | The owner token goes into `dict_hooks` | `dict_hooks` is copied into `wire::entry_context` (`group_view.hpp`), whose size `tests/codegen/flyweight_shape_test.cpp` pins with a `static_assert`, and every generated group flyweight is asserted to be exactly that size. `OffsetTable` stores a `hooks_` member, and several fail-loud tests tune their caps to `sizeof(OffsetTable)` bands (`tests/wire/nested_group_slices_failloud_test.cpp`, `tests/wire/group_view_failloud_test.cpp`, `tests/capi/message_read_failloud_test.cpp`). Nested sub-tables are also `mr->allocate(sizeof(OffsetTable))`. | The token lives on **`MessageView`**, not in `dict_hooks` (§2.1). `dict_hooks`, `entry_context` and `OffsetTable` stay the same size. Re-derive the pins with `grep -rnE 'sizeof\((fixpp::)?(wire::)?(entry_context\|dict_hooks\|MessageView\|OffsetTable)\b' include src tests bench`. |
| C-2 | The owned route "restores no allocation outside `mr`" | `owning_message_handle_from_frame` begins with `new owning_message_handle::impl{mr}`, a **global-heap** allocation, on every route | A zero-global-heap gate on the owned route is RED even with perfect sharing. The impl must come from `mr` (§2.5, D-1c). That was beyond O-1's letter; the owner accepted it as O-4. |
| C-3 | `static_assert(is_nothrow_constructible_v<V, table_view&&> == is_nothrow_move_constructible_v<table_view>)` pins #486 | The trait also counts the **caller-side** move into the by-value parameter. On MSVC that move is potentially-throwing, so the trait is `false` **even with today's unconditional `noexcept`**. The equality holds on the unfixed tree on every toolchain and can never fail. | §5 uses a form that isolates the constructor's own exception specification. |
| C-4 | A long-lived handle pins "the shared table" | On the C-ABI route, and for any C++ `Session` given `SessionConfig::dict_snapshot`, `inbound_tv_` is an **aliasing** pointer into a `dictionary_snapshot`, whose `source_` holds the `Dictionary` | On those routes a handle pins the snapshot: the table **and** the `Dictionary`. Only the `make_shared<const table_view>` route in `Session::open` pins the table alone (§3.4). |
| C-5 | The pin only extends a lifetime | A `Dictionary`'s metadata lives on the caller's **load** `memory_resource` and deallocates into it. A pinned `Dictionary` therefore outlives nothing safely unless that resource does. | New obligation `L-495-2` on the C++ `dict_snapshot` route. The C ABI loads with the process default resource and is unaffected (§3.4). The owner accepted it as O-5. |

---

## 1. Background

### 1.1 #495 — where the time goes

- **Reify.** `detail::owning_message_handle_from_frame` (`src/dictionary/reify.cpp`) seats
  `impl::owned_tv_` (a `std::optional<table_view>`) with `view.membership_copy()` whenever
  `view.is_dict_backed()`. It then re-parses the copied frame with `Parser<Index>{*owned_tv_}`.
- **Clone.** `fixpp_msg_clone` (`src/capi/message_write.cpp`) does the same into `fixpp_msg::owned_tv_`
  (`src/capi/capi_internal.hpp`).
- **What `membership_copy()` costs.** It copy-constructs the whole `table_view` (`parser.hpp`, the
  out-of-line definition), which means every hash container of every message in the dictionary.
- **Where the time was measured.** #494's Gate B found the copy dominates the call. Its paired
  measurement is in the parent repo at `decisions/speckit/090-capi-refusals-verify.md` § *Gate B G-1*.
- **Why the copy exists** (066 Decision 4): a handle outlives its dispatch window, so it cannot
  borrow the session's table. A self-contained copy was the smallest surface that did not change the
  factory signature.

**Every view the Session hands an application comes from one parser.** `Session::parse_and_dispatch_`
(`src/session/session.cpp`) builds `Parser<Index> pd_parser{*inbound_tv_}` for `fromApp`,
`fromAdmin` and `toApp` alike. Re-derive with `grep -n "parse_and_dispatch_(" src/session/session.cpp`:
every application callback is invoked inside one of those calls.

The C ABI reaches the same parser:
- `CapiApplication::fromApp` / `toApp` (`src/capi/engine.cpp`) wrap that same `MessageView` in a stack
  `fixpp_msg` (`inbound.view = &msg`).
- A C inbound handle's view is therefore `pd_parser`'s output, and a `fixpp_msg_clone` of it goes
  through the owned route once `pd_parser` is built over the owner (§2.6).
- `fixpp_session::tv_` (`src/capi/session.cpp`) and `Session::inbound_tv_` are both formed by
  `shared_dictionary_view` over the **same** `dictionary_snapshot`, the one `fixpp_session_open`
  mints before `register_session`.

### 1.2 #493 — four re-parse sites, one private field

`OffsetTable::Config` (`include/fixpp/wire/offset_table.hpp`) holds `max_offset_entries` and
`max_group_entries_per_instance`. `OffsetTable` stores it in the private `cfg_` and has no reader.
`OffsetTable::build` enforces the entry cap. The group-instance cap is enforced lazily, when groups
are read.

Four sites re-parse a copied frame. Each takes the **default** `Config`.

| # | Site | Route | Today |
|---|---|---|---|
| S1 | `owning_message_handle_from_frame` | dict-backed | `parser.parse(frame, mr)`, the two-argument overload |
| S2 | `fixpp_msg_clone` | dict-backed | `clone_parser.parse(fv, clone_mr)` |
| S3 | `owning_message_handle_from_frame` | dict-free fallback | `view_cache_.emplace(frame, mr)`, the dict-free two-argument `MessageView` constructor |
| S4 | `fixpp_msg_clone` | dict-free fallback | `make_unique<MessageView<Index>>(fv, clone_mr)` |

- **S1/S2 refuse** a raised-cap source: that is `B-458-2` / `B-458-1`, recorded as `L-458-2`.
- **S3/S4 degrade silently.** The copy "succeeds", but its `OffsetTable` build fails and every read
  on it reports absent. `MessageWrite.CloneDictFreeOversizedSourceStillReturnsOk` asserts only
  `FIXPP_ERR_OK`, so it is green over an **empty** clone today. That makes S3/S4 a live defect of the
  same class, not merely a case of consistency.

### 1.3 #486 — the one `noexcept` context that moves a `table_view`

`explicit dictionary_driven_validator(fixpp::dict::table_view dict) noexcept : dict_{std::move(dict)} {}`
(`include/fixpp/wire/validator.hpp`).
- **MSVC.** The member move allocates, as measured in the issue comment. A `bad_alloc` there becomes
  `std::terminate`.
- **libstdc++ and libc++.** The move is nothrow, so nothing changes there.
- **Do not reintroduce `noexcept` on the move constructor.** `table_view.hpp`'s note beside the
  removed assertion forbids restoring an explicit `noexcept` on `table_view`'s own move constructor.
  **This change does not touch `table_view`.**
- **The one production caller** is `Session::open`: `std::make_unique<dictionary_driven_validator>(*inbound_tv_)`.
  The by-value parameter is copy-initialised **at the call site**, outside the `noexcept`, so that
  site can already throw `bad_alloc` on every toolchain. After the fix, an MSVC failure in the member
  move reaches the same handler instead of terminating. No new exception reaches any caller.

---

## 2. D-1 — #495, the owned route

### 2.1 Where the owner token lives

`MessageView<Mode>` gains one private member:

```cpp
std::shared_ptr<const fixpp::dict::table_view> const* dict_owner_ = nullptr;
```

- **How it is set.** It is a borrowed pointer to a `shared_ptr` **object** that outlives the view
  (§3.1). `nullptr` means borrowed or dict-free. `Parser` sets it after constructing the view, and
  `MessageView` befriends `template <access_mode> friend class Parser;` so no public constructor
  changes.
- **Moves.** The defaulted move constructor carries it. Copy is already deleted.
- **Why a pointer to a `shared_ptr`, and not a `shared_ptr`.** Holding one by value in `Parser` or
  `MessageView` costs an atomic increment and decrement per parsed message on the inbound hot path.
  When sessions share a snapshot, that is a shared cache line. The pointer costs a store per parse
  and nothing else.
- **Why not in `dict_hooks`.** See C-1. `dict_hooks` is copied into every `entry_context` and every
  `OffsetTable`, including nested sub-tables. `MessageView` is the only holder the two copy sites
  read, and no pin constrains its size. Re-derive with the grep in C-1.
- **What grows.** `sizeof(MessageView<Index>)` and `sizeof(MessageView<Iter>)` grow by one pointer.
  Nothing in `include/`, `src/`, `tests/` or `bench/` pins either.
- **Invariant.** When `dict_owner_` is non-null, `dict_owner_->get() == hooks_.opaque_dict()` held
  when it was set. §2.3 **re-checks** this at share time rather than trusting it.

### 2.2 `Parser`'s owning constructor

```cpp
template <class SP>
explicit Parser(SP& owner) noexcept
    requires(std::same_as<std::remove_const_t<SP>,
                          std::shared_ptr<const fixpp::dict::table_view>>)
    : hooks_{dict_hooks::for_table_view(*owner)}, owner_{std::addressof(owner)} {}
// + private: std::shared_ptr<const fixpp::dict::table_view> const* owner_ = nullptr;
```

`parse(frame, mr)`, `parse(frame, mr, cfg)` and `parse_iter(frame)` then set `mv.dict_owner_ = owner_`.

**Why this shape:**
- **Lvalue only.** `owner_` stores the address of the argument, so a temporary would dangle.
  - A prvalue or xvalue of any cv-qualification resolves to the **existing** deleted
    `template <class TV> Parser(TV&&) requires(!is_lvalue_reference_v<TV&&>)`, so it does not
    compile.
- **Exact type.** A `shared_ptr<table_view>` lvalue (non-`const` pointee) must **not** convert.
  Conversion would create a temporary `shared_ptr<const table_view>`, bind it to `const&`, and
  leave `owner_` dangling. The `same_as` constraint rejects it.
- **No clash with the existing constructor.** The `table_view` constructor is constrained to
  `same_as<remove_cvref_t<TV>, table_view>`, so the two never compete.
- **Precondition: `owner` is non-null.** It is `assert`ed, as `pd_parser` already asserts
  `inbound_tv_`. Every production caller holds a non-null pointer by construction (§2.4, §2.6).
- **Unchanged:** the borrowed `Parser{tv}`, `Parser()`, and every existing call site.

### 2.3 `MessageView::shared_membership()`

This is a sibling of `membership_copy()` and replaces it at the two copy sites.

```cpp
[[nodiscard]] std::shared_ptr<const fixpp::dict::table_view> shared_membership() const;
```

It returns:
1. **Owned:** `*dict_owner_` when `dict_owner_ != nullptr && dict_owner_->get() == hooks_.opaque_dict()`.
   That is a refcount increment, with no allocation.
2. **Borrowed and dict-backed:** `std::make_shared<const fixpp::dict::table_view>(membership_copy())`.
   This is today's deep copy plus one control block. It may throw `bad_alloc`, which is why the
   function is not `noexcept`, for the same reason `membership_copy()` is not (its FQ-1 note).
3. **Dict-free:** `nullptr`.

Arm 1's identity check makes a stale owner fail toward a **copy**, never toward the wrong
dictionary. A stale owner is one whose `shared_ptr` was reassigned after the parse. No production
holder is reassigned while views exist (§3.1).

`membership_copy()` stays public and unchanged, for its other callers (`tests/`). Re-derive them
with `grep -rn membership_copy src include tests`.

### 2.4 The two copy sites

**Reify** (`src/dictionary/reify.cpp`):
- `impl::owned_tv_` becomes `std::shared_ptr<const table_view>`, and the factory seats it with
  `view.shared_membership()` when `view.is_dict_backed()`.
- The re-parse is built as `Parser<Index> parser{handle.pimpl_->owned_tv_}`, using the owning
  constructor over the **impl's own member**, which has a stable address because the impl never
  relocates and a handle move moves only the pointer.
- **Consequence:** the handle's own `view()` is an owned-route view. Reifying or cloning it again
  shares the same table.
- The `assert(!owned_tv_.has_value())` and its "#456 seam 6: seated, not assigned" comment go away.
  #456's rule bars *assigning a `table_view`*, and the table is now held behind a pointer and never
  assigned.

**Clone** (`src/capi/message_write.cpp` / `capi_internal.hpp`):
- `fixpp_msg::owned_tv_` becomes `std::shared_ptr<const fixpp::dict::table_view>`, seated the same
  way.
- `clone_parser` is `Parser<Index>{clone->owned_tv_}`, over the heap shell's member, which is
  stable.
- A clone of a clone therefore shares as well.
- `session_tv_` stays a separate member. Its non-null state means "outbound handle", and that
  semantics is unchanged.

**Stays untouched:**
- the factory signature;
- `reify.hpp`'s declarations;
- `emit_dispatch.cpp`;
- the generated dispatch bridge;
- `is_dict_backed()`;
- the three-way refusal contract of `B-458-1` / `B-458-2`.

**G2 lint** (`tools/check_dictionary_snapshot_exclusivity.sh`):
- **What it matches.** Its G2 pattern matches a parenthesised construction
  `shared_ptr<const … table_view>(` whose first argument begins `std::move(`, or is an identifier
  followed by a comma.
- **Safe spellings in this change:**
  - copying or assigning a `shared_ptr` (`owned_tv_ = view.shared_membership();`, `return *dict_owner_;`);
  - `std::make_shared<const table_view>(…)`, which contains no `shared_ptr<` token.
  None of them is an aliasing construction, and none matches.
- ⚠️ **Spelling to avoid.** The implementer must **not** write
  `std::shared_ptr<const table_view>(std::move(x))`. It is a one-argument move, but the pattern
  cannot tell it from the aliasing form and **fires**.
- **Check.** The gate runs in the verification plan (§11), and its `G2 alias-formation sites` line
  must still report the single site, in `src/dictionary/dictionary_snapshot.cpp`.

### 2.5 D-1c — the handle's impl moves into `mr` (C-2; accepted by the owner as O-4, 2026-09-23)

**Today.** `owning_message_handle_from_frame` begins with `new owning_message_handle::impl{mr}`.
Every other allocation on the reify path already comes from `mr`:
- `bytes_`;
- the zero-cap carry;
- the `OffsetTable`'s containers;
- `unk_items_`.

The global `new` is what keeps NFR-003-3's "no allocation outside `mr`" false, and it would fail the
owned-route mallocnesia gate (§8 T-14) whatever the sharing does.

**Change:**
- The impl is allocated with `std::pmr::polymorphic_allocator<>{mr}.new_object<impl>(mr)`.
- `~owning_message_handle` and the move-assignment's release both go through one private helper. It
  recovers the resource from `pimpl_->bytes_.get_allocator().resource()` **before** destroying the
  impl, then calls `delete_object`.
- `mr` must already outlive the handle, because the handle's parsed view lives in it: `bytes_`,
  the `OffsetTable` and `unk_items_`. That makes this **no new lifetime obligation** (O-4's
  rationale).
- A monotonic `mr` reclaims the impl only on `release()`, exactly as it already does for `bytes_`.

**Effect on OOM tests** (§8 T-16): the impl allocation becomes `mr` call #1. Tests that inject
failure at a **fixed** call index through the factory now land on a different allocation:
- `ReifyErrorContract.ViewRebuildOomDegradesNotTerminate` (fails call 2): turns RED, because call 2
  is now the `bytes_` copy.
- `ReifyEagerMaterialization.SpuriousHitControl_DeepCopyOomStillYieldsDictReifyOom` (fails call 1):
  stays green, but now measures the impl allocation instead of the `bytes_` arm it exists to
  control.

Both are **recalibrated by a warm-up count**, as `FailedDictBackedReparseRefuses` already is. The
constants are **not** bumped. `tests/dictionary/reify_oom_test.cpp`'s fixed-index cells go through
`owning_<Msg>::from_view`, not the factory, so they are unaffected. Re-derive with
`grep -n "fail_on_call_n" tests/dictionary/*.cpp`, then confirm which entry point each cell calls.

**Rejected alternative:** a mallocnesia budget gate of 1 on the owned arm. It needs a
`--max-allocs` passthrough that `fixpp_add_mallocnesia_test` lacks, and it would leave NFR-003-3's
"no allocation outside `mr`" clause false. The owner took D-1c instead (O-4).

### 2.6 Session wiring

- **`parse_and_dispatch_`:** `pd_parser{*inbound_tv_}` becomes `pd_parser{inbound_tv_}`, the owning
  constructor over the Session member.
  - This one edit puts every view handed to `fromApp` / `fromAdmin` / `toApp`, in C++ and C alike,
    on the owned route.
- **`validate_inbound_`'s `vg_parser` stays borrowed.** Its view never leaves the function, and
  borrowing it costs nothing.

---

## 3. Lifetime and concurrency

### 3.1 The borrowed `shared_ptr` outlives every view that points at it

| Owner object | Lifetime | Views pointing at it |
|---|---|---|
| `Session::inbound_tv_` | set once in `Session::open` (single-invocation, `state_ != never_opened` rejects a second call), destroyed with the `Session` | `pd_parser`'s output, living inside `parse_and_dispatch_`'s call, which runs on a live Session |
| `owning_message_handle::impl::owned_tv_` | impl lifetime | `impl::view_cache_` |
| `fixpp_msg::owned_tv_` | clone shell lifetime | `fixpp_msg::owned_view_` |

A `MessageView` also aliases its frame and its parse arena. Any view that outlives its owner has
already outlived those, so the pointer adds no new way to dangle.

**The copy is taken while the owner is alive.** A handle or clone copies the `shared_ptr` inside the
dispatch window. After that it holds its own reference and never reads `dict_owner_` again.

### 3.2 Stable addresses

`for_table_view` stores `std::addressof(table)`. The span accessors alias the table's own vectors
("stable for lifetime"). Two properties keep this sound:
- a `shared_ptr`'s pointee never relocates;
- the owning objects (`inbound_tv_`, the impl member, the shell member) are never reassigned while
  a view exists.

The table is reached through the pointee, so moving a handle, which moves the impl **pointer**,
cannot invalidate it.

### 3.3 Sharing across threads

- **Reads.** `table_view` has no `mutable` members, no `thread_local`, no atomics and no lazy cache.
  Re-derive with `grep -nwE "mutable|thread_local|atomic" include/fixpp/dict/table_view.hpp`: the
  only hits are prose ("immutable" inside comments). Every accessor the hooks and the validator call
  is `const` and only reads. So concurrent `const` access by the Session strand and by handles on
  other threads is data-race-free.
  - Per `brain/components/dictionary.md`, this note does **not** call the object "immutable". A
    `const_cast` write through a span remains defined behaviour. No shipped path writes, and the
    #456 seal governs reachability, not writability.
- **Refcounts.** Copying the same `shared_ptr` object from several threads is safe (`const` access).
  Each holder then owns its own copy.
- **Destruction.** The last reference to go may drop on any thread. The table's destructor, or the
  snapshot's, is plain member destruction and touches no Session state.
- **Witness.** The TSan lane runs T-11's cross-thread arm (§8).

### 3.4 What a handle pins (C-4, and the O-1 trade-off in full)

| Route | `inbound_tv_` is | A live handle or clone pins |
|---|---|---|
| C++ `Session`, no `dict_snapshot` | `make_shared<const table_view>(dictionary->as_table_view())` | the table only |
| C++ `Session` with `dict_snapshot`; **every C-ABI session** | aliasing pointer into a `dictionary_snapshot` | the snapshot: the table **and** the `Dictionary` (`dictionary_snapshot::source_`) |
| Borrowed `Parser{tv}` | none | its own deep copy (unchanged) |

**⚠️ A new lifetime obligation on the snapshot route: the `Dictionary`'s load resource.**
- **Where the `Dictionary` lives.** Its metadata block (`detail::dict_metadata_handle`,
  `src/dictionary/dictionary_internal.hpp`) is allocated on the `memory_resource` passed to the
  loader. The block stores that resource (`mr_`) and its `std::pmr::vector` members deallocate into
  it. The control block is `allocate_shared` over the same resource.
- **What the handle could promise before.** Today's copied table uses the global allocator only, so
  a handle "may outlive the Dictionary" and its resource (`table_view.hpp`, `reify.cpp`).
- **What changes on the snapshot route.** A handle keeps the `Dictionary` alive. When the last
  handle dies, `~Dictionary` runs and deallocates into the load resource. **That resource must
  outlive every handle and clone.**
- **By route:**
  - **C ABI: safe.** `fixpp_dict_load_from_xml` (`src/capi/dictionary.cpp`) loads with
    `std::pmr::get_default_resource()`, which lives for the whole process.
  - **C++ with `dict_snapshot`: at risk.** The caller chooses the resource. A caller that loads into
    a scoped arena, as `bench/dictionary/reify_bench.cpp` does with a stack
    `monotonic_buffer_resource`, and lets a handle outlive it gets a use-after-free at the handle's
    destruction.
  - **C++ without a snapshot: unaffected.** Only the table is pinned, and it is global-heap.
- **Recorded as `L-495-2`** (§10), accepted and documented by the owner (O-5, 2026-09-23). It is
  not removed. Removing it would need a second, table-only owner per snapshot-route session: either
  one table copy at `open()`, or a change to `dictionary_snapshot`.
- **Test.** T-11's snapshot arm loads its `Dictionary` into a **scoped** arena, destroys the arena
  after the last handle, and runs under ASan. It pins the obligation in the passing order.

**Memory, per handle:**
- Before: one full table copy on the global heap.
- After, on the owned route: one refcount, plus the pinned objects until the last handle dies.
- Tables freed early: on either owned route, a caller that closes a session while holding handles
  keeps the table alive (on the snapshot route, the `Dictionary` too) for as long as any handle
  lives. That is the pin the owner accepted.

### 3.5 Destruction order

The view is destroyed before the table it aliases:
- **`impl`:** `owned_tv_` is declared before `view_cache_`, and members are destroyed in reverse
  order. This change keeps that order and says so in the member comment.
- **`fixpp_msg`:** `owned_tv_` stays declared before `owned_frame_` / `owned_view_`, and
  `fixpp_msg_destroy` still resets `owned_view_` first.
- **Last reference:** when the handle's reference is the last, the table dies *after* the view.

---

## 4. D-2 — #493, the source's caps travel with the copy

**New accessor:** `[[nodiscard]] Config config() const noexcept { return cfg_; }` on `OffsetTable`,
reachable from an Index view as `view.offsets().config()`. Both copy sites hold an Index view, so no
Iter accessor is needed.

| Site | After |
|---|---|
| S1 | `parser.parse((*framed)[0], mr, view.offsets().config())` |
| S2 | `clone_parser.parse(fv, clone_mr, h->view->offsets().config())` |
| S3 | `view_cache_.emplace((*framed)[0], mr, view.offsets().config(), wire::dict_hooks::none())` |
| S4 | `make_unique<MessageView<Index>>(fv, clone_mr, h->view->offsets().config(), dict_hooks::none())` |

**The dict-free fallbacks (S3, S4) need no new API.**
- **Constructor.** They use the existing four-argument `MessageView` constructor with
  `dict_hooks::none()`, the same constructor `Parser{}.parse(frame, mr, cfg)` already uses for every
  dict-free raised-cap parse. The raised-cap sources in the existing clone tests are built that way.
- **No new refusals.** Calling the constructor directly, not `Parser::parse`, keeps the fallback's
  "never refuses, degrades in place" contract (`B-458-2`'s retained arm).
- **The one difference from the two-argument constructor.** The four-argument one seeds the root
  `group_context` with the view's MsgType, while the two-argument one leaves it empty. On a dict-free
  table, `group()` declines whatever the context (`B-220-1`), so the seed changes nothing observable.
  It also makes a dict-free copy seed its context the same way a dict-free source parsed through
  `Parser{}` did. ⚠️ This rests on the `B-220-1` argument and is **not witnessed**. T-4's frame
  carries no count tag, so a group read there could not tell the two seeds apart.

**What a refused copy can still be, after the fix.** A copy re-parses its source's bytes under its
source's caps, so the re-parse fails only where the source's own build failed. Two ways reach that
state:
- the raw dict-backed `MessageView` constructor, which skips `build_status()` and is used only by
  tests;
- OOM in the copy's own arena.

`B-458-1` / `B-458-2` stay true as written, since their code lists are `translate()`'s image.
`B-493-1` records the narrowed reach (§10).

---

## 5. D-3 — #486, a conditional `noexcept` that can be checked on both sides

```cpp
explicit dictionary_driven_validator(fixpp::dict::table_view dict)
    noexcept(std::is_nothrow_move_constructible_v<fixpp::dict::table_view>)
    : dict_{std::move(dict)} {}
```

**The pin** isolates the constructor's **own** specification (C-3). It goes in a test TU that
includes `validator.hpp` with `table_view` complete and is built by the MSVC legs
(`tests/wire/validator_domain_test.cpp`):

```cpp
using tv_factory = fixpp::dict::table_view (&)() noexcept;
static_assert(noexcept(fixpp::wire::dictionary_driven_validator{std::declval<tv_factory>()()})
              == std::is_nothrow_move_constructible_v<fixpp::dict::table_view>);
```

**Why this form isolates the constructor:**
- The argument is a **prvalue** from a `noexcept` function. Guaranteed elision initialises the
  parameter directly, with no move, so the `noexcept` operator sees only the constructor's
  specification.
- The destructor is the only other call in play, and it is implicitly `noexcept`.

**Two-sided proof, both recorded in the verify record:**
- **MSVC, unfixed tree:** the left side is `true` (unconditional `noexcept`) and the right side is
  `false`, so the assertion **fires**.
- **MSVC, fixed:** `false == false`.
- **Linux, fixed:** `true == true`.
- **Linux mutation:** changing the specification to `noexcept(false)` makes the assertion **fire**.
  This proves the Linux side is not a tautology.
- **Fallback.** If MSVC does not evaluate the prvalue form as described (the MSVC leg is the judge),
  the witness falls back to a behavioural one on MSVC: a TU-local failing `operator new`, calibrated
  by counting the allocations of a parameter-only move. It fails inside the member move.
  - Unfixed: `EXPECT_DEATH`.
  - Fixed: `EXPECT_THROW(std::bad_alloc)`.

**Unaffected:**
- **SC-007 "no virtual edge".** The validator still holds its `table_view` by value. No virtual
  function is added, and the `final` class and its five overrides are unchanged. It **cannot** join
  §2's sharing, because by-value holding is the frozen design point, and this note does not
  reopen it.
- **`table_view`'s move constructor** keeps its inferred specification, per `table_view.hpp`'s
  ⛔ note.

---

## 6. Alternatives rejected

| Alternative | Why rejected |
|---|---|
| Owner token inside `dict_hooks` (the brief's proposal) | Grows `entry_context` (size-pinned; generated flyweights must match it), `OffsetTable` (whose cap bands are tuned to its size) and every nested sub-table allocation. It also costs a copy per group descent. C-1. |
| `MessageView` / `Parser` holding a `shared_ptr` by value | An atomic RMW pair per inbound message on the hot path (`[const §VIII.5]` spirit), and it needs the owner at every parse site. §2.1. |
| Thread a dictionary through the factory, pimpl, bridge and `dict::reify` (066's mechanism (a)) | Changes the factory signature, which forces an `emit_dispatch.cpp` edit and a bridge regeneration. 066 rejected it, and O-1 keeps that. |
| `shared_ptr<const Dictionary>` pin, rebuilding the table from the `Dictionary` | `as_table_view()` is a full dictionary walk. Rebuilding per handle is slower than the copy it replaces, and `[const §XV.1]` bars it off config time. |
| `table_view` refcounted internally (shared storage inside the type) | Every copy of a `table_view` would share, including the validator's by-value copy that SC-007 froze, and #456's sealed type would change. The change is far larger than the defect, and it adds shared mutable-looking storage to a type the brain warns about. |
| (b) Refuse reify/clone on the borrowed route | Rejected by the owner. It breaks every existing borrowed caller, tests included. |
| (c) Amend NFR-003-3 only | Rejected by the owner. It leaves the shipped Session path at hundreds of µs. |
| #493 via a shared `reparse_like(src_view, frame, mr)` helper in `wire` (#493's own suggestion) | Four call sites with two shapes: `Parser::parse` for refusal and a raw constructor for degradation. A helper would have to take both modes. A one-line accessor gives the same single source of truth. |
| #493: new dict-free `MessageView(frame, mr, Config)` constructor for S3/S4 | The four-argument constructor with `none()` is already the in-tree dict-free raised-cap route (§4). A new public constructor would add API for nothing. |
| #486 option 2: `const&` plus copy | The issue comment measured a copy at more allocations than a move, so it keeps `noexcept` while enlarging what it would terminate on. |
| #486 option 1: drop `noexcept` unconditionally | Loses the nothrow promise on the lanes where it is true, for no gain. The conditional form is exact on both. |
| #486: `is_nothrow_constructible_v<V, table_view&&>` as the pin | Cannot fail on any toolchain (C-3). |

---

## 7. Public API delta (`[const §XVII.1]`)

| Symbol | Header | Change |
|---|---|---|
| `template <class SP> explicit Parser<Mode>::Parser(SP& owner) noexcept requires(same_as<remove_const_t<SP>, shared_ptr<const table_view>>)` | `include/fixpp/wire/parser.hpp` | **new** |
| `std::shared_ptr<const fixpp::dict::table_view> MessageView<Mode>::shared_membership() const` | `include/fixpp/wire/parser.hpp` | **new** |
| `OffsetTable::Config OffsetTable::config() const noexcept` | `include/fixpp/wire/offset_table.hpp` | **new** |
| `dictionary_driven_validator::dictionary_driven_validator(table_view)` | `include/fixpp/wire/validator.hpp` | exception specification: `noexcept` → `noexcept(is_nothrow_move_constructible_v<table_view>)`. It is unchanged on libstdc++ and libc++, and **narrowed** on MSVC. |
| `MessageView<Mode>` | `parser.hpp` | +1 private member; befriends `Parser`; size grows by one pointer. No public signature changes. |

**Behaviour deltas with no signature change:**
- `dict::reify` / `detail::owning_message_handle_from_frame` share the table on the owned route and
  allocate the impl from `mr`.
- `fixpp_msg_clone` shares the table on the owned route.
- Both inherit the source's caps (`B-493-1`).

**Also:** no C header, `version.h`, `tools/capi_freeze.sha256` or symbol-golden change. The Python
binding exposes none of these symbols.

---

## 8. Tests — TDD order; each RED names the mutation that must turn it red

**Conventions:**
- "RED today" means the test fails on `3f200360`.
- A test for a symbol that does not yet exist is RED by failing to compile. The implementer records
  the compile error as the RED.
- Mutations run in a **scratch copy**, never the working tree (constitution v3.0; the orchestrator
  does not implement).

### #486
- **T-1 — the §5 `static_assert`.**
  - RED on the MSVC leg, unfixed tree.
  - Linux mutation: `noexcept(false)`.
  - Both results are recorded.

### #493 (before #495, so the cap fix lands on today's copy sites and is witnessed independently)
- **T-2 — C++ reify twin.** New test: `ReifyEagerMaterialization.RaisedCapDictBackedSourceReifiesUnderItsOwnCaps`.
  - Setup: the 4100-field `make_oversized_frame_for_clone_test` shape, parsed dict-backed at
    `max_offset_entries = 8192`, then reified through the factory.
  - Asserts:
    - the result has a value;
    - `view().offsets().config()` equals the source's;
    - `field_value(49) == "SENDERID"`;
    - `view().offsets().entries().size()` equals the source's.
  - RED today: `wire_offset_table_full`.
  - Mutation: S1 back to two arguments.
- **T-3 — `MessageWrite.CloneDictBackedReparseCapExceededYieldsWireLimitExceeded` rewritten.**
  - Renamed `…RaisedCapSourceClonesUnderItsOwnCaps`.
  - Asserts `FIXPP_ERR_OK`, the clone's 49, and the clone's entry count equal to the source's. The
    source-intact assertions stay.
  - RED today: `WIRE_LIMIT_EXCEEDED`.
  - Mutation: S2 back to two arguments.
- **T-4 — dict-free fallbacks.**
  - `MessageWrite.CloneDictFreeOversizedSourceStillReturnsOk` gains `fixpp_msg_get_string(clone, 49)`
    `== "SENDERID"`.
  - A C++ twin reifies a dict-free raised-cap source and reads 49.
  - Both are RED today, because the copy is empty.
  - Mutations: S3 or S4 back to two arguments, each on its own.
- **T-5 — the second cap.**
  - A dict-backed source with raised `max_offset_entries` **and** raised
    `max_group_entries_per_instance`, carrying one group instance longer than the default
    per-instance cap. Clone and reify each read that group.
  - Mutation: inherit only `max_offset_entries`. The group read on the copy must fail. This is what
    proves the **whole** `Config` travels.
- **T-6 — replacement route for clone → `WIRE_LIMIT_EXCEEDED`.**
  - After T-3 nothing else witnesses this. Re-derive: `grep -rn WIRE_LIMIT_EXCEEDED tests/capi`
    shows only translate-table and read-path hits besides T-3.
  - New test: `MessageWrite.CloneOfUnbuiltOversizedSourceYieldsWireLimitExceeded`.
    - The source is the 4100-field frame through the **raw** dict-backed `MessageView` constructor
      at the default cap, so its own build failed. This uses the same lever as
      `…MalformedFieldYieldsWireInvalidFrame`.
    - Assert the exact code, `clone_out == NULL`, and an unchanged source state.
  - This is a **regression pin** (GREEN before and after), not a RED.
  - Mutation: map the re-parse error through a constant instead of `translate()`.

### #495
- **T-7 — constructor constraints**, as `static_assert`s in a wire test TU:
  - `is_constructible_v<Parser<Index>, shared_ptr<const table_view>&>`: true
  - `… const shared_ptr<const table_view>&`: true
  - `… shared_ptr<const table_view>&&`: false
  - `… shared_ptr<table_view>&`: false
  - Mutation: drop the `same_as` constraint. The fourth assertion fires.
- **T-8 — `shared_membership()` arms**, with `MessageViewSharedMembership.*`:
  - **owned:** `s.get() == sp.get()` and `use_count` +1;
  - **borrowed:** `s.get() != sp.get()`, and the content is equivalent (the same `field_valid_for`
    over the frame's tags);
  - **dict-free:** `nullptr`;
  - **stale owner:** keep the old table alive through a second reference, then reassign `sp`. The
    result is a copy of the **old** table, not `sp`'s new one.
  - Mutations: always copy (owned arm RED); drop the identity check (stale arm RED).
- **T-9 — reify shares.**
  - Setup: an owned-route source, reified.
  - Asserts:
    - `handle.view().hooks().opaque_dict() == sp.get()`;
    - reify of `handle.view()` again yields the same address.
  - RED today: a distinct copy.
  - Mutation: arm 1 → arm 2.
- **T-10 — clone shares.**
  - Via `capi_internal.hpp`, as the existing clone tests reach `h.msg`:
    `clone->owned_tv_.get() == sp.get()`, and a clone of the clone shares too.
  - RED today: the type is `optional<table_view>`, so the test fails to compile, and after that the
    address differs.
- **T-11 — owned-route lifetime** (ASan and TSan lanes).
  - A twin of `ReifyMembershipIdentity.GroupMembershipSurvivesSourceDestruction` whose source is
    parsed through `Parser{sp}`, where `sp = shared_dictionary_view(make_dictionary_snapshot(dict))`.
  - Setup: reify and clone, then drop `sp`, the test's own `shared_ptr<const Dictionary>`, the frame
    and the parse arena. The `Dictionary` now lives only through the copies. Its load arena is
    destroyed **after** the copies (§3.4, `L-495-2`).
  - Asserts: the NoLegs group reads membership-bounded from both copies.
  - Second arm: the same with a `make_shared<const table_view>` owner (the table-only route).
  - Third arm: reads the handle on another thread while the original thread drops the last
    `shared_ptr`.
  - Mutation: arm 1 returns a **non-owning** alias of `dict_owner_->get()`. ASan reports
    heap-use-after-free. The mutant is written in the scratch copy only.
- **T-12 — borrowed-route survival, kept as a pin.** `GroupMembershipSurvivesSourceDestruction`
  builds its source with `Parser{tv}` and stays unchanged and green. It is the witness that the
  borrowed route keeps its self-contained copy.
- **T-13 — shipped path, through real `Session` dispatch** (066 Decision 6).
  - A C++ `Application::fromApp` reifies two consecutive inbound messages.
  - Assert: both handles' `view().hooks().opaque_dict()` are equal.
  - RED today, because each handle has its own copy.
  - Mutation: revert `pd_parser` to `{*inbound_tv_}`.
  - C-ABI twin (engine loopback): the recv callback clones two inbound handles. The test compares
    their `owned_tv_` through `capi_internal.hpp`, destroys the engine, then reads a group from each
    clone under ASan.
- **T-14 — owned route performs no global-heap allocation** (owner, 2026-09-23).
  - New binary: `tests/alloc_guard/test_reify_owned_alloc_guard.cpp`.
  - Dual gate, per `test_dict066_grouped_read_alloc_guard.cpp`:
    - a TU-local `operator new` counter, compiled out under `FIXPP_SANITIZER_REPLACES_NEW`;
    - the mallocnesia markers.
  - Setup: the source is parsed through `Parser{sp}` outside the window. `mr` is a
    `monotonic_buffer_resource` over a stack buffer with `null_memory_resource()` upstream, so an
    undersized buffer fails as `dict_reify_oom` rather than reaching the heap.
  - Window: `dict::reify(view, profile, &mr)`, a field read, and the handle's destruction, while
    `sp` is still held, so the destruction is not the last reference.
  - `ReifyOwnedAllocGuard.OwnedRouteZeroGlobalHeap` asserts a counter delta of 0.
  - `ReifyOwnedAllocGuard.BorrowedRouteAllocates` (the positive control): the same, through
    `Parser{*sp}`. It asserts a counter delta > 0 when the counter is compiled in.
  - Registration in `tests/alloc_guard/CMakeLists.txt`:
    - one plain `add_test`;
    - `fixpp_add_mallocnesia_test(NAME reify_owned_alloc_guard_mallocnesia … ENVIRONMENT GTEST_FILTER=*OwnedRoute*)`;
    - `fixpp_add_mallocnesia_test(NAME reify_borrowed_alloc_guard_positive_control_mallocnesia … EXPECT_VIOLATION ENVIRONMENT GTEST_FILTER=*BorrowedRoute*)`.
  - Both carry `LABELS "495;alloc_guard"`, and the helper adds `mallocnesia`.
  - RED:
    - with sharing implemented but the impl still `new`ed (§2.5), the owned gate is RED;
    - on today's tree the test does not compile.
  - Mutations, each RED on its own: revert D-1c; replace arm 1 with arm 2.
- **T-15 — inbound parse stays zero-alloc.**
  - `test_dict066_grouped_read_alloc_guard.cpp` mirrors `parse_and_dispatch_`'s construction, which
    becomes `Parser{inbound_tv_}`. It gains an owning-constructor arm of both its tests, keeping the
    borrowed arms. Its existing `dict066_grouped_read_alloc_guard_mallocnesia` registration covers
    the new arms with no CMake change.
  - `wire_alloc_guard_test_mallocnesia` covers the dict-free parse and serialise path.
  - ⚠️ **No mallocnesia gate covers `Session` dispatch itself.** `tests/alloc_guard/CMakeLists.txt`
    deliberately leaves `alloc_guard_dispatch` and `alloc_guard_session` ungated, because their
    windows wrap `co_spawn` / `ioc.run()`. The parse-level mirror is the only gate. This change does
    not widen that gap. It stores one more pointer and allocates nothing.
- **T-16 — OOM recalibration (D-1c).**
  - `ViewRebuildOomDegradesNotTerminate` and `SpuriousHitControl_DeepCopyOomStillYieldsDictReifyOom`
    derive their failing index from a warm-up count through a counting `failing_pmr_resource`.
  - New arm: failing the **impl** allocation (the first `mr` call) yields `dict_reify_oom`, with no
    handle and no leak under ASan.

---

## 9. Bench plan (`bench/dictionary/reify_bench.cpp` only; no bench CMake edit)

**Baseline first.** The base measurement is taken on `3f200360` **before any edit** (owner).

**Rows:**

| Row | Route | Frame | Judged against |
|---|---|---|---|
| `BM_Reify_DictBacked_20tag` (existing) | borrowed `Parser{tv}` | the existing frame, **< 20 fields** | base-vs-head, no regression beyond +5% (`[const §VIII.2]`) |
| `BM_Reify_Dispatch_20tag` (existing) | returns before dispatch | no MsgType | base-vs-head, no regression |
| `BM_Reify_DictBacked_Owned_20field` (**new**) | owned `Parser{sp}` | a v44 NewOrderSingle with ≥ 20 fields, all valid for `D` in FIX44 | NFR-003-3's **1.2 µs** ceiling |
| `BM_Reify_DictBacked_Borrowed_20field` (**new**) | borrowed, same frame as the row above | same | reported beside the owned row, so the copy's cost is read on one frame |

**Honest comparator.**
- The owned row cannot build at base, because the constructor does not exist there. It is judged
  against the NFR ceiling only.
- Base-vs-head is read on the existing rows. `BM_Reify_DictBacked_20tag` stays borrowed, so it will
  **not** move toward the ceiling.
- Its expected head delta is small but not zero:
  - borrowed arm 2 adds one control block, in the same allocation as the table (`make_shared`);
  - D-1c removes the global impl allocation.
  Whatever it reads is reported, and a slowdown past +5% goes to a non-author approval.

**Setup checks for the new rows**, each failing as `SkipWithError`:
- The row asserts `parsed->offsets().entries().size() >= 20`.
- The arena is a stack buffer with a `null_memory_resource()` upstream, sized with a stated margin.
  An overflow then becomes a refusal instead of a silent heap allocation inside the timed loop.
  `k20TagBufSz`'s 4 KiB is not assumed to fit once the impl lives in `mr`; the row defines its own
  size.

**Parser path.** `bench/wire/parser_bench` is also run base-vs-head, because `MessageView` grew
and `Parser` stores a pointer. It is a CI-`paired` row, so CI gates it too.

**Procedure:**
- `linux-clang-release`, A-B-A-B, min-per-tree, one machine.
- The raw JSON goes into the verify record.
- `reify_bench` is `none:` / `no` in `bench/ci-suite.txt`, so **no automated timing gate** sees these
  rows. The paired manual run is the only instrument (`[const §VIII.2]`'s partial-coverage clause).

---

## 10. Spec, B&L, catalogue and brain deltas

**Spec:**
- **`specs/003-dictionary-codegen/spec.md`, NFR-003-3.** Amend the `dict::reify` clause to:
  > ≤ 1.2 µs (20-tag) **for a source view parsed by a `Parser` constructed over an owning
  > `shared_ptr<const table_view>`** (the shipped C++ `Session` dispatch path). A view parsed
  > from a borrowed `table_view` lvalue pays a per-handle `membership_copy()` and is **not** held to
  > this ceiling (`L-495-1`). It is held to no regression against the merge-base.
  >
  > "No allocation outside `mr`" likewise covers the owned route.

  The row carries an in-place *"Amended (fixpp#495, 2026-09-…)"* note.
- **066 records.** `research.md` Decision 4 and `data-model.md`'s "Reify owning handle owned
  table_view" / "Clone-owned table_view" entities get an in-place *"Superseded in part by
  `.specify/495-493-486-dict-reify-copy.md`"* note. The "without a pin" clause no longer describes
  the owned route. The history stays.

**B&L:**
- **`spec/behaviors-and-limitations.md`**, a new section with:
  - **`B-495-1`**: on the owned route, `dict::reify` and `fixpp_msg_clone` share the source's table
    by reference count. A live handle or clone pins it, and on the snapshot routes it pins the
    `Dictionary` too (§3.4).
  - **`L-495-1`**: a source parsed through a borrowed `Parser{tv}` still deep-copies the table per
    handle. It is above NFR-003-3's ceiling and scoped out of it.
  - **`L-495-2`**: on the C++ `dict_snapshot` route, a reify handle or clone keeps the `Dictionary`
    alive. The `memory_resource` the `Dictionary` was loaded into must therefore outlive every
    handle and clone (§3.4). The C ABI is unaffected, because it loads with the process default
    resource. (O-5.)
  - **Documentation of `L-495-2`** (O-5): the same obligation is stated in the doc comments of:
    - `dict::reify` and `owning_message_handle` (`include/fixpp/dict/reify.hpp`);
    - `SessionConfig::dict_snapshot`;
    - `fixpp_msg_clone`'s C++-side comment, which notes that the C ABI is unaffected.
  - **`B-493-1`**: clone and reify re-parse under the source's `OffsetTable::Config`. A raised-cap
    source now copies, including through the dict-free fallbacks, which used to return an empty copy.
    `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` from clone is reachable only from a source whose own build
    failed.
  - **`B-486-1`**: `dictionary_driven_validator`'s constructor is `noexcept` exactly where
    `table_view`'s move is nothrow. On MSVC an allocation failure in it propagates as `bad_alloc`.
- **`L-458-2`** moves to `spec/behaviors-and-limitations-closed.md`, marked resolved by fixpp#493.
- **`L-456-2`** stays live. Its ⚠️ paragraph drops the validator from "who already paid it" and
  says the constructor now carries a conditional specification (fixpp#486). The other two move
  paths (`build() &&`, `optional::emplace`) are unchanged.
  - `optional::emplace` no longer seats a `table_view` at either copy site, so check whether that
    phrase still names a live path, and cut it if not.

**Catalogue (`spec/feature-catalogue.md`).** Append a dated note to:
- **CA-009**, for the clone;
- the wire rows carrying the 066 / 458 amendment notes;
- **W-014**, for the validator.

Re-derive the wire rows with `grep -n "066-dict\|#458\|090-capi" spec/feature-catalogue.md`. No new
rows.

**Brain:**
- `brain/components/dictionary.md`: *"The reify handle materialises EAGERLY"* gains a paragraph on
  the owned route and the pin, and its ⚠️ residuals line drops `L-458-2`.
- `brain/components/c-api.md`: the clone entry, likewise.
- Each component index lists this note.

**Code comments.** Per the parent `CLAUDE.md`, name the superseding note in header comments at:
- `impl::owned_tv_`;
- `fixpp_msg::owned_tv_`;
- `membership_copy()`'s "the ONE accessor" comment.

---

## 11. Verification plan

Builds are owner-approved before they run (`[const §XVII.7]`). Before building, check
`df -h /mnt/e`.

1. **`linux-clang-debug`, targeted.**
   - `ctest -R 'Reify|reify|MessageWrite|Clone|SharedMembership|validator|dict066|alloc_guard'`.
   - Then the full `ctest -L` sets for `dictionary`, `capi`, `session` and `wire`.
2. **Mallocnesia — point 1** (owner, 2026-09-23): after implementation, before `/simplify` and the
   verify record.
   - `ctest -L mallocnesia` on the Linux non-sanitizer preset. The interceptor is
     `build/<preset>/lib/libmallocnesia.so`.
   - Required: T-14's owned gate passes; its borrowed control is **caught** (EXPECT_VIOLATION); and
     `alloc_guard_positive_control_mallocnesia` is caught. Together these prove the interceptor is
     live.
3. **Sanitizers.** ASan, UBSan and TSan presets, targeted to T-2…T-16. TSan is required for T-11's
   cross-thread arm.
4. **Bench:** §9, on `linux-clang-release`, against the pre-edit baseline.
5. **Coverage.** `linux-clang-coverage`, targeted, over the changed functions in `reify.cpp`,
   `message_write.cpp`, `parser.hpp` and `offset_table.hpp`. Every new branch is covered, including
   `shared_membership()`'s stale-owner arm.
6. **MSVC local**, per the parent repo's `research/G19-fix-fpml-iso20022/msvc-local-build-procedure.md`.
   Derive the toolset from `CMakeCache.txt`'s `CMAKE_LINKER`.
   - T-1 on the **unfixed** tree must fail to compile; the fixed tree compiles.
   - T-16's recalibrated cells run on `windows-msvc-release`. `FIXPP_SKIP_ON_MSVC_DEBUG_ARENA` still
     applies on debug.
7. **Gates:**
   - `tools/check_dictionary_snapshot_exclusivity.sh`: G2 still reports one site, in
     `dictionary_snapshot.cpp`, and its liveness line holds.
   - `.claude/scripts/check-comment-claims.py --root <tree> --base origin/main`.
   - `check_line_citations.py --shift-audit origin/main..HEAD`, because `.specify/` and spec files
     are edited.
8. **Mallocnesia — point 2** (owner, 2026-09-23): re-run step 2 after `/gate-b`'s fixer rounds,
   before merge.
9. **Index.** `codegraph sync` in this worktree after each code-changing phase.

---

## 12. Not in scope

- Sharing the validator's table (SC-007's by-value holding is frozen, §5).
- Unifying `fixpp_msg::session_tv_` with `owned_tv_`.
- `L-458-1` (probe-cap under-indexing).
- The `BM_Reify_Dispatch_20tag` placement effect (owner-approved in #494).
- The MSVC cost of `table_view` moves in `as_table_view()` / `build() &&` (#486's comment, point 2):
  recorded, not measured here.
- `owning_<Msg>::from_view` / `reify_as` (NFR-003-3's other clauses; untouched).
- Making `vg_parser` owned.
- A mallocnesia gate over `Session` dispatch (§8 T-15).

---

## 13. Open questions for Gate A

v0.1's Q-1 and Q-5 were answered by the owner on 2026-09-23 and are recorded as rulings O-4 and
O-5. Q-4 was withdrawn (§0.3).

- **Q-2.** Does MSVC evaluate §5's prvalue `noexcept` form as the standard requires? The MSVC leg
  decides. If it does not, the behavioural fallback in §5 stands in.
- **Q-3.** Is `shared_membership()` the right public home?
  - The alternative is a `detail`-namespaced free function befriended by `MessageView`, which keeps
    it off the public surface.
  - The two callers live in `src/dictionary/` and `src/capi/`, and neither is a `wire` friend, so
    this note chose public, mirroring `membership_copy()`.
