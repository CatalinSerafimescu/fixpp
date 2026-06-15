# Phase 1 Data Model: F-f tail hardening bundle

**No new entities, fields, or persisted structures.** This bundle is hardening / test-completeness /
doc. The only behavioral surface touched is the inbound field-decode disposition (US1). The relevant
invariants:

## Decode disposition invariant (US1)

For the inbound field decoder, for any field whose tag token's decimal value `T`:

| Condition | Index mode (`OffsetTable`) | Scan mode (`field_iterator`) |
|-----------|----------------------------|------------------------------|
| `0 < T ≤ 0xFFFF` (valid) | field inserted under tag `T` (unchanged) | field yielded with tag `T` (unchanged) |
| `T > 0xFFFF` (incl. any value that would wrap a fixed-width accumulator) | `status_ = err_tag_out_of_range()`, `entries_.clear()`, whole message → all fields absent | `done_ = true`, iteration terminates at the field; field never yielded |

**Key invariant (US1, SC-001/FR-005)**: there is NO `T > 0xFFFF` that, via accumulator wrap, becomes
queryable under an aliased small tag. The guard fires on accumulated value crossing `0xFFFF` *during*
the digit scan, before any wrap can occur.

**Boundary (edge cases)**:
- `T = 65535` → valid (decodes unchanged).
- `T = 65536` → rejected (already true; preserved).
- `T = 4294967330` (uint32-wraps to 34) → rejected (the fix; previously aliased to 34).
- Zero-padded in-range token (e.g. `000000000034`) → valid, decodes as 34 (guard is on *value*, not
  digit count).

## C-ABI sentinel behavior (US2 — pinned, not changed)

| Input to `_checked` compare/equal | Result (ratified; pinned by test) |
|-----------------------------------|-----------------------------------|
| `INT64_MIN` sentinel mantissa, exponent `∈ [-38,0]` | `FIXPP_ERR_OK`, ordering 0 / equal 0 |
| exponent `∉ [-38,0]` | `FIXPP_ERR_DECIMAL_INVALID` (exponent-domain validation) |
| two valid in-domain decimals | `FIXPP_ERR_OK` with the true ordering/equality |

No row in this table changes — US2 only adds a test that asserts it.

## US3 / US4 / US5

No data model. US3 = test coverage of existing branches; US4 = a build-gate header list; US5 = doc
wording.
