# 2m — SWIG/Python Binding Shape

| Field | Value |
|---|---|
| **Status** | Draft v0.3 — Gate A round 2 converged (Phase A) |
| **Date** | 2026-05-10 |
| **Owner** | Opus (drafter) |
| **Inherits** | `[const §I.1]`, `[const §IV.3]`, `[const §IV.5]`, `[const §V.1]`, `[const §VII.2]`, `[const §VIII.5]`, `[const §X.1]`, `[const §X.3]`, `[const §X.4]`, `[const §X.5]`, `[const §XI.2]`, `[const §XII.1]`, `[const §XIV.2]`, `[const §XV.15]`, `[const §XVII.1]`, `[const §XVIII.1]`, `[arch §1.2]`, `[arch §2.3]`, `[arch §4.10]`, `[arch §4.12]`, `[arch §5.2]`, `[arch §5.3]`, `[arch §5.5]`, `[arch §5.6]`, `[arch §6]`, `[arch §7.1]`, `[arch §7.4]`, `[arch §8]`, `[arch §9.1]`, `[arch §10] row 2m`, `[SYN §3.5 #18]`, `[2a §5.1]`, `[2a §5.2]`, `[2b §6.4]`, `[2c §5]`, `[2d §4.4]`, `[2d §4.5]`, `[2d §6.7]`, `[2d §7.6]`, `[2e §4.4]`, `[2g §7.6]`, `[2h §7.8]`, `[2i §1.1]`, `[2i §1.2]`, `[2i §3.5]`, `[2i §4.2]`, `[2i §4.3]`, `[2i §4.4]`, `[2i §4.5]`, `[2i §4.6]`, `[2i §4.7]`, `[2i §4.9]`, `[2i §4.10]`, `[2i §5.2]`, `[2i §6.3]`, `[2i §6.5]`, `[2i §7.12]`, `[2i §10 Q5]`, `[2j §4.7]`, `[2j §1.2]`, `[2j §11]`, `[2k §1.1]`, `[2k §2]`, `[2l §1.1]` |
| **Cites** | see Appendix B (every reference grouped by source). |
| **Catalogue rows owned** | PY-001 (sole), PY-002 (sole), PY-003 (sole), PY-004 (sole), PY-005 (sole) |
| **Convergence log** | Initial v0.1 draft (Phase A round 1); v0.2 addresses Codex review (5 P1 / 6 P2 / 4 P3) and Opus adversarial review (combined post-judging 9 P1 / 7 P2 / 5 P3; 3 root causes); v0.3 addresses Codex round-2 review (0 P1 / 2 P2 / 1 P3) and Opus round-2 adversarial review (combined post-judging 1 P1 / 4 P2 / 4 P3; 0 new root causes), see Appendix C. |

---

## §1 Goals

2m locks the v1.0 shape of the `fixpp` Python module — the SWIG-generated CPython extension that wraps the **C ABI** (`<fix/c_api.h>`) per `[arch §4.12]`. Concretely:

1. Publish the Python `fixpp` package (`import fixpp`) as the sole consumption surface for non-C++ callers per `[arch §4.12]` / `[const §IV.3]`. The package wraps **only** the C ABI surface published by `[2i]`; engine-internal C++ headers are forbidden in the SWIG include path per `[arch §8]` / the `tools/check_layers.py` lint. PY-001.
2. Lock the **per-call GIL discipline** per `[arch §4.12]`: every blocking C-ABI call **releases** the GIL at the SWIG boundary (`SWIG_PYTHON_THREAD_BEGIN_ALLOW`); every callback the engine fires into Python **acquires** the GIL via PyGILState_Ensure → director call → PyGILState_Release. Callbacks dispatch on the engine's session strand per `[2d §7.6]`; the SWIG director adapter is the bridge. PY-002.
3. Lock the **exception translation map** from `fixpp_error_t` ranges (per `[2i §4.3]` numeric blocks) to a `fixpp.FixppError` Python exception hierarchy with stable subclass enum values. PY-003. The mapping is bidirectional: a Python exception raised inside a Python `Application` callback is captured by the SWIG director, the traceback is printed to Python's `sys.stderr` via `PyErr_PrintEx(0)` (the engine logger is **not** callable from `bindings/python/` in v1.0 per `[2k §2]` non-goal #7), and the failure is translated back to a new C-ABI code `FIXPP_ERR_BINDING_PYTHON_CALLBACK_RAISED` (numeric `1200` per the `[2i §1.1]` `[1200, 1299]` reserved block — Appendix D §D.1 carries the byte-faithful 2i drop-in). The engine observes only the `1200` return code; no Python traceback string crosses `extern "C"`.
4. Lock the **lifetime / ownership** rule per `[arch §4.12]` / `[2i §4.2]` / `[2i §6.3]`: every Python object that wraps a non-owning C-ABI handle holds a strong reference (`Py_INCREF`) to the owning Python object that holds the producing handle, so a Python `Session` cannot be GC'd while a `Message` derived from its inbound dispatch is still alive. PY-004.
5. Lock the **wheel-build pipeline**: the v1.0 mandatory wheel per `[const §IV.3]` is `fixpp-<ver>-cp310-cp310-manylinux_2_28_x86_64.whl` per `[arch §7.1]`. The wheel is built via `cibuildwheel` on Tier 1 CI; `auditwheel repair` post-processes for manylinux compliance; the engine binary is bundled into the wheel as `fixpp/_fixpp.so` plus `auditwheel`-vendored shared-library dependencies (OpenSSL per `[const §XII.1]`, mimalloc per `[arch §5.2]`). PY-005.
6. Lock the **inbound message lifetime contract**: the Python `Message` object delivered to `fromApp` aliases the engine's per-message arena (the `fixpp_msg_t*` is a non-owning observer of a `wire::MessageView` per `[2i §4.2.1]`). Capturing the `Message` past `fromApp` return is undefined behaviour at the C ABI per `[2b §6.4]`; the Python idiom for safe handoff is `msg.clone()` which calls `fixpp_msg_clone` per `[2i §4.7]` / `[2i §6.3]` and returns a Python-owner-controlled copy.
7. Lock the **§6.4 async callback handoff** decision (the `[SYN §3.5 #18]` open question): v1.0 ships **synchronous reacquire-and-call** on the engine strand thread. Asyncio adapters and producer-consumer queue handoffs are explicitly **future work** (see §10).
8. Pin the **AGPL boundary** structurally per `[arch §8]` / `[const §V.1]`: the SWIG `.i` files include only `<fix/c_api.h>`-tree headers; the CMake target `fixpp-python` per `[arch §7.4]` depends on `fixpp::capi` (the C-ABI consumer target) and the SWIG-generated wrapper, never on `fixpp` (the C++ umbrella) and never on any `<fixpp/...>` header.

### §1.1 Magnitude domain — what ships in the v1.0 wheel

**Python version range.** v1.0 ships **CPython 3.10 only** as the mandatory wheel (matching the `cp310-cp310` ABI tag per `[arch §7.1]` line 460). CPython 3.11 / 3.12 / 3.13 wheels are built best-effort on Tier 1 (cibuildwheel matrix); only `cp310` is mandatory for v1.0 release. PyPy is **out** of v1.0 scope — see §2 non-goal #2.

**Platform matrix.**

| Platform | v1.0 status | CI tier |
|---|---|---|
| Linux x86_64 (manylinux_2_28) | **Mandatory** per `[const §IV.3]` | Tier 1 (every PR) |
| Linux x86_64 (musllinux_1_2) | Best-effort | Tier 1 if cibuildwheel supports |
| Linux aarch64 (manylinux_2_28) | Out (post-v1.0) | n/a |
| Windows x86_64 (`_fixpp.pyd`) | Best-effort per `[arch §7.1]` line 461 | Tier 2 |
| macOS x86_64 / arm64 | Out (post-v1.0) | n/a |

**Public surface scope.** The Python module wraps:
- The full `<fix/c_api/error.h>` enum (translated to `FixppError` subclass hierarchy).
- The full `<fix/c_api/version.h>` macros and `fixpp_version()` accessor.
- The full `<fix/c_api/decimal.h>` boundary (`fixpp_decimal_t` PoD wrapped as a `fixpp.Decimal` Python class).
- The full `<fix/c_api/message.h>` accessor / setter / group surface (`fixpp.Message` Python class).
- The `<fix/c_api/dict.h>` load surface (`fixpp.Dictionary`).
- The `<fix/c_api/store.h>` opaque-handle plumbing only — no per-method exposure (the consumer never directly drives the store).
- The `<fix/c_api/engine.h>` / `<fix/c_api/session.h>` lifecycle (`fixpp.Engine`, `fixpp.Session`) — signatures owned by 2j + the Phase-4 session-module spec; 2m is a *consumer* of those C-ABI symbols.

**Public surface NOT exposed in v1.0.**
- `<fix/c_api/log.h>` / `<fix/c_api/otel.h>` — placeholders only per `[2k §5]`; no Python binding in v1.0. The Python user configures logging via `fixpp.LogConfig` which routes to the engine's C++ logger; reading log records back into Python is post-v1.0.
- The `service/` gRPC client — Python users who need a `ControlPlaneClient` use `grpcio` + the project-published `.proto` directly. No `fixpp.ControlPlaneClient` shim ships in v1.0.

### §1.2 Scope boundary — what 2m owns vs what it doesn't

**2m owns:**
- The Python `fixpp` package layout (§4).
- The SWIG `.i` file shape and the `%typemap` discipline (§4 + §6).
- The GIL release/acquire policy (§6.1).
- The `FixppError` exception hierarchy and its mapping table from `fixpp_error_t` (§4.6 / §6.3).
- The Python-side lifetime / ownership rules and the weakref discipline (§6.2).
- The async-callback-handoff v1.0 shape (§6.4).
- The reentrancy carve-outs for "what may a Python callback call" (§6.5).
- The latency Tier 1 ceilings for SWIG accessor calls (§6.6).
- The new `fixpp_error_t` variants in the `[1200, 1299]` block (§6.7) and the Appendix D drop-in to `[2i §4.3]` / `[2i §6.5]` / `[2i §1.1]`.
- The wheel-build pipeline (`cibuildwheel` + `auditwheel repair`) and its CI integration (§9 seam #5).

**2m does NOT own:**
- The C ABI surface itself — owned by **2i**. 2m consumes `<fix/c_api.h>` as input per `[2i §7.12]`.
- Engine / session lifecycle behaviour — owned by **2j** + the Phase-4 session-module spec (CA-005, CA-006, CA-007 cross-cuts).
- Wire parser, dictionary, decimal, store, threading, sync, TLS, transport — owned by 2a–2h, surfaced through the C ABI which 2m wraps.
- iceoryx2 cross-process tap subscription — `[2l §1.1]` keeps tap C-ABI deferred to a 2i amendment; Python tap subscription is therefore deferred to v1.x (see §7.11 + §10).
- Logger / OTel C-ABI — owned by **2k**; placeholders only in v1.0 per `[2k §5]`. Python logging integration is `fixpp.LogConfig` → engine C++ logger; reading log records back into Python is post-v1.0.
- Control-plane gRPC client — owned by `grpcio` + the project's published `.proto`; 2j does not require a fixpp-side Python shim.

The `[const §XIV.2]` ≤5-pure-virtual cap **does not apply** to 2m's surface — Python bindings own no plugin interface. The `Application` callback object is a SWIG director with 6 methods (`onLogon`, `onLogout`, `toAdmin`, `fromAdmin`, `toApp`, `fromApp`); the 6-method Application interface itself is justified at `[arch §6]` last paragraph (all six are normative semantic distinctions, not boilerplate); 2m mirrors the C++ shape verbatim and inherits the justification.

### §1.3 SWIG director consumption rule (normative — RC#2 backstop)

**(NEW in v0.2 — closes Codex P1-3, P1-4, P2-3, Opus N-P1-1, N-P1-2 as one structural commitment.)**

The SWIG director adapter, the SWIG `.i` files, and every Python helper module under `bindings/python/` consume **only** the C-ABI symbols enumerated in §5 — no `fixpp::*` C++ symbol, no `<fixpp/...>` header, no engine-internal helper. This is the structural backstop for the AGPL boundary per `[const §V.1]` / `[arch §8]` and the operational shape of every cross-doc commitment in the rest of this document.

Specifically:

1. **Inbound message immutability** is enforced by the C ABI per `[2i §10 Q5]` DECIDED v0.2 — `fixpp_msg_set_*` on an inbound flyweight returns `FIXPP_ERR_INVALID_HANDLE` (the handle is a `const wire::MessageView`). The SWIG director does **not** re-implement immutability checks engine-side; it surfaces the C-ABI's `FIXPP_ERR_INVALID_HANDLE` (numeric 4) via the §4.6 mapping to `fixpp.CapiError`. A Python user that wants to mutate an inbound message calls `msg.clone()` first to obtain a mutable outbound-shaped copy. (See §4.5 docstring updates for `fromAdmin` / `fromApp` and §6.5 row 2.) The director additionally sets a Python-side `_is_inbound: bool = True` flag on every inbound `Message` so that `set_*` accessors raise `fixpp.CapiError(code=4)` before the C-ABI round-trip — a latency optimisation, not a divergence from rule (1).

2. **Reentrancy legality** is determined by `[2i §4.10]` annotations; the SWIG director does **not** invent stricter Python-only rules. `Session.send()` from inside `fromApp` is legal at the C ABI per `[2i §4.10]` (every `fixpp_session_send` is `FIXPP_REQUIRES_SESSION_LOCK` and is dispatched on the strand per `[2d §7.6]`); the Python wrapper does **not** refuse it. The §6.5 carve-out table lists only the genuine deadlock cases — `engine.close()` / `session.close()` from inside a callback (which would block on the strand draining itself).

   > **[Article XX amendment — 054-python-gil-exceptions, L-054-1]:** the "legal" claim above was predicated on the *non-blocking strand-dispatch* `fixpp_session_send` design. The **as-built 050 C-ABI** `fixpp_session_send` is **blocking** — `co_spawn(ioc_, …, use_future)` then `fut.get()` (mechanism `src/capi/session.cpp:284-286`; documented deadlock rule `session.h:255-258`). Calling it **from inside the inbound callback** (which runs on the engine worker / session strand) therefore **DEADLOCKS** as-built: the worker blocks in `fut.get()` waiting for the send coroutine to run on the *same* io_context/strand, which cannot progress (**L-054-1**). This is a **strand/io_context reentrancy deadlock — distinct from, and unaffected by, the 053 GIL-teardown deadlock** (053) and the close-from-callback deadlock (row below). It is a **current limitation tied to the blocking as-built**, NOT a permanent "forbidden by design": restoring real legality (non-blocking send-from-callback) is a deferred **engine** item outside the PY-002/PY-003 `0→1` freeze. The binding guarantee stays **documentary** (the callback docstring states the hazard); active detection (`session._in_callback` + a pre-call `CallbackReentrantClose`-style raise) is **PY-004**.

3. **Logging on the director path** uses Python's `sys.stderr` only via `PyErr_PrintEx(0)`. The engine logger is **not** callable from `bindings/python/` in v1.0 per `[2k §2]` non-goal #7 ("No log forwarding to the C ABI in v1.0. Log/OTel access is C++ API only. `c_api/log.h` and `c_api/otel.h` contain placeholder version macros and `#include` guards only; no `extern "C"` symbols in v1.0."). There is no `fixpp_python_callback_log_fatal` or equivalent C-ABI symbol in v1.0. The engine's own logger may emit "code 1200 from session strand" using only the `fixpp_error_t` value (no Python traceback string crosses `extern "C"`); the Python traceback is observable on Python's `sys.stderr`.

4. **Strand-execution discipline** uses GIL-protected session-local markers, **not** OS-thread-id detection. Per the `[2d §4.5]` threading-mode taxonomy (`per_session_strand` | `engine_thread_pool_strand` | `direct_executor`), the session strand is **not** pinned to a single OS thread under `engine_thread_pool_strand` mode (the most likely production deployment with `EngineConfig.io_threads > 1`). The director sets `session._in_callback = True` on the *Python `Session` instance* on entry (under the GIL) and clears it before releasing the GIL on exit; `Session.send` / `Session.close` / `Engine.close` check `self._in_callback` directly. The GIL serialises Python execution; only one callback runs in Python at a time; the marker is GIL-protected without thread-id assumptions. (See §6.5 for the runtime check shape.)

   > **[Article XX amendment — 054-python-gil-exceptions, close-from-callback]:** The `session._in_callback` marker, the `Session.close`/`Engine.close` pre-call check, and the resulting `CallbackReentrantClose` (1204) raise are the **designed behavior** for the SWIG director, described here for the future PY-004 implementation. The **as-built flat v1.0 binding** (053/054) has **no director, no `_in_callback` marker, and no pre-call check** — `session_close` and `engine_destroy` are bare flat functions with `%exception` GIL-release bands. Close-from-callback in the v1.0 flat binding therefore **deadlocks** (same as send-from-callback, L-054-1) — the binding guarantee is **documentary** (the module docstring states the hazard). Active pre-call `CallbackReentrantClose` detection (this marker mechanism) is **PY-004**. Symmetric with L-054-1.

The §3.4 / §9 seam #9 (`tools/check_layers.py` extension to `bindings/python/` — see §9 seam #9 commitment below) is the structural backstop that prevents regressions of rules (1)–(4) above. A `bindings/python/` source file that includes `<fixpp/wire/...>` or links a `fixpp::core::*` symbol fails the lint and the build.

---

## §2 Non-goals

Explicit non-goals for 2m v1.0:

1. **No `async` / `await` Python coroutine surface.** v1.0 callbacks are synchronous Python methods on a SWIG director object; the engine calls them while holding the GIL on the session strand thread. An `asyncio` adapter (where `Session.recv()` returns an `awaitable` driven by a Python event loop) is post-v1.0; tracked in §10 Q1.
2. **No PyPy support.** v1.0 ships CPython 3.10 wheels only; PyPy's `cpyext` emulation has known performance and correctness gaps for SWIG director objects under heavy GIL traffic. PyPy is post-v1.0.
3. **No Linux aarch64 mandatory wheel.** v1.0 mandates Linux x86_64 only per `[const §IV.3]` / `[arch §7.1]`. aarch64 is post-v1.0.
4. **No Windows mandatory wheel.** v1.0 builds Windows wheels best-effort via Tier 2 per `[arch §7.1]` line 461.
5. **No PyPI namespace beyond `fixpp`.** v1.0 publishes `fixpp` only. Sub-namespaces like `fixpp.contrib`, `fixpp.examples` are not part of the v1.0 wheel; they live in a separate documentation repo.
6. **No in-process plugin replacement from Python.** v1.0 Python users cannot supply a custom `Transport`, `cert_source`, `MessageStore`, `Sink`, `ControlPlane`, or `TapConsumer` from Python (these factories take a `std::pmr::memory_resource*` per `[arch §6]`, which has no Python equivalent). C++ plugin authors compile and link with `fixpp::session-iface` per `[arch §7.4]`; Python users get the published defaults.
7. **No multi-version `import fixpp.v44` / `import fixpp.v50sp2` namespacing in v1.0.** The Python `Message` class is **dictionary-resolved at runtime**: the resolved per-message version per `[2c §5]` commitment 1 is exposed via `msg.version` (a `fixpp.MsgVersion` value type), but the Python class hierarchy is not split by FIX version. A consumer who needs typed attributes per FIX version uses `msg.get_string(tag)` etc. The C++ codegen output (`fixpp::v42::*` etc. per `[2c §3]`) is not exposed in Python in v1.0; tracked in §10 Q3.
8. **No Python-side `async_mutex` shim.** `fixpp::sync::async_mutex` per `[2f §4.5]` is C++-coroutine-only. Python's `threading.Lock` and `asyncio.Lock` are sufficient for the rare Python-side serialisation need; the engine's session strand handles all Python-callback serialisation internally.
9. **No `cert_source` / `Pinset` Python-side construction.** Per `[2g §7.6]` / `[2i §7.7]` the TLS surface is C++-only in v1.0; Python users supply file paths via `SessionConfig.cert_source_path`, never a Python factory.
10. **No `Transport` factory from Python.** Per `[2h §7.8]` / `[2i §7.8]` the transport surface is C++-only in v1.0.
11. **No reading log records into Python.** Per `[2k §5]` the log C-ABI is a placeholder in v1.0; the Python `LogConfig` routes through the engine's C++ logger but does not surface a Python-side `Sink` or record-reader.
12. **No wheel signing.** Signing is **not currently mandated by the constitution** — `[const §IV.5]` covers publication gating only ("v1.0 release artifacts are built but not published. Conan packages and Python wheels are attached to GitHub releases; no upload to Conan Center or PyPI in v1"), and contains no signing requirement. v1.0 therefore ships unsigned wheels attached to GitHub releases. If/when PyPI publication unblocks under `[const §IV.5]`'s gate, a constitution amendment per Article XX of `[const]` introduces the signing scheme (Sigstore is the likely candidate per PyPI's recommendation); 2m amends accordingly. Tracked in §10 Q2.
13. **No GIL-free / nogil (PEP 703) build.** PEP 703 builds are post-v1.0; the v1.0 wheel uses standard CPython 3.10 with the GIL.
14. **No streaming serialise from Python.** Per `[2i §2]` non-goal #8 the C ABI has only single-message commit; Python inherits.
15. **No tap subscription from Python in v1.0.** Per `[2l §1.1]` and §1.2 above; tracked in §10 Q4.

---

## §3 Inherited surface

This section quotes the inherited contract verbatim with file:line citations so the reader can re-verify against live source.

### §3.1 From `[arch §4.12]` — `bindings/python` surface inventory (the spine)

> **Public surface:** the Python `fixpp` module.
>
> - SWIG `.i` files generate a CPython extension wrapping the C ABI.
> - GIL handling: reacquire-and-call on receive callbacks (v1.0); async-queue handoff is documented as future work `[SYN §3.5 Q18]`.
> - Exception translation maps `fixpp_error_t` → Python `FixppError` with stable enum values.
> - `pip install fixpp` is the consumption surface; Linux x86_64 wheel is mandatory `[const §IV.3]`.
>
> **Design doc:** **2m**.
>
> **Catalogue rows:** PY-001 to PY-005.

Source: `library/.specify/architecture.md:351–362`.

### §3.2 From `[arch §7.1]` — Build outputs

> Python extension: `_fixpp.so` (SWIG-generated) / `_fixpp.pyd` (Windows).
> Python wheel: `fixpp-<ver>-cp310-cp310-manylinux_2_28_x86_64.whl` (mandatory) / Windows best-effort.

Source: `library/.specify/architecture.md:459–460`. The `cp310-cp310-manylinux_2_28_x86_64` ABI tag locks the v1.0 wheel.

### §3.3 From `[arch §7.4]` — CMake target

> `fixpp-python` — SWIG target; depends only on `fixpp::capi` and the SWIG-generated wrapper.

Source: `library/.specify/architecture.md:499`. The `fixpp::capi` consumer target carries `INTERFACE_INCLUDE_DIRECTORIES = include/fix/` only — the C++ engine umbrella `fixpp` is not in the SWIG target's include path. Verified by §9 seam #9 (no-engine-include test).

### §3.4 From `[arch §8]` — Service-mode boundary (applies equally to Python bindings)

> The `fixppd` daemon and any default plugin implementations under `service/` consume the engine **only through the C ABI**. They must not include engine internal headers (`<fixpp/wire/...>`, `<fixpp/session/...>`, `<fixpp/dict/...>`, etc.).

Source: `library/.specify/architecture.md:511–514`. Per `[arch §2.3]` the same rule applies to `bindings/python` (the table row "`bindings/python` may include from `capi` only"). The structural backstop is `tools/check_layers.py` per `[arch §7.4]` line 501 / `[arch §8]`; **2m commits at sign-off** (mirroring the 2j precedent at `[2j §1.2]` goal 6 / `library/.specify/2j-controlplane.md:22`) that the lint extends to scan **`bindings/python/*.i`, `bindings/python/*.cxx`, `bindings/python/*.py`** for any `#include <fixpp/X/...>` or `%include "fixpp/..."` line where the include is anything other than `<fix/c_api/...>` (the C-ABI tree). The lint failure messages are categorised "**AGPL boundary violation: bindings/python file X includes Y**" — identical phrasing to 2j, identical AGPL-boundary stake under `[const §V.1]`. The §9 seam #9 fixture includes a negative-case test (a fixture file that intentionally includes `<fixpp/wire/...>`) the lint must flag. This is **not** retroactive prose: the v0.2 commitment explicitly extends the existing `tools/check_layers.py` semantics over `service/` (per `[2j §1.2]` goal 6) to `bindings/python/` at 2m sign-off, with the same negative-case fixture pattern.

### §3.5 From `[arch §5.3]` — Error model translation boundary (the second hop)

> **C ABI translates** `fixpp::core::error` → `fixpp_error_t` at the boundary. Out-of-range values from older consumers are tolerated; out-of-range values *to* a consumer are mapped to `FIXPP_ERR_UNKNOWN` `[const §X.4]`.

Source: `library/.specify/architecture.md:389`. 2m adds a second translation hop: `fixpp_error_t` → `fixpp.FixppError` Python exception. The mapping table is in §4.6 / §6.3.

### §3.6 From `[arch §5.5]` — Lifetime model

> **Flyweights** are the rule for `wire::View`, typed messages, and offset-table accessors. They never own buffers `[SYN §3.1 Q2]`.
>
> **Owned types** (`Session`, `MessageStore`, `Engine`) follow standard value semantics; copy is deleted, move is enabled where natural.

Source: `library/.specify/architecture.md:399–401`. The Python wrapping inherits both halves: `fixpp.Message` is a flyweight Python wrapper whose lifetime is bounded by either the `fromApp` window (inbound) or `fixpp_msg_destroy` (outbound); `fixpp.Engine` / `fixpp.Session` are owned types with explicit close semantics.

### §3.7 From `[const §IV.3]` — Linux x86_64 wheel mandatory

> Python bindings ship via SWIG over the C ABI, packaged as a CPython wheel. **Linux x86_64 wheel is mandatory for v1.0**; Windows wheel is best-effort via Tier 2.

Source: `library/.specify/constitution.md:58`. v1.0 release blocked unless the manylinux_2_28_x86_64 cp310 wheel builds and the `pytest` smoke test passes against it.

### §3.8 From `[const §V.1]` — AGPL boundary

> **`fixpp` library:** AGPL-3.0 + commercial dual. The C ABI is the linkage isolation boundary for commercial users.

Source: `library/.specify/constitution.md:66`. The Python wheel sits structurally on the same side of the boundary as `fixppd` per `[arch §2.3]`: both consume only the C ABI. A Python script that `import fixpp` does NOT trigger AGPL on the user's Python code (the import linkage is dynamic over the C ABI); a C++ application that `#include <fixpp/session/...>` DOES trigger AGPL on linkage.

