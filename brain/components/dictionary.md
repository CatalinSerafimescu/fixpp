---
type: Component Decision Map
title: dictionary — two loaders, a version registry, and a catalogue that under-reported them
description: A substantial shipped subsystem whose catalogue status under-reported it until feature 094 reconciled D-001, D-003, D-007 and D-008 to done.
status: stable
refs:
  - src/dictionary/xml_loader.cpp
  - src/dictionary/orchestra_loader.cpp
  - src/dictionary/overlay_merge.cpp
  - src/dictionary/version_registry.cpp
  - src/dictionary/reify.cpp
  - include/fixpp/dict/reify.hpp
  - include/fixpp/dict/version_registry.hpp
  - include/fixpp/dict/version_profile.hpp
  - include/fixpp/dict/dictionary.hpp
  - include/fixpp/dict/table_view.hpp
  - .specify/2c-codegen.md
  - .specify/215-dictionary-view.md
  - .specify/495-493-486-dict-reify-copy.md
  - .specify/456-table-view-seal.md
  - .specify/447-458-452-capi-refusals.md
  - specs/090-capi-refusals/contracts/msg-clone.md
  - specs/090-capi-refusals/data-model.md
  - specs/090-capi-refusals/quickstart.md
  - specs/090-capi-refusals/spec.md
  - specs/090-capi-refusals/tasks.md
  - specs/057-behavioral-reify-unblock/data-model.md
  - specs/057-behavioral-reify-unblock/plan.md
  - specs/057-behavioral-reify-unblock/research.md
  - specs/057-behavioral-reify-unblock/contracts/reify-dispatch-bridge.md
  - specs/099-fixt-appl-version-validation/research.md
  - specs/099-fixt-appl-version-validation/data-model.md
  - specs/100-wire-group-read-integrity/research.md
  - specs/100-wire-group-read-integrity/plan.md
  - spec/behaviors-and-limitations.md
  - dictionaries/README.md
  - docs/src/dictionary/codegen.md
refs_external:
  - research/G19-fix-fpml-iso20022/decisions/2c-codegen.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/002-dictionary-xml-loader-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/057-behavioral-reify-unblock-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/090-capi-refusals-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/090-capi-refusals-implement-log.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/096-fix-latest-tail-evidence-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/097-fix-latest-applverid-10-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/098-dialect-overlay-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/099-fixt-appl-version-validation-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/100-wire-group-read-integrity-gatea.md
codegraph_entry: [Dictionary, xml_loader, orchestra_loader, field_traits, version_registry, table_view_builder]
constitution: ["§I.1"]
---

# `dictionary`

> ## ⚠️ The CODE is authoritative. This page is not.
>
> SecondBrain is a **consultant**, not a source of truth. It points you at the right files and explains
> **why** a decision was taken and what was **rejected** — that half is historical and does not change
> retroactively. It does **not** establish what the code does today.
>
> **Anything here describing current behaviour is a LEAD TO CHECK, not a fact to cite.** Verify against
> source before you rely on it, and cite the source, not this page.
>
> This page exists because signed-off design documents rotted. **It has no immunity from that** — a page
> trusted instead of read becomes the next fossil, and it would be a worse one, because it is the page
> people come to for the fossil list.

## What is actually here

A real subsystem, not a thin layer: **two independent loaders** — `xml_loader.cpp` (QuickFIX-style FIX
XML) and `orchestra_loader.cpp` (FIX Orchestra) — plus `reify.cpp`, `field_traits.cpp`,
`version_registry.cpp`, `version_profile.cpp`, `dictionary_snapshot.cpp` and a
`reify_dispatch_bridge`. **Two owning design docs**: `2c-codegen.md` (header layout, multi-version
coexistence, dialect overlay binding) and `215-dictionary-view.md`. ⚠️ 215's alias design — §3's
`shared_dictionary_view` forming an aliasing pointer, §5b's "third owner of the snapshot's control
block", §6 seam 7's G2 count of one — is **superseded in part** by
`.specify/495-493-486-dict-reify-copy.md` §6 (D-4): the snapshot owns its table in the table's own
control block and G2 asserts zero matches. The passkey and the provenance check stand. ⚠️ 2c's
"dialect overlay binding" half is **superseded by 098**; see the 098 section at the end of this page.

⚠️ **Counts and file lists rot.** Derive the current surface from the graph index; the point above is
the *shape* — two loader front-ends converging on one dictionary representation, with codegen on top.

## ⚠️ The catalogue under-reports this family — a LEAD, not a verdict

> **Resolved 2026-10-22 by feature 094:** D-001, D-003, D-007 and D-008 now read `done`, each with its delivering PR and witnessing tests (`spec/feature-catalogue.md`). The table below is the 2026-08-29 observation that led there, kept as history.

`spec/feature-catalogue.md` defines `Status ∈ {backlog, planning, implementing, done, dropped}`, where
**`backlog` means not started**. Several `dictionary` rows sit at `backlog` while the tree plainly
contains the thing:

