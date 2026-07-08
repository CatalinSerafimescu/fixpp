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
| `expected_t<group_handle> group_begin(uint16_t no_tag, uint16_t delimiter_tag)` | Open a top-level repeating group (count tag `NoXXX`). The **`delimiter_tag`** is the group's first-field tag (the delimiter every instance must lead with) — the author supplies it (no dictionary lookup), and `commit()` enforces it (INV-5). |
| `expected_t<entry_handle> group_handle::add_entry()` | Start a new group instance. |
| `entry_handle::set_{string,char,int,decimal}(tag, v)` | Per-entry field (same validation as flat). The **first** `set_*` on an instance MUST be `delimiter_tag` (checked at `commit()`, INV-5). |
| `expected_t<group_handle> entry_handle::group_begin(uint16_t no_tag, uint16_t delimiter_tag)` | Nested group inside an entry (delimiter carried as above). |
| `expected_t<void> group_end(group_handle)` | LIFO close; must match the top of the open-group stack. |
| `expected_t<std::span<std::byte>> commit(std::span<std::byte> out)` | Validate all groups closed (INV-4), validate each group instance is non-empty and **delimiter-first** (INV-5 — mirrors the C-ABI `validate_group_grammar`, `message_write.cpp:682-701`), serialize (count-precedence), copy to `out` atomically. Returns body span or a typed error (buffer untouched on failure). |

**Invariants**: (INV-2) no framing tags 8/9/34/49/52/56/10 ever emitted; (INV-3) decimals canonical;
(INV-4) all-or-nothing — no partial write to `out`, and `commit()` with any group still open → typed error;
(INV-5) **repeating-group grammar** — every emitted group instance is non-empty and leads with its
`delimiter_tag`; a wrong-first-field or empty instance fails closed at `commit()` (mirrors the C-ABI
`validate_group_grammar` empty-instance + delimiter-first legs, `src/capi/message_write.cpp:682-701`,
called from `fixpp_msg_commit:727`). Because the exemplar author supplies `delimiter_tag`, `body_builder`
enforces INV-5 with **no `wire→dictionary` edge**.

**Buffer/allocation policy** (pinned — closes the research Decision 4 "fixed-scratch OR vector" open item):
`body_builder` assembles into a **fixed internal scratch buffer capped at the C-ABI `kFrameCap` (3800 B)**
and carries **no `memory_resource`** on the ctor or `commit()` — the simplest lifetime-correct option, no
per-call arena ([[feedback_monotonic_arena_percall_pmr_vector_leaks]]). A body that would exceed the cap
fails closed (typed error, `out` untouched). The 5 exemplar bodies are well under 3800 B; if FR-015a later
needs a larger or growable accumulator, that is an FR-015a API decision, not 061's.

**Internal state** (not public): open-group LIFO stack; accumulated entries (recursive: scalar | group of
instances, each instance a field list) — structurally the C-ABI `AccumulatorEntry`/`GroupInstance` shape.

## 2. Exemplar builders (session layer, on top of `body_builder`)

Signatures follow the 020 precedent: `expected_t<std::span<std::byte>> build_<msg>(std::span<std::byte> out, <typed fields…>) noexcept`.

The **required** set of each seed is dictionary-derived (see §3 for the enumerated, `FIX44.xml`-cited
tables); optionals are added only to exercise a field *type* or the nesting *shape*.

