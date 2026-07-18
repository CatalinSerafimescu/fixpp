# Contracts: fixpp#201 verification surfaces

These are internal test/verification contracts (this feature exposes no external API — C-ABI frozen 1.5.0; no new public C++ surface beyond the additive `table_view` accessors).

## Contract 1 — Non-circular required-set census (exact set-equality)

**Surface**: a gtest over all 10 dictionaries.

**Given** dictionary `D` loaded, and an independent raw-XML walker `expected(D, msg)` — sharing no code with the loaders or IR — that computes the message-level required set as **full-ancestor-chain component-AND composition**: a field is message-level-required iff its own `required='Y'` AND every enclosing componentRef usage on the path to the message root is `required='Y'`, AND it is not enclosed by any group — **EXCEPT StandardHeader/StandardTrailer fields (tags 8/9/34/35/49/52/56/10), which are treated as structurally-always-required and are NEVER dropped even when a message references the header/trailer componentRef with default (optional) presence** (parity-tolerance note: QuickFIX `DataDictionary.cpp:510/:522` ANDs only the *immediate* enclosing component, not the full ancestor chain; the vendored dicts contain 0 nested-optional-component sites, so this divergence never bites). The oracle is deliberately **stronger** than the loader (which threads no component-AND), so an optional-component over-require would make the oracle drop the field while the loader keeps it → RED.

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

- **Context store (PRIMARY pin — drives FR-004 per-instance rejection)**: for **every** `(msg_type, parent_path, no_tag)` in all 10 dicts **except FIX42**, shipped context `group_required_members(msg_type, parent_path, no_tag)` == the independent walker's **per-context** required set — **exact set equality, both directions**. **FIX42 carve-out** (L-066-1 / issue #196): FIX 4.2 group-count fields are XML type `INT`, so the context store (`dictionary.cpp:358` gates on `field_data_type::NumInGroup`) is empty for FIX42 while the raw-XML oracle sees FIX42's structural `<group>` members — the leg would spuriously RED. FIX42 is asserted **context-store-empty** instead (a pin that flips intentionally when #196 lands); FIX42's message-level census leg (Contract 1) is unaffected (the loader's message-level `<group>` exclusion is structural, `xml_loader.cpp:531`).
- **Bare store (fallback contract)**: shipped bare `group_required_members(no_tag)` == the **global first-seen** variant for `no_tag` (the value populated from the first-seen `group_fields(no_tag)`, reached by the validator ONLY on a context miss). The bare store is a fallback — it is NOT required to equal every per-context oracle, so assert bare == the first-seen variant only.
- both legs cross-checked against QuickFIX per-group required members (`DataDictionary.cpp:560/:570`) where available (9 QuickFIX dicts).

**Why distinct**: the context store drives per-instance rejection (the validator queries it first, falls back to bare only on a miss), so this leg guards a **wrong or incomplete per-context store** or an omitted per-group required member that SC-002's example frames would miss — a defect the message-level census does not cover.

**RED-proof**: inject/omit a per-context required member → the context-store leg MUST fail.

Also census the shipped **maximum** per-group required-member count across all 10 dicts (RC5 — pins the "groups carry 0–3 required members" assumption so the dynamic-width check cannot silently regress to a bounded skip).

## Contract 2 — QuickFIX required-set parity (9 QuickFIX dicts)

**Surface**: a checked-in golden (captured locally with `FIXPP_BUILD_QUICKFIX_GOLDEN=ON`, consumed in CI with no QuickFIX link) + a gtest.

**Given** quickfix-cpp 1.16.0 `DataDictionary` for each of the 9 QuickFIX-schema dicts:
- `quickfix_required_set(dict, msg)` == `expected(dict, msg)` (the census oracle) — **exact set equality**, per message.

**Extraction API (named)**: the golden generator reads the set via `DataDictionary::isRequiredField(msgType, tag)` iterated over each message's per-message field set — the required flag already encodes the component AND-rule (`DataDictionary.cpp:510`, immediate-enclosing-component only per `:522`) and per-group required members (`:560/:570`); no verdict-inference or private-internals access needed. Header/trailer fields appear as ordinary required fields in the per-message set. The golden carries a **manifest + content hash** and a **stale-golden regeneration/diff rule** per the hardened 075 precedent (a dictionary refresh regenerates + diffs the golden, never silently drifts).

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
