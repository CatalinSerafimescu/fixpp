# Contract: Repeating-Group Detection

**Feature**: `082-structural-group-detection` | **Date**: 2026-07-29

A **behavioral** contract over existing accessors — it introduces no new interface. It defines the
single predicate that answers "is tag *T* a repeating-group count tag in dictionary *D*", and the
observable consequences consumers may rely on.

---

## C1 — The predicate

> Tag **T** is a repeating-group count tag in dictionary **D** **iff D declares a group whose
> count tag is T** — i.e. `<group name=N>` with `N → T` in the `<fix>` schema, or
> `<fixr:group><fixr:numInGroup id="T">` in the Orchestra schema.

**Explicitly NOT part of the predicate**: the declared datatype of field T. A tag typed
`NUMINGROUP` that no `<group>` declares is **not** a group; a tag typed `INT` that a `<group>`
declares **is** one.

### C1.1 — Runtime realization

`Dictionary::group_first_field(T) != 0`, applied per field of a message's own field run.

- Returns the delimiter tag for a declared group, `0` otherwise (`dictionary.cpp:92`).
- Already public (`dictionary.hpp:111`) — **this contract adds no API**.

### C1.2 — Codegen realization

Membership in `VersionIR::group_tags`, the union of `{e.no_tag : e ∈ m.group_order}` over all
messages of the version.

### C1.3 — Required properties of any realization

| ID | Property |
|---|---|
| **P1** | **Member-independent** — a `<group>` with zero members still satisfies the predicate. Derivations from a group's members (e.g. `{FieldRef::group_no_tag}`) are non-conforming: a group's own count field carries its *parent's* tag, never its own. |
| **P2** | **Per-dictionary** — evaluated against one loaded dictionary. No global tag-keyed group set. |
| **P3** | **Deterministic** — same input document ⇒ same set, same order. Emission order must remain stable or the goldens' determinism test fails. |
| **P4** | **Single-sourced** — exactly one predicate in the codebase per tier. Neither a union with the datatype test nor a per-version/per-dictionary special case is conforming. |
| **P5** | **Reachability-preserving** — a group registers under the messages whose field run contains its count tag, exactly as before. Global enumeration of the group table is non-conforming (it would add component-only groups and break C3). |

## C2 — Ground truth per dictionary

Authoritative output of `contracts/predicate_census.py` (raw XML; loads neither `Dictionary` nor
the codegen IR).

**Registration is reachability-restricted.** Both `as_table_view()` loops filter over a *message's
own field run*, so a group registers only if it is transitively reachable from a `<message>` —
including via `<header>`/`<trailer>`, which `xml_loader.cpp:926-931` expands into **every**
message's run. Registered-count columns below are therefore `set ∩ reachable`, **measured** by the
oracle, not inferred from set cardinality:

- **registered today** = `type ∩ struct ∩ reachable` (nominated by datatype, a real `<group>`, and reachable)
- **registered after** = `struct ∩ reachable`

| dictionary | type set | **struct set (normative)** | registered today | **registered after** | delta |
|---|---:|---:|---:|---:|---|
| FIX40 | 0 | 4 | 0 | **4** | +4 |
| FIX41 | 0 | 7 | 0 | **7** | +7 |
| FIX42 | 0 | 18 | 0 | **18** | +18 |
| FIX43 | 34 | 34 | 33 | **34** | **+576** |
| FIX44 | 59 | 59 | 59 | **59** | — |
| FIX50 | 69 | 69 | 67 | **67** | — |
| FIX50SP1 | 99 | 99 | 97 | **97** | — |
| FIX50SP2 | 507 | 507 | 505 | **505** | — |
| FIXT11 | 1 | 1 | 1 | **1** | — |
| Orchestra FIX Latest | 524 | 524 | 524 | **524** | — |

**Why FIX50/SP1/SP2 register 2 fewer than they declare** (a cross-check on this model, not an
anomaly): those dictionaries ship an **empty `<header/>`** — the FIXT.1.1 session layer owns the
standard header (feature 081 / L-041-2) — so `NoHops(627)` is unreachable there, and
`NoMsgTypes(384)` belongs to `Logon`, which lives in FIXT11 rather than the application
dictionary. Both are unreachable **before and after**, so C3 is unaffected.

