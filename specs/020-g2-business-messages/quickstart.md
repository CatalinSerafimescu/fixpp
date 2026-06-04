# Quickstart: typed NewOrderSingle → ExecutionReport

End-to-end use of the G2 surface: build a typed order, send it via `Engine::send`, and read the typed reply at `fromApp`.

## 1. Send a NewOrderSingle (initiator side)

```cpp
#include <fixpp/session/business_messages.hpp>
#include <fixpp/core/decimal.hpp>

using fixpp::core::Decimal;

std::array<std::byte, 512> buf;
auto body = fixpp::session::build_new_order_single(
    buf,
    /*cl_ord_id   */ "ORD001",
    /*symbol      */ "AAPL",
    /*side        */ '1',                       // Buy
    /*order_qty   */ Decimal::parse("100"),
    /*price       */ Decimal::parse("190.50"),
    /*transact_time*/ "20260604-12:00:00.000");
if (!body) { /* handle error::… */ }

// Send through the 019 any-thread entry point; the engine stamps the session
// header and places MsgType(35) in field-3 position (D1).
co_await engine.send(initiator_id, *body);
```

## 2. Respond with a fully-filled ExecutionReport (acceptor Application)

```cpp
// Inside Application::fromApp, on receiving a NewOrderSingle:
fixpp::v44::NewOrderSingle nos{msg};
auto sym = nos.symbol();           // expected_t<string_view>
auto qty = nos.order_qty(mr);      // expected_t<decimal_t>
auto px  = nos.price(mr);          // expected_t<decimal_t>

std::array<std::byte, 512> rbuf;
auto er = fixpp::session::build_execution_report(
    rbuf,
    /*order_id  */ "EX-ORD001",
    /*exec_id   */ "EXEC001",
    /*exec_type */ 'F',            // Filled (FIX 4.4)
    /*ord_status*/ '2',            // Filled
    /*symbol    */ *sym,
    /*side      */ *nos.side(),
    /*leaves_qty*/ Decimal::parse("0"),
    /*cum_qty   */ *qty,           // fully filled → CumQty = OrderQty
    /*avg_px    */ *px);           // AvgPx = Price
co_await engine.send(acceptor_id, *er);
```

## 3. Read the ExecutionReport (initiator's fromApp)

```cpp
fixpp::v44::ExecutionReport rpt{msg};
auto status = rpt.ord_status();    // expected_t<char> → '2' (Filled)
auto cum    = rpt.cum_qty(mr);     // expected_t<decimal_t> → 100
auto avg    = rpt.avg_px(mr);      // expected_t<decimal_t> → 190.50
```

## Notes

- The builder emits the **body only** (`35=D`/`35=8` + business fields); `8/9/34/49/52/56/10` are engine-stamped — never include them.
- Numeric fields are `core::Decimal`; serialization is canonical and locale-independent (round-trips exactly).
- Order type is **Limit** (`40=2`) in v1.0; Market and full-field coverage are deferred (FR-015a). FIX 4.2/5.0SP2/FIXT.1.1 are deferred and scheduled post-v1.0 (FR-015b).
- A malformed inbound business message you reject from `fromApp` produces a `BusinessMessageReject(35=j)` (019/A-014); the session survives.
