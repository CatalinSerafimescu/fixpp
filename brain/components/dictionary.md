---
type: Component Decision Map
title: dictionary — two loaders, a version registry, and a catalogue row that disagrees with the tree
description: A substantial shipped subsystem whose catalogue status under-reports it. D-007 says backlog; xml_loader.cpp is over a thousand lines.
status: stable
refs:
  - src/dictionary/xml_loader.cpp
  - src/dictionary/orchestra_loader.cpp
  - src/dictionary/version_registry.cpp
  - .specify/2c-codegen.md
  - .specify/215-dictionary-view.md
refs_external:
  - research/G19-fix-fpml-iso20022/decisions/2c-codegen.md
codegraph_entry: [Dictionary, xml_loader, orchestra_loader, field_traits, version_registry]
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

## Where the design decisions live

`2c-codegen.md` is **v1.4 post-sign-off** and was scanned clean in the Step-R sweep. ⚠️ Beside it sits
`2c-codegen.draft-r1.md` — an archived **v0.1** holding a design that an adversarial review **rejected**
as needing a full rewrite. It now carries a forward-pointing banner; before 2026-08-29 it did not.
