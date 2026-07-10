# Contract: Generated Builder Output (FR-015a-lite)

**What the codegen writer-emitter's output MUST satisfy.** This EXTENDS the frozen 061 write shape-oracle (`specs/061-typed-app-messages/contracts/builder-shape-oracle.md`, C1–C6) — every 061 guarantee still holds for generated output; this contract adds the emitter-specific obligations (ordering, completeness, level-scoped validation). The 5 exemplar builders + `body_builder` remain the byte-for-byte reference.

## G1 — Inherits 061 C1–C4, C6 verbatim (C5 extended by G2/G6 below)

Every generated `build_<Msg>(out, args)` output satisfies 061 C1 (body-only framing, no `{8,9,34,49,52,56,10}`, `35=<MsgType>\x01` first, multi-char OK), C2 (canonical decimals + the ≥1-decimal byte-exact pin), C3 (repeating-group grammar: `No<G>=<N>` count-first, delimiter-first non-empty instances, nested depth), C4 (fail-closed atomicity: typed `wire_*` error + `out` untouched), C6 (no new public error/C-ABI/wire-semantics surface). 061 C5 (round-trip + external-golden agreement) is not inherited as-is but **superseded and widened** by this contract's G2 (shape-oracle byte-equality) + G6 (non-tautological round-trip witness), which extend the guarantee from the 5 exemplars to all 33 OFFICIAL messages. These are enforced by the single `wire::body_builder` core (FR-002) — the emitter adds no second serializer.

## G2 — Shape-oracle byte-equality (the headline pin, FR-003)

For each of the 5 exemplar MsgTypes **D, 8, 9, E, AS** in **v44**, driven by the **identical 061 seed** (`tests/session/exemplar_seeds.hpp`):

- `build_<Msg>(out, args)` output bytes **== the hand-written 061 exemplar body bytes** (direct byte compare), AND
- both **byte-match the QuickFIX golden** (`tests/session/golden/<msg>.fix`) via `shape_oracle_profile()` (excludes only `{8,9,10,34,52}`; every business tag incl. `TransactTime(60)` matched verbatim; decimals by value), AND
- the ≥1-decimal-per-exemplar direct byte compare (C2) holds on generated output.

This is the feature's reason to exist. If any exemplar fails, the emitter is wrong by definition.

## G3 — Canonical field order (INV-ORDER, FR-003/FR-008)

Generated output orders fields by the two-regime rule (research R1), independent of `<Msg>Args` member declaration or caller supply:

- **Top-level (`group_no_tag==0`): tag ascending** — served by the tag-sorted `m.fields` run. A group's `No<G>` count tag occupies its ascending-tag position; the group's instances immediately follow it.
- **Within a group entry: THIS group's DECLARATION member order** (from `MessageIR.group_order`, the codegen-local declaration-order XML walk — research R9), NOT tag order and **NOT the tag-sorted `m.fields` run**. Nested `No<sub>` sits at its member position; instances follow.

