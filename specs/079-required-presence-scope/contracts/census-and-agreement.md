# Contracts: fixpp#201 verification surfaces

These are internal test/verification contracts (this feature exposes no external API — C-ABI frozen 1.5.0; no new public C++ surface beyond the additive `table_view` accessors).

## Contract 1 — Non-circular required-set census (exact set-equality)

**Surface**: a gtest over all 10 dictionaries.

**Given** dictionary `D` loaded, and an independent raw-XML walker `expected(D, msg)` — sharing no code with the loaders or IR — that computes the message-level required set as **full-ancestor-chain component-AND composition**: a field is message-level-required iff its own `required='Y'` AND every enclosing componentRef usage on the path to the message root is `required='Y'`, AND it is not enclosed by any group — **EXCEPT StandardHeader/StandardTrailer fields (tags 8/9/34/35/49/52/56/10), which are treated as structurally-always-required and are NEVER dropped even when a message references the header/trailer componentRef with default (optional) presence** (parity-tolerance note: QuickFIX `DataDictionary.cpp:510/:522` ANDs only the *immediate* enclosing component, not the full ancestor chain; the vendored dicts contain 0 nested-optional-component sites, so this divergence never bites). **⚠️ UPDATED (Gate B r1 F3):** the oracle is no longer deliberately stronger than the loader on this axis — the loader (T020, both runtime loaders) now threads the SAME full-ancestor-chain component-AND at message level, and (Gate B r1 F1) a group-relative component-AND per group. The census's discriminating power against a regression comes from independence (the oracle shares no code with the loaders), not from asymmetric strength: a reverted/omitted component-AND on either side (loader or per-group store) would make the two sides disagree → RED (proven by the F1 mutation test and the pre-existing message-level RED-proof obligations below).

**For every** message `msg` in `D`:
- `expected(D, msg)` == `table_view::required_fields(msg_type)` — the exact set the runtime validator's **Step-2 required-field scan iterates** (Step-2's literal input; `dict_` is a `table_view` held by value in the validator, so this accessor IS the probe surface, not a sibling projection). Step-2 skips exactly tags {8,9,10} (framer-guaranteed), so the census compares the pre-skip `required_fields()` span on both sides (which also verifies 8/9/10 are present — a header/trailer carve-out regression is caught) — **exact set equality, both directions**.
- `expected(D, msg)` == codegen **IR data-structure** top-level required list for `msg` (the `MessageIR` projection — present for every version including FIX42; not the emitted validator) — safety-net leg. The three-way equality is consistent on 8/9/10 under the pre-skip definition: the IR `collect_top_fields` (`group_no_tag==0` filter) includes the top-level-required 8/9/10, matching the pre-skip runtime `required_fields()` span and the expected oracle.

**Failure**: any tag present in one set and absent in the other, for any message, in any dict → test RED with `msg`, dict, and the differing tag(s) named.

**RED-proof obligation (TWO witnesses)**:
1. **Group leak** — with the `in_group` gate reverted (group-member leak restored), the census MUST fail.
2. **Synthetic optional-component** — with a synthetic **non-header/trailer** optional-component-`required='Y'` field injected (a header/trailer field would be kept by the carve-out and so would NOT go RED), the full-component-AND oracle drops it while the loader keeps it, so the census MUST fail **even though the real corpus has 0 optional-component sites**. This is what makes the "scope narrowing does not narrow verification" claim load-bearing (RC1), not vacuous.

Prove both RED before GREEN.

**Non-circularity**: the expected walker is an independent pugixml pass; it must NOT call `XmlLoader`/`OrchestraLoader`/`build_ir()` (banner + review check).

### Contract 1a — Per-group required-member census (FR-009a)

A **distinct** set-equality leg (the message-level census above does not cover it — the per-group required-member store drives per-instance rejection, FR-004):

The census splits into **two legs** — the bare and context stores have *different* contracts, so requiring both to equal every per-context oracle is unsatisfiable on real dicts (FIX44 tag 295 NoQuoteEntries is reused with divergent direct-required members: `QuotCxlEntriesGrp` → `{}` vs `QuotEntryGrp` → `{299}`; the bare store is a single value keyed on `no_tag` alone and cannot equal both):