| Builder | MsgType | Group shape emitted | Required-complete seed (see §3) |
|---|---|---|---|
| `build_new_order_single` (refactor) | D | none | msg-required `11,55,54,60,38,40` + optional `44` (decimal) |
| `build_execution_report` (refactor) | 8 | none | msg-required `37,17,150,39,55,54,151,14,6` (existing 020 set — already required-complete) |
| `build_order_cancel_reject` (new) | 9 | **none** (group-free) | msg-required `37,11,41,39,434` + optional `102` (int) |
| `build_new_order_list` (new) | E | `NoOrders(73)` → `NoPartyIDs(453)` → `NoPartySubIDs(802)` (3-level) | msg-required `66,394,68,73`; per-order required `11,67,55,54,38`; ≥2 orders, ≥1 carrying the nested `453→802` party chain; count-0 case on the **optional** `453` group (§3) |
| `build_allocation_report` (new) | AS | top-level `NoPartyIDs(453)` → `NoPartySubIDs(802)` (2-level, optional group) | msg-required `755,71,794,87,857,54,55,53,6,75`; one nested `453→802` party chain — representative, not all of AS's groups |

**Representative shape-oracle rule**: set **all message-level `required='Y'` fields + the identifying first
field of each `required='Y'` component** (Symbol←Instrument, OrderQty←OrderQtyData — see §3 note), plus
enough optionals to exercise every field *type* + the group/nesting *shape*; NOT every optional field (per
clarify Q2 = A). AS's seeded nesting is the **2-level** `453→802` chain (distinct from E's **3-level**
`73→453→802`); AS's other groups (`OrdAllocGrp NoOrders(73)`, `AllocGrp NoAllocs(78)`) are out of the
representative subset.

## 3. Seed table (witness-harness input)

### 3.1 Required-field-complete seeds (dictionary-derived from `dictionaries/FIX44.xml`)

Each seed is the **message-level `required='Y'` set** for that message PLUS the identifying first field of
every `required='Y'` component, PLUS a small number of optionals to exercise a field *type* or the nesting
*shape*. Tag numbers verified against `dictionaries/FIX44.xml`.

> **Component-required convention (read this before checking a tag against the XML):** `FIX44.xml` marks
> the fields *inside* a component (e.g. `Symbol(55)` in `Instrument`, `OrderQty(38)` in `OrderQtyData`,
> `PartyID(448)` in `Parties`) as `required='N'` even when the component itself is `required='Y'` on the
> message. A `required='Y'` component cannot be meaningfully emitted (nor authored by QuickFIX-cpp for the
> golden) without its identifying first field, so those first fields are treated as **required** in these
> seeds. The seeds below therefore do **not** claim `55`/`38`/`448` are `required='Y'` in the XML — they are
> the mandatory *content* of a required component.

| Exemplar | Message-level `required='Y'` (`FIX44.xml`) | Component-required content | Group / nested seed | Optional (type-exercise) |
|---|---|---|---|---|
| **D** NewOrderSingle (`:326`) | `ClOrdID(11)`, `Side(54)`, `TransactTime(60)`, `OrdType(40)` | `Symbol(55)`←Instrument(`:352` req); `OrderQty(38)`←OrderQtyData(`:361` req) | — | `Price(44)` decimal |
| **8** ExecutionReport (`:115`) | `OrderID(37)`, `ExecID(17)`, `ExecType(150)`, `OrdStatus(39)`, `Side(54)`, `LeavesQty(151)`, `CumQty(14)`, `AvgPx(6)` | `Symbol(55)`←Instrument(`:152` req) | — | — (types covered) |
| **9** OrderCancelReject (`:255`) | `OrderID(37)`, `ClOrdID(11)`, `OrigClOrdID(41)`, `OrdStatus(39)`, `CxlRejResponseTo(434)` | — (group-free, no required component) | — | `CxlRejReason(102)` int |
| **E** NewOrderList (`:404`) | `ListID(66)`, `BidType(394)` int, `TotNoOrders(68)` int, `ListOrdGrp`→`NoOrders(73)` req (`:423`,`:2944`) | per order (`:2944`): `ClOrdID(11)`, `ListSeqNo(67)` int, `Side(54)`, `Symbol(55)`←Instrument, `OrderQty(38)`←OrderQtyData | `NoOrders(73)`→ optional `NoPartyIDs(453)`→ optional `NoPartySubIDs(802)`; per-party `PartyID(448)`,`PartyIDSource(447)`,`PartyRole(452)`; per-sub `PartySubID(523)`,`PartySubIDType(803)` | (nesting is the exercise) |
| **AS** AllocationReport (`:1901`) | `AllocReportID(755)`, `AllocTransType(71)`, `AllocReportType(794)`, `AllocStatus(87)`, `AllocNoOrdersType(857)`, `Side(54)`, `Quantity(53)`, `AvgPx(6)`, `TradeDate(75)` | `Symbol(55)`←Instrument(`:1923` req) | top-level optional `Parties`→`NoPartyIDs(453)`→`NoPartySubIDs(802)` (`:2508`,`:3701`); same per-party/per-sub tags as E | (multi-char `35=AS` + nesting are the exercise) |