**Per-occurrence, NOT version-wide (RC#1); declaration-order source, NOT `m.fields` (RC#7):** the delimiter and member order for a group come from `MessageIR.group_order` — the group's DECLARED order in THIS message's own XML — NOT from the tag-sorted `m.fields` (which loses declaration order — `xml_loader.cpp:695-702`) and NOT from a version-wide `MemberMap` union. The same `no_tag` differs across messages — `NoMDEntries(268)` is delimiter-269 / `MDEntryType`-first in W but delimiter-279 / `MDUpdateAction`-first in X; because X tag-sorts 269 before 279, an `m.fields`-derived delimiter would wrongly pick 269 for X (the RC#7 defect). Pinned by G2 on the 5 exemplars (goldens encode the rule), by the **W/X paired byte-goldens** (G6, the shared-`no_tag` discriminator), and by G6 byte-structural asserts on the rest.

**Discriminating pins (RC#7):** the W/X paired goldens assert the generated X builder's `NoMDEntries(268)` delimiter is **`MDUpdateAction(279)`** (NOT 269), and the E exemplar golden asserts the `NoOrders` entry emits **`Symbol(55)` before `Side(54)`** (declaration order, NOT the tag-sorted `54 55`). Either would regress if the emitter sourced group order from the tag-sorted `m.fields` instead of `group_order`.

## G4 — Completeness: exact set over the 33 OFFICIAL MsgTypes (FR-004)

The generated builder set for v44 covers **exactly** these 33 distinct MsgTypes — no more, no fewer (exact-set equality, not subset-presence):

`D E F G H 8 9 q r AF AC t u` (A, 13) · `V W X Y c d e f g h i b S R AG Z a` (M, 17) · `J P AS` (P, 3).

The completeness gate asserts set-equality of the generated **MsgType→builder registry keys** (the MsgType wire strings, multi-char `AF/AC/AG/AS` included) against this literal 33-element expected set. It does NOT compare `build_<Identifier>` symbol names (`build_NewOrderSingle`) to MsgType strings (`D`) — those are different namespaces (identifiers via `to_identifier`; the MsgType string reaches only the `body_builder{"D"}` ctor). The emitter therefore emits a MsgType-keyed registry (the MsgType wire strings) so the gate keys on MsgType.

## G5 — Level-scoped required-presence validation (FR-006/FR-006a, INV-VALIDATE)

A generated `validate_<Msg>(args)` (SEPARATE from `build_`; `commit()` output is unaffected by it):

- returns `error::wire_required_field_missing` (pre-existing, =38 — no new value) when a required field is absent;
- checks the **top-level body** required set = `{group_no_tag==0 ∧ rule==Required ∧ tag ∉ {8,9,10,34,35,49,52,56} ∪ header_trailer_tags}` (excluded by **provenance** — the 8-tag set is a defensive floor; the primary exclusion is the FULL `<header>`/`<trailer>` tag set, recursively resolved through `<component>`/`<group>` refs — `VersionIR::header_trailer_tags`, data-model.md §2.1/§3), AND
- recursively checks **every present group entry** against its **per-occurrence** required set = `{f ∈ THIS message's fields : group_no_tag==<group> ∧ rule==Required}` — per (message, group occurrence), NOT a single version-wide table per `no_tag`. The same `no_tag` differs across messages: `NoMDEntries(268)` requires `MDEntryType(269)` in W but `MDUpdateAction(279)` in X; a version-wide table would over/under-reject;
- rejects a **required** group (non-optional span) with `size()==0` (`size() > 0`); an **optional** group (`std::optional<std::span>`) that is `nullopt` or engaged-empty is allowed;
- derives both required SETs from IR `FieldRef.rule` + `FieldRef.group_no_tag` off the message's own `m.fields` run as the **normal** source (order-independent — the tag-sort of that run does not affect set membership; NOT from the header-polluted, level-flattened `Validator.hpp` `<Msg>_rules`, NOT from the read emitter's version-wide `MemberMap`). A tag the mandatory /tasks census (plan.md N3) finds collapsed by `append_run` tag-dedup across ≥2 levels in one message is instead sourced from **the codegen-local declaration walk, extended to carry per-member `rule` (and a top-level declaration set)** — NOT `group_order`, which carries member ORDER not `rule`. Note the required SET (this contract) comes from `m.fields`; the group **delimiter + member order** used by `build_` (G3) come instead from `MessageIR.group_order` — the two are different axes (SET vs ORDER), only the latter needs the declaration-order walk (RC#7/R9);
- enum-value-domain and conditional-required are NOT checked (cut for v1.0).

## G6 — Non-tautological round-trip witness (FR-008)

For every OFFICIAL MsgType, a seed-driven witness:

- **build → framed → `dict::reify()`/typed flyweight → read-back**, asserting each seeded field (incl. nested group entries) at its exact input value; AND
- **byte-structural asserts** on the built body: field order matches the emitter's canonical order (G3), correct SOH count, NO framing tags — so a builder+parser co-wrong cannot stay green;
- exemplars additionally anchored by their QuickFIX golden (G2);
- **insurance (research R5)**: the **W (35=W) and X (35=X) paired byte-goldens** — REQUIRED, the shared-`NoMDEntries(268)` per-occurrence discriminator (269-first vs 279-first) that pins RC#1; plus ≥1 further grouped message (MassQuote 35=i, optionally AllocationInstruction 35=J) byte-anchored by a newly-generated QuickFIX golden.

## G7 — No collateral surface change (FR-009)

Read path, wire semantics, codegen READ layout (`Messages.hpp`/`Fields.hpp`/`Reify.hpp`/`Validator.hpp`), C-ABI, and Python bindings are byte-identical/unchanged. The determinism golden for the existing read emitters stays green after any shared-helper extraction. C-ABI 1.5.0 freeze byte-identical; `error_codes_v1.txt`/abidiff/occupancy gates unchanged.

## G9 — Generated-wrapper fail-closed test home (RC#6)

The 061 fail-closed seam (`tests/session/test_exemplar_build_failclosed.cpp`, C4) pins the HAND builders; the generated path adds NEW conversion surfaces that need direct pins. A `tests/session/test_067_builder_failclosed.cpp` MUST carry discriminating witnesses for:

- **undersized `out` untouched** — `build_*` into a too-small span returns a typed error and leaves `out` byte-unchanged (INV-4);
- **control-byte/SOH in a value** — a `string_view` field containing `0x01` is rejected before any byte reaches `out`;
- **Bool → `Y`/`N`** — a set Boolean (e.g. `LocateReqd(114)`) serializes `114=Y`/`114=N`, never `114=1` (FR-007a);
- **Length+Data coupling** — clean-text Data auto-derives its Length; both emitted, coupled (FR-007a);
- **required-group-zero rejected** — a required group with `size()==0` fails `validate_*` with `wire_required_field_missing`; an optional group `nullopt` omits `No<G>` (RC#2);
- **W-vs-X per-occurrence delimiter discrimination** — the generated W and X builders emit distinct `NoMDEntries` delimiter/order from the same `no_tag` (RC#1); a version-wide plan would make one wrong.

## G8 — Generated-code hygiene

`Builders.hpp` carries the standard generated banner (`// GENERATED by fixpp-codegen … DO NOT EDIT.` + `#pragma once`), lands only in the build tree (`build/<preset>/_codegen/include/fixpp/v44/`), never the source tree; forced regeneration is `git`-clean (codegen build-graph cleanliness gate); `write_file` empty-skip permits incremental TDD (emitter inactive ⇒ no file ⇒ tests RED, not stale-file green).
