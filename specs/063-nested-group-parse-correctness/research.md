# Phase 0 Research: Nested Group-Parse Correctness (063)

**Date**: 2026-07-07 · **Branch**: `063-nested-group-parse-correctness`
**Inputs**: `spec.md`; finding `research/G19-fix-fpml-iso20022/research/findings/dict-group-tag-collision-2026-07-05.md`; Explore source-surface map (2026-07-07); lightweight census (`scratchpad/census.py`, all nine XMLs).

---

## Source-surface map (verified 2026-07-07, both defects pre-existing on `main`, 062-untouched via git)

### Defect A — loader first-seen-wins group registration
- `LoaderState::group_index_by_no_tag_` — `std::unordered_map<uint16_t,uint16_t>` (no_tag → idx into `groups_`), `src/dictionary/xml_loader.cpp:275`. First-seen-wins guard `xml_loader.cpp:486` (`if (!contains(no_tag))`), emplace `:525`, inside the `<group>` arm of `expand_field_list` (`:464-529`).
- **Context available at registration**: `expand_field_list(...)` params `enclosing_group_no_tag`, `enclosing_component_index` (`:249-252`); `GroupDef.parent_group_no_tag` (`:212`, set `:522`). `msg_type` is NOT in scope here (expand runs over component/message-body walks).
- Collision materializes in `table_view.group_members_` / `group_first_` — both keyed by bare no_tag (`include/fixpp/dict/table_view.hpp:215,218`); accessors `group_member_tags`/`group_first_field` (`:112,119`). `Dictionary::as_table_view()` (`src/dictionary/dictionary.cpp:295-357`) faithfully propagates the collision. Handle accessors also bare-no_tag keyed (`dictionary.cpp:90-97,225,250,269`).
- **Parse-time query point (single site)**: the `group_member_fn` captureless lambda bound in `Parser::Parser` (`include/fixpp/wire/parser.hpp:484-494`) → `group_member_tags(no_tag)`. Second consumer: `wire/validator.hpp:219`.
- **`group_member_fn_t`**: `bool(*)(void const*, uint16_t no_tag, uint16_t member_tag) noexcept` (`include/fixpp/wire/offset_table.hpp:29`). **No context arg** — the plumbing a context param extends. Thread path: Parser → MessageView → OffsetTable → `entry_context`.

### Defect B — `OffsetTable::group()` flat boundary walk
- `OffsetTable::group()` `src/wire/offset_table.cpp:402-482`; flat `seen_in_instance` loop `:450-459` (breaks instance on any member-tag repeat). No nested recursion. Access: `group_member_fn_`, `opaque_dict_`, `delim`, `entries_`, `cfg_.max_group_entries_per_instance` — no nested-count awareness.
- `group_slices()` `:484-546` calls `group()`; **10 callers** (blast radius incl. `src/capi/message_read.cpp`, `MessageView::group<>` `parser.hpp:271`, `nested_group_slices`).
- 062 downstream mechanism (`build_nested_subview` `:552`, `nested_group_slices` `:582-633`) is CORRECT — re-slices whatever (possibly truncated) outer slice it's handed. `entry_context` (`include/fixpp/wire/group_view.hpp:31-47`, trivially-copyable) carries span/mr/opaque_dict/group_member_fn/gen/parent_cache_owner/outer_occurrence_id — the ride-along for a context field.

