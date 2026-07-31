# Contract — `fixpp::` Export Set

**Feature**: 084-packaging-cpack-export · **Date**: 2026-07-31

The consumer-facing CMake contract. Evidence for every claim is in [`research.md`](../research.md) R1–R4, R7.

---

## 1. What a consumer writes

```cmake
find_package(fixpp REQUIRED)
target_link_libraries(my_app PRIVATE fixpp::fixpp)
```

That is the whole contract. The consumer names **no** include directory, **no** library path, **no** third-party dependency of fixpp, and **no** link ordering.

---

## 2. Export set membership

**Exported** — the transitive closure a real FIX client links (research R2):

| Target | Rationale |
|---|---|
| `fixpp::fixpp` | Umbrella; links `fixpp_session`, which pulls the rest transitively |
| `fixpp_session` | Sessions — linked directly by the real client |
| `fixpp_transport` | Transport — linked directly by the real client |
| `fixpp_tls` | Secured transport — linked directly by the real client |
| `fixpp_wire` | Parser/encoder — reachable from the public parser header |
| `fixpp_dictionary` | Dictionaries and runtime loading |
| `fixpp_core` | Base — every other member depends on it |
| `fixpp_sync` | Synchronisation symbols required by session/transport/tls |
| `fixpp_log` | Logging — **carries `FIXPP_LOG_MIN_LEVEL` as a PUBLIC compile definition** |
| `fixpp::dict::<ver>` | Per-version generated typed headers (INTERFACE) |

**Exported only when built** — telemetry targets. Their presence is configuration-dependent, which is why the config file must be generated from what was built rather than from a fixed list (invariant I3).

**Never exported** (FR-007, settled by 078 Gate B P1):

| Target | Why |
|---|---|
| `fixpp_builders_<ver>` | Install-scope coherence — an installed consumer would get unresolvable `build_` symbols |
| `fixpp_validators_<ver>` | Same, for `validate_` symbols |

---

## 3. Boundary rules

**B1 — Closure.** No exported target may expose a link-interface dependency on a non-exported target. CMake enforces this at generate time; SC-007 additionally requires a check proven to fail on a deliberately broken input.

**B2 — The install interface must not reach denylisted content.** `CMakeLists.txt:351-355` excludes `messages/`, `groups/`, `validators/`, `all.hpp`, `groups.hpp`. Any `$<INSTALL_INTERFACE:>` added by this feature must resolve to the *installed* include directory, whose contents are already filtered.

**B3 — `Args` stay unexported (FR-010, verified).** The span-based typed-builder `Args` live under `messages/` and `groups/`. Two independent mechanisms keep them out (research R1): the denylist, and the fact that dict targets currently carry no install interface at all. **Confirmed clean — no escalation.**

> **Standing obligation.** This confirmation holds only for the export set as designed here. Any future `$<INSTALL_INTERFACE:>` resolving somewhere other than the denylisted install tree re-opens FR-010, and the deferred "Option 3" `Args` decision must then be escalated before shipping.

**B4 — Compile definitions.** `FIXPP_LOG_MIN_LEVEL` **must** propagate (public headers branch on it; it is build-type-conditional — Debug `0`, Release `2`). `FIXPP_BUILD_OTEL` **must not** (it reaches no public header; adding it would create an ODR mismatch that currently cannot occur). Research R4.

**B5 — Link ordering is ours, not the consumer's.** `libfixpp_tls.a` references cryptography symbols and requires the fixpp archives to precede them. Exported target dependencies must let CMake derive this; the hand-ordering in `perf/CMakeLists.txt:56-57` and `tests/interop` exists only because those link raw archive paths. Research R7, FR-010b.

---

## 4. Third-party dependency resolution

`fixppConfig.cmake` resolves, so consumers need not:

| Dependency | Why | Always? |
|---|---|---|
| OpenSSL | `fixpp_transport` PUBLIC; `fixpp_tls` PRIVATE | Yes |
| asio | `fixpp_session`, `fixpp_tls` PUBLIC | Yes |
| pugixml | `fixpp_dictionary` PRIVATE — static libs don't link their private deps, so the consumer's final link must resolve it | Yes |
| opentelemetry-cpp | Telemetry targets | **Only if built with telemetry** |

**Deliberately NOT resolved: ZLIB.** No fixpp target links it (research R3). It appears in `tests/transport` and `perf` only because those bypass the imported targets with raw `find_library(... NO_DEFAULT_PATH)`. A consumer using the imported targets gets any compression dependency transitively. **A spurious requirement is as much a packaging defect as a missing one** — the real-client witness links for real, so a genuine need surfaces as a link failure rather than being masked.

---

## 5. Version compatibility

Version comes from `project(VERSION)` — `0.0.1` today (`CMakeLists.txt:5`), never a packaging-local literal (FR-005). A later bump propagates with no packaging change.

An incompatible request fails at **configure** time with a version-specific diagnostic (FR-006, SC-006). Failing later — at build or link — would be a defect.

---

## 6. What this contract does not promise

- **No shared libraries.** Every C++ target is STATIC by design. Adding shared variants is an ABI commitment and an explicit non-goal; REMAINING-WORK A-1 deliberately holds the `0→1` freeze.
- **No typed-builder API.** `build_<Msg>` / `validate_<Msg>` are unavailable to installed consumers (B3, FR-007).
- **No C ABI changes.** `include/fix/c_api*` is untouched.
- **No cross-configuration mixing.** A consumer built in a different configuration must resolve correctly or fail with a clear diagnostic — never with an undefined symbol at link time (spec User Story 1, scenario 3).
