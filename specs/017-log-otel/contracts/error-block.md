# Contract: error block [1000, 1099] (FR-015, §6.3)

**Anchor**: `.specify/2k-log-otel.md` §6.3 + `[2i §1.1]` reserved-block layout. 017 owns the `[1000,1099]` block and introduces **exactly 7** variants; the block is currently free (highest used slot in `core/error.hpp` is 121).

## Variants to add to `fixpp::core::error` (`include/fixpp/core/error.hpp`)
| Variant | `fixpp_error_t` | Surface / semantics |
|---|---|---|
| `log_queue_overflow` | 1000 | Internal C++ only — macros are `void`; reserved slot maps to no C-ABI return in v1.0; counter via `Logger::drop_count()`. |
| `log_sink_open_failed` | 1001 | `Sink::open()` failed at startup ⇒ sink disabled, Logger continues with remaining sinks. |
| `log_sink_write_failed` | 1002 | `Sink::emit()` threw (caught by drain) ⇒ `sink_error_count(i)`++. |
| `log_sink_flush_failed` | 1003 | `Sink::flush()` threw. |
| `log_drain_timeout` | 1004 | Returned from `Logger::shutdown(drain_timeout)` on timeout ⇒ bumps `timeout_drop_count()` (NOT `drop_count()`). |
| `otel_export_failed` | 1010 | OTLP trace/metric/log batch export failure ⇒ internal counter; engine continues. |
| `otel_provider_init_failed` | 1011 | `TracerProvider`/`MeterProvider` ctor failed ⇒ no-op provider fallback; returned as `expected_t<void>` failure. |

## Obligations
- **Exactly these 7 slots** consumed in v1.0; 1005–1009 and 1012–1099 remain reserved (a completeness test asserts the block contains exactly this set — exact-SET equality, not subset, per `[[feedback_completeness_gate_exact_set_not_subset]]`).
- The error-string table (`error_message`/`to_string`) and any `fixpp_error_t` C-ABI mapping table get matching entries; `c_api/log.h` + `c_api/otel.h` still expose **no** symbols (slot 1000 is reserved for a future v1.x `fixpp_logger_drop_count()` but maps to no return path now).
- No reentrancy annotation needed in v1.0 (no C-ABI symbol consumes these yet).
