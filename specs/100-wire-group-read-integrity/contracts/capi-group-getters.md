# Contract: C-ABI group getters (C-ABI 1.15, BREAKING)

Functions: `fixpp_msg_get_group(msg, group_tag, &group_out, &count_out)` and
`fixpp_group_get_nested_group(g, i, nested_tag, &nested_out, &nested_count_out)` (`include/fix/c_api/message.h`).

"Registered" below means the registration predicate holds for the tag in the read's context `(msg_type,
parent_path)`. Its arms are stated in research D2 only.

## Return codes

One table for both getters, evaluated in this order; the first condition that holds is the answer.

| # | Condition | Code | `*out` | `*count_out` |
|---|---|---|---|---|
| 0a | either out-parameter pointer is null | `NULL_HANDLE` (unchanged) | untouched | untouched |
| 0b | both out-parameter pointers are non-null, and the handle (`msg` / `g`) is null or invalid, or the instance index is out of range | `NULL_HANDLE` / handle error / `INDEX_OUT_OF_RANGE` (unchanged codes) | null | 0 |
| 1 | the tag is not in the message (flat) or not in instance `i` (nested), on an ample or an exhausted arena | `FIXPP_ERR_TAG_NOT_FOUND` | null | 0 |
| 2 | the tag is present **and registered**, and the read could not complete: an allocation from the per-message arena failed (instance slices, a cache row, nested sub-table, cursor shell), or a group wire limit was hit (top level: instance entry cap, nesting depth; nested: the sub-table's `wire_offset_table_full`); on the first and on every repeat read. This includes a nested group whose sub-table could not be built, a zero-count one included | `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` | null | 0 |
| 3 | the tag is present and not established as a group: not registered in this context, wherever it sits and whatever its value; or (nested) the instance's sub-table build failed for a reason other than an allocation or a wire limit | `FIXPP_ERR_TYPE_MISMATCH` | null | 0 |
| 4 | the tag is registered and zero instances are read: declared count 0, a count field last in its instance, or a declared count whose next field is not a member | `FIXPP_ERR_OK` | null | 0 |
| 5 | the tag is registered and n ≥ 1 instances are read | `FIXPP_ERR_OK` | cursor | n |

Row 1 before row 2, and row 2 before row 3, hold for both getters: a read is reported failed only for a present,
registered tag (the invariants in data-model `group_slices_result` and `nested_slices_result`), so neither an absent tag
nor a present scalar answers `WIRE_LIMIT_EXCEEDED`. Rows 1 and 3 need no memory, so an exhausted arena does not change
them. A nested `wire_group_too_large` maps to row 2 too, but is defensive: no shipped construction reaches it (research
D5).

No exception leaves either function.

## Changes from 1.14 (BREAKING)

Each is witnessed RED on `fa778a8e` (research D9).

- Flat getter, row 4, a registered zero count mid-body or as the last body field (or a header or trailer group's count
  tag holding 0, research D9 (ii-0)): was `TYPE_MISMATCH`.
- Flat getter, row 4, a registered lying count (`n ≥ 1`, next field not a member): was `TYPE_MISMATCH`. (The nested
  getter already answers `OK`/0 for it.)
- Flat getter, row 3, a nested group's count tag read at the top level with a value `n ≥ 1` (for example 802 outside
  its `[453]` parent): was a cursor through the bare member fallback (derived from source; the RED run confirms or
  corrects it). With value 0 it was and stays `TYPE_MISMATCH`.
- Flat getter, row 3, a body group read in a context its dictionary does not declare (a message whose entry does not
  declare it, for example NoPartyIDs(453) in a declared message without Parties, or a message type the dictionary does
  not declare), with a value `n ≥ 1`: was a cursor through the bare member fallback (derived from source; the RED run
  confirms or corrects it). It now answers as QuickFIX `getGroup` does. Header or trailer groups, at any depth, are not
  in this population (Unchanged). This includes a tag the header declares as a scalar and some message declares as a
  body group (QuickFIX reads it as a header scalar).
- Flat getter, row 2: was `TYPE_MISMATCH` for slice exhaustion and group-too-large; was an escaping `std::bad_alloc`
  for the cursor shell.
- Nested getter, row 3, a present scalar on an exhausted arena: was `WIRE_LIMIT_EXCEEDED` when the sub-table build or
  its cache row did not fit, `OK`/0 when both did. (The flat getter drops the flag and already answers
  `TYPE_MISMATCH`; it is a control.)
- Nested getter, row 3, a present scalar mid-instance or last in its instance: was `OK`, count 0.
- Nested getter, row 3, a registered count tag read in a context it has no record for: was `OK`/0 (or a cursor through
  the bare fallback; derived from source, confirmed by the RED run).
- Nested getter, row 1, an absent tag on an exhausted arena: was `WIRE_LIMIT_EXCEEDED` or `TAG_NOT_FOUND`, depending
  on whether the sub-table build and the cache row fitted.
- Nested getter, row 2: the cursor shell was an escaping `std::bad_alloc`; a nested sub-table over its entry cap read
  `OK`/0.
- Nested getter, row 0b: a null `g` with both out-parameters non-null returned `NULL_HANDLE` without writing them.
- Nested getter, row 3, a present tag in an instance whose sub-table build failed for another reason (073's
  malformed-data degradation): was `OK`/0. Whether a sub-table built over bytes the root already parsed, with the same
  bundle, can reach this arm is determined in the RED phase; if it cannot, the arm is defensive and says so.
- Session observers (`include/fix/c_api/session.h`, the 1.11 clause "a frame the session admits always parses for
  dispatch"): a frame crafted against the index seed (research D1) now fails the parse and takes 092's late-parse
  terminal close.

## Unchanged

- A dict-free message: the flat getter answers `TYPE_MISMATCH` for any present tag (fixpp#220 declines group reads).
- The flat getter for CheckSum(10) (the root table's last entry): `TYPE_MISMATCH`.
- A registered zero-count nested group (mid-instance or last): `OK`, count 0.
- A header or trailer group, or a group nested in one, read in a message type the dictionary does not declare (for
  example NoHops(627) in a FIX 4.3, FIX 4.4 or FIXT 1.1 frame): a group, as QuickFIX C++ reads it (research D2's
  section arm); with n ≥ 1 instances, a cursor, as today. With count 0 it reads row 4, a change from 1.14 listed
  above.
- Error code values and names; no symbol added or removed.

## Typed C++ (`MessageView::group<>()`, generated group accessors)

| Condition | `size()` | `alloc_failed()` | `read_failed()` |
|---|---|---|---|
| absent / not established as a group, on an ample or an exhausted arena; or zero instances read (the read completed) | 0 | false | false |
| an allocation failed (row 2's allocation causes, including a nested group whose sub-table could not be built, a zero-count one included) | 0 | **true** (top level: was false) | true |
| a group wire limit (row 2's limit causes) | 0 | false | **true** (was: empty, not failed) |
| n instances | n | false | false |

`read_failed()` is new; `alloc_failed()` keeps its meaning.

## Parse

A frame whose index build's total probe work exceeds `kMaxBuildProbe` × occurrences fails `Parser::parse` with
`wire_offset_table_full` (`FIXPP_ERR_WIRE_LIMIT_EXCEEDED` through the C ABI). On an inbound session it takes 092's
late-parse-failure ending: a terminal close, no Reject, no Logout, no callback. A frame within the budget indexes every
distinct tag.
