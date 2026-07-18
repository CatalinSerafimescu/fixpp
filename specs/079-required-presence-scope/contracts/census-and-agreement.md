# Contracts: fixpp#201 verification surfaces

These are internal test/verification contracts (this feature exposes no external API — C-ABI frozen 1.5.0; no new public C++ surface beyond the additive `table_view` accessors).

## Contract 1 — Non-circular required-set census (exact set-equality)

**Surface**: a gtest over all 10 dictionaries.

**Given** dictionary `D` loaded, and an independent raw-XML walker `expected(D, msg)` that computes the top-level required set (field `required='Y'`, group members excluded), sharing no code with the loaders or IR:

**For every** message `msg` in `D`:
- `expected(D, msg)` == `Dictionary::required_fields(msg)` (shipped runtime set) — **exact set equality, both directions**.
- `expected(D, msg)` == codegen-IR top-level required set for `msg` (safety-net leg).

**Failure**: any tag present in one set and absent in the other, for any message, in any dict → test RED with `msg`, dict, and the differing tag(s) named.

**RED-proof obligation**: with the `in_group` gate reverted (group-member leak restored), the census MUST fail (proves it detects the bug, not a tautology). Prove RED before GREEN.

**Non-circularity**: the expected walker is an independent pugixml pass; it must NOT call `XmlLoader`/`OrchestraLoader`/`build_ir()` (banner + review check).

## Contract 2 — QuickFIX required-set parity (9 QuickFIX dicts)

**Surface**: a checked-in golden (captured locally with `FIXPP_BUILD_QUICKFIX_GOLDEN=ON`, consumed in CI with no QuickFIX link) + a gtest.

**Given** quickfix-cpp 1.16.0 `DataDictionary` for each of the 9 QuickFIX-schema dicts:
- `quickfix_required_set(dict, msg)` == `expected(dict, msg)` (the census oracle) — **exact set equality**, per message.

**Scope note**: NO vlatest row (quickfix 1.16.0 does not parse Orchestra — a parity row for an absent surface goes spuriously RED). vlatest is covered by Contract 1 only.

**Purpose**: confirms the independent walker encodes the AND-rule faithfully (breaks the "both implement the same wrong reading" circularity).

## Contract 3 — Two-tier verdict agreement

**Surface**: a gtest.

**Given** a frame `f` (conforming or malformed) for an affected message:
- `runtime_validate(f)` verdict (accept/reject) == `generated_typed_validate_<Msg>(f)` verdict.

**For**: the named messages + one-per-version corpus (both conforming and malformed frames).

**Purpose**: guards the Phase-0 "no codegen change" conclusion. An unexpected mismatch localizes a missed codegen leg.

## Contract 4 — Behavioral real-frame regressions

**Surface**: gtests in `tests/wire` / `tests/dictionary`.

- **Accept**: a conforming FIX44 PositionReport without NoUnderlyings → accepted (was: `wire_required_field_missing(732)`). FIX50SP2 TradeCaptureReport without NoSides → accepted. One representative conforming frame per affected version.
- **Reject (per-instance)**: a group whose second instance omits an intra-group required member → rejected, offending tag surfaced. One representative per affected version.
- **No over-correction**: a message genuinely missing a top-level required field → still rejected. Control: FIX44 NewOrderSingle message-level required set excludes Symbol(55).