| Row | Says | Observed 2026-08-29 |
|---|---|---|
| **D-007** — XML data dictionary format loader | `backlog` | `src/dictionary/xml_loader.cpp` exists and is **over a thousand lines** |
| **D-003** — FIX 5.0SP2 + FIXT.1.1 dictionaries | `backlog` | ⚠️ **not concluded.** Generated headers are produced at **CMake configure time**, so their absence from `include/` proves nothing either way |
| **D-008** — code-generated constexpr field metadata | `backlog` | ⚠️ **not concluded, and plausibly accurate** — a search for constexpr field-metadata surfaces found nothing. Codegen shipping does **not** imply this specific mechanism did |

> ⭐ **Only D-007 is stated as a discrepancy.** The other two are the interesting part of this table:
> they *look* stale and are **not established as stale**, because the obvious probe is blind to
> configure-time generation. **A row that looks wrong is not a row that is wrong** — and a page that
> flattened all three into "the catalogue is stale" would be manufacturing exactly the false claim this
> bundle exists to prevent.

> **Adjudicated 2026-08-31 (user), for the COLUMN not for these rows.** The same question was open
> on `nfr` and is now closed: a `backlog` cell in this catalogue can be **merely unflipped**, not
> unbuilt. See [`nfr-and-tooling.md`](nfr-and-tooling.md) for the reasoning and the derivation recipe.
> An out-of-repo planning tracker additionally names **D-001, D-003 and D-008** in its
> *delivered-practice-never-flipped* set. ⚠️ **That is a lead and nothing more.** It is planning
> material, not a source of truth, and this page's own probe for D-003/D-008 was *blind* rather than
> negative — so the two rows above stay **not concluded**. Resolve them by reading the configure-time
> codegen output, not by believing either document.

**This needs per-row adjudication against source, not a bulk verdict.** See the sibling note on `nfr`
in [`nfr-and-tooling`](./nfr-and-tooling.md), where the same status column is unreliable for a
different and possibly legitimate reason.

## Group context keys — one walk, one clamp, and three rejected alternatives

A repeating-group *context* is keyed by `(msg_type, ancestor count-tag chain OUTERMOST-FIRST, no_tag)`
and clamped to `kMaxGroupContextDepth`. Several places reconstruct that chain. The decisions below are
the **why**; verify any behavioural statement against source before citing it.

**DECIDED — one walk, one clamp, in that order.** `detail::group_parent_path` walks and reverses but
does NOT clamp; the clamp lives only in `make_group_ctx_delim` / `make_group_ctx_key`, which keep the
first — i.e. OUTERMOST — K entries.

**REJECTED — clamping inside the walk.** Stopping the walk at K keeps the INNERMOST K, which is a
*different key for the same context* once the chain is longer than K. That was issue #264: the FR-023
completeness probe clamped during its walk while the loaders' capture path and `as_table_view()`
clamped after theirs, so a COMPLETE dictionary was refused at load by a message asserting an internal
invariant violation — sending readers to look for a bug in `as_table_view()` rather than in the probe.
⚠️ The tempting variant *"bound the walk at K+1 since the clamp discards the rest anyway"* is the same
defect in new clothing; the walk must stay unbounded.

**REJECTED — truncating the chain at a repeated tag when the relation has a cycle.** This looks
fail-closed and is not: a truncated array is a **well-formed key**, so instead of missing every record
it can COLLIDE with one. A self-parented group truncates to `[G]`, which is exactly the key of that
group's own inner occurrence, so the probe *matches* and reports the dictionary complete. A cycle now
yields no path at all, and both callers treat that as a violation.

> ⚠️ **The parent relation is NOT guaranteed acyclic, and the reason is easy to miss.** Both loaders
> reduce a message's field run to one `FieldRef` per tag using an **unstable** sort, so which of two
> equal-tag occurrences survives is *unspecified* — and when the inner occurrence of a self-nested
> group wins, the relation holds `immediate_parent[G] == G`. Do not re-derive how often that happens;
> the answer is a property of the standard library's sort, not of this code.

**REJECTED — refusing any chain longer than K.** Measured, not argued: a *lone* context past the clamp
loads and resolves correctly end-to-end, because `wire::group_context::pushed` saturates by dropping
the push at K — keeping the OUTERMOST K, the same end `make_group_ctx_key` keeps — and every production
query derives its span from a `group_context`. Store key and wire query key therefore coincide past the
clamp. Depth alone is not the defect, so a depth rule would refuse input that demonstrably works.

**DECIDED — refuse only a measured COLLISION.** Past K the clamp is lossy, so two genuinely different
contexts can produce byte-identical keys, and `group_ctx_key::parent_path` is a fixed K-element array
that cannot hold them apart. The key-dedup in the shared `flush_group_ctx_delims` would keep the first
and silently DROP the second, leaving the survivor to answer for both — a message nested under the
dropped parent resolving the *wrong delimiter*. Each record now carries its unclamped chain beside the
clamped key, so the flush can tell "same key, same chain" (a benign duplicate — a group declared in
both header and body) from "same key, different chain" (unrepresentable, refused). Loader-local: the
store, `group_ctx_key` and every query path are untouched.

