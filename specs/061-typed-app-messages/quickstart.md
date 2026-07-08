# Quickstart: 061-slim exemplar builders + witness harness

## Build an outbound body (flat exemplar)

```cpp
#include <fixpp/session/business_messages.hpp>

std::byte out[512];
auto body = fixpp::session::build_order_cancel_reject(
    out, /*order_id=*/"ORD1", /*cl_ord_id=*/"C1",
    /*orig_cl_ord_id=*/"C0", /*ord_status=*/'8', /*cxl_rej_response_to=*/'1');
// body -> span of "35=9\x0137=ORD1\x01...": body-only, no 8/9/34/49/52/56/10.
// On invalid input or short buffer: body.error() set, `out` untouched.
```

## Build a grouped/nested body (the pivotal E exemplar) via `wire::body_builder`

```cpp
#include <fixpp/wire/body_builder.hpp>

fixpp::wire::body_builder b{"E"};                 // 35=E
b.field(66, "LIST1");                              // ListID
auto orders = b.group_begin(73);                   // NoOrders
{
  auto e0 = orders->add_entry();
  e0.set_string(11, "C1"); e0.set_string(55, "AAPL"); e0.set_char(54, '1');
  auto parties = e0.group_begin(453);              // NoPartyIDs (nested)
  {
    auto p0 = parties->add_entry();
    p0.set_string(448, "BROKER"); p0.set_char(447, 'D');
    auto subs = p0.group_begin(802);               // NoPartySubIDs (nested-in-nested)
    subs->add_entry().set_string(523, "DESK");
    p0.group_end(*subs);
  }
  e0.group_end(*parties);
}
b.group_end(*orders);

std::byte out[1024];
auto body = b.commit(out);                          // fails closed if any group left open
```

## Read it back (round-trip, dict-backed path)

```cpp
#include "support/app_message_read_scaffold.hpp"

auto frame = make_frame("FIX.4.4", body.value());
auto mv    = parse_dict(frame, fix44_dict(), &mr);  // 5-arg dict-backed parse
fixpp::v44::NewOrderList nol{mv};
assert(nol.orders()[0].parties()[0].party_id().value() == "BROKER");
assert(nol.orders()[0].parties()[0].party_sub_ids()[0].party_sub_id().value() == "DESK");
```

## Verify against the external golden

```cpp
#include "interop/support/golden_diff.hpp"
auto expected = load_golden("tests/session/golden/new_order_list.fix");   // body-only, QuickFIX-authored
auto diff = diff_transcripts(expected, body.value(), default_normalization_tags());
assert(diff.status == DiffStatus::Match);   // decimal-by-value; non-deterministic tags excluded
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
