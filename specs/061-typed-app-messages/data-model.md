# Data Model: 061-slim (write shape-oracle)

This is a C++ library feature; the "data model" is the set of new types/contracts and the test-data
shapes. No persistent storage or DB entities.

## 1. `wire::body_builder` (new wire-layer type)

A body-only FIX serializer with a LIFO repeating-group API. Assembles into an internal buffer and commits
to a caller span on success (fail-closed atomic). Mirrors the C-ABI `OutboundAccumulator` shape.

| Member (indicative signatures) | Purpose |
|---|---|
| `explicit body_builder(std::string_view msg_type)` | Seeds the leading `35=<msg_type>\x01`. Multi-char (`AS`) supported. |
| `expected_t<void> field(uint16_t tag, std::string_view v)` | Flat string field (validated: non-empty if required, no control bytes / SOH). Lift of 020 `wfield`. |
| `expected_t<void> field(uint16_t tag, char c)` | Flat char field. Lift of 020 `wchar`. |
| `expected_t<void> field(uint16_t tag, std::int64_t v)` | Flat int field (ASCII). |
| `expected_t<void> field(uint16_t tag, const decimal_t& v)` | Flat decimal, canonical via `decimal_t::format` (INV-3). Lift of 020 `wdecimal`. |
| `expected_t<group_handle> group_begin(uint16_t no_tag)` | Open a top-level repeating group (count tag `NoXXX`). |
| `expected_t<entry_handle> group_handle::add_entry()` | Start a new group instance. |
| `entry_handle::set_{string,char,int,decimal}(tag, v)` | Per-entry field (same validation as flat). |
| `expected_t<group_handle> entry_handle::group_begin(uint16_t no_tag)` | Nested group inside an entry. |
| `expected_t<void> group_end(group_handle)` | LIFO close; must match the top of the open-group stack. |
| `expected_t<std::span<std::byte>> commit(std::span<std::byte> out)` | Validate all groups closed, serialize (count-precedence, delimiter-first), copy to `out` atomically. Returns body span or a typed error (buffer untouched on failure). |

**Invariants**: (INV-2) no framing tags 8/9/34/49/52/56/10 ever emitted; (INV-3) decimals canonical;
(INV-4) all-or-nothing — no partial write to `out`. Buffer bounded (commit-cap analogous to C-ABI 3800B).

**Internal state** (not public): open-group LIFO stack; accumulated entries (recursive: scalar | group of
instances, each instance a field list) — structurally the C-ABI `AccumulatorEntry`/`GroupInstance` shape.

## 2. Exemplar builders (session layer, on top of `body_builder`)

Signatures follow the 020 precedent: `expected_t<std::span<std::byte>> build_<msg>(std::span<std::byte> out, <typed fields…>) noexcept`.

| Builder | MsgType | Group shape emitted | Field-coverage bar |
|---|---|---|---|
| `build_new_order_single` (refactor) | D | none (or count-0) | flat scalars: `11,55,54,38,44,40,60` (existing 020 set) |
| `build_execution_report` (refactor) | 8 | none (or count-0) | flat scalars: `37,17,150,39,55,54,151,14,6` (existing 020 set) |
| `build_order_cancel_reject` (new) | 9 | **none** (group-free) | `37,11,41,39,434` + required |
| `build_new_order_list` (new) | E | `NoOrders(73)` → `NoPartyIDs(453)` → `NoPartySubIDs(802)` | ≥2 orders, ≥1 with ≥1 party + ≥1 party-sub-id; per-order `11,55,54,38,40`; count-0 case tested separately |
| `build_allocation_report` (new) | AS | nested (73/78/453→802/…) — a representative subset | multi-char `35=AS`; ≥1 alloc + ≥1 nested party-sub — representative, not all 10 groups |

**Representative shape-oracle rule**: set all *required* fields + enough optionals to exercise every field
*type* + the group/nesting *shape*; NOT every optional field (per clarify Q2 = A).

## 3. Seed table (witness-harness input)

One constexpr/`static` seed record per exemplar drives both the builder call and the read-back assertions:

```
struct ExemplarSeed {
  std::string_view msg_type;          // "D","8","9","E","AS"
  std::string_view begin_string;      // "FIX.4.4" (all v44)
  // typed field values (scalars) + a nested group-shape description
  // (counts + per-entry field seeds) sufficient to (a) drive the builder,
  //  (b) assert each field's exact read-back value,
  //  (c) diff against the golden.
  std::string_view golden_path;       // tests/session/golden/<msg>.fix
};
```

## 4. External golden (test asset)

- One `tests/session/golden/<msg>.fix` per exemplar: the **body-only** byte sequence QuickFIX-cpp produces
  for the same seed (session-header tags stripped, decimals canonical), plus a provenance note (QuickFIX
  version + seed reference).
- Consumed by `golden_diff.hpp::diff_transcripts` with a normalization/exclusion tag set (decimal-by-value).

## 5. Read-scaffold (test support)

- `tests/support/app_message_read_scaffold.hpp`: `make_frame(begin_string, body) → bytes` and
  `parse_dict(bytes, dict, mr) → MessageView<Index>` using the **5-arg dict-backed** path. Reused by both
  the round-trip harness and the independent inbound-read witnesses.

## State transitions

None (stateless serialization + parse). The only "lifecycle" is `body_builder`'s open-group LIFO stack,
which MUST be empty at `commit()` (else typed error).
