---
type: Component Decision Map
title: dictionary — two loaders, a version registry, and a catalogue row that disagrees with the tree
description: A substantial shipped subsystem whose catalogue status under-reports it. D-007 says backlog; xml_loader.cpp is over a thousand lines.
status: stable
refs:
  - src/dictionary/xml_loader.cpp
  - src/dictionary/orchestra_loader.cpp
  - src/dictionary/version_registry.cpp
  - src/dictionary/reify.cpp
  - include/fixpp/dict/reify.hpp
  - .specify/2c-codegen.md
  - .specify/215-dictionary-view.md
  - .specify/456-table-view-seal.md
  - .specify/447-458-452-capi-refusals.md
  - spec/behaviors-and-limitations.md
refs_external:
  - research/G19-fix-fpml-iso20022/decisions/2c-codegen.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/090-capi-refusals-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/090-capi-refusals-implement-log.md
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
coexistence, dialect overlay binding) and `215-dictionary-view.md`.

⚠️ **Counts and file lists rot.** Derive the current surface from the graph index; the point above is
the *shape* — two loader front-ends converging on one dictionary representation, with codegen on top.

## ⚠️ The catalogue under-reports this family — a LEAD, not a verdict

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
- ⚠️ **Open residuals:** `L-458-2` (fixpp#493). The re-parse uses the DEFAULT offset-entry cap, so a
  source accepted under a raised cap cannot be cloned or reified. `L-458-1` covers a dict-backed
  build that under-indexes near the probe cap while reporting success. Check the live B&L file
  before treating either as open.

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

`2c-codegen.md` is **v1.4 post-sign-off** and was scanned clean in the Step-R sweep. ⚠️ Beside it sits
`2c-codegen.draft-r1.md` — an archived **v0.1** holding a design that an adversarial review **rejected**
as needing a full rewrite. It now carries a forward-pointing banner; before 2026-08-29 it did not.
