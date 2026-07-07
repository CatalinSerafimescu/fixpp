# Feature Specification: Nested Group-Parse Correctness

**Feature Branch**: `063-nested-group-parse-correctness`
**Created**: 2026-07-07
**Status**: Draft
**Input**: Fix two pre-existing, production-reachable defects that block multi-instance / tag-reused nested repeating-group typed reads against the real dictionary, so that typed grouped reads (and feature 061's grouped read + round-trip witnesses) parse via `Dictionary::as_table_view()` with correct membership and extents.

**Sources**: `research/G19-fix-fpml-iso20022/research/findings/dict-group-tag-collision-2026-07-05.md` (authoritative — consolidated 063 scope sketch for both defects); `spec/behaviors-and-limitations.md` `L-062` (the two limitations this feature clears); feature 062 (`PR #168`, `9d5ac6cb`) US2 MassQuote nested witnesses that surfaced both defects. Prerequisite-of: feature 061 (typed application messages — grouped reads parse via the real dictionary).

## Clarifications

### Session 2026-07-07

- Q: Both defects in one feature, or split? → A: Both in 063 (consolidated per the 2026-07-05 finding decision). Defect B's boundary walk needs Defect A's correct membership to identify which member tags are nested counts, so they are coupled and fixed together (A first, or together).
- Q: Which dictionaries/messages must be proven, and against what? → A: Real, shipped `Dictionary::as_table_view()` (not a hand-built `table_view`). MassQuote (FIX44 tag 295/296 quote-set/quote-entry nesting) is the confirmed exemplar; the membership fix additionally requires a census of every reused NumInGroup tag with differing membership across FIX44/FIX42/FIX50SP2, with each collision it changes covered by a regression guard.
- Q: May 062's nested-read mechanism change? → A: No. 062's `build_nested_subview` / cache / generated entry accessors are correct and re-slice faithfully. Both bugs are UPSTREAM — dictionary membership (Defect A) and `OffsetTable::group()` outer-slice extent (Defect B). This feature changes only those two, plus tests + docs.
- Q: Fix + witness scope — the three typed-flyweight namespaces (v42/v44/v50sp2), or all nine loaded runtime-XML dictionaries? → A: **All nine.** Defect A is a dict-wide loader bug affecting every loaded dictionary's group membership (incl. runtime-XML `view.get`/group access on FIX40/41/43/50/50SP1). Fix the loader correctly for all nine; the reused-tag census AND discriminating regression witnesses cover all nine shipped runtime-XML dictionaries (typed-read witnesses where flyweights exist; runtime-XML group-access witnesses on the other six).
- Q: Given the C-ABI GA freeze (1.5.0), what surface may 063 change? → A: The **public C++ typed-read / membership API MAY gain a context parameter** where the clean per-context fix requires it (the C++ API is not ABI-frozen). The **C-ABI stays byte-identical** — no new/changed exported C symbols, the freeze SHA holds; no new public error-enum / wire-format surface. Any public C++ signature change is threaded through the existing `entry_context` plumbing where possible so 062's generated-accessor slicing/cache logic (FR-005) is preserved.
- Q: Alloc budget for the nesting-aware `OffsetTable::group()` boundary walk? → A: **Allocation-free** — recursion over the existing buffer/offsets, stack-only, no new heap allocation on the group-extent path; guarded by the existing alloc/mallocnesia discipline. 062's separate bounded nested sub-view arena is unchanged.

### Session 2026-07-07 (Gate A round 1)

- Q: How does Defect A's context-scoped membership get resolved — a per-`no_tag` union (mirror codegen) or exact per-message-context membership? → A: **Exact context-scoped membership (Option A)**, keyed **`(msg_type, bounded parent-no_tag-path, no_tag)`** at runtime. The union alternative was proven unsound *for the parser*: `OffsetTable::group()` uses membership to define the byte-slice boundary, so a union false-positive trailing field is swallowed into the slice, and the engine's out-of-order body-field acceptance means a census cannot bound the risk. **The codegen union `G_<no_tag>` is kept for generated ACCESSORS only** (a post-slice superset — absent fields read as not-found); it does NOT define runtime slicing membership. `msg_type` is mandatory in the key (MassQuote vs MassQuoteAck share the full path `root→296→295`).
- Q: Under Option A, does FR-005's "don't modify 062's mechanism" still hold, given the context must reach nested slices? → A: **Nuanced**: 062's slicing/caching **algorithm** and the generated accessors are unchanged; but the `entry_context` / nested-descent **plumbing MAY gain a context field** (a trivially-copyable `msg_type` + fixed inline bounded parent-no_tag path), which `build_nested_subview`/`nested_group_slices` carry so nested slices recompute exact membership. The cache key and eviction are unchanged (context is constant within one message parse).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Correct group membership for a reused NumInGroup tag (Defect A) (Priority: P1)

An application developer parses an inbound FIX message containing a repeating group whose `NumInGroup` count tag is **also used to head a different group** elsewhere in the same dictionary (the same tag, different member sets). Reading the group's entries must return the members of the group **as used in that message**, not the members of some other same-tagged group that happened to appear first in the dictionary XML.

**Why this priority**: Wrong membership silently yields empty or misparsed entries against the real dictionary — a correctness failure on a legitimate, common FIX pattern (a `NumInGroup` tag reused with differing membership). It blocks every typed grouped read that touches a colliding tag, and is a hard prerequisite for feature 061.

**Independent Test**: Load the real `FIX44.xml` via the shipped loader, query group membership for a confirmed colliding tag (295 NoQuoteEntries), and assert it resolves to the membership of the group **as MassQuote uses it** (includes QuoteEntryID 299 / BidPx 132 / OfferPx 133), not the earlier same-tagged variant (QuotCxlEntriesGrp) that lacks them.

**Acceptance Scenarios**:

1. **Given** a dictionary in which a `NumInGroup` tag heads two different groups with different members, **When** membership is resolved for that tag *in the context of a message/component that uses the later-declared variant*, **Then** the returned member set is that variant's — not the first-declared variant's.
2. **Given** the real `FIX44.xml` and a well-formed MassQuote wire frame parsed via `Dictionary::as_table_view()`, **When** `quote_sets()[s].quote_entries()` is read, **Then** the entry count and each entry's business fields (299/132/133) are the exact wire values — not size 0.
3. **Given** the census of reused-with-differing-membership `NumInGroup` tags across all nine runtime-XML dictionaries (incl. FIXT.1.1), **When** membership is resolved for each, **Then** every case resolves to the contextually-correct variant, each covered by a discriminating regression assertion.

---

### User Story 2 - Correct outer-slice extent for a multi-instance nested group (Defect B) (Priority: P1)

An application developer parses a message where an **outer** group instance contains a **nested** repeating group with **more than one** entry (e.g. one QuoteSet holding two QuoteEntries). All nested entries within that one outer instance must be read back — the outer instance's byte extent must not be truncated at the point where the nested group's fields legitimately repeat.

**Why this priority**: The multi-instance nested case is the **common** case (a MassQuote routinely carries several QuoteEntries per QuoteSet). Truncation returns too few entries — silent data loss on a well-formed message. Single-entry nested cases already work; this closes the honest gap 062 documented and `GTEST_SKIP`'d.

**Independent Test**: Parse a MassQuote whose QuoteSet[0] contains two QuoteEntries with distinct prices via the real dictionary, and assert `quote_sets()[0].quote_entries().size() == 2` with **both** entries' distinct field values read back exactly (not 1).

**Acceptance Scenarios**:

1. **Given** an outer group instance containing a nested group with N>1 entries, **When** the outer instance's extent is computed, **Then** it spans all N nested entries (the boundary is not declared at the 2nd nested entry's reappearing member tag).
2. **Given** a MassQuote QuoteSet[0] with two QuoteEntries (distinct 132/133 values), **When** parsed via `Dictionary::as_table_view()`, **Then** `quote_entries().size() == 2` and entry[0] and entry[1] each return their own exact prices.
3. **Given** a nested group nested to depth ≥3 with multiple entries at more than one level, **When** parsed, **Then** each level's entry count and per-entry fields are correct (no cross-level truncation).

