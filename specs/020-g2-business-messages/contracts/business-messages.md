# Contract: Typed Business-Message Builders (`session/business_messages.hpp`)

Public, `noexcept`, allocation-free builders that turn typed minimal fields into the FIX application **body** consumed by `Engine::send`. Mirrors the `admin_messages.hpp` build contract. Read is via the generated `fixpp::v44` flyweights (separate, already-shipped contract).

## Conventions (shared with `admin_messages.hpp`)

- Output is written into the caller-supplied `std::span<std::byte> out` via `wire::Writer`; returns `expected_t<std::span<std::byte>>` covering the written body. No heap allocation.
- The builder emits the **app body only**: leads with `35=MsgType`, then business fields. It emits **no** `8/9/34/49/52/56` (engine-stamped) and **no** `10=` trailer.
- Numeric fields are `core::Decimal`, serialized with `Decimal::format` (canonical, locale-independent).
- All required fields must be supplied; a build that cannot emit a complete, valid body returns `std::unexpected(error::…)` and writes nothing usable.

## `build_new_order_single`

```cpp
// NewOrderSingle (35=D), catalogue A-001. Limit-only (OrdType fixed to 2).
// Emits: 35=D 11=<cl_ord_id> 55=<symbol> 54=<side> 38=<order_qty>
//        40=2 44=<price> 60=<transact_time>
// side: '1'=Buy '2'=Sell. transact_time: pre-formatted UTCTimestamp.
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>> build_new_order_single(
    std::span<std::byte> out,
    std::string_view cl_ord_id,
    std::string_view symbol,
    char side,
    const fixpp::core::Decimal& order_qty,
    const fixpp::core::Decimal& price,
    std::string_view transact_time) noexcept;
```

- Returns `error::wire_frame_too_large` if `out` is too small (parity with `build_logon`).
- Required: all parameters; empty `cl_ord_id`/`symbol`/`transact_time` or an out-of-range `side` is rejected (typed error, no emission).

## `build_execution_report`

```cpp
// ExecutionReport (35=8), catalogue A-006.
// Emits: 35=8 37=<order_id> 17=<exec_id> 150=<exec_type> 39=<ord_status>
//        55=<symbol> 54=<side> 151=<leaves_qty> 14=<cum_qty> 6=<avg_px>
// Round-trip fully-filled: exec_type='F', ord_status='2', leaves_qty=0,
//   cum_qty=order_qty, avg_px=price (caller-supplied).
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>> build_execution_report(
    std::span<std::byte> out,
    std::string_view order_id,
    std::string_view exec_id,
    char exec_type,
    char ord_status,
    std::string_view symbol,
    char side,
    const fixpp::core::Decimal& leaves_qty,
    const fixpp::core::Decimal& cum_qty,
    const fixpp::core::Decimal& avg_px) noexcept;
```

- Same error/required contract.

## Send-path obligation (D1 / INV-1)

The send path (`Session::send_impl` via `Engine::send`) MUST place the body's leading `35=MsgType` field at **wire field-3** position (after `9=BodyLength`), ahead of the engine-stamped `49/56/34/52`. The builders rely on this: they emit `35=` first in the body and the send path hoists it. Asserted by a unit regression that parses the produced wire bytes and checks field-3 == MsgType.

## Read contract (consumed, not added)

```cpp
// On fromApp delivery (MessageView<Index> msg):
fixpp::v44::NewOrderSingle nos{msg};
auto id    = nos.cl_ord_id();          // expected_t<string_view>
auto sym   = nos.symbol();             // expected_t<string_view>
auto px    = nos.price(mr);            // expected_t<decimal_t>
// …
fixpp::v44::ExecutionReport er{msg};
auto et    = er.exec_type();           // expected_t<char> ('F')
auto cum   = er.cum_qty(mr);           // expected_t<decimal_t>
```

Each accessor returns a typed `expected_t`; a missing/ill-typed required field surfaces as the accessor's error (FR-006).