**Delimiter tags** (the `delimiter_tag` arg to `group_begin`, = each group's first field in `FIX44.xml`):
`NoOrders(73)`→`ClOrdID(11)`; `NoPartyIDs(453)`→`PartyID(448)`; `NoPartySubIDs(802)`→`PartySubID(523)`.

**Type coverage across the set**: string (`11,55,37,…`), char (`54,40,150,39,71`), int (`394,68,67,102,794,87,857`),
decimal/Qty/Price (`38,44,151,14,6,53`), UTCTimestamp (`60`), LocalMktDate (`75`).

**AS optional-group note**: `AllocNoOrdersType(857)` is seeded `0` (*NotSpecified*) so QuickFIX golden
authoring does not treat `OrdAllocGrp NoOrders(73)` as conditionally required; the seeded nesting stays the
single `453→802` chain.

**Count-of-zero witness target**: the `NoXXX=0` present-but-empty case is exercised on an **optional** group —
`NoPartyIDs(453)` (`required='N'`) — NOT on E's `required='Y'` `NoOrders(73)`. This is kept distinct from the
"never-opened → emit nothing" case (an absent optional group emits no `NoXXX` tag at all; §1 / C3).

### 3.2 Seed record

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
- Consumed by `golden_diff.hpp::diff_transcripts` with a **061-specific `shape_oracle_profile()`** exclusion
  set — NOT the interop `default_normalization_tags()`. Because these are byte-exact *write shape-oracles*,
  the profile excludes **no business tags**: it excludes only the framing tags `{8,9,10,34,52}` (which a
  body-only golden never contains anyway), so seeded business fields including `TransactTime(60)` are matched
  verbatim. (`default_normalization_tags()` = `{9,10,34,52,60,112,122}` drops business tag `60` and is built
  for live-interop transcript diffing — the wrong calibration for a shape-oracle.) The `diff_transcripts`
  decimal comparison remains **by value** (it normalizes trailing zeros / representation).
- **Byte-exact canonical-decimal assertion (INV-3, pins format independently of the by-value golden):** the
  by-value golden diff and the read-back parse both compare decimals *by value*, so neither can catch a
  serialization-*format* defect (`1.9E2`, locale artifacts, non-canonical trailing zeros — all value-equal to
  `190.50`). Therefore each exemplar's round-trip witness MUST additionally assert, for **at least one decimal
  field**, that the emitted `<tag>=<ascii>\x01` bytes equal the canonical expected bytes exactly (e.g. D asserts
  the raw bytes of `44=190.50\x01`; 8 asserts `6=<avg_px>\x01`). This is a direct byte compare on the builder
  output, separate from `diff_transcripts`.

## 5. Read-scaffold (test support)

- `tests/support/app_message_read_scaffold.hpp`: `make_frame(begin_string, body) → bytes` and
  `parse_dict(bytes, dict, mr) → MessageView<Index>` using the **5-arg dict-backed** path. Reused by both
  the round-trip harness and the independent inbound-read witnesses.

## State transitions

None (stateless serialization + parse). The only "lifecycle" is `body_builder`'s open-group LIFO stack,
which MUST be empty at `commit()` (else typed error).