---

### User Story 3 - The deferred 062 witness turns green; L-062 is cleared (Priority: P2)

The engine maintainer needs the witness 062 had to `GTEST_SKIP()` (`NestedQuoteEntriesPerInstancePrices`, hand-built dict → proves Defect B) un-skipped and green, **plus** a net-new real-dictionary multi-entry MassQuote nested read (proves Defect A + B end-to-end), and the two `L-062` limitation rows retired, so grouped typed reads are trustworthy for feature 061 and for downstream clients.

**Why this priority**: This is the acceptance witness that proves both defects are fixed end-to-end through the shipped read path (real dictionary + generated flyweights), and the documentation state that unblocks 061. It depends on US1 and US2.

**Independent Test** — TWO named witnesses (the currently-skipped 062 witness is hand-built and proves only Defect B; it cannot be mechanically "re-pointed" at `as_table_view()` without becoming a different test — so add a net-new real-dict witness rather than mutate it):

- **US3a — un-skip the hand-built Defect-B witness UNCHANGED**: remove the `GTEST_SKIP()` from `nested_group_read_test.cpp:353` `NestedQuoteEntriesPerInstancePrices` (which uses `make_correct_massquote_dict()`, a hand-built `table_view`) and confirm it passes green — proving Defect B (nesting-aware extent) in isolation.
- **US3b — ADD a net-new real-`as_table_view()` MassQuote A+B witness**: load real `FIX44.xml` → `Dictionary::as_table_view()` → parse a MassQuote with a QuoteSet holding 2 QuoteEntries and assert both entries' distinct 299/132/133 — exercising Defect A (context-correct 295 membership) AND Defect B (full outer extent) end-to-end through the shipped read path.
- Confirm `spec/behaviors-and-limitations.md` no longer carries the two `L-062` open-limitation rows.

