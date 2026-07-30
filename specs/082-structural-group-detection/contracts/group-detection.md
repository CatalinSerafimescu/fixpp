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

- Returns the delimiter tag for a declared group, `0` otherwise (`dictionary.cpp:92-99`).
- Already public (`dictionary.hpp:111`) — **this contract adds no API**.
- **Already the runtime tier's predicate in production**, on the C-ABI outbound write path:
  `src/capi/message_write.cpp:157` (`is_group_collision`), `:719` (delimiter resolve), `:812` and
  `:923` (`fixpp_msg_group_begin` / nested entry, `== 0 ⇒ FIXPP_ERR_TYPE_MISMATCH`). This
  realization therefore **converges** the tier onto one predicate (P4) rather than adding a second.
- **Sentinel ambiguity — ambiguous in isolation, but its input is UNREACHABLE (FR-023).** The
  accessor returns `0` both for "not a declared group" and for "declared group whose
  `first_field_tag` is 0", i.e. a member-less `<group>` (`xml_loader.cpp:610`/`:644` stores
  `first_field_tag = 0` for one). Read in isolation that is genuinely ambiguous, and it stays so —
  082 does not change the accessor. What 082 changes is the **input**: FR-023 makes a member-less
  `<group>` a **load error** in both loaders, so no `Dictionary` the loaders admit can carry the
  ambiguous state. Over the set of dictionaries admitted, `group_first_field(T) != 0` is therefore
  **exactly** the predicate of C1 — no caveat. See P1-NON and K11.

### C1.2 — Codegen realization

Membership in `VersionIR::group_tags`, the union of `{e.no_tag : e ∈ m.group_order}` over all
messages of the version.

### C1.3 — Required properties of any realization

