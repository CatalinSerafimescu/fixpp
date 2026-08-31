---
type: Component Decision Map
title: C ABI — the legal isolation boundary, why GA is 1.5.0, and what the ABI gate does NOT check
description: The C ABI is the licence seam, not a convenience wrapper. Its first stable version is 1.5.0 for a reason, and its CI gate checks symbols, not layout.
status: stable
refs:
  - include/fix/c_api.h
  - include/fix/c_api/version.h
  - include/fix/c_api/handles.h
  - src/capi/fixpp_capi.map
  - .specify/2i-capi.md
  - tools/check_layers.py
  - .github/workflows/abi-golden.yml
refs_external:
  - research/G19-fix-fpml-iso20022/decisions/2i-capi.md
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

## ⭐ Why the first stable version is `1.5.0` and not `1.0.0`

This looks like a mistake and is not. **Do not "fix" it.**

The forward-compat error downgrade (see [`errors.md`](./errors.md)) is keyed on the ABI **MINOR**: a
code introduced at a later minor than the consumer published is downgraded to `UNKNOWN`. At the
`0 → 1` GA freeze the MINOR was **preserved at 5**, not reset to 0, because resetting it would place
the current version *below* the introducing-minor of codes that were already published — so a
conforming consumer would see **already-shipped codes downgraded to `UNKNOWN`**. An incoherent
baseline.

> The rule is stated in `version.h` and it generalises: **MINOR may reset at a BREAKING major, and only
> there.** At `2.0.0` the introducing-minor table is rebased and a 1.x consumer is already refused by
> the major check, so the reset costs nothing. At a *non-breaking* freeze it would silently break the
> downgrade frame.

The versioning contract itself — additive bumps MINOR, any break requires a MAJOR — is `[const §X.1]`.
⚠️ This is the **C-ABI surface** version, which moves independently of the C++ library version.

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
