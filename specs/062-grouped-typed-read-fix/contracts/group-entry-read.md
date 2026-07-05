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
- CONTRACT: returns the EXACT value for THIS entry's slice (per-instance, not the message's first occurrence).
- CONTRACT: absent field → the existing typed not-found error (never a defaulted value).
- CONTRACT (FR-004a): a scalar read builds no per-entry sub-index and performs no heap allocation.

## Read a NESTED group inside an entry

```cpp
auto sub = e.<nested_group>();     // group_view<G_c>, e.g. quoteSet.quote_entries()
for (auto ne : sub) {
    auto px = ne.bid_px(mr);       // per-instance nested field (e.g. MassQuote BidPx)
}
```
- CONTRACT: nested entries are read per-instance-typed, recursively (parity with QuickFIX C++/J).
- CONTRACT (FR-004): first descent into an instance builds one cached sub-view (arena-owned, parent lifetime); repeat descents reuse it — no per-access allocation.

## Lifetime & stability
- CONTRACT (INV-G1): an entry (and any nested group/entry from it) is valid only while the parent parsed message is alive.
- CONTRACT (FR-007): no change to top-level message field reads, the C-ABI, or the error enum.

## Regression guard (FR-006)
A test MUST instantiate `operator[]`/`iter()` on a GENERATED entry flyweight (not a hand-written stub) and read a field, so reverting the fix re-breaks the build/test.
