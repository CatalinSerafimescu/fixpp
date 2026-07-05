# Quickstart: Reading Repeating-Group Entries (062)

After 062, typed reads of repeating-group entries compile and return correct per-instance values.

## One-level group (e.g. NewOrderList orders)

```cpp
auto mv  = parse_frame(bytes, &arena);        // MessageView<Index>
fixpp::v44::NewOrderList nol{mv};
auto orders = nol.orders();                    // group_view<G_73>
for (std::size_t i = 0; i < orders.size(); ++i) {
    auto o = orders[i];
    EXPECT_EQ(*o.cl_ord_id(), expected_clordid[i]);
    EXPECT_EQ(*o.side(),      expected_side[i]);
    EXPECT_EQ(*o.order_qty(&arena), expected_qty[i]);
}
// One-level scalar reads: no per-entry sub-index, zero allocation (FR-004a).
```

## Nested group (e.g. MassQuote NoQuoteSets -> NoQuoteEntries)

```cpp
fixpp::v44::MassQuote mq{mv};
auto sets = mq.quote_sets();                   // group_view over NoQuoteSets
for (auto set : sets) {
    EXPECT_EQ(*set.quote_set_id(), ...);       // one-level scalar
    for (auto qe : set.quote_entries()) {      // nested descent (lazy, cached-once)
        EXPECT_EQ(*qe.bid_px(&arena), ...);    // per-instance nested field
        EXPECT_EQ(*qe.offer_px(&arena), ...);
    }
}
// Nested descent builds one cached sub-view per (group, instance); repeat reads reuse it.
```

## Empty group

```cpp
auto g = msg.some_group();
EXPECT_EQ(g.size(), 0u);
EXPECT_TRUE(g.begin() == g.end());             // no entry dereferenced
```

## What breaks if the fix regresses
Instantiating `orders[0].cl_ord_id()` on a generated flyweight fails to compile (or the regression-guard test fails) — the exact symptom the fix removes.