### §3.9 From `[const §VIII.5]` — Hot-path discipline

> **Allocator policy on the hot path:** zero `new`/`delete` between parse and `fromApp` callback. Arena/PMR is the default; deviations require justification in the relevant `/plan`.

Source: `library/.specify/constitution.md:106`. **Allocation discipline at the SWIG director boundary (RC#1 normative paragraph — NEW in v0.2).** The SWIG director runs *on* the engine session strand thread but is structurally outside the engine's arena/PMR allocator pool — it allocates exclusively from the Python heap (CPython's refcounted `PyObject_Malloc` pool). `[const §VIII.5]`'s "zero `new`/`delete` between parse and `fromApp` callback" rule scopes to *engine-side* allocations on the engine arena path, verified by the `mallocnesia` interceptor's symbol-scope filter (`fixpp::*` and `fixpp_*` symbols only — see `[arch §5.2]` line 381 and §9 seam #1 acceptance-criterion update). Python-heap allocations made by the SWIG director and by user `fromApp` code are *user-code allocations* in the architecturally permitted "engine yields the strand to user code" window — the SWIG director / GIL spine architectural anchor at `[arch §4.12]` (`library/.specify/architecture.md:351–362`) is the home of the Python-callback shape, and the strand-yielded-to-user-code framing is a 2m-internal axiom following from the §1.3 rule (4) / §3.9 RC#1 paragraph rather than a verbatim quote of any specific architecture-section text. (v0.3: cite reframed from `[arch §6]` last paragraph — Opus round-2 N-2-P3-1 close. The v0.2 cite attached the framing to `[arch §6]`'s 6-method `Application` justification, which talks about pure-virtual cap exceptions and not about strand-yielding semantics.) The Python user has opted out of the engine's no-alloc contract by virtue of using a Python binding. Concretely: the per-callback `PyObject*` (the `Message` wrapper, the argument tuple, the bound-method-object-wrapper, the result `PyObject*`) is allocated on the Python heap; this is permitted and not a `[const §VIII.5]` violation. The §6.6 wrapper-pool optimisation (§10 Q10) is a Python-side latency lever, not a `[const §VIII.5]` correctness blocker — v1.0 ships naive per-callback allocation; the pool is post-v1.0. This stance is canonicalised across §1.1, §6.1, §6.6, and §8.

### §3.10 From `[const §X.1]` — C ABI SemVer track

> The C ABI may stay at MAJOR=1 across multiple library MAJOR bumps, provided the surface stays compatible.

Source: `library/.specify/constitution.md:131` operational detail. The Python wheel's `fixpp.__version__` is the **library** version; the wheel also exposes `fixpp.FIXPP_C_ABI_VERSION_MAJOR/MINOR/PATCH` which track the C ABI per `[2i §4.5]`. A wheel built against C-ABI v1.1 may be loaded against an engine binary at C-ABI v1.0 (the consumer's `consumer_abi_version_major == FIXPP_C_ABI_VERSION_MAJOR (engine)` test in `[2i §4.5]` passes; the engine's forward-compat downgrade per `[2i §4.4]` handles new error variants).

### §3.11 From `[const §X.4]` — Error stability

> **Error reporting at the C ABI:** `fixpp_error_t` is a bounded enum with reserved range and explicit forwards-compatibility rules. Out-of-range values are mapped to a documented "unknown error" code on read; unknown values from old consumers are tolerated by the engine.

Source: `library/.specify/constitution.md:135–136`. 2m's Python `FixppError` exception class hierarchy is **stability-track**: every **published** numeric `fixpp_error_t` value (i.e., a value present in a tagged C-ABI release header) maps to a stable Python subclass; **unrecognised** values from a newer engine binary surface as the **block parent class** (e.g., `BindingError` for an unknown `[1205, 1299]` value) until the wheel is upgraded — this matches the `[2i §4.4]` forward-compat downgrade rule. New variants in v1.x append new subclasses **at the same numeric value** when the wheel is rebuilt; numeric values are never re-bound to a different subclass per `[const §X.4]`. (Codex P2-6 demoted to P3, editorial clarification — v0.2; headroom updated from `[1204, 1299]` to `[1205, 1299]` in v0.3 per Codex round-2 P2-1 split.)

### §3.12 From `[const §X.5]` — Reentrancy contract

> **Reentrancy contract** is documented per C ABI symbol (thread-safe / single-thread / requires-session-lock). No undocumented reentrancy.

Source: `library/.specify/constitution.md:137`. 2m inherits the per-symbol annotation per `[2i §4.10]`: when a Python method calls into the C ABI, the SWIG wrapper enforces the reentrancy rule. Concretely: `Session.send()` is `FIXPP_REQUIRES_SESSION_LOCK` per `[2i §4.10]`; SWIG releases the GIL but the underlying C-ABI thunk dispatches the work onto the session strand via `fixpp_session_send`'s strand-aware shape per `[2j §6.5]`. The Python caller doesn't see the dispatch directly; they get back an `fixpp_error_t` translated to `FixppError`.

> **[Article XX amendment — 054, L-054-1]:** the "dispatches onto the session strand" description matches the *intended* non-blocking shape. The **as-built 050** `fixpp_session_send` blocks on the dispatch (`co_spawn(ioc_, …, use_future)` + `fut.get()`), so the "SWIG releases the GIL … dispatches onto the strand" path is safe **only off the strand**. From *inside* an inbound callback (on the strand) it deadlocks — see the §1.3 rule (2) amendment and **L-054-1**.

### §3.13 From `[const §XV.15]` — Banned pattern: drop-oldest on app/session message paths

> **Application-layer message drops on slow consumer.** Backpressure-aware dispatch with two configurable modes: `block` (push back to the producer) or `disconnect-and-recover`. `drop-oldest` is **never** permitted on the application or session message path.

Source: `library/.specify/constitution.md:221`. The Python async-callback queue handoff that 2m explicitly defers (§6.4 / §10 Q1) was the implicit candidate here; we close it cleanly: v1.0 uses synchronous reacquire-and-call (no queue, no drop possible). When async-queue handoff is added in v1.x, the queue MUST be `block` mode per `[const §XV.15]` (slow Python consumer slows the FSM dispatch; the FSM may then `disconnect-and-recover`); `drop-oldest` is structurally banned.

### §3.14 From `[2i §4.2]` — Opaque handles

> Per `[arch §4.10]` the v1.0 catalogue is `fixpp_engine_t`, `fixpp_session_t`, `fixpp_msg_t`, `fixpp_dict_t`, `fixpp_store_t`. Each is declared in its owning split header as an incomplete forward struct.

Source: `library/.specify/2i-capi.md:347–365`. 2m wraps each handle in a Python class:

| C-ABI handle | Python class | SWIG `%nodefaultctor`? |
|---|---|---|
| `fixpp_engine_t` | `fixpp.Engine` | yes (constructed via factory `Engine(config)`) |
| `fixpp_session_t` | `fixpp.Session` | yes (constructed via `engine.open_session(config)`) |
| `fixpp_msg_t` | `fixpp.Message` | partial — `Message(msg_type, session)` calls `fixpp_msg_create_outbound`; inbound `Message` instances are constructed by the SWIG callback adapter |
| `fixpp_dict_t` | `fixpp.Dictionary` | yes (constructed via `Dictionary.load_xml(path)`) |
| `fixpp_store_t` | `fixpp.MessageStore` | yes (constructed via `session.message_store` accessor; cannot be created standalone) |

### §3.15 From `[2i §4.3]` — `fixpp_error_t` numeric blocks

The full layout per `[2i §1.1]`:

```
[0,    99]   FIXPP_ERR_CAPI_*       (cross-cutting; 2i-owned; 11 occupied)
[100,  199]  FIXPP_ERR_WIRE_*       (2b-owned; 13 occupied)
[200,  299]  FIXPP_ERR_DICT_*       (2c-owned; 20 occupied)
[300,  399]  FIXPP_ERR_THREAD_*     (2d-owned; 9 occupied)
[400,  499]  FIXPP_ERR_STORE_*      (2e-owned; 10 occupied)
[500,  599]  FIXPP_ERR_SYNC_*       (2f-owned; 4 occupied)
[600,  699]  FIXPP_ERR_TLS_*        (2g-owned; 15 occupied)
[700,  799]  FIXPP_ERR_TRANSPORT_*  (2h-owned; 22 occupied)
[800,  899]  FIXPP_ERR_DECIMAL_*    (2a-owned; 4 occupied)
[900,  999]  FIXPP_ERR_CTRL_*       (2j-owned; 2 occupied)
[1000, 1099] RESERVED: 2k log + otel
[1100, 1199] RESERVED: 2l tap
[1200, 1299] RESERVED: 2m bindings translation        ← THIS DOC OWNS
[1300, 1399] RESERVED: post-v1.x growth
```

Source: `library/.specify/2i-capi.md:55–71`. **The `[1200, 1299]` block was reserved for 2m at 2i v0.3 sign-off** (per `library/.specify/2i-capi.md:68`); 2m v0.2 *populated* the pre-reserved block by introducing 4 new variants; v0.3 splits the v0.2 `GilDeadlock` (1201) into two semantically distinct codes (`SUBINTERPRETER` 1201 + `CALLBACK_REENTRANT_CLOSE` 1204) per Codex round-2 P2-1, so the block now holds **5 variants** in §6.7 (codes 1200, 1201, 1202, 1203, 1204). The 100-wide block still accommodates ≥ 4× the worst current count and ≥ 5× the project median per `[2i §1.1]` headroom rule. Appendix D §D.1 / §D.2 / §D.3 / §D.5 are framed as "populate" amendments (not "create the reservation") — the reservation already exists; this doc fills it. (Codex P3-1 close v0.2; Codex round-2 P2-1 close v0.3.)

### §3.16 From `[2i §4.5]` — Engine-binding version-check protocol

> When the consumer calls `fixpp_engine_create(consumer_abi_version_major, consumer_abi_version_minor, ...)`:
> - If `consumer_abi_version_major != FIXPP_C_ABI_VERSION_MAJOR (engine)`: engine refuses to construct, returns `FIXPP_ERR_VERSION_MISMATCH`.
> - If `consumer_abi_version_minor < engine_minor`: engine constructs but stores `consumer_minor` for forward-compat downgrade per `[2i §4.4]`.

Source: `library/.specify/2i-capi.md:710–712`. The Python `fixpp.Engine.__init__` calls `fixpp_engine_create` with the wheel's compiled-in `FIXPP_C_ABI_VERSION_MAJOR / MINOR`. When the user `pip install fixpp` brings in a wheel built against ABI v1.0 and the engine binary in the same wheel was built against ABI v1.0, they always match. When the user has a system-wide engine binary at v1.1 and the wheel at v1.0, the consumer-side minor is stored and forward-compat downgrade applies.

### §3.17 From `[2i §4.6]` — Field accessor lifetime contract

> On success, `*value_out` aliases the underlying wire buffer. The pointer is valid until: (a) the next call to `fixpp_msg_set_*(...)` on the same msg, OR (b) for inbound messages, the `fromApp` callback returns, OR (c) for outbound messages, `fixpp_msg_destroy(msg)` is called.

Source: `library/.specify/2i-capi.md:761–767`. The Python wrapper inherits the contract: `msg.get_string(tag)` returns a Python `str` constructed by **copy** from the aliased C buffer (because Python `str` is immutable and the consumer typically wants a copy); the underlying alias is dropped at SWIG `%typemap(out)` exit. There is no Python-level "view-style" zero-copy accessor in v1.0 (memoryview-over-the-buffer is post-v1.0; tracked in §10).

### §3.18 From `[2i §4.7]` — `fixpp_msg_clone`

> `fixpp_msg_clone(src, &clone_out)` — used as the v1.0 cross-strand-handoff escape hatch.

Source: `library/.specify/2i-capi.md:941–942`. The Python `Message.clone()` method calls `fixpp_msg_clone`; the returned `Message` is **owned** (its lifetime is controlled by Python GC + a `__del__` that calls `fixpp_msg_destroy`); cross-thread / cross-coroutine handoff after `clone()` is safe.

### §3.19 From `[2i §4.9]` — Cancellation translation

> Every C++ engine-side outcome that carries `expected_t::unexpected{*_aborted}` / `*_cancelled` is translated **uniformly** to `FIXPP_ERR_CANCELLED` (numeric `1`) at the C ABI boundary.

Source: `library/.specify/2i-capi.md:1145–1147`. The Python translation is `FIXPP_ERR_CANCELLED → fixpp.Cancelled` (subclass of `fixpp.FixppError`). Python users catch `fixpp.Cancelled` to handle cancellation uniformly per the §6.3 mapping table.

### §3.20 From `[2i §4.10]` — Reentrancy annotation taxonomy

> `FIXPP_THREAD_SAFE` / `FIXPP_SINGLE_THREAD` / `FIXPP_REQUIRES_SESSION_LOCK`. Every public C ABI symbol carries exactly one annotation.

Source: `library/.specify/2i-capi.md:1162–1167`. The Python binding maps each annotation to a SWIG `%feature("threading")` directive — see §6.5.

### §3.21 From `[2i §5.2]` — Construction-vs-steady-state thunk split

> Two flavours of `guarded_call` are published, and every `extern "C"` symbol is placed on exactly one side: construction-time (catches `std::exception`, translates to `FIXPP_ERR_*_CONFIG`) vs steady-state (catches and `std::abort`).

Source: `library/.specify/2i-capi.md:1219–1222`. 2m's SWIG director adapter (the wrapper that the engine calls into for Python callbacks) is a **steady-state** path: the engine has already constructed; the Python callback fires from inside `fromApp`. Per §6.1 / §6.7, an exception escaping the Python callback is captured by the SWIG director, translated to `FIXPP_ERR_BINDING_PYTHON_CALLBACK_RAISED` (numeric 1200), logged via the engine logger, and the C++ engine receives the C-ABI return — **no exception ever crosses `extern "C"` from Python back into the engine**.

### §3.22 From `[2i §6.3]` — Cross-strand handoff via `fixpp_msg_clone`

> The v1.0 cross-strand handoff escape hatch is `fixpp_msg_clone(src, &clone)`. Clone is one bulk memcpy plus an offset-table rebuild; ≤ 1 µs warm-cache for a ~200-byte message.

Source: `library/.specify/2i-capi.md:1368`. The Python idiom for "I want to keep this message past `fromApp` return" is:

```python
class MyApp(fixpp.Application):
    def fromApp(self, session, msg):
        # msg is a flyweight; its lifetime ends when this method returns.
        copy = msg.clone()  # fixpp_msg_clone — Python now owns 'copy'
        self.queue.put(copy)  # safe to hand off across threads
```

### §3.23 From `[2i §7.12]` — Hand-off to 2m

> 2i's surface IS 2m's input. The 2m design doc (drafted later in Phase 2) consumes:
> - The full `<fix/c_api.h>` umbrella.
> - The opaque-handle declarations.
> - The `fixpp_error_t` enum (which 2m maps to a Python `FixppError` exception class with stable enum values per `[arch §4.12]`).
> - The reentrancy annotations (which 2m maps to GIL-acquire / GIL-release patterns per `[SYN §3.5 #18]`).

Source: `library/.specify/2i-capi.md:1525–1531`. This doc is the response to the hand-off.

### §3.24 From `[SYN §3.5 #18]` — Async callback handoff (open question)

> Async-queue handoff for receive callbacks: queue + Python-thread drain vs reacquire-and-call on receive callback. v1.0 ships reacquire-and-call; queue handoff is a future-work knob.

Source: `research/SYNTHESIS.md` §3.5 #18 (paraphrased — the live source is the SYNTHESIS doc; quoted in `[arch §4.12]` line 356 as "GIL handling: reacquire-and-call on receive callbacks (v1.0); async-queue handoff is documented as future work"). 2m §6.4 closes this question.

---

## §4 Public Python API surface

The `fixpp` Python package shipped in the v1.0 wheel. SWIG-generated; the surface listed here is the **post-`%pythoncode`** Python-facing API, not the raw `_fixpp` extension module.

### §4.1 Package layout and module top-level

```
fixpp/                          # the installed package
├── __init__.py                 # re-exports the public API; sets __version__, ABI version
├── _fixpp.so                   # SWIG-generated CPython extension (Linux)
├── _fixpp.pyd                  # SWIG-generated CPython extension (Windows; best-effort)
├── _native.py                  # SWIG %pythoncode shim — internal, not re-exported
├── application.py              # class fixpp.Application (the SWIG director base class)
├── config.py                   # EngineConfig / SessionConfig / LogConfig dataclasses + TOML loader
├── enums.py                    # SecurityProfile, BackpressurePolicy, LogLevel, ErrorCode, MsgVersionKind
├── errors.py                   # FixppError + 10 subclass hierarchy
├── decimal_.py                 # fixpp.Decimal Python class wrapping fixpp_decimal_t
├── message.py                  # class fixpp.Message
└── py.typed                    # PEP 561 marker — type stubs ship in the wheel
```

**Top-level re-exports (in `fixpp/__init__.py`):**

```python
# fixpp/__init__.py — public API
from .errors import (
    FixppError, ParseError, ValidatorError, StoreError, TransportError,
    TlsError, ControlPlaneError, LogError, CapiError, Cancelled, Unknown,
    BindingError,  # parent class for the [1200, 1299] block
    PythonCallbackRaised, SubInterpreterRejected, ObjectLifetime,
    WheelAbiMismatch, CallbackReentrantClose,
)
from .enums import (
    SecurityProfile, BackpressurePolicy, LogLevel, ErrorCode, MsgVersionKind,
)
from .decimal_ import Decimal
from .message import Message, MsgVersion
from .application import Application
from .config import EngineConfig, SessionConfig, LogConfig
from ._fixpp import (
    Engine, Session, Dictionary,
    FIXPP_C_ABI_VERSION_MAJOR, FIXPP_C_ABI_VERSION_MINOR, FIXPP_C_ABI_VERSION_PATCH,
    fixpp_version, fixpp_library_version,
)

__version__ = "0.1.0a0"  # the LIBRARY SemVer per [arch §9.2]; the wheel build pipeline
                         # (cibuildwheel) reads this from CMake at build time.
__all__ = [
    "FixppError", "ParseError", "ValidatorError", "StoreError", "TransportError",
    "TlsError", "ControlPlaneError", "LogError", "CapiError", "Cancelled", "Unknown",
    "BindingError", "PythonCallbackRaised", "SubInterpreterRejected",
    "ObjectLifetime", "WheelAbiMismatch", "CallbackReentrantClose",
    "SecurityProfile", "BackpressurePolicy", "LogLevel", "ErrorCode", "MsgVersionKind",
    "Decimal", "Message", "MsgVersion",
    "Application", "EngineConfig", "SessionConfig", "LogConfig",
    "Engine", "Session", "Dictionary",
    "FIXPP_C_ABI_VERSION_MAJOR", "FIXPP_C_ABI_VERSION_MINOR", "FIXPP_C_ABI_VERSION_PATCH",
    "fixpp_version", "fixpp_library_version",
]
```

The `from ._fixpp import ...` line pulls SWIG-generated wrapper classes; `from .errors import ...` pulls Python-only exception classes. The split is intentional: SWIG generates wrappers around C-ABI handles; Python-pure classes (exceptions, dataclasses) are hand-written.

### §4.2 `fixpp.Engine`

```python
class Engine:
    """Wraps fixpp_engine_t per [2i §4.2.1]. Owning handle.

    Construction calls fixpp_engine_create(consumer_abi_major, consumer_abi_minor, ...);
    raises fixpp.VersionMismatch on incompatible engine binary per [2i §4.5].
    """

    def __init__(self, config: EngineConfig) -> None: ...
    def open_session(self, config: SessionConfig) -> Session: ...
    def close(self) -> None:
        """Calls fixpp_engine_destroy. Idempotent; safe to call multiple times.
        After close(), all open sessions are closed and their handles invalidated."""

    def __enter__(self) -> "Engine": ...
    def __exit__(self, exc_type, exc_val, exc_tb) -> None: ...

    @property
    def is_open(self) -> bool: ...
```

**Reentrancy:** `__init__` is `FIXPP_SINGLE_THREAD`; subsequent methods are mostly `FIXPP_THREAD_SAFE` (per the C-ABI annotations on the underlying symbols). Verified by §6.5 enforcement.

**Construction failure modes.** Per `[2i §4.5]`: `VersionMismatch` (engine major != consumer major); per `[2i §6.5]` row 8: `BindingError(FIXPP_ERR_CAPI_CONFIG_INVALID)` for any other construction-time exception (bad config, OOM during arena setup); per `[2j §3.10]` `EngineConfig` validation may raise `ControlPlaneError(FIXPP_ERR_CTRL_CONFIG)`.

**Close discipline.** `Engine.close()` calls `fixpp_engine_destroy`; the engine destructor drains all open sessions per `[2d §6.6]` / `[2i §5.3]`. Calling `close()` on an already-closed engine is a no-op (idempotent per `[2i §4.2.1]`). The `__exit__` context manager hook calls `close()` automatically.

### §4.3 `fixpp.Session`

```python
class Session:
    """Wraps fixpp_session_t per [2i §4.2.1]. The native handle is
    engine-owned (the engine's session-registry per [2j §4.6] is the
    authoritative lifetime owner); the Python `Session` is the
    explicit-close driver for the bound session and MUST be closed
    before the engine via Session.close() / Engine.close() / context-
    manager `__exit__` per the §6.2 cycle-GC paragraph. (v0.3: phrasing
    tightened from v0.2's "non-owning observer" — Codex round-2 P3-1
    close. The native-level non-owning property is preserved; the
    wording now matches the §6.2 close-flow which explicitly drives
    `fixpp_session_close` from the Python wrapper.)

    Constructed via Engine.open_session(SessionConfig); cannot be constructed
    standalone (SWIG %nodefaultctor).
    """

    def open(self) -> None:
        """Drives the FIX Logon flow. Blocks until logon completes or fails.
        Releases the GIL during blocking I/O per §6.1.
        Raises fixpp.TransportError on network failure;
        raises fixpp.TlsError on TLS handshake failure;
        raises fixpp.Cancelled on close-during-open."""

    def close(self) -> None:
        """Drives the FIX Logout flow + transport close. Idempotent.
        Releases the GIL during blocking I/O.
        Raises fixpp.Cancelled if the close path is itself cancelled
        (e.g., a concurrent Engine.close fires; per [2i §4.9] every
        cancellation source surfaces as Cancelled)."""

    def send(self, msg: Message) -> None:
        """Calls fixpp_session_send. The message is enqueued for transmission;
        this call returns when the engine has accepted the message into its
        outbound queue, NOT when it has been transmitted (transmission is async).
        Raises fixpp.SessionError on send failure; raises fixpp.Cancelled
        if the session is closed mid-call."""

    def is_logged_in(self) -> bool: ...

    @property
    def session_id(self) -> str:
        """The (sender_comp_id, target_comp_id) pair as a "S->T" string."""

    def register_application(self, app: Application) -> None:
        """Bind an Application callback object to this session.
        The Application's onLogon/.../fromApp methods will be invoked
        on the session strand thread per [2d §7.6]; the SWIG director
        adapter handles the GIL acquire/release per §6.1.
        Calling register_application twice replaces the prior binding.

        Legality from inside a callback (NEW v0.3 — Opus round-2
        N-2-P3-3 / N-2-P3-4 close). Calling register_application from
        inside an in-flight callback is LEGAL: the swap takes effect at
        the NEXT callback dispatch (the in-flight callback continues to
        see the old `self._application` reference; the new application
        binding is installed for the next strand-dispatched callback).
        This separates cleanly from the close-from-callback case
        (which deadlocks as-built in v1.0 [documentary; PY-004 implements the
        active CallbackReentrantClose 1204 pre-call detection per §6.5]): swap is
        non-blocking and does not deadlock the strand. Implementation
        sets self._application to the new app under the GIL; the
        in-flight callback's bound-method-object dispatch already
        captured the old self._application before the swap."""

    def __enter__(self) -> "Session": ...
    def __exit__(self, exc_type, exc_val, exc_tb) -> None: ...
```

**Inbound message reception.** v1.0 uses **synchronous reacquire-and-call** per §6.4: when a message arrives, the engine dispatches `fromApp` on the session strand; the SWIG director adapter acquires the GIL, calls the user's Python `fromApp`, releases the GIL on return. There is no Python-side iterator or `recv()` blocking method (which would require a queue handoff per §6.4 future-work).

The alternative shape considered (a `for msg in session: ...` iterator) is post-v1.0 — it requires the async-queue handoff that §6.4 / §10 Q1 defers.

**Threading mode.** The Python `Session` does NOT take an executor argument: the engine picks per the `EngineConfig.executor` (which itself is engine-internal — Python users supply a thread-count via `EngineConfig.io_threads` per `[2d §4.4]` shape). Mid-session reconfiguration is rejected per `[arch §5.6]`.

### §4.4 `fixpp.Message`

```python
class Message:
    """Wraps fixpp_msg_t per [2i §4.2.1]. Three shapes:

    1. Inbound flyweight: constructed by the SWIG director adapter when an
       inbound message is dispatched to fromApp. Lifetime is the fromApp
       window per [2b §6.4]. The director arms a sentinel: BEFORE releasing
       the GIL on fromApp return, it sets the wrapper's _dead = True; any
       subsequent accessor call on the captured Python Message raises
       fixpp.ObjectLifetime (numeric 1202 per §6.7). There is no
       "undefined behaviour in release" path — capture-past-return surfaces
       as a deterministic Python exception (Codex P1-3 close, v0.2). The
       SWIG director also flags the wrapper as _is_inbound = True so that
       set_* accessors raise fixpp.CapiError(code=4) per §1.3 rule (1) /
       [2i §10 Q5] DECIDED v0.2 — inbound messages are immutable at the
       C ABI; the binding does not silently mutate them.

    2. Outbound mutable: constructed via Message(msg_type, session). The
       Python __init__ internally posts the fixpp_msg_create_outbound
       construction onto the session's strand via fixpp_session_post per
       [2i §6.3] line 1365 (which is FIXPP_REQUIRES_SESSION_LOCK per
       [2i §4.10]) and blocks the calling Python thread on a future with
       the GIL released. From the user's perspective, Message(msg_type,
       session) looks synchronous; from the C-ABI perspective, every
       fixpp_msg_create_outbound + every subsequent fixpp_msg_set_* runs
       on the session strand thread. This honours the strand-only
       reentrancy contract of every fixpp_msg_get_* / fixpp_msg_set_* per
       [2i §4.10] without forcing the Python user to manually post their
       own closures (Codex P1-2 close, v0.2). The fixpp_session_post
       symbol itself is owned by 2j per [2i §6.3] / [2j §11]; v0.3
       queues its declaration in 2j's hand-off table via Appendix D §D.4
       so it ships as a v1.0 C-ABI thunk (Opus round-2 N-2-P1-1 close).
       Lifetime is owner-controlled until either Session.send(msg) or
       Message.destroy() / __del__.

    3. Cross-strand-safe clone: constructed via msg.clone(). Calls
       fixpp_msg_clone per [2i §4.7] / [2i §6.3]. Owner-controlled; safe
       to hand off across threads / coroutines.
    """

    # --- Construction (outbound) ---
    def __init__(self, msg_type: str, session: Session) -> None: ...

    # --- Field accessors (CA-008 surface; per [2i §4.6]) ---
    def get_string(self, tag: int) -> str: ...
    def get_int(self, tag: int) -> int: ...
    def get_double(self, tag: int) -> float: ...
    def get_decimal(self, tag: int) -> Decimal: ...
    def get_bytes(self, tag: int) -> bytes: ...
    def has_tag(self, tag: int) -> bool: ...
    @property
    def msg_type(self) -> str: ...
    @property
    def version(self) -> MsgVersion: ...

    # --- Field setters (CA-009 surface; per [2i §4.7]) ---
    def set_string(self, tag: int, value: str) -> None: ...
    def set_int(self, tag: int, value: int) -> None: ...
    def set_double(self, tag: int, value: float) -> None: ...
    def set_decimal(self, tag: int, value: Decimal) -> None: ...
    def set_bytes(self, tag: int, value: bytes) -> None: ...
    def remove_tag(self, tag: int) -> None: ...

    # --- Repeating groups (CA-010 surface; per [2i §4.8]) ---
    def get_group(self, group_tag: int) -> Group: ...
    def begin_group(self, group_tag: int) -> GroupBuilder: ...

    # --- Lifetime / cross-strand handoff ---
    def clone(self) -> "Message": ...
    def destroy(self) -> None:
        """Calls fixpp_msg_destroy. Idempotent; called automatically by __del__
        for outbound messages and clones. For inbound flyweights this is a no-op
        (the engine destroys at parse-window close).

        __del__-vs-interpreter-shutdown caveat (NEW v0.3 — Opus round-2
        N-2-P3-2 close). __del__ is the PRIMARY teardown path for outbound
        Message and clones (typical user pattern lets the wrapper go out of
        scope without calling destroy() explicitly). At interpreter shutdown
        CPython makes weak guarantees about __del__ invocation order across
        modules (see also §6.2 cycle-GC paragraph); an outbound Message
        whose __del__ runs after the parent Engine.__del__ has cleared the
        C-ABI symbol table will hit a stale-pointer call into
        fixpp_msg_destroy. The supported teardown path is therefore: bound
        outbound Message lifetime by either Session.send(msg) (which
        transfers ownership to the engine for serialisation), an explicit
        Message.destroy(), or the surrounding Session's explicit close()
        (which walks its WeakSet[Message] and arms _dead per §6.2).
        Reliance on __del__ is supported only when the parent Session
        outlives the Message (the typical case); interpreter-shutdown
        __del__ ordering across modules is not guaranteed by CPython.
        Same caveat as §6.2 cycle-GC paragraph; the engine-driven
        explicit-close flow is the reliable path. __del__ is a defensive
        backstop only."""

    # --- Pythonic dunders ---
    def __getitem__(self, tag: int) -> str:
        """Sugar over get_string(tag). Raises KeyError on missing tag."""
    def __setitem__(self, tag: int, value: str) -> None: ...
    def __contains__(self, tag: int) -> bool: ...
    def __repr__(self) -> str: ...
```

**Inbound vs outbound flag.** Internal — the SWIG wrapper carries an `_is_inbound: bool` attribute so `__del__` can decide whether to call `fixpp_msg_destroy` (outbound) or no-op (inbound, engine-managed). The flag is set at construction and not user-visible.

**Lifetime invariant.** The Python `Message` holds a strong reference (`Py_INCREF`) to the parent `Session` (for inbound) or constructed-on `Session` (for outbound) per §6.2. A `Message` cannot be GC'd before its parent `Session`; conversely, a `Session.close()` invalidates all `Message` handles derived from inbound dispatches that still have live Python references — those Python objects raise `fixpp.ObjectLifetime` (numeric 1202 per §6.7; subclass of `BindingError`, **not** `InvalidHandle`) on any subsequent accessor call. (v0.2 resolves Codex P2-2: `InvalidHandle` is dropped from the binding-side surface; the binding-side invalidation marker is `ObjectLifetime`. `FIXPP_ERR_INVALID_HANDLE` is the C-ABI surface for inbound `set_*` rejection per §1.3 rule (1) and surfaces via the §4.6 mapping as `fixpp.CapiError` (numeric 4) — a distinct path from the Python-wrapper `_dead` sentinel.)

**`msg.clone()` is the cross-strand escape hatch.** Per `[2i §4.7]` / `[2i §6.3]` / §3.22 above. The clone is a fresh `Message` with `_is_inbound = False`; `__del__` calls `fixpp_msg_destroy`.

### §4.5 `fixpp.Application`

```python
class Application:
    """The Python-side callback object. Subclass and override the six methods.

    SWIG generates a director (cross-language polymorphism) so that the engine
    can call into Python from C++ via the C-ABI callback-trampoline.

    Threading: every callback fires on the session strand thread per [2d §7.6].
    The SWIG director adapter acquires the GIL before the call (PyGILState_Ensure)
    and releases it after (PyGILState_Release).

    Allocation: the Python `Message` argument is a wrapper over an inbound
    flyweight (per [2b §6.4]). Its lifetime is the callback's return; the
    SWIG director arms a _dead sentinel before GIL release on return, so
    any post-return accessor raises fixpp.ObjectLifetime (1202) — there is
    no UB-in-release path (§4.4 v0.2). Use msg.clone() for cross-strand
    handoff.

    Exceptions: a Python exception raised in any callback is captured by the
    SWIG director, the traceback is printed to Python's sys.stderr via
    PyErr_PrintEx(0) (the engine logger is NOT callable from
    bindings/python/ in v1.0 per [2k §2] non-goal #7 / §1.3 rule 3), and
    the failure is translated to FIXPP_ERR_BINDING_PYTHON_CALLBACK_RAISED
    (numeric 1200 per §6.7). The engine observes only the 1200 return
    code; no Python traceback string crosses extern "C".
    """

    def onLogon(self, session: Session) -> None:
        """Fired when the FIX Logon flow completes successfully."""

    def onLogout(self, session: Session) -> None:
        """Fired when the FIX Logout flow completes (cleanly or not)."""

    def toAdmin(self, session: Session, msg: Message) -> None:
        """Fired before an admin message is sent. The msg is OUTBOUND
        (mutable); the user MAY mutate it (e.g., add dialect-specific
        tags); mutations are deep-copied into the per-message arena per
        [2i §4.7] set-path contract. The dispatch is on the session
        strand per [2d §7.6]; the SWIG director adapter latency is
        bounded per §6.6."""

    def fromAdmin(self, session: Session, msg: Message) -> None:
        """Fired after an admin message is received and validated. The msg
        is INBOUND (immutable per [2i §10 Q5] DECIDED v0.2 / §1.3 rule 1);
        msg.set_*(tag, value) raises fixpp.CapiError(code=4) — call
        msg.clone() first to obtain a mutable outbound-shaped copy. The
        msg is a flyweight; lifetime ends at this method's return. The
        SWIG director arms a _dead sentinel; post-return accessor calls
        raise fixpp.ObjectLifetime (1202)."""

    def toApp(self, session: Session, msg: Message) -> None:
        """Fired before an application message is sent. The msg is OUTBOUND
        (mutable); the user MAY mutate it. The dispatch is on the session
        strand per [2d §7.6]; latency budget per §6.6 (≤ 5 µs p99 dispatch
        overhead). NOTE: every outbound message — including high-frequency
        app messages — fires toApp before serialisation, so a Python user
        sending high message rates pays the dispatch cost on every
        outbound message. Users who do not need outbound mutation should
        leave toApp unimplemented (the C++ no-op base is used; no SWIG
        director cost is paid)."""

    def fromApp(self, session: Session, msg: Message) -> None:
        """Fired after an application message is received and validated.
        The msg is INBOUND (immutable per [2i §10 Q5] DECIDED v0.2 /
        §1.3 rule 1); msg.set_*(tag, value) raises fixpp.CapiError(code=4)
        — call msg.clone() first. The msg is a flyweight; lifetime ends at
        this method's return. The SWIG director arms a _dead sentinel
        before releasing the GIL; post-return accessor calls raise
        fixpp.ObjectLifetime (1202). No "undefined behaviour in release"
        path (§4.4 v0.2)."""
```

**Six methods over the ≤5 cap.** Per `[arch §6]` last paragraph: the six callbacks are normative semantic distinctions (admin vs app, in vs out); removing any collapses a normative semantic users rely on for compliance and audit. The justification is reviewed at Gate A on `[arch]` itself; 2m mirrors the C++ shape and inherits the justification. v1.0 wheel installs the 6-method director; not user-overridable.

**Optional methods.** A subclass may override fewer than six (the default implementation is a no-op for each). The SWIG director's `%feature("director")` declares all six as virtual; SWIG's standard director-method-resolution picks the Python override or falls back to the C++ no-op base.

**Application lifetime.** The Python `Application` is held by `Session.register_application(app)` via a strong reference; the `Session` un-references on `Session.close()`. A user who creates an `Application` and never registers it is responsible for keeping it alive (standard Python ref-counting).

### §4.6 `fixpp.FixppError` — exception hierarchy

```python
class FixppError(Exception):
    """Base class for all fixpp-originated exceptions.

    Attributes:
        code: int — the numeric fixpp_error_t value.
        name: str — the symbolic name (e.g., "FIXPP_ERR_TRANSPORT_IO").
        message: str — fixpp_strerror(code) result.
    """
    code: int
    name: str
    message: str

# --- Cross-cutting block [0, 99] (2i-owned) ---
class CapiError(FixppError):
    """[0, 99] block. Sentinels + handle / version / type-mismatch errors.

    Notable codes (surfaced via .code attribute, NOT as dedicated subclasses):
    - 4 = FIXPP_ERR_INVALID_HANDLE — surfaced when the C ABI detects a
      destroyed/null handle, OR when fixpp_msg_set_* is called on an
      inbound flyweight per [2i §10 Q5] DECIDED v0.2 / §1.3 rule (1).
      Distinct from BindingError.ObjectLifetime (1202), which is the
      Python-wrapper sentinel for parent-handle invalidation.
    """
class Cancelled(CapiError):
    """code = 1 (FIXPP_ERR_CANCELLED). Fired on any cancellation per [2i §4.9].

    Cancelled is the SINGLE Python class for ALL ten cancellation pre-image
    variants enumerated in [2i §4.9] (`*_aborted`, `*_cancelled`,
    `sync_lock_aborted`, `clock_sleeps_cancelled`, `dispatch_aborted`,
    `tls_load_cancelled`, `transport_*_cancelled`, `accept_cancelled`,
    `store_cancelled`, `store_visitor_aborted_due_to_cancel`). A Python
    user catching Cancelled handles every cancellation case uniformly —
    Session.send, Session.open, and Session.close all surface
    cancellation as Cancelled (Opus N-P2-1 close, v0.2)."""
class Unknown(CapiError):
    """code = 2 (FIXPP_ERR_UNKNOWN). Forward-compat downgrade per [2i §4.4]."""

# --- Wire block [100, 199] (2b-owned) ---
class ParseError(FixppError):
    """[100, 199] block. Wire parsing failures."""

# --- Dict block [200, 299] (2c-owned) ---
class ValidatorError(FixppError):
    """[200, 299] block. Dictionary / validator failures."""

# --- Threading block [300, 399] (2d-owned) ---
class SessionError(FixppError):
    """[300, 399] block. Session lifecycle / strand / clock errors."""

# --- Store block [400, 499] (2e-owned) ---
class StoreError(FixppError):
    """[400, 499] block. MessageStore failures."""

# --- Sync block [500, 599] (2f-owned) ---
class SyncError(FixppError):
    """[500, 599] block. async_mutex failures (rare; mostly cancellation)."""

# --- TLS block [600, 699] (2g-owned) ---
class TlsError(FixppError):
    """[600, 699] block. TLS handshake / cert-source / pinset failures."""

# --- Transport block [700, 799] (2h-owned) ---
class TransportError(FixppError):
    """[700, 799] block. Transport I/O / lifecycle / handshake failures."""

# --- Decimal block [800, 899] (2a-owned) ---
class DecimalError(FixppError):
    """[800, 899] block. Decimal parse / format / precision-loss failures."""

# --- Control plane block [900, 999] (2j-owned) ---
class ControlPlaneError(FixppError):
    """[900, 999] block. ControlPlane configuration / runtime failures."""

# --- Log + OTel block [1000, 1099] (2k-reserved) ---
class LogError(FixppError):
    """[1000, 1099] block. Logger / OTel exporter failures (placeholder in v1.0)."""

# --- Tap block [1100, 1199] (2l-reserved) ---
class TapError(FixppError):
    """[1100, 1199] block. Tap consumer failures (placeholder in v1.0;
    no Python tap surface in v1.0 per §1.2)."""

# --- Bindings block [1200, 1299] (2m-owned per §6.7) ---
class BindingError(FixppError):
    """[1200, 1299] block. SWIG binding / Python-callback / GIL / lifetime
    failures introduced by 2m."""
class PythonCallbackRaised(BindingError):
    """code = 1200 (FIXPP_ERR_BINDING_PYTHON_CALLBACK_RAISED). A Python
    Application callback raised an unhandled exception; captured by the
    SWIG director, translated, and surfaced via the engine logger."""
class SubInterpreterRejected(BindingError):
    """code = 1201 (FIXPP_ERR_BINDING_SUBINTERPRETER). Engine.__init__
    was invoked from a CPython sub-interpreter (PEP 554 / `interpreters`
    API); v1.0 supports only the main interpreter per §6.1. Refused at
    construction; no native handle is allocated. (v0.3: split from the
    v0.2 draft's single `GilDeadlock` (1201) semantic overload into a
    distinct sub-interpreter code — Codex round-2 P2-1 close. Catching
    this distinctly from `CallbackReentrantClose` lets users write a
    clean handler for "do not run my code from a sub-interpreter"
    without conflating it with the close-from-callback deadlock case.)"""
class ObjectLifetime(BindingError):
    """code = 1202 (FIXPP_ERR_BINDING_OBJECT_LIFETIME). A Python wrapper's
    underlying C-ABI handle has been invalidated (e.g., parent Session was
    closed); the Python object outlived the native handle."""
class WheelAbiMismatch(BindingError):
    """code = 1203 (FIXPP_ERR_BINDING_WHEEL_ABI_MISMATCH). The wheel's
    embedded engine binary has an ABI version incompatible with the wheel's
    SWIG wrapper. Should never happen (cibuildwheel guarantees match);
    surfaces if a user manually replaces _fixpp.so."""
class CallbackReentrantClose(BindingError):
    """code = 1204 (FIXPP_ERR_BINDING_CALLBACK_REENTRANT_CLOSE). A Python
    callback attempted engine.close() / session.close() from inside the
    callback — the close path waits for the strand to drain, but the
    strand is mid-callback waiting for the close to return; the resulting
    deadlock is detected pre-call via the §1.3 rule (4) GIL-protected
    session-local marker (`session._in_callback`). The designed behavior
    is to raise this code BEFORE entering the C ABI so no native deadlock
    occurs. NOTE [054 / Article XX]: as-built flat v1.0 binding (053/054)
    has NO director, NO `_in_callback` marker, and NO pre-call check —
    `session_close`/`engine_destroy` are bare flat functions with
    %exception GIL-release bands. Close-from-callback in the v1.0 flat
    binding DEADLOCKS (same as send-from-callback per L-054-1); the
    binding guarantee is DOCUMENTARY (module docstring states the hazard).
    Active pre-call CallbackReentrantClose detection (this marker
    mechanism) is PY-004. (v0.3: split from the v0.2 draft's single
    `GilDeadlock` (1201) semantic overload into a distinct
    close-from-callback code — Codex round-2 P2-1 close.) Session.send
    from inside fromApp is NOT banned by *this code* (it is design-legal
    at the C ABI per [2i §4.10] / §1.3 rule (2)); only close-from-callback
    raises this. NOTE [054 / L-054-1, Article XX]: as-built (050 blocking
    fixpp_session_send = co_spawn(ioc_, …, use_future)+fut.get()),
    Session.send from inside the callback DEADLOCKS — a strand/io_context
    reentrancy deadlock distinct from this close-from-callback deadlock and
    from the 053 GIL-teardown deadlock. It stays documentary in v1.0;
    active detection (a pre-call raise mirroring this one) is PY-004. See
    the §1.3 rule (2) and §6.5 amendments.

    The numeric value 1201 was NEVER published in a tagged C-ABI release
    (v0.2 was an internal draft within Gate A — nothing tagged or
    shipped); the v0.3 re-allocation 1201 → SubInterpreterRejected,
    1204 → CallbackReentrantClose is therefore not a stability-rule
    violation per [const §X.4]. v0.3 is the FIRST signed-off version of
    this doc; no source-compatibility alias for the v0.2 draft name is
    retained."""

# --- Session/app + message-construction block [1400, 1499]
#     ([2i §4.3] / 051 D-6; ADDED to this hierarchy by 054 / Article XX) ---
class AppError(FixppError):
    """[1400, 1499] block. Session/app + message-construction failures
    (FIXPP_ERR_SESSION_INVALID_ARGUMENT/1400 .. FIXPP_ERR_MSG_FRAMING_TAG_FORBIDDEN/1405).
    The block was minted in [2i §4.3] / 051 AFTER this doc's §4.6 sign-off,
    so AppError is an additive amendment (no new fixpp_error_t code — the
    0->1 freeze holds). Distinct from SessionError ([300,399] threading);
    the block could not reuse that name. [054 / Article XX]"""
```

**Mapping rule (numeric → Python class).** Implemented in `fixpp.errors._map_to_class(code: int) -> type[FixppError]` (realized **module-level** as `fixpp._map_to_class` in v1.0 per 054 / PY-003; the `fixpp.errors.*` package form is the deferred package alias → PY-005):

| `fixpp_error_t` range | Python class |
|---|---|
| 0 (`FIXPP_ERR_OK`) | (no exception raised — `FIXPP_ERR_OK` is never translated) |
| 1 | `Cancelled` |
| 2 | `Unknown` |
| [3, 99] | `CapiError` |
| [100, 199] | `ParseError` |
| [200, 299] | `ValidatorError` |
| [300, 399] | `SessionError` |
| [400, 499] | `StoreError` |
| [500, 599] | `SyncError` |
| [600, 699] | `TlsError` |
| [700, 799] | `TransportError` |
| [800, 899] | `DecimalError` |
| [900, 999] | `ControlPlaneError` |
| [1000, 1099] | `LogError` |
| [1100, 1199] | `TapError` |
| 1200 | `PythonCallbackRaised` |
| 1201 | `SubInterpreterRejected` (v0.3 — split from v0.2 `GilDeadlock` per Codex round-2 P2-1) |
| 1202 | `ObjectLifetime` |
| 1203 | `WheelAbiMismatch` |
| 1204 | `CallbackReentrantClose` (v0.3 — split from v0.2-draft `GilDeadlock` per Codex round-2 P2-1) |
| [1205, 1299] | `BindingError` (parent class for unrecognised v1.x growth) |
| **[1400, 1499]** | **`AppError`** ([054 / Article XX] — the Phase-4 session/app + message-construction block minted in `[2i §4.3]` / 051 D-6, codes 1400–1405; post-`[2m §4.6]`-sign-off additive amendment; distinct from `SessionError` `[300,399]`) |
| anything else | **direct `_map_to_class` call: `FixppError` (root)** ([054 / Article XX] reconciliation, FR-009 — a wholly unmapped/future block falls back to the root, NOT `Unknown`; `Unknown` maps **only** code 2 / `FIXPP_ERR_UNKNOWN`, and a separate `UnknownError` would collide with it). **Runtime path: `Unknown`** — the `[2i §4.4]` forward-compat downgrade collapses any code newer than the consumer's registered minor to `FIXPP_ERR_UNKNOWN`(2) **before** it reaches the translator, so the out-typemap never hands `_map_to_class` a truly-unmapped code; the root fallback is the **direct-call** (SC-006) / forward-compat path. |

**Stability rule.** Once a `fixpp_error_t` numeric value is **published** in a tagged C-ABI release, the Python subclass binding **never changes**. Adding a new variant in v1.x adds a new subclass under the same parent (e.g., `FIXPP_ERR_BINDING_FOO` at code 1205 would add `class Foo(BindingError)`); never re-binds existing ones. **Unrecognised** numeric values (from a newer engine binary that the wheel doesn't know about) surface as the block parent class (e.g., `BindingError` for any `[1205, 1299]` value not yet in the wheel's compile-time table); this matches the `[2i §4.4]` forward-compat downgrade rule. Verified by §9 seam #6.

### §4.7 `fixpp.SessionConfig`, `fixpp.EngineConfig`, `fixpp.LogConfig`

Python dataclass mirrors of the C-ABI config PoDs. Per `[arch §5.6]`: `SessionConfig` is value-typed and frozen at session open.

```python
from dataclasses import dataclass
from typing import Optional, List
from .enums import SecurityProfile, BackpressurePolicy, LogLevel

@dataclass(frozen=True)
class EngineConfig:
    """Mirrors the engine-anchor config per [2d §4.4]. Frozen per [arch §5.6]."""
    io_threads: int = 1
    """Number of I/O threads. Default 1; engine creates a thread pool of this
    size and runs all session strands on it. Replaces 'executor' (Python users
    cannot supply an asio executor)."""

    default_dictionary_path: Optional[str] = None
    """Path to a QuickFIX-XML dictionary; loaded via fixpp_dict_load_from_xml.
    May be None if the user supplies per-session dictionaries instead."""

    log_config: Optional[LogConfig] = None
    """LogConfig for the engine logger. None = default (FileSink to stderr)."""

    enable_tap: bool = False
    """If True, the engine's RingBufferTap is constructed; Python users cannot
    drain it directly in v1.0 (no fixpp_tap_* C-ABI per §1.2). Use fixppd +
    iceoryx2 if you need tap. Reserved for v1.x."""

    # ABI version stamping — set by the wheel build, not user-set
    consumer_abi_major: int = 1  # FIXPP_C_ABI_VERSION_MAJOR at wheel build time
    consumer_abi_minor: int = 0  # FIXPP_C_ABI_VERSION_MINOR at wheel build time

@dataclass(frozen=True)
class SessionConfig:
    """Mirrors the per-session config per [2d §4.5]. Frozen at session open per [arch §5.6]."""
    sender_comp_id: str
    target_comp_id: str
    fix_version: str  # e.g., "FIX.4.4"; resolves to a dictionary in the engine's registry
    security_profile: SecurityProfile = SecurityProfile.MTLS_CA
    cert_source_path: Optional[str] = None  # PEM/DER file path per [2g §4.x]
    pinset_paths: List[str] = ()  # leaf certs for mtls_pinned per [2g §4.3]
    heartbeat_seconds: int = 30
    backpressure: BackpressurePolicy = BackpressurePolicy.BLOCK
    message_store_path: Optional[str] = None  # FileStore path; None = MemoryStore
    dialect_overlay_path: Optional[str] = None  # per [2c §4.x]

    @classmethod
    def from_toml(cls, path: str) -> "SessionConfig": ...
    @classmethod
    def from_quickfix_cfg(cls, path: str) -> "SessionConfig": ...

@dataclass(frozen=True)
class LogConfig:
    """Mirrors the LogConfig per [2k §4.3]; the Python LogConfig is a subset
    (the C++ Sink interface is not exposed; sinks are picked by name)."""
    level: LogLevel = LogLevel.INFO
    sink: str = "stderr"  # "stderr", "file", "syslog", "otlp" — resolved engine-side
    file_path: Optional[str] = None  # required if sink == "file"
    otlp_endpoint: Optional[str] = None  # required if sink == "otlp"
    json_format: bool = False
```

**Validation.** Configuration is validated at construction (Python dataclass `__post_init__`) AND at `fixpp_engine_create` / `fixpp_session_open` time (engine-side). Python-side validation catches type errors and obvious shape errors (a `pinset_paths` empty list when `security_profile == MTLS_PINNED` raises `ValueError`); engine-side validation per `[2j §3.10]` / `[2i §6.5]` row 8 catches deeper config errors (TLS file readability, dictionary parse failure) and surfaces them as `FixppError` subclasses.

**TOML loader.** `SessionConfig.from_toml(path)` uses Python 3.10's stdlib `tomllib` (3.10 is the wheel's mandatory ABI tag, so no extra dep). The TOML schema is documented in the user-facing docs; lives outside this design doc.

