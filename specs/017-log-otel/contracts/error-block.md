# Contract: error block — 7 `core::error` enumerators (slots 122–128) + C-ABI `[1000,1099]` mapping (FR-015, §6.3)

**Anchor**: `.specify/2k-log-otel.md` §6.3 + `[2i §1.1]` reserved-block layout. 017 introduces **exactly 7** new error enumerators.

**Representation decision (Gate A round 1, New 1 / option A).** `include/fixpp/core/error.hpp` is `enum class error : std::uint8_t` (max enumerator 255; highest live slot `session_unknown_acceptor_session = 121`), and `expected_t<T> = std::expected<T, error>`. Numeric values in `[1000,1099]` **cannot** be represented in that enum — and two of these errors are returned via `expected_t<void>` (`Logger::shutdown()`→`log_drain_timeout`; `TracerProvider`/`MeterProvider` ctor→`otel_provider_init_failed`), so they **must** be representable `core::error` enumerators. Therefore:

- The 7 new variants are added to `fixpp::core::error` at the **next free `std::uint8_t` slots 122–128** (append-only / non-renumbering per `[const §X.4]` — slot 121 is the current highest).
- The `[1000,1099]` integers from anchor §6.3 / `[2i §1.1]` are the **C-ABI `fixpp_error_t` mapping block** (`fixpp_error_t` is `typedef int`, `include/fix/c_api/decimal.h:24`) — a future v1.x C-ABI exposure. In v1.0 the C-ABI exposes **no** log/OTel symbols (FR-020), so these integers are an occupancy/mapping-table reservation only; nothing consumes them at the C-ABI boundary in v1.0.

The anchor §6.3 table is mislabeled ("`fixpp::core::error` variant | `fixpp_error_t` value" — line 1121): the second column is the C-ABI numeric block, not the C++ enum value. This contract resolves the mislabel without a 2k/2i amendment (anchor §6.3's own note already says slot 1000 "maps to no `fixpp_error_t` return path" in v1.0).

## Variants to add to `fixpp::core::error` (`include/fixpp/core/error.hpp`)
| Enumerator (`fixpp::core::error`) | `core::error` slot (uint8_t) | C-ABI `fixpp_error_t` map value | Surface / semantics |
|---|---|---|---|
| `log_queue_overflow` | 122 | 1000 | Internal C++ only — macros are `void`; counter via `Logger::drop_count()`; the C-ABI slot maps to no return path in v1.0. |
| `log_sink_open_failed` | 123 | 1001 | `Sink::open()` failed at startup ⇒ sink disabled, Logger continues with remaining sinks. |
| `log_sink_write_failed` | 124 | 1002 | `Sink::emit()` threw (caught by drain) ⇒ `sink_error_count(i)`++. |
| `log_sink_flush_failed` | 125 | 1003 | `Sink::flush()` threw. |
| `log_drain_timeout` | 126 | 1004 | Returned via `expected_t<void>` from `Logger::shutdown(drain_timeout)` on timeout ⇒ bumps `timeout_drop_count()` (NOT `drop_count()`). |
| `otel_export_failed` | 127 | 1010 | OTLP trace/metric/log batch export failure ⇒ internal counter; engine continues. |
| `otel_provider_init_failed` | 128 | 1011 | `TracerProvider`/`MeterProvider` ctor failed ⇒ returned via `expected_t<void>`; no-op provider fallback. |

## Obligations
- **Exactly these 7 enumerators** are added to `fixpp::core::error` in v1.0. The completeness test asserts the **enumerator set** equals `{log_queue_overflow, log_sink_open_failed, log_sink_write_failed, log_sink_flush_failed, log_drain_timeout, otel_export_failed, otel_provider_init_failed}` at slots 122–128 — exact-SET equality, not subset, per `[[feedback_completeness_gate_exact_set_not_subset]]`.
- The error-string table (`error_message`/`to_string`) gets matching entries for the 7 enumerators.
- The C-ABI `[1000,1099]` occupancy (1000/1001/1002/1003/1004/1010/1011; 1005–1009 + 1012–1099 reserved) is recorded in `tools/abi_history/error_codes_v1.txt` (append-only) as the future `fixpp_error_t` mapping; it is an abidiff / `tools/check_capi_occupancy.sh` concern (`[const §X.4]`), **not** a `core/error.hpp` enumerator value. `c_api/log.h` + `c_api/otel.h` expose **no** symbols (FR-020).
- No reentrancy annotation needed in v1.0 (no C-ABI symbol consumes these yet).
- The bundle MUST NOT instruct adding variants "at 1000–1011" to `core/error.hpp` — that is a non-compile.
