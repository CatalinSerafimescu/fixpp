# Phase 1 Data Model — C ABI Feature A

The C ABI is a contract surface, so the "entities" are types, code spaces, and the mapping between the C++ error domain and the published C codes. All values are normative-sourced from `[2i §4.2/§4.3/§4.5]` + `include/fixpp/core/error.hpp`.

## E-1 — Opaque handle catalogue (CA-001)

Five incomplete (forward-declared) structs, `fixpp_*_t` naming per `[2i §4.2]`. Declared in `include/fix/c_api/handles.h`; concrete definitions are engine-internal and arrive with the owning feature.

Ownership / destroy is deferred wholesale to `[2i §4.2.1]` (the ownership table is copied verbatim below; do not restate a simplified variant):

| Handle | Owning? | Lifetime | Destroyed by |
|--------|---------|----------|--------------|
| `fixpp_engine_t`  | Owning | Engine lifetime | `fixpp_engine_destroy(engine)` (CA-005, Feature B / `[2j]`) |
| `fixpp_session_t` | Non-owning observer (engine owns the underlying `Session*`) | Bounded by engine + session-open/-close cycle | `fixpp_session_close(session)` then handle becomes invalid (Feature B) |
| `fixpp_msg_t`     | Non-owning observer of a wire flyweight | `fromApp` callback dispatch window (inbound) / commit cycle (outbound) | Engine destroys at parse-window close (inbound); `fixpp_msg_destroy(msg)` for outbound (Feature C) |
| `fixpp_dict_t`    | Owning (refcounted via shared_ptr) | Engine lifetime (registered into engine config) | `fixpp_dict_destroy(dict)` (Feature C / `[2c]`) |
| `fixpp_store_t`   | Non-owning observer of a session-owned store per `[2e §6.7]` N1 | Bounded by session lifetime | No separate destroy — becomes invalid when `fixpp_session_close(session)` returns (`[2e]`) |

**Feature A deliverable**: the forward typedefs + the per-handle **destroy/invalidation discipline** doc copied from `[2i §4.2.1]` (handles with a `*_destroy` — engine, dict, outbound msg — have idempotent, NULL-safe, never-throwing destroy; `session` is closed via the lifecycle `fixpp_session_close` then invalidates, not a `*_destroy`; `store` has no destroy at all — it invalidates when its session closes) + the null/invalid handle code contract (E-3 rows 3,4). No create/destroy *functions* here.

**Validation rules**: every (future) handle-taking function checks `NULL` first → `FIXPP_ERR_NULL_HANDLE`; a destroyed/corrupted handle → `FIXPP_ERR_INVALID_HANDLE` (`[2i §4.2.1]`). Feature A only publishes the codes + documents the rule.

## E-2 — `fixpp_error_t` master code space (CA-002)

Typedef `int32_t` (NOT a C `enum`; constants via `#define` so storage size is ABI-stable). Per-domain reserved blocks, verbatim from `[2i §4.3]`:

