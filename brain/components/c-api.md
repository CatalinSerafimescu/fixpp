---
type: Component Decision Map
title: C ABI — the legal isolation boundary, why the GA-freeze baseline is 1.5.0, and what the ABI gate does NOT check
description: The C ABI is the licence seam, not a convenience wrapper. Its GA-freeze baseline is 1.5.0 for a reason (the first stable version is the 1.0.0 of the first public release), and its CI gate checks symbols, not layout.
status: stable
refs:
  - include/fix/c_api.h
  - include/fix/c_api/version.h
  - include/fix/c_api/handles.h
  - src/capi/fixpp_capi.map
  - src/capi/message_write.cpp
  - src/capi/config.cpp
  - .specify/2i-capi.md
  - .specify/447-458-452-capi-refusals.md
  - .specify/495-493-486-dict-reify-copy.md
  - specs/090-capi-refusals/spec.md
  - spec/behaviors-and-limitations.md
  - tools/check_layers.py
  - .github/workflows/abi-golden.yml
refs_external:
  - research/G19-fix-fpml-iso20022/decisions/2i-capi.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/090-capi-refusals-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/090-capi-refusals-implement-log.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/090-capi-refusals-verify.md
codegraph_entry: [fixpp_engine_t, fixpp_session_t, fixpp_msg_t, fixpp_strerror]
constitution: ["§V.1", "§IV.2", "§X.1", "§X.4"]
---

# C ABI

> ## ⚠️ The CODE is authoritative. This page is not.
>
> It records **why** the C surface has the shape it has, and one thing its CI gate does **not** prove.
> The function list, the version numbers and the handle catalogue all live in the headers.

## It is the licence boundary before it is anything else

`[const §V.1]` / `[const §IV.2]`: the C ABI is **the unique linkage seam between the AGPL engine and a
non-AGPL consumer**. That is why it exists — not to be friendlier than C++.

Everything else follows from it, and this is the part to internalise before changing anything here:

- **`src/capi/fixpp_capi.map`** exports exactly `fixpp_*` and makes everything else `local:`. The
  boundary is enforced by the linker, not by convention.
- **Handles are opaque** — `typedef struct fixpp_engine fixpp_engine_t;` and friends are *incomplete
  types*. No consumer can see a layout, so no consumer can depend on one.
- **`tools/check_layers.py`** enforces that the Python bindings and the C examples may include
  **`capi` only** — never the C++ umbrella. A binding that reaches past the seam would defeat it.

## ⭐ Why the GA-freeze baseline is `1.5.0` and not `1.0.0`

This looks like a mistake and is not. **Do not "fix" it.**

The forward-compat error downgrade (see [`errors.md`](./errors.md)) is keyed on the ABI **MINOR**: a
code introduced at a later minor than the consumer published is downgraded to `UNKNOWN`. At the
`0 → 1` GA freeze the MINOR was **preserved at 5**, not reset to 0, because resetting it would place
the current version *below* the introducing-minor of codes that were already published — so a
conforming consumer would see **already-shipped codes downgraded to `UNKNOWN`**. An incoherent
baseline.

> The rule is stated in `version.h` and it generalises: **MINOR may reset only where no conforming
> consumer can hold an older minor** — at a BREAKING major, or at the first public release
> (`[const §X.7]`, constitution v2.0). At `2.0.0` the introducing-minor table is rebased and a 1.x
> consumer is already refused by the major check; at the first release no consumer predates it. In
> both cases the reset costs nothing. At a *non-breaking* freeze it would silently break the downgrade
> frame.

The versioning contract is `[const §X.7]`: until the first public release a break bumps MINOR and is
declared **BREAKING**; at that release the version resets to `1.0.0`; after it, any break requires a
MAJOR. (`[const §X.1]` is the review requirement.)
⚠️ This is the **C-ABI surface** version, which moves independently of the C++ library version.

## C-ABI 1.6: the first breaking change before the first release (#428)

`fixpp_msg_set_string`, `fixpp_entry_set_string`, `fixpp_msg_commit` and
`fixpp_msg_create_outbound` refuse call sequences that used to succeed. That makes it a
BREAKING change, shipped as MINOR 1.6 under constitution Article X §7 (v2.0).

What was rejected:
- **A MINOR "conformance fix" ruling.** Every refused call produced malformed FIX, but the
  owner ruled on 2026-09-15 that a call that used to succeed and now fails is breaking.
- **Pair logic in `fixpp_msg_set_bytes`.** It stays type-agnostic; commit refuses what it can
  mis-build.
- **A pair setter that moves entries.** Open group builders hold indices into those vectors.

Decided in Gate B, after the first pass over this page:
- **A derived Length half that is a framing tag is refused (H-1).** The Data setters derive the
  Length tag from the pair table. A dictionary pairs any LENGTH field with the DATA field declared
  after it, so it can pair BeginString(8). The setters refuse that with
  `FIXPP_ERR_MSG_FRAMING_TAG_FORBIDDEN`, the code the direct setters already return for a framing tag,
  rather than write a second `8=`.
- **The "a Data field cannot delimit a group" rule uses the group's exact context (G-3).** A group tag
  reused in another MsgType, or under another parent, can open with a different field, so the
  dictionary-wide first-seen delimiter refused pairs that are well-formed in that context. When the
  context has no entry, the setter defers to commit, which fails closed. Rejected: guessing from the
  first-seen delimiter.

## C-ABI 1.7: three refusals, declared BREAKING (090, fixpp#447 / #458 / #452)