**Acceptance Scenarios**:

1. **Given** the hand-built `NestedQuoteEntriesPerInstancePrices` witness with its `GTEST_SKIP()` removed (US3a), **When** the wire test suite runs, **Then** it passes green (Defect B proven in isolation).
2. **Given** the net-new real-`as_table_view()` MassQuote 2-QuoteEntries witness (US3b), **When** it runs against real `FIX44.xml`, **Then** `quote_entries().size()==2` and both entries' 299/132/133 are exact (Defect A + B proven end-to-end).
3. **Given** feature completion, **When** `spec/behaviors-and-limitations.md` is read, **Then** the two `L-062` rows (A: reused-tag wrong membership; B: multi-entry nested truncation) are marked resolved with this feature's evidence reference.

---

### Edge Cases

- **Group count of zero** for a nested group inside a populated outer instance — the nested count reads 0 and consumes no nested extent; the outer boundary walk resumes correctly (must not regress the existing count-of-zero behavior, `B-004-7`).
- **Single-entry nested group** (the case that already works) — must remain correct after the boundary walk becomes nesting-aware (no over-consumption past one entry).
- **A colliding NumInGroup tag whose two variants happen to share a delimiter** but differ in later members — membership must still resolve per context.
- **Same NumInGroup tag reused with identical membership** (a benign reuse) — must continue to resolve; the fix must not reject legitimate reuse.
- **A member tag that also legitimately appears as a non-nested field** in the outer group — the boundary walk must distinguish a nested `NumInGroup` count from an ordinary repeated field only via dictionary membership (Defect A must be correct first).
- **Deeply nested (depth ≥3/4, e.g. MassQuote NoQuoteSets→NoQuoteEntries→NoLegs→NoLegSecurityAltID)** — extent computation recurses correctly at every level.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Group membership resolution MUST be **context-scoped** — when a `NumInGroup` tag heads different groups (different member sets) in different components/messages, membership queried for that tag in a given message/component context MUST return that context's member set, not a globally first-declared variant. (Defect A root cause: global first-seen-wins registration keyed only by the `NumInGroup` tag.)
- **FR-002**: The system MUST census every `NumInGroup` tag that is reused with **differing** membership across **all nine shipped runtime-XML dictionaries** (FIX40/41/42/43/44/50/50SP1/50SP2 **+ FIXT.1.1** — the `[const §I.1]` set; Article I §1 lists FIXT.1.1 as the ninth runtime-XML version, and its session-layer groups such as NoMsgTypes 384 are equally subject to reused-tag collision, so it is **included**), and MUST resolve each to its contextually-correct membership. Tag 295 (NoQuoteEntries in FIX44) is one confirmed instance; the census MUST find and cover the rest, with a discriminating regression guard per collision (typed-read witness where a generated flyweight exists — v42/v44/v50sp2; runtime-XML group-access witness for the other six).
- **FR-003**: The instance-boundary computation for a repeating group MUST be **nesting-aware** — when an outer group instance contains a nested repeating group, the outer instance's byte extent MUST include **all** nested entries, computed by consuming the nested group's declared count and extent, not by a flat "a member tag repeated ⇒ new outer instance" heuristic. (Defect B root cause: flat duplicate-tag boundary walk with no nested recursion.) The nesting-aware walk MUST be **allocation-free** — stack-only recursion over the existing buffer/offsets, no new heap allocation on the group-extent path — and MUST be guarded by the existing alloc/mallocnesia discipline.
- **FR-004**: A multi-instance nested read via the real `Dictionary::as_table_view()` MUST return the exact number of nested entries present on the wire and each entry's exact field values (a discriminating witness — not "parse succeeded", and not a truncated subset).
- **FR-005**: The fix MUST preserve feature 062's nested-read **algorithm**: the sub-view slicer's slicing/caching logic, its **cache keying/eviction**, the generated `G_<no_tag>` union **member sets**, the generated-accessor **semantics** (a post-slice superset — absent fields read as not-found), and the message-level flyweights MUST be **unchanged**. The generated nested-group accessor **call site DOES change**, however: threading Defect-A context into nested descent adds a **context argument** to the emitted `nested_group_slices(...)` call, so `tools/codegen/fixpp-codegen/emit_messages.cpp` gains a mechanical emitter edit (`emit_messages.cpp:260-270`) and the generated golden headers are **force-regenerated**; `entry_context` and the nested-descent plumbing (`build_nested_subview`/`nested_group_slices`) gain a context field. The **public C++ typed-read / membership API MAY gain a context parameter** where the per-context fix (Defect A) requires it (`group_member_fn_t`); the **C-ABI MUST remain byte-identical** (no new/changed exported C symbols; the freeze SHA holds) and NO new public error-enum or wire-format surface may be introduced. Only the dictionary membership registration/resolution (Defect A), the wire-parser instance-boundary walk (Defect B), the context-threading they require (incl. the emitter call site + forced golden regen), tests, and documentation may change.
- **FR-006**: The feature MUST preserve all existing correct behavior: single-entry nested reads, count-of-zero groups, flat (non-nested) groups, and benign same-membership tag reuse MUST remain correct (regression-guarded).
- **FR-007**: On completion the two `L-062` limitation rows in `spec/behaviors-and-limitations.md` MUST be marked resolved with an evidence reference. The 062 witness `GTEST_SKIP()`'d pending 063 (`NestedQuoteEntriesPerInstancePrices`, hand-built dict) MUST be un-skipped **unchanged** (it proves Defect B in isolation); and a **net-new** real-`Dictionary::as_table_view()` multi-entry MassQuote nested-read witness MUST be added (it proves Defect A + B end-to-end). The hand-built witness is not "re-pointed" at the real dictionary — that would change its purpose; the real-dict coverage is the net-new witness.