| Block | Range | Owner | Codes defined now |
|-------|-------|-------|-------------------|
| Cross-cutting | 0–99 | 2i | `OK`=0, `CANCELLED`=1, `UNKNOWN`=2, `NULL_HANDLE`=3, `INVALID_HANDLE`=4, `VERSION_MISMATCH`=5, `BUFFER_TOO_SMALL`=6, `TYPE_MISMATCH`=7, `TAG_NOT_FOUND`=8, `INDEX_OUT_OF_RANGE`=9, `CAPI_CONFIG_INVALID`=10 |
| Wire | 100–199 | 2b | `WIRE_INVALID_FRAME`=100, `WIRE_LIMIT_EXCEEDED`=101, `WIRE_CONFORMANCE`=102 |
| Dict | 200–299 | 2c | `DICT_CONFIG`=200, `DICT_LIMIT_EXCEEDED`=201, `DICT_OOM`=202 |
| Threading | 300–399 | 2d | `THREAD_CONFIG`=300, `THREAD_SESSION_LIFECYCLE`=301, `THREAD_RUNTIME`=302 |
| Store | 400–499 | 2e | `STORE_RUNTIME`=400, `STORE_CONSISTENCY`=401, `STORE_CONFIG`=402, `STORE_VISITOR`=403 |
| Sync | 500–599 | 2f | `SYNC_RUNTIME`=500 |
| TLS | 600–699 | 2g | `TLS_CONFIG`=600, `TLS_HANDSHAKE`=601, `TLS_PINSET`=602, `TLS_RUNTIME`=603 |
| Transport | 700–799 | 2h | `TRANSPORT_LIFECYCLE`=700, `TRANSPORT_IO`=701, `TRANSPORT_HANDSHAKE`=702, `TRANSPORT_CONFIG`=703 |
| Decimal | 800–899 | 2a | `DECIMAL_INVALID`=800, `DECIMAL_PRECISION_LOSS`=801 |
| Control plane | 900–999 | 2j | `CTRL_CONFIG`=900, `CTRL_RUNTIME`=901 |
| Log+OTel (reserved) | 1000–1099 | 2k | — (reserved; no `#define` yet) |
| Tap (reserved) | 1100–1199 | 2l | — |
| Bindings | 1200–1299 | 2m | `BINDING_PYTHON_CALLBACK_RAISED`=1200, `BINDING_SUBINTERPRETER`=1201, `BINDING_OBJECT_LIFETIME`=1202, `BINDING_WHEEL_ABI_MISMATCH`=1203, `BINDING_CALLBACK_REENTRANT_CLOSE`=1204 |
| Post-v1.x growth | 1300–1399 | — | reserved (`[2i §4.3]`) |
| Future expansion | 1400+ | — | reserved (`[2i §4.3]`) |

**Invariants**: (a) underlying type is exactly `int32_t`; (b) within a block, growth appends at unused slots; (c) once major==1, a published slot never changes meaning (does not yet bind — major 0). **Note**: `TRANSPORT_HANDSHAKE` peer-cert rejection coalesces with `TLS_HANDSHAKE` per `[2i §3.16]`/error.hpp — `translate()` honours the documented grouping.

## E-3 — `fixpp::core::error` → `fixpp_error_t` coalescing map (the `translate()` table)

Domain: the **116 `fixpp::core::error` enumerators** (highest assigned slot 131, holes at {2–9, 14–19, 70}; slot 70 is the permanent session hole). Range: the E-2 codes.

`translate()` is **not** a mechanical transcription. `error.hpp` *recovers* the intended C code, but **not uniformly**: only ~72 enumerators carry an inline `→ FIXPP_ERR_*` arrow; `store_*`/`session_*`/`tls_*` are annotated by grouped `← { … }` prose (`error.hpp:172–181,249–259,340–350`), and the cancellation rows say "Joins FIXPP_ERR_CANCELLED" (`error.hpp:86,131,163,228`). Feature A therefore makes an **audited coalescing decision**: for each group below, (source annotation in `error.hpp`) → (Feature-A chosen C code) → (reason where it diverges). Several groups are **deliberate overrides** of the source annotation, not transcriptions (marked **Override** below):

