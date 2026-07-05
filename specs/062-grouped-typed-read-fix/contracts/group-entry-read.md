# Contract: Group-Entry Typed Read

Public read surface for repeating-group entries after 062. Caller-facing shape is unchanged from the (previously non-compiling) intended API; the fix makes it actually work.

## Enumerate a group

```cpp
auto grp = msg.<group>();          // group_view<G_n>, e.g. msg.orders()
std::size_t n = grp.size();        // occurrence count (NoXXX)
for (auto entry : grp) { ... }     // range-for / iter()
auto e = grp[i];                   // random access; e is a G_n by value
```
- CONTRACT: `grp[i]` and enumeration via `begin()/end()`/`iter()` yield identical entries in identical order (seam-#8).
- CONTRACT: empty group → `size()==0`, `begin()==end()`; no entry is dereferenced.

## Read a scalar field of an entry

```cpp
core::expected_t<std::string_view> s = e.cl_ord_id();   // string
core::expected_t<char>             c = e.side();         // char
core::expected_t<std::int32_t>     i = e.some_int();     // int
core::expected_t<decimal_t>        d = e.order_qty(mr);  // decimal (needs mr)
core::expected_t<field_view>       f = e.field_value(tag);
```
- CONTRACT: returns the EXACT value for THIS entry's slice (per-instance, not the message's first occurrence). Implemented by the span-scan field accessor (`get(span, tag, token) -> expected_t<field_view>`) over the entry's slice, carrying the parent generation token.
- CONTRACT: absent field → the existing typed not-found error (never a defaulted value).
- CONTRACT (FR-004a): a scalar read builds no per-entry sub-index and performs no heap allocation.
- LIMITATION (N3): the generic `field_value(tag)` escape hatch span-scans the whole entry slice — which includes any nested group's bytes — and returns the FIRST occurrence. A tag that lives ONLY inside a nested group is therefore returned as if it were an outer-entry field (mirrors whole-message `field_value` first-occurrence semantics). Typed accessors are codegen-scoped to the entry's own members and are unaffected. Documented as a future `behaviors-and-limitations.md` B-*/L-* row.

## Read a NESTED group inside an entry

```cpp
auto sub = e.<nested_group>();     // group_view<G_c>, e.g. quoteSet.quote_entries()
for (auto ne : sub) {
    auto px = ne.bid_px(mr);       // per-instance nested field (e.g. MassQuote BidPx)
}
```
- CONTRACT: nested entries are read per-instance-typed, **recursively** — the sub-view is built with the dict-aware `OffsetTable` ctor and yields entries carrying their own `entry_context` whose `outer_occurrence_id` is THAT child slice's own globally-unique `data`-pointer identity (ctx threads down the chain, `parent_cache_owner` stays the root cache owner), so depth-3/4 nesting and inner-extent correctness hold (parity with QuickFIX C++/J — e.g. MassQuote `NoQuoteSets → NoQuoteEntries → NoLegs → NoLegSecurityAltID`).
- CONTRACT (FR-004b): first descent into a stable outer occurrence builds ONE cached sub-view (arena-owned by the root OffsetTable, parent lifetime), keyed by `(unique_slice_identity, nested_no_tag)` where `unique_slice_identity` = the outer entry slice's **globally-unique `data` pointer** — distinct outer occurrences never collide **at any nesting depth** (every occurrence is carved in place from the one parent frame buffer, so its `data` is a distinct address); repeat descents on the same occurrence reuse it with no allocation. The `(no_tag + ordinal i)` key is DELETED (it collides across repeated outer occurrences — e.g. `QuoteSets[0]` vs `QuoteSets[1]` both keying `((NoQuoteEntries,0), NoLegs)` → silent wrong values).

## Lifetime & stability
- CONTRACT (INV-G1): an entry (and any nested group/entry from it) is valid only while the parent parsed message is alive.
- CONTRACT (FR-007): no change to top-level message field reads, the C-ABI, or the error enum.

## Regression guard (FR-006)
A test MUST instantiate `operator[]`/`iter()` on a GENERATED entry flyweight (not a hand-written stub) and read a field, so reverting the fix re-breaks the build/test.