### Key Entities

- **NumInGroup tag**: the FIX count tag heading a repeating group (e.g. 295 NoQuoteEntries, 296 NoQuoteSets). May be reused to head different groups in different contexts — the crux of Defect A.
- **Group membership (context-scoped)**: the ordered member-tag set of a repeating group *as used in a given message/component*. Determines which member tags are ordinary fields vs nested-group count tags.
- **Group instance extent**: the byte range of one occurrence of an outer group, which for a nested-bearing group must enclose all nested-group entries (Defect B).
- **Reused-tag census**: the enumerated set of `NumInGroup` tags reused with differing membership across the shipped dictionaries — the scope boundary for FR-002.
- **L-062 limitations**: the two documented 062 gaps (A wrong-membership, B multi-entry truncation) this feature clears.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001a**: The previously-`GTEST_SKIP`'d **hand-built-dict** `NestedQuoteEntriesPerInstancePrices` witness (`nested_group_read_test.cpp:353`, `make_correct_massquote_dict()`) is un-skipped **UNCHANGED** and passes — proving Defect B (nesting-aware extent) in isolation.
- **SC-001b**: A **net-new** witness that loads real `FIX44.xml` → `Dictionary::as_table_view()` and parses a MassQuote with a QuoteSet containing two QuoteEntries reads back **both** entries with their exact distinct field values (299/132/133) — proving Defect A (context-correct 295 membership) + Defect B end-to-end through the shipped read path.
- **SC-002**: 100% of the censused reused-with-differing-membership `NumInGroup` tags (across **all nine** shipped runtime-XML dictionaries) resolve to their contextually-correct membership, each covered by a discriminating regression assertion; the tag-295 collision resolves to the QuotEntryGrp (299/132/133) variant in MassQuote context.
- **SC-003**: Both defects are proven by discriminating regression guards that **fail on the pre-fix code** (mutation-proven RED) and pass on the fixed code — one for the membership collision (A), one for the multi-entry nested truncation (B).
- **SC-004**: No regressions — the Tier-1 sanitizer/analysis matrix is green; single-entry nested, count-of-zero, flat-group, and benign same-membership reuse behaviors remain correct; the two `L-062` rows are retired with evidence.
- **SC-005**: The C-ABI freeze holds — `tools/capi_freeze.sha256` verification passes unchanged (no exported C symbol added/changed); and the nesting-aware boundary walk allocates zero heap on the group-extent path (proven by the alloc/mallocnesia gate).