| C++ variants (slots) | error.hpp annotation | Feature-A C code | Override / reason |
|----------------------|----------------------|------------------|-------------------|
| `out_of_memory`(1) | (no inline arrow) | `UNKNOWN`=2 | **Override** — no cross-cutting OOM code in `[2i §4.3]`; a `switch(error)` cannot be call-site-dependent. Domain OOM (e.g. `dict_xml_oom`) maps to its own block code (`DICT_OOM`). |
| `decimal_invalid_input`(10), `decimal_overflow`(11) | `→ DECIMAL_INVALID` | `DECIMAL_INVALID`=800 | direct |
| `decimal_precision_loss`(12) | `→ DECIMAL_PRECISION_LOSS` | `DECIMAL_PRECISION_LOSS`=801 | direct |
| `decimal_buffer_too_small`(13) | `→ BUFFER_TOO_SMALL` | `BUFFER_TOO_SMALL`=6 | direct (cross-cutting reuse) |
| `dict_*`(20–29) | `→ DICT_*` arrows | `DICT_CONFIG`/`DICT_LIMIT_EXCEEDED`/`DICT_OOM` | direct, per `[2c §6.7]` grouping |
| `wire_*`(30–42) | `→ WIRE_*` arrows | `WIRE_INVALID_FRAME`/`WIRE_LIMIT_EXCEEDED`/`WIRE_CONFORMANCE` | direct, per `[2b §6.7]` |
| `sync_lock_aborted`(43) | "Joins FIXPP_ERR_CANCELLED" prose | `CANCELLED`=1 | grouped-prose-derived |
| `sync_lock_*`(44–46) | `→ SYNC_RUNTIME` | `SYNC_RUNTIME`=500 | direct |
| threading(47–55) | mixed arrows + "Joins CANCELLED" | `CANCELLED`/`THREAD_CONFIG`/`THREAD_SESSION_LIFECYCLE`/`THREAD_RUNTIME` | per annotation |
| `store_*`(56–65) | grouped `← { … }` prose | `CANCELLED`/`STORE_RUNTIME`/`STORE_CONSISTENCY`/`STORE_CONFIG`/`STORE_VISITOR` | grouped-prose-derived |
| `tls_*`(78–93) | grouped `← { … }` prose | `CANCELLED`/`TLS_CONFIG`/`TLS_PINSET`/`TLS_RUNTIME`/`TLS_HANDSHAKE` | grouped-prose-derived |
| `transport_*`(94–115) | `→ TRANSPORT_*` arrows + "Joins CANCELLED" | `CANCELLED`/`TRANSPORT_LIFECYCLE`/`TRANSPORT_IO`/`TRANSPORT_HANDSHAKE`/`TRANSPORT_CONFIG` | per annotation |
| `log_*`(122–126) | `→ C-ABI 1000–1004` (`error.hpp:709–724`) | `UNKNOWN`=2 | **Override + deferral (L-049-2)** — `[2i §4.3]` publishes **no `#define` for the 1000–1099 log/otel block** yet; mapping to a non-existent constant is illegal. Feature-B `[2i]` amendment publishes the block. |
| `otel_*`(127–128) | `→ C-ABI 1010–1011` (`error.hpp:728–733`) | `UNKNOWN`=2 | **Override + deferral (L-049-2)** — same: 1010/1011 are unpublished. |
| `app_*`(129–131) | `→ C-ABI reserved (future mapping)` (`error.hpp:746,754,768`) | `UNKNOWN`=2 | **Override** — source itself defers; no published code. |
| `session_*`(66–77,116–121) | grouped `← { FIXPP_ERR_SESSION_* }` prose | `UNKNOWN`=2 | **Override + deferral (L-049-2)** — `[2i §4.3]` publishes **no session block** (D-8); no Feature-A producer. Feature B publishes `FIXPP_ERR_SESSION_*`. |

