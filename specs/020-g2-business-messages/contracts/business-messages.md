# Contract: Typed Business-Message Builders (`session/business_messages.hpp`)

Public, `noexcept`, allocation-free builders that turn typed minimal fields into the FIX application **body** consumed by `Engine::send`. Mirrors the `admin_messages.hpp` build contract. Read is via the generated `fixpp::v44` flyweights (separate, already-shipped contract).

## Conventions (shared with `admin_messages.hpp`)

- Output is written into the caller-supplied `std::span<std::byte> out` via `wire::Writer`; returns `expected_t<std::span<std::byte>>` covering the written body. No heap allocation (builder write path is stack/no-heap; an alloc-discipline witness — `counting_resource` over the builder — confirms it).
- **Atomicity**: the builder builds into a **local stack scratch buffer** and copies into the caller `out` only on full success. On any failure the returned span is **absent** (`std::unexpected`) and `out`'s contents are **unspecified** — the caller MUST NOT inspect `out`.
- The builder emits the **app body only**: leads with `35=MsgType`, then business fields. It emits **no** `8/9/34/49/52/56` (engine-stamped) and **no** `10=` trailer. (INV-2; named test scans the body for these tags and asserts absence.)
- Numeric fields are `const fixpp::decimal_t&` (`= core::decimal<FIXPP_DECIMAL_T>`), serialized with `decimal_t::format(std::span<std::byte>)` (canonical, locale-independent). There is no `parse(string-literal)`; a `decimal_t` is constructed via `decimal_t::parse(std::span<const std::byte>, std::pmr::memory_resource*)`.
- All required fields must be supplied with a **valid value**; a build with an empty required string, an out-of-range enum char, an invalid/unformattable decimal, an ill-formed UTCTimestamp, or a too-small `out` returns `std::unexpected(error::…)` and writes nothing usable. (Omission is impossible — positional signatures make it a compile error.)

## `build_new_order_single`

```cpp
// NewOrderSingle (35=D), catalogue A-001. Limit-only (OrdType fixed to 2).
// Emits: 35=D 11=<cl_ord_id> 55=<symbol> 54=<side> 38=<order_qty>
//        40=2 44=<price> 60=<transact_time>
// side: '1'=Buy '2'=Sell. transact_time: caller-pre-formatted UTCTimestamp (builder validates length+shape).
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>> build_new_order_single(
    std::span<std::byte> out,
    std::string_view cl_ord_id,
    std::string_view symbol,
    char side,
    const fixpp::decimal_t& order_qty,
    const fixpp::decimal_t& price,
    std::string_view transact_time) noexcept;
```

- Returns `error::wire_frame_too_large` if `out` is too small (parity with `build_logon`).
- Required: all parameters with valid values; empty `cl_ord_id`/`symbol`, an out-of-range `side`, an unformattable `order_qty`/`price` decimal, or an ill-formed `transact_time` UTCTimestamp (FR-008 validation) is rejected (typed error, no emission, `out` unspecified).

## `build_execution_report`

```cpp
// ExecutionReport (35=8), catalogue A-006.
// Emits: 35=8 37=<order_id> 17=<exec_id> 150=<exec_type> 39=<ord_status>
//        55=<symbol> 54=<side> 151=<leaves_qty> 14=<cum_qty> 6=<avg_px>
// Round-trip fully-filled: exec_type='F' (Trade, the 4.4 fill wire value), ord_status='2', leaves_qty=0,
//   cum_qty=order_qty, avg_px=price (caller-supplied).
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>> build_execution_report(
    std::span<std::byte> out,
    std::string_view order_id,
    std::string_view exec_id,
    char exec_type,
    char ord_status,
    std::string_view symbol,
    char side,
    const fixpp::decimal_t& leaves_qty,
    const fixpp::decimal_t& cum_qty,
    const fixpp::decimal_t& avg_px) noexcept;
```

- Same error/required/atomicity contract (empty `order_id`/`exec_id`/`symbol`, out-of-range `exec_type`/`ord_status`/`side`, unformattable decimal, or too-small `out` → typed error, no emission).

## Send-path obligation (D1 / INV-1 / INV-8 / FR-004a / FR-016)

The send path (`Session::send_impl` via `Engine::send`) MUST, for application messages:
1. Place the body's leading `35=MsgType` field at **wire field-3** position (after `9=BodyLength`), ahead of the engine-stamped `49/56/34/52`. The builders rely on this: they emit `35=` first in the body and the send path hoists it.
2. Emit **digit-only / unpadded** `9=BodyLength` (no leading zeros — `.specify/2b-wire.md`; the current `send_impl` emits `9=000045`, a contract violation). Recommended `/implement`: route app sends through `wire::Writer` (whose `commit()` already produces field-3 MsgType + digit-only `9=` + checksum) or correct the manual framer.
3. Emit a valid `10=CheckSum`.
4. **Validate the opaque payload fail-closed BEFORE seqnum assignment/store** (this applies to all 019 opaque callers, not just these builders): exactly **one** leading `35=`; reject empty payload, duplicate `35=`, or any embedded session header/trailer tag (`8/9/34/49/52/56/10`) with `std::unexpected(error::app_payload_malformed)` (slot **131**) — **no transmit, no seqnum consumption**.

Asserted by unit regressions on the **captured `transport_send` / stored frame bytes** (not the `fromApp` value): field-3 == MsgType + unpadded `9=` + valid checksum (INV-1, RED before fix); and opaque-payload rejection with no seqnum consumed (INV-8). This touches the proven 015/019 send path → real Gate B.

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
