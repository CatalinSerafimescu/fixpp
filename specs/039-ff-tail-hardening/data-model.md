# Phase 1 Data Model: F-f tail hardening bundle (LOW)

**No new entities, fields, or persisted structures, and no production behavior change.** This bundle
is test-completeness / build-gate / doc. (US1 — the only behavioral surface — was split out to
`040-inbound-tag-overflow-hardening`.)

## C-ABI sentinel behavior (US2 — pinned, not changed)

| Input to `_checked` compare/equal | Result (ratified; pinned by test) |
|-----------------------------------|-----------------------------------|
| `INT64_MIN` sentinel mantissa, exponent `∈ [-38,0]` | `FIXPP_ERR_OK`, ordering 0 / equal 0 |
| exponent `∉ [-38,0]` | `FIXPP_ERR_DECIMAL_INVALID` (exponent-domain validation) |
| two valid in-domain decimals | `FIXPP_ERR_OK` with the true ordering/equality |

No row in this table changes — US2 only adds a test that asserts it, plus a cross-reference comment.

## US3 / US4 / US5

No data model. US3 = test coverage of existing branches; US4 = a build-gate header list; US5 = doc
wording.