## Assumptions

- **Consolidation** (decided, Clarifications 2026-07-07): both defects ship in feature 063; Defect A is fixed before/with Defect B because the nesting-aware boundary walk (B) depends on correct context-scoped membership (A) to identify nested count tags.
- **Real dictionary is the bar** (decided): witnesses parse via the shipped `Dictionary::as_table_view()`, not a hand-built `table_view`; a hand-built-table witness would false-pass the shipped read path (the client uses the real dictionary).
- **062 mechanism is correct and frozen** (verified 2026-07-05, source-read): the sub-view slicer, its cache, and generated entry accessors faithfully re-slice whatever outer slice / membership they are handed; both bugs are upstream, so this feature does not touch them.
- **Both defects are pre-existing on `main`** (verified: 062 added `build_nested_subview`/`nested_group_slices` and never modified the dictionary loader's group registration nor the wire-parser instance-boundary walk).
- MassQuote (FIX44) is the confirmed exemplar for both defects; the FR-002 census may surface additional colliding tags whose coverage is added as discriminating guards (data/test scope, not new mechanism).
- **Scope = all nine runtime-XML dictionaries incl. FIXT.1.1** (decided, Clarifications 2026-07-07): Defect A is a dict-wide loader bug, so the loader fix and the reused-tag census+guards span the full `[const §I.1]` set (FIX40/41/42/43/44/50/50SP1/50SP2 + FIXT.1.1), not only the three typed-flyweight namespaces.
- **Surface** (decided): the public C++ typed-read/membership API may gain a context parameter; the C-ABI stays byte-identical (GA-frozen 1.5.0) and no new public error-enum/wire-format surface is added. This is a Wire-format + dictionary-layer change → full mandatory controls + Gate A/B.
- **Allocation-free boundary walk** (decided): Defect B's nesting-aware extent computation adds no heap allocation on the group-extent path; 062's bounded nested sub-view arena is untouched.
