# Phase 0 Research: Typed Application Messages (061)

**Date**: 2026-07-05 | **Branch**: `061-typed-app-messages`

## Scope census (authoritative source: `spec/feature-catalogue.md` lines 134–204)

- **A — Order Management (13 rows, 13 messages)**: D, E, F, G, H, 8, 9, q, r, AF, AC, t, u.
- **M — Market Data / Ref Data / Quotation (12 rows, 17 distinct MsgTypes)**: V, W, X, Y, c, d, e, f, g, h, i, b, S, R, AG, Z, a. Note `35=b` is cited by both M-008 (MassQuoteAcknowledgement) and M-009 (QuoteAcknowledgement) — one flyweight, witnessed once.
- **P — Allocation (3 rows, 3 messages)**: J, P, AS.
- **Total: 33 distinct messages** across 28 rows.

**C-/R- families and P-004..008, N-001 are DEFERRED post-v1.0** per the §I.3 triage (`research/findings/v1-official-row-triage-2026-06-22.md`, the "DEFER-POST-V1.0 (81)" list — C(3), R(5), P(5), N(1)). Constitution §XVIII.7's mention of "C-/R- families" as v1.0 typed-message scope is **stale relative to the triage** (a later signed user decision, 2026-06-22, which governs). → Doc-hygiene: amend §XVIII.7 to drop C-/R- from v1.0 typed-scope (tracked as a close-out nit, not this feature's work). **N-002/003 remain the separate FSM-dispatch follow-on.**

## Decision: representative namespace per row