⚠️ **This check REJECTS a dictionary, so its false-positive arm is the load-bearing one.** A test that
only proves it fires cannot catch it firing when it should not — and a spurious hit here is a hard
rejection of valid input, which is the defect #264 was filed about. Both arms are witnessed, and both
loaders are, because the refusal is shared but each raises its own exception type (FR-006c).

> ⭐ **This is failure class 7 in [`failure-classes.md`](../failure-classes.md), and #264 is its
> reference instance.** The spurious rejection was *the only thing* keeping chains past the clamp out
> of a store that cannot represent them. Fixing the false rejection — correctly — is what exposed the
> silent merge behind it. The fix and the newly-reachable defect were found in the same review round,
> by two independent reviewers, neither of which was looking for the second one.

**The query side does NOT clamp, and that is now ASSERTED rather than left as a lead.**
`group_ctx_query` / `group_ctx_equal` compare the caller's span verbatim — a four-iterator
`std::equal`, so any length difference is a MISS, not a truncation. An over-long path therefore
matches *nothing* and reads to the caller as "context not declared": a fall-through to the bare global
store for `group_first_field`, a `nullopt` for `group_first_field_exact`.

No production caller can reach it. Every context lookup builds its span as
`{ctx.parent_path.data(), ctx.depth}` from a `wire::group_context`, whose `parent_path` is a fixed
`kMaxGroupContextDepth` array and whose `pushed()` DROPS the push at capacity. The residual hazard is a
TYPE one: "clamped key" and "raw ancestor chain" are both `std::span<std::uint16_t const>`, so nothing
at a call site distinguishes them — and the dict layer now has an unclamped chain builder
(`detail::group_parent_path`) one step away.

The precondition is stated ONCE, in `make_ctx_query`, which all four context accessors go through. It
is `assert`-based deliberately: those accessors are `noexcept` and sit on the validator's per-group
path, so it must cost nothing in release. That means the guard exists only where `NDEBUG` is undefined
— it converts a silent wrong answer into a loud one under test and debug builds, and changes nothing
in release. ⚠️ A release build therefore proves nothing about it; the witness
(`TableViewCtxQuery.OverLongAncestorPathTripsTheClampAssertion`) is compiled and COUNTED under
`NDEBUG` via `GTEST_SKIP` rather than `#ifdef`'d away, so the release leg reports a skip instead of the
test silently vanishing from the suite.

**Making the wrong key unrepresentable — a `group_ctx_path` type producible only by clamping — was
considered and NOT done.** It would change key types in an installed public header and touch every
consumer, which is a different change from the one that fixed #264. The assertion is the proportionate
guard for a footgun that is real but currently unreachable.

## Length+Data pairs from Orchestra `lengthId` (#427)

The Orchestra loader reads `lengthId`, so `Dictionary::length_pair_data_tag` answers for
FIX Latest (B-427-1). The standard pair table (`include/fixpp/core/length_data_pairs.hpp`)
is drift-tested against the union of all ten dictionaries. A dictionary's own pair matters
only when neither of its tags is standard (B-426-3).

