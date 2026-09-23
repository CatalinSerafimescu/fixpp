# #495 / #493 / #486 — a shared reify table, copies that keep the source's caps, and an honest validator `noexcept`

> **Status: v0.2 — Gate A round 1 applied, round 2 pending.**
>
> Batch branch `fix/495-493-486-dict-reify-copy`, cut from `origin/main` `3f200360`. This note is the
> design authority for all three issues and **replaces a Spec-Kit bundle**. It triggers Gate A under
> `[const §XVII.1]`: it touches the public C++ API (§9), the parser (§2.2) and, through R-D, the C ABI
> (§7).
>
> Claims about code name functions or files, never line numbers (426-428 precedent). Where a count
> would rot, the note gives the **condition** and a **re-derivation recipe** instead. Every negative
> grep carries a positive control, so that a recipe returning nothing can be told apart from a broken
> one.
>
> **Owner decisions (2026-09-23), not open for review:**
> - **O-1 — #495 takes option (a), the owned route, narrowed by R-A.**
>   - The owned route is the **shipped `Session` dispatch path** and the handles and clones derived
>     from it. It is reached through a `detail` passkey (§2.2), so it is **not** public C++ API.
>   - A view parsed through a borrowed `Parser{tv}`, which includes every C++ user's own parse,
>     keeps the deep copy. It is scoped out in NFR-003-3's text and in a new B&L row.
>   - The factory signature `(rmv, view, mr)` and `tools/codegen/fixpp-codegen/emit_dispatch.cpp` stay
>     **untouched**. 066 rejected changing them (`specs/066-dict-backed-inbound-parse/research.md`
>     Decision 4; `plan.md`'s complexity table, "Mechanism (a) … rejected"), and this note keeps that
>     rejection.
>   - **Accepted trade-off.** 066 chose a membership copy "without a `shared_ptr<const Dictionary>`
>     pin". The owned route reverses part of that: a long-lived handle now pins the shared **table**.
>     After R-B it never pins the `Dictionary` (§3.4).
>   - What the owner gets in return:
>     - memory is lower in aggregate, since N handles share one table where today each holds a copy;
>     - the reify path's allocations stay inside `mr`, through D-1c (O-4). Sharing alone does not
>       deliver this (C-2).
> - **O-2 — #493:** every re-parse inherits the source's full `OffsetTable::Config` (both caps),
>   read through a new public `OffsetTable::config()`. This applies at every re-parse site, the
>   dict-free fallbacks included (§1.2 gives the census recipe). `L-458-2` moves to the closed file.
> - **O-3 — #486:** `wire::dictionary_driven_validator`'s constructor becomes
>   `noexcept(std::is_nothrow_move_constructible_v<fixpp::dict::table_view>)`, pinned by a
>   compile-time check that the Linux lanes discharge on its true branch and the MSVC legs on its
>   false branch. `L-456-2` stays **open**, because it records an STL property; its text is amended.
> - **O-4 — D-1c accepted (on v0.1's Q-1).** The reify handle's impl is allocated from `mr` (§2.5).
>   - **Rationale:** the handle's parsed view already lives in `mr` (`bytes_`, the `OffsetTable`,
>     `unk_items_`), so `mr` must already outlive the handle. Moving the impl adds **no new lifetime
>     constraint**.
>   - The OOM tests are recalibrated by phase, not by a fixed index (§10 T-16).
> - **R-A — the owned route is not public API (closes Root cause 1).** The owning `Parser`
>   constructor and `shared_membership()` sit behind `detail` access (§2.2, §2.3). No public API
>   exposes a pointer to a caller's `shared_ptr`. NFR-003-3's amendment is worded around the shipped
>   dispatch path (§12). The lifetime argument is re-derived in §3.1, and the `Session::inbound_tv_`
>   invariant is stated and commented (§2.6).
> - **R-B — O-5 re-ruled, option (i′) (supersedes O-5 and its `L-495-2`).** `dictionary_snapshot`
>   owns its `table_view` in a control block of its own, and `shared_dictionary_view` hands out a
>   copy of that owner. Handles and clones therefore pin only a self-contained table, never the
>   `Dictionary` or its load resource (§6). No lifetime obligation on the load resource remains, so
>   there is no `L-495-2`.
> - **R-C — the G2 gate flips (§6.4).** `tools/check_dictionary_snapshot_exclusivity.sh`'s G2 asserts
>   **zero** aliasing `shared_ptr<const table_view>` constructions. Its self-test gains seeded
>   positives for G2. G1 is unchanged.
> - **R-D — ride-along: the C loader stops using the host's default resource (§7).** Owner: "C-ABI
>   changes are fine now, we have no clients." `fixpp_dict_load_from_xml` loads with
>   `std::pmr::new_delete_resource()`. This fixes a pre-existing hazard: an outbound `fixpp_msg`
>   pins the `Dictionary`, so `~Dictionary` can run on any thread into a host-installed resource.
> - **Rejected by the owner:**
>   - (b), refusing reify or clone on the borrowed route;
>   - (c), only amending NFR-003-3;
>   - v0.1's O-5 (document `L-495-2` and keep the hazard): its premise that the C ABI loads from a
>     process-lifetime resource was false (Codex #2), and it missed the deallocation-thread half
>     (triage N-2).
>
> **Fable consult (supporting analysis, not the authority).** Problem key
> `b13-reify-handle-pins-dictionary-resource`, trigger 1, confidence **`probable`**. It proposed (i′)
> and established that `table_view` is self-contained. The orchestrator re-verified its facts before
> the owner ruled. The repo accepts a consult's resolution only at `beyond doubt`, so the authority
> for §6 is owner ruling R-B. The consult's load-bearing facts are reproduced with their recipes in
> §6.1.
>
> ⚠️ **Five premises in the brief did not survive the source.** §0.4 lists them. Each one changes the
> design or makes a test able to fail.

---

## 0. Scope

### 0.1 Issues

| Issue | Defect | Disposition |
|---|---|---|
| fixpp#495 | Every `dict::reify()` of a dict-backed view pays a full `membership_copy()`, measured at hundreds of µs against NFR-003-3's 1.2 µs | O-1/R-A: owned route shares; borrowed route keeps the copy, scoped out |
| fixpp#493 | `fixpp_msg_clone` and the reify factory re-parse under the **default** caps, so a source accepted at a raised cap is refused (`L-458-2`) | O-2: inherit the source's `Config` at every re-parse site |
| fixpp#486 | `dictionary_driven_validator`'s `noexcept` constructor move-constructs a `table_view`. On MSVC that move allocates (the issue comment measured about 20 allocations), so OOM there means `std::terminate` | O-3: conditional `noexcept` |
| ride-along (R-B) | Sharing would pin the `Dictionary` and its load resource through a snapshot alias | D-4: the snapshot's table gets its own owner |
| ride-along (R-D) | An outbound C `fixpp_msg` pins the `Dictionary`, which deallocates into whatever default resource the host had installed at load | D-5: the C loader uses `new_delete_resource()` |

### 0.2 Why one note

All three issues touch the same objects:
- the `table_view` a copy site holds (#495);
- how a copy site re-parses (#493);
- how a `table_view` is moved into its long-lived holder (#486).

#493's two dict-backed sites are the same two statements #495 rewrites, so fixing them separately
would mean editing the same statements twice. D-4 and D-5 exist because #495's sharing changes what
a handle pins.

### 0.3 Constitution triggers

- `[const §XVII.1]`: this is a public C++ API change (§9), it touches the parser (a new `Parser`
  constructor), and it touches the C ABI (D-5).
- `[const §X.1]` / `[const §X.6]` / `[const §X.7]`: **the C ABI changes** (§7).
  - `include/fix/c_api/dict.h`'s documented loader resource changes. That is a breaking change under
    §X.7, so `FIXPP_C_ABI_VERSION_MINOR` goes 7 → 8, marked BREAKING (§7.2).
  - `fixpp_msg_clone` **widens** (#493): a raised-cap source it used to refuse with
    `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` now clones. A call that used to fail and now succeeds is not
    breaking under §X.7.
  - **What a C program can observe from #493/#495:** clones of raised-cap sources succeed, and a
    clone keeps its table resident until it is destroyed.
    - `L-458-2` records that no C consumer can raise a cap. Re-derive with
      `grep -rn "max_offset_entries" src include`.
  - §X.6 fires all four Appendix A controls. They are discharged as `.specify/447-458-452-capi-refusals.md`
    discharged them in issue mode:
    - Codex Gate A: this loop.
    - `/clarify`: by hand. The owner rulings O-1…O-4 and R-A…R-D are its record.
    - `/analyze`: run over this note before implementation. **Pending.**
    - owner `/plan` sign-off: recorded in this header before implementation starts. **Pending.**
- `[const §VIII.2]`: any bench row past +5% needs approval from someone other than the author (§11).
- `[const §VII.7]`: the parser change adds no scanning code. The new `Parser` constructor builds the
  same `dict_hooks` as the existing one, and the existing `tests/fuzz/fuzz_wire_*` harnesses already
  cover the scanners it feeds. No new harness is planned; Gate A may object.

### 0.4 Where the source contradicts the brief

| # | Brief premise | What the source shows | Consequence here |
|---|---|---|---|
| C-1 | The owner token goes into `dict_hooks` | `dict_hooks` is copied into `wire::entry_context` (`group_view.hpp`), whose size `tests/codegen/flyweight_shape_test.cpp` pins with a `static_assert`, and every generated group flyweight is asserted to be exactly that size. `OffsetTable` stores a `hooks_` member, and fail-loud tests tune their caps to `sizeof(OffsetTable)` bands. Nested sub-tables are `mr->allocate(sizeof(OffsetTable))`. | The token lives on **`MessageView`**, not in `dict_hooks` (§2.1). `dict_hooks`, `entry_context` and `OffsetTable` keep their sizes. Re-derive the pins with `grep -rnE 'sizeof\((fixpp::)?(wire::)?(entry_context\|dict_hooks\|MessageView\|OffsetTable)\b' include src tests bench`. |
| C-2 | The owned route "restores no allocation outside `mr`" | `owning_message_handle_from_frame` begins with `new owning_message_handle::impl{mr}`, a **global-heap** allocation, on every route | A zero-global-heap gate on the owned route is RED even with perfect sharing. The impl must come from `mr` (§2.5, D-1c; the owner accepted it as O-4). |
| C-3 | `static_assert(is_nothrow_constructible_v<V, table_view&&> == is_nothrow_move_constructible_v<table_view>)` pins #486 | The trait also counts the **caller-side** move into the by-value parameter. On MSVC that move is potentially-throwing, so the trait is `false` **even with today's unconditional `noexcept`**, and the equality holds on the unfixed tree on every toolchain. | §5 uses a form that isolates the constructor's own exception specification. |
| C-4 | A long-lived handle pins "the shared table" | On `origin/main`, for the C ABI and for any C++ `Session` given `SessionConfig::dict_snapshot`, `inbound_tv_` is an **aliasing** pointer into a `dictionary_snapshot`, whose `source_` holds the `Dictionary` | Sharing that pointer would pin the `Dictionary`. D-4 (R-B) gives the snapshot's table its own owner, so every route pins the table only (§3.4). |
| C-5 | Pinning only extends a lifetime | A `Dictionary`'s metadata lives on the caller's **load** `memory_resource` and deallocates into it, on the thread that drops the last reference | With the alias, every handle and clone would have carried a lifetime *and* a thread obligation on that resource, including C-ABI ones under a host-installed default resource. D-4 removes it for handles and clones. D-5 closes the pre-existing outbound-handle instance. |

---

## 1. Background

### 1.1 #495 — where the time goes

- **Reify.** `detail::owning_message_handle_from_frame` (`src/dictionary/reify.cpp`) seats
  `impl::owned_tv_` (a `std::optional<table_view>`) with `view.membership_copy()` whenever
  `view.is_dict_backed()`. It then re-parses the copied frame with `Parser<Index>{*owned_tv_}`.
- **Clone.** `fixpp_msg_clone` (`src/capi/message_write.cpp`) does the same into `fixpp_msg::owned_tv_`
  (`src/capi/capi_internal.hpp`).
- **What `membership_copy()` costs.** It copy-constructs the whole `table_view` (the out-of-line
  definition in `parser.hpp`), which means every hash container of every message in the dictionary.
- **Where the time was measured.** #494's Gate B found that the copy dominates the call. Its paired
  measurement is in the parent repo at `decisions/speckit/090-capi-refusals-verify.md` § *Gate B G-1*.
- **Why the copy exists** (066 Decision 4): a handle outlives its dispatch window, so it cannot
  borrow the session's table. A self-contained copy was the smallest surface that did not change the
  factory signature.

**Every view the Session hands an application comes from `parse_and_dispatch_`'s parser.**
`Session::parse_and_dispatch_` (`src/session/session.cpp`) builds `pd_parser` over `inbound_tv_` for
`fromApp`, `fromAdmin` and `toApp` alike. Re-derive:
- `grep -n "parse_and_dispatch_(" src/session/session.cpp`: every application callback is invoked
  inside one of those calls;
- `grep -rnE "Parser<[^>]*>[^(;]*[{(]" src` lists every production `Parser` construction, plus
  comment lines that spell one. At `3f200360` the code hits are `pd_parser`,
  `validate_inbound_`'s `vg_parser`, and the two copy sites' re-parse parsers.

The C ABI reaches the same parser:
- `CapiApplication::fromApp` / `toApp` (`src/capi/engine.cpp`) wrap the Session's `MessageView` in a
  stack `fixpp_msg` (`inbound.view = &msg`).
- A C inbound handle's view is therefore `pd_parser`'s output. A `fixpp_msg_clone` of it goes through
  the owned route once `pd_parser` is built over the owner (§2.6).
- `fixpp_session::tv_` (`src/capi/session.cpp`) is **not** an owner object for any view. The owner
  token of a C-ABI inbound view names the `Session`'s `inbound_tv_`.

### 1.2 #493 — the re-parse sites, one private field

`OffsetTable::Config` (`include/fixpp/wire/offset_table.hpp`) holds `max_offset_entries` and
`max_group_entries_per_instance`. `OffsetTable` stores it in the private `cfg_` and has no reader.
`OffsetTable::build` enforces the entry cap. The group-instance cap is enforced lazily, when groups
are read.

**Census condition:** a site re-parses a copied frame if it is a copy site (`grep -rn
"membership_copy()" src`, today's dict-backed branch) or that site's dict-free fallback (the
`MessageView` construction in the `else` branch next to it). At `3f200360` that gives the four
sites below, and each takes the **default** `Config`. Treat the table as a dated snapshot. The
condition, not the table, is what binds.

| # | Site | Route | Today |
|---|---|---|---|
| S1 | `owning_message_handle_from_frame` | dict-backed | `parser.parse(frame, mr)`, the two-argument overload |
| S2 | `fixpp_msg_clone` | dict-backed | `clone_parser.parse(fv, clone_mr)` |
| S3 | `owning_message_handle_from_frame` | dict-free fallback | `view_cache_.emplace(frame, mr)`, the dict-free two-argument `MessageView` constructor |
| S4 | `fixpp_msg_clone` | dict-free fallback | `make_unique<MessageView<Index>>(fv, clone_mr)` |

- **S1/S2 refuse** a raised-cap source. That is `B-458-2` / `B-458-1`, recorded as `L-458-2`.
- **S3/S4 degrade silently.** The copy "succeeds", but its `OffsetTable` build fails and every read
  on it reports absent. `MessageWrite.CloneDictFreeOversizedSourceStillReturnsOk` asserts only
  `FIXPP_ERR_OK`, so it is green over an **empty** clone today. That makes S3/S4 a live defect of the
  same class, not merely a case of consistency.

### 1.3 #486 — a `noexcept` context that moves a `table_view`

`explicit dictionary_driven_validator(fixpp::dict::table_view dict) noexcept : dict_{std::move(dict)} {}`
(`include/fixpp/wire/validator.hpp`).
- **MSVC.** The member move allocates, as measured in the issue comment. A `bad_alloc` there becomes
  `std::terminate`.
- **libstdc++ and libc++.** The move is nothrow, so nothing changes there.
- **Do not reintroduce `noexcept` on the move constructor.** `table_view.hpp`'s note beside the
  removed assertion forbids restoring an explicit `noexcept` on `table_view`'s own move constructor.
  **This change does not touch `table_view`.**
- **Production callers.** Re-derive with `grep -rn "dictionary_driven_validator>(" src`. At
  `3f200360` the only one is `Session::open`'s
  `std::make_unique<dictionary_driven_validator>(*inbound_tv_)`.
  - The by-value parameter is copy-initialised **at the call site**, outside the `noexcept`, so that
    site can already throw `bad_alloc` on every toolchain.
  - After the fix, an MSVC failure in the member move reaches the same handler instead of
    terminating. No new exception reaches any caller.

---

## 2. D-1 — #495, the owned route

### 2.1 Where the owner token lives

`MessageView<Mode>` gains one private member:

```cpp
std::shared_ptr<const fixpp::dict::table_view> const* dict_owner_ = nullptr;
```

- **How it is set.** It is a borrowed pointer to a `shared_ptr` **object**. `nullptr` means borrowed
  or dict-free. `Parser` sets it after constructing the view, and `MessageView` befriends
  `template <access_mode> friend class Parser;` so no public constructor changes.
- **Moves.** The defaulted move constructor carries it. Copy is already deleted.
- **Why a pointer to a `shared_ptr`, and not a `shared_ptr`.** Holding one by value in `Parser` or
  `MessageView` costs an atomic increment and decrement per parsed message on the inbound hot path.
  When sessions share a snapshot, that is a shared cache line. The pointer costs a store per parse.
  The price is a new lifetime dependency on the owner **object**, which §3.1 confines to audited
  holders (R-A).
- **Why not in `dict_hooks`.** See C-1. `MessageView` is where both copy sites read the source's
  table, and no pin constrains its size. Re-derive with the grep in C-1.
- **What grows.** `sizeof(MessageView<Index>)` and `sizeof(MessageView<Iter>)` grow by one pointer.
  The C-1 grep, run with `MessageView` in its alternation, finds no pin on either. Re-run it before
  implementing.

### 2.2 `Parser`'s owning constructor, behind a `detail` passkey (R-A)

```cpp
namespace fixpp::wire::detail {
// Passkey for the owned route. Constructible anywhere; `detail` is the "do not use" signal.
// Tests and benches name it, the way they already call detail::owning_message_handle_from_frame.
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
= delete;
// + private: std::shared_ptr<const fixpp::dict::table_view> const* owner_ = nullptr;
```

`parse(frame, mr)`, `parse(frame, mr, cfg)` and `parse_iter(frame)` then set `mv.dict_owner_ = owner_`.

**Why this shape:**
- **Passkey, not `friend`.** Tests and the bench build owned-route parsers (§10 T-7…T-11, T-14,
  T-15, T-17; §11's owned row). A friend list would exclude them.
  - The key is publicly constructible. `detail` **signals** that it is not a supported entry point;
    it does not stop external code from reaching it.
  - `explicit` on the default constructor forces the spelling `detail::owned_route_key{}` at every
    call, so a braced `{}` cannot hide it.
- **Lvalue only, with its own deletion.** `owner_` stores the argument's address, so a temporary
  would dangle.
  - The existing deleted `template <class TV> Parser(TV&&) requires(!is_lvalue_reference_v<TV&&>)`
    guards only the one-argument form.
  - The two-argument form needs its own deleted rvalue overload. Without it, a `SP&` parameter would
    accept a **const rvalue**, because `SP` deduces `const shared_ptr<…>` and `const&` binds to an
    rvalue. The forwarding-reference shape with `is_lvalue_reference_v` rejects both const and
    non-const rvalues.
- **Exact type (`same_as`), corrected rationale (triage N-4).** A `shared_ptr<table_view>` lvalue
  (non-`const` pointee) binds `SP&&` directly, so no conversion or temporary is created.
  - Without the constraint, the member initialiser `owner_{std::addressof(owner)}` is ill-formed.
    That error sits in the body, outside the immediate context, so `is_constructible_v` would
    report `true` for a construction that does not compile.
  - The constraint moves the rejection into overload resolution. T-7's wrong-pointee row is its
    witness.
- **No clash with the existing constructor.** The existing borrowed constructor takes one argument,
  so the two never compete.
- **Null owner.** `checked_owner` asserts before dereferencing, inside the member initialiser. Every
  production caller holds a non-null pointer by construction (§2.4, §2.6). In release builds, null
  is a violated precondition of a `detail` entry point.
- **Unchanged:** the borrowed `Parser{tv}`, `Parser()`, and every existing call site.

### 2.3 `shared_membership()` — private, reached through a `detail` accessor (R-A)

This is a sibling of `membership_copy()`, and it replaces `membership_copy()` at the two copy sites.
It is a **private** `MessageView` member, reached only through:

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
1. **Owned:** `*dict_owner_` when `dict_owner_ != nullptr && dict_owner_->get() == hooks_.opaque_dict()`.
   That is a refcount increment, with no allocation.
2. **Borrowed and dict-backed:**
   `std::make_shared<const fixpp::dict::table_view>(*static_cast<fixpp::dict::table_view const*>(hooks_.opaque_dict()))`.
   - This copies **in place from the table reference**, not from a `membership_copy()` prvalue
     (Root cause 3). The control block and the table share one allocation, which comes **first**.
     The copy's own allocations come **last**, and there is no `table_view` move.
   - That order keeps the two global-new OOM calibrations valid
     (`tests/dictionary/reify_membership_copy_oom_test.cpp`,
     `tests/capi/dict066_clone_membership_copy_oom_test.cpp`; §10 T-16 (b)). It also avoids adding an
     MSVC move, which would cost about 20 allocations, to every borrowed reify and clone.
   - It may throw `bad_alloc`, which is why the function is not `noexcept`. `membership_copy()` is
     not `noexcept` for the same reason (its FQ-1 note).
3. **Dict-free:** `nullptr`.

**What arm 1's identity check does and does not do.** It is defence in depth for one misuse: an
owner object reassigned while its old table is still alive. In that case the check misses and arm 2
copies the **old** table, which is the table this view was parsed against. It does **not** make a
dead or reassigned owner safe:
- reading a destroyed owner object is undefined behaviour before the comparison runs;
- if the old table has died, the view was already dangling under the borrowed-route precondition;
- a new table allocated at the dead one's address passes the check (ABA).

All three lie outside §3.1's precondition, which the `detail` gate confines to audited holders.

`membership_copy()` stays public and unchanged, for its other callers. Re-derive them with
`grep -rn membership_copy src include tests`.

### 2.4 The two copy sites

**Reify** (`src/dictionary/reify.cpp`):
- `impl::owned_tv_` becomes `std::shared_ptr<const table_view>`. When `view.is_dict_backed()`, the
  factory assigns it
  `wire::detail::message_view_membership_access::shared_membership(view)`.
- The re-parse is `Parser<Index> parser{wire::detail::owned_route_key{}, handle.pimpl_->owned_tv_}`,
  over the **impl's own member**. That member has a stable address: the impl never relocates, and a
  handle move moves only the pointer.
- **Consequence:** the handle's own `view()` is an owned-route view. Reifying or cloning it again
  shares the same table.
- The `assert(!owned_tv_.has_value())` and its "#456 seam 6: seated, not assigned" comment go away.
  #456's rule bars *assigning a `table_view*`. The table is now held behind a pointer and is never
  assigned.

**Clone** (`src/capi/message_write.cpp` / `capi_internal.hpp`):
- `fixpp_msg::owned_tv_` becomes `std::shared_ptr<const fixpp::dict::table_view>`, seated the same
  way.
- `clone_parser` is `Parser<Index>{wire::detail::owned_route_key{}, clone->owned_tv_}`, over the heap
  shell's member, which is stable.
- A clone of a clone therefore shares as well.
- `session_tv_` stays a separate member. Its non-null state means "outbound handle", and that
  meaning is unchanged.

**Stays untouched:**
- the factory signature;
- `reify.hpp`'s declarations;
- `emit_dispatch.cpp`;
- the generated dispatch bridge;
- `is_dict_backed()`;
- the three-way refusal contract of `B-458-1` / `B-458-2`.

**Spelling under G2** (`tools/check_dictionary_snapshot_exclusivity.sh`; flipped to "zero" by R-C,
§6.4):
- **What it matches.** A parenthesised construction `shared_ptr<const … table_view>(` whose first
  argument begins `std::move(`, or is an identifier followed by a comma.
- **Safe spellings in this change:**
  - copying or assigning a `shared_ptr` (`owned_tv_ = …shared_membership(view);`, `return *dict_owner_;`);
  - `std::make_shared<const table_view>(…)`, which contains no `shared_ptr<` token.
- ⚠️ **Spelling to avoid.** `std::shared_ptr<const table_view>(std::move(x))` is a one-argument move,
  but the pattern cannot tell it from the aliasing form, so it **fires**. The same applies to new
  **comments**: G2 does not strip comments, so no new prose may spell that form either.

### 2.5 D-1c — the handle's impl moves into `mr` (C-2; the owner accepted it as O-4)

**Today.** `owning_message_handle_from_frame` begins with `new owning_message_handle::impl{mr}`.
Every other allocation on the reify path already comes from `mr`:
- `bytes_`;
- the zero-cap carry (it allocates nothing, which T-16's probe asserts);
- the `OffsetTable`'s containers;
- `unk_items_`.

The global `new` is what keeps NFR-003-3's "no allocation outside `mr`" false. It would also fail
the owned-route mallocnesia gate (§10 T-14), however well the sharing works.

**Change:**
- The impl is allocated with `std::pmr::polymorphic_allocator<>{mr}.new_object<impl>(mr)`.
- `~owning_message_handle` and the move-assignment's release both go through one private helper. It
  recovers the resource from `pimpl_->bytes_.get_allocator().resource()` **before** destroying the
  impl, then calls `delete_object`.
- `mr` must already outlive the handle, because the handle's parsed view lives in it: `bytes_`,
  the `OffsetTable` and `unk_items_`. So this adds **no new lifetime obligation** (O-4's rationale).
- A monotonic `mr` reclaims the impl only on `release()`, exactly as it already does for `bytes_`.

**Effect on the OOM tests.** The impl allocation becomes the first `mr` call, so every cell that
fails a **fixed** `mr` ordinal through the factory now lands on a different phase. §10 T-16
recalibrates them by phase. Re-derive the cell population with
`grep -n "fail_on_call_n" tests/dictionary/*.cpp`, then confirm which entry point each cell calls.
`tests/dictionary/reify_oom_test.cpp`'s cells go through `owning_<Msg>::from_view`, not the factory.

**Rejected alternative:** a mallocnesia budget gate of 1 on the owned arm. It needs a
`--max-allocs` passthrough that `fixpp_add_mallocnesia_test` lacks, and it would leave NFR-003-3's
"no allocation outside `mr`" clause false. The owner took D-1c instead (O-4).

### 2.6 Session wiring, and the `inbound_tv_` invariant (N-3)

- **`parse_and_dispatch_`:** `pd_parser{*inbound_tv_}` becomes
  `pd_parser{wire::detail::owned_route_key{}, inbound_tv_}`, the owning constructor over the Session
  member.
  - This one edit puts every view handed to `fromApp` / `fromAdmin` / `toApp`, in C++ and C alike,
    on the owned route.
- **`validate_inbound_`'s `vg_parser` stays borrowed.** Its view never leaves the function, and
  borrowing it costs nothing.
- **The invariant `inbound_tv_` now carries memory safety.** It is the owner object of every
  dispatched view. Once `parse_and_dispatch_` can run, it must never be reassigned or reset.
  - **Where it is enforced.** `inbound_tv_` is written only in `Session::open`, and before
    `state_ = lifecycle::open`. `parse_and_dispatch_` runs only after that transition. After it,
    `open()`'s first check (`state_ != lifecycle::never_opened` → `session_already_open`) makes
    every further write unreachable. Re-derive the write census with
    `grep -n "inbound_tv_ *=" src/session/session.cpp`.
  - **Where it is commented.** `inbound_tv_`'s member comment (`include/fixpp/session/session.hpp`)
    gains this invariant and names this note.
  - **No assertion at the seating site.** Triage N-3 suggested one. `assert(!inbound_tv_)` would be
    wrong: an `open()` that fails after the assignment (for example, on the security-profile check)
    leaves `state_` at `never_opened`, so a legitimate retry reassigns `inbound_tv_` while no view
    exists. The state guard above is the enforcement.

---

## 3. Lifetime and concurrency

### 3.1 The owner object must outlive every view parsed through it

A borrowed-route view depends on three things: its frame, its parse arena, and the **pointee**
table. An owned-route view adds a fourth: the owner `shared_ptr` **object** at `dict_owner_`. That
object can die or be reassigned while the pointee lives on. **This is a new dependency**, and v0.1
wrongly denied it (Codex #1, triage #1).

**Precondition of the owned route:** the owner object outlives every view parsed through it and is
not reassigned while any such view may be shared.

R-A confines who can take on that precondition to code that names `detail::owned_route_key`.
Re-derive the production census with `grep -rn "owned_route_key" src`. It must list only the three
holders below; test and bench users carry the precondition themselves.

| Owner object | Why the precondition holds | Views pointing at it |
|---|---|---|
| `Session::inbound_tv_` | Single write in `open()` before dispatch can run (§2.6); destroyed with the `Session` | `pd_parser`'s output, which lives inside `parse_and_dispatch_`'s call on a live Session. C-ABI inbound handles wrap that same view. |
| `owning_message_handle::impl::owned_tv_` | Seated once by the factory; the impl never relocates and is never reassigned | `impl::view_cache_` |
| `fixpp_msg::owned_tv_` | Seated once by `fixpp_msg_clone`; the shell never relocates | `fixpp_msg::owned_view_` |

**The copy is taken while the owner is alive.** A handle or clone copies the `shared_ptr` inside the
dispatch window. After that it holds its own reference and never reads `dict_owner_` again.

### 3.2 Stable addresses

`for_table_view` stores `std::addressof(table)`. The span accessors alias the table's own vectors
("stable for lifetime"). Two properties keep this sound:
- a `shared_ptr`'s pointee never relocates;
- the owner objects are never reassigned while a view exists (§3.1).

The table is reached through the pointee, so moving a handle, which moves the impl **pointer**,
cannot invalidate it.

### 3.3 Sharing across threads

- **Reads.** After this change, N handles on N threads, plus the Session strand, read **one**
  `table_view` concurrently. That is safe only if every accessor the hooks and the validator call is
  `const` and has no hidden writes. `table_view` has no `mutable` member, no `thread_local` and no
  atomic.
  - Re-derive with `grep -nwE "mutable|thread_local|atomic" include/fixpp/dict/table_view.hpp`.
    Expect no output. Positive control: the same grep over `include/fixpp/wire/parser.hpp` prints
    hits.
  - A grep cannot stop someone adding a lazy cache later. **T-17** is the executed witness: two
    handles sharing one table read it concurrently under TSan.
  - Per `brain/components/dictionary.md`, this note does **not** call the object "immutable". A
    `const_cast` write through a span remains defined behaviour. No shipped path writes, and the
    #456 seal governs reachability, not writability.
- **Refcounts.** Copying the same `shared_ptr` object from several threads is safe (`const` access).
  Each holder then owns its own copy.
- **Destruction.** The last reference to a table may drop on any thread. After D-4, that reference
  never owns a `Dictionary` (§3.4), so destruction is `~table_view` alone.
  - `~table_view` deallocates only through default allocators (§6.1), that is, the thread-safe
    global heap.
  - It touches no Session state.
  - T-11 drops the last reference on a second thread.

### 3.4 What a handle pins

| Route | `inbound_tv_` is | A live handle or clone pins |
|---|---|---|
| C++ `Session`, no `dict_snapshot` | `make_shared<const table_view>(dictionary->as_table_view())` | the table only |
| C++ `Session` with `dict_snapshot`; **every C-ABI session** | a copy of the snapshot's own table owner (D-4, §6) | the table only, never the snapshot or the `Dictionary` |
| Borrowed `Parser{tv}` | none | its own deep copy (unchanged) |

- **No load-resource obligation remains for handles and clones.** The pinned object is a
  self-contained `table_view` that allocates only through default allocators (§6.1). The
  `Dictionary`, and with it the load resource, dies when the application's and the session's own
  references go, exactly as on `origin/main` before this change.
- **Outside this change, one object still pins the `Dictionary`:** an **outbound** C `fixpp_msg`,
  through `fixpp_msg::dict_`, which `fixpp_msg_create_outbound` copies from the session. That
  predates this note. D-5 (§7) makes its load resource `new_delete_resource()`, which is immortal
  and thread-safe. `fixpp_session::dict_` and a C++ caller's own `shared_ptr<const Dictionary>`
  follow the owner's lifetime, as they always have.

**Memory, per handle:**
- Before: one full table copy on the global heap.
- After, on the owned route: one refcount, plus the shared table until the last handle dies.
- **Tables freed early.** A caller that closes a session while it holds handles keeps that session's
  table alive for as long as any handle lives. That is the pin the owner accepted (O-1).

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
  `dict_hooks::none()`. `Parser{}.parse(frame, mr[, cfg])` already uses that constructor for every
  dict-free parse. The raised-cap sources in the existing clone tests are built that way.
- **No new refusals.** Calling the constructor directly, not `Parser::parse`, keeps the fallback's
  "never refuses, degrades in place" contract (`B-458-2`'s retained arm).
- **The root group context is normalised (Codex #5, triage #5).**
  - The four-argument constructor seeds the root `group_context` with the view's MsgType. The
    two-argument one leaves it empty. A copy therefore now carries the **same** root context as any
    source parsed through `Parser{}`.
  - Only a source built through the raw two-argument constructor sees a difference: its copy's
    context is now seeded where the source's is empty.
  - It is observable only through the public `OffsetTable::group_context_for()`. Its production
    consumer is `fixpp_msg_get_group`'s cursor seed (`src/capi/message_read.cpp`; re-derive with
    `grep -rn "group_context_for" src`). On a dict-free table that group read declines whatever the
    context (`B-220-1`).
  - It is recorded in `B-493-1`, and T-4 witnesses the `Parser{}`-parsed case.

**What a refused copy can still be, after the fix.** A copy re-parses its source's bytes under its
source's caps, so the re-parse fails only where the source's own build failed. Two ways reach that
state:
- the raw dict-backed `MessageView` constructor, which skips `build_status()` and is used only by
  tests;
- OOM in the copy's own arena.

`B-458-1` / `B-458-2` stay true as written, since their code lists are `translate()`'s image.
`B-493-1` records the narrowed reach (§12).

---

## 5. D-3 — #486, a conditional `noexcept` that can be checked on both sides

```cpp
explicit dictionary_driven_validator(fixpp::dict::table_view dict)
    noexcept(std::is_nothrow_move_constructible_v<fixpp::dict::table_view>)
    : dict_{std::move(dict)} {}
```

**The pin** isolates the constructor's **own** specification (C-3). It goes in a test TU that
includes `validator.hpp` with `table_view` complete, and that the MSVC legs build
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
- **SC-007 "no virtual edge".** The validator still holds its `table_view` by value, and no virtual
  function is added. It **cannot** join §2's sharing, because by-value holding is the frozen design
  point, and this note does not reopen it.
- **`table_view`'s move constructor** keeps its inferred specification, per `table_view.hpp`'s
  ⛔ note.

---

## 6. D-4 — the snapshot owns its table in its own control block (R-B, R-C)

### 6.1 Why this is safe: `table_view` is self-contained

A handle can pin the table without pinning the `Dictionary` only if the table holds nothing that
points back into the `Dictionary` or its load resource.
- `table_view`'s data members, from `valid_` to `has_nonstandard_pair_`, are standard containers on
  default allocators, or scalars. The nested types (`group_ctx_key`, `enum_domain`) hold `std::string`
  and `std::vector`.
- It has no `pmr` member, no `string_view` or `span` member, and no raw-pointer member.
- Re-derive, as a lead, with:
  ```
  awk '/^class table_view \{/,/^\};/' include/fixpp/dict/table_view.hpp | grep -vE '^\s*//' \
    | grep -E '^\s{4}[^(]*(pmr|string_view|span<|\*)[^(]*\s[a-z_]+_\s*(=[^;]*)?;'
  ```
  Expect no output. Positive control: the same grep over the **whole** header prints
  `valid_tag_set_view::set_`, the one pointer member. It belongs to a separate view type, not to
  `table_view`.
  - ⚠️ The recipe is a lead, not a proof. Its single-line pattern misses a member whose declaration
    wraps, as `valid_` and `required_` do (their types are `std::unordered_map<std::string, …>`,
    read by hand).
  - T-11 is the executed proof: under ASan it reads a pinned table after the `Dictionary` and its
    load arena are gone.
- The populators copy into `std::string` rather than viewing the `Dictionary`'s name pool
  (`table_view_builder`; read `as_table_view()`'s population calls).

### 6.2 The change

In `include/fixpp/dict/dictionary_snapshot.hpp` / `src/dictionary/dictionary_snapshot.cpp`:
- `view_` becomes `std::shared_ptr<const table_view>`. The constructor seats it with
  `std::make_shared<const table_view>(std::move(tv))`.
- `view()` returns `*view_`. `view_` is never null, because `make_shared` throws rather than
  returning null.
- `shared_dictionary_view(snap)` returns a **copy of `snap`'s `view_`**, or `nullptr` for a null
  `snap`. It stays `noexcept`, since a `shared_ptr` copy cannot throw. The aliasing construction in
  its body disappears.
- **Access.** `shared_dictionary_view` is a free function, and `view_` is private.
  - It cannot be befriended: G1's assertion (b) requires **exactly one** `friend` declaration in
    `dictionary_snapshot.hpp`, and R-C keeps G1 unchanged.
  - Instead the snapshot gains one public accessor, `[[nodiscard]] std::shared_ptr<const table_view>
    const& view_owner() const noexcept [[clang::lifetimebound]]`, and `shared_dictionary_view`
    returns `snap->view_owner()`.
  - `shared_dictionary_view` stays the call both consumers use. It keeps the null handling, and the
    two call sites do not change.
  - The accessor exposes only a `const` table, the same thing the helper returns, so C1's closure
    (`.specify/215-dictionary-view.md` §3) is unaffected.
- **Provenance is unaffected.** `Session::open`'s C4 check compares `source()` only. Re-derive with
  `grep -n "source()" src/session/session.cpp`.
- **Consumers.** `grep -rn "shared_dictionary_view(" src` lists the production readers of the
  snapshot's table owner. At `3f200360` they are `Session::open` and `fixpp_session_open`
  (`src/capi/session.cpp`).
- **Header pointer.** Per the parent `CLAUDE.md`, `dictionary_snapshot.hpp`'s header comment names
  this note as superseding 215's alias design.

### 6.3 Cost (config time only, `[const §XV.1]`)

Relative to 215 §4's specified path:

| path | `table_view` moves | allocations |
|---|---:|---:|
| 215 §4, specified (`make_shared<const dictionary_snapshot>`) | 2 | 1 |
| **D-4** | **2** | **2** |

- The moves are unchanged. The prvalue initialises the by-value parameter (move 1). Move 2 used to
  go into the `view_` member and now goes into the table's `make_shared` block.
  - On MSVC each move costs allocations (#486's comment). That is config-time and unchanged in
    count.
  - L-456-2's move-path list gains this path, and loses the member move it replaces (§12).
- The table's control block adds one allocation per snapshot.
  - Nothing pins the count. Re-derive with
    `grep -rn make_dictionary_snapshot tests/ bench/ | grep -i 'alloc\|count\|mallocnesia'`, which
    is empty.
  - Positive control: `grep -rn make_dictionary_snapshot tests/ bench/` alone prints the snapshot's
    users.

### 6.4 G2 flips to "zero aliases" (R-C)

`tools/check_dictionary_snapshot_exclusivity.sh`'s G2 section:
- **The assertion becomes `g2_all_n -eq 0`.** Any aliasing `shared_ptr<const table_view>`
  construction would re-pin whatever object owns its control block. For a snapshot, that is the
  `Dictionary`.
- **The `G2 DEAD` liveness line (`-ge 1`) is deleted.** The flipped tree fails it by design. The
  seeded positives below replace it: they show that the pattern can still report non-zero.
- **The "(in the factory: N, elsewhere: M)" split and assertion (b) collapse into the one count.**
- **Scope is stated honestly.** G2 scans `src/ include/ bindings/ tools/ tests/`, so "zero" is
  **tree-wide**. That is stricter than R-C's "in production": a test that needs an aliasing
  construction would need an allowlist. None does today.
- **G2 has no self-exclusion, unlike G1's `SELF` / `SELF_TEST`, and does not strip comments.** The
  gate's own prose and its self-test live under `tools/`. Any literal alias spelling added there
  would therefore read as a hit.
  - The self-test **assembles its seed text from fragments at run time**. The fragments are held
    in variables and concatenated only when the seed is written, so no line of the self-test source
    matches the pattern.
  - Alternatively, G2 gains a `SELF` / `SELF_TEST` exclusion that mirrors G1's. The implementer
    picks one and records which.
- **G1 is unchanged.**
- **The "required helper calls" census does not exist in the shipped script.** 215 v0.4's header
  describes one. PR #262 replaced it with **behavioural** pins, and
  `grep -n "shared_dictionary_view" tools/*.sh` finds only a comment. The pins are:
  - `CapiGroupDelimiterCtx.SessionHandleAliasesDictionarySnapshotControlBlock`
    (`tests/capi/capi_group_delimiter_ctx_test.cpp`): `sess->tv_.use_count() > 1`. It still tells a
    share from a copy under D-4, because a copy owns a fresh control block and reads 1.
  - `SessionTableViewReuse.OpenAdoptsAConfigSuppliedSnapshotAndWalksZeroTimes`
    (`tests/session/test_session_table_view_reuse.cpp`): amended by T-19, because
    `snap.use_count()` no longer rises across `open()`.

**Self-test** (`tools/test_dictionary_snapshot_exclusivity_gate.sh`; today it seeds only G1 cases):
see §10 T-18.

---

## 7. D-5 — the C loader uses `new_delete_resource()` (R-D)

### 7.1 The change

- `fixpp_dict_load_from_xml` (`src/capi/dictionary.cpp`) calls
  `load_any(path, std::pmr::new_delete_resource())` instead of
  `load_any(path, std::pmr::get_default_resource())`.
- **Why.** An outbound `fixpp_msg` copies the session's `Dictionary` into `fixpp_msg::dict_`
  (`fixpp_msg_create_outbound`, `src/capi/message_write.cpp`). Its destruction can therefore be the
  last reference, on any thread. Before the fix, `~Dictionary` then deallocated into whatever default
  resource the host had installed at load time. That resource might be dead, or might not be safe
  to use from that thread.
  - Re-derive the holders with `grep -n "dict_ = " src/capi/*.cpp`.
  - `new_delete_resource()` is immortal and thread-safe, so both halves close.
- **`include/fix/c_api/dict.h`'s doc comment** changes from
  "Wraps fixpp::dict::XmlLoader::load(path, std::pmr::get_default_resource())" to name
  `fixpp::dict::load_any(path, std::pmr::new_delete_resource())`.
  - It adds that a host's `std::pmr::set_default_resource()` does not affect the dictionary's
    storage, and marks this **BREAKING (C-ABI 1.8)**.
  - The old text also named `XmlLoader::load`, where the code calls `load_any`. The sentence is
    being rewritten anyway, so it is corrected in the same edit.
  - ⚠️ Edit only that comment's words. Never reformat `include/fix/c_api/*.h`.

### 7.2 Classification under `[const §X.7]`

- **Deciding clause.** §X.7's breaking-change definition incorporates `.specify/api-contract.md` §11,
  which lists changing a C-ABI symbol's *"documented ownership, lifetime or reentrancy rule"*.
  `dict.h` documents the resource that backs the returned `Dictionary`'s storage. Changing that
  resource changes a documented ownership rule.
  - A C++ host that installed a counting or limiting default resource around the call observes the
    difference.
- **Reading taken: breaking.** Before the first public release, §X.7 requires the steps below. They
  apply provided `gh release list --exclude-drafts` lists nothing; re-run it at implementation. It
  printed nothing on 2026-09-23.
  - **MINOR 7 → 8.** `FIXPP_C_ABI_VERSION_MINOR` in `include/fix/c_api/version.h`, with a re-authored
    trailing comment naming this change.
  - **BREAKING markers** in `dict.h`'s comment, in the PR description, and in the B&L delta (§12).
  - **Every in-repo consumer updated in the same PR.** The pin population is the one
    `.specify/447-458-452-capi-refusals.md` §5b enumerates for 1.7, re-derived for 1.8:
    - `tests/capi/version_test.cpp`: the exact-version cell's name and its minor assertion, and
      `CompositeMacroValue`'s composite;
    - the freeze manifest (below);
    - Tier 2 prose pins such as `src/capi/version.cpp`'s header comment and `version.h`'s narrative.
    - Re-derive the candidates repo-wide, which covers the binding, tests and interop harnesses
      §X.7 names: `git grep -ln "VERSION_MINOR\|0x010700\|1_7_0\|(7U << 8U)" -- . ':!specs'`. Most hits
      compare against the macro and move automatically. 447 §5b's tiers classify which ones are
      hard pins.
    - No error code is minted, so `introducing_minor()` and
      `tools/abi_history/error_codes_v1.txt` do not change.
- **The other reading, recorded so it can be judged.** One could argue the resource is an
  implementation detail and not a "result". But the documentation does specify it, and §X.7 counts
  "changing output the documentation leaves unspecified" as additive only when it is *un*specified.
  Under version.h's rule, "additive changes bump MINOR too", so **MINOR bumps either way**. The
  readings differ only in the BREAKING marker (§15 Q-4).
- **Freeze manifest re-pin** (`tools/capi_freeze.sha256`, gate `tools/check_capi_freeze.sh`):
  1. edit `dict.h` and `version.h`;
  2. run `bash tools/check_capi_freeze.sh`. It must **fail** on exactly the headers edited, which is
     the positive control. `include/fix/c_api.h` carries no C-ABI version literal (it has only the
     C++ library's `FIXPP_VERSION_*`), so it should not be among them;
  3. replace those two manifest lines with `sha256sum include/fix/c_api/dict.h include/fix/c_api/version.h`
     output;
  4. re-run the gate. It must **pass**, reporting the same header count as before.

### 7.3 Who else is affected

- **C++ callers are unaffected.** They choose their own load resource. After D-4, no handle or clone
  pins their `Dictionary`.
- **The Python binding** wraps the C ABI's `dict_load_from_xml`, so it gets the fix transparently.
  Re-derive with `grep -n "dict_load_from_xml" bindings/python/fixpp.i`.

---

## 8. Alternatives rejected

| Alternative | Why rejected |
|---|---|
| Owner token inside `dict_hooks` (the brief's proposal) | Grows `entry_context` (size-pinned; generated flyweights must match it), `OffsetTable` (whose cap bands are tuned to its size) and every nested sub-table allocation. It also costs a copy per group descent. C-1. |
| `MessageView` / `Parser` holding a `shared_ptr` by value (triage root cause 1(b)) | Safe by construction, and it could stay public. But it costs an atomic RMW pair per inbound message on the hot path (`[const §VIII.5]` spirit). R-A chose the zero-atomic `detail` route. |
| A `weak_ptr` token | Still costs atomics, and adds a lock step. |
| A token owned by the `Parser`, relying on `parse()`'s `[[clang::lifetimebound]]` | At both copy sites the `Parser` is block-local and its view is moved into the impl or the shell, so views routinely outlive their `Parser`. |
| Thread a dictionary through the factory, pimpl, bridge and `dict::reify` (066's mechanism (a)) | Changes the factory signature, which forces an `emit_dispatch.cpp` edit and a bridge regeneration. 066 rejected it, and O-1 keeps that. |
| `shared_ptr<const Dictionary>` pin, rebuilding the table from the `Dictionary` | `as_table_view()` is a full dictionary walk. Rebuilding per handle is slower than the copy it replaces, and `[const §XV.1]` bars it off config time. |
| `table_view` refcounted internally | Every copy would share, including the validator's by-value copy that SC-007 froze, and #456's sealed type would change. |
| O-5 as ruled in v0.1: keep the alias and document `L-495-2` | Its C-ABI premise was false, and it missed the deallocation-thread obligation. Superseded by R-B. |
| (i) A second, table-only owner **copied** at `open()` on the snapshot route | One resident `table_view` per snapshot-route session, including every C-ABI session. (i′) gets the same property without the copy. |
| (ii) D-5 alone | Closes the C half only; C++ `dict_snapshot` callers keep the obligation. Taken **with** D-4, for the outbound-handle pin D-4 does not reach. |
| (b) Refuse reify/clone on the borrowed route | Rejected by the owner. It breaks every existing borrowed caller, tests included. |
| (c) Amend NFR-003-3 only | Rejected by the owner. It leaves the shipped Session path at hundreds of µs. |
| #493 via a shared `reparse_like(src_view, frame, mr)` helper in `wire` (#493's own suggestion) | The sites come in two shapes: `Parser::parse`, which refuses, and a raw constructor, which degrades. A helper would have to take both modes. A one-line accessor gives the same single source of truth. |
| #493: new dict-free `MessageView(frame, mr, Config)` constructor for S3/S4 | The four-argument constructor with `none()` is already the in-tree dict-free route (§4). |
| #486 option 2: `const&` plus copy | The issue comment measured a copy at more allocations than a move, so it keeps `noexcept` while enlarging what it would terminate on. |
| #486 option 1: drop `noexcept` unconditionally | Loses the nothrow promise on the lanes where it is true. The conditional form is exact on both. |
| #486: `is_nothrow_constructible_v<V, table_view&&>` as the pin | Cannot fail on any toolchain (C-3). |

---

## 9. Public API delta (`[const §XVII.1]`)

| Symbol | Header | Change |
|---|---|---|
| `wire::detail::owned_route_key`, `wire::detail::checked_owner` | `include/fixpp/wire/parser.hpp` | **new**, `detail` (not a supported entry point) |
| `template <class SP> Parser<Mode>::Parser(detail::owned_route_key, SP&&)`: lvalue `shared_ptr<const table_view>` only; rvalue overload deleted | `include/fixpp/wire/parser.hpp` | **new**, reachable only through the `detail` key |
| `wire::detail::message_view_membership_access::shared_membership(MessageView<M> const&)` | `include/fixpp/wire/parser.hpp` | **new**, `detail` |
| `MessageView<Mode>` | `parser.hpp` | +1 private member; one private member function; befriends `Parser` and the `detail` accessor; grows by one pointer. No public signature changes. |
| `OffsetTable::Config OffsetTable::config() const noexcept` | `include/fixpp/wire/offset_table.hpp` | **new**, public |
| `dictionary_driven_validator::dictionary_driven_validator(table_view)` | `include/fixpp/wire/validator.hpp` | exception specification `noexcept` → `noexcept(is_nothrow_move_constructible_v<table_view>)`: unchanged on libstdc++ and libc++, **narrowed** on MSVC |
| `dictionary_snapshot::view_owner() const noexcept` | `include/fixpp/dict/dictionary_snapshot.hpp` | **new**, public (§6.2) |
| `shared_dictionary_view` | same | signature unchanged; returns a copy of the snapshot's table owner instead of an alias |

**Behaviour deltas with no C++ signature change:**
- `dict::reify` / `detail::owning_message_handle_from_frame` share the table on the owned route and
  allocate the impl from `mr`.
- `fixpp_msg_clone` shares the table on the owned route.
- Both inherit the source's caps (`B-493-1`).
- A snapshot's table no longer keeps the snapshot or its `Dictionary` alive.

**C ABI (§7):** `dict.h`'s doc comment, `version.h`'s MINOR, and the freeze manifest change. No
prototype, error code or symbol-golden line changes. The Python binding exposes none of the C++
symbols above.

---

## 10. Tests — TDD order; each RED names the mutation that must turn it red

**Conventions:**
- "RED today" means the test fails on `3f200360`.
- A test for a symbol that does not yet exist is RED by failing to compile. The implementer records
  the compile error as the RED.
- Mutations run in a **scratch copy**, never the working tree (constitution v3.0; the orchestrator
  does not implement).
- "Owned-route parse" means `Parser<Index>{wire::detail::owned_route_key{}, sp}`. The test keeps `sp`
  alive for as long as §3.1 requires.

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
  - Both twins also assert `copy.offsets().group_context_for(t) == source.offsets().group_context_for(t)`
    for a `Parser{}`-parsed source, where `t` is any count tag. This witnesses §4's normalisation.
  - Both are RED today, because the copy is empty.
  - Mutations: S3 or S4 back to two arguments, each on its own. The context assertion goes RED under
    the two-argument form.
- **T-5 — the second cap.**
  - A dict-backed source with raised `max_offset_entries` **and** raised
    `max_group_entries_per_instance`, carrying one group instance longer than the default
    per-instance cap. Clone and reify each read that group.
  - Mutation: inherit only `max_offset_entries`. The group read on the copy must fail. This is what
    proves the **whole** `Config` travels.
- **T-6 — a replacement route for clone → `WIRE_LIMIT_EXCEEDED`.**
  - After T-3 nothing else witnesses this. Re-derive with `grep -rn WIRE_LIMIT_EXCEEDED tests/capi`.
    Besides T-3, it should show only translate-table and read-path hits.
  - New test: `MessageWrite.CloneOfUnbuiltOversizedSourceYieldsWireLimitExceeded`.
    - The source is the 4100-field frame through the **raw** dict-backed `MessageView` constructor
      at the default cap, so its own build failed. This is the same lever
      `…MalformedFieldYieldsWireInvalidFrame` uses.
    - Assert the exact code, `clone_out == NULL`, and an unchanged source state.
  - This is a **regression pin** (GREEN before and after), not a RED.
  - Mutation: map the re-parse error through a constant instead of `translate()`.

### #495
- **T-7 — constructor constraints**, as `static_assert`s in a wire test TU. With
  `K = wire::detail::owned_route_key` and `S = shared_ptr<const table_view>`:
  - `is_constructible_v<Parser<Index>, K, S&>`: true
  - `… K, S const&`: true
  - `… K, S&&`: false
  - `… K, S const&&`: false
  - `… K, shared_ptr<table_view>&`: false
  - Mutations:
    - drop the `same_as` constraint. The wrong-pointee row fires (N-4);
    - change the parameter to `SP&` and delete the rvalue overload. The `S const&&` row fires;
    - delete only the rvalue overload. The `S&&` row fires.
- **T-8 — `shared_membership()` arms**, through the `detail` accessor, with `MessageViewSharedMembership.*`:
  - **owned:** `s.get() == sp.get()` and `use_count` +1;
  - **borrowed:** `s.get() != sp.get()`, and the content is equivalent (the same `field_valid_for`
    over the frame's tags);
  - **dict-free:** `nullptr`;
  - **reassigned owner, old table alive:** keep the old table alive through a second reference, then
    reassign `sp`. The result is a copy of the **old** table. This is the one misuse arm 1 is
    defence against (§2.3). Owner-object death and ABA are outside the precondition, and no test
    probes them.
  - Mutations: always copy (the owned arm goes RED); drop the identity check (the reassigned arm
    goes RED).
- **T-9 — reify shares.**
  - Setup: an owned-route source, reified.
  - Asserts:
    - `handle.view().hooks().opaque_dict() == sp.get()`;
    - reifying `handle.view()` again yields the same address.
  - RED today: a distinct copy.
  - Mutation: arm 1 → arm 2.
- **T-10 — clone shares.**
  - Via `capi_internal.hpp`, as the existing clone tests reach `h.msg`:
    `clone->owned_tv_.get() == sp.get()`, and a clone of the clone shares too.
  - RED today: the type is `optional<table_view>`, so the test fails to compile, and after that the
    address differs.
- **T-11 — a pinned table outlives the `Dictionary` and its load arena** (ASan and TSan lanes;
  replaces v0.1's `L-495-2` test).
  - **Setup:**
    1. Heap-allocate a `monotonic_buffer_resource` **and** its buffer (`unique_ptr` for both). A
       deallocation after destruction is then a heap-use-after-free that ASan reports; a stack
       arena would be a stack-use-after-scope.
    2. Load the `Dictionary` into it, `snap = make_dictionary_snapshot(dict)`, and
       `sp = shared_dictionary_view(snap)`.
    3. Owned-route parse of a NoLegs-bearing frame, in a separate parse arena. Reify into a
       separate handle arena that lives to the end, and clone.
    4. Destroy, in order: the source view, the frame and the parse arena (so no view parsed through
       `sp` outlives it, per §3.1), then `sp`, the test's `dict`, `snap`, and last the load arena.
  - **Second thread:** read the NoLegs group, membership-bounded, from both the handle and the clone.
    Then drop the handle and the clone **there**, so the last reference to the table dies off the
    loading thread.
  - **Asserts:** both reads succeed and are membership-bounded. ASan and TSan are clean.
  - **RED arm (ASan only; TSan cannot see a use-after-free).**
    - A scratch-copy mutant restores the pre-(i′) alias in `shared_dictionary_view`: compute
      `table_view const* p = snap->view_owner().get();` **before** moving `snap`, then return the
      aliasing construction over `std::move(snap)` and `p`.
    - The handle then pins the snapshot and the `Dictionary`.
    - The second thread's final drop runs `~Dictionary` into the destroyed load arena, and ASan
      reports heap-use-after-free.
    - The mutant exists only in the scratch copy. The G2 gate would reject it in the tree.
- **T-12 — borrowed-route survival, kept as a pin.** `GroupMembershipSurvivesSourceDestruction`
  builds its source with `Parser{tv}`, and it stays unchanged and green. It is the witness that the
  borrowed route keeps its self-contained copy.
- **T-13 — shipped path, through real `Session` dispatch** (066 Decision 6).
  - A C++ `Application::fromApp` reifies two consecutive inbound messages.
  - Assert: both handles' `view().hooks().opaque_dict()` are equal.
  - RED today, because each handle has its own copy.
  - Mutation: revert `pd_parser` to the borrowed `{*inbound_tv_}`.
  - C-ABI twin (engine loopback): the recv callback clones two inbound handles. The test compares
    their `owned_tv_` through `capi_internal.hpp`, destroys the engine, then reads a group from each
    clone under ASan.
- **T-14 — the owned route performs no global-heap allocation** (owner, 2026-09-23).
  - New binary: `tests/alloc_guard/test_reify_owned_alloc_guard.cpp`.
  - Dual gate, per `test_dict066_grouped_read_alloc_guard.cpp`:
    - a TU-local `operator new` counter, compiled out under `FIXPP_SANITIZER_REPLACES_NEW`;
    - the mallocnesia markers.
  - Setup: the source is an owned-route parse, done outside the window. `mr` is a
    `monotonic_buffer_resource` over a stack buffer with a `null_memory_resource()` upstream, so an
    undersized buffer fails as `dict_reify_oom` rather than reaching the heap.
  - Window: `dict::reify(view, profile, &mr)`, a field read, and the handle's destruction. `sp` is
    still held, so the destruction is not the last reference.
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
- **T-15 — the inbound parse stays zero-alloc.**
  - `test_dict066_grouped_read_alloc_guard.cpp` mirrors `parse_and_dispatch_`'s construction, which
    becomes the owned-route parse. Each of its tests gains an owned-route arm, and the borrowed arms
    stay. Its existing `dict066_grouped_read_alloc_guard_mallocnesia` registration covers the new
    arms with no CMake change.
  - `wire_alloc_guard_test_mallocnesia` covers the dict-free parse and serialise path.
  - ⚠️ **No mallocnesia gate covers `Session` dispatch itself.** `tests/alloc_guard/CMakeLists.txt`
    deliberately leaves `alloc_guard_dispatch` and `alloc_guard_session` ungated, because their
    windows wrap `co_spawn` / `ioc.run()`. The parse-level mirror is the only gate. This change does
    not widen that gap: it stores one more pointer and allocates nothing.
- **T-16 — OOM recalibration by phase (Root cause 4; Codex #6).**
  - **(a) PMR cells.**
    - **Probe.** Run `owning_message_handle_from_frame(rmv, wire::MessageView<Index>{}, &r)` with
      `r` a counting `failing_pmr_resource` (`fail_on_call_n = 0`) over an **empty** default view.
      Call the resulting count `I`.
    - **The precondition, stated in the test.** On the empty path, `bytes_.assign` of zero bytes,
      the zero-cap `pmr_carry_buffer`, the `Framer`, and the default `view_cache_.emplace()` allocate
      nothing from `mr`.
    - **The probe asserts `I == 1`.** That is the impl's single `new_object` allocation, and the
      assertion witnesses the precondition as well: if any of those steps allocated, `I` would
      exceed 1.
    - Then, for a real frame, `bytes_` is call `I+1`, and the first `OffsetTable`-build call is
      `I+2`. The zero-cap carry and the framer allocate nothing on the non-empty path either, since
      a complete frame never appends to the carry.
    - `ViewRebuildOomDegradesNotTerminate` fails `I+2`, and `SpuriousHitControl_…` fails `I+1`.
    - **Impl arm:** failing call `I` yields `dict_reify_oom`, with no handle and no leak under ASan.
    - **Mutation: revert D-1c.** `I` becomes 0, and the probe's `I == 1` goes RED. This is what
      v0.1's "call 1" arm could not show.
  - **(b) Global-new cells (Root cause 3; triage N-1).**
    - `reify_membership_copy_oom_test.cpp` arms `fail_at = t_dict`. Its premise is that the copy's
      K allocations are the last K.
    - `dict066_clone_membership_copy_oom_test.cpp` arms `dict_total - 1`. Its premise is exactly one
      further global allocation, the `make_unique<MessageView>`.
    - Both premises hold again under arm 2's in-place spelling.
    - **Witness (libstdc++ lane):** a `gdb` backtrace at each armed ordinal lands inside
      `table_view`'s copy constructor.
    - **Mutation:** the prvalue spelling
      `make_shared<const table_view>(membership_copy())`. The armed ordinal then lands in the
      control-block allocation, `__allocate_shared`.
  - `FIXPP_SKIP_ON_MSVC_DEBUG_ARENA` still applies on MSVC debug.
- **T-17 — concurrent readers of one shared table** (TSan; Codex #7, triage #7).
  - Two handles reified from owned-route sources over the same `sp` share one table (assert
    `opaque_dict()` equality first).
  - A `std::barrier` releases two threads together. Each loops over membership-bearing group reads
    plus `unknown_fields()` / classification reads on its own handle.
  - Pass: TSan is clean.
  - Mutation (scratch copy): add a `mutable` lazily-filled cache to a `const` `table_view` accessor on
    that path. TSan reports a race.
  - Kept separate from T-11's refcount-drop arm.

### D-4 / R-C (the snapshot and its gate)
- **T-18 — G2 seeded positives** in `tools/test_dictionary_snapshot_exclusivity_gate.sh`.
  - Clean tree: exit 0, and the log shows `G2 alias-formation sites = 0`.
  - Seed 1: the `(std::move(` spelling, appended to `src/dictionary/dictionary_snapshot.cpp` with a
    backup and a restore trap, the same mechanism the A5 cases use.
  - Seed 2: the `(identifier,` spelling, same file.
  - Each seed: the gate exits 1 **and** its log contains `G2 FAIL`. The exit code alone is not
    enough, because G1 runs first and any unrelated exit-1 would pass a bare check.
  - The seed text is assembled from fragments at run time (§6.4). A self-test line that spells the
    alias must not appear in the source.
  - ⚠️ The self-test already has an `EXIT` trap that restores the A5 TU. A second `trap … EXIT`
    would replace it. The seed file's restore must go into that same handler.
  - Mutation: revert the G2 assertion to `-eq 1`. The clean-tree case goes RED.
- **T-19 — amended control-block pins.**
  - `DictionarySnapshot.SharedDictionaryViewAliasesRatherThanCopies`
    (`tests/dictionary/dictionary_snapshot_test.cpp`) is renamed for sharing the table owner.
    - It **keeps** `owner.get() == &snap->view()`, which is identity with the snapshot's own table.
      "A different control block from `snap`" is true of a copy too, so identity is what
      discriminates.
    - It replaces the two `owner_before` assertions, which asserted the same control block as the
      snapshot.
    - It **adds the direct (i′) witness:** sample `dict.use_count()` before minting the snapshot.
      After `snap.reset()`, with the table owner still alive, the count returns to that sample.
      Under the old alias it stays elevated.
    - It keeps `use_count() == 1` and the post-reset read.
    - Mutation: the T-11 alias mutant. The `dict.use_count()` assertion goes RED.
    - ⚠️ The TU's five `static_assert` blocks (A1–A5) stay byte-for-byte. The gate self-test's awk
      requires exactly five of them to rewrite.
  - `SessionTableViewReuse.OpenAdoptsAConfigSuppliedSnapshotAndWalksZeroTimes`
    (`tests/session/test_session_table_view_reuse.cpp`) samples
    `shared_dictionary_view(snap).use_count()` before and after `open()`, instead of
    `snap.use_count()`.
    - Both samples include the temporary's own reference, so the comparison stays like-for-like.
    - Mutation: `open()` copies the table (`make_shared<const table_view>(snap->view())`). The rise
      disappears.
  - `CapiGroupDelimiterCtx.SessionHandleAliasesDictionarySnapshotControlBlock`: the assertion
    stands. Its name and message text drop "alias".

### D-5 / R-D (the C loader)
- **T-20 — a C dictionary is not allocated from the installed default resource.**
  - New test in `tests/capi/`, `CapiDictionary.LoadDoesNotRetainInstalledDefaultResource`.
  - Install a counting `memory_resource`, one that tracks bytes currently held, with an RAII guard
    that restores the previous default. Call `fixpp_dict_load_from_xml`.
  - Assert the bytes the counting resource **still holds** after the call returns are 0.
    - Do not assert a total allocation count. A default-constructed `pmr` temporary inside the
      loader may allocate and free transiently, and a count would then go RED on the fixed tree.
  - Then `fixpp_dict_destroy`, restore, and run under ASan.
  - RED today (the positive control): held bytes > 0, because the `Dictionary`'s metadata lives on
    the installed resource.
  - Mutation: revert to `get_default_resource()`.
  - If the test stays RED after the one-line fix, the loader has a `get_default_resource()` fallback
    for storage the `Dictionary` keeps. That is a **finding** to fix in the loader, not a test
    defect.
- **T-21 — the C-ABI version and freeze pins.** The §7.2 pin population is updated. The
  `check_capi_freeze.sh` fail-then-pass sequence of §7.2 is recorded in the verify record.

---

## 11. Bench plan (`bench/dictionary/reify_bench.cpp` only; no bench CMake edit)

**Baseline first.** The base measurement is taken on `3f200360` **before any edit** (owner).

**Rows:**

| Row | Route | Frame | Judged against |
|---|---|---|---|
| `BM_Reify_DictBacked_20tag` (existing) | borrowed `Parser{tv}` | the existing frame, **< 20 fields** | base-vs-head, no regression beyond +5% (`[const §VIII.2]`) |
| `BM_Reify_Dispatch_20tag` (existing) | returns before dispatch | no MsgType | base-vs-head, no regression |
| `BM_Reify_DictBacked_Owned_20field` (**new**) | owned-route parse (`detail::owned_route_key`) | a v44 NewOrderSingle with ≥ 20 fields, all valid for `D` in FIX44 | NFR-003-3's **1.2 µs** ceiling |
| `BM_Reify_DictBacked_Borrowed_20field` (**new**) | borrowed, same frame as the row above | same | reported beside the owned row, so the copy's cost is read on one frame |

**Honest comparator.**
- The owned row cannot build at base, because the constructor does not exist there. It is judged
  against the NFR ceiling only.
- Base-vs-head is read on the existing rows. `BM_Reify_DictBacked_20tag` stays borrowed, so it will
  **not** move toward the ceiling.
- Its expected head delta is small but not zero:
  - borrowed arm 2 adds a control block, in the same allocation as the table (`make_shared`);
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
  Re-derive with `grep -n reify_bench bench/ci-suite.txt`.

---

## 12. Spec, B&L, catalogue and brain deltas

**Spec:**
- **`specs/003-dictionary-codegen/spec.md`, NFR-003-3.** Amend the `dict::reify` clause to:
  > ≤ 1.2 µs (20-tag) **for a source view delivered by the shipped `Session` dispatch path** (C++
  > `Application` callbacks and C-ABI inbound handles), and for handles and clones derived from one.
  > A view a caller parses itself through a borrowed `Parser{tv}` pays a per-handle
  > `membership_copy()` and is **not** held to this ceiling (`L-495-1`). It is held to no
  > regression against the merge-base.
  >
  > "No allocation outside `mr`" likewise covers that path.

  The row carries an in-place *"Amended (fixpp#495, 2026-09-…)"* note.
- **066 records.** `research.md` Decision 4 and `data-model.md`'s "Reify owning handle owned
  table_view" / "Clone-owned table_view" entities get an in-place *"Superseded in part by
  `.specify/495-493-486-dict-reify-copy.md`"* note. The "without a pin" clause no longer describes
  the owned route. The history stays.
- **`.specify/215-dictionary-view.md`** gets a header pointer: *"The aliasing design (§3's
  `shared_dictionary_view`, §5b's 'third owner of the snapshot's control block', §6 seam 7's G2) is
  superseded by `.specify/495-493-486-dict-reify-copy.md` §6 (D-4): the snapshot owns its table in its
  own control block, and G2 asserts zero aliases."* The body stays as history.

**B&L** (`spec/behaviors-and-limitations.md`), a new section with:
- **`B-495-1`**: on the shipped dispatch path, `dict::reify` and `fixpp_msg_clone` share the source's
  table by reference count. A live handle or clone keeps that **table** alive, never the
  `Dictionary`.
- **`L-495-1`**: a source parsed through a borrowed `Parser{tv}` still deep-copies the table per
  handle. It is above NFR-003-3's ceiling and scoped out of it.
- **`B-495-2`** (D-4): a `dictionary_snapshot`'s table outlives the snapshot for as long as a view
  of it is held. The snapshot's `Dictionary` does not.
- **`B-495-3`** (D-5), **BREAKING (C-ABI 1.8)**: `fixpp_dict_load_from_xml` allocates from
  `std::pmr::new_delete_resource()`. A host's installed default resource no longer backs a C
  dictionary.
- **`B-493-1`**: clone and reify re-parse under the source's `OffsetTable::Config`.
  - A raised-cap source now copies, including through the dict-free fallbacks, which used to return
    an empty copy.
  - A dict-free copy's root group context now matches the parsed-source form (§4).
  - `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` from clone is reachable only from a source whose own build
    failed.
- **`B-486-1`**: `dictionary_driven_validator`'s constructor is `noexcept` exactly where
  `table_view`'s move is nothrow. On MSVC an allocation failure in it propagates as `bad_alloc`.
- **`L-458-2`** moves to `spec/behaviors-and-limitations-closed.md`, marked resolved by fixpp#493.
- **`L-456-2`** stays live. Its ⚠️ paragraph changes in three places:
  - It drops the validator from "who already paid it" and says the constructor now carries a
    conditional specification (fixpp#486).
  - It keeps `optional::emplace` as a live, generic move path, scoped: *"no production copy site
    uses it after fixpp#495"*.
    - Re-derive with `grep -rnE "optional<.*table_view|owned_tv_\.emplace" src include tests`. After
      the change, that shows only `tests/dictionary/table_view_test.cpp`,
      `tests/wire/dict_hooks_custom_pair_test.cpp` and `table_view.hpp`'s own note.
  - It replaces the snapshot's member move with its move into the table's `make_shared` block (D-4,
    same count). Borrowed arm 2 adds **no** move path, because it copies in place.

**Catalogue (`spec/feature-catalogue.md`).** Append a dated note to:
- **CA-009**, for the clone;
- the wire rows carrying the 066 / 458 amendment notes;
- **W-014**, for the validator;
- the 215 row, for D-4;
- the C-ABI dictionary row, for D-5.

Re-derive the rows with
`grep -n "066-dict\|#458\|090-capi\|215\|fixpp_dict_load" spec/feature-catalogue.md`. No new rows.

**Brain:**
- `brain/components/dictionary.md`:
  - *"The reify handle materialises EAGERLY"* gains a paragraph on the owned route and the table pin;
  - its ⚠️ residuals line drops `L-458-2`;
  - its entries for `.specify/215-dictionary-view.md` flag the alias design, including §5b's "third
    owner of the snapshot's control block", as **superseded in part** by this note. The 215 header
    pointer is not enough on its own.
- `brain/components/c-api.md`: the clone entry likewise, and the loader entry for D-5.
- Each component index lists this note.

**Code comments.** Per the parent `CLAUDE.md`, name the superseding note in header comments at:
- `impl::owned_tv_`;
- `fixpp_msg::owned_tv_`;
- `membership_copy()`'s "the ONE accessor" comment;
- `Session::inbound_tv_`, with the §2.6 invariant;
- `dictionary_snapshot.hpp`'s header and class comment.

**Stale alias prose to rewrite** (R-B). Re-derive with
`grep -rn -i "alias" include/fixpp/dict/dictionary_snapshot.hpp src/dictionary/dictionary_snapshot.cpp src/session/session.cpp include/fixpp/session/session.hpp src/capi/session.cpp src/capi/capi_internal.hpp tests/dictionary/dictionary_snapshot_test.cpp tests/session/test_session_table_view_reuse.cpp tests/capi/capi_group_delimiter_ctx_test.cpp`.
It returns hits in every one of those files at `3f200360`, which is the positive control. The
places it finds:
- `dictionary_snapshot.hpp`'s class comment ("NOT for pointer stability … the aliasing pointer")
  and the helper's comment ("The ONE production site that forms the aliasing view pointer");
- `dictionary_snapshot.cpp`'s helper comments, including the "Spelled out on purpose … G2 counts"
  comment and its `NOLINTNEXTLINE`, which lose their reason;
- `session.hpp`'s `inbound_tv_` comment;
- `session.cpp`'s `open()` comment ("the sole production alias-formation site");
- `src/capi/session.cpp`'s `fixpp_session_open` comment;
- `capi_internal.hpp`'s `session_tv_` and `owned_tv_` comments;
- the three tests' names and message texts (T-19).

⚠️ Per §2.4, none of the rewritten prose may spell the aliasing construction.

---

## 13. Verification plan

Builds are owner-approved before they run (`[const §XVII.7]`). Before building, check
`df -h /mnt/e`.

1. **`linux-clang-debug`, targeted.**
   - `ctest -R 'Reify|reify|MessageWrite|Clone|SharedMembership|validator|dict066|alloc_guard|DictionarySnapshot|SessionTableViewReuse|CapiGroupDelimiterCtx|CapiDictionary|version'`.
   - Then the full `ctest -L` sets for `dictionary`, `capi`, `session` and `wire`.
2. **Mallocnesia — point 1** (owner, 2026-09-23): after implementation, before `/simplify` and the
   verify record.
   - `ctest -L mallocnesia` on the Linux non-sanitizer preset. The interceptor is
     `build/<preset>/lib/libmallocnesia.so`.
   - Required: T-14's owned gate passes; its borrowed control is **caught** (EXPECT_VIOLATION); and
     `alloc_guard_positive_control_mallocnesia` is caught. Together these prove the interceptor is
     live.
3. **Sanitizers.** ASan, UBSan and TSan presets, targeted to T-2…T-20.
   - TSan is required for T-11 and T-17.
   - T-11's RED arm runs on ASan.
4. **Bench:** §11, on `linux-clang-release`, against the pre-edit baseline.
5. **Coverage.** `linux-clang-coverage`, targeted, over the changed functions in `reify.cpp`,
   `message_write.cpp`, `parser.hpp`, `offset_table.hpp`, `dictionary_snapshot.cpp` and
   `src/capi/dictionary.cpp`. Every new branch is covered, including `shared_membership()`'s
   reassigned-owner arm.
6. **MSVC local**, per the parent repo's `research/G19-fix-fpml-iso20022/msvc-local-build-procedure.md`.
   Derive the toolset from `CMakeCache.txt`'s `CMAKE_LINKER`.
   - T-1 must fail to compile on the **unfixed** tree, and compile on the fixed tree.
   - T-16's recalibrated cells run on `windows-msvc-release`.
7. **Gates:**
   - `bash tools/test_dictionary_snapshot_exclusivity_gate.sh`. It must show T-18's two seeded G2
     cases RED with `G2 FAIL`, and the clean tree green.
   - `bash tools/check_dictionary_snapshot_exclusivity.sh`: `G2 alias-formation sites = 0`, and the
     G1 lines unchanged.
   - `bash tools/check_capi_freeze.sh`: §7.2's fail-then-pass.
   - `.claude/scripts/check-comment-claims.py --root <tree> --base origin/main`.
   - `check_line_citations.py --shift-audit origin/main..HEAD`, because `.specify/` and spec files
     are edited.
8. **Mallocnesia — point 2** (owner, 2026-09-23): re-run step 2 after `/gate-b`'s fixer rounds,
   before merge.
9. **Index.** `codegraph sync` in this worktree after each code-changing phase.

---

## 14. Not in scope

- Sharing the validator's table (SC-007's by-value holding is frozen, §5).
- Unifying `fixpp_msg::session_tv_` with `owned_tv_`.
- `L-458-1` (probe-cap under-indexing).
- The `BM_Reify_Dispatch_20tag` placement effect (owner-approved in #494).
- The MSVC cost of `table_view` moves in `as_table_view()` / `build() &&` (#486's comment, point 2):
  recorded, not measured here.
- `owning_<Msg>::from_view` / `reify_as` (NFR-003-3's other clauses; untouched).
- Making `vg_parser` owned.
- A mallocnesia gate over `Session` dispatch (§10 T-15).
- A public owned route for C++ callers who parse their own views (R-A; §8's by-value alternative is
  the recorded route if it is ever wanted).

---

## 15. Open questions for Gate A

v0.1's Q-1 and Q-5 were answered by the owner as O-4 and O-5; O-5 is now superseded by R-B. Q-3
(public home of `shared_membership()`) is answered by R-A: not public. Q-4 was withdrawn.

- **Q-2.** Does MSVC evaluate §5's prvalue `noexcept` form as the standard requires? The MSVC leg
  decides. If it does not, the behavioural fallback in §5 stands in.
- **Q-4 (new, owner).** D-5's classification (§7.2): BREAKING is the reading taken. It bumps MINOR
  either way. Does another in-flight branch also bump `FIXPP_C_ABI_VERSION_MINOR`? If so, the two
  must be sequenced.
- **Q-5 (new).** G2's scope: keep it tree-wide (tests included), or narrow it to production to match
  R-C's wording (§6.4)?

---

## Normative References

Per `.specify/215-dictionary-view.md`'s precedent: `[const §VI.5]` binds the Normative References
requirement to `/specify` artifacts, and a `.specify/` design doc is not one. The section is included
**voluntarily**, as `.specify/447-458-452-capi-refusals.md` and `.specify/456-table-view-seal.md` do.

**Normative FIX references informing this design: NONE.** The three issues and the two ride-alongs
decide fixpp-owned policy: ownership of a dictionary table, the caps a copy re-parses under, an
exception specification, and a loader's memory resource. None of it changes wire behaviour, message
grammar, the session FSM or dictionary semantics. No `[DocAbbrev §X.Y.Z]` entry from
`spec/coverage-index.md` informs it.

**Process / constitutional references** (each opened in `.specify/constitution.md` for this revision):

| citation | what it says, as used here |
|---|---|
| `[const §XVII.1]` | *"Touches the public C++ API or C ABI"* and *"Touches the wire format, parser, or codegen layout"*: this gate's triggers |
| `[const §X.1]` | *"The C ABI in `include/fix/c_api.h` is a versioned contract … Codex Gate A is mandatory"*: D-5 |
| `[const §X.6]` | *"ABI-affecting features trigger all four mandatory controls (Appendix A)"*: §0.3's discharge plan |
| `[const §X.7]` | *"Before the first public release, a breaking C-ABI change is allowed but must be declared"*, with a MINOR bump: §7.2 |
| `[const §VIII.2]` | *"a slowdown greater than +5% … measured as a paired base-vs-candidate run on one runner"*: §11 |
| `[const §VIII.5]` | *"zero `new`/`delete` between parse and `fromApp` callback"*: why the token is a pointer (§2.1, §8) |
| `[const §VII.7]` | *"New parser-touching code without a fuzz harness is a Gate B blocker"*: §0.3's no-new-scanner argument |
| `[const §XV.1]` | *"Heap-allocate per message or per field on the hot path"*: D-4's allocation is config-time (§6.3) |
| `[const §VI.5]` | *"Every `/specify` artifact must include a Normative References section"*: why this section is voluntary |

**Inherited design contracts** (not FIX-normative): SC-007's by-value validator; 066 Decision 4 and
Decision 6; `.specify/215-dictionary-view.md`'s Option C (passkey, provenance check), whose alias
mechanism §6 supersedes; `.specify/456-table-view-seal.md`'s no-assignment rule;
`.specify/api-contract.md` §11's C-ABI effect list.

---

## Gate A

- Round 1 applied 2026-09-23: Codex P1=3 P2=4 P3=3; Opus post-judging P1=1 P2=5 P3=8; rewrite addresses root causes 1-4 + owner rulings R-A..R-D (Fable consult b13-reify-handle-pins-dictionary-resource). Reviews: `research/G19-fix-fpml-iso20022/research/reviews/codex_495_493_486_1_dict-reify-copy_review.md`, `research/G19-fix-fpml-iso20022/research/reviews/opus_495_493_486_1_dict-reify-copy_triage.md` (parent repo).
