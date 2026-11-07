# Contract: C-ABI group getters (C-ABI 1.15, BREAKING)

Functions: `fixpp_msg_get_group(msg, group_tag, &group_out, &count_out)` and
`fixpp_group_get_nested_group(g, i, nested_tag, &nested_out, &nested_count_out)` (`include/fix/c_api/message.h`).

## Return codes

Evaluated in this order; the first that holds is the answer.

| # | Condition | Code | `*out` | `*count_out` |
|---|---|---|---|---|
| 0 | a null out-parameter / invalid handle / bad instance index | `NULL_HANDLE` / handle error / `INDEX_OUT_OF_RANGE` (unchanged) | null | 0 |
| 1 | the tag is not in the message (flat) or not in instance `i` (nested) | `FIXPP_ERR_TAG_NOT_FOUND` | null | 0 |
| 2 | the read could not complete: an allocation from the per-message arena failed (instance slices, nested sub-table, cursor shell), or a group wire limit was hit (instance entry cap, nesting depth, a nested sub-table's index limit) | `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` | null | 0 |
| 3 | the tag is present but the dictionary does not register it as a group count in this context | `FIXPP_ERR_TYPE_MISMATCH` | null | 0 |
| 4 | a group with declared count 0, wherever its count field sits | `FIXPP_ERR_OK` | null | 0 |
| 5 | a group with n ≥ 1 instances | `FIXPP_ERR_OK` | cursor | n |

For the nested getter, row 2 is checked before row 1 (073's order, kept): the nested sub-table is built over the
instance before the tag is looked up, so on an exhausted arena an absent nested tag answers `WIRE_LIMIT_EXCEEDED`. The
flat getter's lookup needs no allocation, so it answers row 1 first.

No exception leaves either function.

## Changes from 1.14 (BREAKING)

- Nested getter, row 3: was `OK`, count 0.
- Flat getter, row 2: was `TYPE_MISMATCH` for slice exhaustion and group-too-large; was an escaping `std::bad_alloc` for
  the cursor shell.
- Flat getter, row 4: was `TYPE_MISMATCH`.
- Nested getter, row 2: the cursor shell was an escaping `std::bad_alloc`.

## Unchanged

- A dict-free message: the flat getter answers `TYPE_MISMATCH` for any present tag (fixpp#220 declines group reads).
- Error code values and names; no symbol added or removed.

## Typed C++ (`MessageView::group<>()`, generated group accessors)

| Condition | `size()` | `alloc_failed()` |
|---|---|---|
| absent / not a group / zero count | 0 | false |
| the read did not complete (row 2's causes) | 0 | **true** (top level: was false) |
| n instances | n | false |

## Parse

A frame whose index build reaches the probe cap fails `Parser::parse` with `wire_offset_table_full`
(`FIXPP_ERR_WIRE_LIMIT_EXCEEDED` through the C ABI). On an inbound session it takes 092's late-parse-failure ending: a
terminal close, no Reject, no Logout, no callback.
