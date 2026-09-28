---
type: Component Decision Map
title: Error taxonomy — one C++ enum, a deliberately coarser C ABI, and a version-keyed downgrade
description: Two tiers with different shapes on purpose. The C ABI is not a C enum, and an error newer than the consumer's ABI minor is downgraded rather than shown.
status: stable
refs:
  - include/fixpp/core/error.hpp
  - include/fix/c_api/error.h
  - src/capi/error.cpp
  - .specify/2k-log-otel.md
  - .specify/2i-capi.md
  - .specify/api-contract.md
  - .specify/2m-pybind.md
  - .specify/447-458-452-capi-refusals.md
  - specs/092-garbled-frame-reject/spec.md
  - specs/092-garbled-frame-reject/data-model.md
  - spec/behaviors-and-limitations.md
refs_external:
  - research/G19-fix-fpml-iso20022/decisions/2k-log-otel.md
  - research/G19-fix-fpml-iso20022/decisions/2i-capi.md
  - research/G19-fix-fpml-iso20022/decisions/2m-pybind.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/090-capi-refusals-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/092-garbled-frame-reject-gatea.md
  - research/G19-fix-fpml-iso20022/decisions/speckit/092-garbled-frame-reject-evidence.md
codegraph_entry: [error, expected_t, error_message, translate, translate_for_consumer, fixpp_strerror]
constitution: ["§X.4"]
---

# Error taxonomy

> ## ⚠️ The CODE is authoritative. This page is not.
>
> It records **why** the taxonomy has the shape it has and what that shape rules out. It does **not**
> tell you which variants exist — that list changes with every feature and is the first thing here that
> would rot. **Read the headers.**

## Where to look

| You want | Go to |
|---|---|
| The C++ error set | `include/fixpp/core/error.hpp` |
| The C ABI error set | `include/fix/c_api/error.h` |
| How one maps onto the other | `src/capi/error.cpp` — `translate`, `translate_for_consumer` |
| Why the enum is shaped this way | `.specify/2k-log-otel.md` — ⚠️ **yes, the logging/OTel doc owns the engine-wide error enum.** Nobody guesses that; the header states it |
| Why the C surface differs | `.specify/2i-capi.md` |

## The four rules that do not rot

**1. One engine-wide enum, `uint8_t`, slot 0 reserved.** Not per-module error types. Every subsystem
contributes variants to the same set, in its own slot range, and the header comments name the owning
feature for each range.

**2. Numbering is additive — nothing is ever renumbered.** `[const §X.4]` forwards-compat. This is the
constraint that explains the layout: ranges look sparse and arbitrarily ordered because they were
appended in feature order, not designed as a block. **Do not tidy them.**

**3. The C ABI is deliberately NOT a C enum.** `fixpp_error_t` is a `typedef int32_t` with `#define`d
codes — the header says why in one line: *storage size is ABI-stable*. A C enum's underlying type is
implementation-defined, so it cannot be an ABI contract.

**4. The C set is deliberately COARSER than the C++ set, and `translate()` is lossy on purpose.** The
C++ enum is fine-grained for engine code; the ABI publishes banded, stable codes. Adding a C++ variant
does **not** oblige a new C code.

## ⭐ The decision worth knowing: errors are downgraded per consumer ABI minor

`translate_for_consumer(code, consumer_minor)` returns `FIXPP_ERR_UNKNOWN` when the code was introduced
in a **later** ABI minor than the consumer published.

> **So an old consumer never receives a code it has no name for.** The alternative — hand it the new
> code and let it fall through its own `default:` — is what makes "unknown error" mean two different
> things at the same call site: *the engine does not know* versus *your headers are older than the
> engine*. Keeping them distinct is the whole point.
>
> The consequence to hold onto: **`FIXPP_ERR_UNKNOWN` from a C consumer is not necessarily an engine
> failure.** It may be a perfectly well-understood error that is simply newer than the caller. Check
> the caller's ABI minor before debugging the engine.

The consumer's minor is carried on the engine handle, so the downgrade needs an engine to exist. **Two
exclusions are deliberate, and both are stated in the code at the call site** — the pre-engine MAJOR
gate is not routed through it (there is no handle yet to read a minor from), and success is not routed
through it (`OK` is `OK` at every version). Those two boundaries are the shape of the rule; find them
with `grep -rn translate_for_consumer src/capi/` and read the comments, not this paragraph.