FIX42's 18: `33, 73, 78, 124, 136, 146, 199, 215, 267, 268, 295, 296, 382, 384, 386, 398, 420, 428`.

**FIX43 is the discriminating row.** Its two sets have equal cardinality but differ in membership:

| tag | name | declared type | is a `<group>`? | today | after | note |
|---|---|---|---|---|---|---|
| 82 | `NoRpts` | `NUMINGROUP` | **no** | unregistered | unregistered | already rejected downstream — **no-regression pin**, not a delta |
| 576 | `NoClearingInstructions` | `INT` | **yes** | unregistered | **registered** | the one effective delta |

This is why FIX43 is the discriminating dictionary: its two sets have **equal cardinality**, so a
count-only check passes while the membership is wrong in both directions. Only exact-set equality
catches it — the same reason C3 demands both directions rather than containment.

Cross-version corroboration that both are upstream typos: FIX44 types 82 `INT` and 576
`NUMINGROUP`, the opposite of FIX43 in each case, while declaring the same `<group>` for 576 and
the same plain field for 82.

## C3 — Non-regression

For FIX44, FIX50, FIX50SP1, FIX50SP2, FIXT11 and Orchestra FIX Latest, the registered group set
before and after must be **equal in both directions** — 0 additions, 0 removals. Containment is
not sufficient: a subset check passes while silently dropping a group.

## C4 — Observable consequences

1. **Read/parse (ungated).** For FIX40/41/42/43, a tag inside a newly-registered repeating group
   resolves **membership-bounded** instead of absent or positionally-wrong. This holds
   independently of `validate_inbound_messages` — `inbound_tv_` is built in `open()`
   (`session.cpp:992`) and consumed by `parse_and_dispatch_` (`session.cpp:328`) with no flag on
   the path.
2. **Validation (opt-in).** With `validate_inbound_messages` **on**, group-membership and 079
   per-group required-member enforcement become reachable for FIX40/41/42. New rejections must
   equal exactly the set derivable from the dictionary's `required='Y'` group members. With the
   flag **off**, no new rejection is reachable — read shape changes, acceptance does not.
3. **Codegen.** `v42` emits typed group accessors for its 18 groups (read + reify tiers) and the
   full 078 split builder/validator layout. No other version's emitted output changes.
4. **C-ABI.** No symbol, signature, or `FIXPP_C_ABI_VERSION` change. `fixpp_group_*` on a
   FIX40/41/42 session begins returning membership-bounded results where it returned
   `TYPE_MISMATCH`/absent — a behavior change behind an unchanged surface.

## C5 — Conformance checks

| # | Check |
|---|---|
| **K1** | For each of the 10 dictionaries, the registered group set is exact-set-equal to C2's struct column, both directions. |
| **K2** | C3 holds for the six unchanged dictionaries. |
| **K3** | FIX43 differs from baseline by exactly `{+576}`; 576 carries member `ClearingInstruction`; 82 is unregistered **and** still enforced as a plain required field in `ListStatus`. |
| **K4** | The two `table_view` stores agree on every newly-visible FIX42 group (no half-restructure). |
| **K5** | Regeneration diff: `v44`/`v50sp2`/`vt11`/`vlatest` read goldens and `v44`/`v50sp2`/`vlatest` builder goldens byte-identical; `v42` `Fields.hpp` + `Validator.hpp` byte-identical; `v42` `Messages.hpp` gains exactly 18 `class G_`. |
| **K6** | P4 — asserted **behaviorally**, not by token census: FIX43 tag 576 (`INT`-typed) registering is only possible if no datatype gate survives on the runtime path, and `v42` emitting 18 `class G_` is only possible if none survives on the codegen path. Plus: no version-name predicate remains in the codegen driver. |
| **K7** | The oracle backing K1–K3 derives from raw XML; mutating the production predicate cannot silence it. |
