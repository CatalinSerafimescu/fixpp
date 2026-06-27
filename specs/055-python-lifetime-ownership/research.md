# Research & Decisions — PY-004 Python lifetime/ownership layer (055)

Format per decision: **Decision** / **Rationale** / **Alternatives rejected**. Grounded in `[2m]` (the pybind design doc) §6.2/§6.4/§6.5/§6.7/§1.3/§4.4/§4.5/§9, verified against the as-built `bindings/python/fixpp.i` and `include/fix/c_api/`.

---

## D-1 — Scope boundary: lifetime layer over the EXISTING C-ABI; NOT the §4.5 6-method director

**Decision.** PY-004 builds the lifetime/ownership OO layer over the **existing** C-ABI substrate only. The `Application` wrapper exposes the **single inbound callback** (`fromApp`, via the existing `fixpp_recv_cb` trampoline). The `[2m §4.5]` **6-method `%feature("director")` Application** (adding `onLogon` / `onLogout` / `toAdmin` / `fromAdmin`) and the SWIG-director cross-language-polymorphism mechanism are **OUT of scope** and deferred to a future callback-surface-completeness feature (PY-001's domain).

**Rationale.** PY-004 is the *lifetime/ownership* row; callback *breadth* is callback-surface completeness, a different axis (the `[2m §4.5]` Application definition, not the §6.2 lifetime model the user selected). The four extra director methods have **no C-ABI hooks today** (`session.h` exposes only `fixpp_recv_cb` + the `%ignore`d `fixpp_send_cb`; there is no state/admin callback — the deferred GAP-004). Adding them is **additive** new C-ABI (`fixpp_session_register_state_callback`, admin taps) — addable post-1.0 at a MINOR without touching any existing signature, so it is **not** a freeze concern and PY-004 surfacing it does not obligate a pre-freeze fix (same disposition as GAP-004). The `[2m]` Article XX amendments (§1.3 rule (4), §6.5, §6.7 — lines 97/1197/1262) already state the flat binding has no director and that "active detection is PY-004", explicitly sanctioning this split.

**Alternatives rejected.** (a) Build the full §4.5 6-method director now — needs additive C-ABI + the director machinery; out of the lifetime mandate; gold-plating. (b) Put A/B to the user — there is no genuine fork: the breadth was never in PY-004.

---

## D-2 — Deliver the OO behaviors in the EXISTING hand-trampoline; no SWIG director

**Decision.** Implement the inbound-callback OO behaviors (Message-wrapper construction, `_dead`-arming at callback return, `_in_callback` set/clear) inside the **existing** `%wrapper` receive trampoline (`fixpp_py_recv_trampoline`, `fixpp.i:445`). The trampoline's `userdata` changes from "the bare callable" to "the owning Python `Session` wrapper" (from which the registered `Application`/callback and the `_in_callback` slot are reachable). No `%feature("director")`.

**Rationale.** `[2m]` describes these behaviors "in the director's entry/exit path" because it *assumed* a director, but they are just hand-C in the adapter — and the existing trampoline IS that adapter (it already does `PyGILState_Ensure` → `SWIG_NewPointerObj(..., own=0)` → call Python → `PyGILState_Release`). Hosting wrapper construction (`PyObject_CallFunction` to build a `Message`), attribute sets (`PyObject_SetAttrString` for `_dead`/`_in_callback`), and the `fromApp` dispatch is all feasible in `%wrapper` with **zero C-ABI change**. Verified: the trampoline at `fixpp.i:445-473` is exactly this hand-C.

**Alternatives rejected.** Introduce a SWIG director — larger machinery, and the design's own Article XX amendments say the flat binding has no director and defers it.

---

## D-3 — Liveness sentinel = Python-side `_dead` flag checked BEFORE every C-ABI call

**Decision.** Each handle-bearing wrapper carries `_handle` (the opaque SWIG pointer/handle) + `_dead: bool`. Every accessor checks `_dead` first; if set, it raises `fixpp.ObjectLifetime` (numeric 1202) **without** calling the C-ABI. `_dead` is flipped by the owning parent's close-flow (D-4) or, for inbound messages, by the trampoline at callback return (D-8).

**Rationale.** `[2m §6.2]` sentinel pattern. The check is Python-side and pre-C-ABI, so no native deref of a freed handle is reachable. `ObjectLifetime` (1202) already exists both as a C-ABI code (`error.h:159`) and as a Python class (`fixpp.i:169`, mapping `:252`) from PY-003/054 → **raising it mints no new code; freeze unaffected.** Defence-in-depth: even if a check were missed, the 050/052 native tombstones return an error code (→ `CapiError`), not a UAF.

**Alternatives rejected.** C-side validity/generation token — would require new C-ABI (breaking-window risk) for zero benefit over the Python-side flag.

---

## D-4 — Ownership graph: Py_INCREF parents + weakref child sets + ordered close-flow

**Decision.** Strong-ref UP (`Message` → `Session` → `Engine`, via `Py_INCREF`/holding the Python ref); track children DOWN weakly (`Session` holds `weakref.WeakSet[Message]`; `Engine` holds `weakref.WeakSet[Session]`). `Engine.close()` / `Session.close()` execute the `[2m §6.2]` ordered sequence (see C-3 for the exact steps): for `Session.close()` — (0) **reentrancy fail-fast FIRST** (pure-Python, before arming `_dead` / walking children / dropping `_application` / any C-ABI): `if self._in_callback: raise fixpp.CallbackReentrantClose` (1204) — the per-session backstop preventing a `close()`-from-its-own-callback from arming `_dead`, dropping `_application`, and entering the blocking native close (the L-054-1 deadlock, SC-007); (1) **arm the session's own `_dead` first** (BEFORE the native close — NEW-P2a, so a concurrent call racing the GIL-release window of the blocking native close sees `_dead` and raises `ObjectLifetime` rather than entering the C-ABI); (2) walk the child WeakSet and set each child `_dead = True`; (3) release the owned callable by **dropping `session._application`** (the leak fix; breaks the Application↔Session cycle — the `userdata`=`Session` INCREF is released only *after* the native close, D-7); (4) call the native close (`fixpp_session_close`). `Engine.close()` first runs a **pure-Python all-child-sessions `_in_callback` preflight** (raise `CallbackReentrantClose` before touching any C-ABI; C-3), then **arms the engine's own `_dead` first** (so concurrent public engine entry raises `ObjectLifetime` rather than entering the C-ABI during the GIL-release window), then closes each child session via a **private unguarded close helper**, and finally calls `fixpp_engine_destroy` via a **private unguarded destroy helper**. `close()` is **idempotent** (guarded by a `_dead`/`_was_explicitly_closed` check — second call is a no-op).

**Rationale.** `[2m §6.2]` strong-reference graph + weakref discipline + "Engine close flow (the critical sequence)". Child invalidation happens BEFORE the native close so no Python accessor races the free. The Application↔Session cycle is collectable (the close breaks it; otherwise CPython's cycle GC handles it).

**Alternatives rejected.** Strong child refs — would create uncollectable cycles / leaks. Invalidate after native close — leaves a race window.

---

## D-5 — Reentrancy: GIL-protected `session._in_callback` marker; raise CallbackReentrantClose on ALL THREE blocking APIs (Article XX amendment)

**Decision.** The trampoline sets `session._in_callback = True` on entry and clears it before GIL release on exit (GIL-protected; on the Python `Session` instance, NOT `threading.local`/thread-id). `Session.send`, `Session.close`, and `Engine.close` (walking child sessions) check the marker and raise `fixpp.CallbackReentrantClose` (1204) **before** entering the C-ABI. This covers **all three** blocking APIs (send + close + engine_destroy) per the clarify decision.

**Rationale.** As-built, all three deadlock from inside a callback (L-054-1: the blocking 050 `send` + the close-drain both block the session strand). Detecting all three turns every reachable deadlock into a clean, catchable error — the safest v1.0 behavior. `[2m §1.3]` rule (4) prescribes the GIL-protected marker (correct under all three `[2d §4.5]` threading modes, including `engine_thread_pool_strand` where thread-id detection is unsound). **⚠️ Article XX checkpoint:** raising on `send` contradicts `[2m]` §9 seam #4 / §1.3 rule (2), which call send-from-callback *legal* (aspirational — predicated on a deferred *engine* non-blocking-send fix NOT in v1.0). This feature amends `[2m]` at **6 substantive sites** — §1.3 rule (2), §6.5 carve-out send row, §6.5 carve-out close row, §9 seam #4, §6.7 1204 **table row**, and the §6.7 1204 **prose docstring** (`:818-820`, the residual stale narrative) — plus the editorial `Session.send` method docstring (`:455-460`), so v1.0 raises on send too. The amendment is **carried as proposed text and DEFERRED to `/implement`** (NOT pre-applied in-place); the complete amendment text is in the "Proposed `[2m]` Article XX amendment" section at the end of this file. Gate A reviews (mirrors 054's Article XX pattern).

**Alternatives rejected.** Detect close/destroy only (seam #4-literal) — leaves the common auto-reply `send`-from-callback pattern silently hanging until the deferred engine fix. Document-only — contradicts the design intent that PY-004 implements active detection; leaves deadlock footguns.

---

## D-6 — Sub-interpreter rejection at Engine construction → SubInterpreterRejected (1201)

**Decision.** `Engine` construction detects a non-main CPython interpreter (PEP 554) and raises `fixpp.SubInterpreterRejected` (1201) before any native engine is created.

**Rationale.** `[2m]` §9 seam #4 + §6.7 row 1201. v1.0 supports only the main interpreter; the engine's native state is not sub-interpreter-safe. The check is cheap, at construction. Mechanism (to settle at implement): compare the current interpreter to the main interpreter via the CPython C-API in the trampoline/engine_create hand-wrapper (e.g. `PyInterpreterState_Get()` vs `PyInterpreterState_Main()`), surfacing `SubInterpreterRejected`. Code 1201 already exists (`error.h:157`, `fixpp.i:168`/`:251`) → no new code.

**Alternatives rejected.** Defer — a sub-interpreter user would get undefined behavior instead of a clean error; the design specifies the rejection and the cost is trivial.

---

## D-7 — Callable lifetime: owned-by-Session via `_application`; `userdata` INCREFs the Session

**Decision.** The trampoline's `userdata` is the owning Python `Session` wrapper (D-2), INCREF'd at registration. The registered `Application`/callable is owned by the `Session` through the plain Python attribute `session._application`, and is released by **dropping that attribute** on `Session.close()` and on re-registration. This replaces the 053/054 trampoline's `Py_INCREF`-of-the-callable-until-interpreter-exit retention (no release path today).

**TWO distinct references, distinct owners (this is the feature's own UAF/leak domain).** (1) The binding holds exactly **one binding-owned strong reference** to the **callable**: the Python attribute `Session._application` (the user may additionally hold their own external references — the guarantee is over the binding's reference, not global object lifetime). (2) The **`Session` wrapper** is referenced by the C-ABI registration `userdata`, which the register hand-wrapper `Py_INCREF`s (a strong ref from C into Python) so the native side can dispatch — the trampoline receives the `Session` as `userdata` and reaches the callable via `session._application` (and sets `_in_callback`, builds/arms the inbound `Message`). The leak fix is the **matched release of the callable** = dropping `session._application`; it is NOT "remove an INCREF on the callable" (there is no standing INCREF on the callable in this model).
- **Leak release:** dropping `session._application` (C-3 step 3) releases the **binding's** reference to the callable immediately (the binding then holds no further reference to it); once the caller's own external strong references are also dropped, a `weakref` to it is dead after `gc.collect()` (SC-002). This may happen *before* the native close and is safe — the native side dispatches via the `Session` (`userdata`), which is still alive; a callback already in flight read `_application` under the GIL, and once it is dropped no new dispatch finds a callable (the close has begun; `_dead` / the native tombstone short-circuit any further dispatch).
- **`userdata` release ordering (load-bearing):** the `Session`-`userdata` INCREF MUST be released **at or after** the native close (`fixpp_session_close`), once dispatch has stopped — **never before**. A fixer must NOT DECREF the `Session`-`userdata` ref before the native close, or an in-flight callback dispatches into a finalized `Session` wrapper (UAF).
- **Re-registration** is a plain attribute reassignment (`session._application = new_callable`): the binding's reference to the prior callable is released (the binding then retains no further reference to it); the `userdata` (the `Session`) is **unchanged** — no native re-pointing, no `userdata` DECREF.
- **GC-safety invariant:** because the registration holds a strong (INCREF'd) ref to the `Session`, the `Session` (and its currently-attached `_application`, and transitively anything that keeps alive) cannot be GC'd while the native session can still dispatch. `__del__`'s best-effort close (D-10) MUST use the same ordering (drop `_application`; native close; then release the `Session`-`userdata` INCREF).

**Rationale.** `[2m §6.2]` strong-ref graph + §4.5 "Application lifetime" (Session un-references on close). Fixes the standing per-registration leak (FR-011) WITHOUT opening a dispatch-into-freed-callback/Session window. Witnessed by a `weakref` going dead after `gc.collect()` post-close **once the caller's external strong refs are dropped** (SC-002 — the binding released its own ref), reusing the 053 `test_register_callback_after_start_no_leak` technique; ASan covers the ordering (no callback/Session UAF).

**Alternatives rejected.** Keep hold-until-interpreter-exit — the known leak; unacceptable for a lifetime feature. `userdata` = the bare callable (the as-built `fixpp.i:459` model) — structurally cannot reach the `Session` the PY-004 trampoline needs (to set `_in_callback`, build/arm the inbound `Message`); rejected per D-2. Release the `Session`-`userdata` ref before the native close — opens a UAF if a callback is in flight.

---

## D-8 — Message flavours: inbound flyweight (engine-owned, `_dead` at return) vs outbound (Python-owned, `__del__`→destroy)

**Decision.** The `Message` wrapper carries `_is_inbound: bool`. **Inbound flyweight**: constructed by the trampoline with `own=0`, `_is_inbound=True`, `_dead=False`; the trampoline arms `_dead=True` before GIL release on `fromApp` return (closes **L-053-1**); `__del__` does NOT call `fixpp_msg_destroy`. **Outbound/clone**: Python-owned (`_is_inbound=False`); `__del__` calls `fixpp_msg_destroy` (idempotent). Accessor breadth wraps the existing flat surface (`get_string` + field iteration already exposed by 053; the richer `get_int/double/decimal/bytes/has_tag/get_group/clone` exist in the C-ABI (051) and MAY be wrapped trivially, but exhaustive accessor wrapping is callback-surface completeness — wrap what the lifetime tests + round-trip need, defer the rest with D-1).

**Rationale.** `[2m §4.4]` three Message shapes + §6.2 Flavour 1/2 + §6.4 step 3.f (`_dead` armed before GIL release). The `_is_inbound` flag drives the destroy decision so inbound never double-frees an engine-owned handle and outbound never leaks. `fixpp_msg_set_*` on an inbound message is rejected with `fixpp.CapiError(code=4)` (a distinct path from the `_dead` sentinel, per `[2m §1.3]` rule (1)) — short-circuited Python-side by `_is_inbound`.

**Alternatives rejected.** One uniform Message ownership — would double-free inbound or leak outbound. Full accessor surface now — surface completeness, out of the lifetime mandate (D-1).

---

## D-9 — Pickle-ban on handle-bearing wrappers; value-types out of scope

**Decision.** `Engine` / `Session` / `Message` / `Application` / `Dictionary` implement `__reduce_ex__` / `__reduce__` to raise `TypeError("fixpp.<ClassName> objects are not pickleable; native handles cannot cross process boundaries")`. PY-004 introduces NO value-typed Python classes (Decimal/MsgVersion/EngineConfig/SessionConfig/LogConfig); FR-014's value-type-pickleable leg (and the `[2m]` §9 seam #3 value-pickle assertion) is deferred with those types.

**Rationale.** `[2m §6.2]` pickleability rule — a pickled `void*` would dereference freed/foreign memory on unpickle (the `multiprocessing.Pool(...).map(...)` footgun). Clarify Q3 (2026-06-27): value-types are a separate ergonomics concern, not lifetime.

**Alternatives rejected.** Build the value-types too — out-of-mandate scope creep (clarify-rejected).

---

## D-10 — Context-manager teardown + DeprecationWarning on GC-only teardown

**Decision.** `Engine` and `Session` implement `__enter__`/`__exit__` (calling `close()` on exit). Each carries `_was_explicitly_closed: bool = False`; `__del__` is a best-effort backstop that, if reclaimed without a prior explicit close, emits a `DeprecationWarning` ("explicit close / `with` is the supported teardown path") and still attempts native cleanup. `__del__` ordering across modules at interpreter shutdown is not relied upon.

**Rationale.** `[2m §6.2]` "cycle-GC finalisation order is unspecified — explicit close is the supported teardown path"; `with Engine(config) as engine:` is the recommended idiom. v1.x escalates the warning to a hard error (documented, not implemented here).

**Alternatives rejected.** Rely on `__del__` for correctness — CPython gives weak cross-module finaliser-order guarantees → stale-handle calls at shutdown.

---

## D-11 — Construction surface: wrap existing flat construction; no new value-typed config

**Decision.** The OO wrappers are constructed over the existing flat construction path (`engine_create`, `session_open`, `dict_load_from_xml`, `msg_create_outbound`). Because value-typed config classes are out of scope (D-9), `Engine`/`Session` construction accepts the configuration via the existing flat config-builder handle/functions (mirroring 053's working round-trip), wrapped behind the OO `__init__`. A fully Pythonic typed config is deferred with the value-types.

**Rationale.** Keeps PY-004 lifetime-focused and freeze-clean; reuses the proven 053 construction path. The `[2m §4.x]` constructor signatures that assume typed config (`Message(msg_type, session)`, `Engine(config)`) are honored in shape; the config object itself is the existing flat handle until the value-type feature lands.

**Alternatives rejected.** Introduce typed config now — pulls the value-type surface into PY-004 (clarify-rejected).

---

## D-12 — Test & verification strategy

**Decision.** New pytest files (see plan Project Structure) witness each property; the existing 053/054 tests MUST stay green (SC-004, the additive guarantee). Key witnesses: **seams #3/#8** post-close/post-window → `ObjectLifetime` under **ASan** (SC-001, no UAF); **callable leak** via `weakref` dead after `gc.collect()` once the caller's external strong refs are dropped (SC-002, binding released its own ref); **reentrancy** send/close/engine_destroy-from-callback → `CallbackReentrantClose` under a **watchdog/timeout** that would hang on regression (SC-007, no deadlock); **sub-interpreter** → `SubInterpreterRejected`; **pickle** → `TypeError`; **context-manager** teardown order + `DeprecationWarning`. Gate via the Tier-1 `python-bindings` none/asan/tsan matrix (SC-006).

**Rationale.** Mirrors the discriminating-witness discipline of 053/054 (a sentinel-armed test must fail on a mutation that removes the guard). The reentrancy test must run a real parked-callback shape (per memory `feedback_gil_release_witness_must_exercise_each_blocking_wrapper`) — each of the three blocking APIs exercised as the decisive in-callback op, not just one. It MUST additionally include an **`io_threads>1` (e.g. `io_threads=4`) rotating-pool arm** firing the in-callback raise on a callback that resumes on a different OS thread (FR-016 / `[2m §9 seam #4]`, NEW-P2b) — a single-worker witness would green while leaving the multi-worker thread-independence claim unwitnessed. And it MUST include an **`Engine.close()` all-sessions preflight** test: `engine.close()` called from inside one session's callback raises `CallbackReentrantClose` with **no sibling session closed** (C-3 preflight / Codex P1#2). It MUST ALSO include a **concurrent engine-entry** witness in `test_close_flow.py` or `test_lifetime.py`: while `engine.close()` is parked in the native destroy window, a second Python thread attempts `engine.open_session(...)` / `engine.start()` and MUST raise `fixpp.ObjectLifetime` (1202) **without entering the C-ABI** — the discriminating assertion proving the engine-root `_dead` arm now precedes the GIL-releasing child-close + destroy path. It MUST ALSO include a **`Session.close()` step-0 backstop** test: `session.close()` invoked from inside its **own** callback raises `CallbackReentrantClose` (1204) **AND leaves session + callback state unmodified** — `_dead` NOT armed, `_application` NOT dropped, no `fixpp_session_close` reached — the discriminating assertion proving step-0 precedes the NEW-P2a `_dead`-first arming (a "raises 1204" check alone would still pass if the guard ran after `_dead` was armed).

**Carry-forward waivers to re-confirm at /speckit-verify (not new):** UBSan-leg (053 D-9 / L-054-2); the SC-004 GIL-canary CI-automation (053). The `msg_get_string` non-UTF-8 codec witness remains the standing 053 waiver (PY-003 residue, out of scope).

---

## Proposed `[2m]` Article XX amendment — DEFERRED to `/implement` (added 2026-06-27, Gate A round 1)

**Disposition (orchestrator-resolved 2026-06-27, both Gate A reviewers concurring):** the Article XX amendment to `[2m]` (`.specify/2m-pybind.md`) for the send-from-callback inversion is **carried as proposed text here and applied + committed in the implementation PR at `/implement`** — it is **NOT** pre-applied to the working tree at Gate A. This matches the 043/051/054 precedent (each deferred its live `[2m]`/constitution edit to `/implement`) and eliminates the close-out staging hazard: the Spec-Kit close-out commit stages only `git add specs/<id>/`, which would not stage `.specify/2m-pybind.md`, producing a committed-bundle-vs-committed-doc divergence. The earlier round-1 in-place edit to `.specify/2m-pybind.md` was **reverted** (`git checkout -- .specify/2m-pybind.md`); this section is the authoritative source for the `/implement` step. (Plan Constitution-Check XX row corrected accordingly.)

**Complete site set — 6 substantive sites + 1 editorial.** At `/implement`, apply ALL of the following to `.specify/2m-pybind.md`, then `git add` the design doc in the implementation PR:

1. **§1.3 rule (2)** (committed-state ~`:89`, append an amendment block after the rule). Amendment text:
   > **[Article XX amendment — 055-python-lifetime-ownership, PY-004]:** PY-004 (feature 055) **implements** the active detection promised above **and extends it to the send case**. In v1.0 the Python wrapper raises `fixpp.CallbackReentrantClose` (1204) — via the GIL-protected `session._in_callback` marker (§1.3 rule (4)) — on `Session.send` from inside a callback, **not only** on `close()`/`engine_destroy`. As-built, send-from-callback **deadlocks** (L-054-1), so for v1.0 it is treated identically to the close cases (fail-fast, no deadlock) rather than "legal". This **supersedes**, *for v1.0*, the "`Session.send()` … is legal … the Python wrapper does **not** refuse it" claim in rule (2) above and the matching "succeeds" symmetric test in §9 seam #4. Restoring true legality (a non-raising, non-blocking send-from-callback) remains the deferred **engine** non-blocking-send item; when it lands, a later MINOR MAY relax the send raise back to legal (a safe widening: error → works). (Clarify Q1, user-ratified 2026-06-27; Gate A reviews.)

2. **§6.5 carve-out table — `session.send(other_msg)` row** (committed-state ~`:1185`). Append to the rationale cell:
   > **[055/PY-004 Article XX]:** v1.0 PY-004 RAISES `fixpp.CallbackReentrantClose` (1204) on send-from-callback (active `_in_callback` detection), upgrading this row from documentary to fail-fast — see the §1.3 rule (2) Article XX amendment.

3. **§6.5 carve-out table — `engine.close()` / `session.close()` row** (committed-state ~`:1186`). Append to the rationale cell:
   > **[055/PY-004 Article XX]:** PY-004 (feature 055) IMPLEMENTS this active `CallbackReentrantClose` (1204) detection — and applies it to `Session.send` as well (see the §1.3 rule (2) Article XX amendment) — so close/engine_destroy AND send from inside a callback all raise 1204 instead of deadlocking.

4. **§6.7 1204 error-code TABLE row** (`FIXPP_ERR_BINDING_CALLBACK_REENTRANT_CLOSE`, committed-state ~`:1259`). Append to the description cell:
   > **[055/PY-004 Article XX]:** feature 055 IMPLEMENTS the active `_in_callback` pre-call detection that raises this code — and **broadens its trigger to `Session.send` from inside a callback** (not only close/engine_destroy), since the as-built blocking send deadlocks identically (L-054-1). See the §1.3 rule (2) Article XX amendment.

5. **§9 seam #4** (committed-state ~`:1425`). Append to the seam description:
   > **[055/PY-004 Article XX amendment]:** PY-004 (feature 055) implements this seam. For v1.0 the **symmetric `session.send()`-from-callback test is INVERTED**: it now verifies the call **raises `fixpp.CallbackReentrantClose` (1204)** (not "succeeds"), because the as-built blocking send deadlocks from the strand (L-054-1) — see the §1.3 rule (2) Article XX amendment. The `engine.close()`/`session.close()` → 1204 and the sub-interpreter → `SubInterpreterRejected` (1201) assertions are unchanged. The marker-correctness arm MUST include an `EngineConfig(io_threads>1)` (e.g. `io_threads=4`) rotating-pool callback (seam-#4 thread-independence requirement). Tests land in `bindings/python/tests/` (the as-built test dir). (Clarify Q1, user-ratified 2026-06-27.)

6. **§6.7 1204 PROSE docstring** (committed-state `:818-820` — the **6th site**, the residual stale narrative that the round-1 in-place edit missed; it is the `CallbackReentrantClose` class docstring inside the §6.7 exception listing). Current text says "*Session.send from inside fromApp is NOT banned by *this code* … only close-from-callback raises this*". Amendment — append/supersede:
   > **[055/PY-004 Article XX amendment]:** Under PY-004 (feature 055) `Session.send` from inside `fromApp` is **also** detected by the GIL-protected `_in_callback` marker and **RAISES `CallbackReentrantClose` (1204)** before entering the C ABI — it is **no longer** "design-legal / not banned by this code". The prior "*Session.send … is NOT banned by this code … only close-from-callback raises this*" sentence is **superseded for v1.0**: as-built the blocking 050 send deadlocks from the strand (L-054-1), so v1.0 treats send-from-callback identically to close-from-callback (fail-fast 1204). **Code-name note:** the frozen Python class name `CallbackReentrantClose` (1204) is now a slight **misnomer** post-inversion — 1204 is also raised for reentrant *send*, not only a close — but the name is frozen-code-unfixable (it is a published binding code/class) and is retained as-is. Restoring true non-raising legality remains the deferred engine non-blocking-send item (a later MINOR MAY relax it, error → works). See the §1.3 rule (2) / §6.5 amendments.

**Editorial (completeness, not a contradiction) — `Session.send` METHOD docstring** (committed-state `:455-460`, in the §4.x `Session` class API sketch). It merely *omits* the new raise (no "legal" claim). Append a note for completeness at `/implement`:
   > Under PY-004 (feature 055), calling `send()` from inside an inbound callback raises `fixpp.CallbackReentrantClose` (1204) before entering the C ABI (the as-built blocking send deadlocks from the strand, L-054-1); see §6.5 / §1.3 rule (2).

**Census note.** The §6.5 enforcement block (committed-state `:1197-1209`), the §1.3-rule-(2) original (`:89`), and the historical changelog entries are amended-by-append (054 pattern: original retained, superseding block appended) or carry no send-legal claim; no additional (7th) stale site exists (Opus adversarial review, 2026-06-27).
