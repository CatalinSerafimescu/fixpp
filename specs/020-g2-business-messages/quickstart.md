# Quickstart: typed NewOrderSingle → ExecutionReport

End-to-end use of the G2 surface: build a typed order, send it via `Engine::send`, and read the typed reply at `fromApp`.

## 1. Send a NewOrderSingle (initiator side)

```cpp
#include <fixpp/session/business_messages.hpp>
#include <fixpp/core/decimal_alias.hpp>   // fixpp::decimal_t

using fixpp::decimal_t;

// A decimal_t is built by PARSING a byte span with a memory resource — there is
// no parse(string-literal) overload. Helper unwrapping the expected_t:
auto dec = [](std::string_view s, std::pmr::memory_resource* mr) -> decimal_t {
    auto bytes = std::as_bytes(std::span{s.data(), s.size()});
    return *decimal_t::parse(bytes, mr);   // production code must check the expected_t
};

std::pmr::monotonic_buffer_resource arena;  // caller-owned arena
std::array<std::byte, 512> buf;
auto body = fixpp::session::build_new_order_single(
    buf,
    /*cl_ord_id   */ "ORD001",
    /*symbol      */ "AAPL",
    /*side        */ '1',                       // Buy
    /*order_qty   */ dec("100", &arena),
    /*price       */ dec("190.50", &arena),
    /*transact_time*/ "20260604-12:00:00.000");
if (!body) { /* handle error::… (e.g. invalid field / too-small buffer) */ }

// Send through the 019 any-thread entry point; the engine validates the opaque
// payload (one leading 35=, no embedded session tags — FR-016), stamps the
// session header, places MsgType(35) in field-3 position, and writes a
// digit-only BodyLength (D1 / FR-004a).
co_await engine.send(initiator_id, *body);
```

## 2. Respond with a fully-filled ExecutionReport (acceptor Application)

```cpp
// Inside Application::fromApp, on receiving a NewOrderSingle. NOTE: calling
// Engine::send from inside fromApp is RE-ENTRANT (send-from-callback); it relies
// on 019's any-thread Engine::send (exec-hop + keepalive). This responder is a
// TEST FIXTURE, not a shipped production example (E3); the re-entrant path is
// proven deadlock/UAF-free by a named loopback test (INV-7) before live cells.
fixpp::v44::NewOrderSingle nos{msg};
auto sym = nos.symbol();           // expected_t<string_view>
auto qty = nos.order_qty(&arena);  // expected_t<decimal_t> (caller-supplied arena mr)
auto px  = nos.price(&arena);      // expected_t<decimal_t>

std::array<std::byte, 512> rbuf;
auto er = fixpp::session::build_execution_report(
    rbuf,
    /*order_id  */ "EX-ORD001",
    /*exec_id   */ "EXEC001",
    /*exec_type */ 'F',            // Trade (the FIX 4.4 fill wire value)
    /*ord_status*/ '2',            // Filled
    /*symbol    */ *sym,
    /*side      */ *nos.side(),
    /*leaves_qty*/ dec("0", &arena),
    /*cum_qty   */ *qty,           // fully filled → CumQty = OrderQty
    /*avg_px    */ *px);           // AvgPx = Price
co_await engine.send(acceptor_id, *er);
```

## 3. Read the ExecutionReport (initiator's fromApp)

```cpp
fixpp::v44::ExecutionReport rpt{msg};
auto status = rpt.ord_status();      // expected_t<char> → '2' (Filled)
auto cum    = rpt.cum_qty(&arena);   // expected_t<decimal_t> → 100   (parse uses caller arena)
auto avg    = rpt.avg_px(&arena);    // expected_t<decimal_t> → 190.50
// Compare numerics by decimal_t value-equality (a counterparty may emit 190.5 vs 190.50).
```

## Notes

- The builder emits the **body only** (`35=D`/`35=8` + business fields); `8/9/34/49/52/56/10` are engine-stamped — never include them. On error the returned span is absent and the output buffer is unspecified — do not inspect it.
- Numeric fields are `fixpp::decimal_t`; serialization is canonical and locale-independent and the read path materializes decimals via `decimal_t::parse(bytes, mr)` into your caller-supplied arena (compare by value-equality).
- Order type is **Limit** (`40=2`) in v1.0; Market and full-field coverage are deferred (FR-015a). FIX 4.2/5.0SP2/FIXT.1.1 are deferred and scheduled post-v1.0 (FR-015b).
- A malformed inbound business message you reject from `fromApp` produces a `BusinessMessageReject(35=j)` (019/A-014); the session survives.
