# #215 item 1 — the shape of `SessionConfig::dictionary_view`

> **Status: v0.4, 2026-08-13.** Supersedes v0.3. **Gate A converged at round 3** — the round-3 Opus
> adversarial review returned `P1 = 0, P2 = 0, P3 = 5`, verdict **CONVERGE** (*"do not rewrite, and
> do not send this to a ground-up redesign"*). v0.4 applies the **five approved mechanical
> corrections** it prescribed and **changes no design**: Option C, the passkey, provenance rejected
> fail-closed at `open()`, the six-option fork, §4's cost table, §5a's API table and §5c's ledger
> deltas are all untouched, and no round-3 finding reached them. Everything v0.4 moves is in **six**
> places: §6 seam 1's observable, §6 seam 5's C1/C4 table, §6 seam 7's two census gates, §6's closing
> hand-off note, this header block (the status paragraphs and the v0.4 anchor note below), and one
> corrected anchor inside the round-2 convergence-log entry. Stated as six rather than as the three
> that carry the findings, because a census quoted only at its load-bearing subset is the defect
> class this document already records twice. The round-3 resolutions and the gate output measured for
> each are in the **Convergence log** at the end.
>
> **What v0.4 changes, in one line each.** (i) G1 now requires **exactly one** `friend` declaration
> and **exactly one** production key construction, with liveness asserted **per allowlisted file**
> rather than as a union total; (ii) G2 now requires **exactly one** aliasing-constructor expression
> **and censuses the two *required* helper calls**, closing the case where both production consumers
> copy the view instead of aliasing it; (iii) four new executed RED rows and an honest statement of
> what G2's regex does *not* cover; (iv) the C1 closure table is split from the C4 row wrongly filed
> inside it, and a duplicated dangling clause is deleted; (v) seam 1's observable is re-pointed from
> the `NotConnected` pre-Logon validate gate to the `LogonReceived`/`Active` gate its own frame
> actually traverses.
>
> **v0.3 addressed the round-2 reviews** — Codex (`P1 = 0, P2 = 5, P3 = 0`) and the Opus adversarial
> review that judged it (post-judging `P1 = 0, P2 = 5, P3 = 5` — verdict **CONVERGE**, *"converged
> apart from mechanical corrections; do not rewrite, and do not read 'RC#1 recurred' as
> structural"*). Every round-2 finding, its resolution, the **split verdict on RC#2**, and the **two
> places where v0.3's corrections weaken an argument the doc previously made in its own favour**, are
> also in the Convergence log. The spine is unchanged across all four revisions: Option C remains the
> recommendation, and no round-2 or round-3 finding touched it.
>
> **What was different in kind about v0.3.** Round 1's RC#1 *recurred inside its own fix*: v0.2 added
> the prescribed assertions and re-pointed the prescribed seam, but changed only **what each seam
> touches**, never **what it claims**. v0.3 therefore does not answer that by adding more assertions.
> For every assertion site it **names the mutation the site must go RED under and executes it**
> (§6 seam 5's mutation matrix), and for the two exclusivity claims no `static_assert` can express it
> specifies **grep-census gates proven non-zero on a deliberately violated tree** (§6 seam 7). One of
> those two gates was **broken on first formulation and read zero on the violated tree** — recorded in
> the log, because it is this document's own instance of the class the requirement exists to catch.
>
> **Scope.** This is a Gate A design review of **one public API decision** in open PR #262, not a
> module design. It settles the shape of the pre-built-table-view injection point introduced by
> issue #215 item 1. Items 2–5 of that PR are out of scope and covered by the accepted waiver.
> Trigger: `[const §XVII.1]` first bullet — *"Touches the public C++ API or C ABI"*
> (`.specify/constitution.md:335`).
>
> **Anchor freshness.** All line numbers below were **re-read** from the working tree at
> `215-simplify-followups` @ **`e0574ee7`** for v0.2, and **every citation in `## Normative
> References` plus every source anchor this revision touches was re-opened again for v0.3**. That
> sweep found **two** falsifications of v0.2's blanket *"each opened and verified"* claim, not the one
> the review caught — see the narrowed claim in `## Normative References` and the log. v0.1 declared `b9f52145`, which was already one
> commit stale when it was written; `e0574ee7` *"correct item 3's false depth≥17 equivalence claim;
> drop the O1(c) test (O1)"* changed `spec/behaviors-and-limitations.md` between the two — the
> depth-≥17 disposition under `B-215-1` expanded — so this is a rebind by re-verification, not a
> retyped hash. Three anchors moved against the values v0.1 and the reviews carried and are
> corrected here: `build_version_registry` is at `engine_config.hpp:211` (`:213` is its body),
> `Engine::app_version_registry_` at `engine.hpp:353-361`, and the `as_table_view_call_count()`
> seam block at `dictionary.cpp:420-428`.
>
> The Gate B triage (`research/reviews/opus_pr262_1_triage.md`) cites
> `table_view.hpp:584/:638/:685/:709` and `private:` at `:725`; those are **stale by exactly 7
> lines** — `b9f52145` added a net +7 to that file after the triage was written. Current values are
> given in §2 and are the ones to trust. Do not "correct" them back.
>
> **v0.4 anchor note — the *plausible twin* class has now bitten this document three times.** Round 2
> fixed `:1255`→`:1250`; v0.3's log fixed the round-2 review's `:260-262`→`:256-258`; round 3 found
> seam 1's observable pointing at `session.cpp:2100-2107`, a *different `switch` arm* of the same
> helper. In every case the cited line existed, compiled, and looked right. **v0.4's response is not
> another rule — it is that every line this revision cites was re-opened in the working tree rather
> than copied from the review that reported it**, and that anchors naming a `switch`/state-machine
> arm now cite the `case` label with the line. `emit_session_reject_` has five call sites
> (`session.cpp:2104`, `:2746`, `:3476`, `:3639`, `:3788`), re-derived here; seam 1 needs `:2746`.
> Do not "correct" it back to `:2104`.

---

## 1. What is actually being decided

**Already settled, not reopened here: the mechanism.** `Dictionary::as_table_view()`
(`include/fixpp/dict/dictionary.hpp:212`) has no cache — every call is a full walk of every
message, group and field, plus (since 083) the per-context delimiter store. Letting a caller who
already paid for that walk hand the result to `Session::open()` removes a duplicate walk. The
saving is measured, not argued (`bench/dictionary/table_view_footprint_bench.cpp`,
`linux-clang-release`, 10 repetitions, per `[const §VIII.3]`):

| Dictionary | build (walk) | copy | copy / build |
|---|---|---|---|
| FIX50SP2 | 212 779 µs (cv 3.25 %) | 17 070 µs (cv 2.74 %) | **8.0 %** |
| FIX44 | 2 925 µs (cv 2.59 %) | 679 µs (cv 6.29 %) | **23.2 %** |

One walk plus one copy beats two walks by 4–12×. **The optimization is not in question.** What
follows decides only how the pre-built view enters a session.

Today it enters as a public `SessionConfig` field:

```cpp
// include/fixpp/session/session_config.hpp:212
std::shared_ptr<const fixpp::dict::table_view> dictionary_view;  // null → open() builds one
```

The single in-tree producer is `src/capi/session.cpp:101-103`; `Session::open()`
(`src/session/session.cpp:1001-1004`) adopts it on a bare null check. `Session::inbound_tv_`
(`include/fixpp/session/session.hpp:825`) changed from `std::optional<table_view>` to
`std::shared_ptr<const table_view>` to receive it.

Two questions the field's shape leaves open — C1 (mutability) and C4 (provenance) — are §2.

### 1a. Two constraints disposed of up front, so they are not relitigated

**The C ABI does not need to break, and the granted latitude is declined.** The user explicitly
permitted an ABI break ("we don't have any clients"). It buys nothing here: nothing in this design
touches `include/fix/c_api.h` or `include/fix/c_api/`, `fixpp_session_open`'s signature is
unchanged, and `fixpp_session::tv_` (`src/capi/capi_internal.hpp:513`) is a member of an internal
struct, not a frozen surface. `tools/check_capi_freeze.sh` hashes only the `include/fix/`
headers — none are touched, so it keeps passing without a manifest edit. An ABI break would be
paid-for cost with no purchase. **Declined.**

**`load_any` needs no two-argument overload.** The defaulted trailing parameter
(`include/fixpp/dict/load_any.hpp:33-35`) is sufficient, and the header already records why: it
is *"a plain internal C++ facade"*, while the C ABI's `fixpp_dict_load_from_xml` — the surface
that *is* GA-frozen at `1.5.0` — deliberately carries no policy parameter. Every existing caller
compiles unchanged and keeps the `fail_closed` default. Binary compatibility was never engaged.
No action; question 4 is closed.

---

## 2. The two defects

Both are confirmed by reading the tree, and both are bounded to zero reachable in-tree exercisers
today. That bound is what makes this a design question rather than a bug.

### 2a. C1 — `shared_ptr<const table_view>` does not make the pointee immutable

`table_view` opens `public:` at `include/fixpp/dict/table_view.hpp:250` and does not close until
`private:` at `:732`. Inside that span sit **fifteen** non-`const` member functions — the entire
build-time population surface. Verified by enumerating every non-`const` member in the public span:

| mutator | line |
|---|---|
| `add_valid_tag` | `table_view.hpp:591` |
| `add_required_tag` | `:595` |
| `set_field_type` | `:600` |
| `add_group_member` | `:603` |
| `add_group_required_member` | `:616` |
| `add_valid` | `:628` |
| `add_required` | `:633` |
| `set_type` | `:638` |
| `set_group_first` | `:645` |
| `add_enum` | `:655` |
| `set_multi_value` | `:681` |
| `add_group_member_ctx` | `:692` |
| `add_group_required_member_ctx` | `:703` |
| `set_group_first_ctx` | `:716` |
| `add_fixt_framing_tag` | `:727` |

`std::shared_ptr<const T>` converts implicitly from `std::shared_ptr<T>`. A caller that builds a
view into a `shared_ptr<table_view>`, retains that alias, and assigns it to `dictionary_view` keeps
a mutation handle on the object the session then reads via the parser (`src/session/session.cpp:328`,
`Parser{*inbound_tv_}`) and the strict validator (`:1255-1258`).

**The sharpest form of the argument is the sibling field, one line up.**
`std::shared_ptr<const fixpp::dict::Dictionary> dictionary` (`session_config.hpp:188`) uses the
identical shape and is genuinely immutable: `Dictionary`'s entire public surface
(`include/fixpp/dict/dictionary.hpp:75-214`) is ctors/assignment plus `[[nodiscard]] … const`
accessors, with no non-`const` mutating member. So `shared_ptr<const Dictionary>` is a sound idiom
and `shared_ptr<const table_view>` is not. The new field silently breaks the convention its
neighbour sets — a reader who trusts the `const` in the type is wrong about exactly one of the two
adjacent fields, with nothing to distinguish them.

**Bound on today's exposure, by census.** `grep -rn dictionary_view src/ include/ bindings/ tools/`
returns 7 hits, and only **one** of them writes the field: the declaration
(`session_config.hpp:212`); three comment lines (`session.hpp:812`, `session.cpp:996`,
`capi/session.cpp:97`); the two lines of `open()`'s single read expression (`session.cpp:1001-1002`);
and one assignment (`capi/session.cpp:103`). That assignment is:

```
src/capi/session.cpp:102   tv = std::make_shared<const fixpp::dict::table_view>(sc.dictionary->as_table_view());
src/capi/session.cpp:103   sc.dictionary_view = tv;
```

`make_shared<const table_view>` allocates an **inherently-const** object — no mutable alias to it
can exist, ever. `src/config/` builds `SessionConfig` but never names this field; `bindings/` goes
through the C ABI and never names `SessionConfig` at all. **No shipped path can reach C1.** It is a
hazard on new public API, waiting for the first non-C-ABI caller.

**Thread-safety, checked rather than assumed.** `grep -nE '\bmutable\b'` over `table_view.hpp` and
`dictionary.hpp` returns nothing, and every member outside the 15 above is `const` and reads owned
containers with no lazy fill. So a *genuinely* const `table_view` is safe to share across sessions,
threads and strands without synchronisation. That property is what C1 puts at risk: it holds only
while no mutable alias exists.

### 2b. C4 — a mismatched view silently changes session grammar

`Session::open()` adopts the field on a bare null check with **no** consistency test against
`cfg_.dictionary`:

```cpp
// src/session/session.cpp:1001-1004
inbound_tv_ = cfg_.dictionary_view
                  ? cfg_.dictionary_view
                  : std::make_shared<const fixpp::dict::table_view>(
                        cfg_.dictionary->as_table_view());
```

The adopted object then drives inbound parsing (`:328`) and is copied into the strict validator
(`:1255-1258`). A caller can pair Dictionary A with a view derived from Dictionary B: the session's
identity, `SessionId::from_config(cfg_)`, says A, while field acceptance, required-field sets, enum
domains and group boundaries all follow B.

That is **silent protocol misvalidation** — valid traffic rejected, invalid traffic accepted — not
merely reduced diagnosability. It is recorded as `L-215-1`
(`spec/behaviors-and-limitations.md:2001`), documented and unenforced.

**No cheap fingerprint exists on the current types.** `table_view` carries no token back to its
source; `Dictionary`'s public surface exposes no id, version or hash usable as provenance
(`which_session_version()` at `dictionary.hpp:90` is a FIX version, not a document identity — two
different FIX 4.4 dictionaries share it). Enforcement therefore requires either new state on
`table_view` or an object that couples the two fields. §3 takes that as the design fork.

**Same bound as C1.** The only producer derives both fields from the same `shared_ptr`, two lines
apart. No shipped path can mismatch.

### 2c. What the bound means

Neither defect has a reachable in-tree exerciser, so neither is a live bug and neither is grounds
for a P1 hotfix. The reason to act now is **cost asymmetry, not severity**: `dictionary_view` is
new, unreleased, and has exactly one producer. Fixing its shape today is a contained change to one
call site. Fixing it after the first external caller adopts it is a source break on published API —
and the population of callers that will hit C1/C4 is precisely the population that does not exist
yet. This is the cheapest moment this decision will ever have.

---

## 3. Options

### Option A — keep the current shape; document both holes

Ship `shared_ptr<const table_view>` as-is. Keep `L-215-1`. Add a new limitation row recording the
C1 mutability hole, which is currently written down **nowhere**.

- **C1:** open, disclosed.
- **C4:** open, disclosed (status quo `L-215-1`).
- **Benefit:** the §1 measurement, unamended — A changes no construction path at all, so *"unchanged"*
  here is a measurement rather than an analysis. **Option B's benefit line is on the same footing**
  (it privatizes mutators and likewise changes no construction path); C, D, E and F all state their
  benefit by analysis. v0.2 claimed A was the *only* such row while stating the identical thing about
  B four paragraphs later — the exclusivity was simply false and is withdrawn.
- **C ABI:** untouched.
- **Callers:** none affected.
- **Cost:** one `behaviors-and-limitations.md` row. No code.

**Pros.** Zero risk, zero churn, and it is honest — the census bound is real, and shipping a
documented hazard with no reachable exerciser is a legitimate engineering call. `[const §XVII.1]`
does not require that a Gate A produce a change.

**Cons.** It ships two unenforceable invariants on one new public field, and the second one (C4)
fails *silently* into wrong-grammar validation. A future caller's first contact with this API is a
doc comment saying "you must, but we cannot check." The convention break against the adjacent
`dictionary` field is permanent and invisible at the use site. And the disclosure is the *minimum*
acceptable outcome, not a good one: it is what any other option also delivers, plus a fix.

### Option B — privatize `table_view`'s mutation surface

Move the 15 mutators to `private:` and `friend class Dictionary`, making `table_view` genuinely
immutable-after-construction, exactly like `Dictionary`. `shared_ptr<const table_view>` then
becomes the sound idiom it appears to be, and the field needs no change at all.

- **C1:** closed by construction, at the type. B is the only option that closes C1 for *every*
  holder of a `table_view` rather than only at the config injection point.
- **C4:** untouched — still open.
- **Benefit:** the §1 measurement, unamended — B changes no construction path either, so this row is
  a measurement on the same footing as A's.
- **C ABI:** untouched.
- **Callers:** breaks every builder-surface user.

**Pros.** Fixes C1 at the root, for every present and future holder of a `table_view`, not just for
this one config field. Restores the `Dictionary` convention.

**Cons — and this is decisive.** The builder surface is not vestigial; it is the test-fixture API.
The census, with the scanner stated so it can be re-run. ⚠️ **v0.2 stated a recipe that does not
produce v0.2's own number** — it is corrected here, and both forms are shown with their executed
output, because the discrepancy is the point (a stated recipe nobody runs is not enumerated evidence):

```
# NAMES = the 15 mutators, alternation ordered LONGEST-FIRST so add_valid / add_valid_tag
#         and set_group_first / set_group_first_ctx cannot swallow each other.

# (i) v0.2's stated recipe — SUPERSEDED, it does not yield 28:
grep -rlE "$NAMES" src/ tests/ bench/ tools/ bindings/ include/          -> 36 files
  then, per file, keep it if any hit survives dropping ^[[:space:]]*(//|#|\*)
                                                                        -> 31 files   (NOT 28)

# (ii) the correct scanner — member-call syntax, restricted to .cpp TUs:
grep -rlE "\.($NAMES)[[:space:]]*\(" --include='*.cpp' \
     src/ tests/ bench/ tools/ bindings/ include/                       -> 28 files = 1 + 27
```

**28 files** carry a real builder call: **1 production** (`src/dictionary/dictionary.cpp`, the
legitimate `friend`, 12 calls) and **27 test TUs** — `tests/wire/validator_*_test.cpp`,
`tests/capi/message_read*_test.cpp`, `tests/codegen/*`, `tests/fuzz/fuzz_wire_parser.cpp`, and more,
ranging from 1 call (`fuzz_wire_parser.cpp`, `tests/wire/validator_production_table_view_test.cpp`)
to 83 (`tests/capi/message_read_test.cpp`).

The eight files that (i)'s first step adds over (ii) are **mentions, not calls**, and `comm -23` of
the two sets returns exactly these eight — `table_view.hpp` itself,
`include/fixpp/wire/validator.hpp:510`, `src/capi/message_write.cpp:724-725`,
`tests/support/mock_dict_table.hpp:15-16`, `tests/wire/conformance/w014_validate.csv:21`, and three
test TUs. ⚠️ **Those three are not "comment-only", as v0.2 said — two of the three are string
literals**, which is why (i)'s comment filter cannot reach them and why it stops at 31:

| file | surviving hit | kind |
|---|---|---|
| `tests/dictionary/loader_disposition_test.cpp` | `:555` | comment — (i) *does* drop this one |
| `tests/dictionary/dict_enum_census_test.cpp` | `:449` | **string literal** — (i) keeps it |
| `tests/wire/delimiter_divergence_wire_test.cpp` | `:369` | **string literal** — (i) keeps it |

So (i) drops **5**, not 8, and its residue is the header plus those two string literals. The
conclusion and the eight-file enumeration are untouched by any of this; only the instrument was
wrong. The header says why the surface exists (`table_view.hpp:584-589`): the chain methods are there
so those TUs *"can drop the mock include and use this production type directly (RC-A closure,
T009)."* Privatizing them means either friending the test suite — which defeats the point — or
rewriting 27 TUs. That is a module-scale change, entirely outside a one-field Gate A, and it still
leaves C4 open.

### Option C — a coupled snapshot minted by the dictionary layer *(recommended)*

Replace the field with an opaque, immovable value object that carries the view **and** the
`shared_ptr` it was derived from, minted only by a factory in `fixpp::dict`:

```cpp
// include/fixpp/dict/dictionary_snapshot.hpp  (new)
namespace fixpp::dict {

class dictionary_snapshot;

// Declared BEFORE the passkey: a qualified friend declaration cannot introduce a
// name, so the factory must already be visible when snapshot_key befriends it.
[[nodiscard]] std::shared_ptr<const dictionary_snapshot> make_dictionary_snapshot(
    std::shared_ptr<const Dictionary> dict);

namespace detail {
// Passkey. NAMEABLE and COPYABLE from anywhere (it is a class in a named
// namespace in a public header); what is restricted is CONSTRUCTING one from
// nothing, which only make_dictionary_snapshot can do. That is what makes the
// constructor below public to the standard library (what make_shared needs)
// while leaving it callable by exactly one function (what closes C1).
// The friend list below IS the boundary — seam 5's A5 pins that it is closed,
// seam 7's G1 census pins that it stays a list of one.
class snapshot_key {
    snapshot_key() = default;
    friend std::shared_ptr<const dictionary_snapshot> fixpp::dict::make_dictionary_snapshot(
        std::shared_ptr<const Dictionary>);
};
}  // namespace detail

// A table_view PAIRED WITH the Dictionary it was built from. Copy AND move are
// deleted so the type is reachable ONLY through the shared_ptr the factory
// returns, and so a copy cannot silently duplicate an expensive table_view.
// NOT for pointer stability — a shared_ptr's pointee never relocates, so the
// aliasing pointer below is safe regardless.
class dictionary_snapshot {
public:
    // Public so std::make_shared can reach it; unreachable without a snapshot_key,
    // which only make_dictionary_snapshot can mint. `tv` is BY VALUE: that is the
    // parameter the cost analysis below counts.
    dictionary_snapshot(detail::snapshot_key, std::shared_ptr<const Dictionary> src,
                        table_view tv);

    dictionary_snapshot(dictionary_snapshot const&) = delete;
    dictionary_snapshot& operator=(dictionary_snapshot const&) = delete;
    dictionary_snapshot(dictionary_snapshot&&) = delete;
    dictionary_snapshot& operator=(dictionary_snapshot&&) = delete;

    [[nodiscard]] table_view const& view() const noexcept [[clang::lifetimebound]];
    [[nodiscard]] std::shared_ptr<const Dictionary> const& source() const noexcept
        [[clang::lifetimebound]];

private:
    std::shared_ptr<const Dictionary> source_;
    table_view view_;
};

// The ONE production site that forms the aliasing view pointer. Both Session::open()
// and fixpp_session_open MUST go through this — see §6 seam 4. Null snap → null return.
[[nodiscard]] std::shared_ptr<const table_view> shared_dictionary_view(
    std::shared_ptr<const dictionary_snapshot> snap) noexcept;

}  // namespace fixpp::dict
```

**The factory body, as code — not as a comment.** v0.2 left this a three-line sketch inside a
comment, and every quantity that depends on the *call* rather than on the *type* was wrong in it. It
is written out here because a body nobody can compile is not falsifiable by a reader:

```cpp
// src/dictionary/dictionary_snapshot.cpp
// Walks `dict` ONCE and pairs the result with it. Config-time only [const §XV.1].
std::shared_ptr<const dictionary_snapshot> make_dictionary_snapshot(
    std::shared_ptr<const Dictionary> dict) {
    if (!dict) { return nullptr; }                       // null dict → null return
    auto tv = dict->as_table_view();                     // SEQUENCED: walk first, ...
    return std::make_shared<const dictionary_snapshot>(  // ... then hand over ownership
        detail::snapshot_key{}, std::move(dict), std::move(tv));
}
```

**Why a passkey and not a private constructor + `friend`.** `std::make_shared` constructs the object
inside `std::_Sp_counted_ptr_inplace`, not inside the `friend`, and friendship does not propagate —
so a private constructor forces `std::shared_ptr<T>(new dictionary_snapshot(...))`, which is **two**
allocations (object + control block) instead of one. The passkey keeps the constructor public to the
standard library while leaving it callable by exactly one function, so the `make_shared` call above
is well-formed and single-allocation.

**Why the walk is hoisted to its own statement.** `dict` must not be `std::move`d *inline* into the
same call that dereferences it for `as_table_view()`. Since C++17 the arguments of a function call
are **indeterminately sequenced** — not "unsequenced", which is what v0.2 said and which has been
stale since C++11 — but indeterminate sequencing is already fatal here: one admissible order moves
`dict` before the other argument dereferences it. Hoisting the walk into `auto tv = …;` removes the
hazard **by sequencing** rather than by paying for a copy, which is what lets the factory then
`std::move(dict)` and hold the refcount cost to the single pair the caller already pays for passing
an lvalue (§4). v0.2 avoided the hazard by *not* moving, and thereby bought a second pair it did not
count. **This mechanism is load-bearing on the cost claim
in §4**; naming it is the difference between a specified design and an estimated one.

**What the passkey does and does not pin — corrected.** v0.2 said `snapshot_key` is *"not nameable
outside the factory, so the two-argument form the assertion names has no constructor at all."* Both
halves are wrong, and they are wrong independently.

- **Not nameable → false.** `detail::snapshot_key` is declared in a public header inside a *named*
  namespace. It is **nameable and copyable** from any foreign scope — `fixpp::dict::detail::snapshot_key`
  compiles there, and `std::is_copy_constructible_v` of it is **true**, because an implicitly-declared
  copy constructor is a *public* member regardless of the access region it is declared in. The
  accurate statement is: **nameable and copyable, but not constructible from nothing outside the
  friend list.**
- **The "so" → a non-sequitur.** The two-argument form
  `dictionary_snapshot(shared_ptr<const Dictionary>, table_view)` is unconstructible because **no
  two-argument constructor was ever declared** — exactly one constructor exists, and it takes three
  arguments. That fact is independent of the passkey, independent of nameability, and would hold
  under a design with no passkey at all. Correcting the first half does not repair the second: *"nameable
  but not constructible, so the two-argument form has no constructor"* is still a non-sequitur.

The consequence is the one that matters: **the two-argument assertion pins the absence of a value
constructor, which the passkey neither provides nor ever did — so it cannot pin the passkey
boundary.** The mutation that actually reopens C1 is *opening the key*, and all four of v0.2's
assertions stay green under it (measured — §6 seam 5). The boundary needs its own pin,
`static_assert(!std::is_default_constructible_v<detail::snapshot_key>)`, which §6 seam 5 adds. Keep
the two-argument assertion; it pins a real and different thing.

`SessionConfig` carries `std::shared_ptr<const dictionary_snapshot> dict_snapshot;` in place
of `dictionary_view`, and `open()` gains a two-line provenance gate:

```cpp
// src/session/session.cpp, replacing :1001-1004
if (cfg_.dict_snapshot) {
    if (cfg_.dict_snapshot->source() != cfg_.dictionary) {
        co_return std::unexpected(error::invalid_session_config);
    }
    inbound_tv_ = fixpp::dict::shared_dictionary_view(cfg_.dict_snapshot);
} else {
    inbound_tv_ = std::make_shared<const fixpp::dict::table_view>(
        cfg_.dictionary->as_table_view());
}
```

- **C1:** **closed at the injection point by construction.** The only way to seat a view on a
  `SessionConfig` is `make_dictionary_snapshot`; no mutable alias to the seated view can exist.
- **C4:** **rejected fail-closed at `open()` — a runtime check, not construction closure.** The
  mismatched pairing stays *representable*: `cfg.dictionary` and `cfg.dict_snapshot` remain two
  public authorities and nothing stops a caller assigning disagreeing values. What changes is that
  the disagreement is now *detected and refused* instead of silently driving the wrong grammar. The
  design that would make C4 unrepresentable is Option F below, and it is rejected on cost.
- **Benefit:** unchanged **to within one `noexcept` move and one refcount pair.** Still one walk,
  still one validator copy. §4 derives the delta from the factory body above — the moves and the
  allocation by **compiling** it, the refcount pair by compiling it *and* naming the caller's
  argument category. Not by benchmark.
- **C ABI:** untouched.
- **Callers:** one (`src/capi/session.cpp`), plus test fixtures.

**The aliasing constructor is what makes this contained**, and `shared_dictionary_view` is its one
production site. `std::shared_ptr`'s aliasing ctor produces a `shared_ptr<const table_view>` that
*owns the snapshot* but *points at the view inside it*:

```cpp
// src/dictionary/dictionary_snapshot.cpp — the ONLY place this form appears in production
std::shared_ptr<const table_view> shared_dictionary_view(
    std::shared_ptr<const dictionary_snapshot> snap) noexcept {
    if (!snap) { return nullptr; }
    table_view const* p = &snap->view();
    return std::shared_ptr<const table_view>(std::move(snap), p);  // aliasing ctor
}
```

So `inbound_tv_` keeps its current type and **`session.cpp:328` and `:1255-1258` are
byte-unchanged** — no read site moves, and `dictionary_driven_validator`'s by-value ctor
(`include/fixpp/wire/validator.hpp:112`, `explicit dictionary_driven_validator(table_view dict)`)
still copy-constructs from the const lvalue `*inbound_tv_` exactly as it does today. The change is
one new small header, one factory, one config field, and one `open()` block.

**Pros.**
- Provenance needs **no new fingerprint state**: the identity token is the `shared_ptr<const
  Dictionary>` itself, compared for pointer equality. O(1), no hashing, no version token on
  `table_view`, no coupling of `table_view` to `Dictionary`.
- The reject mirrors an existing fail-closed disposition — `invalid_session_config`, the same
  error `open()` already returns for a null dictionary at `src/session/session.cpp:979-981` and for
  the `SecurityProfile::kind::unset` sentinel at `:1012-1014` (`[const §XII.5]` / N-P2-3). No new error code, no new class of
  failure for a caller to learn.
- `dictionary` and `dict_snapshot` are still two fields a caller must keep in agreement — but the
  agreement is now **checked at `open()`** instead of left to discipline. That is strictly weaker
  than making disagreement unrepresentable (Option F), and strictly stronger than a doc comment
  (Option A).
- `table_view` is untouched, so all 27 builder-surface test TUs and the whole builder surface are
  unaffected.

**Cons.**
- **It does not make the invalid state unrepresentable.** Two public authorities survive; C4 is
  closed by a runtime compare, not by construction. Option F is the design that removes the second
  authority, and it is rejected below on migration cost, not on principle.
- New public type in `fixpp::dict` (~55 header lines + a small `.cpp`, up from v0.1's ~40 now that
  the passkey and `shared_dictionary_view` are specified) — a real, if small, addition to the API
  surface, and one more concept in the dictionary layer.
- Pointer-identity provenance rejects a *semantically* valid pairing: two independent loads of the
  same XML produce two `Dictionary` objects, and a snapshot of one will not be accepted against the
  other. That is deliberately strict and fail-closed, and the fix is trivial (share the
  `shared_ptr`), but it is a real behavioural edge worth stating.
- Delete-all-four on copy/move means `dictionary_snapshot` is usable *only* through the
  `shared_ptr` the factory returns — intended (it is what makes the factory the sole entry point),
  but a constraint. Note the deletes are **not** needed for pointer stability: a `shared_ptr`'s
  pointee never relocates, so the aliasing pointer is safe either way. They exist to keep the
  factory the only constructor and to stop a copy silently duplicating an expensive `table_view`.

**The C1 claim, stated precisely — do not overclaim it.** `Dictionary::as_table_view()` stays
public and stays returning a mutable `table_view` **by value**; it must, because
`dictionary_driven_validator` holds one by value under the frozen SC-007 design point. So Option C
does **not** make `table_view` immutable. The exact claim is: *the session-config injection point
no longer admits a mutable alias* — the only way to seat a view on a `SessionConfig` is through
`make_dictionary_snapshot`, which constructs the `table_view` in place inside an object that
exposes it as `table_view const&` and cannot be copied, moved or reconstructed. A caller can still
mutate a `table_view` they own; they can no longer hand a session one they can still mutate.

**And the "only way" in that sentence is itself an exclusivity claim, so it is gated rather than
asserted.** It holds only while `make_dictionary_snapshot` is the sole minter of a `snapshot_key`,
which is a property of a **friend list** — not of any type trait, and not something seam 5's A5 can
see (a second friend leaves A5 green). §6 seam 7's G1 census gate is what pins it, and G2 pins the
matching claim for `shared_dictionary_view`. This is the distinction v0.2 lost: an assertion can pin
what a type *is*, only a census can pin how many places *do* something.

### Option D — a provenance fingerprint on `table_view`

Add a source token to `table_view` (a `shared_ptr<const Dictionary>` member, or a `uint64` id
minted per `Dictionary`), and have `open()` compare it against `cfg_.dictionary`.

- **C1:** untouched — still open.
- **C4:** rejected fail-closed at `open()`, same as C — a runtime compare, on a token instead of a
  `shared_ptr`. Also not construction closure.
- **Benefit:** marginally reduced, by analysis: a `shared_ptr` member adds one atomic refcount pair
  to every `table_view` copy, including the per-session validator copy. Negligible, but non-zero —
  and unlike C's one-off construction cost, this one is per-copy.
- **C ABI:** untouched.
- **Callers:** none break; the token defaults null on hand-built fixtures.

**Pros.** Smallest diff of the enforcing options. Fixes provenance for *every* holder of a
`table_view`, not only the config path.

**Cons.** Closes only one of the two defects, and the weaker-argued one — C1 is the finding with
the convention-break argument behind it. It also puts a `Dictionary` reference inside a type that
**27 test TUs** populate standalone through the builder surface (the Option B census above),
muddying `table_view`'s "plain value type" character; those fixtures have no `Dictionary` to token
from, so they would carry a null token that the check must then treat as "unknown provenance,
allow" — an enforcement with a permanently open default arm. Strictly dominated by C.

### Option E — no public injection point; cache the view inside the engine

Delete `dictionary_view` outright. Have `Engine::register_session`
(`include/fixpp/session/engine.hpp:239`) keep a view cache keyed on the `Dictionary` and hand
`Session::open()` a shared view.

- **C1:** closed most completely — nothing is public at all.
- **C4:** genuinely closed by construction, and the only option besides F that is — the engine
  derives the view itself; a mismatch is unrepresentable.
- **Benefit:** *increased* — the saving generalizes to every caller, not only the C ABI.
  N sessions on one dictionary become one walk instead of N. Unmeasured, like every non-A row.
- **C ABI:** untouched at the boundary, but see below.
- **Callers:** `src/capi/session.cpp` must change shape.

**Pros.** Principled: the cleanest answer to Gate A question 1 is "it was never configuration."
Best projected outcome of the six. Nothing new on the public surface.

**Cons — priced against the in-tree precedent, which is narrower than v0.1 claimed.** v0.1 rejected
E on four cons; **three of them do not survive contact with `Engine`'s existing
`app_version_registry_`**, which is the same shape and already ships.
`build_version_registry(cfg)` (`include/fixpp/core/engine_config.hpp:211-214`) builds a registry
from `EngineConfig::dictionaries` (`:129`, a `vector<shared_ptr<const Dictionary>>`), and `Engine`
holds it as a member built **once at construction** (`include/fixpp/session/engine.hpp:353-361`):
*"engine-lifetime application version registry built once from `engine_cfg_.dictionaries` at
construction… Sessions hold a non-owning const\* handle; the registry outlives all Sessions because
`Engine::stop()` drains+joins Sessions before the Engine destructs."* An eager, immutable,
built-once map of `Dictionary → table_view`, keyed the same way, inherits all of that:

| v0.1's con | verdict against `app_version_registry_` |
|---|---|
| raw-`Dictionary*` key is a lifetime hazard | **refuted** — it keys the same `shared_ptr` vector |
| `shared_ptr` key ⇒ owns dictionaries indefinitely ⇒ needs eviction | **refuted** — lifetime is the engine's, exactly as the registry's is |
| shared mutable config-time state ⇒ needs synchronisation, re-triggers `[const §XVII.1]`'s concurrency bullet (`:336`) | **refuted, conditionally** — built before any session exists, read-only thereafter |
| the C ABI still needs a fetch path | **survives** |

v0.1 costed a **lazy mutable cache** and charged those costs to Option E generally. That was
estimation, not precedent.

**The two cons that survive, and they are still enough.**

1. **The C ABI still needs its own `tv_`** (`capi_internal.hpp:513`) for the outbound commit path
   (consumed at `src/capi/message_write.cpp:294`), so the engine must expose a fetch path — public-ish
   surface again, reintroducing a diluted form of the question this option claims to dissolve.
2. **An engine-side map does not cover the general case.** `EngineConfig::dictionaries` is a fixed
   vector, while `SessionConfig::dictionary` may legitimately be a dictionary **not in it**. E
   therefore needs a fallback arm regardless.

**The synchronisation refutation is conditional on that fallback, so the condition is stated
here rather than left implicit:** it holds only if the fallback arm is a **non-caching inline
build** — i.e. exactly today's `make_shared<const table_view>(cfg_.dictionary->as_table_view())` in
`open()`. If the fallback *caches*, and `register_session` is callable concurrently, eviction and
locking come back through that arm and three refuted cons collapse back to one. Any future Option-E
feature must pin the fallback as non-caching.

**Loses to C on cost, not on principle** — but by a materially narrower margin than v0.1 stated,
which makes §7's "revisit E if fan-out grows" a *stronger* standing recommendation, not a weaker one.

### Option F — the snapshot as the single dictionary authority *(rejected)*

Delete `SessionConfig::dictionary` and replace it with the snapshot itself:

```cpp
// include/fixpp/session/session_config.hpp, replacing :188 and :212
std::shared_ptr<const fixpp::dict::dictionary_snapshot> dictionary;  // required
```

Every reader of the raw `Dictionary` goes through `->source()`; there is no second field, so no
compare and nothing to keep in agreement.

- **C1:** closed at the injection point by construction, as in C.
- **C4:** **genuinely closed by construction — the mismatched state is unrepresentable.** This is
  F's merit and it is real: it is the strongest of the six on exactly the axis this design exists to
  address, and stronger than the recommended option. It is rejected on cost, and the cost is
  enumerated below rather than estimated.
- **Benefit:** unchanged for sessions that open; see (c) for the configs that do not.
- **C ABI:** signature-unchanged, but `fixpp_session_config_set_dictionary`'s semantics change — (b).
- **Callers:** **231 assignment sites across 127 files.**

**Cons.**

**(a) Migration, enumerated.** `grep -rnE "\.dictionary\s*=[^=]"` — the `[^=]` matters, since the
naive pattern also matches `engine.cpp:214`'s `== nullptr`, and a count identity is not proof a
scanner does not over-match:

- `src/ include/ bindings/ tools/`: **1** site — `src/capi/config.cpp:208`.
- `tests/ bench/`: **230** sites across **126** files.

Every one becomes `make_dictionary_snapshot(dict)` — an eager full walk at config-construction time
in 126 test/bench TUs. Against Option C's **one** production site plus **one** test file
(`tests/session/test_session_table_view_reuse.cpp`), that is two orders of magnitude.

The other two interaction axes came back **clean, and that is a checked result rather than an
unchecked one**: `src/config/` and `bindings/` are unaffected — `resolve_engine_dictionary`
(`src/config/selector_resolver.cpp:279-370`) populates the *engine-scope*
`bundle.engine.dictionaries` vector (`include/fixpp/config/config_bundle.hpp:55`), never
`SessionConfig::dictionary`; and `bindings/` reaches sessions through the C ABI, never naming
`SessionConfig`.

**(b) The C-ABI setter becomes a throwing 213 ms walk, on the wrong side of the thunk contract.**
`fixpp_session_config_set_dictionary` (`src/capi/config.cpp:191-210`, declared at
`include/fix/c_api/session.h:128`) is today a validated pointer copy: tag gate, null checks, one
`shared_ptr` assignment. Under F it must mint the snapshot — allocating, walking, throwing. The
function contains **no `try`**. Two things follow, and they point in opposite directions, so both
are stated:

- *A barrier exists to copy.* `src/capi/config.cpp` carries five `catch (...)` barriers — `:40`,
  `:87`, `:103`, `:119`, `:266`. So adding one is the pattern, not an invention.
- *But the thunk contract says otherwise.* `[2i §5.2]` (`.specify/2i-capi.md:1240`) splits C-ABI
  thunks into construction-time and steady-state flavours, and the construction-time whitelist is
  **exactly three symbols** — `fixpp_engine_create`, `fixpp_dict_load_from_xml`,
  `fixpp_msg_create_outbound` (`:1248`). `fixpp_session_config_set_dictionary` is not among them, and
  on the steady-state side *"an escaping exception… implies an `assert` failure at the C++ layer or
  a memory-corruption… bug"* and the thunk **`std::abort()`s** (`:1249`). Moving a genuinely
  throwing operation into a steady-state setter is therefore not a local edit: it either amends that
  whitelist — a change to the C-ABI error model, which is its own Gate A — or it accepts an
  abort-on-bad-dictionary that the current design deliberately routes to
  `FIXPP_ERR_CAPI_CONFIG_INVALID`.

⚠️ **v0.2 costed this with a census that classified barriers by their SYNTACTIC MARKER — `catch (...)`
is present — instead of by WHAT THE HANDLER DOES, and it reached the opposite conclusion to the one
the evidence supports. Both errors are corrected here, and the correction runs against this
document's own earlier framing.** Restated semantically, over the whole file:

| non-whitelisted thunk | barrier | what the handler does |
|---|---|---|
| `fixpp_engine_config_create` `:32` | `:40` | **translates** → `FIXPP_ERR_CAPI_CONFIG_INVALID` |
| `fixpp_session_config_create` `:70` | `:87` | **translates** → `FIXPP_ERR_CAPI_CONFIG_INVALID` |
| `fixpp_session_config_set_comp_ids` `:92` | `:103` | **translates** → `FIXPP_ERR_CAPI_CONFIG_INVALID` |
| `fixpp_session_config_set_begin_string` `:109` | `:119` | **translates** → `FIXPP_ERR_CAPI_CONFIG_INVALID` |
| `fixpp_session_config_set_tcp_endpoint` `:248` | `:266` | **fatal-logs and `std::abort()`s** at `:271` |

**Four translate, one aborts.** v0.2 cited the TCP setter as counter-evidence that *"in-tree practice
is broader than `[2i §5.2]`'s whitelist"*. It is the single strongest in-tree **confirmation** of that
whitelist: its handler aborts under an in-source comment (`src/capi/config.cpp:256-258`) that names
the rule it is obeying —

```
// Steady-state thunk (spec.md FR-011): an escaping exception is an invariant
// violation → fatal-log + abort, never translated.  Mirrors
// fixpp_session_acceptor_bound_endpoint in session.cpp.
```

**And the census was file-scoped for an ABI-wide rule.** `grep -rn "std::abort()" src/capi/` returns
**five** sites, not one — `engine.cpp:93`, `engine.cpp:126`, `session.cpp:179`, `session.cpp:317`,
`config.cpp:271`. Restricting the count to `config.cpp` is what made the abort discipline look like a
one-off exception rather than the shipped norm.

**The ABI-wide view inverts v0.2's conclusion, and that is worse for Option F, not better.** The
tree's practice on *steady-state* thunks **follows** `[2i §5.2]`: the one thunk whose author
actually reasoned about an escaping exception chose **abort**, and said so in a comment naming the
rule. Of the four that translate, two are `fixpp_*_config_create` — **constructor mirrors**, which is
exactly the shape the construction-time whitelist contemplates even though it does not name them, so
they are weak evidence for a *steady-state* claim in either direction. That leaves the two string
setters as a genuine residual inconsistency with `[2i §5.2]` — real, and something F would have to
reckon with too, but two sites against five aborts is not a basis for calling the whitelist the
outlier. (Stated this way deliberately: v0.2's error was an inference from a syntactic marker, and
*"those setters cannot throw anyway"* would be another one — `cfg->cfg.sender_comp_id = sender;` is a
`std::string` assignment and can throw `bad_alloc`, which is presumably why the barrier is there.)
So F does **not** merely force an existing tension to be resolved; it must **choose**:
either amend `[2i §5.2]`'s whitelist to admit a throwing dictionary setter — a C-ABI error-model
change with its own Gate A — or accept `std::abort()` on a bad dictionary, which is materially worse
for the caller than today's `FIXPP_ERR_CAPI_CONFIG_INVALID`. v0.2's *"genuine tension, not a clean
rule F violates"* framing was a counter-weight in F's favour that the evidence does not support, and
it is withdrawn.

Either way the *semantic* change is not repairable by a barrier: a setter that was O(1) becomes a
full walk, and re-setting the dictionary walks again.

**(c) It charges the walk to configs that are never opened — but the premise is narrower than it
looks, and is reported here at its true width.** `session.cpp:1001` builds `inbound_tv_`
**unconditionally**: the nearest `validate_inbound_messages` guard is 249 lines later, at
`:1250` (`if (cfg_.validate_inbound_messages) {`, whose body builds the validator at
`:1255-1258`). So for every session that *opens*, F does not *add* a walk — it *moves* it earlier, and for
N sessions sharing one dictionary it strictly *wins*, 1 walk instead of N. The new cost is
specifically **configs constructed and never opened**, which — given 126 test/bench files and a
C-ABI setter that may be called more than once per config — is still large, but it is not a
general regression.

**Verdict.** F is construction-strongest and migration-worst. Option C buys most of F's benefit for
1/230th of its churn, and F's residual advantage over C is confined to a state that C detects and
refuses at the only point where it could do harm.

---

## 4. Recommendation

**Option C** — accurately, **the lowest-migration checked-pair design**, not the construction-strongest
one. Replace `SessionConfig::dictionary_view` with an immovable
`std::shared_ptr<const fixpp::dict::dictionary_snapshot>` minted by
`fixpp::dict::make_dictionary_snapshot`, and reject a snapshot whose `source()` is not
`cfg_.dictionary` with `error::invalid_session_config`. C1 is closed at the injection point by
construction; **C4 is rejected fail-closed at `open()`** — Options E and F are the two that close C4
by construction, and both are rejected on cost.

**What discriminates it from the runners-up.**

*Against A (the closest call).* Not severity — the census in §2 says neither defect is reachable
today, and A's disclosure is honest. The discriminator is **cost asymmetry**, per §2c: the field is
new and unreleased with exactly one producer, so C costs ~150 lines and one call-site edit *now*
and costs a source break on published API *later*. The population that A's documentation is written
for is the same population that does not exist yet — which is precisely why closing it now is
cheap and closing it later is not. A remains the correct choice if the change is judged too risky
for an in-flight PR; that judgement is §7's, not this section's.

*Against B.* B fixes the better-argued defect at the root but leaves C4 open and costs 27 test TUs
(§3, Option B). C fixes both and costs one test file, because it never touches `table_view`.

*Against D.* D closes one defect of two, adds a permanently-open "unknown provenance" arm for the
27 test TUs that populate a `table_view` standalone, and puts a `Dictionary` reference inside a
value type that exists to be a plain table. C addresses both with no change to `table_view` at all.

*Against E — narrowed, and this is the discriminator that moved most between v0.1 and v0.2.* E is
the better *idea*: it dissolves the question rather than answering it, it generalizes the saving,
and it is one of the two options that genuinely closes C4 by construction. **Three of v0.1's four
arguments against it are withdrawn** — `app_version_registry_` (`engine.hpp:353-361`) proves an
eager, immutable, engine-lifetime map needs no raw-pointer key, no eviction policy and no
synchronisation. Exactly two discriminators survive:

1. the C ABI still needs its own `tv_` (`capi_internal.hpp:513`) for the outbound commit path, so a
   fetch path is required regardless — public-ish surface reintroduced; and
2. `EngineConfig::dictionaries` (`engine_config.hpp:129`) is a fixed vector while
   `SessionConfig::dictionary` may legitimately name a dictionary outside it, so E needs a fallback
   arm and does not cover the general case.

That is still a feature rather than a field-shape decision, and it is enough — but the margin is
narrow, and §7 records E as a live follow-up rather than a closed door. C does not foreclose it: if
E is ever built, `make_dictionary_snapshot` is exactly the primitive the engine map would mint into,
and the config field becomes an override rather than the only path.

*Against F.* F is the only option stronger than C on the axis this design exists to address — it
makes the mismatch unrepresentable rather than merely refused. It loses on enumerated migration
(231 assignment sites across 127 files, against C's one production site and one test file), on
turning an O(1) C-ABI setter into a throwing 213 ms walk on the steady-state side of `[2i §5.2]`'s
thunk contract, and on charging that walk to configs that are built and never opened. C buys most
of F's benefit for 1/230th of its churn.

**What it costs.** One new header + `.cpp` (~55 + ~25 lines) in `fixpp::dict` — the snapshot, the
passkey, `make_dictionary_snapshot`, and `shared_dictionary_view`; one field renamed and retyped in
`SessionConfig`; one `open()` block replaced (net +5 lines); `src/capi/session.cpp` switched from
`make_shared<const table_view>` to `make_dictionary_snapshot` with `fixpp_session::tv_` seated
through `shared_dictionary_view`; test fixtures in `tests/session/test_session_table_view_reuse.cpp`
updated to mint snapshots, plus the second dictionary seam 1 needs.

**The construction cost, derived from the mechanism §3 specifies rather than from an unnamed one.**
This is the claim v0.1 got right for the wrong reason: it asserted a delta without naming the
construction path, and the two paths admissible under its sketch differ in exactly the quantity
asserted.

| path | `table_view` moves | allocations |
|---|---:|---:|
| today — `make_shared<const table_view>(dict->as_table_view())` | 1 | 1 |
| `shared_ptr<T>(new dictionary_snapshot(...))` — private ctor, **rejected** | 1 | **2** |
| **passkey + `make_shared<const dictionary_snapshot>(...)` — specified in §3** | **2** | **1** |

Under the specified path: `as_table_view()`'s prvalue initializes `make_shared`'s forwarding
reference (no move — guaranteed materialization), that forwards into the constructor's **by-value**
`table_view` parameter (**move 1**), which is moved into `view_` (**move 2**).
`table_view`'s move ctor is `noexcept = default` (`include/fixpp/dict/table_view.hpp:260`), and the
snapshot object is one `shared_ptr` wider than a bare `table_view`.

**The move count is not an estimate and it did not change.** Compiling §3's sketch (`clang++ 22.1.2
-std=c++23 -O0 -Wall -Wextra`, zero warnings) gives `tv_moves == 1` on today's path and
`tv_moves == 2` on the passkey path — in **both** v0.2's body and the sequenced body §3 now
specifies, and with a single allocation either way. So the three-row table above stands as written,
and v0.1's *"one extra `noexcept` move"* remains the correct number. Choosing the private-constructor
route instead would trade that move for a second allocation, which is what the passkey exists to
avoid.

**The refcount pair is determined by the CALL, not by the body — and v0.2 counted it wrong.**
Stated precisely, for the one caller Option C creates: `src/capi/session.cpp:102-103` becomes
`make_dictionary_snapshot(sc.dictionary)`, and `sc.dictionary` is an **lvalue** there that must
survive the call — `open()` compares the snapshot's `source()` against it (§3), so the producer
cannot relinquish it:

| path | ctor-entry `use_count` of the dictionary | refcount pairs added vs today |
|---|---:|---:|
| today — `make_shared<const table_view>(dict->as_table_view())` | n/a (dictionary never copied) | **0** |
| v0.2's body — `…(key{}, dict, dict->as_table_view())` | **3** | **2** |
| **§3's sequenced body — `auto tv = …; …(key{}, std::move(dict), std::move(tv))`** | **2** | **1** |
| §3's body called with an **rvalue** — `make_dictionary_snapshot(std::move(d))` | **1** | **0** |

Measured, same probe. v0.2 asserted *"one refcount pair… and it is the only one"* of a body that
produced **two** (caller 1 → factory's by-value `dict` 2 → constructor's by-value `src` 3). The
sequenced body restores the claimed number: the factory's by-value parameter takes **one** atomic
increment from the caller's lvalue, and that single reference is then *moved* twice — no further
atomics — into `source_`, where it lives until the snapshot dies. v0.2's extra pair was **transient**
(the constructor's parameter copy, released at the end of construction), which is why the steady-state
`use_count` was 2 under both bodies and the error was invisible without sampling at ctor entry.

The last row is stated because it is the same lesson one level down: **the pair is a property of how
the factory is called, not of the design.** The C-ABI producer needs `sc.dictionary` after the call,
so it passes an lvalue and pays the one pair; a caller that can relinquish ownership pays none.
Quoting the figure without the call shape is what turned a correct claim into a wrong one in v0.2.

**So the cost is: two `noexcept` moves total — one more than today's path — one allocation, and one
refcount pair for an lvalue caller, against a 213 ms walk. Moves and allocations by compilation;
the pair by compilation and by call shape; the 213 ms by measurement (§1).** Every option row above
states its benefit on these terms except A's and B's, which are measurements because neither changes
any construction path.

**No additional benchmark is required under `[const §VIII.3]`** (`.specify/constitution.md:186`,
*"No perf change merged without a benchmark in the same PR"*). The perf change #262 merges already
ships with its benchmark in the same PR: `bench/dictionary/table_view_footprint_bench.cpp`, added at
`6ad84fef` (`git diff --stat main...HEAD -- bench/` → `+39`). §VIII.3 is satisfied on its own terms.
A second bench arm comparing `make_shared<const table_view>` against `make_dictionary_snapshot` is
**optional and would be welcome**; if added it belongs in that existing file, not a new one. Seam 3
pins the quantity that actually matters — the walk count — empirically rather than by argument.

**No** change to
`table_view`, to the C ABI, to `check_capi_freeze.sh`'s manifest, to the two `session.cpp` read
sites, or to the validator's frozen by-value storage.

---

## 5. Consequences

### 5a. API changes

| surface | before | after |
|---|---|---|
| `SessionConfig` field | `std::shared_ptr<const dict::table_view> dictionary_view` | `std::shared_ptr<const dict::dictionary_snapshot> dict_snapshot` |
| new type | — | `fixpp::dict::dictionary_snapshot` (opaque, non-copyable, non-movable, passkey ctor) |
| new type | — | `fixpp::dict::detail::snapshot_key` (passkey; **nameable and copyable** anywhere, but **not constructible from nothing outside the friend list** — pinned by seam 5's `!is_default_constructible_v`) |
| new function | — | `fixpp::dict::make_dictionary_snapshot(std::shared_ptr<const Dictionary>)` |
| new function | — | `fixpp::dict::shared_dictionary_view(std::shared_ptr<const dictionary_snapshot>)` — the sole production alias-formation site |
| `Session::inbound_tv_` | `std::shared_ptr<const dict::table_view>` | **unchanged** (seated via the aliasing ctor) |
| `Dictionary::as_table_view()` | public, returns by value | **unchanged** |
| `table_view` | 15 public mutators | **unchanged** |
| C ABI (`include/fix/`) | frozen `1.5.0` | **unchanged** |

The field is **renamed**, not just retyped. The type changes anyway, so a caller cannot silently
adapt; renaming makes the compiler name the new concept instead of reporting a type mismatch on a
familiar identifier. The field is `dict_snapshot`, deliberately **not** `dictionary_snapshot`: a
member whose name equals its type's name compiles here (different namespaces, and `SessionConfig`
is an aggregate) but breaks the moment any member function names the type unqualified. Use
`dict_snapshot` for the field and `dictionary_snapshot` for the type, consistently, from here.

**Bindings that continue to apply**, each pinned to its line — see also `## Normative References`.
`make_dictionary_snapshot` is config-time only: it allocates and walks, so it is barred from the
per-message path by `[const §XV.1]` (`.specify/constitution.md:295`), exactly as `as_table_view()`
already is. Nothing in this design sits between parse and `fromApp`, so `[const §VIII.5]` (`:191`,
zero `new`/`delete` on that window) and `[arch §5.3]` (`.specify/architecture.md:396`, *"Hot path is
exception-free. No `throw` between parse and `fromApp`"*) are not engaged — the provenance check is
a pointer compare in `open()`, which is neither an allocation nor a throw. No new pure-virtual
method, so `[const §XIV.2]`'s ≤5 budget (`:285`) is untouched. Unlike Option F, Option C adds no
work to any C-ABI thunk, so `[2i §5.2]`'s construction-time/steady-state split
(`.specify/2i-capi.md:1240-1251`) is not engaged either. Both view-returning accessors carry
`[[clang::lifetimebound]]`; `make_dictionary_snapshot` and `shared_dictionary_view` are
`[[nodiscard]]`.

### 5b. Ownership and lifetime, concretely

- **Who outlives whom.** The snapshot holds `shared_ptr<const Dictionary>`, so the dictionary
  cannot outlive-die under it. `SessionConfig` holds `shared_ptr<const dictionary_snapshot>`;
  `register_session` takes the config **by value** (`engine.hpp:239`), so the session's copy keeps
  the snapshot alive independently of the caller's. `Session::inbound_tv_`'s aliasing `shared_ptr` —
  formed by `shared_dictionary_view`, the sole production site — is a third owner of the snapshot's
  control block, so the view stays valid even if the config copy is destroyed first. Every edge is a
  strong reference; no raw back-pointers. Centralizing alias formation is what makes that last
  sentence checkable rather than asserted: there is exactly one function to get wrong, **seam 4 tests
  that it aliases rather than copies, and seam 7's G2 census gate is what pins that it is the only
  one** — seam 4 alone cannot, since it exercises the helper directly and a second hand-rolled
  aliasing site would leave it green.
- **`register_session` failure.** The by-value config copy is destroyed on the error return, the
  snapshot refcount drops by one, and the caller's `Dictionary` and snapshot are untouched — the
  C-ABI's "builder untouched on failure" contract (`src/capi/session.cpp:95-98`, enforced at
  `:113`) is preserved
  unchanged, and for the same reason it holds today: the snapshot is attached to a **local** copy,
  never to `cfg->cfg`.
- **Null handling, deliberately.** `make_dictionary_snapshot(nullptr)` returns null. A snapshot
  cannot carry a null `source_` any other way, because the factory is the only constructor. And
  `open()` already rejects a null `cfg_.dictionary` at `src/session/session.cpp:979-981`, *before*
  the block being replaced — so a null-carrying snapshot could never reach the identity compare
  anyway. Two independent guards, stated so the redundancy is on purpose rather than accidental.
- **Threads and strands.** The snapshot exposes only `table_view const&` and a `shared_ptr const&`;
  `table_view` has no `mutable` members and no lazy fill (§2a), so a `const dictionary_snapshot` is
  safe to share across sessions, threads and strands with no synchronisation. This is the property
  Option C converts from "true if nobody kept an alias" into "true by construction."

### 5c. `spec/behaviors-and-limitations.md` dispositions

The row deltas differ sharply by option, and that asymmetry is part of why C wins:

| row | under A | under C (recommended) |
|---|---|---|
| `L-215-1` (provenance unenforceable) `:2001` | **stays** verbatim | **removed** — the mismatch is now *detected and rejected*, so the row's claim ("undetectable") stops being true. Replaced by `B-215-2`, below |
| C1 mutability hole | **new limitation row required** — currently recorded nowhere | **not needed** — the injection point no longer admits a mutable alias |
| **`B-215-2`** (NEW behaviour row) | — | *"a snapshot whose `source()` is not the config's `dictionary` is rejected at `open()` with `invalid_session_config`. **The identity rule is `shared_ptr` pointer equality, not value equality:** two independent loads of the same XML produce two `Dictionary` objects, and a snapshot minted from one is refused against the other. The remedy is to share the `shared_ptr`. This is deliberate and fail-closed."* |
| `L-215-2` (validator copies by value) `:2003` | stays verbatim | **stays verbatim** — SC-007 is untouched, the validator still holds `table_view` by value, and the measured copy/build table stays as the evidence for it |
| `B-215-1` (C-ABI commit fails closed on context miss) `:1985` | unaffected | unaffected — different code path, item 2 not item 1 |

**`L-215-1` is not deleted without replacement, and the replacement is narrower than the row it
replaces — deliberately, and said out loud.** Option C's own cons concede that pointer-identity
provenance refuses a *semantically* valid pairing. That is a real behavioural consequence of the
recommended design, not an implementation detail, so it belongs in the ledger rather than only in
§3's cons: `B-215-2` must state the pointer-equality rule explicitly. Removing a limitation and
silently introducing a narrower one would be a worse outcome than keeping `L-215-1`.

Under A, this design must still add the C1 row: the brief is explicit that leaving the mutability
hole undocumented is not an acceptable outcome of this gate, and `L-215-1` records only the
provenance half.

### 5d. Migration

None externally — there are no clients. Internally: `src/capi/session.cpp:101-103` swaps
`make_shared<const table_view>` for `make_dictionary_snapshot`, and `fixpp_session::tv_`
(`capi_internal.hpp:513`) is seated from the same snapshot through **`shared_dictionary_view`** — the
same helper `Session::open()` uses, which is the point — so `src/capi/message_write.cpp:294`
(`h->session_tv_ = …->tv_`) is unchanged. The C ABI itself does not move, so
`tools/check_capi_freeze.sh` needs no manifest edit.

The migration census that makes this small is the one in §2a: `dictionary_view` has exactly **one**
in-tree writer (`src/capi/session.cpp:103`), and outside `src/ include/ bindings/ tools/` the field
is named only in `tests/session/test_session_table_view_reuse.cpp` and one `CMakeLists.txt` comment.
`grep -n dictionary_view` on that test TU returns **five** hits, of which **three are code** and
must migrate — `:157` (an `ASSERT_EQ(cfg.dictionary_view, nullptr)` precondition), `:188` and `:225`
(the two assignments) — and two are comments (`:179`, `:210`). Stated as five-of-which-three rather
than as three, because a census quoted only at its load-bearing subset is how the Option B recipe
above went wrong.

---

## 6. Test seams

These pin the decision. Seam 1 is the falsifiability requirement, and v0.2 states its RED honestly
in **two parts**, because the two are different kinds of evidence.

1. **Provenance rejection — and the discriminating frame it needs.**

   *What v0.1 got wrong.* It asserted only that the session **"reaches `Active`"**. Reaching
   `Active` proves the mismatched state is *representable*; it does not prove the wrong grammar
   *fires*. The proof that this shape cannot discriminate is already in the tree: the existing W3
   test `SessionTableViewReuse.AdoptedViewDrivesInboundParsingAndValidation`
   (`tests/session/test_session_table_view_reuse.cpp:222-237`) seats a **matching** view, feeds a
   group-free `35=A` logon (`make_logon_frame()`, `:65-89` — tags 35/34/49/52/56/98/108, no
   repeating group), and asserts `Active`. By this document's own seam 6, that frame cannot tell two
   grammars apart. An assertion on `Active` would stay green under a mismatched view.

   *The fixture.* Two dictionaries that are **separately loaded**, share a `session_version`, and
   **disagree on an observable rule**. In-tree this is a ~10-line addition to
   `tests/support/validation_test_dictionary.hpp`, which already holds the inline XML
   `kValidationTestFix42Xml` (`:49-119`) and loads it via
   `XmlLoader{}.load_from_string(kValidationTestFix42Xml, mr)` (`:131`) inside
   `make_validation_test_dictionary()` (`:125`, returning `shared_ptr<const Dictionary>`). Add a
   sibling string and factory differing in exactly one field: `OrderQty(38)` on `NewOrderSingle`,
   `required="N"` in A (`:75`) and `required="Y"` in B. Both declare
   `<fix major="4" minor="2">` (`:50`), so `which_session_version()` is **equal** — which kills
   the insufficient implementation that compares versions instead of identity, per Codex's point,
   and is why the pair must be authored rather than borrowed from `spec/dictionaries/`.
   ⚠️ This fixture **does not exist today** and is part of the cost of adopting Option C.

   *The discriminating frame, and how the outcome is observed — both taken from an existing test
   rather than invented.* `tests/session/test_validate_gate_inbound.cpp` is the template: its W3 cell
   `ValidateGateInbound.RequiredFieldMissing_Reason1` (`:325-344`) already drives this exact shape
   over this exact dictionary (`:59`, `:174` — the same
   `make_validation_test_dictionary()`). Reuse its three mechanisms verbatim:

   - **Sequencing.** `open_to_active(sess)` (`:184-200`) — open → `LogonSent` → feed a valid peer
     `35=A` → `ASSERT_EQ(sess.state(), fsm_state::Active)`. **A `35=D` cannot be fed before Logon
     completes**, so the discriminating frame is the *second* message, not the first. W3 in
     `test_session_table_view_reuse.cpp` never gets this far, which is a second reason it cannot
     discriminate.
   - **The frame.** A `35=D` NewOrderSingle carrying `11`/`54`/`60` but **omitting tag 38**
     (`make_raw_frame("FIX.4.2", "D", 2, "TW", "ISLD", body)`, `:336`). Dictionary A declares
     `OrderQty(38)` optional and accepts it; dictionary B declares it required and rejects it.
   - **The observable.** Not `sess.state()` — the session stays `Active` either way, which is the
     whole point. Inbound validation failure emits a session-level **`Reject (35=3)`** through
     `emit_session_reject_`. **Which of its five call sites matters, because the seam's own frame
     reaches exactly one of them.** The discriminating `35=D` is the *second* message (bullet above),
     so it enters `Session::on_inbound_frame` (`:2069`) and takes that function's `switch` on
     `fsm_state_` (`:2087`) at the **`case fsm_state::LogonReceived: case
     fsm_state::Active:`** arm (`src/session/session.cpp:2727-2728`), whose 041 T014 validate gate is
     at **`:2743-2749`** and emits at **`:2746`**. Not `:2100-2107`: that is the *same* 041 T014 gate
     on the **`case fsm_state::NotConnected:`** arm (`:2088`), which runs validate-first on the
     **first** inbound frame, before `interpret_logon` — a frame this seam has just ruled out. The
     two sites are the same helper with the same reason/`RefTagID` shape on different FSM phases,
     which is precisely why the wrong one reads as correct. The emission is captured by the fixture's
     `capture_outbound` (`:177`) and read by `has_reject_with_reason(1)` (`:213`) and
     `reject_ref_tag_id(1)` (`:237`). The assertion is `has_reject_with_reason(1) == false` under A
     and `true` with `reject_ref_tag_id(1) == 38` under B — an **acceptance outcome that flips**,
     which `Active` does not.

   *The two REDs, distinguished.*
   - **Runtime-RED, today, on the unfixed tree — this is the one that proves the instrument is not
     vacuous.** Written against the *legacy* `dictionary_view` field, which does exist:
     `cfg.dictionary = A`, `cfg.dictionary_view = <view of B>`, drive to `Active`, feed the frame.
     Today `session.cpp:1001-1004` adopts the mismatched view on a bare null check, so B's grammar
     fires against A's declared identity: `has_reject_with_reason(1)` is **true** where a
     dictionary-A-only session yields **false**. The instrument's own non-vacuity is checkable the
     same way — the A/A control must be green while the A/B pairing is red, so a broken fixture
     cannot read as a pass.
     That is an executable failing assertion on `e0574ee7`, not a missing symbol.
   - **Compile-RED, under Option C.** Once migrated to `cfg.dict_snapshot`, the same test cannot
     compile on today's tree at all — neither `dict_snapshot` nor `make_dictionary_snapshot` exists.
     v0.1's *"it fails on the current tree"* described **only** this second kind while presenting it
     as the first. A compile failure is not a witness; the runtime pair above is.

   ⚠️ **The runtime-RED has an expiry date, so its execution is SEQUENCED — this is an ordering
   constraint on implementation, not a remark.** The RED above is written against the **legacy
   `dictionary_view` field**, and Option C **deletes and renames** that field (§5a). It is therefore
   executable on `e0574ee7` and on **no tree after the migration lands**: every later spelling of the
   same test is compile-red only, which this seam has just ruled out as a witness. **The runtime-RED
   must be executed and its output — the A/B pairing red *and* the A/A control green — recorded in
   the feature's `/speckit-verify` record BEFORE `SessionConfig` is retyped.** Run it after, and the
   instrument is gone and the falsifiability claim in this section is unprovable for good.

2. **Provenance acceptance.** Same dictionary `shared_ptr` for both fields → `open()` succeeds, and
   the discriminating frame gets **A's** outcome. Guards against a gate so strict it rejects the
   only legitimate pairing (the C-ABI's own). Asserting the frame outcome rather than `Active` for
   the same reason as seam 1.
3. **Walk-count regression — the benefit must survive the redesign.** Extend the existing
   `SessionTableViewReuse.*` tests (`tests/session/test_session_table_view_reuse.cpp`) against 083
   T049's `as_table_view_call_count()` seam (`src/dictionary/dictionary.cpp:420-427`, bumped at
   `:449`): opening a
   session with a snapshot performs **zero** additional walks, and the C-ABI total stays **1**, not
   3. This is the seam that proves Option C did not give back the §1 measurement.
4. **Alias lifetime and alias IDENTITY — tested on `shared_dictionary_view`, the production helper,
   not on `std::shared_ptr`.**

   ⚠️ **v0.2's version of this seam was GREEN for a helper that copies instead of aliasing — measured,
   not argued.** v0.2 re-pointed the seam at the production helper but left its assertion set alone,
   so all it measured was *"a valid `shared_ptr<const table_view>` came back"*. Run the impostor
   `return std::make_shared<const table_view>(snap->view());` through v0.2's script exactly as
   written — call the helper, drop `snap`, read the alias, assert `use_count() == 1` — and it
   **passes**: `use_count == 1`, no UAF, seam green. That implementation reintroduces the full
   17 070 µs FIX50SP2 copy this design exists to avoid, and seam 3 does not catch it either, because
   seam 3 counts `as_table_view_call_count()` (`src/dictionary/dictionary.cpp:420-428`, bumped at
   `:449`) and a `table_view` **copy** does not bump a walk counter.

   The fix is to pin identity and shared ownership, not just validity. **The order is load-bearing:
   the first three assertions require `snap` to still be alive, the last two require it dropped.**

   ```cpp
   auto snap  = fixpp::dict::make_dictionary_snapshot(dict);
   auto alias = fixpp::dict::shared_dictionary_view(snap);

   // WHILE snap is alive — identity and shared control block:
   EXPECT_EQ(alias.get(), &snap->view());     // points INTO the snapshot, not at a copy
   EXPECT_FALSE(alias.owner_before(snap));    // same control block, both directions
   EXPECT_FALSE(snap.owner_before(alias));

   snap.reset();                              // AFTER: lifetime
   EXPECT_EQ(alias.use_count(), 1);
   (void)alias->/* any accessor */;           // UAF here under a non-owning impl (ASan)
   ```

   Measured on the compiled sketch: the real helper gives `same_addr = 1, shared_owner = 1,
   use_count = 1`; the copying impostor gives `same_addr = 0, shared_owner = 0, use_count = 1` — so
   the two new assertions discriminate and the pre-existing one does not. The cross-type
   `owner_before` between `shared_ptr<const table_view>` and `shared_ptr<const dictionary_snapshot>`
   is well-formed (`shared_ptr::owner_before` is a member template over `shared_ptr<U>`) and
   compiles clean under `-Wall -Wextra`.

   *What v0.1 got wrong.* It instructed the test to *"form the aliasing `shared_ptr` from it"* —
   i.e. **in the test**. That asserts that libstdc++'s aliasing constructor works, which was never
   in doubt. Both production sites — `Session::open()` and `fixpp_session_open` seating
   `fixpp_session::tv_` (`capi_internal.hpp:513`) — could have stored a non-owning pointer with the
   seam still green. It passed for the wrong reason.

   *Why the helper is required, not merely convenient.* Making `shared_dictionary_view` the
   **single** production alias-formation site is what makes this seam a real pin: with two
   hand-rolled sites the test can only ever cover one of them, and the design has no way to say
   which. §3 therefore specifies the helper as **mandatory at both sites**, and §5b's ownership
   argument rests on it.

   *Still deliberately not routed through a `Session`:* `register_session` takes `SessionConfig`
   **by value** (`include/fixpp/session/engine.hpp:239`) and the session retains `cfg_`, so a
   session-level "drop the caller's handle" test has a second live owner and passes identically
   under a raw pointer — a canary that can never go red. The helper is the seam that removes that
   problem instead of working around it.

5. **Boundary pins on the properties C1's closure actually rests on.** v0.1's
   `static_assert(!std::is_copy_constructible_v<dictionary_snapshot> &&
   !std::is_move_constructible_v<dictionary_snapshot>)` measures **type shape, not closure**: the
   shape survives a later public value constructor or a `table_view&` accessor, either of which
   reopens the injection hole while the assertion stays green. That is the recorded
   *topology-is-not-a-behavioural-claim* class. Keep it as **A1**, and add the four below. A5 is new
   in v0.3; A2–A4 are v0.2's, retained, and A2 is re-justified because v0.2's stated reason for it
   was wrong (§3):

   ```cpp
   // tests/dictionary/dictionary_snapshot_test.cpp — see seam 7's G1 allowlist.
   // The qualification is load-bearing: these live in a TU OUTSIDE fixpp::dict, which
   // is also what makes A5 meaningful (access checking in is_*_constructible is
   // performed as if in an unrelated context).
   using namespace fixpp::dict;

   // A2 — the value ctor must stay unreachable
   static_assert(!std::is_constructible_v<
       dictionary_snapshot, std::shared_ptr<const Dictionary>, table_view>);

   // A3 — view() must expose const&, never a mutable reference or a copy
   static_assert(std::same_as<
       decltype(std::declval<dictionary_snapshot const&>().view()), table_view const&>);

   // A4 — the factory must hand back a const snapshot, never a mutable one
   static_assert(std::same_as<
       decltype(make_dictionary_snapshot(std::declval<std::shared_ptr<const Dictionary>>())),
       std::shared_ptr<const dictionary_snapshot>>);

   // A5 — NEW in v0.3. The passkey boundary itself: nobody outside the friend list
   //      may mint a key. Access checking in is_*_constructible is performed as if
   //      in an unrelated context, so this goes red exactly when the key is opened.
   static_assert(!std::is_default_constructible_v<detail::snapshot_key>);
   ```

   ⚠️ **Why A5 exists, and why v0.2's four pins could not see the hole.** v0.2 justified keeping A2
   with *"`snapshot_key` is not nameable outside the factory, so the two-argument form has no
   constructor at all."* §3 shows both halves are wrong: the key **is** nameable and copyable from any
   scope, and the "so" does not follow — the two-argument form is unconstructible because **no
   two-argument constructor was ever declared**, which is true independently of the passkey and would
   be true with no passkey at all. A2 pins the absence of a value constructor. That is a real
   property, worth keeping, and it is **not** the passkey boundary. The design under these assertions
   changed between v0.1 and v0.2 from *private ctor + friend* to *public 3-arg ctor + passkey*; the
   assertions did not, so they pin a constructor shape the current design never had while the actual
   boundary — who can mint a `snapshot_key` — went unpinned.

   **The mutation matrix — each pin executed against the mutation it must go RED under.** This is
   what v0.3 does instead of adding further assertions: an assertion whose RED has never been
   observed is a claim, not an instrument. Compiled from §3's sketch, `clang++ 22.1.2 -std=c++23 -O0
   -Wall -Wextra`, one `-D` per row; **every row produced exactly one error, on its own pin**:

   | pin | mutation applied | result |
   |---|---|---|
   | A1 `!is_copy_constructible && !is_move_constructible` | un-delete the copy ctor | **RED** — `static assertion failed due to requirement '!std::is_copy_constructible_v<…dictionary_snapshot>'` |
   | A2 `!is_constructible_v<…, shared_ptr, table_view>` | add a public 2-arg value ctor | **RED** — `'!std::is_constructible_v<…dictionary_snapshot, std::shared_ptr<const …Dictionary>, …table_view>'` |
   | A3 `same_as<decltype(view()), table_view const&>` | `view()` returns `table_view&` | **RED** |
   | A4 factory returns `shared_ptr<const …>` | factory returns `shared_ptr<dictionary_snapshot>` | **RED** |
   | A5 `!is_default_constructible_v<snapshot_key>` | move the key's ctor to `public:` | **RED** |

   And the control that makes A5 necessary rather than merely nice: **compiled with the key opened
   and A5 removed, A1–A4 compile CLEAN.** The mutation that reopens C1 is invisible to every pin
   v0.2 shipped. (Two further measured facts about the key, both from the same probe and both
   contradicting v0.2's prose: `fixpp::dict::detail::snapshot_key` is nameable from a foreign
   namespace, and `std::is_copy_constructible_v` of it is **true** there — an implicitly-declared
   copy constructor is a public member regardless of the access region it appears in.)

   **Why these are sound and a "must not compile" probe is not** — and the v0.1 justification was
   wrong even where its instinct was right. v0.1 defended the trait pin as *"a **positive** property
   that must hold."* The trait is negative (`!is_copy_constructible_v`). The real distinction is
   **evaluated-assertion vs. absence-of-evidence**: a `static_assert` on a negative trait is
   *evaluated by the compiler and can fail*, which makes it a witness; a "must not compile" probe
   passes because nothing happened, which this project has recorded as a false-green generator. All
   **five** assertions above are evaluated — and, as of v0.3, all five have been **observed red**
   under their own mutation rather than merely asserted to be capable of it.

   ⚠️ **What C1's closure rests on, stated once so the two halves do not drift apart.** v0.2 said
   *"C1's closure rests on them plus seam 1."* That is now incomplete in a way that matters: the
   assertions pin what the **type** is, and half of C1's claim is about **how many places do
   something**, which no trait can see. Precisely:

   | half of C1's closure | pinned by |
   |---|---|
   | the seated view is exposed only as `table_view const&`, from a type that cannot be copied, moved, or value-constructed | **A1–A4** |
   | the passkey's friend list is **closed** — nobody outside it can mint a key from nothing | **A5** |
   | the friend list is a **list of one** — `make_dictionary_snapshot` is the sole minter | **seam 7 G1** (A5 stays green when a second friend is added; G1 counts the `friend` declarations and the production key constructions, so both go RED — cases F1/F2) |

   ⚠️ **C4 is a different claim and does not belong in that table.** v0.3 filed *"the mismatched
   pairing is actually refused at runtime | seam 1"* as a fourth row of *what C1's closure rests on*.
   It is C4's, and the distinction is one this document draws sharply elsewhere (§3, Option C:
   *"C1: closed at the injection point by construction; C4: rejected fail-closed at `open()`"*).
   Collapsing them is how "closed by construction" got applied to a runtime check in v0.1 — round
   1's RC#1 — so the two are kept apart here:

   | C4's closure | pinned by |
   |---|---|
   | the mismatched pairing stays **representable** — two public authorities survive — but is **detected and refused** at `open()` rather than silently driving the wrong grammar | **seam 1** (a runtime check, executed; not a construction closure) |
6. **Group-sensitive adoption (inherited gap).** The existing W3 test
   (`test_session_table_view_reuse.cpp:222-237`) feeds a group-free `35=A` and so cannot witness
   that the adopted view drives *group* boundaries — the Gate B triage's C5. Whichever option is
   chosen, that seam should be re-pointed at a frame with a repeating group so it discriminates.
   Listed here because Option C changes the adoption path it exercises; the fix is independent of
   the choice.
7. **Census gates on the two EXCLUSIVITY claims — the ones no `static_assert` can express.** Two
   singularity claims are load-bearing and nothing above pins either of them:

   - *"the only way to seat a view on a `SessionConfig` is `make_dictionary_snapshot`"* (§3 Option C,
     and the precise C1 claim at the end of §3). This rests entirely on the **friend list** inside
     `detail::snapshot_key` — one line away from admitting a second minter, and **A5 stays green
     when it does**, because a second friend does not make the key default-constructible in an
     unrelated context.
   - *"`shared_dictionary_view` … the sole production alias-formation site"*, declared mandatory at
     both sites (§3, §5a, §6 seam 4), on which §5b's *"third owner of the snapshot's control block"*
     depends. Seam 4 tests the helper **directly**; a hand-rolled aliasing constructor added at
     `fixpp_session_open` leaves seam 4 green and silently breaks §5b.

   Neither is a type property, so neither is expressible as a `static_assert` — which is exactly why
   they must not be left to prose. Both become **grep census gates in `/speckit-verify` Step 1**,
   alongside the other source gates. **Each is specified below with an allowlist, and each was
   proven non-zero on a deliberately violated tree before being written down.**

   ⚠️ **v0.4 — a singularity claim needs an OCCURRENCE count, not a file boundary, and it needs the
   REQUIRED sites as well as the forbidden ones.** v0.3's gates enforced a *file* boundary, which is
   what round 2 prescribed and what v0.3 built faithfully. Round 3 executed them and found three
   things the boundary cannot see, all measured `exit 0`: a **second `friend`** and a **second
   minter** added *inside* an allowlisted file; a **second aliasing-ctor site** added inside the
   helper's own TU; and — the sharpest — **both production consumers copying the view instead of
   aliasing it**, which leaves `shared_dictionary_view` uncalled dead code while G2, seam 3 and
   seam 4 all stay green, because a `table_view` *copy* does not bump `as_table_view_call_count()`
   and seam 4 exercises the helper directly. That last one re-adds precisely the 17 070 µs FIX50SP2
   copy §1 exists to eliminate, per session, undetected, and makes §5b's *"third owner of the
   snapshot's control block"* false in production while this document's own designated instrument
   for it reports clean. A fourth was found in G1's liveness half: `[ "$g1_all_n" -ge 3 ]` is a
   **union** total that the header alone satisfies, so the whole A1–A5 TU could be deleted with both
   gates green. The general repair, stated once: **a census supporting a singularity claim must
   count occurrences, must bound liveness per source, and must include the sites the design
   *requires*, not only the ones it forbids.**

   **Each gate now makes SEVERAL assertions, and exits non-zero on the first.** One is not enough: a
   scanner that prints nothing on success prints nothing just as happily when the type is renamed,
   when `src/` is not there, or when cwd is wrong. So every gate must also prove **it can still see
   the thing it guards** — the liveness half, and per allowlisted file rather than as a total.
   Note the `|| { …; exit 1; }` form rather than `<test> && fail`: under `set -e` the latter aborts
   on the *passing* branch, a trap this project has recorded. **And note that the gate prints its
   counts on the way through**: a gate whose success is silence cannot be distinguished from a gate
   that did not run.

   **Three scan scopes appear below, and each is a deliberate choice rather than an oversight** —
   v0.3's `tools/`-in-G1-but-not-G2 asymmetry was an oversight and is corrected:

   - **G1's name census — `src/ include/ bindings/ tools/ tests/`.** Naming `snapshot_key` anywhere
     outside the three allowlisted files is the reviewable event, in test code as much as in
     production.
   - **G1's key-construction count — `src/ include/ bindings/ tools/`, `tests/` excluded.** A test
     may legitimately *name* the key without *minting* one: A5 is
     `!std::is_default_constructible_v<snapshot_key>` and constructs nothing. Including `tests/`
     would make the count a hostage to how the assertions are spelled.
   - **G2's alias census — `src/ include/ bindings/ tools/ tests/`, matching G1's.** This is a *new*
     assertion, stated so it is not mistaken for a scope widened by accident: **no test may
     hand-roll an aliasing constructor either.** Seam 4's own point is that it tests
     `shared_dictionary_view`, the production helper, rather than `std::shared_ptr` — v0.1's version
     formed the alias *in the test* and asserted that libstdc++ works. A test that hand-rolls the
     form re-introduces exactly that, so `tests/` belongs in scope.

   ```bash
   #!/usr/bin/env bash
   # /speckit-verify Step 1 — 215 exclusivity census gates.  Run from the repo root.
   set -euo pipefail

   # EVERY grep is guarded with `|| true`, and each hit list is captured ONCE rather than
   # re-scanned.  Under `set -e` + `pipefail` an unguarded grep aborts the gate on its
   # PASSING branch — grep exits 1 when it matches nothing, which for the "bad" counts is
   # exactly success, and for a leading grep is exactly a clean tree.  See the note below:
   # this is not hypothetical, it killed the first draft of this script silently.

   HDR='include/fixpp/dict/dictionary_snapshot.hpp'
   FACTORY='src/dictionary/dictionary_snapshot.cpp'
   A5TU='tests/dictionary/dictionary_snapshot_test.cpp'

   # `grep -c .` counts NON-EMPTY lines, so an empty capture counts 0 rather than 1.
   n_of() { printf '%s' "${1-}" | { grep -c . || true; }; }
   # Exact first-field match on a `path:line:text` hit list.  awk, not `grep -F "$f:"`,
   # because the latter also matches the path quoted inside a comment, and awk needs no
   # regex escaping of the `/` and `.` in a path.  awk exits 0, so no `|| true`.
   in_file() { printf '%s\n' "${2-}" | awk -F: -v f="$1" '$1==f{n++} END{print n+0}'; }

   # ── G1 — SOLE MINTER ──────────────────────────────────────────────────────────
   # `snapshot_key` may be NAMED only where it is declared, defined, and asserted on.
   # Adding a file to the allowlist is the reviewable event; that is one of the gate's
   # points — but a file boundary alone is green for a second friend or a second minter
   # added INSIDE an allowlisted file, so assertions (b) and (c) count occurrences.
   G1_ALLOW='^(include/fixpp/dict/dictionary_snapshot\.hpp|src/dictionary/dictionary_snapshot\.cpp|tests/dictionary/dictionary_snapshot_test\.cpp):'
   g1_hits=$(grep -rn 'snapshot_key' src/ include/ bindings/ tools/ tests/ || true)
   g1_bad=$(printf '%s\n' "$g1_hits" | { grep -vE "$G1_ALLOW" || true; })
   g1_bad_n=$(n_of "$g1_bad")

   # LIVENESS — PER ALLOWLISTED FILE, never a union total.  A union bound of `>= 3` is met
   # by the header ALONE — it declares, comments on, and befriends around the key — so the
   # entire A1–A5 TU could be deleted with the gate green.  Measured, case M.
   for f in "$HDR" "$FACTORY" "$A5TU"; do
       n=$(in_file "$f" "$g1_hits")
       echo "G1 liveness: $f = $n"
       [ "$n" -ge 1 ] || { echo "G1 DEAD: no 'snapshot_key' in $f — file deleted, type renamed, or scanner broken"; exit 1; }
   done

   # ASSERTION (a) — nothing outside the allowlist may name the key.
   [ "$g1_bad_n" -eq 0 ] || { echo "G1 FAIL: $g1_bad_n site(s) outside the allowlist:"; echo "$g1_bad"; exit 1; }

   # ASSERTION (b) — the friend list is a LIST OF ONE, and its one entry is the factory.
   # Selector: `friend` at STATEMENT POSITION.  A bare `\<friend\>` also matches the
   # header's own prose ("a qualified friend declaration cannot introduce a name", "the
   # friend list below IS the boundary"), which would FAIL a conforming tree — measured,
   # 2 hits on §3's header exactly as written above.  ("befriends" is excluded by \<.)
   g1_friend=$(grep -nE '^[[:space:]]*friend\>' "$HDR" || true)
   g1_friend_n=$(n_of "$g1_friend")
   g1_friend_fact_n=$(n_of "$(printf '%s\n' "$g1_friend" | { grep -E '\<make_dictionary_snapshot\>' || true; })")
   echo "G1 friend decls in $HDR = $g1_friend_n (naming make_dictionary_snapshot: $g1_friend_fact_n)"
   [ "$g1_friend_n" -eq 1 ] || { echo "G1 FAIL: $g1_friend_n friend declaration(s) in $HDR, expected exactly 1:"; echo "$g1_friend"; exit 1; }
   [ "$g1_friend_fact_n" -eq 1 ] || { echo "G1 FAIL: the sole friend declaration does not name make_dictionary_snapshot:"; echo "$g1_friend"; exit 1; }

   # ASSERTION (c) — exactly ONE production construction of a key, and it is the factory's.
   # Scope excludes `tests/`: a test may NAME the key without minting one — A5 does exactly
   # that.  `snapshot_key() = default;` is a DECLARATION, not a construction, and is filtered.
   G1_MINT='snapshot_key[[:space:]]*(\{[[:space:]]*\}|\([[:space:]]*\))'
   g1_mint_raw=$(grep -rnE "$G1_MINT" src/ include/ bindings/ tools/ || true)
   g1_mint=$(printf '%s\n' "$g1_mint_raw" | { grep -vE '=[[:space:]]*default' || true; })
   g1_mint_n=$(n_of "$g1_mint")
   echo "G1 production key constructions = $g1_mint_n"
   [ "$g1_mint_n" -eq 1 ] || { echo "G1 FAIL: $g1_mint_n production snapshot_key construction(s), expected exactly 1:"; echo "$g1_mint"; exit 1; }
   case "$g1_mint" in
       "$FACTORY":*) ;;
       *) echo "G1 FAIL: the sole key construction is not in $FACTORY:"; echo "$g1_mint"; exit 1 ;;
   esac

   # ── G2 — SOLE ALIAS-FORMER, AND THE TWO REQUIRED CALLS ────────────────────────
   # A TWO-ARGUMENT shared_ptr<const table_view> construction IS the aliasing ctor.
   # Two spellings are matched: `(std::move(...)` and `(<identifier>,`.  The pattern's
   # coverage is the LISTED SPELLINGS ONLY — see the limitation stated below the table.
   # Scope now matches G1's; `tests/` is included because seam 4 tests the HELPER, so no
   # test should hand-roll an alias either.
   G2='shared_ptr<[[:space:]]*const[^>]*table_view[[:space:]]*>[[:space:]]*\([[:space:]]*(std::move\(|[A-Za-z_][A-Za-z0-9_]*[[:space:]]*,)'
   g2_hits=$(grep -rnE "$G2" src/ include/ bindings/ tools/ tests/ || true)
   g2_all_n=$(n_of "$g2_hits")
   g2_helper_n=$(in_file "$FACTORY" "$g2_hits")
   g2_bad_n=$(( g2_all_n - g2_helper_n ))
   echo "G2 alias-formation sites = $g2_all_n (in $FACTORY: $g2_helper_n, elsewhere: $g2_bad_n)"

   # LIVENESS — the helper's OWN aliasing ctor must match, or the pattern is dead and the
   # allowlist is filtering an empty set.
   [ "$g2_all_n" -ge 1 ] || { echo "G2 DEAD: pattern matches nothing, not even the helper"; exit 1; }
   # ASSERTION (a) — EXACTLY ONE aliasing-ctor expression in the whole tree.  An occurrence
   # count, not a file boundary: a second one inside the helper's own TU is a second
   # alias-formation site (case G) and is caught here, where v0.3's allowlist was green.
   [ "$g2_all_n" -eq 1 ] || { echo "G2 FAIL: $g2_all_n aliasing-ctor expression(s), expected exactly 1:"; echo "$g2_hits"; exit 1; }
   # ASSERTION (b) — and that one is the helper's.
   [ "$g2_bad_n" -eq 0 ] || { echo "G2 FAIL: $g2_bad_n hand-rolled alias site(s):"; echo "$g2_hits"; exit 1; }

   # ASSERTION (c) — the REQUIRED calls, the census G2 never had.  Without this both
   # production consumers can COPY instead of aliasing (case H) while (a), (b), seam 3 and
   # seam 4 all stay green.  `>= 1` per consumer, not `== 1`: the claim the design makes is
   # "each consumer routes through the helper", and singularity is already carried by (a).
   G2_CALL='\<shared_dictionary_view[[:space:]]*\('
   for f in src/session/session.cpp src/capi/session.cpp; do
       n=$(n_of "$(grep -nE "$G2_CALL" "$f" || true)")
       echo "G2 required call: $f = $n"
       [ "$n" -ge 1 ] || { echo "G2 FAIL: $f never calls shared_dictionary_view — the view is copied, not aliased"; exit 1; }
   done
   ```

   The liveness half of G2 is the observation v0.3 made while proving the gate — *the helper's own
   site matches, so the allowlist is load-bearing rather than a filter over an empty set* — promoted
   from a remark into an assertion. An unasserted observation decays; an asserted one does not.

   **Proof that these are instruments and not decoration — the script above was EXECUTED against
   eleven trees, not reasoned about.** A gate never observed red is a broken gate, and this project
   has been burned by exactly that. The trees carry the legitimate Option-C sites — §3's header and
   factory transcribed **verbatim, comments included**, which is what exposed the `friend` selector
   defect below — plus, per case, a deliberate violation or a deliberate instrument failure. **The
   script was re-extracted from this document's own fence and re-run for v0.4**, so the strings below
   are what the published script prints, not what an earlier draft printed. **`v0.3` is the same
   eleven trees under the v0.3 script**, and it is the column that says which rows are repairs:

   | # | tree | expected | observed (v0.4) | v0.3 |
   |---|---|---|---|---|
   | A | conforming — helper present, no violations | counts printed, `exit 0` | **8 lines of counts (`5 / 1 / 1`, `friend 1`, `mint 1`, `alias 1`, calls `1 / 1`), `exit 0`** | `exit 0`, silent |
   | B | a second minter in `src/capi/session.cpp` | G1 FAIL | **`G1 FAIL: 1 site(s) outside the allowlist`, `exit 1`** | same |
   | C | a hand-rolled alias in `src/capi/session.cpp` | G2 FAIL | **`G2 FAIL: 2 aliasing-ctor expression(s), expected exactly 1`** + both hits, `exit 1` | `G2 FAIL: 1 hand-rolled alias site(s)` |
   | D | `snapshot_key` renamed away (instrument dies) | G1 DEAD | **`G1 DEAD: no 'snapshot_key' in include/fixpp/dict/dictionary_snapshot.hpp — file deleted, type renamed, or scanner broken`, `exit 1`** | `G1 DEAD: 0 hits; type renamed or scanner broken` |
   | E | helper's own aliasing ctor removed (pattern dies) | G2 DEAD | **`G2 DEAD: pattern matches nothing, not even the helper`, `exit 1`** | same |
   | **F1** | **a second `friend` added to the passkey, *inside* the allowlisted header** | **G1 FAIL** | **`G1 FAIL: 2 friend declaration(s) in include/fixpp/dict/dictionary_snapshot.hpp, expected exactly 1`** + both lines, `exit 1` | **`exit 0` — silent** |
   | **F2** | **a second key construction *inside* the allowlisted factory TU** | **G1 FAIL** | **`G1 FAIL: 2 production snapshot_key construction(s), expected exactly 1`** + both hits, `exit 1` | **`exit 0` — silent** |
   | **G** | **a second aliasing-ctor site *inside* the helper's own TU** | **G2 FAIL** | **`G2 FAIL: 2 aliasing-ctor expression(s), expected exactly 1`** + both hits, `exit 1` | **`exit 0` — silent** |
   | **H** | **both production consumers COPY the view (`make_shared<const table_view>(snap->view())`) instead of aliasing it** | **G2 FAIL** | **`G2 required call: src/session/session.cpp = 0`** then **`G2 FAIL: src/session/session.cpp never calls shared_dictionary_view — the view is copied, not aliased`**, `exit 1` | **`exit 0` — silent** |
   | **M** | **the entire A1–A5 TU deleted** | **G1 DEAD** | **`G1 liveness: tests/dictionary/dictionary_snapshot_test.cpp = 0`** then **`G1 DEAD: no 'snapshot_key' in tests/dictionary/dictionary_snapshot_test.cpp — …`**, `exit 1` | **`exit 0` — silent** |
   | **P** | **four alternative spellings of the aliasing ctor in `src/capi/session.cpp` and `src/session/session.cpp`** | *(no expectation — this row records a LIMIT)* | **`exit 0` — silent.** All four evade the regex; see the limitation below | **`exit 0` — silent** |

   Rows B, C, F1, F2, G and H are the assertion halves; **rows D, E and M are the halves that make
   the gates survive maintenance.** Without them a renamed type, a reworded helper or a deleted
   assertion TU turns each gate permanently green, and nothing would say so. Row A matters too: a
   gate that fails on a conforming tree gets disabled within a week — and A is also the row that
   changed most, because **v0.4's gate is no longer silent on success.** It prints eight lines of
   counts. That is deliberate: the project's recorded false-green class is an instrument whose
   *failure* is indistinguishable from its *success*, and silence is exactly that shape.

   *One figure in row A is scratch-tree-specific and is flagged rather than published as a
   property of the design:* the `5 / 1 / 1` per-file liveness counts are what **this** transcription
   of §3's header produces; round 3's own scratch header produced `3 / 1 / 1`. Neither is the real
   tree — Option C does not exist yet — and the argument is unaffected either way, because the
   header alone clears a union bound of `>= 3` at 3 and at 5 alike. **What the gate asserts is
   `>= 1` per file, which is insensitive to both.**

   ⚠️ **The five rows in bold are v0.4's repair, and the `v0.3` column is the measurement that
   justifies it** — every one of them was `exit 0`, silent, under the gate v0.3 published. Case H is
   the sharpest: `shared_dictionary_view` becomes uncalled dead code, `grep -rn shared_dictionary_view
   src/ include/` returns only its declaration and its definition, and G2, seam 3 and seam 4 are all
   green. Case P is not a repair and is **not** marked as one; it is the boundary of the instrument,
   recorded below.

   ⚠️ **What G2's regex covers, stated so the allowlist is not read as an exhaustive census: the
   LISTED SPELLINGS ONLY.** Round 3 placed four legal alternative spellings of the aliasing
   constructor **outside** the allowlisted TU — where case C proves the gate does fire — and the gate
   returned `exit 0` for all four. **v0.4 re-ran them against the exact-count gate above and they
   still evade it (case P), because a hit that never matches never enters a count:**

   1. **east const** — `std::shared_ptr<table_view const>(std::move(s), p)`; the pattern requires
      `<[[:space:]]*const`.
   2. **brace-init** — `std::shared_ptr<const table_view>{std::move(s), p}`; the pattern requires `\(`.
   3. **deduced return** — `return {std::move(s), p};` from a function returning
      `std::shared_ptr<const table_view>`. The aliasing ctor is non-`explicit`, so copy-list-init of
      the return object is well-formed — and **the type name never appears at the call site at all.**
      This is the dangerous one: idiomatic, not contrived, and invisible to any regex over the call.
   4. **type alias** — `using tv_ptr = std::shared_ptr<const table_view>;` then `tv_ptr(std::move(s), p)`.

   **This is a known limitation of the instrument, not an unfixed defect, and the distinction is the
   point.** The gate is not extended to match spellings 1, 2 and 4: doing so would leave 3
   unmatchable by construction while creating the impression of coverage — a worse artifact than an
   honest boundary, and this document's own recorded class (*a selector is an assertion*). **A
   clang-query / AST structural check on the aliasing-constructor expression would be strictly
   stronger and is the right long-run instrument**; if `/speckit-verify` ever grows one, G2 should be
   replaced rather than augmented. Until then the exact-count grep is the **minimum** repair — it
   closes the counting and required-site holes measured in rows F1/F2/G/H, and it does not claim to
   close the spelling hole.

   ⚠️ **Three instrument failures were found by running this — two in v0.3, one in v0.4 — and all
   three are recorded because each is the exact class the requirement exists to catch.**

   1. **G2's first regex read ZERO on the violated tree.** The natural pattern
      `…table_view>[[:space:]]*\([^)]*,` cannot match
      `std::shared_ptr<const table_view>(std::move(snap), p)`, because `std::move(snap)` closes a
      parenthesis before the comma and `[^)]*` stops there. It would have shipped as a
      permanently-green gate over a real violation. Replaced with the alternation above, which
      matches both `(std::move(` and `(<identifier>,`.
   2. **The first script aborted SILENTLY on its own passing branch.** With `set -euo pipefail`, an
      unguarded `grep … | grep -v … | wc -l` fails the whole pipeline when *either* grep matches
      nothing — and for the "bad" counts, matching nothing **is** success. Case A therefore exited
      `1` with no output: a gate that reports failure on a clean tree, which is how gates get
      switched off. This is the project's recorded *`[ cond ] && fail` under `set -e` aborts on the
      passing branch* trap in a second shape — pipeline status rather than test status. Fixed by
      guarding **every** grep with `|| true` and capturing the hit list once.
   3. **v0.4 — the natural `friend`-count selector FAILS A CONFORMING TREE.** The obvious spelling
      of assertion (b) is `grep -cE '\<friend\>' "$HDR"`. Run against §3's header *as this document
      publishes it*, that returns **2**, not 1: the header's own prose says *"a qualified **friend**
      declaration cannot introduce a name"* and *"The **friend** list below IS the boundary"*.
      (`befriends` is correctly excluded by the word boundary.) Case A would have exited `1` with
      `G1 FAIL: 2 friend declaration(s)` — a gate red on a clean tree, the failure mode row A exists
      to catch, and the one that gets a gate switched off. **This was found only because the scratch
      tree was built from §3's sketch with its comments intact**; a stripped synthetic header passes
      and the gate ships broken. Fixed by anchoring the selector to **statement position**,
      `^[[:space:]]*friend\>`, which excludes `//`- and ` * `-prefixed lines by construction while
      still catching `friend class Foo;`. The gate asserts *friend declarations at statement
      position*, and that is what its message says.

   None of the three was found by reading. All three were found by running the gate against a tree
   built to make it speak — which is the entire argument for the requirement, applied to the gates
   written to satisfy it. **A number can be right by luck; an instrument that has never been shown
   red has not been tested at all** — and (3) adds the converse: *an instrument that has never been
   shown GREEN on a faithful conforming tree has not been tested either.*

   Note the coupling to seam 5: **G1's allowlist names
   `tests/dictionary/dictionary_snapshot_test.cpp` as the TU where A1–A5 live.** If the assertions
   are placed elsewhere the allowlist must move with them, or the gate silently admits that file —
   and, as of v0.4, the per-file liveness bound must move with them too, or the gate goes DEAD on a
   legitimate relocation. That is the intended trade: **relocating the assertions is now a gate edit,
   and deleting them is now a gate failure** (case M), where v0.3's union bound was green for the
   deletion.

Under Option A, seams 1, 2, 4, 5 and 7 do not exist and seam 3 is already green — which is the
mechanical statement of "A adds documentation, not enforcement." Seam 1's **runtime-RED half is the
exception**: it is writable against today's `dictionary_view` field and would fail on `e0574ee7`
under any option, which is precisely what qualifies it as a verification instrument rather than a
description of one — **and, per the ⚠️ note in seam 1, only until that field is deleted. Executing it
is the first implementation step under Option C, not a later one.**

⚠️ **Hand-off — seam 7's gates are a binding precondition on implementation, not optional polish.**
They land in **`/speckit-verify` Step 1**, which on this project is the **sole** enforcement point
for source gates that run nowhere in CI: no tier runs them, so a weak gate there has no second line
of defence and its false green is never contradicted. Round 3 graded the repairs above `P3` — *"does
not block the design decision"* — and that is **not** *"can be deferred past implementation"*: the
`/speckit-verify` record for this feature must show the v0.4 script, not v0.3's, and must show it
having been run.

Two implementation notes on the gates themselves. **The two consumer paths are hard-coded in G2's
required-call loop** (`src/session/session.cpp`, `src/capi/session.cpp`); if either file is renamed
or split during implementation, `grep` on a missing path yields 0 and the gate fails **closed but
with the wrong diagnosis** — it will say *"the view is copied, not aliased"* when the file merely
moved. Update the loop with the path, and treat a rename as a gate edit. **And `tests/dictionary/dictionary_snapshot_test.cpp` now appears in G1 twice** — in the allowlist regex
and in the per-file liveness loop — so relocating A1–A5 requires editing both, or the gate goes DEAD
on a legitimate move.

---

## 7. What this does not decide

- **Whether to land Option C in PR #262 or as a follow-up.** This is a design decision, not a
  scheduling one. #262 is open with all three CI tiers green; retyping a public field reopens the
  build and the Gate B loop. Landing C as its own small PR — with #262 shipping under Option A's
  disclosure in the interim — is a defensible sequencing, and so is folding it in. **User's call.**
- **Option E.** Deliberately not chosen and deliberately not foreclosed — and v0.2 upgrades it from
  "revisit if" to a **standing follow-up**, because three of the four arguments against it were
  withdrawn (§3, Option E). Engine-side view caching is the better long-run answer if per-dictionary
  session fan-out grows. It still needs its own feature with its own Gate A, on the two surviving
  discriminators — the C-ABI fetch path and the out-of-`EngineConfig` fallback — not on the
  concurrency bullet, which an eager built-once map modelled on `app_version_registry_` does not
  trigger. Any such feature must pin its fallback arm as **non-caching**; if the fallback caches and
  `register_session` is concurrent, the eviction and locking objections return through it.
- **Option F.** Rejected on migration, not on principle, and the distinction matters if the tree
  ever changes shape: F's 230-site test/bench cost is a property of *today's* fixtures, not of the
  design. If those call sites were ever routed through a shared factory, F would become cheap and
  would then be strictly better than C on the C4 axis.
- **`L-215-2` / SC-007.** Whether `dictionary_driven_validator` should hold its `table_view` by
  `shared_ptr` instead of by value. §1's measurement says the remaining copy is 8 % (FIX50SP2) to
  23 % (FIX44) of *one config-time walk* — a small absolute saving on a path
  `[const §XV.1]` already excludes from the per-message budget. Not worth reopening a frozen design
  point on this evidence. Explicitly out of scope; `L-215-2` stays.
- **Items 2–5 of PR #262.** Covered by the accepted waiver; the Gate B triage found four of five
  mechanically derived from already-approved contracts (T052, FR-006a) with no new design decision.
- **The `find_context_without_delim_record` clamp-direction defect** recorded under `B-215-1`
  (`spec/behaviors-and-limitations.md:1997`): pre-existing 083 T041 code, untouched by #215,
  blocks any depth-≥-17 fixture. Unrelated to this field's shape; still awaiting separate triage.
- **`table_view`'s public mutation surface** (Option B's target). Left open. If it is ever
  privatized, that is a module-scale change touching 27 test TUs and needs its own gate; Option C
  is chosen precisely so this decision does not depend on it.

---

## Normative References

`[const §VI.5]` (`.specify/constitution.md:164`) requires *"Every `/specify` artifact must include a
**Normative References** section listing the exact `[DocAbbrev §X.Y.Z] Title` entries from the
coverage index that inform the spec."*

**This section is here by repo convention, not because its absence was a constitutional violation.**
The distinction is recorded rather than glossed, because v0.1's omission was reviewed as a §VI.5
breach and that reading does not hold. §VI.5's binding subject is a `/specify` **artifact** —
`specs/<id>/` pipeline output — and `architecture.md:737` states the in-repo authoritative reading
directly: *"Strict reading of `[const §VI.5]` binds the Normative References requirement to
`/specify` artifacts; this spine document is not a `/specify` artifact in the Spec Kit sense. The
section is included here **voluntarily** for the same traceability spirit, and sets precedent that
design docs 2a–2m do the same."* A `.specify/` design doc is therefore not literally in
scope. What *is* binding is precedent: `ci254-python-fold.md:733` §11 carries the section, and
`:849` records the same finding being adjudicated **P2 → P3** and applied on exactly that basis.
This document follows ci254's shape. (The instruction that produced v0.1's omission cited
`ci241-coverage-ccache.md`, which has no such section — that instruction was **stale**, not wrong
about the constitution: precedent had moved at ci254.)

**Normative FIX references informing this design: NONE.**

Stated explicitly rather than omitted. §VI.5's object is *coverage-index* entries — FIX normative
sources. This document decides the C++ shape of one configuration field. It changes no wire
behaviour, no session FSM state or transition, no message grammar, no dictionary semantics, and no
C ABI surface; the grammar a session validates against is identical before and after, and the only
new runtime behaviour is refusing a configuration that FIX does not describe. **No
`[DocAbbrev §X.Y.Z]` entry from `spec/coverage-index.md` informs it**, and inventing one would be
worse than none. The model for this case is `2f-async-mutex.md:50` — *"2f's primary drivers are
engineering judgment… and no `[FIX-SL]` / `[FIXT]` / `[FIXS]` reference applies."*

**Process / constitutional references.** v0.2 headed this table *"each opened and verified to resolve
to the text used here"* — a blanket claim that **two of its own rows falsified**, so it is narrowed
rather than repeated:

> Every row below was re-opened at `e0574ee7` for **v0.3** and prints the quoted text at the line
> given. Where a row cites a **section heading** rather than the sentence quoted, that is stated in
> the row.

The two falsifications, recorded because a blanket claim is what a reader is asked to trust and this
one had not been earned:

1. `[arch §5.3]` cited `.specify/architecture.md:391` while quoting *"Hot path is exception-free…"*.
   `:391` is the heading `### 5.3 Error model`; the quoted sentence is at **`:396`**. Corrected in
   the row below and at its second occurrence in §5a. This is the recorded *stale anchor carried
   forward without re-resolution* class, and v0.2's own ⚠️ note two paragraphs down warns against
   exactly it.
2. The `architecture.md:737` quotation in the paragraph above ended *"included here
   **voluntarily**."* — a full stop where the source sentence continues *"voluntarily for the same
   traceability spirit, and sets precedent that design docs 2a–2m do the same."* Nothing in the
   argument turns on the tail, but a period is not an ellipsis; the quotation now carries one.

| citation | line | what it says, as used here |
|---|---|---|
| `[const §VI.5]` | `.specify/constitution.md:164` | this section's own requirement |
| `[const §VIII.3]` | `:186` | *"No perf change merged without a benchmark in the same PR"* — §4; satisfied by `6ad84fef` in this PR's own diff |
| `[const §VIII.5]` | `:191` | *"zero `new`/`delete` between parse and `fromApp`"* — §5a, why the config-time factory does not engage it |
| `[const §XII.5]` | `:253` | the `SecurityProfile` *"no implicit default"* rule whose `unset` sentinel `open()` rejects at `session.cpp:1012-1014` — the second of the two fail-closed dispositions §3 mirrors |
| `[const §XIV.2]` | `:285` | *"≤5 pure-virtual methods"* — untouched; this design adds no virtual |
| `[const §XV.1]` | `:295` | *"Heap-allocate per message or per field on the hot path"* — why `make_dictionary_snapshot` is config-time only |
| `[const §XVII.1]` | `:335` | *"Touches the public C++ API or C ABI"* — this gate's trigger |
| `[const §XVII.1]` | `:336` | *"Touches concurrency / threading / cancellation / executor model"* — the bullet Option E was wrongly said to re-trigger (§3, Option E) |
| `[const §XVII.1]` | `:340` | *"Any new design document under `.specify/` … qualifies by default"* — why this doc goes through Gate A |
| `[arch §5.3]` | `.specify/architecture.md:396` | *"Hot path is exception-free. No `throw` between parse and `fromApp`"* — §5a. (The **section** `### 5.3 Error model` opens at `:391`; v0.2 cited the heading and quoted the sentence) |
| `[2i §5.2]` | `.specify/2i-capi.md:1240` | the C-ABI thunk construction-time/steady-state split |
| `[2i §5.2]` | `:1248` | the construction-time whitelist — *exactly* `fixpp_engine_create`, `fixpp_dict_load_from_xml`, `fixpp_msg_create_outbound`; load-bearing on Option F(b) |
| `[2i §5.2]` | `:1249` | steady-state thunks `std::abort()` on an escaping exception |

⚠️ **Three anchors carried from v0.1 and from the reviews were off and are corrected in this
revision** — `build_version_registry` `:213`→`:211`, `app_version_registry_` `:349-360`→`:353-361`,
the call-count seam `:420-427`→`:420-428`. Re-verify before citing; a citation carried forward
without re-resolution is the *stale-anchor-re-pointed-to-a-plausible-twin* shape this project has
been burned by.

**Inherited design contracts** (not FIX-normative, listed so a reader can tell them apart): 083
T049's `as_table_view_call_count()` seam (`src/dictionary/dictionary.cpp:420-428`, bumped at `:449`),
which seam 3 measures against; 083 T052 / `FR-006a`, the group-context commit rule recorded under
`B-215-1` (`spec/behaviors-and-limitations.md:1985`) and out of scope here; and **SC-007**, the
frozen *"no virtual edge"* design point that keeps `dictionary_driven_validator`'s `table_view`
by value (`include/fixpp/wire/validator.hpp:112`) and is the reason `L-215-2` stays.

**Design-document references:** none. No sibling `.specify/` design doc specifies
`SessionConfig`'s dictionary fields; `2i-capi.md` is cited above for the C-ABI thunk contract only,
which Option C does not engage and Option F would.

---

## Convergence log

---

**▌ROUND 1 → v0.2**

Gate A round 1: Codex review (`P1 = 1, P2 = 4, P3 = 1`, verdict BLOCK) → Opus adversarial review
judging it (`P1 = 0, P2 = 5, P3 = 4`, verdict **CONVERGE** — *"v0.2 can ship after a single
convergence pass — do not rewrite"*). This section records what changed, what did not, and why.

**The recommendation did not change.** Option C remains recommended. No finding attacked the spine:
the two defects are real and correctly bounded, the five-option fork was honestly costed where it
was costed at all, the §2c cost-asymmetry discriminator is the right argument, the aliasing-ctor
containment claim holds down to `capi_internal.hpp:513`, and `invalid_session_config` is
provenance-consistent rather than plausibly chosen. **What v0.1 got wrong was language and rigor.**

### The three root causes

**RC#1 — "closed by construction" was applied to a runtime check, and the phrase then did the
argumentative work.** Once C4 was labelled construction-closed at four sites, three consequences
followed mechanically: the option that would *actually* be construction-closed never got written;
the seam meant to prove closure degenerated to a type-shape assertion, because if the type shape
*is* the closure there is nothing behavioural left to pin; and the lifetime seam migrated out of the
production path for the same reason. v0.1 already contained the correct formulation — §3's closing
paragraph states the C1 claim precisely and warns *"do not overclaim it."* v0.2 applies that
paragraph's discipline to C4 as well: **C1 is closed at the injection point by construction; C4 is
rejected fail-closed at `open()`.** *Collapses findings 2, 3 (seam 5), 4, 9.*

**RC#2 — the cost of the recommended design was argued from a construction path the document never
specified.** *"One extra `noexcept` move"* and every *"Measured benefit: unchanged"* row were claims
about a `make_dictionary_snapshot` whose body was left open — and the two admissible bodies differ
in exactly the quantity asserted. v0.2 specifies the passkey + `make_shared<const
dictionary_snapshot>` mechanism in §3 and restates the cost in §4 as *two `noexcept` moves total —
one more than today's path — plus one refcount pair, against a 213 ms walk, by analysis*.
**Corollary, recorded because it is counter-intuitive: adopting the passkey makes v0.1's number
CORRECT.** The fix was to specify the mechanism, not to retract the number. *Collapses finding 1.*

> ⚠️ **v0.3 split this verdict.** The **move** half above is confirmed by compilation and stands.
> The **refcount** half was wrong: v0.2's body produced **two** pairs, not one. See R2-RC#2 and
> finding R2-3 in the round-2 log.

**RC#3 — options were costed against imagined implementations rather than in-tree precedent.**
Option E was priced as a lazy mutable cache when `app_version_registry_` — the eager immutable one —
already ships; Option F was omitted rather than priced, and its true price was available for the
cost of one grep. v0.1 did this correctly for Option B and Option A and skipped it for exactly the
two options that then carried the ranking. **v0.2's rule: every option's cons cite an in-tree
precedent or an enumerated census.** *Collapses findings 5 and the migration half of 2.*

### Finding-by-finding

| # | source | sev | resolution | RC |
|---|---|---|---|---|
| 1 | Codex #1 | P2 (from P1) | §3 specifies the **passkey**; §4 replaces the bare assertion with a three-row moves/allocations table and restates the delta as *analysis, not measurement*. Every option row's benefit line restated; only A's is a measurement. §VIII.3 half **rejected** — see below | RC#2 |
| 2 | Codex #2 | P2 | Critique **conceded**: §3 C, §4 and §5c no longer say C4 is construction-closed; C is described as *"the lowest-migration checked-pair design."* Counter-proposal added as **Option F and rejected** on the enumerated census | RC#1, RC#3 |
| 3 | Codex #3 | P2 | Seam 5 keeps the shape trait and gains Codex's **three boundary `static_assert`s verbatim**, with a note that the passkey leaves the first one valid. Seam 1 rebuilt: discriminating frame, same-`session_version` pair, and **both REDs stated separately** — runtime-RED today via `dictionary_view`, compile-RED under C. Sequencing and observable pinned to `test_validate_gate_inbound.cpp` rather than asserted (below) | RC#1 |
| 4 | Codex #4 | P2 | **`shared_dictionary_view` adopted** as a mandatory single production helper at both sites; seam 4 tests the helper, not `std::shared_ptr`. §3, §5a, §5b, §5d updated to route through it | RC#1 |
| 5 | Opus N-P2-1 | P2 | Option E's cons rewritten against `build_version_registry` / `app_version_registry_`; three of four **withdrawn** in a verdict table, the non-caching-fallback condition stated. §4's *"Against E"* narrowed to the two survivors; §7 upgrades E to a standing follow-up | RC#3 |
| 6 | Codex #5 | P3 (from P2) | `## Normative References` added on the ci254 template, **explicitly as repo convention rather than as fixing a §VI.5 violation**, with the `architecture.md:737` reading quoted and the stale-instruction provenance recorded | — |
| 7 | Codex #6 | P3 | Header rebound `b9f52145` → **`e0574ee7`** by re-verification. Three anchors were genuinely off and are corrected; the ⚠️ note in Normative References records them | — |
| 8 | Opus N-P3-1 | P3 | §5c now carries an explicit **`B-215-2`** row stating the identity rule is **pointer, not value** equality, plus a paragraph saying the replacement is narrower than the row it replaces | RC#1 |
| 9 | Opus N-P3-2 | P3 | Seam 5's justification restated as **evaluated-assertion vs. absence-of-evidence**; the "positive property" framing dropped as wrong on its own terms | RC#1 |

### Where the adversarial review disagreed with Codex — no change follows

Recorded rather than silently absorbed, because in both cases v0.1 was right and a reader
comparing the Codex review against this document would otherwise read an unfixed finding.

**(a) Codex #1's `[const §VIII.3]` half — rejected outright.** Codex's BLOCK rested on the claim
that the no-benchmark position is *"a direct constitutional contradiction."* It is not.
`[const §VIII.3]` (`.specify/constitution.md:186`) requires a benchmark **in the same PR**, and
`bench/dictionary/table_view_footprint_bench.cpp` **is in PR #262's own diff** — added at `6ad84fef`
*"measure table_view copy-vs-walk cost, settle L-215-2 (C6)"*, `git diff --stat main...HEAD --
bench/` → `+39`. The perf change #262 merges ships with its benchmark. There is no constitutional
contradiction, so there was nothing to escalate on and the BLOCK does not survive. What survived is
the *second* half — the unspecified construction mechanism — which is finding 1 and is fixed. A
second bench arm remains **optional**, not mandated.

**(b) Codex #2's single-authority counter-proposal — added, and rejected.** Codex proposed
`std::shared_ptr<const dict::dictionary_snapshot> dictionary` as the sole field and was **right that
it is genuinely construction-closed** — §3 Option F says so explicitly, and says it is stronger than
the recommended option on exactly the axis this design addresses. It was proposed without pricing
either of its costs, and both are worse than assumed: **230 assignment sites across 126 files**
(`grep -rnE "\.dictionary\s*=[^=]"` over `tests/ bench/`, plus 1 production site), a C-ABI setter
that becomes a throwing 213 ms walk on the steady-state side of `[2i §5.2]`'s thunk contract, and
that walk newly charged to every config built and never opened. Rejected on cost, with the census as
the reason — not on principle, which §7 records.

### Figures re-derived for v0.2, and where they came out different

Per the review's RC#3 rule, every cited figure was re-derived at `e0574ee7` rather than carried
forward. Three moved:

| figure | v0.1 / review | v0.2 | why |
|---|---|---|---|
| Option B blast radius | 1 production + **29 test TUs** (30 files); the review spot-verified and confirmed 29 | 1 production + **27 test TUs** (28 files) | The confirming grep was a plain `grep -rln`, which counts **mentions**. ⚠️ **v0.3 correction:** the *explanation* given here was wrong — only **one** of the three named TUs is comment-only (`loader_disposition_test.cpp:555`); the other two are **string literals**, so the stated filter does not drop them and yields **31**, not 28. §3 now carries the correct member-call scanner with both forms' executed output. **The 1 + 27 answer and the 8-file enumeration were right all along**; only the recipe was wrong |
| Option D's "unknown provenance" arm | **41 hand-built fixtures** | **27 test TUs** | 41 was not re-derivable and the obvious scanner over-matches badly: `table_view tv = dict.as_table_view()` (`tests/session/test_exemplar_read.cpp:67`, `tests/consumer/consumer_witness.cpp:75`) and `= mv->membership_copy()` (`tests/wire/message_view_membership_copy_test.cpp:138`) are **not** hand-built and would carry a real token under D. The correct basis is the builder-surface census — views populated through the mutators, which have no `Dictionary` to token from |
| §2a `dictionary_view` census | 7 hits: *"the declaration, **two** doc comments, the two read sites, and one assignment"* | 7 hits: 1 declaration, **3** comment lines, 2 read lines, 1 assignment | Miscount of the breakdown only; the 7 total and the one-writer conclusion both hold |

**Two further corrections made during the rewrite, neither of which either review raised:**

- **The `validate_inbound_messages` guard is at `session.cpp:1250`**, not `:1255`. `:1255-1258` is
  the validator construction *inside* that guard's body — a plausible twin, and re-pointing an
  anchor to one is worse than leaving it stale. Option F(c) cites `:1250`.
- **Seam 1's observation mechanism was specified rather than assumed.** The first draft of this
  rewrite asserted an "acceptance outcome" without naming what reads it — the same defect class as
  the finding it was fixing. `sess.state()` stays `Active` on a rejected application message, so it
  is not the observable; the observable is a session-level `Reject (35=3)` emitted at
  `session.cpp:2100-2107` — **which is the wrong call site, and round 3 caught it; v0.4 re-points it
  to `:2743-2749` (emitting at `:2746`). See the round-3 section. Left uncorrected here because this
  paragraph records what v0.3 did** — and read through the fixture helpers in
  `tests/session/test_validate_gate_inbound.cpp` (`:213`, `:237`), whose W3 cell (`:325-344`) already
  does exactly this over the same dictionary. Seam 1 also now states that the discriminating `35=D`
  must follow a completed Logon — it cannot be the first frame.

Unchanged on re-derivation: **230 sites / 126 files** for Option F, **15** `table_view` mutators at
the exact lines §2a tabulates, the `public:` `:250` → `private:` `:732` span, `noexcept = default`
move ctor at `:260`, zero `mutable` in either header, **5** `catch (...)` barriers in
`src/capi/config.cpp`, and the §1 benchmark table.

### One correction to the adversarial review's own citation

The review grounded Option F's C-ABI objection on *"a C++ exception escaping the C ABI is a
`[const §X.2]` violation."* **`[const §X.2]` says no such thing** — it is *"No C++ symbol leakage…
CI verifies via `nm` (Linux) and `dumpbin` (Windows)"* (`.specify/constitution.md:221`), which is
about symbol visibility,
not exceptions. v0.2 does not propagate that citation. The correct grounding is `[2i §5.2]`
(`.specify/2i-capi.md:1240-1251`), and it makes the point **sharper**:
`fixpp_session_config_set_dictionary` is not on the three-symbol construction-time whitelist, so it
sits on the steady-state side where an escaping exception **aborts** rather than translating.

~~Against that, an honest counter-weight the review did not reach and Option F(b) now states: **the
tree's actual practice is broader than that whitelist.** Three of the five `catch (...)` barriers in
`src/capi/config.cpp` guard non-whitelisted setters — `..._set_comp_ids` (`:92`/`:103`),
`..._set_begin_string` (`:109`/`:119`), `..._set_tcp_endpoint` (`:248`/`:266`). So F does not
violate a clean rule; it forces an existing tension between `[2i §5.2]` and the shipped code to be
resolved.~~

⚠️ **WITHDRAWN in v0.3 — the counter-weight above was wrong, and it was wrong in F's favour.** The
count is literally true and the *inference* is not: the third of those three, `..._set_tcp_endpoint`,
**aborts** rather than translating, under a comment naming the very rule it obeys, and the census was
taken over one file for an ABI-wide rule (five `std::abort()` sites across `src/capi/`). Corrected
and inverted in §3 Option F(b); see the v0.2 → v0.3 log below, finding R2-4.

### Net effect

No structural change: §1, §2, §2c, §5b and §7's spine are as v0.1 wrote them, and the recommendation
is unmoved. Six sections gained rigor — §3 (passkey, Option B census, Option E rewrite, new Option
F), §4 (cost table, narrowed *Against E*, new *Against F*), §5a/§5c/§5d (helper, `B-215-2`,
pinned bindings), §6 (seam 1 rebuilt, seam 4 re-pointed at a production helper, seam 5 given three
boundary pins) — and two sections are new (**Normative References**, this log). The margin between C
and its runners-up is **narrower** than v0.1 claimed in two directions at once: E loses three of
four objections, and F is conceded to be construction-stronger. C still wins, now on cost that is
enumerated rather than estimated — which is the outcome the review asked for.

---

**▌ROUND 2 → v0.3**

Gate A round 2: Codex review (`P1 = 0, P2 = 5, P3 = 0`, verdict CONVERGE) → Opus adversarial review
judging it (post-judging `P1 = 0, P2 = 5, P3 = 5`, verdict **CONVERGE** — *"converged apart from
mechanical corrections; do not rewrite, and do not read 'RC#1 recurred' as structural"*). **Every
round-2 finding is a line edit, a factory body, a census restatement, or an assertion. None touched
the spine, and Option C remains recommended.** Where the two reviews differed, the adversarial
review governs.

### The three root causes (round 2)

**R2-RC#1 — round 1's RC#1 RECURRED INSIDE ITS OWN FIX: the v0.2 seams pin the ARTIFACT of each fix,
never the EXCLUSIVITY the argument rests on.** Round 1 diagnosed *"the seam pins the shape of the fix
rather than the behaviour it must guarantee."* v0.2 applied both prescribed remedies and reproduced
the class in both:

- **Seam 5** gained three boundary `static_assert`s verbatim — but the design underneath them changed
  from *private ctor + friend* to *public 3-arg ctor + passkey*. The assertions therefore pin a
  constructor shape the new design **never had**, while the real boundary — who can mint a
  `snapshot_key` — went unpinned. Measured: open the key and all four assertions stay green.
- **Seam 4** was re-pointed at the production helper — but its **assertion set was left alone**, so
  it still measured only *"a valid `shared_ptr<const table_view>` came back"*. Measured: a copying
  impostor passes it with `use_count = 1`.

The invariant behind both: **the fix changed what the seam touches; nothing changed what it claims.**

***What v0.3 does differently, and it is deliberately not "add more assertions."*** Adding assertions
is what produced the recurrence. v0.3 instead makes every assertion site carry **the mutation it must
go RED under, executed**:

- §6 seam 5 now ships a **five-row mutation matrix**, each row compiled. Every row produced exactly
  one error, on its own pin. The control row is the load-bearing one: **with the key opened and A5
  removed, A1–A4 compile clean** — proof that the four v0.2 pins are blind to the one mutation that
  reopens C1, rather than an argument that they might be.
- For the two claims no `static_assert` can express — sole minter, sole alias-former — §6 seam 7
  specifies **grep census gates, each proven non-zero on a deliberately violated tree**. An
  assertion pins what a type *is*; only a census pins *how many places do something*. That
  distinction is the general form of this RC.

**R2-RC#2 — the passkey was specified as a TYPE, so every quantity determined by the CALL went
unchecked.** Round 1's instruction was *"specify the construction mechanism."* v0.2 specified the
class, the passkey and the constructor signature, and got the *type*-determined quantity exactly
right. But the factory **body** appeared only as a three-line comment, and the *call*-determined
quantities in it were wrong: two `shared_ptr` copies where the doc counted one, plus a C++11-era
"unsequenced" claim. A mechanism written as commented pseudo-code is not falsifiable by anyone
reading it. **Fix:** §3 promotes the body to real code in the sequenced form; §4 restates the cost
from that body, with the caller's argument category named.

> ⚠️ **SPLIT VERDICT, recorded because half of this RC is a confirmation and half is a defect.**
> **The factory is wrong; round 1's RC#2 move-count corollary is RIGHT and survives intact.**
> Compiling both bodies gives `tv_moves == 2` under **either**, against `1` today — so v0.1's *"one
> extra `noexcept` move"*, and v0.2's corollary that *"adopting the passkey makes v0.1's number
> CORRECT"*, both stand exactly as written. It was a claim about **moves**, and moves are
> type-determined. Only the **refcount** sentence — a call-determined quantity — was wrong. v0.3
> does **not** retract the move count; §4's three-row moves/allocations table is unchanged.

**R2-RC#3 — the re-derivation pass verified NUMBERS but not the INSTRUMENTS and INFERENCES that
produce them.** v0.2's rule was *"every option's cons cite an in-tree precedent or an enumerated
census."* Every number re-derived for v0.2 came out right — and everything one level up from the
number went unchecked: the **scanner** that produces it (the stated recipe yields 31, the conclusion
needs 28); the **inference** drawn from it (a `catch (...)` census used as evidence about exception
*policy*, members classified by syntax rather than by what the handler does); the **scope** it was
taken over (one file, for an ABI-wide rule); the **blanket claim** made about it (*"each opened and
verified"*, falsified by its own row); **internal consistency** between two edits made under one
finding; and the **lifetime of the instrument** (a RED provable only *before* the migration it
justifies). **v0.3's rule: where a census supports an INFERENCE rather than a COUNT, classify members
by behaviour and take the census over the rule's own scope — and execute every stated scanner,
pasting its output.**

### Finding-by-finding (round 2)

| # | source | sev | resolution | RC |
|---|---|---|---|---|
| R2-1 | Codex #1 | P2 | §6 seam 5 adds **A5** `!std::is_default_constructible_v<detail::snapshot_key>` (compiles; goes red exactly when the key opens). Prose corrected at all three sites — §3, §5a, §6 — from *"not nameable"* to **"nameable and copyable, but not constructible from nothing outside the friend list."** The key is nameable as `fixpp::dict::detail::snapshot_key` from a foreign namespace **and** copy-constructible there (both measured) — v0.2 was wrong on two counts, not one | R2-RC#1 |
| R2-2 | Codex #2 | P2 | §6 seam 4 gains `EXPECT_EQ(alias.get(), &snap->view())` and the two `owner_before` assertions, **explicitly ordered before `snap` is dropped** (v0.2's script dropped first, which makes them unwritable). Measured discrimination pasted in: real helper `same_addr=1, shared_owner=1`; impostor `0, 0` | R2-RC#1 |
| R2-3 | Codex #3 | P2 | §3's factory promoted from comment to compilable code in the sequenced form `auto tv = …; make_shared<…>(key{}, std::move(dict), std::move(tv))`. §4's refcount claim corrected from *"one pair and it is the only one"* to a four-row measured table. *"Unsequenced"* → **"indeterminately sequenced"** (C++17), with the conclusion unchanged and the hazard now removed **by sequencing** rather than by paying for a copy. **Move count NOT retracted — see the split verdict above** | R2-RC#2 |
| R2-4 | Codex #4 + Opus escalation | P2 | §3 Option F(b)'s census restated **semantically** — four translate, one aborts — and **widened from one file to the five `std::abort()` sites across `src/capi/`**. The TCP setter's abort and its FR-011 comment (`config.cpp:256-258`) quoted. **v0.2's conclusion is INVERTED, against this document's own earlier interest** — see *"Corrections that weaken our own argument"* below. The stale figure in v0.2's own log is struck through in place | R2-RC#3 |
| R2-5 | **Opus N-P2-1** | P2 | The *"so"* at all three sites deleted. v0.3 states the **correct** reason the two-argument form is unconstructible — **no two-argument constructor was ever declared**, which is independent of the passkey and of nameability, and would hold with no passkey at all. Explicitly recorded that **Codex's remedy does not repair this**: *"nameable but not constructible, so the 2-arg form has no ctor"* is still a non-sequitur. A2 is **kept** and re-justified as pinning a real, different property | R2-RC#1 |
| R2-6 | Codex #5 | P2→P3 | §3 Option B's scanner **swapped** for the member-call `.cpp` form, with **both** forms' executed output shown. *"Comments"* → **"comments and string literals"**, with a three-row table naming which of the three residual TUs is which. The doc's filter drops **5**, not 8. Conclusion and 8-file enumeration untouched — see *"Corrections that weaken our own argument"* | R2-RC#3 |
| R2-7 | **Opus N-P3-1** | P3 | New **§6 seam 7**: G1 (sole minter of `snapshot_key`) and G2 (sole former of the alias), each with an allowlist, **each proven non-zero on a deliberately violated tree and silent on `e0574ee7`**. Cross-referenced from §3's precise C1 claim and §5b's ownership argument, both of which previously rested on unpinned prose | R2-RC#1 |
| R2-8 | **Opus N-P3-2** | P3 | Option A's *"only option row for which unchanged is a measurement"* **withdrawn** at both sites. Restated honestly: **A and B both** carry §1's measurement unamended; C, D, E and F state benefit by analysis. Option B's row updated to say so from its own side, so the two cannot drift apart again | R2-RC#3 |
| R2-9 | **Opus N-P3-3** | P3 | `[arch §5.3]` re-pointed **`:391` → `:396`** in the table and in §5a. The blanket claim *"each opened and verified"* **narrowed rather than re-asserted**, because the v0.3 sweep found a **second** falsification the review did not — see below | R2-RC#3 |
| R2-10 | **Opus N-P3-4** | P3 | §6 seam 1 gains an explicit **sequencing constraint**: the runtime-RED is written against the legacy `dictionary_view` field, which Option C **deletes**, so it must be executed and its output recorded in `/speckit-verify` **before** `SessionConfig` is retyped. Cross-referenced from the closing paragraph of §6 | R2-RC#3 |

### Corrections that weaken an argument this document previously made in its own favour

Recorded separately and prominently, because a convergence log that only records wins is not
evidence. Two of v0.3's corrections cost the document something.

**(a) The C-ABI census now argues AGAINST Option F's rejection being cheap — and against v0.2's own
framing.** v0.2 wrote that *"the in-tree practice is broader than `[2i §5.2]`'s whitelist… so this is
a genuine tension in the existing tree, not a clean rule F violates."* That sentence was a
counter-weight offered **in F's favour**, and it was built on a misclassified barrier: the TCP setter
it cited as evidence **aborts**, under a comment naming FR-011 and the steady-state rule it is
obeying. Widened ABI-wide, the picture inverts — five `abort()` sites, and the one thunk whose author
actually reasoned about an escaping exception chose abort. The honest conclusion is that steady-state
practice **follows** `[2i §5.2]`, so F must *choose* between amending a C-ABI error-model rule (its
own Gate A) and accepting `std::abort()` on a bad dictionary. That is **materially worse for F** than
v0.2's framing. It does not change the verdict — F was already rejected on migration cost — but the
document loses the "it's only a pre-existing tension" softener it had granted itself, and the
mechanism was the recorded *classify a disposition by provenance, not by a plausible syntactic
marker* class, one layer up.

**(b) Option B's cost evidence was produced by a scanner that does not produce it.** v0.2's whole
claim to rigor under RC#3 was *"the scanner stated so it can be re-run."* Run as stated, it returns
**31**, and the doc's conclusion needs **28** — so for one release the document's most-advertised
reproducibility feature was the one figure a reader could not reproduce. Worse, the explanation given
in v0.2's own log (*"three test TUs whose sole hits are comments"*) was wrong about **two of the
three**: they are string literals, which is precisely why a comment filter cannot reach them. The
28-file answer, the 1 + 27 split and the 8-file enumeration were all correct throughout — but they
were correct **despite** the published instrument, not because of it, and that is the finding.

**And a third, smaller, in the same spirit:** the v0.3 anchor sweep found that v0.2's blanket *"each
opened and verified"* was falsified by **two** of its own rows, not the one the review caught — the
`[arch §5.3]` line offset, and a quotation of `architecture.md:737` that ended in a full stop where
the source sentence continues. The claim is therefore **narrowed** rather than re-pointed and
re-asserted; re-asserting a blanket that has now failed twice would be the same error a third time.

### One correction to the round-2 adversarial review's own citation

Recorded for the same reason v0.2 recorded one: a reader checking this document against the review
would otherwise flag the correct line as the error.

The review quotes the FR-011 steady-state comment as `src/capi/config.cpp:260-262`. It is at
**`:256-258`**. `:260-262` is the *"Mirror the L-050-5 seam (capi_loopback_support.hpp:67-68)"*
comment inside the same `try` block — a four-line offset onto a **different comment**, which is the
recorded *plausible-twin* shape rather than a simple stale line. The quoted text and every word of
the finding built on it are correct; only the anchor moved. §3 Option F(b) cites `:256-258`.

### Where v0.3 records a disagreement rather than a silent drop

**Codex's remedy for the passkey prose is adopted but is NOT sufficient, and the doc says so.**
Codex proposed replacing *"not nameable"* with *"nameable but not constructible."* That is correct as
far as it goes and is adopted — but applying it **literally** leaves the sentence *"nameable but not
constructible, **so** the two-argument form has no constructor at all"*, which is still a
non-sequitur, because the two-argument form's unconstructibility has nothing to do with the passkey
in either direction. §3 and §6 therefore delete the "so" clause outright rather than repairing its
premise, and state the actual reason. Recorded here because a reader comparing the Codex review
against this document would otherwise see its counter-proposal applied and assume the finding closed.

**No round-2 finding was rejected.** Unlike round 1 — where `[const §VIII.3]` was rejected outright —
all ten round-2 findings are accepted and applied.

### Figures and instruments re-derived for v0.3

Per R2-RC#3, every figure this revision touches was **executed**, not carried forward, and the
scanner is printed beside the number it produces. Working tree `e0574ee7`; probe compiled with
`clang++ 22.1.2 -std=c++23 -O0 -Wall -Wextra`, **zero warnings**.

| quantity | v0.2 | v0.3 | how |
|---|---|---|---|
| ctor-entry dictionary `use_count`, v0.2's body | 2 (implied by *"the only one"*) | **3** — two pairs | compiled probe, sampled at ctor entry |
| ctor-entry dictionary `use_count`, sequenced body | — | **2** — one pair, as claimed | same probe |
| ctor-entry `use_count`, rvalue caller | — | **1** — zero pairs | same probe; the quantity is **call**-determined |
| `table_view` moves, both bodies | 2 | **2** — unchanged, corollary survives | same probe |
| allocations, both bodies | 1 | **1** — unchanged | same probe |
| seam 4 vs a copying impostor | (unstated) | real `same_addr=1 shared_owner=1 use_count=1`; impostor **`0, 0, 1`** | same probe — v0.2's assertion set is green for the impostor |
| seam 5 pins vs the "key opened" mutation | (unstated) | **A1–A4 compile CLEAN; only A5 goes red** | 3 compilations: closed/5 pins, open/4 pins, open/5 pins |
| seam 5 pins vs their own mutations | (unstated) | **5 of 5 RED, one error each** | 5 compilations, one `-D` per pin |
| Option B scanner, doc recipe | 28 | **31** | executed |
| Option B scanner, member-call `.cpp` | 28 | **28 = 1 + 27** | executed; `comm -23` of the two sets returns exactly the 8 enumerated files |
| `catch (...)` in `src/capi/config.cpp` | 5 | **5** — unchanged, but **4 translate / 1 aborts** | read all five handlers |
| `std::abort()` in `src/capi/` | 1 (`config.cpp:271`, implied) | **5** — `engine.cpp:93`, `engine.cpp:126`, `session.cpp:179`, `session.cpp:317`, `config.cpp:271` | `grep -rn` ABI-wide |
| G1 / G2 census gates | — | **5 cases executed**: conforming `exit 0`; G1 violation FAIL; G2 violation FAIL; G1 DEAD; G2 DEAD | the seam-7 script run verbatim against five synthetic trees |
| `[arch §5.3]` quote | `architecture.md:391` | **`:396`** (`:391` is the heading) | opened |

**Unchanged on re-derivation for v0.3, and re-opened rather than carried:** **230 sites / 126 files**
for Option F over `tests/ bench/` plus **1** production site (`src/capi/config.cpp:208`) = 231/127;
**15** `table_view` mutators at the exact lines §2a tabulates; the `public: :250` → `private: :732`
span; `noexcept = default` move ctor at `:260`; **12** builder calls in `src/dictionary/dictionary.cpp`;
the 7-hit `dictionary_view` census with its 1/3/2/1 breakdown; `[2i §5.2]`'s three-symbol whitelist
verbatim at `2i-capi.md:1248`; the seam-1 fixture anchors (`validation_test_dictionary.hpp:50`,
`:72-74`, `:75`, `:125`, `:131`) and observation helpers (`test_validate_gate_inbound.cpp:184`,
`:213`, `:237`, `:325`, `:336`); `session.cpp:979-981`, `:1001-1004`, `:1012-1014`, `:1250`,
`:2100-2107`; `bench/dictionary/table_view_footprint_bench.cpp` at `+39` in `git diff --stat
main...HEAD -- bench/`; and every remaining row of `## Normative References`.

### The two instruments that failed, and why they are recorded

Both were in **seam 7 — the gates written specifically to satisfy the "proven non-zero on a violated
tree" requirement.** Neither was visible by reading. Both were caught by running.

**(1) G2's first regex read ZERO on the deliberately violated tree.** The natural pattern
`shared_ptr<[[:space:]]*const[^>]*table_view[[:space:]]*>[[:space:]]*\([^)]*,` cannot match
`std::shared_ptr<const table_view>(std::move(snap), p)`, because `std::move(snap)` closes a
parenthesis before the comma and `[^)]*` stops there. It would have shipped as a gate that is
**permanently green over a real violation** — the exact false-green class `/speckit-verify` Step 1
exists to prevent. Replaced with an alternation matching both `(std::move(` and `(<identifier>,`.

**(2) The first gate script aborted SILENTLY on its own passing branch.** Under `set -euo pipefail`,
`grep … | grep -v … | wc -l` fails the whole pipeline when *either* grep matches nothing — and for
the "sites outside the allowlist" count, matching nothing **is** the success condition. The
conforming tree therefore exited `1` with no message: a gate that reports failure on a clean tree,
which is how gates get switched off rather than fixed. This is the project's recorded *`[ cond ] &&
fail` under `set -e` aborts on the passing branch* trap in a **second shape** — pipeline exit status
rather than test exit status — and the general lesson is the wider one: **under `set -e`, `grep` is
not a predicate, it is a command that fails.** Fixed by guarding every grep with `|| true` and
capturing the hit list once.

The second failure also produced the requirement that the gates assert **liveness**, not only
absence. A scanner that prints nothing on success prints nothing just as willingly when the type has
been renamed or cwd is wrong. G1 and G2 therefore each carry a lower bound on their *pre*-allowlist
hit count, and both DEAD arms were executed (cases D and E in seam 7's table) rather than assumed.

Recorded rather than quietly fixed because together they are the strongest evidence in this document
for its own methodology: the requirement caught **two** broken instruments on its first application,
inside the very gates written to satisfy it — and one of them was broken in the direction that reads
as *"everything is fine."* A number can be right by luck. An instrument that has never been shown
red has not been tested at all.

### Net effect of v0.3

**No structural change and no change to the recommendation** — §1, §2, §2c, §5b, §5c and §7 are
untouched apart from cross-references, and Option C wins on the same argument it won on in v0.1.
What moved: §3 gains a compilable factory body, a corrected passkey account, a corrected Option B
census and an inverted Option F(b) census; §4's cost is re-derived from that body with the refcount
claim corrected and the moves confirmed; §6 gains a mutation matrix, a strengthened seam 4, a
sequencing constraint on seam 1 and an entirely new seam 7; `## Normative References` re-points one
anchor and narrows a claim it had not earned.

The document is **weaker in two places than v0.2 presented it** — the C-ABI counter-weight is
withdrawn and Option F's rejection now costs it an explicit error-model choice, and Option B's
published scanner did not derive Option B's published number. Both are stated in the body, not only
here. **What is stronger is the class of evidence:** v0.2 asserted that its seams pinned the design;
v0.3 shows, per assertion site, the mutation each one kills — and shows the four assertions v0.2
shipped killing none of the mutation that matters.

---

**▌ROUND 3 → v0.4 — GATE A CONVERGED**

Gate A round 3: Codex review (one P2, one P3) → Opus adversarial review judging it, **`P1 = 0,
P2 = 0, P3 = 5`, verdict CONVERGE** — *"the document is converged… do not rewrite, and do not send
this to a ground-up redesign. The minimal remaining work is five mechanical edits, none of which
touches §1–§5's argument."* **v0.4 is those five edits and nothing else.** No option was re-costed,
no recommendation moved, no section that no finding touched was grown. Every figure the review
re-derived — the factory's 2 moves / 1 alloc / ctor-entry `use_count` 2, seam 4's `1,1,1` vs
`0,0,1`, Option B's `1 + 27`, the five `std::abort()` sites and their four-translate / one-abort
split, `architecture.md:396`, the §2a 7-hit census — reproduced exactly, and **no blanket claim was
falsified this round**, the first revision of which that is true.

### The one root cause (round 3), and why it is a narrowing rather than a recurrence

**R3-RC#1 — the gates census WHERE a thing is written, never HOW MANY TIMES, and never whether the
REQUIRED thing happens at all.** All five findings are instances at three layers: *quantity* (G1 and
G2 counted files, not occurrences); *existence of the requirement* (G2 censused the **forbidden**
alias formation and never the **mandatory** calls the design declares at both consumers); and *the
source set itself* (G1's liveness was a union total, so a whole allowlisted source could vanish
unnoticed; G2's regex enumerated four spellings and treated them as the population).

This is **not** rounds 1–2's RC#1 on a third pass. RC#1 was *"the seam pins the artifact of the fix
rather than the exclusivity the argument rests on"* — and v0.3 genuinely broke it: it identified
both exclusivity claims, built instruments aimed at them, and proved those instruments red. What
remained is that **the instruments were coarser than the claims they were aimed at**, a calibration
failure one layer below the class that produced the previous two rounds. The general repair is one
sentence, and it is now stated in §6 seam 7 as well as here: **a census supporting a singularity
claim must count occurrences, must bound liveness per source, and must include the sites the design
requires, not only the ones it forbids.**

### Finding-by-finding (round 3)

| # | source | sev | resolution | RC |
|---|---|---|---|---|
| 1 | Codex #1 (P2 → **P3**) | P3 | **G1** now asserts **exactly one `friend` declaration** in the passkey's header, that the one entry names `make_dictionary_snapshot`, and **exactly one production key construction**, in the factory TU. **G2** now asserts **exactly one aliasing-ctor expression** tree-wide. New RED rows **F1**, **F2**, **G**, all three `exit 0` under v0.3 | R3-RC#1 |
| 2 | Opus escalation of #1 (case **H**) | P3 | **G2 gains a REQUIRED-call census**: `src/session/session.cpp` and `src/capi/session.cpp` must each call `shared_dictionary_view` at least once. New RED row **H**, `exit 0` under v0.3 | R3-RC#1 |
| 3 | Codex #2 | P3 | The `⚠️ What C1's closure rests on` table is **split**: three C1 rows (A1–A4, A5, seam 7 G1) and a **separate C4 table** whose single row is seam 1, labelled *a runtime check, not a construction closure*. The duplicated dangling clause that followed the table (*"Never on a compilation that was expected to fail. C1's closure rests on them plus seam 1 — never on a compilation that was expected to fail."*) is **deleted** | — |
| 4 | Opus N-P3-1 | P3 | Seam 1's observable **re-pointed** from `session.cpp:2100-2107` to **`:2743-2749`, emitting at `:2746`**, and the arm is now named: `case fsm_state::LogonReceived: case fsm_state::Active:` (`:2727-2728`), inside `Session::on_inbound_frame` (`:2069`), reached via the `switch` at `:2087`. The twin is named too — `:2104` is the same 041 T014 gate on `case fsm_state::NotConnected:` (`:2088`), on the **first** frame, which this seam's second-message `35=D` cannot reach | — |
| 5 | Opus N-P3-2 | P3 | **G1's liveness is now per allowlisted file** (`>= 1` each) instead of `[ "$g1_all_n" -ge 3 ]` over the union — which the header alone satisfied. New RED row **M**. This also makes G1 do what its own comment always claimed | R3-RC#1 |
| 6 | Opus N-P3-3 | P3 | **Not "fixed" — bounded and recorded.** G2's regex covers the **listed spellings only**; the four evasions are written into §6 seam 7 with row **P** measuring them against the *repaired* gate. The **scan-scope asymmetry is fixed** (G2 now scans `tools/ tests/` like G1) and all three scopes are justified in one clause each | R3-RC#1 |

### The severity call, recorded because it is the round's most consequential judgement

Codex graded finding 1 **P2**. The adversarial review **downgraded it to P3 while escalating its
content** — and both halves matter, so both are recorded rather than collapsed into a verdict.

- **Why the downgrade.** Round 2's `N-P3-1` graded the *total absence* of both singularity gates
  **P3**, with no pre-registered escalation condition, and its prescribed remedy **was** a
  file-boundary allowlist, written out at the same granularity v0.3 then shipped. v0.3 executed that
  prescription and added two liveness halves and five executed cases nobody asked for. Grading the
  faithful execution of a P3 prescription *above* the gap it closes is a severity inversion. The
  review applied a counterfactual test and reports it would have called P3 **absent any round cap**.
- **Why the content was escalated anyway.** **Case H is not Codex's.** Codex constructed three
  sub-claims (second friend, second in-file minter, second in-file alias); the adversarial pass
  constructed the fourth and sharpest — both production consumers *copying* the view — and it is the
  one that re-adds the 17 070 µs FIX50SP2 copy per session while G2, seam 3 and seam 4 all stay
  green. The repair for it (the required-call census) is the assertion v0.4 would have been most
  wrong to omit.
- **P3 here means *does not block the design decision*, not *can be deferred past implementation*.**
  §6 seam 7 now says so in its own hand-off note.

### The plausible-twin anchor class, on its third occurrence

Round 2 fixed `:1255`→`:1250`. v0.3's log fixed the round-2 review's `:260-262`→`:256-258`. Round 3
found seam 1's observable on `session.cpp:2100-2107`. In all three the cited line existed, compiled,
and read correctly — a *same helper, same shape, wrong context* substitution, which is strictly worse
than a stale anchor because a stale anchor usually fails to resolve.

**v0.4's response is deliberately not a fourth rule.** Rules have not stopped it; three revisions of
this document already carry anchor-freshness discipline and it recurred anyway. The response is a
practice, recorded in the header: **every line this revision cites was re-opened in the working tree
rather than copied from the review that reported it.** That is how `:2743-2749` / `:2746` /
`:2727-2728` / `:2088` / `:2069` and the five `emit_session_reject_` call sites were obtained here,
and it is how the `case` labels — the thing the twin substitution turns on — entered the citation at
all. The one habit worth generalising is narrow: **when an anchor names a `switch`/state-machine arm,
cite the `case` label with the line.**

### Gate output measured for v0.4, and what it changed

The v0.4 script was **re-extracted from this document's own fence** and run against eleven scratch
trees built from §3's sketch *with its comments intact*; the v0.3 script was run against the same
eleven. Full strings are in §6 seam 7's table. The summary:

| tree | v0.3 | v0.4 |
|---|---|---|
| A conforming | `exit 0`, silent | `exit 0`, **8 lines of counts** |
| B second minter outside the allowlist | `exit 1` | `exit 1` (message unchanged) |
| C hand-rolled alias outside the allowlist | `exit 1` | `exit 1` (message now the exact-count one) |
| D type renamed | `exit 1` | `exit 1` (message now names the file) |
| E helper's alias removed | `exit 1` | `exit 1` (message unchanged) |
| **F1** second `friend` in-file | **`exit 0`** | **`exit 1`** |
| **F2** second minter in-file | **`exit 0`** | **`exit 1`** |
| **G** second alias site in-file | **`exit 0`** | **`exit 1`** |
| **H** both consumers copy instead of aliasing | **`exit 0`** | **`exit 1`** |
| **M** A1–A5 TU deleted | **`exit 0`** | **`exit 1`** |
| P four alternative spellings | `exit 0` | `exit 0` — **recorded limitation, not a repair** |

Two things follow that are worth stating rather than leaving in the table. **Row A's recorded
output changed**, because v0.4's gate is no longer silent on success — carrying v0.3's *"silent,
`exit 0`"* forward would have been an asserted row rather than an executed one. And **a third
instrument failure was found in the process**: the natural `grep -cE '\<friend\>'` selector returns
**2** on §3's header as published, because the header's own comments contain the word — case A would
have gone red on a conforming tree. It is fixed by anchoring to statement position and is recorded
in §6 seam 7 alongside v0.3's two. That failure was only visible because the scratch tree copied the
header verbatim, comments and all; the corollary is now stated there too — **an instrument never
shown green on a faithful conforming tree has not been tested either.**

### Net effect of v0.4

**No design change and no structural change.** §1, §2, §2a–§2c, §3, §4, §5a–§5d, §7 and
`## Normative References` are byte-untouched. What moved is **six** places, enumerated in full
rather than at the three that carry the findings:

1. **§6 seam 1's observable** — one bullet, re-pointed and given its FSM arm.
2. **§6 seam 5's C1 table** — split into C1 and C4, corrupted clause deleted.
3. **§6 seam 7** — both gates strengthened, three scan scopes justified, six new executed rows, one
   limitation stated, one instrument failure added.
4. **§6's closing paragraph** — the hand-off note on `/speckit-verify` Step 1.
5. **The header block** — status paragraphs, and the v0.4 anchor note recording the plausible-twin
   recurrence.
6. **The round-2 log entry's seam-1 anchor** — corrected in place with a pointer to this section,
   because leaving a known-wrong anchor in the log is the trap the entry itself is about.

Items 4–6 carry no finding. They are listed because a census quoted only at its load-bearing subset
is how this document's Option B recipe went wrong, and a wrong census of what moved — inside the
section whose job is recording what moved — would be the first thing a round-4 reader opened with.

**What is stronger is one class of claim.** v0.3 proved its gates could go red. v0.4 proves *which
mutations they go red under and which they do not* — and publishes the second list. The four
regex-evadable spellings are shipped as a stated boundary of the instrument rather than quietly
left inside an allowlist that reads like a census, which is the same distinction v0.3 drew between
an evaluated assertion and a "must not compile" probe, applied one level up to the gate itself.
