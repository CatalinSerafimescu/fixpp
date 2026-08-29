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

**This needs per-row adjudication against source, not a bulk verdict.** See the sibling note on `nfr`
in [`nfr-and-tooling`](./nfr-and-tooling.md), where the same status column is unreliable for a
different and possibly legitimate reason.

## Where the design decisions live

`2c-codegen.md` is **v1.4 post-sign-off** and was scanned clean in the Step-R sweep. ⚠️ Beside it sits
`2c-codegen.draft-r1.md` — an archived **v0.1** holding a design that an adversarial review **rejected**
as needing a full rewrite. It now carries a forward-pointing banner; before 2026-08-29 it did not.