- **Decision**: one representative version namespace per row — **v44** for order-management (A) + allocation (P); **v42** for market-data (M).
- **Rationale**: the row-done clarification fixed one representative namespace (FR-015b all-version deferred). v44 contains every A/P message (A-001..007 are 4.0–5.0SP2, A-008..013 + P-003 are 4.4+, P-001/002 are 4.0/4.1+ → all present in v44). v42 contains every M message (all M rows include 4.2, and M-009/M-010 are 4.0+ → present in v42). This gives one consistent namespace per family with the flyweight guaranteed to exist.
- **Alternatives rejected**: (a) authority-tag-driven (mixed v42/v44/v50sp2 per the row's `[FIXxx]` spec-ref) — inconsistent within a family, no benefit; (b) all-three namespaces — that is the deferred FR-015b.
- **MUST verify per message** at implementation start: grep the chosen namespace's generated `Messages.hpp` for each flyweight (planning existence claims are unreliable; verify vs real headers). Spot-checked 2026-07-05: v44 has NewOrderList/OrderCancelRequest/AllocationInstruction/QuoteRequest/MassQuote; v42 has MarketDataRequest/NewOrderList/QuoteRequest/MassQuote.

## ⚠️ CRITICAL FINDING: typed repeating-group READ does not compile today

**Verified against source (not just the build tree):**
- `include/fixpp/wire/group_view.hpp:34-37` — `group_view<GroupT>::operator[](i)` returns `GroupT{ std::span<const std::byte>{ s.data, s.len } }` (and `iterator::operator*` → same, `:42`).
- `tools/codegen/fixpp-codegen/emit_messages.cpp:209-217` — every generated group-entry class `G_<no_tag>` emits ONLY `G_n() noexcept = default;` and `explicit G_n(MessageView<Index> const& v) noexcept : view_(&v) {}`. **There is no `std::span<std::byte>` constructor.**

**Consequence**: `group_view<G_n>::operator[]` / `begin()` / `*it` are ill-formed the moment they are ODR-instantiated on a generated flyweight. `msg.orders()` and `msg.orders().size()` compile (member functions of a class template instantiate lazily), but reading any group **entry field** (`msg.orders()[0].cl_ord_id()`) does **not** compile. Confirmed no existing test exercises it: `tests/wire/repeating_group_equivalence_test.cpp:39` uses a hand-written span-constructible `TestLeg`; `tests/codegen/typed_accessor_test.cpp:68-73` only calls `size()` and default-constructs an entry.

**Why it matters for 061**: the discriminating read/round-trip witnesses (FR-005/006) require typed group-entry field assertions for grouped messages. `group_view` today carries only `std::span<slice const>` + a generation token (`group_view.hpp:66-67`) — not the `memory_resource*` (and possibly dict/`group_member_fn`) needed to build a per-slice sub-`MessageView`. Fixing it means changing BOTH `group_view::operator[]` (construct an entry over a sub-view built from the slice + carried `mr`) AND the codegen entry-class contract in `emit_messages.cpp` (give `G_n` a matching ctor). This is a **wire-format/parser + codegen-layout change** (mandatory-trigger Appendix A) and a hard prerequisite for the grouped subset of the 33 messages. **Flat messages are unaffected.**

→ This is a scope fork surfaced to the user (see plan.md Summary / Complexity Tracking). Flat messages can ship now; grouped messages require this fix first.

## Repeating-group READ mechanics (once unblocked)

- Message-level accessor = a named group getter returning `wire::group_view<groups::G_<noTag>>`, e.g. `NewOrderList::orders()` → `view_.group<73, groups::G_73>()`. No separate count getter — the `NoXXX` count is `group_view::size()`.
- Groups are shared once in `namespace fixpp::v<ver>::groups`, keyed by NoTag, reused across messages (`G_453`/`G_627` recur).
- Entry flyweight per-field accessors return `core::expected_t<T>` (string→`string_view`, char, int32; decimal takes `memory_resource*`, e.g. `order_qty(mr)`). Nested groups exposed the same way on the entry.

## Repeating-group WRITE mechanics (hand-written builder)

- Existing builders (`src/session/business_messages.cpp:55-203`) are flat, body-only, using **TU-local `static`** helpers `wfield`/`wchar`/`wdecimal` (anonymous namespace) — **not reusable** from another TU as-is.
- A real group writer exists but is coupled to `wire::Writer` (`include/fixpp/wire/writer.hpp:102` `open_group`; `group_writer::append_field`), which always injects `8=`/`9=`/`10=` (full frame) — **no body-only mode**, so unusable by these body-only builders (noted at `business_messages.cpp:24-27`).
- **Conclusion**: a body-only grouped builder must emit `NoXXX=<count>\x01` (int→ASCII via `wfield`) then a manual per-entry loop of `wfield`/`wchar`/`wdecimal` in dictionary tag order. The helpers must be **lifted out of the anonymous namespace** (into a shared internal header) or duplicated so grouped builders can reuse them. C-ABI reference impl for group encoding: `src/capi/message_write.cpp` (`fixpp_group_builder`).

## Read-test frame scaffold

- `tests/session/test_business_messages_read.cpp`: `make_frame(body)` (`:43-59`) prepends `8=FIX.4.4\x01 9=<len>\x01`, appends `10=<sum%256,%03u>\x01` (`body` must start `35=<type>\x01`); `parse_frame(buf, mr)` (`:62-72`) drives the production `wire::Framer` and returns the **2-arg** `MessageView<Index>{frame, mr}` (no dict). Flyweight then `fixpp::v44::NewOrderSingle nos{mv};`.
- **No shared session-read helper header exists** — the pair is duplicated inline. Replicating ~33× should **factor a small shared test-support header** (`tests/support/`), parameterised by BeginString (v42 needs `8=FIX.4.2`).
- Grouped-witness caveat: group slices are cut by a first-field-reappearance heuristic in the `OffsetTable`; a dict-backed `Parser<Index>::parse` (5-arg `MessageView`) gives member-set-correct extents. `tests/support/frame_view_factory.hpp` (`make_frame_view`) + `Parser::parse` is the dict-capable path. Decide per grouped message whether the 2-arg heuristic slicing suffices or the dict path is needed — verify by reading `OffsetTable::group_slices` during the exemplar.

## Header-install mechanics (FR-007)

- Current install is a single header drop: `CMakeLists.txt:266-270` `install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/ ...)`. No `install(TARGETS/EXPORT)`, no package config.
- Generated headers live only at `${CMAKE_BINARY_DIR}/_codegen/include/fixpp/{v42,v44,v50sp2,vt11,_dispatch}/` (`cmake/Codegen.cmake:6-7,61`), attached to codegen INTERFACE targets via **BUILD_INTERFACE only** (`Codegen.cmake:264-276`), with an explicit "No install() rules — build-scaffolding" comment (`:258`).
- **Fix**: add `install(DIRECTORY "${CMAKE_BINARY_DIR}/_codegen/include/" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")` with **exclusions**: `PATTERN "_dispatch" EXCLUDE` (build-tree-private reify bridge) and an explicit keep/drop on `vt11` (FIXT.1.1, not part of the public v42/v44/v50sp2 set → exclude). No target-level `$<INSTALL_INTERFACE>` split needed (pure header drop, matching the existing model). Per-preset gotcha: install pulls from whichever preset is installed (no single canonical output dir) — acceptable; document it.

## decimal / timestamp helpers

- `decimal_t::format(std::span<std::byte> dst) -> expected_t<std::size_t>` (`include/fixpp/core/decimal.hpp:64`); existing `wdecimal` formats into a 64-byte stack buffer then copies (`business_messages.cpp:189-201`) — reuse verbatim.
- Existing builders take `transact_time` **pre-formatted** as `string_view`, shape-validated by TU-local `is_valid_utc_timestamp` (17/21 chars, `business_messages.cpp:130-155`). If a builder must PRODUCE a UTCTimestamp: `fixpp::core::utc_time_to_fix_string(tp, …, span<char>)` (`include/fixpp/core/fix_time.hpp:68-89`).
