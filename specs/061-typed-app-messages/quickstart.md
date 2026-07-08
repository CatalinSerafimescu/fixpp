# Quickstart: 061-slim exemplar builders + witness harness

## Build an outbound body (flat exemplar)

```cpp
#include <fixpp/session/business_messages.hpp>

std::byte out[512];
auto body = fixpp::session::build_order_cancel_reject(
    out, /*order_id=*/"ORD1", /*cl_ord_id=*/"C1",
    /*orig_cl_ord_id=*/"C0", /*ord_status=*/'8', /*cxl_rej_response_to=*/'1',
    /*cxl_rej_reason=*/1);
// body -> span of "35=9\x0137=ORD1\x01...": body-only, no 8/9/34/49/52/56/10.
// On invalid input or short buffer: body.error() set, `out` untouched.
```

## Build a grouped/nested body (the pivotal E exemplar) via `wire::body_builder`

`TRY(x)` below is shorthand for the project's expected-unwrap (propagate the error, else bind the value);
in a test sample use explicit `.value()`. Every `group_begin` carries its **delimiter tag** (the group's
first field), which `commit()` enforces (INV-5). The seed is the required-field-complete E set (§3.1):
root `66,394,68`, per-order `11,67,55,54,38`.

```cpp
#include <fixpp/wire/body_builder.hpp>

fixpp::wire::body_builder b{"E"};                  // 35=E
b.field(66, "LIST1");                              // ListID   (required)
b.field(394, std::int64_t{2});                     // BidType  (required, int)
b.field(68,  std::int64_t{1});                     // TotNoOrders (required, int)
auto orders = TRY(b.group_begin(73, /*delim=*/11)); // NoOrders, delimiter ClOrdID(11)
{
  auto e0 = TRY(orders.add_entry());
  TRY(e0.set_string(11, "C1"));                    // ClOrdID  (delimiter — must be first)
  TRY(e0.set_int(67, 1));                          // ListSeqNo (required, int)
  TRY(e0.set_string(55, "AAPL"));                  // Symbol   (Instrument content)
  TRY(e0.set_char(54, '1'));                       // Side     (required)
  TRY(e0.set_decimal(38, decimal_t{"100"}));       // OrderQty (OrderQtyData content)
  auto parties = TRY(e0.group_begin(453, /*delim=*/448)); // NoPartyIDs, delimiter PartyID(448)
  {
    auto p0 = TRY(parties.add_entry());
    TRY(p0.set_string(448, "BROKER"));             // PartyID (delimiter)
    TRY(p0.set_char(447, 'D'));                    // PartyIDSource
    TRY(p0.set_int(452, 1));                       // PartyRole
    auto subs = TRY(p0.group_begin(802, /*delim=*/523)); // NoPartySubIDs, delimiter PartySubID(523)
    { auto s0 = TRY(subs.add_entry()); TRY(s0.set_string(523, "DESK")); TRY(s0.set_int(803, 2)); }
    TRY(p0.group_end(subs));
  }
  TRY(e0.group_end(parties));
}
TRY(b.group_end(orders));

std::byte out[1024];
auto body = b.commit(out).value();                  // fails closed: any group left open, empty
                                                    // instance, or wrong delimiter-first field
```

## Read it back (round-trip, dict-backed path)

```cpp
#include "support/app_message_read_scaffold.hpp"

auto frame = make_frame("FIX.4.4", body);
auto mv    = parse_dict(frame, fix44_dict(), &mr);  // 5-arg dict-backed parse
fixpp::v44::NewOrderList nol{mv};
assert(nol.orders()[0].party_i_ds()[0].party_id().value() == "BROKER");
assert(nol.orders()[0].party_i_ds()[0].party_sub_i_ds()[0].party_sub_id().value() == "DESK");
```

## Verify against the external golden

```cpp
#include "interop/support/golden_diff.hpp"
auto expected = load_golden("tests/session/golden/new_order_list.fix");   // body-only, QuickFIX-authored
// 061-specific profile: excludes NO business tags (only framing {8,9,10,34,52}); NOT
// default_normalization_tags(), which drops business tag 60 (TransactTime).
auto diff = diff_transcripts(expected, body, shape_oracle_profile());
assert(diff.status == DiffStatus::match);   // decimal-by-value; framing tags only excluded
```

## Byte-exact canonical-decimal check (INV-3, format pinned independently)

The golden diff and the parse round-trip both compare decimals *by value*, so neither catches a wrong
canonical *format* (`1.9E2` == `190.50` by value). Assert the raw bytes of ≥1 decimal field directly:

```cpp
// D exemplar: Price(44) seeded 190.50 → canonical bytes "44=190.50\x01" appear verbatim.
auto d_body = fixpp::session::build_new_order_single(out, /*…seed…*/).value();
assert(contains_bytes(d_body, "44=190.50\x01"));   // exact bytes, no scientific notation / locale drift
```

## Run the harness

```bash
# from the library submodule, linux-clang-debug preset (built via the Article XVII §7 local gate)
ctest --preset linux-clang-debug -R "exemplar_(read|roundtrip)"
```

## Add a new exemplar (pattern)
1. Add the seed record (fields + group shape + golden path) to the seed table.
2. Author the builder on `body_builder` (representative shape-oracle coverage).
3. Author the golden once via QuickFIX-cpp (offline), strip to body-only, record provenance.
4. The table-driven harness auto-covers read-back + golden-diff for the new seed.