> **No open mapping decision.** `translate()` is a **total** switch (no `default`; `-Wswitch` enforces completeness — this totality discipline is sound, mirroring `error_message()`'s 116 default-less arms). Every one of the 116 enumerators has a target above. The override groups (`session_*`/`log_*`/`otel_*`/`app_*`/`out_of_memory`) map to `UNKNOWN`=2 **as documented v1.0 behaviour**, each with a `// publish in Feature B`-style deferral comment; the enumerating test asserts these arms `== UNKNOWN` explicitly so a later refinement trips a test rather than shifting the surface silently (see E-3-test below).

### E-3-test — the enumerating test is a correctness oracle, not a publishedness proxy (NEW-P1)

The enumerating test MUST drive the **exact 116-enumerator set** (derived from a checked-in expected table or the `error_message()` arms — **never** iterating a 1..131 range, which would drive 15 non-existent values) and assert each variant maps to its **specific expected C code** drawn from a **checked-in expected (variant → exact code) table** — the same artifact that seeds `error_codes_v1.txt`. The table is **mutation-tested**: flipping one arm (e.g. `tls_handshake_failed → STORE_RUNTIME` instead of `TLS_HANDSHAKE`) MUST turn the test RED. "Returns *a* published code" is a bypassable proxy that would enshrine a mis-coalesced arm (`-Wswitch` enforces totality, not correctness) — it is replaced by the exact-code oracle. The explicit `== UNKNOWN` assertions for the override groups are kept on top of the oracle.

## E-4 — Version descriptors (CA-004)

```
fixpp_version_t { uint16_t major; uint16_t minor; uint16_t patch; uint16_t _reserved; }   // PoD, [2i §4.5]
```
- `fixpp_version()` → the C-ABI surface version (currently `0.x`; this feature 0.1→0.2).
- `fixpp_library_version()` → the C++ library version (from `fixpp::core::FIXPP_VERSION`, currently `0.0.1`), separate track per `[arch §9.2]`.
- Macros in `version.h`: `FIXPP_C_ABI_VERSION_{MAJOR,MINOR,PATCH}` + composite `FIXPP_C_ABI_VERSION`.

**Downgrade rule (E-5)**: `translate_for_consumer(fixpp_error_t code, uint16_t consumer_minor)`: compare the code's **introducing minor ordinal** (a `uint16_t`) against `consumer_minor`; if `introducing_minor > consumer_minor` → `UNKNOWN`=2; else pass through. Per `[const §X.4]` the downgrade keys on the consumer's published **ABI minor** and stability binds only at MAJOR==1 — so a code born in this feature has **introducing_minor = 2** (the `.2` of C-ABI 0.2.0), *not* "1.0". Major mismatch is the engine-construction concern (`VERSION_MISMATCH`=5), owned by Feature B. Introducing-minor source = `error_codes_v1.txt`'s introducing column (all current codes = `2`).

## E-5 — Reentrancy class (CA-003)

Enum-of-doc {`thread-safe`, `single-thread`, `requires-session-lock`} attached to each exported symbol via doc-comment. Feature A's three exports are all `thread-safe`. Completeness is a **discrete, independently-runnable** check (not folded into the occupancy script): for every exported `fixpp_*` symbol it asserts the symbol's doc-block (the contiguous `///`/`/** … */` comment immediately preceding the declaration) contains **exactly one** of the three class tokens — 0 symbols unannotated, 0 with two classes (FR-014 / SC-005).

## E-6 — Audit + occupancy artifacts

- `tools/abi_history/error_codes_v1.txt`: append-only, one line per published code: `<numeric> <SYMBOL> <introducing_minor>`, where `introducing_minor` is the **`uint16_t` ABI minor ordinal** at which the code was introduced (all current codes = `2`, the minor of C-ABI 0.2.0) — this is the column `translate_for_consumer` consumes (E-5). CI asserts the header's `#define`s are a superset-preserving match (no slot redefined).
- `tools/check_capi_occupancy.sh`: the occupancy gate is **two independent checks** (the two quantities are *not* comparable to each other — see `[2i §4.3]` ~line 608 + P2-5):
  - **Check A** — header `#define` *layout* equals the `[2i §4.3]` published `#define` values (e.g. decimal publishes exactly `DECIMAL_INVALID`=800, `DECIMAL_PRECISION_LOSS`=801; `decimal_buffer_too_small` reuses cross-cutting `BUFFER_TOO_SMALL`=6).
  - **Check B** — the **source-domain variant-row counts** (`[2i §4.3]` counts `| `*_*` | ` rows in each sibling `[2X §6.X]` errors table: decimal 4, wire 13, dict 20, threading 9, store 10, sync 4, TLS 15, transport 22) equal an expected coalescing-coverage table. (Comparing the 2 decimal `#define`s against the 4 decimal variants directly — as the prior spec did — would always FAIL.)
  - Reentrancy completeness is a **separate** discrete check (E-5 / D-7), not folded in here.
  - Runs Tier 1; non-zero exit fails CI.