**QuickFIX CFG loader.** `SessionConfig.from_quickfix_cfg(path)` parses the QuickFIX `[DEFAULT]` / `[SESSION]` CFG format per `[const §XV.16]` mandate. Implemented as a Python parser over the file (no SWIG / C-ABI wrapping needed; the format is line-oriented INI-like).

---

## §5 Public C ABI

This section is short — the C ABI is owned by `[2i]`. The Python module wraps:

**Wrapped symbols (v1.0):**
- All of `<fix/c_api/error.h>` (the `fixpp_error_t` enum + `fixpp_strerror`).
- All of `<fix/c_api/version.h>` (`FIXPP_C_ABI_VERSION_*` macros + `fixpp_version()` + `fixpp_library_version()`).
- All of `<fix/c_api/decimal.h>` (`fixpp_decimal_t` + `fixpp_decimal_parse` / `_format` / `_compare` / `_equal` / `_init`).
- All of `<fix/c_api/message.h>` (the §4.6 / §4.7 / §4.8 surface of `[2i]`).
- `<fix/c_api/dict.h>` — `fixpp_dict_load_from_xml`, `fixpp_dict_destroy`, the `FIXPP_APPL_VER_*` constants.
- `<fix/c_api/store.h>` — opaque-handle plumbing only; no per-method exposure (Python users don't directly drive the store).
- `<fix/c_api/engine.h>` — `fixpp_engine_create`, `fixpp_engine_destroy`, the `EngineConfig` PoD per `[2j §3.10]`.
- `<fix/c_api/session.h>` — `fixpp_session_open`, `fixpp_session_close`, `fixpp_session_send`, `fixpp_session_register_callback`, the receive-callback signature per CA-005 / CA-006 / CA-007 (signatures owned by 2j + Phase-4 session-module spec).

**Symbols deliberately NOT wrapped in v1.0:**
- `<fix/c_api/log.h>` and `<fix/c_api/otel.h>` — placeholders only per `[2k §5]`. Python users configure logging via `fixpp.LogConfig` (which routes through engine-side construction) but cannot read records back into Python.
- Any future tap C-ABI per `[2l §1.1]` — deferred.
- Any future cert/pinset rotation C-ABI per `[2g §7.6]` — deferred.
- Any future transport-level C-ABI per `[2h §7.8]` — deferred.

---

## §6 Behavioral contract

### §6.1 GIL discipline

**Rule (binding contract).** Per `[arch §4.12]`: the SWIG-generated wrapper releases the GIL around every blocking C-ABI call and acquires the GIL around every callback the engine fires into Python.

**SWIG `%typemap` enforcement.** The release / acquire is inserted by SWIG via `%feature("threading") "1"` declared at the `.i` file scope, which wraps every generated wrapper function in:

```c
/* In SWIG-generated code (illustrative; the exact macros are
 * SWIG_PYTHON_THREAD_BEGIN_ALLOW / SWIG_PYTHON_THREAD_END_ALLOW for
 * release, SWIG_PYTHON_THREAD_BEGIN_BLOCK / SWIG_PYTHON_THREAD_END_BLOCK
 * for re-acquire inside callbacks. */
SWIG_PYTHON_THREAD_BEGIN_ALLOW;
fixpp_error_t err = fixpp_session_send(self->session, msg->msg);
SWIG_PYTHON_THREAD_END_ALLOW;
if (err != FIXPP_ERR_OK) { /* translate to PyExc; raise; */ }
```

For director (callback) methods:

```c
/* SWIG-generated director method — engine calls this on the strand thread.
 * The SWIG director MUST acquire the GIL before calling the Python override. */
PyGILState_STATE gstate = PyGILState_Ensure();
PyObject *result = PyObject_CallMethodObjArgs(self->py_application, "fromApp",
                                              session_py, msg_py, NULL);
if (!result) {
    /* Python exception raised — capture, translate per §6.7. */
    fixpp_error_t code = capture_and_translate_python_exception();
    PyGILState_Release(gstate);
    return code;
}
Py_DECREF(result);
PyGILState_Release(gstate);
return FIXPP_ERR_OK;
```

**Coverage table.**

| Operation | GIL state | Why |
|---|---|---|
| `Engine(config)` → `fixpp_engine_create` | RELEASED during call | Construction does ASIO setup, dictionary load, possibly file I/O — blocking. |
| `engine.open_session(...)` → `fixpp_session_open` | RELEASED | Construction-time per `[2i §5.2]`; may do TLS handshake / Logon — blocking. |
| `session.send(msg)` → `fixpp_session_send` | RELEASED | Steady-state per `[2i §5.2]`; submits to the strand and returns; may block briefly under backpressure (`[const §XV.15]` `block` mode). |
| `msg.get_string(tag)` etc. | **HELD** (NOT released) | Read accessor; ≤ 50 ns p99 per `[2i §6.4]` — releasing GIL would cost more than the call. |
| `msg.set_string(tag, value)` etc. | **HELD** | ≤ 200 ns p99 per `[2i §6.4]` — same reasoning. |
| `msg.clone()` → `fixpp_msg_clone` | RELEASED | ≤ 1 µs but may be longer for big messages; release for ergonomics. |
| `engine.close()` / `session.close()` | RELEASED | Drains in-flight work; may block. |
| `Application.fromApp(...)` (engine → Python) | ACQUIRED before call, RELEASED after | Director adapter responsibility. |
| `Application.toApp(...)` (engine → Python) | ACQUIRED before call, RELEASED after | Same. |
| `fixpp_strerror`, `fixpp_version`, `fixpp_library_version` | **HELD** | Sub-10 ns calls; releasing would dwarf the call cost. |

**The "release for sub-microsecond calls?" trade-off.** PyGILState acquire/release costs ~50–100 ns on x86_64. The accessor hot path (`msg.get_string`) targets ≤ 50 ns at the C-ABI plus ~500 ns of SWIG marshalling overhead per §6.6 → total ~500 ns. Releasing the GIL for 500 ns of work is net-negative when the calling Python thread is single-threaded (the dominant Python case); we keep the GIL for accessors and pay the cost only when the Python application actively benefits (long blocking calls, callbacks).

**Multi-threaded Python.** Users who want to drive multiple sessions concurrently from Python use multiple Python threads (the GIL is released during `Session.send` and similar blocking calls); the Python thread interleaves with engine strand threads via the GIL. Each `Application` instance is bound to one `Session`; concurrent calls into different sessions produce concurrent callbacks on different strand threads, each acquiring the GIL serially. This is the standard SWIG-director pattern and tested at §9 seam #1.

**Supported interpreter model (v1.0)** (NEW in v0.2 — Codex P2-1 close).

- **Single main interpreter only.** v1.0 supports the standard CPython 3.10 interpreter model: one main interpreter per process. Sub-interpreters (PEP 554 / `interpreters` API) are **not supported**: when sub-interpreters exist, `PyGILState_Ensure` always returns the *main* interpreter's state, so a Python user who creates an `Engine` from a sub-interpreter and then triggers a callback gets cross-interpreter object-access UB. `Engine.__init__` records the main-interpreter state via `PyInterpreterState_Get()` and refuses construction (raising `fixpp.SubInterpreterRejected` — `BindingError` subclass with code **1201 / `FIXPP_ERR_BINDING_SUBINTERPRETER`**) if invoked from a sub-interpreter. This is a 5-line guard that prevents a subtle UB class. **(v0.3: code 1201 is the sub-interpreter rejection only; the callback-reentrant-close deadlock case is now its own code 1204 / `FIXPP_ERR_BINDING_CALLBACK_REENTRANT_CLOSE` — Codex round-2 P2-1 close.)**
- **Shutdown ordering.** `Engine.close()` / `Session.close()` MUST run before interpreter shutdown (i.e., before `Py_Finalize` is reached either explicitly or via process exit's atexit hooks). Callbacks are not permitted after shutdown begins. The recommended idiom is `with Engine(config) as engine: ...` so that `__exit__` drives close before the surrounding scope exits.
- **GIL-build assumption.** v1.0 assumes the standard GIL-enabled CPython 3.10 build. PEP 703 (nogil) builds are deferred to v1.x per §10 Q8.
- **`PyGILState_Ensure` correctness.** Every callback path enters via `PyGILState_Ensure` and exits via `PyGILState_Release`; the §6.4 strand-side dispatch holds the GIL across the user `fromApp` body and arms the §4.4 `_dead` sentinel before release. The pair is balanced on every exit path including the Python-callback-raised path (§6.3).

§9 seam #4 exercises orderly shutdown and verifies the §1.3 rule (4) GIL-protected session-local marker; the sub-interpreter rejection is exercised by a fixture in `tests/python/test_subinterpreter_rejection.py`.

### §6.2 Lifetime / ownership

**Rule (binding contract).** Per `[arch §4.12]` PY-004: Python objects must not outlive the native sessions / engines they wrap.

**Python-side strong-reference graph.**

```
Engine (Python, owning)
  ├── owns:  fixpp_engine_t* via __init__ → fixpp_engine_create
  └── strong-refs (Py_INCREF): 0 (Engine is the root)

Session (Python, engine-owned native handle; explicit-close driver)
  ├── native handle: fixpp_session_t* is engine-owned (lifetime managed by
  │                  the engine's session-registry per [2j §4.6]); the
  │                  Python wrapper drives fixpp_session_close on its
  │                  side of the boundary as the supported teardown path
  │                  per §6.2 cycle-GC paragraph (v0.3 phrasing — Codex
  │                  round-2 P3-1 close).
  └── strong-refs: Engine (the parent — Py_INCREF on engine.open_session)

Message (Python, two flavours)
  Flavour 1 — INBOUND flyweight (constructed by SWIG director):
    ├── owns:  no — fixpp_msg_t* is engine-owned per [2i §4.2.1]
    ├── strong-refs: Session (the parent — Py_INCREF when SWIG director constructs)
    └── lifetime: bounded by the fromApp window; Python __del__ does NOT call
                  fixpp_msg_destroy (no-op on inbound).
  Flavour 2 — OUTBOUND mutable / CLONE (constructed by user):
    ├── owns:  yes — fixpp_msg_t* per fixpp_msg_create_outbound or fixpp_msg_clone
    ├── strong-refs: Session (the constructed-on session — Py_INCREF on Message.__init__)
    └── lifetime: Python-owned; __del__ calls fixpp_msg_destroy.

Application (Python, user-owned)
  ├── owns:  no native handle
  └── strong-refs: Session.register_application stores app in session._application;
                   the user typically holds their own reference too.

Dictionary (Python, owning)
  ├── owns:  fixpp_dict_t* via Dictionary.load_xml → fixpp_dict_load_from_xml
  └── strong-refs: 0 (Dictionary is a root; the engine refcounts internally per [2i §5.3])
```

**Sentinel pattern.** Each Python wrapper carries a private `_handle: ctypes.c_void_p` plus a `_dead: bool`. After the parent's `close()` is called, the parent walks its child weakrefs and sets each child's `_dead = True`. Subsequent accessor calls check `_dead` first; if set, raise `fixpp.ObjectLifetime` (numeric 1202 per §6.7) without calling into the C ABI.

**Weakref discipline.** Each `Session` holds a `weakref.WeakSet` of derived `Message` (inbound flyweights are added on construction by the SWIG director; outbound `Message`s are added on `Message.__init__`). On `Session.close()`, the weakref set is walked and each `Message`'s `_dead` is set. The walk happens BEFORE `fixpp_session_close` is invoked (to ensure no Python-side accessor races with the close).

**The cycle hazard.** A `Message` strong-refs its `Session`; a `Session` strong-refs (via WeakSet) its `Message`s — no, weak-refs don't cycle. But a user-supplied `Application` strong-refs the `Session` (via the `register_application` storage) and the `Session` strong-refs the `Application` (via its `_application` slot). This IS a reference cycle. The Python GC handles it (CPython has a cycle collector); both objects are released when neither has external references.

**Pickleability — Engine / Session / Message / Application / Dictionary are NOT pickleable** (NEW in v0.3 — Opus round-2 N-2-P2-1 close). Each of these classes wraps an opaque C-ABI handle (`fixpp_engine_t*`, `fixpp_session_t*`, `fixpp_msg_t*`, an engine-side `_application` slot, `fixpp_dict_t*`) whose validity is bounded by the host process's address space and the engine's lifetime. A pickled handle is just a `void*` integer; an unpickling process cannot use it (the engine in the unpickling process knows nothing about that handle), and even within the same process a pickled-then-unpickled handle outliving the engine would dereference a freed pointer — violating PY-004's "Python objects must not outlive native sessions / engines" contract per `[const §X.5]`'s opaque-handle uniform-destroy discipline. The Python wrappers therefore implement `__reduce_ex__(protocol)` / `__reduce__` to raise `TypeError("fixpp.<ClassName> objects are not pickleable; native handles cannot cross process boundaries")`. By contrast, `Decimal`, `MsgVersion`, `EngineConfig`, `SessionConfig`, `LogConfig` are value-typed dataclasses with no native handles and **are** pickleable via the default dataclass `__reduce_ex__`. §9 seam #3 verifies that `pickle.dumps(engine)` / `pickle.dumps(session)` / `pickle.dumps(message)` raises `TypeError` (the most common silent-failure case is `multiprocessing.Pool(...).map(callback, list_of_messages)` patterns; the explicit `TypeError` is the preferred surface).

**Cycle-GC finalisation order is unspecified — explicit close is the supported teardown path** (NEW in v0.2 — Opus N-P2-2 close). Both `Session` and `Application` (and `Engine`) hold native handles via `__del__`. Per PEP 442 (Python 3.4+), CPython's cycle collector runs `__del__` for objects in cycles, but the **order** of finaliser invocation within a cycle is *unspecified*. If `Session.__del__` runs before `Application.__del__`, `Application` may hold a stale `Session` reference; the resulting native-handle access is undefined at the C ABI. The guarantee is therefore: **explicit `Engine.close()` / `Session.close()` is the supported teardown path; cycle-GC-driven finalisation is best-effort only**. The recommended idiom is the `with Engine(config) as engine:` context-manager pattern (which calls `close()` on `__exit__` deterministically before any cycle-GC could run). v1.0 emits a `DeprecationWarning` if cycle-GC teardown is detected without prior explicit close (the engine sets a `_was_explicitly_closed: bool = False` flag that `__del__` reads); v1.x escalates to a hard error. §9 seam #3 verifies the warning fires.

**Engine close flow (the critical sequence).**

1. User calls `engine.close()`.
2. Engine walks its `weakref.WeakSet[Session]` and calls `session.close()` on each.
3. Each `session.close()`:
   a. Walks its `weakref.WeakSet[Message]`; sets `_dead = True` on each.
   b. Releases its `_application` ref (breaks the Application↔Session cycle).
   c. Calls `fixpp_session_close(session_handle)`.
4. After all sessions are closed, engine calls `fixpp_engine_destroy(engine_handle)`.
5. Engine sets `self._dead = True`.

Verified by §9 seam #3 (Python `Session` outlives the engine) and §9 seam #8 (Python `Message` outlives the session).

### §6.3 Exception translation

**Boundary 1: `fixpp_error_t` → `fixpp.FixppError`.** Per §4.6 mapping table. Implemented at every SWIG-generated wrapper return site:

```python
# fixpp/errors.py — the mapping logic, called by SWIG-generated wrappers.
def raise_if_error(code: int, name: Optional[str] = None) -> None:
    """Translate a non-zero fixpp_error_t to an exception and raise it."""
    if code == 0:  # FIXPP_ERR_OK
        return
    cls = _map_to_class(code)
    sym_name = name or _lookup_symbolic_name(code)  # via fixpp_strerror or table
    msg = _strerror(code)  # calls fixpp_strerror via the C ABI
    err = cls(msg)
    err.code = code
    err.name = sym_name
    err.message = msg
    raise err
```

**Cancellation handling.** Per `[2i §4.9]`: every cancellation source maps to `FIXPP_ERR_CANCELLED = 1` → Python `Cancelled` exception. The Python user catches `Cancelled` once for all cancellation cases (transport read, TLS handshake, store write, etc. all surface as `Cancelled`).

**Boundary 2: Python exception → `fixpp_error_t`.** A Python exception raised inside an `Application` callback is captured by the SWIG director adapter, **never** propagates back into engine C++ code. v0.2 closes Codex P1-4 / P2-5 / Opus N-P1-2: the SWIG director does NOT call any engine-internal logger (no `fixpp_python_callback_log_fatal` symbol exists; `fixpp::core::Logger` is engine-internal C++ that `bindings/python/` cannot access per §1.3 rule (3) / `[2k §2]` non-goal #7). The director writes the traceback to Python's `sys.stderr` via `PyErr_PrintEx(0)` (CPython's standard mechanism — Python itself implements this with stable Python-heap allocations, all on the Python heap per §1.3 rule (3) and §3.9 RC#1):

```c
/* SWIG director method — engine called us; we called Python; Python raised. */
PyObject *result = PyObject_CallMethodObjArgs(self->py_application, "fromApp", ...);
if (!result) {
    /* Python exception is set on the GIL state. */
    PyObject *py_type, *py_value, *py_traceback;
    PyErr_Fetch(&py_type, &py_value, &py_traceback);
    PyErr_NormalizeException(&py_type, &py_value, &py_traceback);

    /* Re-set the exception so PyErr_PrintEx can format it. */
    PyErr_Restore(py_type, py_value, py_traceback);
    PyErr_PrintEx(0);  /* writes traceback to sys.stderr; allocates on Python heap under GIL */
    PyErr_Clear();

    return FIXPP_ERR_BINDING_PYTHON_CALLBACK_RAISED;  /* numeric 1200 */
}
```

The C-ABI return is `FIXPP_ERR_BINDING_PYTHON_CALLBACK_RAISED`; the engine observes only this `fixpp_error_t` value. The engine MAY emit a structured log record like "code 1200 from session strand" using its **own internal logger** with only the `fixpp_error_t` value as input — **no Python traceback string crosses `extern "C"`**. The FSM treats a callback-raised the same as a `fromApp` returning normally with a sequence-number reject (Session-Reject per `[FIX-SL §4.7]`); the session continues. The Python-side observer of the failure is the traceback printed to `sys.stderr` (capturable by `pytest`'s `capsys` fixture or a user's stderr redirection).

**No exception across `extern "C"`.** The `PyErr_Fetch` / `PyErr_Restore` / `PyErr_PrintEx` / `PyErr_Clear` sequence guarantees the Python exception is consumed in the director adapter; no Python exception survives the return into the engine's C++ callback dispatch. Verified by §9 seam #2.

### §6.4 Async callback handoff (the `[SYN §3.5 #18]` decision)

**Decision (v1.0): synchronous reacquire-and-call on the engine strand thread.**

When an inbound message arrives:
1. The engine FSM dispatches `fromApp` on the session strand thread per `[2d §7.6]`.
2. The C-ABI receive-callback trampoline (a steady-state thunk per `[2i §5.2]`) is invoked with the inbound `fixpp_msg_t*`.
3. The SWIG director adapter:
   a. Acquires the GIL via `PyGILState_Ensure`.
   b. Sets `session._in_callback = True` on the Python `Session` instance (per §1.3 rule (4) / §6.5 enforcement). **[054 / Article XX: this step is the PY-004 director design; the v1.0 flat binding omits the marker — see §1.3 rule (4) and §6.5 enforcement amendment.]**
   c. Constructs a Python `Message` wrapper with `_is_inbound = True`, `_dead = False` (one `PyObject*` allocation on the Python heap per RC#1 / §3.9 v0.2; not on the engine arena).
   d. Calls `application.fromApp(session_py, msg_py)`.
   e. If the call raises, captures and translates per §6.3 boundary 2 (`PyErr_PrintEx(0)` to `sys.stderr`; return `1200`).
   f. **Arms the `_dead` sentinel BEFORE GIL release**: `msg_py._dead = True` (so any post-return capture-and-access raises `fixpp.ObjectLifetime` (1202) instead of dereferencing a stale `fixpp_msg_t*`).
   g. Clears `session._in_callback = False`. **[054 / Article XX: PY-004 director design; v1.0 flat binding omits.]**
   h. Releases the GIL via `PyGILState_Release`.
4. The C-ABI return code propagates back to the FSM.

**Justification against `[arch §4.12]`.** The architectural rule is "GIL handling: reacquire-and-call on receive callbacks (v1.0); async-queue handoff is documented as future work" — verbatim. v1.0 implements that rule.

**Justification against `[const §XV.15]`.** `drop-oldest` is banned on the application/session message path. A queue-handoff design would need to choose between `block` mode (the queue back-pressures the FSM dispatch — fine, but adds a thread + a synchronisation primitive for no gain in v1.0) and `drop-oldest` (banned). v1.0 picks the simpler shape and defers queue handoff to v1.x where the design space is wider.

**The trade-off (and what we lose).**
- **Pro.** Simple to implement; testable; no extra threads; no extra synchronisation; lowest possible latency (no queue inflate / drain).
- **Pro.** Direct mapping to the C++ `Application` shape per `[arch §6]` last paragraph (the 6-method `Application` justification — `library/.specify/architecture.md:446`) — the user's mental model is "Python callback runs where C++ callback would run."
- **Con.** A slow Python `fromApp` (e.g., one that does network I/O, or a synchronous database write) blocks the session strand. This blocks all subsequent message dispatch on the same strand and stalls heartbeats.
- **Mitigation.** The user runs slow Python work on a separate Python thread; from `fromApp`, they `msg.clone()` then `queue.put(msg_clone)` (where `queue` is a `queue.Queue` they drain in another thread). The clone is ~1 µs warm-cache per `[2i §6.4]`; the strand returns immediately.
- **Mitigation 2.** Document the pattern prominently in the `examples/python/` cookbook and CI-test it (§9 seam #4).

**Asyncio adapter (post-v1.0).** A future `fixpp.aio.AsyncSession` would expose `async def recv(self) -> Message: ...` driven by a producer-consumer queue between the engine strand thread and the user's asyncio event loop. The queue MUST be `block` mode per `[const §XV.15]`; `drop-oldest` is structurally banned. Tracked in §10 Q1.

### §6.5 Threading carve-outs ("what may a Python callback call?")

**Reentrancy at the C ABI per `[2i §4.10]`:** every `fixpp_msg_get_*` / `fixpp_msg_set_*` is `FIXPP_REQUIRES_SESSION_LOCK` — must run on the session strand. From inside `fromApp` (which IS on the session strand per `[2d §7.6]`), all of these are legal at the C ABI. The Python binding does **not** invent stricter rules per §1.3 rule (2).

**The Python question:** what may a Python callback call? The v0.2 table is normalised against §1.3 (rules 1–4):

| Python call | Legal from inside `fromApp` (or other inbound callback)? | Why / mitigation |
|---|---|---|
| `msg.get_string(tag)` (and other inbound accessors) | YES | `FIXPP_REQUIRES_SESSION_LOCK`; we're on the strand. |
| `msg.set_string(tag, value)` on **inbound** `msg` | **NO** | Per `[2i §10 Q5]` DECIDED v0.2 / §1.3 rule (1): `fixpp_msg_set_*` on an inbound flyweight returns `FIXPP_ERR_INVALID_HANDLE`. The Python wrapper raises `fixpp.CapiError` with `code = 4` (the `_is_inbound = True` flag short-circuits before the C-ABI round-trip). Use `msg.clone()` first to get a mutable outbound-shaped copy. (Codex P1-3 / Opus N-P1-1 close.) |
| `msg.set_string(tag, value)` on **outbound** `msg` (passed to `toAdmin` / `toApp` or constructed via `Message(msg_type, session)`) | YES | The set path mutates the per-message arena per `[2i §4.7]`. This is the intended use of `toAdmin` / `toApp` — outbound mutation. |
| `msg.clone()` | YES | `fixpp_msg_clone` is `FIXPP_REQUIRES_SESSION_LOCK`; we're on the strand. |
| `session.send(other_msg)` | **NO as-built (L-054-1)** — design-legal, blocked by the 050 blocking shape | **[054 / L-054-1 amendment, Article XX]** The v0.2 "YES" assumed the *non-blocking strand-dispatch* `fixpp_session_send`. The **as-built 050** `fixpp_session_send` blocks (`co_spawn(ioc_, …, use_future)` + `fut.get()`; `src/capi/session.cpp:284-286`, rule `session.h:255-258`), so from inside the callback (on the strand) it **DEADLOCKS** — the worker blocks on `fut.get()` for a coroutine that needs the same strand. A strand/io_context reentrancy deadlock, **distinct** from the close-from-callback row below and from the 053 GIL-teardown deadlock. **Documentary** (callback docstring); active detection = PY-004. Mitigation: `msg.clone()` from the callback and `queue.put()` to drain on **another thread**, then `session.send()` off the strand. Restoring true legality (non-blocking send-from-callback) is a deferred **engine** item. _(Original v0.2 rationale retained for history: `FIXPP_REQUIRES_SESSION_LOCK`, dispatched on the strand per `[2i §4.10]` / `[2d §7.6]`; the C ABI carves this as legal; the v0.1 ban conflated CPython's reentrant GIL acquire semantics with strand-level deadlock — but the as-built blocking shape reintroduces a genuine, different deadlock.)_ |
| `engine.close()` / `session.close()` | **NO** | Closing while inside a callback deadlocks: the close path posts a "drain the strand" closure and waits for the strand to drain, but the strand is mid-callback waiting for the close to return. **[054 / Article XX amendment]:** The **designed** behavior (PY-004 SWIG director) is for the Python wrapper to raise `fixpp.CallbackReentrantClose` (1204) **without entering the C ABI** — v0.3 split out from the v0.2-draft `GilDeadlock` (1201) per Codex round-2 P2-1. **As-built v1.0 flat binding (053/054):** no director, no `_in_callback` marker, no pre-call check — close-from-callback **DEADLOCKS** (same shape as send-from-callback, L-054-1). The binding guarantee is **DOCUMENTARY** (the module docstring states the hazard); active `CallbackReentrantClose` detection is **PY-004**. (No alias for the v0.2-draft name is retained; v0.3 is the first signed-off version.) The §6.1 sub-interpreter case is a distinct rejection at construction time and surfaces as `SubInterpreterRejected` (1201). |
| `dict.load_xml(path)` | YES (rare; not perf-sensitive) | `FIXPP_SINGLE_THREAD`; not on a session strand reentrancy. |
| Calling user's own Python code | YES | Including `print`, `logging`, `numpy.array(...)`, etc. |
| `time.sleep(seconds)` | YES (legal — but blocks the strand) | The strand is single-threaded; sleeping blocks all other dispatch. Document as "don't do this." |
| `os.fork()` / `subprocess.Popen(...)` | YES (legal) | Standard CPython semantics. |

**Enforcement of the close-from-callback ban — GIL-protected session-local marker, NOT thread-id detection.** Per §1.3 rule (4) (NEW in v0.2 — replaces the v0.1 thread-id detection that was unsound under `[2d §4.5]` `engine_thread_pool_strand` mode; Opus N-P1-3 close):

> **[Article XX amendment — 054-python-gil-exceptions, close-from-callback]:** The marker mechanism described below is the **designed behavior (PY-004 SWIG director)**, not the as-built v1.0 flat binding. The **v1.0 flat binding** (053/054) has **no director, no `_in_callback` marker, and no pre-call check**. Close-from-callback in v1.0 **deadlocks** (same as send-from-callback per L-054-1); the guarantee is **documentary**. Active pre-call `CallbackReentrantClose` (1204) detection (this whole enforcement mechanism) is **PY-004**. The design prose below is retained as the normative PY-004 specification.

- The SWIG director's entry path stores `session._in_callback = True` on the *Python `Session` instance* (NOT on a `threading.local()` and NOT keyed by `threading.get_ident()`), guarded by the GIL.
- The director's exit path stores `session._in_callback = False` BEFORE releasing the GIL on return.
- `Session.send` / `Session.close` / `Engine.close` check `self._in_callback` directly (or, for `Engine.close`, walk all child sessions checking each `_in_callback`).
- If the close path observes any session's `_in_callback == True`, it raises `fixpp.CallbackReentrantClose` (1204) without entering the C ABI.
- **`_in_callback` check race (NEW v0.3 — Opus round-2 N-2-P3-3 close).** If `Engine.close()` is called from Python thread A while another thread B is mid-callback on session S2, the GIL serialises Python execution but the strand-thread B has already entered the C-ABI thunk and may have temporarily released the GIL while doing C-ABI work. The `_in_callback = True` flag on S2 was set under the GIL in the SWIG director's entry path; `Engine.close()` reads each session's `_in_callback` while holding the GIL. The race is therefore bounded by the GIL acquire/release granularity, not by the strand: any in-flight callback either has the flag set when `close` examines it (and `close` raises `CallbackReentrantClose`) or has cleared it before `close` examined it (and `close` proceeds, blocking the strand-side teardown via the engine's normal session-close drain). Acceptable in practice; the spec records the race description for completeness.

This works correctly under all three `[2d §4.5]` threading modes (`per_session_strand`, `engine_thread_pool_strand`, `direct_executor`): the GIL serialises Python execution; only one callback runs in Python at a time per process; the `_in_callback` flag on the `Session` Python object is GIL-protected and survives strand resumption on a different OS thread (because it lives on the Python heap, not in `threading.local()`). The `_strand_thread_id` field is **dropped** from the §4.3 `Session` class layout (Opus N-P1-3 close).

§9 seam #4 verifies this under `EngineConfig(io_threads=4)` (multi-threaded I/O — the production-likely shape) where v0.1's `threading.get_ident()` check would fire intermittently / fail.

### §6.6 Allocation / exceptions / threading sub-section + latency Tier 1 ceilings

**Allocation discipline.** Per RC#1 / §3.9 v0.2 / §1.3 rule normative scope: the SWIG director runs on the engine strand thread but is **outside the engine's PMR/arena allocator pool** — it allocates exclusively from the Python heap. `[const §VIII.5]`'s "zero `new`/`delete` between parse and `fromApp`" rule is an *engine-side* discipline (verified by `mallocnesia`'s `fixpp::*` / `fixpp_*` symbol-scope filter; see §9 seam #1 acceptance-criterion update). The Python user has opted out of the no-alloc contract by using a Python binding; per-callback Python-heap allocations are permitted.

| Operation | Allocation site | Counts toward `[const §VIII.5]`? |
|---|---|---|
| SWIG director adapter constructing the Python `Message` wrapper for inbound dispatch | Python heap — `PyObject_Malloc` pool | **No** — Python heap, not engine arena; outside `mallocnesia`'s symbol-scope filter. |
| SWIG director allocating the argument tuple, bound-method-object-wrapper, result `PyObject*` for vectorcall | Python heap | **No** (same reasoning). |
| Python user code inside `fromApp` (e.g., `numpy.array(...)`, `dict[k] = v`) | Python heap (user's responsibility) | **No** (the engine has yielded the strand to user code per the §3.9 RC#1 paragraph; the SWIG director / GIL spine sits at `[arch §4.12]`). |
| `msg.get_string(tag)` returning a Python `str` (one `PyObject*` per call; `str` is a copy of the C buffer) | Python heap (HELD GIL) | **No**. |
| `msg.set_string(tag, value)` | Python `str` is borrowed (`PyArg_ParseTuple` `s` typecode); the engine deep-copies into the per-message arena per `[2i §4.7]` (engine-side) | Engine-side allocation is on the per-message arena (PMR) — within `[const §VIII.5]`'s scope but explicitly permitted by `[2i §4.7]`'s set-path contract. |
| `msg.clone()` | One Python wrapper allocation (Python heap); the underlying `fixpp_msg_clone` allocates from the per-message arena per `[2i §4.7]` (engine-side) | Same as `set_string` row — engine arena is within `[2i §4.7]`'s permitted set. |
| Director-path exception capture (`PyErr_PrintEx(0)`) | Python heap (CPython's standard `sys.stderr` formatting) | **No** — Python heap; on the §1.3 rule (3) sanctioned path. |

**The "one PyObject per callback" cost.** Approximately 50 ns of CPython `PyObject_Init` + dictionary slot — on the Python heap, not the engine arena. The wrapper-pool optimisation (§10 Q10 / §6.6 latency-lever — at session open allocate 16 `Message` wrappers; on `fromApp` entry pull from a free list; on `fromApp` exit return to the free list) is a Python-side latency lever, **not a `[const §VIII.5]` correctness blocker** per RC#1 / §3.9 v0.2. v1.0 ships naive per-callback allocation; the pool is post-v1.0 (§10 Q10).

**Exceptions.**
- No exception crosses `extern "C"` (per §6.3 boundary 2 / `[2i §5.2]`).
- A SWIG-generated wrapper turns C-ABI errors into Python exceptions via §6.3 boundary 1.

**Threading.**
- The engine strand thread runs the C++ FSM and the SWIG director adapter.
- The user's Python thread(s) call into `Session.send`, `msg.get_*`, etc.
- The GIL serialises Python execution; the strand serialises C-ABI execution.

**Latency Tier 1 ceilings.**

| Operation | Ceiling | Rationale |
|---|---|---|
| `msg.get_string(tag)` warm-cache | **≤ 1 µs p99** | C-ABI `fixpp_msg_get_string` ≤ 50 ns per `[2i §6.4]` + SWIG marshalling ~500 ns + Python `str` construction from C buffer (`PyUnicode_FromStringAndSize`) ~200 ns + return-value packaging ~100 ns ≈ ~850 ns. Provisional until v1.0 bench data. |
| `msg.get_int(tag)` warm-cache | **≤ 1 µs p99** | C-ABI ≤ 80 ns + SWIG ~500 ns + Python `int` construction ~100 ns. |
| `msg.get_decimal(tag)` warm-cache | **≤ 1.5 µs p99** | C-ABI ≤ 80 ns + SWIG ~500 ns + `fixpp.Decimal` Python wrapper construction ~700 ns (Python class with `__slots__` + 16-byte PoD copy). |
| `msg.set_string(tag, value)` warm-cache, ≤ 64-byte value | **≤ 1.5 µs p99** | C-ABI ≤ 200 ns + SWIG ~700 ns (string borrow + length compute) + Python overhead ~600 ns. |
| Cached enum lookup (e.g., `SecurityProfile.MTLS_CA`) | **≤ 200 ns p99** | Python class attribute access; HELD GIL; no C-ABI call. |
| `msg.clone()` warm-cache, ~200-byte msg | **≤ 5 µs p99** | C-ABI ≤ 1 µs per `[2i §6.4]` + GIL release/acquire ~100 ns + Python wrapper construction ~500 ns. The release/acquire is justified because the underlying `fixpp_msg_clone` may be longer for big messages. |
| `Application.fromApp` callback dispatch (engine → Python) | **≤ 5 µs p99 dispatch overhead** | Engine-side strand resume ~200 ns + C-ABI thunk ~100 ns + GIL acquire ~100 ns + Python `Message` wrapper alloc ~50 ns + Python method call ~500 ns + `_dead` sentinel arm + GIL release ~100 ns ≈ ~1 µs of pure overhead. The Python user's `fromApp` body is on top of that and is not 2m's concern. |
| `Application.fromAdmin` callback dispatch (engine → Python) | **≤ 5 µs p99 dispatch overhead** | Same SWIG director path as `fromApp`; the `Message` is inbound (immutable per §1.3 rule (1)). Latency budget identical to `fromApp`. |
| `Application.toApp` callback dispatch (engine → Python) — **outbound hot path** | **≤ 5 µs p99 dispatch overhead** | (NEW in v0.2 — Opus N-P1-4 close.) Every outbound message — including high-frequency app messages like NewOrderSingle — fires `toApp` *before* serialisation onto the wire. Under the §6.4 synchronous reacquire-and-call shape, the entire send path waits on Python execution. For 100k orders/sec, a 5 µs `toApp` overhead is 500 ms/sec — significant; users who do not need outbound mutation should leave `toApp` unimplemented (the C++ no-op base is then used and no SWIG-director cost is paid). The post-v1.0 escape hatch is the Python user *not subclassing* `Application.toApp`. |
| `Application.toAdmin` callback dispatch (engine → Python) — **outbound admin path** | **≤ 5 µs p99 dispatch overhead** | (NEW in v0.2.) Lower-frequency than `toApp` but same SWIG director cost; budget is symmetric. |
| `Application.onLogon` / `onLogout` callback dispatch (engine → Python) | **≤ 10 µs p99 dispatch overhead** | (NEW in v0.2.) Once-per-session events; the budget is looser to allow user-side initialisation work in the callback. Higher-throughput users should defer heavy work off-strand via `msg.clone()` + worker-thread pattern. |

CI flags > 5% regression on the hot-path rows per `[const §VIII.2]`. Verified by §9 seam #1; outbound-callback rows additionally verified by §9 seam #13 (NEW — `toApp` overhead measurement across 1M outbound messages, asserts ≤ 5 µs p99 dispatch). Note: 2m's accessor-row ceilings (≤ 1 µs SWIG-end-to-end) are **20× the C++/C-ABI ceiling** (≤ 50 ns) — that's the cost of the language boundary; we target the boundary cost itself (~500 ns of SWIG marshalling) rather than the absolute end-to-end.

### §6.7 Errors introduced by this design

2m introduces **5 new `fixpp_error_t` variants** in the `[1200, 1299]` C-ABI block reserved for 2m by `[2i §1.1]` (v0.3: 5 variants, up from v0.2's 4 — Codex round-2 P2-1 split `GilDeadlock` into a distinct sub-interpreter code 1201 + a distinct callback-reentrant-close code 1204). Per the `[2i §4.3]` stability rule and the `[2i §1.1]` 2× headroom rule, the block accommodates ≥ 8 future variants without overflow (95 codes still free in `[1205, 1299]`).

| `fixpp_error_t` variant | Numeric | Source section | Remediation class | Python class |
|---|---|---|---|---|
| `FIXPP_ERR_BINDING_PYTHON_CALLBACK_RAISED` | 1200 | §6.3 — a Python `Application` callback raised an unhandled exception; SWIG director captured + translated. | Programmer error (Python) — fix the callback. The Python traceback is printed to `sys.stderr` via `PyErr_PrintEx(0)` (capturable by `pytest`'s `capsys`); the engine logger is **not** called from the director path per §1.3 rule (3) / `[2k §2]` non-goal #7. The engine MAY log "code 1200 from session strand" via its own internal logger using only the `fixpp_error_t` value — no traceback string crosses `extern "C"`. | `PythonCallbackRaised` |
| `FIXPP_ERR_BINDING_SUBINTERPRETER` | 1201 | §6.1 — `Engine.__init__` was invoked from a CPython sub-interpreter (PEP 554 / `interpreters` API); v1.0 supports only the main interpreter. (v0.3: split out from v0.2's `FIXPP_ERR_BINDING_GIL_DEADLOCK` per Codex round-2 P2-1; the sub-interpreter case is semantically wrong-interpreter UB-prevention, not a deadlock.) | Programmer / deployment error — construct `Engine` from the main interpreter only. | `SubInterpreterRejected` |
| `FIXPP_ERR_BINDING_OBJECT_LIFETIME` | 1202 | §6.2 — a Python wrapper accessor was called after the parent native handle was invalidated (e.g., parent `Session` closed). | Programmer error — observe the close ordering / use `Session` as a context manager. | `ObjectLifetime` |
| `FIXPP_ERR_BINDING_WHEEL_ABI_MISMATCH` | 1203 | §1 / §6.1 — the wheel's bundled `_fixpp.so` ABI does not match the SWIG wrapper's compiled-in expectation. Should never happen (cibuildwheel guarantees match); surfaces if a user manually replaces the binary. | Configuration error — reinstall the wheel. | `WheelAbiMismatch` |
| `FIXPP_ERR_BINDING_CALLBACK_REENTRANT_CLOSE` | 1204 | §6.5 — a Python callback called `engine.close()` / `session.close()` from inside the callback; the close path waits for the strand to drain but the strand is mid-callback. The designed behavior (PY-004 SWIG director) is for the §1.3 rule (4) GIL-protected session-local marker (`session._in_callback`) to detect the case before the C-ABI call and surface this code instead of allowing the native deadlock. **As-built v1.0 flat binding (053/054): no marker, no pre-call check — close-from-callback deadlocks; enforcement is DOCUMENTARY (PY-004).** [054 / Article XX] (v0.3: split from v0.2-draft's `FIXPP_ERR_BINDING_GIL_DEADLOCK` per Codex round-2 P2-1; this is the genuine deadlock case.) | Programmer error — restructure to call `close()` from a non-callback Python thread (typical pattern: a separate "shutdown coordinator" thread that signals the FSM out-of-band). | `CallbackReentrantClose` |

**Stability rule applied.** Each numeric value (1200, 1201, 1202, 1203, 1204) is published in the v1.0 wheel's compiled-in headers; subsequent v1.x wheels never re-bind these slots. Future variants append at 1205+. The v0.2 → v0.3 numeric reshuffle (1201's semantic split) does not violate `[const §X.4]` because v0.2 was a draft, never a tagged release; no published numeric value is re-bound to a different Python class.

**Coalescing groups for 2m.** Unlike 2b/2d/2e/2g/2h which coalesce many internal variants into 3–4 C-ABI codes, 2m's 5 variants are already at the right granularity — each represents a distinct programmer-action class. No further coalescing needed.

**Cross-cutting cancellation.** A Python callback that wants to indicate "abort the FSM" cannot do so directly — `fromApp` returns `None` (success path) or raises (callback-raised path). To cancel, the user calls `session.close()` from a different Python thread; the engine's cancellation propagation per `[2d §4.7]` translates each blocked operation to `FIXPP_ERR_CANCELLED → fixpp.Cancelled` per `[2i §4.9]`.

**Appendix D drop-in shape.** See Appendix D §D.1 for the exact Before / After block adding these 5 variants (v0.3: 5, up from v0.2's 4 per Codex round-2 P2-1 split) to `[2i §4.3]` enum table, `[2i §1.1]` magnitude table, and `[2i §6.5]` introduced-variants table; §D.4 (NEW v0.3) queues the `fixpp_session_post` declaration in `[2j §11]` hand-off table. The drop-in is queued for application at 2m sign-off; it amends 2i v0.3 → v0.4 (or whatever the live 2i revision is at sign-off time).

---

## §7 Integration with adjacent modules

### §7.1 2a (decimal PoD at C boundary)

Per `[2a §5.1]` lines 233–240: `fixpp_decimal_t` is `(int64 mantissa, int8 exponent, int8 _reserved[7])`. Per `[2a §5.2]` boundary functions live in `c_api/decimal.h`.

**2m's role:** publish `fixpp.Decimal` Python class wrapping `fixpp_decimal_t`:

```python
@dataclass(frozen=True)
class Decimal:
    mantissa: int   # int64
    exponent: int   # int8

    @classmethod
    def parse(cls, s: str) -> "Decimal":
        """Calls fixpp_decimal_parse per [2a §5.2]."""

    def format(self) -> str:
        """Calls fixpp_decimal_format with a 41-byte buffer per [2a §5.2]."""

    def __eq__(self, other: object) -> bool:
        """Calls fixpp_decimal_equal per [2a §5.2]."""

    def __lt__(self, other: "Decimal") -> bool:
        """Calls fixpp_decimal_compare < 0."""
```

The PoD `_reserved[7]` bytes are zeroed at Python construction (the dataclass omits them; the SWIG `%typemap(in)` to C zeros the bytes). Round-trip preservation is verified by §9 seam #11 (mirrors `[2i §9]` seam #10).

**SWIG boundary type sizes — Python `int` is unbounded; C-ABI fields are sized.** (NEW in v0.2 — Codex P3-2 close, editorial.) Python `int` is unbounded; the C-ABI `fixpp_decimal_t.mantissa` is `int64_t` per `[2a §5.1]` line 234. SWIG's default `int64` typemap raises `OverflowError` on out-of-range Python `int` (the failure mode is not silent). v1.0 ships Linux x86_64 only as the mandatory wheel (per §1.1), so 32-bit truncation is moot for the mandatory artefact; the note is for users compiling against a 32-bit Python build (out of v1.0 scope but a likely v1.x consideration if 32-bit windows wheels become Tier 1). The same applies to other sized integer fields surfaced via SWIG: tag values (`uint16_t` in C-ABI per `[2c §5]`) raise `OverflowError` on Python `int > 65535`.

### §7.2 2b (wire view ↔ Python accessor lifetime)

Per `[2b §6.4]` flyweight lifetime contract: the C-ABI accessor returns a `const char*` aliasing the wire buffer; lifetime is bounded by the per-message arena slot.

**2m's role:** the Python `msg.get_string(tag)` returns a Python `str` constructed by **copy** from the aliased C buffer. The SWIG `%typemap(out)` is:

```python
%typemap(out) (const char* value, size_t len) {
    /* Construct a new Python str copying the C buffer.
     * The C buffer's lifetime is the fromApp window per [2b §6.4];
     * after this typemap returns, $1 / $2 are no longer dereferenced. */
    $result = PyUnicode_DecodeUTF8($1, $2, "strict");
}
```

The Python user CANNOT obtain a zero-copy view in v1.0. A `memoryview`-over-the-buffer accessor is post-v1.0 (would require careful lifetime tying via `__buffer__` protocol; tracked in §10 Q5).

### §7.3 2c (dictionary / typed message)

Per `[2c §5]` commitments 1–6: 2m is **dictionary-resolved at runtime**. The `fixpp.Message` Python class has no per-FIX-version subclasses in v1.0; the resolved version is exposed via `msg.version` of type `fixpp.MsgVersion`:

```python
@dataclass(frozen=True)
class MsgVersion:
    kind: MsgVersionKind  # SESSION_ADMIN | APPLICATION
    session: int          # session_version byte per [2c §5] commitment 1
    application: int      # application_version byte
```

Multi-version coexistence per `[2c §3]`: the engine loads multiple dictionaries; sessions pick one at open. Python users `import fixpp` once and use the same `fixpp.Message` class for all FIX versions; the dictionary is selected via `SessionConfig.fix_version` (a string like `"FIX.4.4"` resolved engine-side per `[2c §4.9]` `dict::version_registry`).

**`import fixpp.v44` legality.** **NOT supported in v1.0.** A typed namespace like `fixpp.v44.NewOrderSingle` would require codegen of Python-class-per-message stubs; that's post-v1.0 (§10 Q3).

### §7.4 2d (where the strand runs vs Python)

Per `[2d §4.5]`: each `Session` runs on a `strand` derived from `EngineConfig::executor`. Per `[2d §7.6]`: callbacks dispatch onto the strand by default.

**2m's role:** Python users supply `EngineConfig.io_threads = N`; the engine creates a thread pool of size `N` and runs all session strands on it. **Python users do NOT supply an executor object** — there is no Python ↔ ASIO executor adapter in v1.0.

**If the user has their own `asyncio` event loop:** it does NOT become the engine executor. The engine's strand threads are independent of the asyncio loop. To bridge: the user runs the engine on its own threads; their asyncio code uses `loop.run_in_executor(None, session.send, msg)` to call `Session.send` from asyncio without blocking the loop.

### §7.5 2e (store factories from Python)

Per `[2e §4.4]`: `MessageStoreFactory` is engine-anchor + session-override. The factory takes a `std::pmr::memory_resource*` argument per `[arch §6]`.

**2m's decision:** **BAN custom store factories from Python in v1.0.** Per §2 non-goal #6: Python users cannot supply a custom `MessageStore` factory. They pick one of the defaults via `SessionConfig.message_store_path`:
- `None` → `MemoryStore` (the engine default).
- `"/path/to/store"` → `FileStore` rooted at that path.

A user who needs a custom store writes a C++ plugin and links it into the engine; the Python wheel uses the linked default. Tracked for v1.x (§10 Q6).

### §7.6 2f (no Python `async_mutex` shim in v1.0)

Per `[2f §4.5]`: `async_mutex` is C++-coroutine-only. Python's `threading.Lock` and `asyncio.Lock` are sufficient for the rare Python-side serialisation need. Per §2 non-goal #8: no `fixpp.AsyncMutex` Python class in v1.0.

### §7.7 2g (cert_source factories from Python — banned in v1.0)

Per `[2g §7.6]`: TLS surface is C++-only in v1.0. Python users supply file paths via `SessionConfig.cert_source_path` and `SessionConfig.pinset_paths`; the engine constructs `file_cert_source` per `[2g §4.x]`. Custom Python cert sources are post-v1.0 (§10 Q7).

### §7.8 2h (transport factories from Python — banned in v1.0)

Per `[2h §7.8]`: transport surface is C++-only in v1.0. Python users get the default `asio_tls_transport` per `[2h §4.5]`; no `fixpp.Transport` Python class. Custom Python transports are post-v1.0.

### §7.9 2i (the actual surface)

**2m consumes `<fix/c_api.h>` as input** per `[2i §7.12]`. The wrapping is enumerated in §5. Where 2i defers a C-ABI surface (cert_source, pinset, transport, tap consumer), 2m inherits the deferral structurally — no Python wrapper is published.

**Cross-doc cancellation contract.** Per `[2i §4.9]`: every cancellation source maps uniformly to `FIXPP_ERR_CANCELLED`. 2m's Python translation surfaces this as `fixpp.Cancelled` exception. The Python user catches `Cancelled` once for all cancellation cases.

**Cross-doc forward-compat contract.** Per `[2i §4.4]` / `[const §X.4]`: an out-of-range `fixpp_error_t` is mapped to `FIXPP_ERR_UNKNOWN` on the return path. The Python wheel built against C-ABI v1.0 receives `FIXPP_ERR_UNKNOWN` for any v1.x-introduced variant beyond its compile-time table, and surfaces it as `fixpp.Unknown` exception. The Python user catches `Unknown` for "this engine knows about more error codes than my wheel does — please upgrade your wheel."

### §7.10 2j (control plane — Python uses gRPC directly)

Per `[2j §1.2]`: control-plane consumers use gRPC (the default). Python users who need the control plane use **`grpcio` + the project's published `.proto`** — no `fixpp.ControlPlaneClient` Python shim in v1.0. The gRPC schema lives at `service/proto/fixpp_control.proto` per `[2j §4.7]`; the Python client is generated by `grpc_tools.protoc`.

A Python user typically runs `fixpp.Engine` in-process for direct C++-FFI access (no need for a control plane to themselves) OR runs `fixppd` separately and uses gRPC to talk to it — in the latter case, no `fixpp` Python wheel is needed for the consumer (only the gRPC stubs).

### §7.11 2k (Logger — Python `logging` adapter? No.)

Per `[2k §5]`: log C-ABI is a placeholder in v1.0. **2m's decision:** no Python `logging` adapter in v1.0. Python users configure logging via `EngineConfig.log_config` (which routes through the engine's C++ logger to `FileSink` / `OtlpLogSink` / `SyslogSink` per `[2k §4.4]`–`[2k §4.7]`). Reading log records back into Python (e.g., adapting them to `logging.Logger`) is post-v1.0 — requires the C-ABI subscription surface deferred in `[2k §10]` Q1.

### §7.12 2l (tap — Python sync callback shape — deferred)

Per `[2l §1.1]`: tap C-ABI is deferred to a 2i amendment. **2m's decision:** no Python tap surface in v1.0. The `fixpp.RingBufferTap` / `fixpp.SyncCallbackTap` Python wrappers are post-v1.0.

A Python user who needs tap subscription in v1.0 runs `fixppd` with `Iox2Tap` enabled and uses the `iceoryx2` Python bindings to subscribe to the SHM topic per `[2l §4.5]`. This is documented in the user-facing docs; not part of the v1.0 `fixpp` wheel surface.

---

## §8 PMR — recap

**Python bindings allocate on Python's heap (ref-counted CPython).** There are **NO PMR arenas on the Python side.**

The C-ABI surface always returns either:
- An owned value (a `fixpp_decimal_t` PoD struct copied by value, an `int64_t`, etc.), OR
- An opaque handle (a `fixpp_msg_t*`, etc.) with explicit lifetime rules per `[2i §4.2.1]`.

**Python wrappers never alias engine arenas.** The Python `Message.get_string(tag)` returns a Python `str` that is a **copy** of the aliased C buffer — the alias is dropped at SWIG `%typemap(out)` exit. The Python `Decimal` is a value-typed copy of the PoD `fixpp_decimal_t`. The Python `Message` itself wraps a `fixpp_msg_t*` whose underlying arena is engine-managed; the Python wrapper never sees the arena memory directly.

**Storage class table (mirrors `[2i §8]`).**

| Storage | Lifetime | Holds | Reset by |
|---|---|---|---|
| **Python heap (CPython refcounted)** | Determined by Python GC | Every `fixpp.Engine` / `fixpp.Session` / `fixpp.Message` / `fixpp.Decimal` / etc. Python wrapper. | Python `__del__` (which may call `fixpp_*_destroy` for owned-handle types). |
| **Engine arena (per `[arch §5.2]`)** | Engine lifetime | Engine-internal state. Python wrappers see opaque handles; never touch the arena bytes directly. | `fixpp_engine_destroy` (called from `Engine.__del__` / `Engine.close`). |
| **Session arena** | Session lifetime | Session-internal state (MessageStore, Pinset, Transport, etc.). Python wrappers see opaque handles. | `fixpp_session_close` (called from `Session.__del__` / `Session.close` / parent's `Engine.close`). |
| **Per-message arena** | One outbound message construction cycle, OR one inbound `fromApp` dispatch window | Wire buffer (outbound), offset-table cache (inbound), cursor objects, deep-copies of setter values. Python wrappers see the `fixpp_msg_t*` handle; the bytes are engine-side. | `fixpp_msg_destroy` (outbound, called from `Message.__del__`) or `fromApp` return (inbound, engine-managed). |

**Per `[const §VIII.5]`: zero `new`/`delete` between parse and `fromApp` (engine-side discipline; not a Python-heap rule).** Per RC#1 / §3.9 v0.2 / §1.3 normative scope: `[const §VIII.5]` is an **engine-side PMR/arena allocator discipline**, verified by `mallocnesia` per `[arch §5.2]` line 381 with the symbol-scope filter restricted to `fixpp::*` and `fixpp_*` symbols (§9 seam #1 acceptance criterion). The SWIG director adapter on the engine strand thread allocates Python-heap objects (one `PyObject*` per inbound callback for the `Message` wrapper, plus argument tuple and result `PyObject*` for the vectorcall) — this is on **CPython's refcounted `PyObject_Malloc` pool**, NOT the engine's PMR arenas, and is **outside `[const §VIII.5]`'s scope** because the SWIG director represents the architectural transition where the engine has handed control to user code (the SWIG director / GIL spine architectural anchor is `[arch §4.12]` per §3.1; the strand-yielded-to-user-code framing is a 2m-internal axiom — v0.3, Opus round-2 N-2-P3-1 close). The Python user has opted out of the no-alloc contract by using a Python binding; per-callback Python-heap allocations are permitted and not a violation. The §9 seam #1 allocation guard enforces this scoping: any `fixpp::*` / `fixpp_*` engine symbol allocating between parse and `fromApp` fails the test; Python-heap allocations are not counted. The §6.6 wrapper-pool optimisation (post-v1.0; §10 Q10) is a Python-side latency lever, not a `[const §VIII.5]` correctness blocker.

---

## §9 Test seams

Per `[arch §10]` requirement (4) and `[const §VII.4]`. 2m ships **12 seams** (≥ 10 brief minima; each named per `[2d §9]` / `[2g §9]` / `[2i §9]` cross-referencing precedent).

1. **Latency regression — Python accessor + setter via SWIG.** Google Benchmark on the warm-cache `msg.get_string` / `msg.get_int` / `msg.get_decimal` / `msg.set_int` / `msg.set_string` paths from Python; verify the §6.6 ceilings. CI flags > 5 % regression. Lives in `bench/python/bench_msg_accessors.py` (driven from C++ via `pybind11`-style invocation, but the actual benchmark target is the SWIG-wrapped path). Cross-references `[2i §9]` seam #2.

2. **Python-callback-raises captured + translated.** Python `Application` subclass with a `fromApp` that `raise RuntimeError("boom")`; drive an inbound message; verify (a) the C-ABI return is `FIXPP_ERR_BINDING_PYTHON_CALLBACK_RAISED` (numeric 1200), (b) **the formatted Python traceback appears on `sys.stderr`** (captured via `pytest`'s `capsys` fixture — replaces v0.1's "engine logger emits a fatal-level record" criterion; the engine logger is not callable from `bindings/python/` per §1.3 rule (3) / `[2k §2]` non-goal #7), (c) the engine continues running (no abort, no segfault), (d) subsequent inbound messages dispatch normally, (e) **no `fixpp::core::*` C++ symbol is reachable from the SWIG director path** (verified by linker symbol audit — see seam #9). Lives in `tests/python/test_callback_raises.py`. Verifies §6.3 boundary 2 + §6.7 row 1 + §1.3 rule (3).

3. **Lifetime — Python `Session` outlives the engine; raises `ObjectLifetime`.** Construct an `Engine` and `Session`; `engine.close()`; from Python, attempt `session.send(msg)`; verify the call raises `fixpp.ObjectLifetime` (numeric 1202) with no segfault. Symmetric test: `session.close()` then attempt `msg.get_string(tag)` on a `Message` derived from an inbound dispatch; verify `ObjectLifetime`. **Pickleability check (NEW v0.3 — Opus round-2 N-2-P2-1 close):** verify `pickle.dumps(engine)` / `pickle.dumps(session)` / `pickle.dumps(message)` each raise `TypeError` with the documented "not pickleable; native handles cannot cross process boundaries" message; verify `pickle.dumps(decimal)` / `pickle.dumps(msg_version)` / `pickle.dumps(engine_config)` succeed (the value-typed dataclasses round-trip). Lives in `tests/python/test_lifetime.py`. Verifies §6.2 + §6.7 row 3.

4. **Reentrancy carve-out — `engine.close()` from inside `fromApp` raises `CallbackReentrantClose`; `session.send()` from inside `fromApp` is LEGAL (does not raise).** **[054 / Article XX: this acceptance test verifies the PY-004 director behavior; the v1.0 flat binding has no marker/pre-call check — close-from-callback deadlocks (documentary, L-054-1). This test is deferred to PY-004.]** (Reshaped in v0.2 draft — Codex P2-3 close; tightened in v0.3 — Codex round-2 P2-1 close, the close-from-callback case now surfaces as `CallbackReentrantClose` (1204), distinct from the sub-interpreter rejection at `SubInterpreterRejected` (1201).) Python `Application.fromApp` that calls `engine.close()` directly: verify the call raises `fixpp.CallbackReentrantClose` (numeric 1204) without entering the C ABI. Symmetric test: Python `Application.fromApp` that calls `session.send(reply_msg)` directly: verify the call **succeeds** (the outbound message is enqueued; no exception raised); the v0.1 ban was a Python-only invention that conflicted with `[2i §4.10]` per §1.3 rule (2). Additional sub-interpreter test (NEW v0.3): construct `Engine` from a CPython sub-interpreter (PEP 554) and verify it raises `fixpp.SubInterpreterRejected` (numeric 1201) — distinct catch-clause from the close-from-callback case. Additional test: under `EngineConfig(io_threads=4)`, fire callbacks on rotating pool threads and verify the §1.3 rule (4) GIL-protected session-local marker (`session._in_callback`) detects the close-from-callback case correctly even when the callback resumes on a different OS thread (Opus N-P1-3 close). Lives in `tests/python/test_callback_reentrancy.py`. Verifies §6.5 + §6.7 + §1.3 rule (4).

5. **Wheel-build smoke test — `auditwheel repair` produces a manylinux_2_28 wheel.** The CI builds the wheel via `cibuildwheel`; runs `auditwheel show` on the output and verifies the platform tag is `manylinux_2_28_x86_64`; runs `auditwheel repair` and verifies the result has all OpenSSL / mimalloc deps vendored under `fixpp.libs/` per `auditwheel`'s convention; runs `pip install <wheel>` in a clean venv; runs `python -c "import fixpp; print(fixpp.__version__)"`. Lives in `.github/workflows/wheel-build.yml` + `tests/wheel/test_wheel_smoke.sh`. Verifies §1 PY-005 + §1.1 platform matrix.

6. **Exception-mapping — every `fixpp_error_t` block maps to the correct Python class.** A pytest parametrised over every published `fixpp_error_t` value (table-driven from the C-ABI header at test time); for each value, a stub C++ thunk forces the engine to return that code; verify `fixpp.errors._map_to_class(code)` returns the correct Python class per the §4.6 table. Special cases: `FIXPP_ERR_OK = 0` raises nothing; `FIXPP_ERR_CANCELLED = 1` raises `Cancelled`; `[1200, 1204]` raise the 5 new variants (v0.3: 5, up from v0.2's 4 per Codex round-2 P2-1 split — `1200` → `PythonCallbackRaised`, `1201` → `SubInterpreterRejected`, `1202` → `ObjectLifetime`, `1203` → `WheelAbiMismatch`, `1204` → `CallbackReentrantClose`); `[1205, 1299]` unrecognised values surface as `BindingError` parent class. Lives in `tests/python/test_error_mapping.py`. Verifies §4.6 + §6.7.

7. **ABI-compat test — wheel built against C-ABI v1.0 runs against engine binary at C-ABI v1.1 patched library.** Build a test wheel with `FIXPP_C_ABI_VERSION_MINOR = 0`; build a test engine binary with `FIXPP_C_ABI_VERSION_MINOR = 1` (and a synthetic new `fixpp_error_t` variant at code 199 in the wire block); have the engine return code 199 from a thunk; verify the wheel surfaces `fixpp.Unknown` (NOT a crash, NOT a missing-class error, NOT a wrong-class error). Cross-references `[2i §9]` seam #6 (forward-compat). Lives in `tests/python/test_abi_compat.py`. Verifies §3.16 + §3.19 forward-compat downgrade through Python.

8. **`pytest` smoke test — `import fixpp` works; basic engine + session works.** A minimal pytest that imports `fixpp`, constructs an `Engine` with default config + a memory `MessageStore` + a mock TCP transport (engine-side mock loaded via a test-only env var), opens a `Session`, sends a NewOrderSingle, receives an ExecutionReport via a mock counterparty, closes. Verifies the end-to-end Python → C-ABI → engine → C-ABI → Python loop. Lives in `tests/python/test_smoke.py`. Per `[const §VII.2]`: pytest is mandatory.

9. **No-engine-include test — SWIG wrapper does NOT include `<fixpp/...>` headers; `tools/check_layers.py` extension to `bindings/python/`.** (Escalated v0.1 P2 → v0.2 P1 — Codex P2-4 close, mirroring 2j precedent.) A CI script that runs `nm` or `objdump` over the built `_fixpp.so` and verifies the symbol table contains only `fixpp_*` C-ABI symbols + Python interpreter symbols; no `_ZN5fixpp7session*` or other C++-mangled engine-internal names. Plus the **normative `tools/check_layers.py` extension commitment** (per §3.4 v0.2 / mirroring `[2j §1.2]` goal 6 / `library/.specify/2j-controlplane.md:22`): the lint scans `bindings/python/*.i`, `bindings/python/*.cxx`, `bindings/python/*.py` for `#include <fixpp/X/...>` (where `X != detail-of-c_api`) and `%include "fixpp/..."` and fails the build with the categorised message "**AGPL boundary violation: bindings/python file X includes Y**". Includes a negative-case fixture (`tests/python/fixtures/violation_includes_wire.cxx` that intentionally contains `#include <fixpp/wire/view.h>`) which the lint MUST flag. Lives in `tests/ci/test_swig_no_engine_include.sh` + `tools/check_layers.py`. Verifies §1.3 / §3.4 / `[arch §8]` boundary structurally with the same AGPL-boundary stake as 2j (`[const §V.1]`).

10. **`auditwheel show` audit for vendored shared libs.** After `cibuildwheel` produces the wheel, run `auditwheel show <wheel>` and assert: (a) the wheel's `manylinux_2_28_x86_64` platform tag is set; (b) the dependency list under `fixpp.libs/` includes (at minimum) `libssl.so.*` (OpenSSL — per `[const §XII.1]`), `libmimalloc.so.*` (per `[arch §5.2]`); (c) NO system libc / libstdc++ vendored (those come from the manylinux_2_28 platform itself). Lives in `tests/wheel/test_auditwheel.sh`. Verifies §1 PY-005 + the mandatory wheel platform-tag rule.

11. **Decimal PoD round-trip preserves precision through Python.** Python parametrised test over a list of `fixpp.Decimal` edge cases (`Decimal(mantissa=INT64_MIN, exponent=0)`, `Decimal(0, 0)`, max / min values per `[2a §9]` seam #5); for each, `msg.set_decimal(tag, d)`; serialise the message; parse it back; `d2 = msg.get_decimal(tag)`; assert `d == d2` byte-for-byte. Cross-references `[2i §9]` seam #10. Lives in `tests/python/test_decimal_roundtrip.py`. Verifies §3.14 + §7.1.

12. **GIL re-entry under multi-threaded callbacks — concurrent `fromApp` on different sessions doesn't deadlock or corrupt state.** Construct an `Engine` with `io_threads=4`; open 4 `Session`s; register 4 `Application` objects; from a mock counterparty, fire concurrent inbound messages on all 4 sessions simultaneously; verify each `fromApp` is invoked on its own session strand thread; verify the GIL serialises the Python execution (no two `fromApp` runs simultaneously in Python); verify no deadlock; verify the per-session Python `Application` state is consistent (each callback sees its own `Application` instance). Lives in `tests/python/test_gil_concurrency.py`. Verifies §6.1 + §6.5 multi-threaded section.

13. **Outbound director latency — `toApp` dispatch overhead at high message rate.** (NEW in v0.2 — Opus N-P1-4 close.) Construct an `Engine` with `io_threads=1`; open one `Session`; register an `Application` whose `toApp` is overridden as an empty Python method (`def toApp(self, session, msg): pass`); `session.send` 1M outbound messages back-to-back; measure the per-message wall-clock dispatch overhead (the time spent in the SWIG director path between the engine's "about to call `toApp`" and "back from `toApp`"); assert p99 ≤ 5 µs per the §6.6 latency table outbound row. Symmetric negative test: `Application` with `toApp` left unimplemented (the C++ no-op base is used; no SWIG director cost is paid); assert the per-message overhead drops to engine-only ≤ 100 ns p99. Lives in `bench/python/bench_toapp_overhead.py`. Verifies §6.6 outbound director latency budget + the §4.5 `toApp` docstring escape hatch.

(13 seams. Brief minima 10. The 3 extras are #5 (`auditwheel repair` wheel-build smoke), #10 (`auditwheel show` vendored-libs audit) — both required for the v1.0 mandatory wheel deliverable per §1 PY-005 — and #13 (NEW v0.2; outbound director latency). Seam #1's allocation-guard acceptance criterion is updated to scope `mallocnesia` to `fixpp::*` / `fixpp_*` symbols only per RC#1 / §3.9 v0.2.)

---

## §10 Open questions

| # | Question | Disposition | Owner |
|---|---|---|---|
| Q1 | **Asyncio adapter shape (`fixpp.aio.AsyncSession`).** v1.0 is sync reacquire-and-call per §6.4; the asyncio adapter uses producer-consumer queue handoff (block mode per `[const §XV.15]`). Need to spec: (a) what's the Python-side queue type (`asyncio.Queue` requires the engine to know about the user's event loop — coupling); (b) does the engine drive the queue from the strand thread (cross-thread queue write), or does the user supply a thread-safe `Queue` and a drain coroutine; (c) cancellation propagation (asyncio `CancelledError` → `fixpp_session_close`). | DEFERRED to v1.x; tracked in `feature-catalogue.md` as a new PY-006 row at v1.x sign-off. | 2m or a dedicated 2m-async doc |
| Q2 | **Wheel signing.** Signing is **not currently mandated by the constitution** — `[const §IV.5]` covers publication gating only ("v1.0 release artifacts are built but not published. Conan packages and Python wheels are attached to GitHub releases; no upload to Conan Center or PyPI in v1") with no signing requirement. v1.0 therefore ships unsigned wheels attached to GitHub releases. If/when PyPI publication unblocks under `[const §IV.5]`'s gate, a constitution amendment per Article XX of `[const]` introduces the signing scheme (Sigstore is the likely candidate per PyPI's recommendation). (Codex P1-5 close, v0.2.) | **No constitutional backing yet** — DEFERRED to PyPI-publication unblock; constitution amendment per Article XX of `[const]` when adopted. | constitution amendment |
| Q3 | **Per-FIX-version Python typed messages (`import fixpp.v44.NewOrderSingle`).** v1.0 has runtime-resolved `Message`; per-version codegen would generate Python class stubs for every typed message in `[2c §3]` namespaces (`fixpp::v42::*`, `fixpp::v44::*`, etc.). Requires: a Python codegen target in the build (mirroring the C++ codegen per `[2c §4.x]`), per-class type stubs (`*.pyi`), and an extension to `fixpp.Message.__class__` resolution. | DEFERRED to v1.x; significant codegen effort; not a v1.0 user requirement. | new design doc 2m-codegen-py at v1.x |
| Q4 | **Cross-process iceoryx2 Python tap subscriber.** A Python user who wants tap subscription needs the `iceoryx2` Python bindings (independent project). v1.0 documents the pattern but does not ship a `fixpp.Tap` wrapper. v1.x may ship a thin `fixpp.tap.Iox2Subscriber` over the iceoryx2 Python API. | DEFERRED to v1.x; depends on `iceoryx2` Python bindings maturity. | 2m or 2l v1.x |
| Q5 | **Zero-copy Python view (`memoryview`-over-the-buffer).** v1.0 `msg.get_string(tag)` returns a copy. A `memoryview` accessor would expose the wire buffer directly via Python's buffer protocol; lifetime tied via `__buffer__` / `release_buffer`. | DEFERRED to v1.x; needs careful lifetime design and is not blocked-on for the conformance test corpus. | 2m v1.x |
| Q6 | **Custom `MessageStore` from Python.** Per §7.5: banned in v1.0 because PMR has no Python equivalent. A v1.x design could adapt a Python class implementing a 5-method protocol to a C++ `MessageStore` shim, paying allocation costs at the boundary. | DEFERRED to v1.x. | 2m + 2e v1.x |
| Q7 | **Custom `cert_source` from Python.** Per §7.7: TLS surface is C++-only in v1.0 per `[2g §7.6]`. A v1.x design could expose a `fixpp.CertSource` Python protocol. | DEFERRED to v1.x; depends on a 2g amendment exposing the C-ABI cert_source surface. | 2m + 2g v1.x |
| Q8 | **PEP 703 (nogil) build.** When PEP 703 stabilises and CPython ships a default nogil build, the GIL discipline in §6.1 needs revisiting (the GIL release/acquire macros become no-ops; the strand-thread Python execution may run concurrently). | DEFERRED to PEP 703 stable adoption; likely v1.x or v2. | 2m amendment |
| Q9 | **aarch64 Linux mandatory wheel.** Currently best-effort. Becomes mandatory when production deployments require it; needs constitution amendment. | DEFERRED; amendment per `[const §XX]`. | constitution amendment |
| Q10 | **Pre-allocated `Message` wrapper pool — does it actually help?** §6.6 mentions a pool optimisation deferred to v1.x. Decision pending v1.0 perf data: if `msg.get_string(tag)` warm-cache p99 turns out to be > 1 µs, the pool is the obvious next lever. | DEFERRED until v1.0 perf data; no spec-doc work needed if v1.0 ceilings are met without it. | 2m v1.x |

---

## §11 Hand-off

The Phase 4 specs / modules that 2m unblocks (or that 2m hands off to):

1. **Phase 4 module `bindings/python/`** — implementation of the SWIG `.i` files, the `cibuildwheel` config, the `pyproject.toml`, the `setup.py` glue, the `tests/python/` suite. 2m provides the design contract; Phase 4 implements.
2. **The `pip install fixpp` user story** — documentation under `docs/python/` (getting-started, callback cookbook, lifetime cookbook, troubleshooting). Driven by `[const §XIX.1]`.
3. **Phase 4 wheel-build CI workflow** (`.github/workflows/wheel-build.yml`) — the cibuildwheel + auditwheel + smoke test pipeline. Driven by §9 seam #5 + #10.
4. **Examples under `examples/python/`** — minimum: a `quickstart.py` (open session, send NewOrderSingle, receive ExecutionReport, close); a `cross_strand_handoff.py` (the §3.22 / §6.4 mitigation pattern); a `multi_session.py` (the §6.1 multi-threaded callback pattern). Per `[const §XIX.3]` examples are CI-exercised.
5. **2i Appendix D drop-ins (this doc § Appendix D §D.1 / §D.2 / §D.3 / §D.5)** — the 5 new `fixpp_error_t` variants in the `[1200, 1299]` block (v0.3: 5 variants, up from v0.2's 4 per Codex round-2 P2-1 split). Applied at 2m sign-off; amends 2i v0.3 (or live revision) → next minor. Plus § Appendix D §D.4 — the cross-doc amendment to `[2j §11]` queueing the `fixpp_session_post` C-ABI symbol declaration (NEW v0.3 — Opus round-2 N-2-P1-1 close); applied at 2j's next minor revision after 2m sign-off.
6. **`feature-catalogue.md` amendments** — at sign-off, append a `Coverage column` reference to `[2m §X.Y]` for each of PY-001..PY-005. (No new rows in v1.0; v1.x may add PY-006 for the asyncio adapter per §10 Q1.)
7. **Hand-off TO Phase-4 session-module spec.** 2m is a *consumer* of CA-005 / CA-006 / CA-007 (engine/session lifecycle, send, register-callback). The Phase-4 session-module spec OWNs the C-ABI signatures and FSM behaviour; 2m wraps the published shape. The v1.0 strand-post primitive `fixpp_session_post(session, closure, userdata)` (per `[2i §6.3]` line 1365) is **declared by 2j** at 2j sign-off via Appendix D §D.4 (the 2m-driven amendment to `[2j §11]`'s hand-off table) — NOT a future Phase-4 addition. §4.4 outbound `Message.__init__` consumes the symbol; the declaration is mandatory for v1.0. (v0.3 — closes Opus round-2 N-2-P1-1.) If Phase-4 introduces *additional* new C-ABI symbols beyond `fixpp_session_post`, 2m extends the `Session` Python class accordingly via a separate amendment at that time.

---

## Appendix A — Catalogue row coverage

| Row | Description | 2m sections covering |
|---|---|---|
| **PY-001** | SWIG interface wrapping C ABI — `import fixpp`, Session, Message classes (all FIX versions). | §1 goal 1; §3.1 (verbatim quote); §4.1 (package layout); §4.2–§4.5 (Engine, Session, Message, Application); §5 (wrapped C-ABI symbols); §7 (per-doc integration); §9 seam #8 (smoke test). |
| **PY-002** | GIL correctness — release GIL during blocking I/O; reacquire in callbacks. | §1 goal 2; §3.1 (verbatim quote); §6.1 (full GIL discipline); §6.5 (carve-outs); §9 seam #1 (latency under GIL); §9 seam #4 (deadlock detection); §9 seam #12 (multi-threaded callbacks). |
| **PY-003** | Exception translation — C error codes → Python exceptions. | §1 goal 3; §3.5 / §3.11 (verbatim quotes); §4.6 (full exception hierarchy); §6.3 (translation boundaries); §6.7 (new variants); §9 seam #2 (callback-raises); §9 seam #6 (mapping coverage); §9 seam #7 (ABI compat). |
| **PY-004** | Ownership / lifetime — Python objects don't outlive native sessions. | §1 goal 4; §3.6 (verbatim quote); §6.2 (full lifetime / ownership); §6.7 row 3 (`ObjectLifetime`); §9 seam #3 (Python `Session` outlives engine). |
| **PY-005** | pip-installable wheel (Linux x86_64 minimum) via CI. | §1 goal 5; §1.1 (platform matrix); §3.2 / §3.7 (verbatim quotes); §9 seam #5 (cibuildwheel + auditwheel repair); §9 seam #10 (auditwheel show vendored-libs audit); §11 hand-off item 3 (CI workflow). |

---

## Appendix B — Normative References

> Format follows `[const §VI.2]`: `[DocAbbrev §X.Y.Z] Section title`. Line ranges given for every cross-doc citation.

### B.1 Constitution (`library/.specify/constitution.md`)

| Citation | Lines | Architectural relevance to 2m |
|---|---|---|
| `[const §I.1]` | 11–17 | v0.2 codegen-vs-runtime-XML split; Python `Message` is runtime-resolved per §7.3. |
| `[const §IV.3]` | 58 | Linux x86_64 wheel mandatory in CI per §1.1 / PY-005. |
| `[const §IV.5]` | 60 | v1.0 release artifacts built but not published; covers PUBLICATION GATING only (no signing requirement) per §10 Q2 / non-goal #12. |
| `[const §XII.1]` | 156 | OpenSSL on TLS for both Linux and Windows; relevant to §1 goal 5 (auditwheel-vendored OpenSSL) / §9 seam #10. |
| `[const §V.1]` | 66 | AGPL boundary — Python bindings reach the engine through C ABI only per §1 goal 8 / §3.8. |
| `[const §VII.2]` | 88 | Python tests: pytest against the SWIG bindings per §9 seam #8. |
| `[const §VIII.5]` | 106 | Zero allocation between parse and `fromApp`; SWIG callback adapter discipline per §6.1 / §3.9. |
| `[const §X.1]` | 131 | C ABI SemVer track independent from library SemVer per §3.10 / §4.1. |
| `[const §X.3]` | 134–135 | Decimal PoD at C-ABI; `fixpp.Decimal` mirror per §7.1. |
| `[const §X.4]` | 135–136 | `FIXPP_ERR_UNKNOWN` translation rule per §6.3 / §3.11 / §3.19. |
| `[const §X.5]` | 137 | Reentrancy / thread-safety contract on C-ABI handles; pin which Python ops can run per §6.5 / §3.12. |
| `[const §XI.2]` | 145 | ASIO native cancellation slots; `fixpp.Cancelled` translation per §6.3 / §3.19. |
| `[const §XIV.2]` | 197 | ≤5 pure-virtual cap; the 6-method `Application` justification inherited per §1 goal 8 / §1.2. |
| `[const §XV.15]` | 221 | Banned drop-oldest on app/session message paths; async-callback queue MUST be block mode per §3.13 / §6.4. |
| `[const §XVII.1]` | 244–253 | Codex Gate A is mandatory for this design doc. |
| `[const §XVIII.1]` | 269 | v1.0 scope locked — Python bindings are part of v1.0 per §1 goal 1. |

### B.2 Architecture (`library/.specify/architecture.md`)

| Citation | Lines | Architectural relevance |
|---|---|---|
| `[arch §1.2]` | 37–44 | v1.0 non-goals; structural bounds on what Python wraps. |
| `[arch §2.3]` | 109–127 | Allowed-edges table — `bindings/python` may include from `capi` only per §3.4 / §6.5 enforcement. |
| `[arch §4.10]` | 318–331 | C-ABI surface inventory — opaque-handle catalogue per §3.14. |
| `[arch §4.12]` | 351–362 | The `bindings/python` spine — verbatim quoted at §3.1; this is the rule 2m operationalises. |
| `[arch §5.2]` | 376–381 | Allocator policy — Python heap vs engine arenas per §8. |
| `[arch §5.3]` | 383–389 | Error model translation boundary — verbatim quoted at §3.5; the second hop. |
| `[arch §5.5]` | 398–403 | Lifetime model — flyweights vs owned types per §3.6 / §6.2. |
| `[arch §5.6]` | 405–409 | Frozen-at-open `SessionConfig` per §4.7. |
| `[arch §6]` | 425–446 | Plugin pattern — the `Application` 6-method justification inherited per §1.2 / §4.5. |
| `[arch §7.1]` | 452–462 | Build outputs — wheel name, mandatory platform per §3.2. |
| `[arch §7.4]` | 491–501 | CMake target layout — `fixpp-python` depends only on `fixpp::capi` per §3.3. |
| `[arch §8]` | 509–521 | Service-mode boundary — applies equally to bindings/python per §3.4. |
| `[arch §9.1]` | 539–544 | Header discipline — no transitive C++ leaks per §3.4 / §9 seam #9. |
| `[arch §10] row 2m` | 578 | Hand-off table row — Owns: ownership transfer, GIL handling, exception translation, async callbacks. |

### B.3 Sibling design docs

| Citation | Document | Lines | Relevance |
|---|---|---|---|
| `[2a §5.1]` | 2a-decimal.md | 233–240 | `fixpp_decimal_t` PoD shape; `fixpp.Decimal` mirror per §7.1. |
| `[2a §5.2]` | 2a-decimal.md | 252–274 | Boundary functions; SWIG wraps each per §5. |
| `[2b §6.4]` | 2b-wire.md | 610–615 | Flyweight lifetime contract; Python `Message` inherits per §3.17 / §6.2. |
| `[2c §5]` | 2c-codegen.md | 1554–1572 | Dictionary C-ABI commitments 1–6; runtime-resolved version per §7.3. |
| `[2d §4.4]` | 2d-threading.md | 412–495 | `EngineConfig` shape; Python dataclass mirror per §4.7. |
| `[2d §4.5]` | 2d-threading.md | 496–656 | `SessionConfig` shape; Python dataclass mirror per §4.7. |
| `[2d §6.7]` | 2d-threading.md | 1169–1181 | Threading errors coalesced per `[2i §3.11]`; Python `SessionError` per §4.6. |
| `[2d §7.6]` | 2d-threading.md | 1241–1244 | Transport strand-dispatch (callbacks run on session strand per `[2d §1]` goal 1 / §4.8 wrapper); SWIG director on the strand thread per §6.1. |
| `[2e §4.4]` | 2e-msgstore.md | 678–738 | `MessageStoreFactory` shape; banned from Python in v1.0 per §7.5. |
| `[2g §7.6]` | 2g-tls.md | 1052–1054 | TLS C-ABI deferred; banned from Python in v1.0 per §7.7. |
| `[2h §7.8]` | 2h-transport.md | 1304–1306 | Transport handle shapes deferred per `[2i §3.17]`; banned from Python in v1.0 per §7.8. |
| `[2i §1.1]` | 2i-capi.md | 30–73 | `fixpp_error_t` numeric blocks; 2m claims `[1200, 1299]` per §3.15 / §6.7. |
| `[2i §1.2]` | 2i-capi.md | 75–97 | 2i scope boundary; 2m is downstream consumer per §1.2. |
| `[2i §3.5]` | 2i-capi.md | 159–166 | `[const §X]` full ABI policy quoted in 2i; 2m inherits. |
| `[2i §4.2]` | 2i-capi.md | 345–399 | Opaque handle types; Python wrapper-class mapping per §3.14 / §4.2–§4.4. |
| `[2i §4.3]` | 2i-capi.md | 472–599 | The master `fixpp_error_t` enum; Python `FixppError` mapping per §4.6. |
| `[2i §4.4]` | 2i-capi.md | 601–648 | `fixpp_strerror` + forward-compat downgrade; Python `Unknown` per §3.11. |
| `[2i §4.5]` | 2i-capi.md | 650–712 | Versioning macros + engine-binding version protocol per §3.16 / §4.7. |
| `[2i §4.6]` | 2i-capi.md | 714–869 | Field accessors; Python `Message.get_*` mapping per §4.4. |
| `[2i §4.7]` | 2i-capi.md | 878–1006 | Field setters + `fixpp_msg_clone`; Python `Message.set_*` / `clone()` per §4.4 / §3.18. |
| `[2i §4.9]` | 2i-capi.md | 1143–1158 | Cancellation translation; Python `Cancelled` per §6.3 / §3.19. |
| `[2i §4.10]` | 2i-capi.md | 1159–1202 | Reentrancy taxonomy; Python carve-outs per §6.5 / §3.20. |
| `[2i §5.2]` | 2i-capi.md | 1213–1314 | Construction-vs-steady-state thunk split; Python director adapter is steady-state per §3.21. |
| `[2i §6.3]` | 2i-capi.md | 1361–1368 | Cross-strand handoff via `fixpp_msg_clone`; Python idiom per §3.22. |
| `[2i §6.5]` | 2i-capi.md | 1389–1407 | 2i-introduced variants; 2m's drop-in extends this table per Appendix D. |
| `[2i §7.12]` | 2i-capi.md | 1525–1531 | Hand-off to 2m; this doc is the response per §3.23. |
| `[2i §10 Q5]` | 2i-capi.md | 1604 | DECIDED v0.2 — inbound messages immutable; `fixpp_msg_set_*` on inbound flyweight returns `FIXPP_ERR_INVALID_HANDLE`. Drives §1.3 rule (1) / §4.5 / §6.5 row 2. |
| `[2j §1.2]` | 2j-controlplane.md | 22 | `tools/check_layers.py` extension precedent for `service/`; mirrored to `bindings/python/` in §3.4 / §9 seam #9 v0.2. |
| `[2j §4.7]` | 2j-controlplane.md | 672–757 | `OpenSessionRequest` proto shape; relevant to §4.7 `SessionConfig`. |
| `[2j §11]` | 2j-controlplane.md | 1114–1133 | 2j hand-off table; v0.3 Appendix D §D.4 amendment declares `fixpp_session_post` here per Opus round-2 N-2-P1-1. |
| `[2k §1.1]` | 2k-log-otel.md | 28–46 | Scope boundary — log C-ABI is C++-only in v1.0; Python `LogConfig` constraint per §7.11. |
| `[2k §2]` | 2k-log-otel.md | 84 | Non-goal #7: no log forwarding to the C ABI in v1.0; drives §1.3 rule (3) / §6.3 boundary 2. |
| `[2l §1.1]` | 2l-tap.md | 30–55 | Scope boundary — tap C-ABI deferred; no Python tap surface per §7.12. |

### B.4 SYNTHESIS

| Citation | Source | Relevance |
|---|---|---|
| `[SYN §3.5 #18]` | research/SYNTHESIS.md §3.5 #18 | Async callback handoff (queue vs reacquire); resolved at §6.4. |

---

## Appendix C — Convergence log

### v0.1 → v0.2 (Phase A round 1 convergence pass)

**Reviews addressed:** Codex Gate A (5 P1 / 6 P2 / 4 P3) and Opus adversarial review (combined post-judging 9 P1 / 7 P2 / 5 P3; 3 root causes). Closing recommendation followed: "v0.2 can ship after a single convergence pass" — line-edit + root-cause shape, no full rewrite.

#### Root causes (RC#1–RC#3) — resolved at v0.2

| RC | Title | Resolution |
|---|---|---|
| **RC#1** | Allocation contract conflated: `[const §VIII.5]` engine-arena discipline vs Python-heap allocations on the strand-yielded-to-user-code window | NEW normative paragraph at §3.9: SWIG director runs *on* the engine strand thread but *outside* the engine's PMR/arena pool — it allocates on CPython's `PyObject_Malloc` heap. `[const §VIII.5]` scopes to engine-side `fixpp::*` / `fixpp_*` symbols (verified by `mallocnesia`'s symbol-scope filter — §9 seam #1 acceptance criterion update). Python-heap allocations are user-code allocations in the architecturally permitted "engine yields the strand to user code" window per `[arch §6]`. The §6.6 wrapper-pool optimisation is a Python-side latency lever, NOT a `[const §VIII.5]` correctness blocker. Canonicalised across §1.1, §3.9, §6.1, §6.6, §8. |
| **RC#2** | SWIG director boundary described twice with incompatible mechanics: half "C-ABI only" (correct) and half "engine-internal C++ access" (incorrect) | NEW normative §1.3 "SWIG director consumption rule" (4 sub-rules: inbound immutability deferred to `[2i §10 Q5]` not Python-side checks; reentrancy legality from `[2i §4.10]` not Python-invented bans; director-side logging via `sys.stderr` / `PyErr_PrintEx` only; strand-execution via GIL-protected session-local markers, not OS thread-id). The single highest-leverage fix in v0.2 — collapses 5 findings (Codex P1-3, P1-4, P2-3, Opus N-P1-1, N-P1-2) into one structural commitment that becomes the structural backstop for §4.5 / §6.3 / §6.5 / §6.7 / §9 seam updates. |
| **RC#3** | Citation discipline (mechanical) | One sweep over §1.1 (drop `[2g §1.2]` for OpenSSL bundling; replace with `[const §XII.1]`), §6.4 (`[arch §4.4]` → `[arch §6]` for the user-mental-model rationale), §10 Q2 (drop the spurious `[const §IV.5]` "unsigned wheels per" cite; reframe as "no constitutional backing yet"). Appendix B amended with new entries (`[const §IV.5]`, `[const §XII.1]`, `[2i §10 Q5]`, `[2j §1.2]`, `[2k §2]`). 4 cite-hygiene corrections + 5 new Appendix B entries; mechanical, no behaviour change. |

#### Per-finding resolution table

| Finding | Source | Opus verdict | Resolution at v0.2 | Section(s) touched |
|---|---|---|---|---|
| **P1-1** Hot-path allocation policy contradicted | Codex | Confirm @ P1; **clustered into RC#1** | RC#1 normative paragraph at §3.9 + canonicalisation across §1.1 / §6.1 / §6.6 / §8. Codex's recommended fix (mandate wrapper pool in v1.0) was REJECTED by Opus; the defensible v1.0 contract is "Python-heap allocation is OK on the strand-yielded-to-user-code window; engine PMR allocation is not." Pool deferred to v1.x as a latency lever (§10 Q10). | §1.1, §3.9, §6.1, §6.6, §8 |
| **P1-2** Outbound `Message(msg_type, session)` lacks strand-safe story | Codex | Confirm @ P1; standalone | §4.4 v0.2 outbound docstring: `Message.__init__` internally posts `fixpp_msg_create_outbound` onto the session strand via `fixpp_session_post(session, closure, userdata)` per `[2i §6.3]` line 1365 and blocks on a future with the GIL released. Synchronous Python idiom preserved; C-ABI strand-only contract honoured. | §4.4 |
| **P1-3** Inbound `Message` usable after `fromApp` return (UB in release) | Codex | Confirm @ P1; **clustered into RC#2** | §4.4 / §4.5 / §6.4 v0.2: SWIG director arms `_dead = True` on the wrapper BEFORE GIL release on `fromApp` return. Post-return accessor calls raise `fixpp.ObjectLifetime` (1202) — NOT `InvalidHandle` (resolves P2-2 inconsistency). The `_dead` flag is set in step 3.f of the §6.4 dispatch sequence; no UB-in-release path remains. The Session-close walk (the `weakref.WeakSet[Message]` traversal in §6.2) is preserved as a SEPARATE mechanism for engine-close-time invalidation. | §4.4, §4.5, §6.2, §6.4 |
| **P1-4** Callback exception logging contradicts C-ABI-only boundary | Codex | Confirm @ P1; **clustered into RC#2** | §6.3 v0.2: deleted the `fixpp_python_callback_log_fatal(buf)` call (a fabricated symbol) and the `StringIO` formatting block. Replaced with `PyErr_Restore` + `PyErr_PrintEx(0)` + `PyErr_Clear` — Python's standard `sys.stderr` traceback path. The engine sees only the `1200` return code; no traceback string crosses `extern "C"`. §1.3 rule (3) is the structural backstop. §6.7 row 1 remediation column updated. | §1.3, §6.3, §6.7 |
| **P1-5** Open question Q2 cites a non-existent constitutional rule | Codex | Confirm @ P1; **clustered into RC#3** | §2 non-goal #12 + §10 Q2 v0.2: the `[const §IV.5]` cite is corrected — it covers publication gating, not signing. The new framing is "no constitutional backing yet"; if PyPI publication unblocks, a constitution amendment per Article XX of `[const]` introduces the signing scheme. Honest reframing replaces the fabricated cite. | §2 non-goal #12, §10 Q2 |
| **P2-1** GIL discipline omits required support matrix constraints | Codex | Confirm @ P2; standalone | §6.1 v0.2: NEW "Supported interpreter model (v1.0)" subsection (single main interpreter only; ban on sub-interpreters per PEP 554 with `Engine.__init__` rejection via `PyInterpreterState_Get`; shutdown ordering — close before `Py_Finalize`; `PyGILState_Ensure`/`Release` balance on every exit path including raised). §9 seam #4 covers orderly shutdown. | §6.1 |
| **P2-2** Lifetime invalidation exception type inconsistent (`InvalidHandle` vs `ObjectLifetime`) | Codex | Confirm @ P2; standalone | §4.4 v0.2 + §4.6 v0.2 + §6.7 row 3: `InvalidHandle` is dropped from the binding-side surface. Binding-side invalidation is `fixpp.ObjectLifetime` (1202) uniformly. The C-ABI `FIXPP_ERR_INVALID_HANDLE` (numeric 4) surfaces via the §4.6 mapping as `fixpp.CapiError` — distinct path; documented in `CapiError` docstring. | §4.4, §4.6 |
| **P2-3** Binding-level ban on `Session.send()` inside `fromApp` diverges from C-ABI reentrancy contract | Codex | Confirm @ P2; **clustered into RC#2** | §6.5 row 4 v0.2: ban removed. `Session.send` from inside `fromApp` is LEGAL in v0.2 per `[2i §4.10]` / §1.3 rule (2). The §6.5 v0.1 rationale (recursive callback starvation) was unsound. `GilDeadlock` (1201) is repurposed for the GENUINE deadlock case kept at row 5: `engine.close()` / `session.close()` from inside a callback. §9 seam #4 reshape: positive test (send-from-callback succeeds) + negative test (close-from-callback raises). | §1.3, §6.5, §6.7 GilDeadlock docstring, §9 seam #4 |
| **P2-4** Layering enforcement weaker than 2j pattern | Codex | **Escalate P2 → P1**; standalone | §3.4 v0.2 + §9 seam #9 v0.2: NORMATIVE commitment to `tools/check_layers.py` extension over `bindings/python/*.i` / `*.cxx` / `*.py` for `#include <fixpp/...>` / `%include "fixpp/..."` (excluding `<fix/c_api/...>` which IS allowed). Failure messages categorised "AGPL boundary violation: bindings/python file X includes Y" — identical phrasing to 2j precedent at `library/.specify/2j-controlplane.md:22`. Negative-case fixture file in §9 seam #9 acceptance criteria. The retroactive "extended in v0.1 of this doc" v0.1 prose is replaced with a concrete sign-off commitment. | §3.4, §9 seam #9 |
| **P2-5** "Callback-raises" snippet allocates in director path | Codex | Confirm @ P2; **clustered into RC#1** | §6.3 v0.2: `StringIO` formatting deleted. Replaced with `PyErr_PrintEx(0)` (CPython's standard `sys.stderr` formatting; allocates on Python heap on the §1.3-rule-(3)-sanctioned path; no engine-arena allocation; outside `mallocnesia`'s `fixpp::*` / `fixpp_*` symbol-scope per RC#1). Engine logs "1200 occurred" with no traceback string. | §6.3, §6.6 allocation table |
| **P2-6** `fixpp_error_t` block mapping per-block vs per-code stability ambiguity | Codex | **Demote P2 → P3**; editorial | §3.11 v0.2 + §4.6 stability-rule paragraph v0.2: clarified to "every PUBLISHED `fixpp_error_t` value maps to a stable Python subclass; UNRECOGNISED values surface as the block parent class until the wheel is upgraded — matches `[2i §4.4]` forward-compat rule." No behaviour change. | §3.11, §4.6 |
| **P3-1** `[1200, 1299]` already reserved for 2m in 2i §1.1 | Codex | Confirm @ P3 (refutation accepted); **clustered into RC#3** | §3.15 v0.2: prose tightened — "the `[1200, 1299]` block was reserved for 2m at 2i v0.3 sign-off; 2m v0.2 *populates* the pre-reserved block." Appendix D re-framed as "populate" amendments (not "create the reservation"). | §3.15, Appendix D headers |
| **P3-2** Missing 32-bit integer/truncation note for SWIG boundary types | Codex | Confirm @ P3; editorial | §7.1 v0.2: NEW paragraph documenting `int64_t mantissa` boundary and SWIG's default `OverflowError` behaviour on out-of-range Python `int`; v1.0 ships Linux x86_64 only so 32-bit truncation is moot for the mandatory wheel; note for v1.x 32-bit considerations. | §7.1 |
| **P3-3** §6.4 matches `[arch §4.12]`'s "reacquire-and-call" rule | Codex | Confirm @ P3 (refutation accepted); standalone | No change required; the §6.4 decision is architecturally compliant. | (none) |
| **P3-4** Wheel pipeline details explicitly named | Codex | Confirm @ P3 (refutation accepted); standalone | No change required; §1.1 toolchain coverage retained. | (none) |
| **N-P1-1** §6.5 row 2 contradicts `[2i §10 Q5]` DECIDED v0.2 (inbound messages immutable) | Opus (NEW) | P1; **clustered into RC#2** | §1.3 rule (1) + §6.5 row 2 v0.2: prose updated to "`msg.set_string(tag, value)` on inbound: NO — `fixpp_msg_set_*` returns `FIXPP_ERR_INVALID_HANDLE` per `[2i §10 Q5]` DECIDED v0.2 / §1.3 rule (1); Python wrapper translates to `fixpp.CapiError(code=4)`. Use `msg.clone()` first." §4.5 docstrings for `fromAdmin` / `fromApp` updated to explicitly say "msg is INBOUND (immutable per `[2i §10 Q5]`)." Director sets `_inbound = True` on inbound wrappers; `set_*` accessors raise before C-ABI round-trip (latency optimisation). | §1.3, §4.5, §6.5 |
| **N-P1-2** `fixpp_python_callback_log_fatal()` is a fabricated symbol (AGPL boundary violation) | Opus (NEW) | P1; **clustered into RC#2** | §6.3 v0.2: deleted the `fixpp_python_callback_log_fatal(buf)` call (a fabricated symbol; greps over 2i / 2j / 2k / arch return no match) and the `StringIO` formatting block. Replaced with `PyErr_PrintEx(0)` per RC#2 / §1.3 rule (3) / `[2k §2]` non-goal #7. §9 seam #2 acceptance criterion (b) updated to "the formatted Python traceback appears on `sys.stderr` (captured via pytest's `capsys` fixture)" — replacing the engine-logger criterion. | §1.3, §6.3, §6.7 row 1, §9 seam #2 |
| **N-P1-3** Strand-thread-id detection in §6.5 enforcement is unsound under `[2d §4.5]` threading taxonomy | Opus (NEW) | P1; standalone | §1.3 rule (4) + §6.5 v0.2: enforcement replaced with **GIL-protected session-local marker** — `session._in_callback = True` set on the *Python `Session` instance* (not on `threading.local()` and not keyed by `threading.get_ident()`). Works correctly under all three `[2d §4.5]` threading modes (`per_session_strand`, `engine_thread_pool_strand`, `direct_executor`). `_strand_thread_id` field dropped from the §4.3 `Session` class layout. §9 seam #4 reshape: under `EngineConfig(io_threads=4)`, fire callbacks on rotating pool threads and verify detection works. | §1.3, §4.3 (implicit), §6.5, §9 seam #4 |
| **N-P1-4** `Application` 6-method director adds outbound-callback latency budget §6.6 does not bound | Opus (NEW) | P1; standalone | §6.6 latency table v0.2: NEW rows for `toAdmin` / `toApp` / `fromAdmin` / `onLogon` / `onLogout` callback dispatch overhead. `toApp` / `toAdmin` budgeted at ≤ 5 µs p99 dispatch overhead (matching `fromApp`). Documented escape hatch in §4.5 / §6.6: users who don't need outbound mutation leave `toApp` unimplemented (the C++ no-op base is used; no SWIG director cost is paid). NEW §9 seam #13 — outbound director latency bench across 1M outbound messages. | §4.5 toApp docstring, §6.6, §9 seam #13 |
| **N-P2-1** Cancellation taxonomy: `Cancelled` mapping omits enumeration of pre-translation sources | Opus (NEW) | P2; standalone | §4.6 `Cancelled` docstring v0.2: enumerates the ten cancellation pre-image variants from `[2i §4.9]` (`*_aborted`, `*_cancelled`, `sync_lock_aborted`, `clock_sleeps_cancelled`, `dispatch_aborted`, `tls_load_cancelled`, `transport_*_cancelled`, `accept_cancelled`, `store_cancelled`, `store_visitor_aborted_due_to_cancel`). `Session.close()` docstring at §4.3 line 411 v0.2 adds `Cancelled` to its raises list. | §4.3 close docstring, §4.6 Cancelled docstring |
| **N-P2-2** `Engine.close()` cycle-collection / `Application↔Session` cycle has unspecified finalisation order | Opus (NEW) | P2; standalone | §6.2 v0.2: NEW paragraph "Cycle-GC finalisation order is unspecified — explicit close is the supported teardown path." Per PEP 442, CPython's cycle collector runs `__del__` for objects in cycles but the finaliser order is *unspecified*. Recommended idiom is the `with Engine(config) as engine:` context-manager pattern. v1.0 emits a `DeprecationWarning` if cycle-GC teardown is detected without prior explicit close; v1.x escalates to a hard error. §9 seam #3 verifies the warning fires. | §6.2, §9 seam #3 |
| **N-P3-1** Multiple cite errors compound `[const §IV.5]` (Codex P1-5) | Opus (NEW) | P3; **clustered into RC#3** | Three additional citation defects fixed: (a) §1 goal 5 — `[2g §1.2]` for OpenSSL/mimalloc bundling replaced with `[const §XII.1]` (OpenSSL) / `[arch §5.2]` (mimalloc); (b) §1 goal 5 already had `[arch §5.2]` — kept; (c) §6.4 — `[arch §4.4]` for "user mental model" replaced with `[arch §6]` last paragraph (the 6-method `Application` justification). Appendix B amended with new entries. | §1 goal 5, §6.4, Appendix B |

#### Codex findings disagreed-and-recorded (none at this round)

The Opus review confirmed all 15 Codex findings (with 2 demotions and 1 escalation; all addressed at the Opus-judged severity). No Codex finding was marked "Disagree" by Opus — every Codex finding either confirmed at Codex's severity, demoted (P2-2 to clarification, P2-6 to P3, P3-1/P3-3/P3-4 as accepted refutations), or escalated (P2-4 to P1).

#### Net-effect summary

- **Lines added:** ~280 (NEW §1.3 normative paragraph, NEW §6.1 supported-interpreter subsection, NEW §6.2 cycle-GC paragraph, NEW §6.3 director snippet, expanded §4.5 docstrings, expanded §6.5 enforcement prose, NEW §6.6 outbound-callback rows, NEW §6.7 row 1 remediation update, NEW §7.1 32-bit note, NEW §9 seam #13, populated Appendix C).
- **Lines removed:** ~30 (the fabricated `fixpp_python_callback_log_fatal` call + comment, the `StringIO` formatting block, the v0.1 §6.5 row 4 ban with its unsound rationale, the `_strand_thread_id` mention in enforcement, "engine logger" criterion in §9 seam #2, the v0.1 §3.9 incorrect "MUST NOT allocate Python objects on the engine strand" prose).
- **Lines changed:** ~120 across §1.1, §1.2 boundary, §3.4, §3.9, §3.11, §3.15, §4.4, §4.6, §6.4, §6.6 allocation table, §6.7 row 1, §9 seam #2 / #4 / #9, §10 Q2, Appendix B, status block.
- **New sub-sections:** §1.3 (SWIG director consumption rule — RC#2 normative backstop); §6.1 "Supported interpreter model (v1.0)"; §6.2 "Cycle-GC finalisation order" paragraph.
- **§1.3 SWIG director consumption rule paragraph (RC#2):** YES — added as a 4-bullet normative paragraph; collapses 5 findings (Codex P1-3, P1-4, P2-3, Opus N-P1-1, N-P1-2).
- **Appendix D drop-in to `[2i §1.1]` §1.1 line 68:** YES — promoted from stub to byte-faithful Before/After block at §D.3 quoting the actual `library/.specify/2i-capi.md:65–67` lines (block was already RESERVED for 2m at 2i v0.3 sign-off; the drop-in pivots it from "RESERVED" to "OCCUPIED-by-2m" with the four new variants `FIXPP_ERR_BINDING_PYTHON_CALLBACK_RAISED = 1200`, `_GIL_DEADLOCK = 1201`, `_OBJECT_LIFETIME = 1202`, `_WHEEL_ABI_MISMATCH = 1203`). Matches the byte-faithful Before/After pattern from 2j v0.3 / 2k v0.5 / 2l v0.4. (See §D.3 below.)
- **Test seams:** 12 → 13 (NEW seam #13 — outbound director latency bench). Seam #1 acceptance criterion updated to scope `mallocnesia` to `fixpp::*` / `fixpp_*` symbols only per RC#1. Seam #2 acceptance criterion (b) replaced (engine-logger → `sys.stderr`). Seam #4 reshaped (positive-test for send-from-callback; multi-thread `io_threads=4` verification of the §1.3 rule (4) marker). Seam #9 escalated to P1 with the `tools/check_layers.py` extension commitment.
- **New `fixpp_error_t` variants:** 0 (still 4 — codes 1200, 1201, 1202, 1203 introduced at v0.1; no new codes in v0.2). `GilDeadlock` (1201) is repurposed in v0.2: ban on `session.send()` removed; ban on `engine.close()` / `session.close()` from inside callback retained.
- **New published symbols:** 0 (the `fixpp_python_callback_log_fatal` fabricated symbol is **deleted**; not added as a real symbol).
- **§10 dispositions:** Q2 reshaped to "no constitutional backing yet"; Q1 / Q3–Q10 unchanged.
- **Cite hygiene corrections:** 4 (drop `[2g §1.2]` for OpenSSL bundling at §1 goal 5; replace `[arch §4.4]` with `[arch §6]` at §6.4; correct `[const §IV.5]` framing at §10 Q2 / non-goal #12; tighten `[2i §10 Q5]` reference at §1.3 / §4.5 / §6.5).
- **New Appendix B entries:** 5 (`[const §IV.5]`, `[const §XII.1]`, `[2i §10 Q5]`, `[2j §1.2]`, `[2k §2]`).

The doc's spine is unchanged: package layout (§4.1), `FixppError` block-mapped hierarchy (§4.6), the `[1200, 1299]` populate-by-D.1/D.2/D.3 amendment pattern, the wheel pipeline (§1.1 / §3.2 / §3.7 / §9 seam #5+#10), the §6.4 sync reacquire-and-call decision, §6.6 latency table, §10 open-questions slate, Appendix A coverage table — all preserved per Opus closing recommendation.

| Round | Reviewer | Findings (P1 / P2 / P3) | Resolution |
|---|---|---|---|
| 1 | Codex Gate A | 5 P1 / 6 P2 / 4 P3 | All addressed at v0.2 (per-finding rows above). |
| 1 | Opus adversarial | 9 P1 / 7 P2 / 5 P3 (combined post-judging); 3 root causes (RC#1 / RC#2 / RC#3) | All addressed at v0.2 via line-edits + 3 root-cause shape changes (§1.3 / §3.9 / RC#3 cite sweep). |
| 2 | Codex Gate A | 0 P1 / 2 P2 / 1 P3 | All addressed at v0.3 (per-finding rows in v0.2 → v0.3 sub-table below). |
| 2 | Opus adversarial | 1 P1 / 4 P2 / 4 P3 (combined post-judging); 0 new root causes | All addressed at v0.3 via line-edit + N-2-P1-1 fix (Appendix D §D.4 cross-doc amendment to 2j §11). |

### v0.2 → v0.3 (Phase A round 2 convergence pass)

**Reviews addressed:** Codex Gate A round 2 (0 P1 / 2 P2 / 1 P3) and Opus round-2 adversarial review (combined post-judging 1 P1 / 4 P2 / 4 P3; **0 new root causes** — RC#1 fully landed in v0.2; RC#2 landed with one tail captured as Codex P2-1; RC#3 landed with three tails captured as Codex P2-2 + N-2-P2-2 + N-2-P3-1). Closing recommendation followed: "v0.3 can ship after a single convergence pass" — line-edit + the N-2-P1-1 cross-doc-amendment fix; no full rewrite, no new RC.

#### Per-finding resolution table

| Finding | Source | Opus verdict | Resolution at v0.3 | Section(s) touched |
|---|---|---|---|---|
| **Codex round-2 P2-1** Error-code semantic overload: `GilDeadlock` (1201) used for sub-interpreter rejection AND close-from-callback deadlock | Codex round 2 | Confirm @ P2; standalone (post-RC#2 tail) | Split into two distinct codes: `FIXPP_ERR_BINDING_SUBINTERPRETER = 1201` (Python class `SubInterpreterRejected`; sub-interpreter rejection only) AND `FIXPP_ERR_BINDING_CALLBACK_REENTRANT_CLOSE = 1204` (Python class `CallbackReentrantClose`; close-from-callback deadlock). The v0.2-draft numeric value 1201 was never published in a tagged C-ABI release (v0.2 was an internal Gate A draft); the re-allocation does not violate `[const §X.4]`. (Sign-off P3 sweep removed the v0.2-draft `GilDeadlock` alias entirely — v0.3 is the first signed-off version, so there is no source-compat to preserve.) §6.7 table now lists 5 variants (was 4); §4.6 hierarchy gains both classes; §4.6 mapping table updated; Appendix D §D.1 / §D.3 / §D.5 list 5 occupied codes (was 4) leaving `[1205, 1299]` headroom (was `[1204, 1299]`). | §6.1, §4.6 hierarchy + mapping table, §4.1 `__init__.py`, §6.5 row + enforcement, §6.7 table + stability paragraph, Appendix D §D.1 / §D.3 / §D.5 |
| **Codex round-2 P2-2** Citation-table self-inconsistency: "(per-doc)" placeholders contradict "line ranges given for every cross-doc citation" | Codex round 2 | Confirm @ P2; cluster RC#3 tail | Five `(per-doc)` placeholders in Appendix B replaced with concrete line ranges verified by `sed -n` / `grep -n` against the live sibling docs: `[2b §6.4]` 610–615; `[2d §4.4]` 412–495; `[2d §4.5]` 496–656; `[2d §7.6]` 1241–1244; `[2e §4.4]` 678–738. New `[2j §11]` Appendix B entry added (1114–1133) for the v0.3 §D.4 amendment. The "Lines given for every cross-doc citation" header claim is now true of every Appendix B row. | Appendix B |
| **Codex round-2 P3-1** Ownership phrasing: `Session` "non-owning observer" yet drives `fixpp_session_close` | Codex round 2 | Confirm @ P3; standalone (editorial) | §4.3 `Session` docstring rewritten: "Wraps fixpp_session_t per [2i §4.2.1]. The native handle is engine-owned (the engine's session-registry per [2j §4.6] is the authoritative lifetime owner); the Python `Session` is the explicit-close driver for the bound session and MUST be closed before the engine via Session.close() / Engine.close() / context-manager `__exit__` per the §6.2 cycle-GC paragraph." Symmetric update to §6.2 strong-reference graph removes the "non-owning observer" framing and replaces it with "engine-owned native handle; explicit-close driver". No behaviour change. | §4.3, §6.2 |
| **N-2-P1-1** Outbound `Message.__init__` depends on `fixpp_session_post`, a not-yet-published C-ABI symbol | Opus round 2 (NEW) | P1; standalone (post-RC#2 tail) | **Option A applied (per the Opus reviewer's recommendation):** `[[nodiscard]] fixpp_error_t fixpp_session_post(fixpp_session_t* session, void (*closure)(void*), void* userdata)` queued as a v0.3 cross-doc amendment to `[2j §11]` hand-off table via NEW Appendix D §D.4. Reentrancy class `FIXPP_THREAD_SAFE`; runs the closure on the session strand; steady-state thunk per `[2i §5.2]`. §11 hand-off bullet 7 in this doc rewritten to remove the "future Phase-4 addition" framing and to point at §D.4 as the authoritative declaration site. §4.4 outbound `Message.__init__` docstring now references §D.4 as the symbol's declaration home. The Python idiom `Message(msg_type, session)` from non-strand threads is preserved (Option B narrowing was rejected). The amendment is byte-faithful Before/After against `[2j §11]` lines 1116–1122. | §4.4 docstring, §11 hand-off bullet 7, NEW Appendix D §D.4, status-block Inherits gains `[2j §11]`, Appendix B B.3 gains `[2j §11]` row |
| **N-2-P2-1** No pickleability statement for Session / Message / Engine | Opus round 2 (NEW) | P2; standalone | New paragraph in §6.2 "Pickleability — Engine / Session / Message / Application / Dictionary are NOT pickleable": each native-handle-wrapping class implements `__reduce_ex__` / `__reduce__` to raise `TypeError("fixpp.<ClassName> objects are not pickleable; native handles cannot cross process boundaries")`. Value-typed dataclasses (`Decimal`, `MsgVersion`, `EngineConfig`, `SessionConfig`, `LogConfig`) remain pickleable via the default dataclass `__reduce_ex__`. Cite `[const §X.5]` opaque-handle uniform-destroy discipline as the rationale (a pickled handle could outlive the engine, violating PY-004). §9 seam #3 gains an acceptance criterion verifying `pickle.dumps(engine) / dumps(session) / dumps(message)` raises `TypeError`. | §6.2 |
| **N-2-P2-2** Appendix D §D.3 Before block off-by-one line range (claimed "65–69" verifiably wrong; live `sed -n` output starts at line 66) | Opus round 2 (NEW) | P2; cluster RC#3 tail | §D.3 Before block annotation corrected from `lines 65–69` to `lines 66–70`; the `sed -n` verification command in the prose updated from `sed -n '65,69p'` to `sed -n '66,70p'`. Body content of the Before block (the 5 lines `[1000, 1099]` through `[1400+]`) is unchanged — it was already byte-faithful against the actual lines 66–70; only the annotated range was off. Mechanical 2-character fix per Opus's recommendation. | Appendix D §D.3 |
| **N-2-P3-1** `[arch §6]` last-paragraph cite reaches for content not in the section ("engine yields the strand to user code" framing) — 3 sites | Opus round 2 (NEW) | P3; cluster RC#3 tail | All three live sites reframed: §3.9 line 189 (RC#1 paragraph), §6.6 allocation-table row "Python user code inside `fromApp`", and §8 PMR recap. The "engine yields the strand to user code" framing is now a 2m-internal axiom (no fabricated cite); the SWIG director / GIL spine architectural anchor `[arch §4.12]` (lines 351–362) is the home of the Python-callback shape. The v0.2 cite attached the framing to `[arch §6]`'s 6-method `Application` justification, which talks about pure-virtual cap exceptions — content mismatch. The `[arch §6]` cites at §1.2 (line 77), §4.5 (line 643), §6.4 (line 1108), §7.5 (line 1285) are content-correct (they cite the 6-method `Application` justification or the `std::pmr::memory_resource*` factory rule, both of which ARE in §6) and are kept unchanged. | §3.9, §6.6 allocation table, §8 |
| **N-2-P3-2** Outbound `Message.__del__` reliance for native-handle teardown not flagged with §6.2 cycle-GC caveat | Opus round 2 (NEW) | P3; standalone (editorial) | §4.4 outbound `Message.destroy()` docstring extended with a "__del__-vs-interpreter-shutdown caveat" paragraph: __del__ is the PRIMARY teardown path for outbound Message and clones in typical user pattern, but CPython makes weak guarantees about __del__ invocation order at interpreter shutdown; an outbound Message whose __del__ runs after the parent Engine.__del__ has cleared the C-ABI symbol table will hit a stale-pointer call into fixpp_msg_destroy. The supported teardown path is therefore Session.send(msg) (ownership transfer to engine), explicit Message.destroy(), or surrounding Session's explicit close() (which walks WeakSet[Message] and arms _dead). __del__ is the defensive backstop; the engine-driven explicit-close flow is the reliable path. | §4.4 |
| **N-2-P3-3** `Engine.close` race / `register_application` legality bundled — should be separated | Opus round 2 (NEW) | P3; standalone (editorial) | Two distinct edits: (a) §6.5 enforcement adds a new "_in_callback check race" bullet describing the GIL-bounded race between an in-flight callback's _in_callback flag clear and Engine.close()'s read; the race is bounded by GIL acquire/release granularity. (b) §4.3 `Session.register_application` docstring adds a "Legality from inside a callback" paragraph: calling register_application from inside an in-flight callback is LEGAL; the swap takes effect at the NEXT callback dispatch (the in-flight callback continues to see the old `self._application` reference). Separates cleanly from the close-from-callback case (which raises CallbackReentrantClose). | §4.3 register_application docstring, §6.5 enforcement bullets |

#### Codex findings disagreed-and-recorded (none at this round)

The Opus round-2 review confirmed all 3 Codex round-2 findings (P2-1, P2-2, P3-1) at Codex's severity. No Codex finding was disagreed; no Codex finding was demoted or escalated.

#### Net-effect summary (v0.2 → v0.3)

- **Lines added:** ~95 (NEW Appendix D §D.4 cross-doc amendment to 2j §11; NEW §6.2 pickleability paragraph; NEW §6.5 _in_callback-race bullet; NEW §4.3 register_application legality paragraph; NEW §4.4 destroy()/__del__ interpreter-shutdown caveat; NEW §6.7 table row for `FIXPP_ERR_BINDING_CALLBACK_REENTRANT_CLOSE`; NEW §4.6 `SubInterpreterRejected` and `CallbackReentrantClose` Python class definitions; v0.2→v0.3 Appendix C sub-table).
- **Lines removed:** ~10 (the v0.2 `GilDeadlock` (1201) class definition and table row replaced with the split shape; the v0.2 §4.3 "non-owning observer" prose tightened; the §D.3 v0.2 line-range annotation `65–69` corrected to `66–70`).
- **Lines changed:** ~40 across status block, §4.1 `__init__.py` re-exports, §4.3 Session docstring + register_application docstring, §4.4 outbound docstring, §4.6 hierarchy + mapping table + stability paragraph, §6.1 sub-interpreter rejection prose, §6.2 strong-reference graph, §6.5 close-from-callback row + enforcement bullet, §6.7 table + stability paragraph, §11 hand-off bullet 7, §3.9 + §6.6 allocation table + §8 (the `[arch §6]` cite reframing), Appendix B (5 line-range populations + new `[2j §11]` row), Appendix D §D.1 / §D.2 / §D.3 / §D.5 (5-variant updates) + NEW §D.4.
- **New cross-doc amendments:** 1 (NEW Appendix D §D.4 — `fixpp_session_post` declaration queued in `[2j §11]` hand-off table).
- **New `fixpp_error_t` variants:** 1 (the v0.2 `FIXPP_ERR_BINDING_GIL_DEADLOCK = 1201` is split into TWO codes: `FIXPP_ERR_BINDING_SUBINTERPRETER = 1201` and `FIXPP_ERR_BINDING_CALLBACK_REENTRANT_CLOSE = 1204`; net change = +1 published code; the `[1200, 1299]` block now has 5 occupied / 95 free per the Codex round-2 P2-1 split).
- **Cite hygiene corrections:** 3 sites of `[arch §6]` last-paragraph reach reframed (§3.9, §6.6, §8) per N-2-P3-1; one Appendix D §D.3 line-range off-by-one correction per N-2-P2-2 (`65–69` → `66–70`); 5 Appendix B line-range populations + 1 new entry per Codex P2-2.
- **Test seams:** 13 → 13 (no new seams; §9 seam #3 gains a pickleability acceptance criterion per N-2-P2-1).
- **§10 dispositions:** unchanged.
- **Status block:** Status → `Draft v0.3 — Gate A round 2 converged (Phase A)`; Date → `2026-05-10`; Convergence-log line extended with the round-2 review tally.

> **Sign-off P3 sweep (no version bump):** `fixpp_session_post` formal declaration gains `[[nodiscard]] fixpp_error_t` return qualifier at the 2 declaration sites (Appendix C convergence-log row + Appendix D §D.4 After block) per N3-P3-1; the v0.2-draft `GilDeadlock` alias removed entirely (Option A — v0.3 is the first signed-off version, so there is no source-compat to preserve) per N3-P3-2 — functional uses removed at the §4.1 import block / §4.1 `__all__` list / §4.6 `GilDeadlock = CallbackReentrantClose` line / `CallbackReentrantClose` docstring alias-paragraph / §4.6 mapping table 1204 row / §6.5 row 5 / §6.5 enforcement bullet / §6.7 table row / §9 seam #4 prose, with convergence-log historical references retained as records of the v0.2-draft → v0.3 split; the Python attribute `_inbound` renamed to `_is_inbound` at 4 code sites (§1.3 rule (1), §4.4 outbound docstring, §6.4 dispatch step (c), §6.5 row 2) per N3-P3-3 — PEP 8 boolean idiom; convergence-log historical references to `_inbound` retained as records of the rename. Editorial only, no semantic shape change.

The doc's spine is unchanged: the §1.3 SWIG director consumption rule, the §3.9 RC#1 allocation paragraph, the §4.1 package layout, the `FixppError` block-mapped hierarchy, the wheel pipeline, the §6.4 sync reacquire-and-call decision, §6.6 latency table, §10 open-questions slate, Appendix A coverage table — all preserved per Opus closing recommendation. v0.3 is the convergence pass that closes the round-2 tail; no further root-cause shape changes are expected at sign-off.

---

## Appendix D — Cross-doc amendments queued at sign-off

The following Before / After drop-ins amend sibling design docs at 2m sign-off. They are **stubs** at v0.1 — applied only when 2m converges through Gate A.

### §D.1 `[2i §4.3]` enum table — append the `[1200, 1299]` block

**Before** (excerpt from `library/.specify/2i-capi.md` `c_api/error.h` definitions — §4.3 lines 574–579):

```c
/* ── Reserved blocks ─────────────────────────────────────────────────── */
/* [1000, 1099] reserved for 2k log + otel (FIXPP_ERR_LOG_*, FIXPP_ERR_OTEL_*) */
/* [1100, 1199] reserved for 2l tap (FIXPP_ERR_TAP_*) */
/* [1200, 1299] reserved for 2m bindings translation (FIXPP_ERR_BINDING_*) */
/* [1300, 1399] reserved for post-v1.x growth */
/* [1400+]      reserved for future expansion */
```

**After** (same location, with the `[1200, 1299]` block populated):

```c
/* ── Reserved blocks ─────────────────────────────────────────────────── */
/* [1000, 1099] reserved for 2k log + otel (FIXPP_ERR_LOG_*, FIXPP_ERR_OTEL_*) */
/* [1100, 1199] reserved for 2l tap (FIXPP_ERR_TAP_*) */

/* ── Bindings block [1200, 1299] — 2m-owned per [2m §6.7] (5 variants) ── */
#define FIXPP_ERR_BINDING_PYTHON_CALLBACK_RAISED   ((fixpp_error_t) 1200)
#define FIXPP_ERR_BINDING_SUBINTERPRETER           ((fixpp_error_t) 1201)
#define FIXPP_ERR_BINDING_OBJECT_LIFETIME          ((fixpp_error_t) 1202)
#define FIXPP_ERR_BINDING_WHEEL_ABI_MISMATCH       ((fixpp_error_t) 1203)
#define FIXPP_ERR_BINDING_CALLBACK_REENTRANT_CLOSE ((fixpp_error_t) 1204)
/* [1205, 1299] reserved for 2m growth. */

/* [1300, 1399] reserved for post-v1.x growth */
/* [1400+]      reserved for future expansion */
```

### §D.2 `[2i §1.1]` magnitude-domain table — append the 2m row

**Before** (excerpt from `library/.specify/2i-capi.md` §1.1 lines 34–46):

```
| `FIXPP_ERR_DECIMAL_*` | 2a | 4 | `[2a §7.4]` |
| `FIXPP_ERR_WIRE_*` | 2b | 13 | `[2b §6.7]` |
…
| `FIXPP_ERR_CAPI_*` (2i-introduced) | 2i | 8 | §6.5 |
| `FIXPP_ERR_THREAD_SESSION_*` (lifecycle subset) | 2d (already counted above) | (subset of 2d) | `[2d §6.7]` |
```

**After** (append one row):

```
| `FIXPP_ERR_DECIMAL_*` | 2a | 4 | `[2a §7.4]` |
| `FIXPP_ERR_WIRE_*` | 2b | 13 | `[2b §6.7]` |
…
| `FIXPP_ERR_CAPI_*` (2i-introduced) | 2i | 8 | §6.5 |
| `FIXPP_ERR_THREAD_SESSION_*` (lifecycle subset) | 2d (already counted above) | (subset of 2d) | `[2d §6.7]` |
| `FIXPP_ERR_BINDING_*` (2m-introduced) | 2m | 5 | `[2m §6.7]` |
```

### §D.3 `[2i §1.1]` final-layout block — pivot the `[1200, 1299]` annotation from RESERVED to OCCUPIED-by-2m (byte-faithful Before/After)

The Appendix D drop-in promotes the `[1200, 1299]` block from "RESERVED for 2m" (the v0.3 2i carve-out at `library/.specify/2i-capi.md:68`) to "OCCUPIED-by-2m with 5 variants assigned" at 2m sign-off (v0.3: 5 variants, up from v0.2's 4 — Codex round-2 P2-1 split `GilDeadlock` into a distinct sub-interpreter code 1201 and a distinct callback-reentrant-close code 1204). The Before block is the byte-faithful current text of `library/.specify/2i-capi.md:66–70` (the 5 lines from `[1000, 1099]` through `[1400+]`); the After block matches the byte-faithful 2j v0.3 / 2k v0.5 / 2l v0.4 amendment pattern (Codex P3-1 close — populate within an already-reserved block; the reservation itself was made at 2i v0.3 sign-off). **(v0.3: line-range corrected from `65–69` to `66–70` per Opus round-2 N-2-P2-2 — the v0.2 annotation was off-by-one against the live `sed -n` output.)**

**Before** (byte-faithful excerpt from `library/.specify/2i-capi.md` §1.1 lines 66–70 — verified by `sed -n '66,70p'` against the current 2i v0.3):

```
[1000, 1099] RESERVED: 2k log + otel (FIXPP_ERR_LOG_*, FIXPP_ERR_OTEL_*)
[1100, 1199] RESERVED: 2l tap (FIXPP_ERR_TAP_*)
[1200, 1299] RESERVED: 2m bindings translation (FIXPP_ERR_BINDING_*)
[1300, 1399] RESERVED: post-v1.x growth (one of: SOFH, FIX-Latest, SBE, FIXP, FAST per [const §XVIII.2])
[1400+]      RESERVED: future expansion
```

**After** (replace the `[1200, 1299]` line; surrounding lines unchanged — byte-faithful match against the rest of the block):

```
[1000, 1099] RESERVED: 2k log + otel (FIXPP_ERR_LOG_*, FIXPP_ERR_OTEL_*)
[1100, 1199] RESERVED: 2l tap (FIXPP_ERR_TAP_*)
[1200, 1299] FIXPP_ERR_BINDING_*  (2m-owned per [2m §6.7]; 5 occupied; assigned at 2m sign-off 2026-05-10)
[1300, 1399] RESERVED: post-v1.x growth (one of: SOFH, FIX-Latest, SBE, FIXP, FAST per [const §XVIII.2])
[1400+]      RESERVED: future expansion
```

The 5 occupied variants are `FIXPP_ERR_BINDING_PYTHON_CALLBACK_RAISED = 1200`, `FIXPP_ERR_BINDING_SUBINTERPRETER = 1201`, `FIXPP_ERR_BINDING_OBJECT_LIFETIME = 1202`, `FIXPP_ERR_BINDING_WHEEL_ABI_MISMATCH = 1203`, `FIXPP_ERR_BINDING_CALLBACK_REENTRANT_CLOSE = 1204`, leaving `[1205, 1299]` for v1.x densification per the §1.1 "100-wide block accommodates ≥ 4× the worst current count" rule. The sign-off date placeholder is filled in at the actual 2m Gate A sign-off (2026-05-10).

### §D.4 Cross-doc amendment to `[2j §11]` hand-off — declare `fixpp_session_post` as a v1.0 C-ABI symbol owned by 2j

**(NEW in v0.3 — closes Opus round-2 N-2-P1-1.)** The §4.4 outbound `Message.__init__` contract specifies that the Python `__init__` posts the `fixpp_msg_create_outbound` construction onto the session strand via `fixpp_session_post(session, closure, userdata)` per `[2i §6.3]` line 1365 (which annotates the function as "signature owned by 2j"). v0.2 §11 hand-off bullet 7 framed this as a future Phase-4 addition; that framing is incompatible with the v1.0 outbound construction contract being **mandatory** in v1.0. v0.3 resolves the gap by queueing the symbol declaration in 2j's hand-off as a 2m-driven amendment — the symbol becomes a declared v1.0 C-ABI thunk at 2j sign-off (or at 2j's next minor revision after 2m sign-off, whichever is later, per the orchestrator's apply ordering). Option A from the Opus review's recommendation; Option B (narrow §4.4 to strand-only construction in v1.0) is rejected because it forces every Python user constructing an outbound message to write the strand-post boilerplate themselves, defeating the published Python idiom `Message(msg_type, session)`.

**Before** (byte-faithful excerpt from `library/.specify/2j-controlplane.md` §11 lines 1116–1122 — verified by `sed -n '1116,1122p'` against the current 2j v0.3):

```
**Docs unblocked by 2j sign-off (downstream):**

- **2k (async logger + OTel)** — needs the `StreamMetrics` / `StreamLogs` wire shape (§4.7 / §4.8) including the OTel correlation field tags pinned in §D.3, and the close-on-overflow per-stream backpressure contract (§4.8 / §6.4). The C-ABI registration symbols (`fixpp_engine_register_metric_consumer` / `_log_sink`) are **owed by 2k**; v0.2 does not introduce them (RC#1 close).
- **2l (session-tap consumer)** — needs the explicit non-overlap statement (§7.9 / §1.2) so 2l's iceoryx2 + in-process-tap design knows what 2j does NOT cover. Without 2j, 2l would need to assume an over-broad scope. SVC-002 / SVC-003 catalogue rows remain 2l's per `[arch §8.2]` (RC#2 close).
- **Session-module Phase-4 spec** — needs the `OpenSession` / `CloseSession` / `Configure` proto request shapes (§4.7) so it can lock the session-config-builder C-ABI thunks (CA-005) the gRPC handlers consume. Without 2j, the session-module Phase-4 spec doesn't know what fields are operator-settable from outside the engine. The `StreamSessionEvents` C-ABI registration symbol shape is owed to that spec, not 2j.
- **2m (SWIG/Python)** — consumes 2i's C-ABI surface as input per `[2i §1.2]`. v0.2 introduces no new C-ABI symbols (RC#1 / RC#5 close), so 2m's consumption surface is 2i v0.3 plus whatever 2k / Phase-4 publish later.
- **2i v0.4 (post-v1.0)** — the rotation surface (`fixpp_pinset_add` / `fixpp_pinset_remove` / `fixpp_engine_reload_cert_source` or equivalents) is the prerequisite for v1.x discharging §10 Q1 + Q9. 2i v0.4 is owed at the v1.x milestone, not at v1.0.
```

**After** (replace the 2m bullet; surrounding bullets unchanged):

```
**Docs unblocked by 2j sign-off (downstream):**

- **2k (async logger + OTel)** — needs the `StreamMetrics` / `StreamLogs` wire shape (§4.7 / §4.8) including the OTel correlation field tags pinned in §D.3, and the close-on-overflow per-stream backpressure contract (§4.8 / §6.4). The C-ABI registration symbols (`fixpp_engine_register_metric_consumer` / `_log_sink`) are **owed by 2k**; v0.2 does not introduce them (RC#1 close).
- **2l (session-tap consumer)** — needs the explicit non-overlap statement (§7.9 / §1.2) so 2l's iceoryx2 + in-process-tap design knows what 2j does NOT cover. Without 2j, 2l would need to assume an over-broad scope. SVC-002 / SVC-003 catalogue rows remain 2l's per `[arch §8.2]` (RC#2 close).
- **Session-module Phase-4 spec** — needs the `OpenSession` / `CloseSession` / `Configure` proto request shapes (§4.7) so it can lock the session-config-builder C-ABI thunks (CA-005) the gRPC handlers consume. Without 2j, the session-module Phase-4 spec doesn't know what fields are operator-settable from outside the engine. The `StreamSessionEvents` C-ABI registration symbol shape is owed to that spec, not 2j.
- **2m (SWIG/Python)** — consumes 2i's C-ABI surface as input per `[2i §1.2]`. 2j publishes one v1.0 C-ABI symbol owed to 2m: **`[[nodiscard]] fixpp_error_t fixpp_session_post(fixpp_session_t* session, void (*closure)(void*), void* userdata)`** — the strand-post primitive `[2i §6.3]` line 1365 references as "signature owned by 2j". The reentrancy class is `FIXPP_THREAD_SAFE` (callable from any Python thread; the closure runs on the session strand). The thunk is a steady-state path per `[2i §5.2]`. 2m's §4.4 outbound `Message.__init__` is the v1.0 consumer; without this declaration, the Python idiom `Message(msg_type, session)` from non-strand threads has no honest implementation. (NEW v0.3 — closes 2m round-2 N-2-P1-1.)
- **2i v0.4 (post-v1.0)** — the rotation surface (`fixpp_pinset_add` / `fixpp_pinset_remove` / `fixpp_engine_reload_cert_source` or equivalents) is the prerequisite for v1.x discharging §10 Q1 + Q9. 2i v0.4 is owed at the v1.x milestone, not at v1.0.
```

**Why an amendment to 2j (not 2i, not 2m).** Per `[2i §6.3]` line 1365, 2i explicitly defers the declaration to 2j ("signature owned by 2j"). Per the v0.2 §11 hand-off bullet 7, 2m's stance is consumer-only ("does NOT own ... the C ABI surface itself — owned by 2i"); 2m introducing a new C-ABI symbol would stretch its scope. 2j is the right home: 2j already owns the engine/session control-plane symbols (the `fixpp_session_close` / `fixpp_engine_destroy` family), and `fixpp_session_post` is the strand-post primitive in the same family. The v0.2 framing of the symbol as "Phase-4 future" was a category error caught in round 2 — Phase-4 does not exist yet in the design-doc layer, and 2m's v1.0 contract cannot depend on a Phase-4 symbol.

**Companion §11 hand-off bullet update in 2m (v0.3).** §11 hand-off bullet 7 in this doc is rewritten in v0.3 to remove the "future Phase-4 addition" framing for `fixpp_session_post` and to point at this Appendix D §D.4 amendment as the authoritative declaration site.

### §D.5 `tools/abi_history/error_codes_v1.txt` append-only audit file — add 5 lines

**Before** (last lines of the file as published in 2i v0.3):

```
…
900  FIXPP_ERR_CTRL_CONFIG               2j v0.3
901  FIXPP_ERR_CTRL_RUNTIME              2j v0.3
```

**After** (append 5 lines at 2m sign-off — v0.3: 5 lines, up from v0.2's 4, per Codex round-2 P2-1 split):

```
…
900   FIXPP_ERR_CTRL_CONFIG                          2j v0.3
901   FIXPP_ERR_CTRL_RUNTIME                         2j v0.3
1200  FIXPP_ERR_BINDING_PYTHON_CALLBACK_RAISED       2m v0.1 (sign-off)
1201  FIXPP_ERR_BINDING_SUBINTERPRETER               2m v0.3 (sign-off)
1202  FIXPP_ERR_BINDING_OBJECT_LIFETIME              2m v0.1 (sign-off)
1203  FIXPP_ERR_BINDING_WHEEL_ABI_MISMATCH           2m v0.1 (sign-off)
1204  FIXPP_ERR_BINDING_CALLBACK_REENTRANT_CLOSE     2m v0.3 (sign-off)
```

### §D.6 `feature-catalogue.md` PY-001..PY-005 rows — add `Coverage` column references

At sign-off, the Coverage column for PY-001..PY-005 in `library/spec/feature-catalogue.md` is updated from the placeholder `—` to `2m §X.Y` per the Appendix A table above. This mirrors the pattern used by 2k and 2l at their sign-offs.