## ⚠️ `FIXPP_ERR_CAPI_CONFIG_INVALID` was documented with a producer set the code never had

**Was wrong, and CORRECTED by 090 (fixpp#488).** `.specify/2i-capi.md` tied the code to the
construction-time thunks: §6.5's row said *"Used only by `guarded_call_construction`"*, the
`k_strerror_table` string named *"engine_create / dict_load / msg_create_outbound"*, and §5.2 said
*"CI grep enforces"* the thunk split. The shipped tree already returned it from explicit refusals at
entry points outside that whitelist, such as the session-config setters. Two passages restated the
rule: `.specify/api-contract.md` §7.5 (*"… for engine creation"*) and `.specify/2m-pybind.md`'s
*Construction failure modes*.

090 rewrote all three as a **condition**: the code comes from an entry point that cannot complete,
through either an explicit refusal or a caught exception on a fallible step. It is not
construction-only. The *"CI grep enforces"* clause was **deleted**, not reworded. fixpp#488 tracks
the closure. Before citing any of these passages, check that your copy carries the condition-stated
wording.

> **Do not re-derive a producer list for this code from a design doc.** Read the sites:
> `git grep -n FIXPP_ERR_CAPI_CONFIG_INVALID src/capi/`. A list goes stale with the next refusal.
> The code is the lesson: a documented producer set nobody re-checked went wrong without anyone
> noticing.

## A range that is frozen by the compiler, not by a comment

At least one sub-range is pinned with a `static_assert` on **both** endpoints, so inserting or
appending a variant that shifts either end fails the build.

⭐ **That is the pattern worth copying.** A comment saying *"this set is frozen"* is a claim nobody
re-checks; a `static_assert` is the same claim, checked on every build. **Re-derive which ranges are
frozen:** `grep -n static_assert include/fixpp/core/error.hpp`.

## Rules 2 and 4 applied: the validator's field faults (092, fixpp#507)

092 FR-012 made `dictionary_driven_validator::validate` reject a malformed tag or a Length+Data
mismatch instead of ending its walk silently. It needed new C++ variants. How they were added
is the rules above, applied:

- **Appended, not slotted in.** The new variants form their own block at the next contiguous slots
  after the enum's previous last value, although they are wire errors and the older wire slots sit
  elsewhere. Nothing was renumbered (rule 2), and the frozen range's `static_assert` endpoints did
  not move. The boundary tests are written in terms of the new enumerator, not a
  literal. Read `error.hpp` for the values.
- **Coalesced in the C ABI, not mirrored.** `translate` maps both to the existing
  `FIXPP_ERR_WIRE_INVALID_FRAME` (rule 4). So no C code was added, no `include/fix/c_api/*.h` changed
  for them (so the freeze pin does not move; re-derive with `tools/check_capi_freeze.sh`), and the C
  ABI version did not move **for this change**. The validator is not reachable
  from C in any case.
- **Mapped to FIX codes in `reject_reason_map`**, to the same SessionRejectReason values the session
  sends for the same fault kinds.
- Rejected: **373 = 99 ("Other")** for a field fault. 99 is not valid on FIX.4.2. The session's
  decision and its other rejected alternatives are on
  [`inbound-message-path`](./inbound-message-path.md).

⚠️ **Do not confuse this with C-ABI 1.10.** 092 did bump the C ABI, BREAKING. The bump is for the
session's disposition of such frames, not for these error codes; see [`c-api`](./c-api.md). The
seqnum_max close (FR-019) reuses the existing `store_seqnum_overflow`, which `assign_outbound`
already returned for the outbound side.

Re-derive the error-enumerating oracles with 092 research R-7's commands. Every oracle that
enumerates the error set moves with the enum.

## What this page deliberately does NOT contain

No variant list, no counts, no slot numbers. They change with every feature that adds an error, and a
count reproduced here would be wrong within a release while still reading as authoritative.

**Re-derive the shape:**

```bash
grep -nE '^    // ── |^    // .* variants' include/fixpp/core/error.hpp   # ranges and their owners
grep -c '= [0-9]' include/fixpp/core/error.hpp                            # rough variant count
grep -n static_assert include/fixpp/core/error.hpp                        # what is frozen
```

## Related

- `expected_t<T>` is `std::expected<T, error>` — the return convention that keeps the hot paths free of
  exceptions. A function returning it is telling you it does not throw.
- [`security-profile.md`](./security-profile.md) — the other place where two namespaces hold
  same-named types with deliberately different member sets, for the same class of reason.
