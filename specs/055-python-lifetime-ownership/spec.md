# Feature Specification: Python bindings ownership / lifetime layer (PY-004)

**Feature Branch**: `055-python-lifetime-ownership`
**Created**: 2026-06-27
**Status**: Draft
**Input**: User description: PY-004 — build the full `[2m §6.2]` pure-Python object-oriented wrapper layer on top of the flat SWIG substrate shipped by 053/054, so Python objects can never outlive the native sessions / engines / messages they wrap (`[arch §4.12]` / `[2m §3.6]`). User-chosen scope: the full §6.2 OO layer (not a flat-only descope).

## Context & background *(non-normative)*

The Python binding shipped so far is a **flat substrate**: 053 (PY-001) exposes raw module-level functions (`fixpp.session_send(handle, payload)`, `fixpp.session_close(handle)`, …) operating on opaque SWIG pointer handles; 054 (PY-002/PY-003) added GIL discipline and the typed exception hierarchy. The design `[2m §6.2]` always specified a **pure-Python object-oriented layer on top of** that substrate ("Pythonic classes live in a pure-Python layer over the flat SWIG bindings") — that layer has not been built. Today nothing prevents a Python program from holding a handle (or an inbound message view) past the point its native backing is freed; the result is undefined behaviour / use-after-free at the C boundary. **L-053-1** records exactly this for the borrowed inbound-message view (valid only inside the dispatch window, with no active post-window guard).

This feature builds that OO layer in full and makes the binding **memory-safe by construction**: every Python wrapper knows when its native handle is gone and refuses to use it.

This feature also closes the only lifetime-management gap left by 053/054: the registered inbound callback is currently `Py_INCREF`'d and **held until interpreter exit** with no release path — a per-registration leak.

**C-ABI freeze note.** PY-004 is the LAST consumer to exercise the C-ABI before the `0→1` GA freeze. No `include/fix/c_api.h` change is expected — §6.2's guard is a Python-side check performed **before** the C-ABI call, and post-destroy native calls are already tombstoned by 050/052. Any gap PY-004 surfaces is fixed at MINOR (free now; breaking after `MAJOR>=1` per `[const §X.1]`) before the freeze closes. (Confirmed at `/plan`.)

**Article XX checkpoint (design-doc amendment).** The reentrancy decision below (raise on send-from-callback too) **contradicts `[2m]` §9 seam #4 / §1.3 rule (2)**, which currently claim send-from-callback is legal (an aspirational claim predicated on a deferred *engine* non-blocking-send fix that is NOT in v1.0; as-built it deadlocks per L-054-1). This feature therefore amends `[2m]` at the sites enumerated in research.md (§1.3 rule (2), §6.5 carve-out send + close rows, §6.7 1204 table row + prose docstring, §9 seam #4 — rule (4) is the marker mechanism PY-004 *implements*, not amends) to make v1.0 PY-004 raise `CallbackReentrantClose` on all three blocking APIs. The amendment is carried as proposed text and deferred to `/implement` (not pre-applied) — Gate A reviews it (mirrors 054's Article XX checkpoint).

## Clarifications

### Session 2026-06-27

