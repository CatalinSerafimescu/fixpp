---
type: History Door
title: History — on demand, not on the path
description: Superseded claims and where the project's own archives live. Deliberately not linked from the routing table.
status: stable
---

# History — ask for it, do not trip over it

Nothing here describes current behaviour.

| Archive | Holds |
|---|---|
| `spec/behaviors-and-limitations-closed.md` | B-\*/L-\* rows whose limitation no longer exists. IDs stay citable. Cut rule: only **self-declared-resolved** entries move; `wontfix` / `deferred` stay live |
| `CLAUDE-history.md` | Merged-feature changelog, newest first |
| `../remaining-work/HISTORY.md` (parent) | Delivered work moved out of the v1.0 tracker |
| `MEMORY-archive.md` (agent memory) | Older close-outs |

Superseded **claims** — documents still in the tree that describe a design that no longer ships — are
not archived, because they are still cited. They are flagged in place on the component page that owns
them. Start there, not here.

**Rule when a claim is superseded:** never edit an accepted claim into a new one. Flag it, say what
replaced it, and leave it reachable. Editing destroys the record of what was believed when the code
was written — and this repo has spent whole Gate B rounds watching a fix replace one false claim with
a new false claim, converging only by deletion.