⚠️ **The QuickFIX-XML loader pairs by ADJACENCY, and before 091 its container walk did not enter
`<component>` or `<group>`** (found at 091's Gate A, fixpp#418). `LoaderState::detect_length_pairs`
paired a LENGTH with the DATA/XMLDATA that immediately follows it in two places only:
- in `<fields>` declaration order;
- among the direct `<field>` children of `<header>`, `<trailer>` and each `<message>`.

FIX 5.0 SP2 declares some standard pairs adjacently only inside components or groups, with their
`<fields>` order inverted or far apart. The loader therefore reported no pair for them, and codegen,
which couples only what the loader reports, emitted them as two independent Args members. One of
those messages emits Data before Length.

091 (FR-017) added a second walk into every `<component>` definition and every `<group>` at any
depth; read `detect_length_pairs` for the current walk rather than this page.

The union drift test could not see this, because Orchestra supplies every pair; see
[`wire`](wire.md). Re-derive the affected set with 091's `research.md` R-11 recipe rather than from
this page.

Decisions (owner, 091 Gate A):
- **Fix the loader, not codegen.** Coupling in codegen from the standard table would have left the
  v50sp2 `Fields`/`Validator` tables silently missing the pairs, and added a second source of pair
  truth.
- **The walk recurses into every `<group>`, whatever its parent** (header, trailer, message,
  component or group), and a non-field child breaks adjacency in the new containers. Reviewers kept
  finding one more parent the witnesses did not cover. The class closed only when the witness set was
  derived from the complete set of parents found in `dictionaries/*.xml` plus what the schema allows.
  Listing examples did not close it.
- **The change reaches user-loaded dictionaries too.** A custom pair declared only inside a component
  becomes a dictionary pair, and a C-ABI commit that used to succeed now refuses. 091 declares it
  **C-ABI 1.9 BREAKING** (`[const §X.7]`; `include/fix/c_api/version.h`'s 1.9 history), not scoped
  out, so the loader's answer stays right for the dictionary the user wrote. See
  [`c-api`](c-api.md) for the population.

⚠️ **The table lives in `core`, not `wire`.** `table_view` must classify a tag, and the
dictionary layer may not include wire ([arch §2.3]); `include/fixpp/wire/length_data_pairs.hpp`
survives as a re-export of the same names, so a reader who follows an older pointer lands on
using-declarations rather than the table. ⚠️ `tools/check_layers.py` walks `src/` and
`bindings/` only, so a dictionary **header** including a wire header passes it **silently** —
the move was made for correctness, not because a gate demanded it.

`table_view` carries one `bool` — some registered pair has **both** tags outside the standard
table — because `wire::dict_hooks::for_table_view` installs the pair callback **only** when it
is set, and that bundle is built per message. What was rejected: a sixth `dict_hooks` pointer to
an inline 8 KiB bitset (contradicted design §3's five fields and `table_view`'s own
footprint-sized structure), and computing the predicate inside `for_table_view` (it is on
per-message paths). `set_length_pair_data_tag` is strongly exception-safe at **O(1)** — an
earlier transactional version copied both maps per registered pair and made
`Dictionary::as_table_view()` quadratic (+26.3 % on FIX42); do not reintroduce it. Zero is
refused on **both** halves, at the setter *and* at pair formation in both loaders, because zero
is the "no pair" answer of every accessor.

⚠️ **fixpp#457 then moved the refusal UPSTREAM of all three, and that changed which of them a
test can still reach** — the kind of thing a page like this exists to record, because the guards
are still in the source and read as live. A field numbered 0 is now refused at DECLARATION in
both loaders, upstream of both guards.

**Do not read either guard as a witnessed path, and do not take a reachability verdict from this
page** — derive it, because the two halves differ and the difference follows from where each value
comes from: a `data_tag` is a key of the loader's own field store, which the declaration rule bars
from zero; a `length_tag` comes from a `lengthId=` reference, parsed with the shared
`parse_orchestra_id` (which must keep admitting zero for the structural-id namespace) and resolved
*after* the declaration check. The recipe: read `parse_document` / `collect_fields` call order and
ask, for each argument, whether it originates in a declaration or in a reference. The guards are
kept as boundary conditions — zero is the "no pair" answer of the accessors, which is a property of
the accessors rather than of today's callers.

⚠️ **This closes a disposition `specs/002-dictionary-xml-loader` deferred for itself.** That spec's
§10 **follow-up F4** and its `CHK017` checklist row both list `<field number="0">` as an XML-grammar
edge case left undecided in v1.0 ("rejected vs accepted" unstated). The bundle is frozen, so it
still reads as open; the answer is REJECTED, here and in `B-457-1`. Pointer lives here because a
frozen bundle cannot carry it. The decision that keeps the
rule off the shared parser is the load-bearing one: `<fixr:component id>` / `<fixr:group id>`
are a repository-local surrogate key where `id="0"` is legal, so the Orchestra rule lives in
`parse_orchestra_field_tag` and the false-positive arm
(`OrchestraFailClosed.ZeroStructuralXmlIdsAreStillAccepted`) is what holds the two namespaces
apart. **`table_view`'s own mutator surface was the residue, and fixpp#456 MOVED it rather than
closing it.** The population members are private now and reachable only through
`table_view_builder` — `include/fixpp/dict/table_view.hpp`'s STORAGE banner states the mechanism,
and is the one place that does. The *hazard* is unchanged: the builder accepts everything the view
used to, and `build()` is `return std::move(tv_);` with no validation of any kind, so a hand-built
table can still plant a zero where a loaded dictionary cannot. That is why `B-384-2` was **amended,
not closed** — its wording now names the `table_view_builder` surface instead of the hand-built
view — and why `L-456-1` was filed beside it.

## The reify handle materialises EAGERLY and refuses a failed re-parse (090, fixpp#458)

`fixpp::dict::detail::owning_message_handle_from_frame` (`src/dictionary/reify.cpp`) builds the
handle's view when it mints the handle. Before 090 the build happened lazily, on the first `view()`.
When a dict-backed source's re-parse fails, the factory now returns that wire error through its
`expected_t` channel, and so do `reify()` and the generated dispatch. Before, it fell back silently
to a dict-free view that lost the dictionary's Length+Data pairs. This is `B-458-2`, BREAKING for a
direct C++ caller. The C-ABI twin is `fixpp_msg_clone`; see [`c-api.md`](./c-api.md).

- **Why eager.** `view()` is a `noexcept` accessor that returns a reference, so a refusal found
  lazily had nowhere to go. The factory already had a refusal channel (`dict_reify_oom`). It simply
  ran the fallible work later. The design note's v0.1 had called that deferral a source fact rather
  than a choice.
- **Rejected:** v0.1's three-state status accessor on the handle. It could not report a framing
  failure, and it could not be both non-allocating and post-materialisation.
- **Kept as it was, on purpose:** a framing failure, a framed-but-empty span, and a dict-free view's
  degraded build still return a live handle. Only the re-parse of a frame that framed successfully
  refuses (`include/fixpp/dict/reify.hpp`'s factory comment). `.specify/2c-codegen.md`'s reify
  error entry carries an in-place *"Updated (fixpp#458 …)"* note saying the same.
- **What eager costs, measured at Gate B (PR #494).**
  - **The moved work is not new work:** the framer pass plus the dict-backed parse used to run on the first `view()`, and a caller that reads the handle pays it either way.
  - **It is a small fraction of the factory call.** The factory is dominated by the per-handle `membership_copy()`, which predates 090.
  - ⚠️ **The benchmark that existed before could not see any of this.** `BM_Reify_Dispatch_20tag` passes a view with no MsgType, so `reify()` returns before dispatch. `BM_Reify_DictBacked_20tag` was added to reach the factory. Check that a bench reaches the path it is named for before citing it.
  - **One pre-existing row still went past +5%**, from code *placement*: the function's instructions were identical and its alignment moved.
  - **Rejected:** forcing alignment or moving the function to a separate file to recover it. Either would tune the code to one benchmark and move the layout luck elsewhere.
  - `[const §VIII.2]` makes accepting it a non-author decision. The figures and procedure are in the 090 verify record's *Gate B G-1* section.
- **fixpp#495 / #493 (`.specify/495-493-486-dict-reify-copy.md`):** the factory no longer
  deep-copies the membership table for a view parsed on `Parser`'s **owned route** (every view the
  `Session` hands an application, and every handle's own view); the handle holds one more reference
  to that table, and pins the table only — never the `Dictionary` (D-4). The handle's impl comes
  from `mr` (D-1c). Every copy re-parses under its source's `OffsetTable::Config` (#493). Rejected:
  a public owned route (R-A keeps it `detail`), a `shared_ptr` by value on every view (an atomic
  pair per inbound message), and the owner token inside `dict_hooks` (size-pinned `entry_context`).
  A view parsed through a borrowed `Parser{tv}` still copies (`L-495-1`).
- ⚠️ **Superseded in part (fixpp#493):** 090 made a dict-backed clone of a raised-cap source
  refuse with `FIXPP_ERR_WIRE_LIMIT_EXCEEDED`. Frozen 090 records that still describe that
  refusal, flagged here and not edited (frozen `specs/` are not rewritten):
  `specs/090-capi-refusals/contracts/msg-clone.md` §4 and `data-model.md` §4.1 (the
  `wire_offset_table_full` "raised-cap route" row), `quickstart.md` V5, `spec.md` User Story 3's
  *Independent Test*, and `tasks.md` US3 / T047 all describe a source parsed at a raised
  `max_offset_entries` making a dict-backed `fixpp_msg_clone` return
  `FIXPP_ERR_WIRE_LIMIT_EXCEEDED`. Since #493 that clone succeeds under the source's own caps:
  `.specify/495-493-486-dict-reify-copy.md` §4 and T-3. The code still comes back for a source
  whose own build failed at the default cap (T-6).
- ⚠️ **Superseded in part (fixpp#495 D-1c):** frozen 057 records describe the
  `owning_message_handle` as a **heap** pimpl, flagged here and not edited (frozen `specs/` are
  not rewritten): `specs/057-behavioral-reify-unblock/data-model.md` E-1, `plan.md`,
  `research.md` and `contracts/reify-dispatch-bridge.md`. Since #495 the impl is allocated from
  the `mr` passed to `reify`, not the global heap: `.specify/495-493-486-dict-reify-copy.md` §2.5
  (D-1c), the `include/fixpp/dict/reify.hpp` class comment, and `B-495-4`. Re-derive the sites
  with `git grep -n -i 'heap pimpl' -- specs/057-behavioral-reify-unblock`.
- ⚠️ **Open residuals:** `L-458-1` covers a dict-backed build that under-indexes near the probe cap
  while reporting success; `L-495-1` the borrowed-route copy. Check the live B&L file before
  treating either as open. ⚠️ **Superseded by 100 (`100-wire-group-read-integrity`) for `L-458-1`:**
  the index build has a per-build probe budget and fails over it, and `L-458-1` is in the closed B&L
  file (`B-100-2`); see [`wire.md`](./wire.md).

> ⚠️ **Before you write "immutable" about a `table_view`, read that banner.** #456 took three Gate B
> rounds and found **zero** code defects across all three; every finding was prose claiming more than
> the type delivers. What the seal buys is reachability of the population members. It does not make
> the object unwritable: the `std::uint16_t` elements behind the span accessors are allocated by the
> member vectors and are not `const` objects, so a `const_cast` and a write through it is defined
> behaviour — on a `const` view as much as a non-`const` one. Declaring the view `const` closes
> move-from and `optional::emplace` re-seating, and not that. The rejected alternatives are worth
> knowing too: a runtime `frozen_` flag, freezing at `as_table_view()`, and a facade over a reference
> all died to the same discriminator — #456's witness mutates a *default-constructed* view, so any
> design leaving a reachable mutation surface on a `table_view` **value** fails it.

## Where the design decisions live

`2c-codegen.md` is **v1.4 post-sign-off** and was scanned clean in the Step-R sweep. ⚠️ Its
dialect-overlay design did not ship; 098's in-place notes are listed in the 098 section below. ⚠️ Beside it sits
`2c-codegen.draft-r1.md` — an archived **v0.1** holding a design that an adversarial review **rejected**
as needing a full rewrite. It now carries a forward-pointing banner; before 2026-08-29 it did not.

`specs/096-fix-latest-tail-evidence/data-model.md` §B.2 and its `research.md` restate the Orchestra
loader's group-delimiter rule (the first emission, component and nested-group emissions included) as
the behaviour a test-side Orchestra walk must match independently. It describes, it does not decide:
verify against `capture_first_emission` in `orchestra_loader.cpp`. See [`test.md`](./test.md).

## FIX Latest is ApplVerID 10 (097) — superseded records, flagged in place

`097-fix-latest-applverid-10` gives FIX Latest its own application version: `application_version::vlatest`, wire ApplVerID `10`, with its own `version_registry` slot and a one-way fallback (an empty `v50sp2` slot answers from the `vlatest` slot, never the reverse). Verify against `include/fixpp/dict/version_profile.hpp` and `src/dictionary/version_registry.cpp`, and read B-097-1..B-097-4 in the live B&L file. Frozen `specs/` records that describe the older model are flagged here and not edited:

- ⚠️ **Superseded by 097 (`097-fix-latest-applverid-10`):** `specs/074-orchestra-native-reader/` — its reconcile (FIX Latest sharing the `v50sp2` application version and registry slot), FR-010 (the FIX50SP2 + FIX Latest fail-loud guard) and L-074-1 describe the model 097 replaced; L-074-1 now sits in the closed B&L file.
- ⚠️ **Superseded by 097 (`097-fix-latest-applverid-10`):** `specs/076-fix-latest-typed-codegen/` prose describing the `vlatest` IR row's `.a = v50sp2` and `app_version_enum` mapping `vlatest` to `v50sp2`; since 097 the row reads `.a = vlatest` and the generated `fixpp::vlatest` classes report `application_version::vlatest` (B-097-2). The vlatest golden is a test oracle and is regenerated, not flagged.
- ⚠️ **Superseded by 097 (`097-fix-latest-applverid-10`):** `specs/076-fix-latest-typed-codegen/contracts/build-and-verification.md` V-7 and `specs/076-fix-latest-typed-codegen/quickstart.md` claim the `_dispatch/` output is byte-identical between the `FIXPP_CODEGEN_FIX_LATEST` OFF and ON runs; since 097 `_dispatch/reify_dispatch_application.hpp` gains FIX Latest's arm in the ON run only, and `DeterminismTest.AdditiveOffOnByteDiff` asserts that one insertion for that file (097 contracts W-10).
- ⚠️ **Superseded by 097 (`097-fix-latest-applverid-10`):** `specs/080-orchestra-runtime-load/` L-080-1 (a configuration listing both a FIX50SP2 and a FIX Latest dictionary aborts at `version_registry` construction); since 097 the two coexist in one registry, L-080-1 sits in the closed B&L file, and its config half continued as L-097-1, closed by 099.
- `.specify/2c-codegen.md` (2c-codegen) carries in-place `Updated (097-fix-latest-applverid-10)` notes in §4.3 (the `application_version` enum sketch and the wire `ApplVerID(1128)` mapping table), §4.8 (the runtime-dispatch switch paragraph and the `reify_dispatch_application.hpp` sketch) and §6 (§6.3 step 3, §6.6 item 8); the text before each note is history.
- C-ABI: 2c-codegen §5 item 5's application-version C-ABI constant set (`FIXPP_APPL_VER_*`) was never implemented, so 097 adds no FIX Latest C-ABI constant and does not change the C ABI (B-097-2). While that holds, `git grep -n FIXPP_APPL_VER -- include src bindings` prints nothing. ⚠️ **Superseded by 099:** that grep now prints, because C-ABI 1.14 adds the constant set to `include/fix/c_api/dict.h`, with `FIXPP_APPL_VER_FIXLATEST` among them; see [`c-api.md`](./c-api.md).

## Dialect overlay (098) — superseded records, flagged in place

`098-dialect-overlay` ships the overlay as a **load-time** call: a fragment in the base's own format is
spliced into the parsed base document, and the loader builds one ordinary `Dictionary` from the result
(`XmlLoader::load_with_overlay`, `OrchestraLoader::load_with_overlay`, `dict::load_any_with_overlay`;
the merge and its guards are in `src/dictionary/overlay_merge.cpp`). There is no `DialectOverlay` type,
no `Dictionary::with_overlay`, no `was_dialect_promoted` and no conflict policy: every conflict is
refused. Verify against those headers, and read the 098 section of the live B&L file.

**DECIDED — splice the overlay into the parsed base, then run the base's own build** (research R-1).
The merged dictionary is then the hand-edited file by construction, so every downstream store is
equivalent at once. **REJECTED — a merge at the handle level** (2c's `Dictionary::with_overlay`):
every handle store would need its own merge, outside the finalize code that defines it, so
equivalence would become a claim to test instead of a property of the construction; the owner also
ruled the attachment point (spec Clarifications). **REJECTED — a merge into the loader state between
parse and finalize**, for the same reason. **REJECTED — one
overlay format for both bases** (research R-2): the Orchestra loader ignores unknown element names
silently, so a QuickFIX-shaped node in an Orchestra overlay would vanish without an error.

⚠️ **The send path's header set reads the dictionary since 098** (FR-022): `table_view::is_header_tag`,
filled from the metadata handle's header store. See [`session.md`](./session.md), whose rejected
alternative on this point (L-422-1) is superseded.

Records that describe the older design are flagged here and not rewritten. Each carries an in-place
`Updated (098-dialect-overlay)` note naming what shipped; the text before each note is history.

- ⚠️ **Superseded by 098:** `.specify/2c-codegen.md`. Its status block carries a pointer note, and
  each section that states the old design carries its own: §1 (goals 1 and 4), §1.1, §1.2 (the
  merge-cost line), §1.3 (D-009), §2 (the overlay non-goals, "No mid-session overlay grammar extension"
  included), §3, §4.1 (the `FieldRef` comments naming `dict_overlay_unsupported_length_pair`), §4.3
  (`with_overlay`, `was_dialect_promoted`, the metadata-handle sharing path), §4.4 (`DialectOverlay`;
  this note states what shipped), §4.4.1 (the grammar), §4.4.2 (the caps: the values kept, the
  `FIXPP_DICT_*` knobs not), §4.5 (`load_overlay`), §4.6, §4.7.1, §5, §6.1, §6.1.1, §6.2, §6.4 (merge
  contract, conflict policy), §6.5, §6.7, §7.1, §7.3, §7.4, §7.5, §7.6, §8, §9, §10, §11, Appendix A
  (D-009) and Appendix B. Re-derive the set with
  `grep -n 'Updated (098-dialect-overlay)' .specify/2c-codegen.md`.
- `.specify/2c-codegen.draft-r1.md` is the archived v0.1 and is history; it carries no 098 note.
- ⚠️ **Amended by 098:** `.specify/456-table-view-seal.md` §6 seam 2. The friendship census gains
  `friend struct table_view_test_access;` (read-only test access, defined only in `tests/support/`,
  never installed; research R-10 §2, Correction), and the census recipe's pattern gains that spelling.
  Population stays `table_view_builder`'s.
- ⚠️ **Superseded by 098:** the overlay statements of `.specify/2b-wire.md` (§1 goal 5, §4.3's
  `field_iterator` paragraph, §10 row 5), `.specify/2d-threading.md` (§4.5's `SessionConfig` listing),
  `.specify/2i-capi.md` (§4.6's type-mismatch rule), `.specify/2j-controlplane.md` (§4.7's
  `dialect_overlay_id`), `.specify/2l-tap.md` (§D.1's rationale), `.specify/2m-pybind.md` (§4.7's
  Python key), `.specify/api-contract.md` (the 2c surface row) and `.specify/architecture.md` (§4.2,
  §4.4, Appendix Z-6). Each carries an in-place note.
- ⚠️ **Retired by 098:** L-002-2 (the overlay ships), L-422-1 (FR-022) and L-004-1 (stale since
  fixpp#426: `field_iterator` takes a `dict_hooks` value and pairs through it). They sit in the closed
  B&L file.
- **Open from 098:** L-098-3 (the base load paths have no caps and no cycle guard, fixpp#650) and
  L-098-4 (the `std::uint16_t` side-table index wraps; the vendored FIX50SP2 component table already does,
  fixpp#654).

## Two dictionaries on a FIXT session (099) — what this family gained, and what was rejected

`099-fixt-appl-version-validation` is decided on [`session.md`](./session.md), whose section has the session half; this section keeps what lands here. Verify against `include/fixpp/dict/version_registry.hpp`, `include/fixpp/dict/dictionary.hpp`, `include/fixpp/dict/version_profile.hpp` and `include/fixpp/dict/reify.hpp`.

**DECIDED — the registry keeps a FIXT.1.1 dictionary and answers `transport()`; `get()` is unchanged.** A session-layer dictionary is not an application version. **DECIDED — the content test, not the label** (`defines_fixt_admin`, research R-2): all seven admin messages and a Logon accepting 1137, because FIXT11 and FIX Latest pass, a FIX50-family file has no admin messages, and a FIX 4.4 file has them and rejects 1137. **DECIDED — `Dictionary::as_table_view(fixt_framing)` is an overload**, so a non-FIXT session's table cannot move: W-17 pins every store of the no-argument form to a literal per vendored file. **DECIDED — `dict::reify` takes the view's byte when the session wrote one** (research R-19), so the version validated and the version reified cannot differ.

**REJECTED — the registry owning a validator or a table** (research R-3): `[arch §2.3]`, and [`session.md`](./session.md) has the rest. **REJECTED — replacing the session dictionary with the registry's for the same version** (research R-1): it loses the overlay. **REJECTED — composing two dictionaries into one `dict_hooks`** (research R-20): the nested cache keys on one dictionary pointer. The route table is built with S's header pairs instead, `as_table_view(fixt_framing, extra)`. **REJECTED — growing `version_profile` to hold per-MsgType defaults** (research R-19): it is a statically asserted 4-byte struct carried by value, which every `reify` caller and `negotiated_version_profile()` copy. **REJECTED — a `reify` overload taking a map** (research R-19): it leaves C with nothing to pass.

**`FIXT11.xml` is no longer verbatim** (owner item 8a, research R-19, R-17). A stated exception in `dictionaries/README.md`, whose refresh recipe re-applies it: the group on Logon and its three missing fields, nothing else. **REJECTED — adopting QuickFIX/J's whole file:** a larger diff in the generated files, and it trips the same pins.

**Records this supersedes, flagged here and not edited.**

- ⚠️ **Superseded by 099:** `specs/003-dictionary-codegen/contracts/reify.hpp`'s resolution order, and `specs/003-dictionary-codegen/contracts/version_registry.hpp` where it has no `transport()`. `include/fixpp/dict/reify.hpp` and `include/fixpp/dict/version_registry.hpp` name 099 in their header comments.
- `.specify/2c-codegen.md` carries in-place `Updated (099-fixt-appl-version-validation)` notes at §4.3, §4.6, §4.9 and §5; `.specify/2b-wire.md` at §6.5. The text before each note is history.
- ⚠️ **Retired by 099:** L-041-2 and L-097-1, in the closed B&L file. L-041-2's accept-only divergence stays live as `B-081-1`. `L-098-1` stays open, reworded.

## Group registration in a context (100) — what this family gained, and what was rejected

`100-wire-group-read-integrity` (research D2, the one statement of the predicate) makes a group read
answer by whether the dictionary registers the count tag in the read's context. Verify against
`include/fixpp/dict/table_view.hpp` (`group_registered`, `wire_group_member_tags`,
`wire_group_first_field`), `src/dictionary/dictionary.cpp` (`build_table_view`) and both loaders'
`collect_section_groups`.

**DECIDED — one context-exact predicate, asked before anything else.** `table_view::group_registered`
holds for `(msg_type, parent_path, no_tag)` when any of three arms holds: the context arm (a
`group_ctx_` key, or a key of the arm-1 store, which `build_table_view` fills from the loaders'
per-occurrence delimiter records so that a count tag reached at two parent paths is keyed at both);
the section arm (a section record for `(parent_path, no_tag)`, from the loaders' header and trailer
walks, whatever the message type); the bare-only arm (no context key anywhere, and `group_bit` set).
The wire reaches it through `dict_hooks`' registration callback. The count's value never decides it.

**DECIDED — per-occurrence records.** Each arm-1 key and each section key carries its occurrence's
delimiter and direct-member set, and the wire's membership and delimiter lambdas read `group_ctx_`,
then the arm-1 record, then the section record, then the bare store. So a group whose occurrence the
dictionary defines differently from its bare store's definition reads at its own definition
(`B-100-4`). The records are written while the table is built and are read-only after.

**REJECTED** (research D2 "Alternatives"): the delimiter oracle with its bare fallback (context-blind,
so 802 at the root becomes a phantom group); `group_first_field_exact` alone, or exact registration
with a fallback only when a table has no contextual records (makes 099's NoHops not a group on route
tables); deciding by the value 0 (a scalar `0` becomes a phantom group, and the count parser reads
`""`, `abc` and `-1` as 0). **REJECTED at Gate A round 2** (plan `### Round 2 — disagreements`):
folding registration into `group_delim_fn`'s result to keep `sizeof(dict_hooks)` unchanged, because
shipped fixtures pass a null delimiter callback with a membership callback and would flip.

**Not fixed: custom dictionaries that define one count tag differently at one key** (`L-100-1`,
fixpp#665; the validator's bare fallback is fixpp#666). The census scripts of research D2's recipe
re-derive that shipped dictionaries reach none of them.