- **Context store (PRIMARY pin — drives FR-004 per-instance rejection)**: for **every** `(msg_type, parent_path, no_tag)` in all 10 dicts **except FIX40/FIX41/FIX42**, shipped context `group_required_members(msg_type, parent_path, no_tag)` == the independent walker's **per-context** required set — **exact set equality, both directions**. **FIX40/FIX41/FIX42 carve-out** (L-066-1 / issue #196; same INT-typed-count root as L-063-1): FIX 4.0/4.1/4.2 declare group-count fields as XML type `INT` not `NUMINGROUP`, so the context store (`dictionary.cpp:358` gates on `field_data_type::NumInGroup`) is empty for all three while the raw-XML oracle sees their structural `<group>` members — the leg would spuriously RED. All three are asserted **context-store-empty** instead (a pin that flips intentionally when #196 lands); their message-level census leg (Contract 1) is unaffected (the loader's message-level `<group>` exclusion is structural, `xml_loader.cpp:531`).
- **Bare store (fallback contract)**: shipped bare `group_required_members(no_tag)` == the **global first-seen** variant for `no_tag` (the value populated from the first-seen `group_fields(no_tag)`, reached by the validator ONLY on a context miss). The bare store is a fallback — it is NOT required to equal every per-context oracle, so assert bare == the first-seen variant only.
- **QuickFIX per-group cross-check — empirically run, NOT wired as an automated exact-match gate (Gate B r1 F2).** A local, one-off probe (linking real quickfix-cpp 1.16.0, chaining `DataDictionary::getGroup(msgType, no_tag, delim, pDD)` from the message root down each real `(msg_type, path, no_tag)` context the independent oracle enumerates, then `pDD->isRequiredField(msgType, tag)` per member — the exact mechanism `DataDictionary.cpp:560/:570`'s `checkHasRequired` itself uses) was run body-only (header/trailer groups are keyed by QuickFIX under sentinel msgTypes `"_header_"`/`"_trailer_"`, `DataDictionary.cpp:315/351` — out of scope, same carve-out as Contract 2's golden) across all 7 non-L-066-1-blind QuickFIX-schema dicts: **29,223 / 29,247 real group contexts (99.92%) matched exactly**, including both of F1's exact fix targets verified byte-for-byte (FIX50SP2 `DO`/`NoMDStatistics(2474)` excludes 2456/2457 on BOTH sides; FIX50SP1 `AR`/`NoSides(552)` retains 54 on BOTH sides). The 24 residual mismatches are **ALL strict supersets** — fixpp's shipped context store is `⊇` QuickFIX's, **never** `⊂` (no case where fixpp is missing a required member QuickFIX has) — so none is a silent-accept regression. Root cause (traced in QuickFIX's own `addXMLGroup`, `DataDictionary.cpp:536-582`): a DIRECT field member's group-required-ness in real QuickFIX is additionally gated by the **enclosing group's own `required=` attribute** (`groupRequired`, passed down from the group's own usage-site declaration — e.g. `<group name='NoUnderlyings' required='N'>` blocks EVERY direct member from ever being group-required, even one declared `required='Y'`, such as FIX44 `AP`/`UnderlyingSettlPrice(732)`) — a QuickFIX-specific rule fixpp deliberately does NOT apply. fixpp instead implements "required-once-present" (a member's own `required='Y'` stands once its group instance is present, independent of whether the *group itself* is optional) — a **NEW, in-scope** policy choice this feature makes in implementing its first-ever per-instance group-required enforcement (FR-004; `main` has zero `group_required` state and zero per-instance group-required enforcement — `add_group_required_member`/`group_required_members` first land on this branch, commit `177a0535`), stricter-safe (strict superset only, no false-accept), pinned by `tests/dictionary/required_scope_test.cpp::Fix44AsTableViewContextRequiredMembers` (asserts 732/733 ARE required members of the optional `NoUnderlyings(711)`). Reconciling this divergence (adopt literal QuickFIX parity vs. keep required-once-present) is a spec-level semantic decision, not a Gate-B fixer's call — escalated to the orchestrator. Given the semantic gap, an automated exact-match CI gate against a checked-in per-group golden is NOT added this round (it would need a per-site classification pass first, or a decision to relax fixpp's own semantic); the per-group legs above stay **self-oracled** (verified internally consistent + RED-proven — see F1's two direct regression pins) but externally corroborated only by the one-off run recorded here, not a durable CI-checked golden.

**Why distinct**: the context store drives per-instance rejection (the validator queries it first, falls back to bare only on a miss), so this leg guards a **wrong or incomplete per-context store** or an omitted per-group required member that SC-002's example frames would miss — a defect the message-level census does not cover.

**RED-proof**: inject/omit a per-context required member → the context-store leg MUST fail.

Also census the shipped **maximum** per-group required-member count across all 10 dicts (RC5 — pins the "groups carry 0–3 required members" assumption so the dynamic-width check cannot silently regress to a bounded skip).

## Contract 2 — QuickFIX required-set parity (9 QuickFIX dicts)

**Surface**: a checked-in golden (captured locally with `FIXPP_BUILD_QUICKFIX_GOLDEN=ON`, consumed in CI with no QuickFIX link) + a gtest.

**Given** quickfix-cpp 1.16.0 `DataDictionary` for each of the 9 QuickFIX-schema dicts:
- `quickfix_required_set(dict, msg)` == `expected(dict, msg)` (the census oracle) — **exact set equality**, per message.

**Extraction API (named)**: the golden generator reads the set via `DataDictionary::isRequiredField(msgType, tag)` iterated over each message's per-message field set — the required flag already encodes the component AND-rule (`DataDictionary.cpp:510`, immediate-enclosing-component only per `:522`) and per-group required members (`:560/:570`); no verdict-inference or private-internals access needed. **⚠️ CORRECTED (2026-07-19, T018/T019, Finding #5):** the original text here ("Header/trailer fields appear as ordinary required fields in the per-message set") is FALSE against real QuickFIX source — `isRequiredField(msgType, tag)` has NO header/trailer surface: header/trailer required fields populate `m_headerFields`/`m_trailerFields`, keyed independently of `msgType`, never `m_requiredFields[msgType]`. The parity golden + gate are therefore scoped **body-only** (the message-level component-AND set); the StandardHeader/StandardTrailer carve-out is pinned exclusively by Contract 1's census (which DOES verify 8/9/10 present via the pre-skip span), per [[feedback_parity_corpus_row_needs_a_surface_the_reference_engine_has]]. The golden carries a **manifest + content hash** and a **stale-golden regeneration/diff rule** per the hardened 075 precedent (a dictionary refresh regenerates + diffs the golden, never silently drifts).

**Scope note**: NO vlatest row (quickfix 1.16.0 does not parse Orchestra — a parity row for an absent surface goes spuriously RED). vlatest is covered by Contract 1 only — so the genuine optional-component blind spot (guarded by the stronger walker + synthetic RED witness, Contract 1) is **vlatest-only**; the 9 QuickFIX dicts are independently guarded here (QuickFIX encodes the component AND-rule at `:510`).

**Purpose**: confirms the independent walker encodes the AND-rule faithfully (breaks the "both implement the same wrong reading" circularity).

## Contract 3 — Two-tier verdict agreement

**Surface**: a gtest.

**Given** a frame `f` (conforming or malformed) for an affected message in a version that **has a typed tier**:
- `runtime_validate(f)` verdict (accept/reject) == `generated_typed_validate_<Msg>(f)` verdict.

**For**: the named messages + one-per-version corpus, **v44 / v50sp2 / vlatest**. **v44** carries the end-to-end full-frame verdict comparison (conforming + malformed). **v50sp2 / vlatest full-frame `validate()` is blocked by the empty FIXT `<header/>`** (L-041-2 / issue #203 — FIXT application dicts, standard header owned by FIXT.1.1; runtime Step 1 rejects on tag 8 before required-scan), so for those versions the two-tier agreement is asserted at the **derivation tier** — both the runtime `table_view::required_fields(msg)` and the typed tier's message-level required set exclude the group tag — rather than a full-frame accept/reject verdict.

**Scope — FIX42 excluded**: FIX42 has no generated typed `validate_<Msg>` (codegen driver skips builder/validator emission at `tools/codegen/fixpp-codegen/main.cpp:132` `if (ir.ns != "v42")` — L-077-1/#196: FIX 4.2 `NumInGroup=INT` ⇒ 0 typed groups). FIX42 is covered runtime-only (Contract 4) plus the census-vs-IR-**structure** leg (Contract 1 — the `MessageIR` top-level list exists for v42 even though no validator is emitted).

**Purpose**: guards the Phase-0 "no codegen change" conclusion. An unexpected mismatch localizes a missed codegen leg.

## Contract 4 — Behavioral real-frame regressions

**Surface**: gtests in `tests/wire` / `tests/dictionary`.

- **Accept**: a conforming **FIX44** PositionReport without NoUnderlyings → accepted end-to-end (was: `wire_required_field_missing(732)`); a **FIX42** conforming frame (Allocation, populated header) → accepted. For **FIX50SP2** TradeCaptureReport without NoSides, acceptance is verified at the derivation tier (required set excludes 54) — full-frame `validate()` blocked by L-041-2 / #203 (empty FIXT header).
- **Reject (per-instance)**: a **FIX44** group whose second instance omits an intra-group required member → rejected, offending tag surfaced (the end-to-end reject corroboration). FIX50SP2 full-frame reject is blocked (L-041-2 / #203); FIX42 per-instance enforcement is inert (INT-typed group counts → `consume_group` never fires, L-066-1 / #196) — both carved out, with the synthetic >64-member RED (T003) proving the dynamic-width logic independently.
- **No over-correction**: a message genuinely missing a top-level required field → still rejected. Control: FIX44 NewOrderSingle message-level required set excludes Symbol(55).