- Q: When a Python callback calls a blocking API (send / close / engine_destroy) on its own engine/session — actively detect-and-raise, or keep documentary? → A: **Detect all three.** Raise `fixpp.CallbackReentrantClose` (numeric 1204) via a GIL-protected `session._in_callback` marker, checked *before* the C-ABI call. As-built, all three deadlock (L-054-1), so every reachable deadlock becomes a clean catchable error. Requires the Article XX amendment above (design seam #4 claimed send-from-callback legal; v1.0 raises pending the deferred engine non-blocking-send fix).
- Q: Reject Engine construction from a CPython sub-interpreter (PEP 554) in v1.0? → A: **Yes — in scope.** Engine construction detects a non-main interpreter and raises `fixpp.SubInterpreterRejected` (numeric 1201). Aligns with `[2m]` §9 seam #4.
- Q: Does PY-004 build the value-typed Python classes (Decimal / MsgVersion / EngineConfig / SessionConfig / LogConfig) that §6.2's pickle-ban contrasts against the handle wrappers? → A: **No — handle-bearing wrappers only.** The pickle-ban applies to `Engine` / `Session` / `Message` / `Application` / `Dictionary`. Value-typed config/decimal Python classes are a separate ergonomics concern (not lifetime); FR-014's "value-types stay pickleable" leg (and the `[2m]` §9 seam #3 value-pickle assertion) is deferred until those types are introduced by a later feature.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - No use-after-free reachable from Python (Priority: P1)

A Python developer uses the binding's object API (`Engine`, `Session`, `Message`) and, by accident or by design, keeps a reference to a wrapper object after the native resource behind it has been torn down — for example, stashing an inbound `Message` and reading a field after the callback returns, or calling `session.send(...)` after the owning engine was closed. Instead of corrupting the interpreter, every such access raises a clear, catchable `fixpp.ObjectLifetime` error and the process stays alive.

**Why this priority**: This is the safety mandate the feature exists for (`[2m §6.2]` / `[arch §4.12]`). Without it the binding is unsafe for any non-trivial program. It is the MVP: shipping only this story already eliminates the UAF class and closes L-053-1. The Pythonic wrapper classes are introduced here because the safety mechanism (the per-object liveness sentinel) lives on them.

**Independent Test**: Drive the §9 acceptance seams under AddressSanitizer: (#8) keep an inbound `Message` past the dispatch window / past `session.close()` and read a field; (#3) keep a `Session` past `engine.close()` and call `send`. Verify each raises `fixpp.ObjectLifetime` with zero ASan findings and no crash.

**Acceptance Scenarios**:

1. **Given** an inbound `Message` delivered to a callback, **When** the callback returns and code later calls an accessor (e.g. `msg.get_string(tag)`) on that stashed `Message`, **Then** the call raises `fixpp.ObjectLifetime` (numeric 1202) and does not dereference freed memory. *(closes L-053-1)*
2. **Given** a `Session` derived from an `Engine`, **When** `engine.close()` has run and code then calls `session.send(...)` (or any session accessor), **Then** the call raises `fixpp.ObjectLifetime` before entering the C ABI.
3. **Given** a `Message` derived from an inbound dispatch on a `Session`, **When** that `Session` is closed and code then reads the `Message`, **Then** the read raises `fixpp.ObjectLifetime`.
4. **Given** any wrapper whose native handle is still live, **When** it is accessed normally, **Then** it behaves exactly as the underlying flat substrate does (the liveness check is transparent in the happy path).
5. **Given** the existing flat-substrate tests (053 round-trip, 054 GIL / exceptions), **When** this layer is added, **Then** they continue to pass unchanged (the OO layer is additive over the flat functions).

---

### User Story 2 - Deterministic teardown and no callback leak (Priority: P2)

A Python developer manages engine/session lifetime with a context manager (`with Engine(config) as engine:`) and trusts that closing the engine deterministically closes its sessions and releases every native and Python resource it owns — including the registered application/callback object, which today leaks. If they forget to close explicitly and rely on garbage collection, they get a `DeprecationWarning` rather than silent best-effort teardown.

**Why this priority**: Deterministic, ordered teardown is what makes the P1 liveness guarantee reachable in ordinary code, and it fixes the standing callback-refcount leak. It is P2 because the raw safety property (P1) holds even without the ergonomic context-manager sugar.

**Independent Test**: Register a callback whose Python object is tracked by a `weakref`; **`del` the caller's own external strong reference to it** (so the binding's `_application` ref is the only surviving one); close the session (or re-register a different callback); after `gc.collect()` verify the original callback's weakref is dead — proving the **binding released its own reference** (not relying on global GC; a retained external ref would keep the weakref alive for a non-leak reason, making the witness non-discriminating). Separately, open an engine + session inside a `with` block and verify `close()` ran on exit in the documented order; and verify that GC-only teardown without an explicit close emits a `DeprecationWarning`.

**Acceptance Scenarios**:

1. **Given** a session with a registered callback whose Python object is weak-referenced **and whose caller-held external strong references have been dropped (so the binding's `_application` ref is the only one left)**, **When** the session is closed, **Then** after `gc.collect()` the callback object is released (weakref dead) — the binding dropped its own reference, no hold-until-interpreter-exit leak.
2. **Given** a session with a registered callback, **When** a different callback is registered in its place, **Then** the previously registered callback is released.
3. **Given** `with Engine(config) as engine:` that opens sessions, **When** the block exits, **Then** `engine.close()` runs deterministically, closing each session (marking its derived messages dead before the native close) and destroying the engine handle last.
4. **Given** an `Engine`/`Session` that is never explicitly closed and is reclaimed by garbage collection, **When** finalisation runs, **Then** a `DeprecationWarning` is emitted indicating explicit close is the supported teardown path. *(best-effort teardown still attempted)*

---

### User Story 3 - Handles cannot silently cross process boundaries (Priority: P3)

A Python developer (often unintentionally, e.g. via `multiprocessing.Pool(...).map(...)`) tries to pickle an object that wraps a native handle. Rather than pickling a meaningless pointer integer that would dereference freed/foreign memory when unpickled, the attempt fails immediately with a clear `TypeError`. Value-typed objects with no native handle remain pickleable.

**Why this priority**: Prevents a real cross-process UAF footgun, but it is a narrower, lower-frequency failure mode than P1/P2 and is independently shippable.

**Independent Test**: `pickle.dumps(engine)` / `pickle.dumps(session)` / `pickle.dumps(message)` each raise `TypeError` with a message explaining native handles cannot cross process boundaries. *(Value-typed round-trip is out of scope — PY-004 introduces no value-types per clarify Q3 / FR-014; deferred to the later value-type feature.)*

**Acceptance Scenarios**:

1. **Given** a handle-bearing wrapper (`Engine`, `Session`, `Message`, `Application`, `Dictionary`), **When** it is pickled, **Then** a `TypeError` is raised with the documented "not pickleable; native handles cannot cross process boundaries" message.
2. **Given** a value-typed object that holds no native handle, **When** it is pickled, **Then** it serialises and round-trips successfully. *(scoped to value-types that exist in the binding; see Assumptions)*

---

### Edge Cases

- **Double close**: `engine.close()` / `session.close()` called twice (or a context-manager exit after an explicit close) is idempotent — the second close is a no-op, not an error or a double-free.
- **Blocking call from inside a callback**: calling `session.send` / `session.close` / `engine.close` (→ `engine_destroy`) from inside an inbound callback. As-built, all three deadlock on the session strand (L-054-1). v1.0 PY-004 actively detects this (a GIL-protected `session._in_callback` marker) and raises `fixpp.CallbackReentrantClose` (numeric 1204) *before* entering the C-ABI — converting every deadlock into a clean, catchable error. *(Resolved 2026-06-27; see Clarifications + the Article XX checkpoint — this raises on send too, amending design seam #4.)*
- **Sub-interpreter construction**: constructing an `Engine` from a CPython sub-interpreter (PEP 554). v1.0 PY-004 detects a non-main interpreter at construction and raises `fixpp.SubInterpreterRejected` (numeric 1201). *(Resolved 2026-06-27; aligns with `[2m]` §9 seam #4.)*
- **Callback resumes on a different OS thread**: with multiple engine worker threads, the GIL-protected liveness/ownership state must remain correct regardless of which worker thread runs the callback (state is GIL-protected, not OS-thread-keyed).
- **Outbound vs inbound message ownership**: an outbound/clone `Message` is Python-owned and frees its native handle on finalisation; an inbound flyweight `Message` is engine-owned and must NOT free it. Mixing them up must not double-free or leak.
- **Application↔Session reference cycle**: the user-supplied application strong-refs its session and the session strong-refs the application — the cycle must be collectable (broken on explicit close; otherwise left to the cycle collector).

## Requirements *(mandatory)*

### Functional Requirements

**Wrapper layer & liveness sentinel (US1)**

- **FR-001**: The binding MUST provide pure-Python object wrappers — `Engine`, `Session`, `Message`, `Application`, `Dictionary` — layered over the existing flat SWIG functions, per `[2m §6.2]`. The flat substrate MUST remain available and unchanged in behaviour (the OO layer is additive).
- **FR-002**: Each handle-bearing wrapper MUST carry a private native-handle reference and a private liveness flag, and MUST check liveness **before** making any C-ABI call (`[2m §6.2]` sentinel pattern).
- **FR-003**: Any accessor invoked on a wrapper whose native handle has been invalidated MUST raise `fixpp.ObjectLifetime` (numeric 1202, a `BindingError` subclass already defined by PY-003 / `[2m §6.7]` row 3) and MUST NOT dereference the freed handle. No use-after-free path may be reachable from Python.
- **FR-004**: An inbound flyweight `Message` (engine-owned, constructed for a callback) MUST be marked invalid when the callback returns (before the dispatch window closes), so a stored reference read afterward raises `fixpp.ObjectLifetime` rather than aliasing freed wire memory. *(closes L-053-1; `[2m §6.2]` Flavour 1 / §6.4 dispatch sequence)*

**Ownership graph & teardown (US2)**

- **FR-005**: The wrappers MUST maintain the `[2m §6.2]` strong-reference graph so a child cannot be finalised before its parent: a `Message` keeps its parent `Session` alive; a `Session` keeps its parent `Engine` alive.
- **FR-006**: Each `Session` MUST track its derived `Message` wrappers, and each `Engine` MUST track its `Session` wrappers, via weak references (no strong child→noticing cycle), so the parent can invalidate all live children on close.
- **FR-007**: `Engine.close()` and `Session.close()` MUST execute the `[2m §6.2]` ordered close sequence: invalidate derived child wrappers FIRST, release the owned application reference, THEN call the native close/destroy; the engine destroys its native handle only after all its sessions are closed.
- **FR-008**: `close()` MUST be idempotent — a second close (including a context-manager exit after an explicit close) is a no-op, never a double-free or error.
- **FR-009**: `Engine` and `Session` MUST be usable as context managers, invoking `close()` deterministically on block exit (`with Engine(config) as engine:`).
- **FR-010**: If a handle-bearing wrapper is reclaimed by garbage collection without a prior explicit close, the binding MUST emit a `DeprecationWarning` stating explicit close is the supported teardown path, and MUST still attempt best-effort native cleanup. (Finalisation order within a GC cycle is unspecified; explicit close is the only guaranteed-correct path.)
- **FR-011**: A registered inbound callback / application object MUST be owned by its `Session` and released (its reference dropped) when the session is closed OR when a different callback is registered in its place — replacing the current hold-until-interpreter-exit retention. **No binding-owned reference to the callback may outlive its session** — i.e. the binding retains no reference to the callback after close (the user remains free to hold their own external references; the guarantee is over the binding's reference, not global object lifetime).
- **FR-012**: An outbound/clone `Message` MUST own and free its native handle on finalisation; an inbound flyweight `Message` MUST NOT free its (engine-owned) handle. The two flavours MUST be distinguished so neither double-frees nor leaks.

**Cross-process safety (US3)**

- **FR-013**: Handle-bearing wrappers (`Engine`, `Session`, `Message`, `Application`, `Dictionary`) MUST refuse to be pickled, raising `TypeError` with a message explaining native handles cannot cross process boundaries (`[2m §6.2]` pickleability rule).
- **FR-014**: The pickle-ban (FR-013) MUST be scoped to the handle-bearing wrappers only. PY-004 does NOT introduce value-typed Python classes (Decimal / MsgVersion / EngineConfig / SessionConfig / LogConfig); the "value-types stay pickleable" leg of `[2m §6.2]` (and the `[2m]` §9 seam #3 value-pickle assertion) is **deferred** until such value-types are introduced by a later feature. *(Resolved 2026-06-27 — see Clarifications.)*

**Boundaries & invariants**

- **FR-015**: This feature MUST NOT change the C-ABI surface (`include/fix/c_api.h`); the `0→1` freeze stays held. The liveness guard is Python-side and relies on the existing 050/052 native-handle tombstones for defence in depth.
- **FR-016**: The liveness / ownership state MUST be correct regardless of which engine worker thread runs a callback — it is protected by the GIL, not keyed to an OS thread id (`[2m §1.3]` director rule 4).
- **FR-017**: The binding MUST actively detect a blocking API call (`session.send` / `session.close` / `engine.close`/destroy) made from inside an inbound callback and raise `fixpp.CallbackReentrantClose` (numeric 1204) BEFORE entering the C-ABI, using a GIL-protected per-`Session` `_in_callback` marker set on callback entry and cleared on exit (`[2m §1.3]` rule (4)). The marker MUST be correct regardless of which worker thread runs the callback (GIL-protected, not OS-thread-keyed). **Trampoline exit discipline** (applies jointly to this requirement and FR-004): On callback return the new OO trampoline MUST perform BOTH (a) arm the inbound `Message._dead` sentinel (FR-004) AND (b) clear `Session._in_callback`, unconditionally before the single GIL release — including on the `PyErr_Print` exception-exit path. The trampoline MUST NOT take an early return on a raising callback that skips either postcondition: skipping (a) is the L-053-1 UAF (a stashed inbound `Message` read after a raising callback dereferences freed wire memory); skipping (b) causes a post-exception legitimate blocking call to falsely raise `CallbackReentrantClose` (1204). (`[2m §6.4]` steps 3f–3h establish the ordering; the key requirement is that neither postcondition may be bypassed by an early return.) *(All three deadlock as-built per L-054-1; this converts each into a clean error. Amends `[2m]` §9 seam #4 / §1.3 rule (2) via the Article XX checkpoint — send-from-callback was claimed legal.)*
- **FR-018**: The binding MUST detect `Engine` construction from a non-main CPython interpreter (PEP 554 sub-interpreter) and raise `fixpp.SubInterpreterRejected` (numeric 1201) at construction (`[2m]` §9 seam #4).

### Key Entities *(include if feature involves data)*

- **Engine (wrapper)**: Owns the native engine handle; root of the ownership graph; tracks its sessions weakly; destroys the native handle last in the close sequence.
- **Session (wrapper)**: Native handle is engine-owned; the wrapper drives `close()`; strong-refs its parent `Engine`; tracks its derived messages weakly; owns the registered application/callback reference.
- **Message (wrapper)**: Two flavours — inbound flyweight (engine-owned, invalidated at callback return) and outbound/clone (Python-owned, frees its handle on finalisation); strong-refs its parent `Session`.
- **Application (wrapper)**: User-supplied callback target; owned by the session; released on session close; participates in the (collectable) application↔session cycle.
- **Dictionary (wrapper)**: Owns a native dictionary handle; a root of the graph.
- **Liveness sentinel**: Per-wrapper state (native-handle reference + liveness flag) checked before every C-ABI call; flipped dead by the owning parent's close sequence or, for inbound messages, at callback return.
- **In-callback marker**: A GIL-protected per-`Session` flag set on inbound-callback entry and cleared on exit; the blocking APIs check it to detect reentrant calls (`[2m §1.3]` rule (4)).
- **`fixpp.ObjectLifetime`**: The typed exception (numeric 1202) raised on any access to a dead wrapper (already defined by PY-003 / `[2m §6.7]` row 3).
- **`fixpp.CallbackReentrantClose`** (numeric 1204) and **`fixpp.SubInterpreterRejected`** (numeric 1201): typed exceptions already defined by PY-003 (054) in the `[1200,1299]` binding block; this feature is the first to *raise* them. Raising them mints no new error code (the `0→1` freeze is unaffected).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Zero use-after-free / segfault is reachable from Python for any post-close or post-dispatch-window access. In the single-threaded / happens-after-close-completes case, **100%** of such accesses raise `fixpp.ObjectLifetime`, witnessed by the §9 seam tests (#3 session-outlives-engine, #8 message-outlives-session) running under AddressSanitizer with **zero** ASan findings. *(Concurrent-close edge: both wrappers now arm their own `_dead` FIRST before the GIL-releasing native teardown (`Session.close()` before `fixpp_session_close`; `Engine.close()` before the child-close walk + `fixpp_engine_destroy`), so a concurrent `session.send()` or `engine.open_session(...)` / `engine.start()` racing that window is expected to raise `fixpp.ObjectLifetime` from the early wrapper-side check rather than enter the C-ABI. If a native tombstone fallback is still hit in a residual close-race, that note applies symmetrically to both wrappers; in all cases the concurrent window is **never** a UAF. The "100%-`ObjectLifetime`" guarantee is scoped to the non-concurrent-close case, with the close-race safety characterization stated symmetrically for session and engine wrappers.)*
- **SC-002**: The binding releases its own reference to a registered callback within the close of its session and on re-registration — witnessed, **after the caller's external strong references are dropped**, by the callback's `weakref` becoming dead after `gc.collect()` (the binding holds no surviving ref). I.e. zero binding-owned callback references leak for the interpreter lifetime. *(The witness must drop the external strong ref first; otherwise a retained user reference keeps the weakref alive for a non-leak reason. The guarantee is over the binding's reference, not global GC.)*
- **SC-003**: 100% of pickle attempts on handle-bearing wrappers (`Engine` / `Session` / `Message` / `Application` / `Dictionary`) raise `TypeError`; any existing non-handle values, if present, are not regressed. *(Value-typed round-trip is out of scope — PY-004 introduces no value-typed Python classes per clarify Q3 / FR-014; value-type pickle round-trip is deferred to the later value-type feature.)*
- **SC-004**: All pre-existing flat-substrate tests (053 round-trip, 054 GIL-release canary / exception coverage / watchdog) pass unchanged after the OO layer is added.
- **SC-005**: The C-ABI surface (`include/fix/c_api.h`) is byte-unchanged by this feature; the `0→1` freeze remains held.
- **SC-006**: The Tier-1 `python-bindings` CI matrix (none / asan / tsan legs) is green with the new lifetime tests included.
- **SC-007**: 100% of blocking-API calls (send / close / engine_destroy) made from inside an inbound callback raise `fixpp.CallbackReentrantClose` (1204) instead of deadlocking — verified by a watchdog/timeout test that would hang on a regression; and `Engine` construction from a sub-interpreter raises `fixpp.SubInterpreterRejected` (1201).

## Assumptions

- **Pure-Python OO layer over the flat substrate**: the wrappers are implemented in Python on top of the existing flat SWIG functions (per the 053 decision record and `[2m §6.2]`), not by re-generating SWIG over C++. The flat functions remain the substrate and keep working.
- **No new C-ABI**: the liveness guard is achievable Python-side (a pre-call check + the existing 050/052 native tombstones); confirmed at `/plan`. If `/plan` discovers a genuine need for native support, it is added at MINOR before the freeze.
- **`ObjectLifetime` already exists**: numeric 1202 and its Python class were introduced by PY-003 (054); this feature raises it, it does not mint a new error code (the `0→1` freeze is not affected).
- **Value-types are out of scope (clarified 2026-06-27)**: PY-004 builds only the handle-bearing wrappers. The value-typed config/decimal Python classes do not exist in the as-built flat binding and are NOT introduced here; FR-014's value-type-pickleability leg is deferred with them.
- **Binding-side error codes already exist**: `CallbackReentrantClose` (1204), `SubInterpreterRejected` (1201), and `ObjectLifetime` (1202) are already defined by PY-003 (054) as Python classes in the `[1200,1299]` binding block (`fixpp.i`); these are binding-internal codes raised Python-side (never returned across the C-ABI). This feature is the first to raise them — no new error code, `0→1` freeze unaffected.
- **Tests** live in `bindings/python/tests/` (pytest, `[const §VII.2]`); the binding gate is the Tier-1 `python-bindings` matrix (none / asan / tsan).
- **Out of scope**: the `msg_get_string` non-UTF-8 argout codec witness — that is PY-003 codec residue, not lifetime, and remains the standing 053 waiver. The wheel/packaging work is PY-005, a separate follow-on feature.
- **Design amendment required (Article XX)**: the reentrancy decision (raise on send-from-callback too) amends `[2m]` §9 seam #4 / §1.3 rule (2); Gate A reviews it.