### Surface & tests
- **C-ABI frozen group symbols (must NOT change)**: `fixpp_msg_get_group`, `fixpp_msg_group_begin/end`, `fixpp_entry_group_begin`, `fixpp_group_get_field_string/_int/_double/_decimal`, `fixpp_group_get_nested_group`, `fixpp_group_builder_add_entry`. Golden `tests/abi/golden/fixpp_capi_symbols.txt`; header freeze `tools/capi_freeze.sha256`; map `src/capi/fixpp_capi.map`. `fixpp_msg_get_group` (`src/capi/message_read.cpp:336`) sits atop `group_slices` — its OUTPUT legitimately corrects, surface frozen.
- **Alloc gates**: `tests/alloc_guard/wire_alloc_guard_test.cpp`; `tests/codegen/group_entry_alloc_gate_test.cpp` (header comment already names Defect B deferred-to-063).
- **062 skipped witness**: `tests/codegen/nested_group_read_test.cpp:353` `NestedQuoteEntriesPerInstancePrices` — uses a **hand-built** `table_view` (`make_correct_massquote_dict()` `:118-133`), so un-skipping proves only Defect B. **The real-`as_table_view()` MassQuote multi-entry witness is NET-NEW for 063** (does not exist) and is the SC-001 anchor exercising both A+B. `group_slice_trailing_soh_test.cpp` is also entirely hand-built table_view (this is the finding's "why it stayed hidden").

---

## PIVOTAL FINDING — the collision is systemic and NOT parent-disambiguable

Lightweight census (`scratchpad/census.py`) over all nine runtime XMLs — reused NumInGroup tags with **differing immediate membership**:

| Dict | # colliding tags | Examples |
|---|---|---|
| FIX40 | 1 | 73 |
| FIX41 | 1 | 73 |
| FIX42 | 7 | 73, 78, 146, 268, 295, 296, 420 |
| FIX43 | 9 | +552, 555 |
| FIX44 | **12** | 73, 78, 124, 146, 268, 295, 296, 420, 552, 555, 711, 936 |
| FIX50 | 13 | +386 |
| FIX50SP1 | 14 | +1351 |
| FIX50SP2 | **22** | +702, 1310, … |

**Two conclusions:**
1. **Systemic** — 12–22 colliding tags per modern dict, not the single tag-295 the finding named. Any per-context fix must be general, and the FR-002 census + guards span a broad set.
2. **`(parent_group_no_tag, no_tag)` is INSUFFICIENT** — every collision is PARENT-AMBIGUOUS (still multiple membership variants under the same parent), most at parent=0 (top-level of different messages/components). Membership is genuinely **message/component-path-scoped** — the same tag (e.g. 146 NoRelatedSym: 12 variants in FIX50SP2) legitimately carries different fields in different messages. This mirrors how QuickFIX resolves groups by walking the message-definition tree, not a global tag→members map.

**Census verified with component expansion** (`census2.py`, mirrors the loader's `<component>`-ref expansion): the systemic + parent-ambiguous conclusion HOLDS (FIX44 12 tags all parent-ambiguous; FIX50SP2 22→21, negligible). Ground-truth on the trusted case now matches the finding exactly — FIX44 tag 295: parent=0 variant lacks 299 (QuotCxlEntriesGrp), parent=296 variant has 299 (QuotEntryGrp) — **and even `(parent=296, 295)` still carries ≥2 variants** (MassQuote vs MassQuoteAck), confirming membership is message-scoped, not merely parent-scoped. (Conclusions (1)/(2) matter for **Option A**; they are the reason a *simple parent-key* fix is impossible — but see the codegen-union reframing below, which makes **Option B** the recommended path and sidesteps the context-key problem entirely.)

---

## Codegen already solved membership — with a UNION model (the design oracle)

Verified in `tools/codegen/fixpp-codegen/emit_messages.cpp` (comments `:126-139,383-394`; keying `:390-406`): codegen emits **one shared `G_<no_tag>` per no_tag** in `namespace groups`, whose member set is the **UNION of all member tags** seen under that no_tag across every message (keyed by `FieldRef::group_no_tag`, deduped first-encounter — explicitly "what tames FIX50SP2"). Confirmed on the wire: the single `v44 groups::G_295` (generated `Messages.hpp:2999`) bakes 299/132/133 (`quote_entry_id`/`bid_px`/`offer_px`) — the union includes MassQuote's QuotEntryGrp fields, so **MassQuote typed reads are already correctly accessored**; MassQuote references it at `:23130` (`group<295,G_295>()`), and `G_296` nests `group_view<G_295>` (`:3440`).

**This RESOLVES the FR-005 tension**: the flyweight is a permissive **superset** (reading a field absent from a given message's wire just returns not-found), so 063 does NOT need to touch generated accessors — Defect A/B fix the runtime SLICING/EXTENT, and the superset flyweight reads the correctly-sliced bytes. Codegen is untouched.

## `OffsetTable::group()` mechanics (verified `offset_table.cpp:402-482`)
Membership (`group_member_fn`) does two things: (1) **acceptance** — `:433` rejects the group if `delim` (first field after the count) isn't a member (THE Defect-A symptom: first-seen-wins picked QuotCxlEntriesGrp whose members lack 299 → `group_member_fn(295,299)=false` → empty); (2) **outer extent** — `:447-448` a non-member tag ends the instance/group. The `seen_in_instance` repeat heuristic (`:450-459`) is the Defect-B truncation. So the loader's membership drives both acceptance and extent.

## Design decisions (Phase 0)

### D-A · Group-membership fix (Defect A) — DESIGN FORK for Gate A
The finding proposed full per-context membership. The census + the codegen-union discovery reframe this into a genuine fork (plan.md carries it as the primary Gate-A question; the exhaustive FR-002 census + over-extension analysis adjudicate):

- **Option A — per-message-context membership** (finding's proposal): correct in all cases; **large** — a message/enclosing-path-scoped key (census proved parent-group-no_tag alone is insufficient: every collision parent-ambiguous) threaded through loader → `Dictionary`/`table_view` → `group_member_fn_t` signature → parser/`OffsetTable`/`entry_context` → `validator.hpp:219`.
- **Option B — union-per-no_tag membership in the loader** (mirror codegen, RECOMMENDED to Gate A): **small** — change the loader's first-seen-wins (`xml_loader.cpp:486`) to **accumulate the union** of members per no_tag (keyed by `(group_no_tag, no_tag)` to match codegen exactly). `group_member_fn(295,299)` then returns true (299 ∈ union) → symptom fixed; NO signature change, C-ABI trivially unaffected. **Risk to close**: union can **over-extend** `group()`'s outer walk (`:447`) if a field that *follows* the group on the wire happens to be a member in another message's variant of the same no_tag. Gating analysis: extend the FR-002 census to check, for every OFFICIAL message (and ideally all nine dicts), that no group's trailing wire-neighbour collides with its no_tag's union — where it does, document as a limitation or fall back to Option A for that tag.
- **Recommendation**: **Option B** — it mirrors the proven codegen model, is minimal, keeps the C-ABI and signatures stable, and fixes the reported symptom; contingent on the over-extension analysis coming back clean for the OFFICIAL set. Option A is the fully-correct fallback if the analysis finds real over-extension on an in-scope message.
- **Clarify note**: the "public C++ API may gain a context param" allowance (clarify Q2) is only *exercised* under Option A; under Option B no public signature changes — still within the sanctioned envelope.

### D-B · Nesting-aware boundary walk (Defect B), allocation-free
- **Decision**: in `OffsetTable::group()`'s per-instance walk, when a member tag is itself a **NumInGroup count in this context** (determined via the now-context-correct membership predicate), read its count value from `entries_` and **recursively consume that nested group's extent** (its count × per-entry span, honoring its own delimiter) before resuming outer-boundary detection — replacing the flat `seen_in_instance` heuristic.
- **Allocation-free**: recursion over `entries_` indices via a depth-bounded helper (depth cap = existing `cfg_` group nesting bound; explicit index cursor, no heap). Guarded by `group_entry_alloc_gate_test.cpp` + `wire_alloc_guard_test.cpp`.
- **Ordering**: B depends on A — the walk must know which member tags are nested counts *in context*, which requires context-correct membership first.

### D-C · Witnesses
- **SC-001 real-dict witness (NET-NEW)**: load real `FIX44.xml` → `Dictionary::as_table_view()` → parse a MassQuote with a QuoteSet holding 2 QuoteEntries → assert `quote_entries().size()==2` + both entries' 299/132/133. Exercises A (correct 295 membership) AND B (full outer extent).
- **Un-skip** `nested_group_read_test.cpp:353` after the B fix (hand-built-dict variant → proves B in isolation).
- **Defect-A discriminating guard**: real-dict membership query for the censused colliding tags resolves to the contextually-correct variant; mutation-proven RED on the pre-fix global key.
- **Defect-B discriminating guard**: multi-entry nested outer-extent (mutation-proven RED on the flat heuristic).
- **Regression**: single-entry nested, count-of-zero (B-004-7), flat groups, benign same-membership reuse remain correct.
- **C-ABI**: `abi_symbol_golden` + `capi_freeze.sha256` unchanged; a witness that `fixpp_msg_get_group` nested output is now correct.

### D-D · Scope (clarify-decided)
- Loader fix + census + guards span **all nine** runtime XMLs; typed-read witnesses where flyweights exist (v42/v44/v50sp2), runtime-XML group-access witnesses for the other six.

---

## Risks / Gate-A questions
1. **Defect-A fix: Option A (per-context) vs Option B (union-per-no_tag, mirror codegen)** — THE primary Gate-A design decision. Recommendation Option B (minimal, mirrors the proven codegen union model, no signature/C-ABI change), **gated on** the over-extension analysis (risk 2) coming back clean for the OFFICIAL set; Option A is the correct-in-all-cases fallback.
2. **Option-B over-extension**: union membership can make `group()`'s outer walk (`offset_table.cpp:447`) swallow a post-group field that collides with another variant's membership. Must be closed by the extended FR-002 census (per OFFICIAL message: does any group's trailing wire-neighbour ∈ its no_tag union?). Clean → Option B ships; a real hit on an in-scope message → Option A (or a per-tag exception).
3. **Defect-B allocation-free recursive walk** on the hot read path — depth-bounded index recursion over `entries_`, no heap; prove via `group_entry_alloc_gate_test.cpp` + `wire_alloc_guard_test.cpp`. Orthogonal to A/B and needed either way.
4. **FR-005 tension RESOLVED**: codegen emits a superset `G_n` (union), so 063 touches no generated accessors; runtime fix + superset flyweight compose correctly.
5. **Scope vs finding**: Defect A is *systemic* (12–22 colliding tags/dict) but under Option B the FIX is small; the *census + guards* are the broad part. Not a user-scope question — engineering for Gate A (per advisor 2026-07-07); the user-owned scope (FR-015a-in, 063-first) is already decided.
6. **C-ABI freeze**: `fixpp_msg_get_group` etc. sit atop `group_slices` — outputs legitimately correct, symbols/headers frozen (`abi_symbol_golden` + `capi_freeze.sha256` unchanged) under both options.