| ID | Property |
|---|---|
| **P1** | **Not derived from a message's own field-run membership.** Derivations from a group's members — `{FieldRef::group_no_tag : != 0}` over `all_fields`, or a `members.empty()` test — are **non-conforming**, because they conflate "**D** declares a group with count tag T" with "T has members **in this message**": a group's own count field carries its *parent's* tag, never its own, and a tag reused as a plain scalar answers wrongly. This is what the predicate owes. **Scope: P1 binds the *detection predicate*.** `dictionary.cpp:463`'s post-detection `if (members.empty()) continue;` registration guard runs *downstream* of the detection filter at `:446`, is unchanged by 082, and is **outside P1's scope** — see P1-NON. (Same scoping as FR-001's absolute, which likewise binds detection sites only.) |
| **P1-NON** | **Zero-member `<group>` visibility is RETIRED AT THE LOADER, not documented around (OD-1 resolved 2026-07-30 — FR-023).** Earlier drafts stated P1 as "member-independent — a `<group>` with zero members still satisfies the predicate". That is unachievable at the runtime tier and stays withdrawn, and the reason is **representational**: `table_view` cannot represent a delimiter-less group (`set_group_first(t, 0)` at `table_view.hpp:570-575` would set the group bit **and** insert member tag **0**, a malformed registration the parser and validator would consume), and the 063 context store's `if (members.empty()) continue;` (`dictionary.cpp:463`) defeats visibility under *any* predicate. **That reason is now the ground on which rejection is right, rather than the ground on which a limitation is tolerable:** a declaration with no usable downstream representation is a malformed dictionary, so FR-023 makes it a **load error** in both loaders — mirroring `xml_loader.cpp:584`'s sibling rejection and closing `:1017`'s `first_field_tag != 0` carve-out. **Consequence:** the zero-member state is **unreachable by construction**, so C1.1's realization is exactly C1's predicate with **no caveat**. **Do not overstate it:** the sentinel is still ambiguous *when read in isolation* (`dictionary.cpp:92-99` is unchanged); what changed is that the ambiguous **input** can no longer reach it. The codegen realization (C1.2) would still *see* a zero-member group (`walk_level` appends unconditionally) — moot post-FR-023, since no such dictionary loads, and still not to be restated as a feature-level guarantee. No vendored dictionary declares a member-less `<group>` (K7/S0), so **zero** shipped dictionaries are affected by the rejection. Conformance: **K11**. |
| **P2** | **Per-dictionary** — evaluated against one loaded dictionary. No global tag-keyed group set. |
| **P3** | **Deterministic** — same input document ⇒ same set, same order. Emission order must remain stable or the goldens' determinism test fails. |
| **P4** | **Single-sourced, and the source is NAMED.** Exactly one predicate per tier: the **runtime** tier's is `Dictionary::group_first_field(t) != 0`, shared by `as_table_view()`'s two registration loops **and** the four C-ABI write sites listed in C1.1; the **codegen** tier's is membership in `VersionIR::group_tags`. Neither a union with the datatype test, nor a per-version/per-dictionary special case, nor a *second structural realization inside one tier* (e.g. `Dictionary::group(t).has_value()` in `as_table_view()` while `message_write.cpp` keeps `group_first_field`) is conforming — the last is the half-restructure FR-004 exists to prevent. |
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
| FIX43 | 34 | 34 | 33 | **34** | **+1 tag (576)** |
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
3. **Codegen.** `v42` emits typed group accessors for its 18 group **tags** (read + reify tiers) and
   the full 078 split builder/validator layout — **28** shared plan headers over the **17** tags
   that reach the `is_application`-gated builder tier (`384 NoMsgTypes`'s only host is the admin
   message `Logon`), 226 files under `--families all`. No other version's emitted output changes.
4. **C-ABI.** No symbol, signature, or `FIXPP_C_ABI_VERSION` change. The behavior change behind that
   unchanged surface is **asymmetric**, and the two legs must not be conflated:
   - **Read family — CHANGES.** `fixpp_msg_get_group` and friends resolve through
     `OffsetTable::group_slices` and therefore through `inbound_tv_`, so on a FIX40/41/42 session
     they begin returning **membership-bounded** results where they returned `TYPE_MISMATCH`/absent.
     This is the leg US1 AC3, SC-008 and SC-008a bind.
   - **Write family — ALREADY WORKS.** `fixpp_msg_group_begin(268)` on a FIX 4.2 dictionary
     **succeeds today**, because its gate is `group_first_field(268) == 0` (`message_write.cpp:812`)
     and 268 *is* a declared `<group>`. 082 does not change the write family's behaviour at all; it
     is cited here as the corroboration that the chosen predicate is already in production (C1.1,
     P4). Any claim that "`fixpp_group_*` begins returning membership-bounded results" applies to
     the read family only.

## C5 — Conformance checks

| # | Check |
|---|---|
| **K1** | For each of the 10 dictionaries, the **bare** store's registered group set is exact-set-equal to C2's **registered after** column (reachability-restricted — *not* the struct column, which would fail on FIX50/SP1/SP2 where 69/99/507 are declared but 67/97/505 registered), both directions. |
| **K2** | C3 holds for the six unchanged dictionaries. |
| **K3** | FIX43 differs from baseline by exactly `+1 tag (576)`; 576 carries member `ClearingInstruction`; 82 is unregistered **and** still enforced as a plain required field in `ListStatus`. |
| **K4** | **Per-context member-set equality, not a tag-set projection.** For a **divergent-signature** tag — `NoRelatedSym(146)`, 4 distinct direct-member lists across its 6 FIX42 occurrences — the 063 context store holds the *distinct* member set for each `(msg_type, parent path, no_tag)`, equal to the oracle's per-context set; and the bare store holds the loader's first-seen set. The two stores are keyed differently, so "the stores agree" can only mean a tag-set projection, which passes while every per-context member set is wrong. `LinesOfText(33)` is **not** a valid subject here: its two occurrences carry identical members `{58, 354, 355}`, so a collapse on it is unobservable. |
| **K5** | Regeneration diff: `v44`/`v50sp2`/`vt11`/`vlatest` read goldens and `v44`/`v50sp2`/`vlatest` builder golden sets byte-identical; `v42` `Fields.hpp` + `Validator.hpp` byte-identical; `v42` `Messages.hpp` keeps 46 message classes and gains exactly **18** `class G_`. (`v42` has no `Manifest.txt` — see spec FR-016.) |
| **K6** | P4 — asserted **behaviorally**, not by token census: FIX43 tag 576 (`INT`-typed) registering is only possible if no datatype gate survives on the runtime path, and `v42` emitting 18 `class G_` is only possible if none survives on the codegen path. Plus: no version-name predicate remains in the codegen driver. |
| **K6b** | P4's *named-source* leg, which K6 does not reach: the predicate used in `as_table_view()` is the **same accessor** the C-ABI write path uses. Asserted by a cross-path behavioral pin — on a FIX 4.2 dictionary, `fixpp_msg_group_begin(t)` succeeds for exactly the same tag set that `as_table_view()` registers in the bare store, both directions. This catches a divergent second structural realization, which K6 would pass. |
| **K7** | The oracle backing K1–K4 derives from raw XML, extends `tests/dictionary/required_scope_oracle.hpp` rather than forking a third walker, and reproduces the reachability restriction (component expansion + `<header>`/`<trailer>` merge); mutating the production predicate cannot silence it. It also reports zero member-less `<group>` elements across all ten dictionaries — the **no-regression evidence for FR-023/K11** (the rejection affects 0 shipped dictionaries), and the standing measurement behind P1-NON. |
| **K8** | Builder-tier plan counts, derived by construction from `emit_builders`' interning rule and reproducible via `contracts/builder_plan_census.py` (research D-9a): `--families all` ⇒ **28** `groups/<PlanName>.hpp`, 226 files, `builder_registry` 39; `--families official` ⇒ **19** plan headers, 147 files, registry 25. The official-mode leg is a **structural** witness mirroring `determinism_test.cpp`'s `OfficialModeBuildersStructuralShape` — there is no `--families official` golden for any version. |
| **K9** | `validate_<Msg>` rejects an omitted `required='Y'` group at all **14** FIX42 pairs, with the nested `MassQuote`/295-inside-296 pair exercised via its own construction (a 296 **entry** carrying an empty 295 span, checked by `gc.validate_entry`), not as a 14th top-level omission. |
| **K10** | **Three** Article VIII benchmark obligations land in the same PR — **§2 re-baselining for (a) and (b), §3 run-and-record for (c)** (spec FR-022, research D-12 §1; leg (c) produces no baseline, so it discharges §3, not §2): **(a)** the `as_table_view()` build-time profile — `bench/dictionary/table_view_footprint_bench.cpp`, the one existing bench that times the changed function — is re-measured on FIX44 / FIX50SP2 **plus a new FIX 4.2 row**, with `BM_TableView_Sizeof` re-reported (expected unchanged; the `group_bits_` heap growth stated alongside), checked in as `bench/baselines/dictionary/table_view_footprint_bench.json` (this profile has **no** `bench/baselines/` entry today — 075 recorded it only in-file, so §2's ±5% budget has nothing to compare against); **(b)** a FIX 4.2 group-bearing **parse** bench with a fresh baseline; **(c)** the existing `bench/codegen/compile_time_bench/` harness **run** and its `v42` TU figure recorded — 082 adds 18 `class G_` to `v42/Messages.hpp`/`Reify.hpp`, and its ≤3 s single-version ceiling is load-bearing with only `v50sp2` exempt (`compile_time_bench.sh:139-143` does `exit 1`; the all-versions ceiling is WARN-only). **No CI job runs it** — `tier1.yml`'s `bench` job is soft and runs only `placeholder_bench` — so the obligation is to run it and record the figure; no baseline file, it is a ceiling check. Plus the 8-file pre-existing re-check set (spec SC-012) within ±5%. Asserting any leg unmoved instead of measuring it is non-conforming. |
| **K11** | **FR-023 — a member-less `<group>` is a load error, per loader.** A synthetic dictionary declaring a `<group>` with no resolvable `field`/`group`/`component` child fails to load: the `<fix>` loader throws `fixpp::dict::xml_parse_error` (`error.hpp:44`), the Orchestra loader throws `fixpp::dict::orchestra_parse_error` (`error.hpp:98`), each diagnostic naming the group's `name` and its `no_tag`. Asserted **per loader**, not once — and on a fixture where the member-less `<group>` is **not** the first-seen occurrence of its `no_tag`, since both `GroupDef` records sit inside a first-seen-wins dedup guard (`xml_loader.cpp:609`, `orchestra_loader.cpp:626`) and a check placed inside it would make the rule order-dependent. No new exception subclass and no `fixpp::core::error` variant (`error.hpp:18-27`), so C4.4's unchanged-surface claim and FR-017/SC-009 hold. Companion no-regression leg: all ten vendored dictionaries still load clean (K7). |