Design authority: `.specify/447-458-452-capi-refusals.md`. ⚠️ **Its Gate A did NOT converge** — the
feature carries `gate-a-waived`; read the gate record before treating a decision below as reviewed.
What each refusal is, per symbol, is in `include/fix/c_api/message.h` / `session.h` and the B&L rows
`B-447-1`, `B-458-1`, `B-452-1`. What follows is the half the headers do not state.

- **`fixpp_msg_remove_tag` refuses while ANY group builder is open** (`src/capi/message_write.cpp`).
  Rejected: **re-indexing the open builders after the erase** (the issue's Option B). It cannot
  repair the case where the erased entry *is* the open group — there is no correct index to
  renumber to — so it would have to refuse there anyway, giving one call two behaviours. Also
  rejected: a narrower guard keyed on the tag or the erase position. It over-refuses less, but its
  outcome depends on accumulator layout the C ABI does not expose. The code is
  `FIXPP_ERR_INVALID_HANDLE` because `fixpp_msg_commit` already answers "a builder is open" with it.
  ⚠️ The guard knowingly refuses two calls that were safe (an absent tag; a tag after every open
  group). The design note's v0.1 claim that no correct program loses anything was false and was
  withdrawn.
- **Every group/instance index dereference reachable from the C ABI is a defined refusal.** This is
  defence in depth after the guard above, and it is **not** a BREAKING change on its own.
- **`fixpp_msg_clone` refuses a failed dict-backed re-parse** instead of returning a silently
  dict-free clone. Rejected: **minting a new error code.** It returns `translate(parsed.error())`,
  whose cost is nothing, where a new enumerator re-baselines the freeze and the ABI history.
  Rejected too: `FIXPP_ERR_DICT_OOM` for the out-of-memory route, whose name scopes it to dictionary
  load. The exception boundary is **nested**: an inner `bad_alloc` gives `FIXPP_ERR_CAPI_CONFIG_INVALID`
  and an outer `catch (...)` gives a fatal log and `abort`. ⭐ **Clone stays a steady-state symbol.**
  `[2i §5.2]`'s construction-time whitelist was **not** amended. The defect was the old blanket
  catch's *width*, not its code.
- **`fixpp_session_config_set_comp_ids` / `_set_begin_string` refuse a byte `< 0x20` or `'='`**
  (`src/capi/config.cpp`). They use the same predicate as `Session::open`; see
  [`session.md`](./session.md) for why it is one function and why it is policy, not grammar.

All three changed a call that used to succeed, so under `[const §X.7]` they are BREAKING, as with
1.6. The C++ track (the reify factory, `Session::open`) moves with them but is not ABI.

⚠️ **Superseded in part (fixpp#495/#493):** `fixpp_msg_clone` of a `Session`-dispatched view (and of
a clone) now SHARES the source's membership table instead of copying it, and re-parses under the
source's `OffsetTable::Config`, so the raised-cap refusal described above no longer occurs for a
source whose own build succeeded (`L-458-2` resolved). See `.specify/495-493-486-dict-reify-copy.md`.

## C-ABI 1.8: the dictionary loader stops using the host's default resource (fixpp#495 D-5)

`fixpp_dict_load_from_xml` (`src/capi/dictionary.cpp`) allocates the Dictionary from
`std::pmr::new_delete_resource()`, not the default resource installed at load. Why: a C host may
replace, destroy or never make thread-safe the default resource while a dictionary it loaded is still
alive — held for the process lifetime by a retained session shell, or destroyed on another thread.
BREAKING under `[const §X.7]` because `dict.h` documents the resource and a host with a counting
default resource observes the difference (`B-495-3`). Rejected: leaving it and documenting the hazard;
it was closed together with D-4 (owner ruling R-D). The library version is not bumped (R-F).

## ⚠️ What the ABI gate actually checks — and what it does not

`abi-golden.yml` runs on every PR and is an **`nm` symbol-set check**: a new or removed exported
`fixpp_*` function fails it. **It does not diff layout or types.**

It was an `abidiff` gate, and the reason it is not any more is worth carrying, because it is this
repo's signature failure mode:

> The release archive is built **without debug info**, so `abidiff` had nothing to compare and *"only
> ever reported the symbol set"* — the same check, dressed as a stronger one. Separately, the pinned
> libabigail asserts and core-dumps on debug-info-less archives. So the gate was replaced by the check
> it had silently degraded into, **honestly labelled**. The `abidiff` suppression/golden/baseline files
> are retained as the hook for the real thing.

⭐ **The consequence to hold onto: a green ABI gate proves the symbol set is unchanged. It does not
prove ABI compatibility.** Changing a struct passed by value, or a parameter's meaning, passes it.
Layout/type diffing is a deliberate, recorded gap — not an oversight, and not something to assume is
covered.

## Re-derive rather than trust this page

```bash
grep -n 'typedef struct' include/fix/c_api/handles.h        # the opaque handle catalogue
sed -n '25,60p' include/fix/c_api/version.h                  # the version contract, in its own words
cat src/capi/fixpp_capi.map                                  # what is actually exported
python3 tools/check_layers.py                                # the layering rule, executable
```

## Related

- [`errors.md`](./errors.md) — the error taxonomy and the MINOR-keyed downgrade this page's version
  decision exists to protect.
- [`plugin-factory-ownership.md`](./plugin-factory-ownership.md) — the C++ config surface behind the
  seam.
