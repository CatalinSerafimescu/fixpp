# Feature Specification: Thin End-to-End Python Binding (PY-001)

**Feature Branch**: `053-python-thin-binding`
**Created**: 2026-06-26
**Status**: Draft
**Input**: User description: "Thin end-to-end Python binding over the C-ABI (PY-001) — the first real C-ABI consumer and the 0→1 ABI freeze validator. A minimal SWIG-generated CPython binding that drives the full happy-path C-ABI flow in-process (load dictionary → engine → acceptor+initiator session → send → receive-in-callback → read field) on Linux, statically linked, built in-tree via the Tier-1 python-bindings job. GIL discipline (PY-002), typed exception hierarchy (PY-003), ownership/lifetime guards (PY-004), and wheel packaging (PY-005) are explicitly deferred."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Round-trip a FIX message from Python over a loopback session (Priority: P1)

A Python application developer, using only the installed `fixpp` Python module (no C/C++ toolchain, no manual linking, no separate fixpp shared library), can write a single Python script that: loads a FIX dictionary from a bundled XML file, starts an engine, brings up two sessions (one acceptor, one initiator) over a loopback TCP connection, sends a FIX application message from the initiator, and receives that message in a registered Python callback where they read a field value off it.

**Why this priority**: This is the entire feature. It is the minimal vertical slice that proves the C-ABI is usable end-to-end from Python, and it is the first real consumer that validates the ABI before the `0→1` freeze. Any C-ABI ergonomic or correctness gap a pure-Python consumer would hit surfaces here, while it is still free to fix at an additive MINOR.

**Independent Test**: Run a pytest that performs the full in-process loopback round-trip and asserts the field value received in the callback equals the value sent. Delivers a green Tier-1 `python-bindings` gate that exercises the real flow instead of a version-string smoke test.

**Acceptance Scenarios**:

1. **Given** the `fixpp` module is importable with no toolchain present, **When** a developer loads a bundled dictionary XML by filesystem path, **Then** a dictionary object is returned and no error is raised.
2. **Given** an engine and a loaded dictionary, **When** the developer configures an acceptor session bound to a loopback ephemeral port, reads back the bound port, configures an initiator session pointed at that port, and opens both, **Then** both sessions reach the established state within a bounded timeout.
3. **Given** two established sessions, **When** the initiator constructs and sends a FIX application message, **Then** the acceptor's registered Python callback is invoked with that message and the developer can read the asserted field's value from it, equal to what was sent.
4. **Given** a C-ABI call returns a non-OK status (e.g., a bad dictionary path), **When** that surfaces to Python, **Then** the developer observes a clear error (a raised `fixpp.Error` carrying the strerror text, or a documented non-OK return) rather than a silent failure.
5. **Given** the callback is delivered from an internal engine worker thread, **When** the Python callback executes interpreter code, **Then** the interpreter is not corrupted and the test neither crashes nor deadlocks.

### Edge Cases

- Dictionary XML path missing or malformed → the load surfaces an observable error to Python; no crash.
- Initiator cannot connect (acceptor not yet bound, or wrong port) → the session does not reach established within the timeout; the test observes the non-established state without crashing.
- The Python callback itself raises an exception → [thin scope] it MUST NOT corrupt the interpreter or the engine worker; minimal trampoline handling (catch/log, do not propagate into the worker) is acceptable for PY-001. Defining the full propagation policy is PY-003.
- Objects destroyed out of order (e.g., engine destroyed while a session handle is still referenced from Python) → out of scope for hardening (PY-004); the P1 test uses explicit, correct destroy ordering.

## Clarifications

### Session 2026-06-26

- Q: Which FIX dictionary should the PY-001 loopback round-trip load and establish over? → A: FIX 4.4 (`FIX44.xml`) for PY-001 — classic single dictionary, no `1137` negotiation. FIXT.1.1 + FIX 5.0 SP2 (the modern transport/app split) is a planned follow-on slice, not this feature.
- Q: How much of the C-ABI message surface should the round-trip exercise? → A: Scalar field only — send one application message and assert one scalar field. Repeating-group accessors (shipped + Gate-B-hardened in 051) are validated by a later PY slice, not PY-001.
- Q: How should SC-002's "≥50 consecutive runs, no flake" determinism target be verified? → A: The Tier-1 `python-bindings` job runs the pytest once as the merge gate; the ≥50× no-flake determinism is a local pre-PR stress expectation (CI is not looped).

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The `fixpp` Python module MUST expose loading a FIX dictionary from a filesystem path to a bundled dictionary XML, returning an opaque dictionary object, and releasing it.
- **FR-002**: The module MUST expose creating and destroying an engine that owns its own internal I/O execution (there is no consumer-supplied event loop).
- **FR-003**: The module MUST allow configuring a session's peer TCP endpoint and, for an acceptor, reading back the bound (possibly ephemeral) endpoint so an initiator can target it.
- **FR-004**: The module MUST allow opening and closing acceptor and initiator sessions and querying whether a session is established.
- **FR-004a**: The module MUST expose the establishment knobs required for a fresh pair to log on, and the round-trip MUST set them. **Three mirror the gold-reference loopback** `make_session_cfg` (`capi_loopback_support.hpp`, which itself uses **FIX 4.2**): an **explicit security profile** (`set_security`, `INSECURE_PLAIN_TCP` for the loopback — without it the `unset` sentinel is rejected at `Session::open()` per constitution Article XII §5 and the session CANNOT establish), **per-role `reset_on_logon`** (`set_reset_on_logon`, true for the initiator / false for the acceptor), and the **heartbeat interval** (`set_heartbeat_seconds`). The round-trip **adds a fourth knob the gold-reference does not set** — the **reset-seqnum policy** (`set_reset_seqnum_policy`, `BILATERAL_LENIENT` — accepts the one-sided 141=Y the per-role `reset_on_logon` asymmetry produces) — which the FIX 4.4 pairing needs. These call already-shipped 052 setters (no C-ABI change).
- **FR-005**: The module MUST allow constructing a FIX application message and sending it from Python over an established session.
- **FR-006**: The module MUST allow registering a Python callable as a session's inbound-message callback; for an inbound application message the callback MUST be invoked with an object from which at least one field value can be read.
- **FR-007**: The inbound callback is invoked from an internal engine worker thread; the binding MUST correctly reacquire the Python GIL for that trampoline so executing Python code in the callback does not corrupt the interpreter. (Only this one trampoline; comprehensive GIL discipline — release around blocking calls, all other trampolines, witnessed failure modes — is deferred to PY-002.)
- **FR-008**: A non-OK C-ABI status surfaced to Python MUST produce an observable error — a raised generic `fixpp.Error` carrying the `fixpp_strerror` text, or a documented non-OK return — never a silent swallow. (The typed exception hierarchy is deferred to PY-003.)
- **FR-009**: The module MUST expose the library version string.
- **FR-010**: The Python extension MUST statically link the C-ABI static library (and the C++ standard library on Linux) so the built extension imports and runs without a C/C++ toolchain, Conan, or a separate fixpp shared library present at runtime.
- **FR-011**: A pytest end-to-end test under `bindings/python/tests/` MUST perform the full loopback round-trip (FR-001..006) and assert the received field equals the sent value, and MUST replace the current import+version smoke test as the Tier-1 `python-bindings` gate.
- **FR-012**: This feature MUST NOT require a C-ABI source change (the C-ABI surface is complete as of the 052 slice). If implementation surfaces a C-ABI gap, it MUST be closed by an additive (MINOR) C-ABI change before this feature merges, preserving the held `0→1` freeze.
- **FR-013**: The binding MUST `Py_INCREF` the Python callable registered as a session's inbound callback on register and keep it alive for as long as the native session can invoke it; for PY-001's single-callback test the callable is held until interpreter exit. Releasing it on reregister / deregistration / engine teardown — which requires a session-keyed registry — is deferred to PY-004. (Minimal lifetime guarantee the callback path forces; the comprehensive ownership/lifetime guards are deferred to PY-004.)
- **FR-013a**: The Python callback MUST NOT make a blocking C-ABI call (`session_send` / `session_close`) from inside the inbound trampoline — doing so risks deadlock against the engine worker (as-built 050, `session.h:256-260`). PY-001's guarantee is **documentary**: the binding states this in the callback docstring (T017); active detection/enforcement is deferred. This supersedes `[2m §6.5]`'s "send-from-`fromApp` is legal" note (the formal `[2m]` amendment is PY-002).
- **FR-014**: The inbound message object handed to the Python callback is valid ONLY for the duration of that callback invocation (the native dispatch window); the binding MUST expose it as a **non-owning view** and the callback MUST read its field(s) within the call. The thin guarantee is **read-within-the-window**: the binding does NOT add an active post-window invalidation guard — that requires the SWIG director + a `fixpp.Message` wrapper class, both of which 053 explicitly defers to PY-004 (a raw `own=0` SWIG proxy cannot carry a `_dead` flag or intercept post-window access). A Python reference to the view stored past the callback and dereferenced later is a use-after-free; that escape is a **documented limitation (L-053-1)**, not a guard 053 ships. SC-004 (the ASan round-trip) is the witness that the in-scope **test** path reads in-window and does not escape. (Escape-via-clone and the active lifetime guard are PY-004.)

### Key Entities *(include if feature involves data)*

- **Dictionary**: an opaque, reference-counted FIX dictionary loaded from XML; required to interpret messages.
- **Engine**: owns the internal I/O execution and the lifetime of the sessions it creates.
- **Session**: an acceptor or initiator FIX session with an establishment state and a peer TCP endpoint.
- **Message**: the outbound message the initiator constructs/sends and the inbound view the callback reads a field from.
- **Error / status**: the C-ABI status surfaced to Python on failure, with `fixpp_strerror` text as the message baseline.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A Python developer can complete the full loopback round-trip (load dict → engine → two sessions established → send → receive-in-callback → read field) from a single Python script using only the installed module, with no C/C++ toolchain or separate fixpp shared library present.
- **SC-002**: The end-to-end pytest passes once in the Tier-1 `python-bindings` job (the merge gate) within a bounded per-run timeout; a local pre-PR stress run of at least 50 consecutive iterations shows no flake (CI is not looped).
- **SC-003**: The field value read in the callback equals the value sent by the initiator (a correct round-trip, not merely "a message arrived").
- **SC-004**: Executing the Python callback from the engine worker thread never corrupts the interpreter — the test neither crashes nor deadlocks across the SC-002 repeated runs. Because the GIL trampoline + callable-lifetime + borrowed-message path is the riskiest surface in this slice, the round-trip MUST pass under an AddressSanitizer build of the extension **and that sanitized run MUST be wired into the Tier-1 `python-bindings` CI job** (not local-only), satisfying constitution Article IX §2 for the binding's trampoline. A ThreadSanitizer leg over the GIL-trampoline worker path is added alongside ASan (with a CPython suppressions file as needed).
- **SC-005**: Every non-OK C-ABI status the test exercises (e.g., a bad dictionary path) produces an observable Python-level error, with zero silent failures.

## Assumptions

- The "user" is a Python application developer consuming fixpp; this binding is a thin, flat layer over the C-ABI. Pythonic ergonomics (classes, context managers) are a later, separate concern, not this feature.
- The round-trip uses the FIX 4.4 dictionary (`FIX44.xml`, bundled under `dictionaries/`); the test sends a single FIX 4.4 application message and asserts one **scalar** field (exact message type and field chosen at implementation time — e.g., an application message carrying an echoable scalar field). Repeating groups are out of scope for PY-001 (deferred to a later PY slice). FIXT.1.1 + FIX 5.0 SP2 establishment is a planned follow-on, not this feature.
- The loopback test stands up **two C-ABI engines** (one acceptor-role, one initiator-role) over a loopback TCP connection, mirroring the established `capi_loopback_support.hpp` pattern — but driven entirely through the **public 052 surface** (`fixpp_dict_load_from_xml`, `fixpp_session_config_set_tcp_endpoint`, `fixpp_session_acceptor_bound_endpoint`) rather than the internal test-only seams, which is precisely what makes PY-001 the freeze validator.
- The inbound callback delivers inbound application messages (fromApp); admin-message delivery semantics follow the existing C-ABI callback contract (CA-007) and are not extended here.
- A Python callback that itself raises is handled minimally at the trampoline (must not corrupt the interpreter or worker); defining the propagation policy is PY-003.
- Object lifetime in the P1 test uses explicit, correct destroy ordering; lifetime-guard hardening (preventing use-after-free on out-of-order destroy) is PY-004.
- **L-053-1 (documented limitation)**: the inbound message view is non-owning and valid only inside the callback (FR-014). The binding ships **no active post-window invalidation guard** — storing the Python view past the callback and dereferencing it later is a use-after-free. The active guard (a `_dead` sentinel) needs the SWIG director + `fixpp.Message` wrapper class that 053 defers to PY-004. Witnessed-safe for the in-scope test path by SC-004 (ASan), which reads in-window only.
- Build and test are in-tree via the existing `-DFIXPP_BUILD_PYTHON=ON` Tier-1 `python-bindings` job on Linux x86_64; pip/wheel packaging, abi3 (`Py_LIMITED_API`), macOS, and Windows wheels are PY-005 / deferred.
- CPython 3.12 is the reference interpreter (matches the Tier-1 job's `setup-python`).
- SWIG is the binding generator (constitution §IV.3 mandates SWIG-over-C-ABI); the binding wraps the C-ABI headers `fix/c_api/{engine,session,message,error,version,handles,dict}.h`, not the C++ API.

## Normative References

Per constitution Article VI §5, the normative entries informing this spec (from the coverage index / catalogue row PY-001):

- `[2m §4.1]` Python module surface (`import fixpp`; flat binding over the C ABI).
- `[2m §4.2–§4.5]` Binding layer — session/message/dictionary wrapping, field accessors.
- `[2m §5]` End-to-end usage flow (engine → session → send → receive → read).
- `[2m §7]` Packaging/build context (the wheel itself is PY-005; PY-001 builds in-tree).
- `[2m §6.1, §6.5]` GIL discipline — referenced as the **deferred** PY-002 boundary (only the single inbound-callback trampoline is in scope here, per FR-007).
- `[2m §6.2, §6.7]` Ownership/lifetime — referenced as the **deferred** PY-004 boundary (only the callable-lifetime and within-window message read are in scope here, per FR-013/FR-014).
- `[const §IV.3]` Distribution Model — Python bindings ship via SWIG over the C ABI; Linux x86_64 mandatory.
- `[const §VII.2, §VII.3, §VII.4]` Testing — pytest against the SWIG bindings; TDD mandatory; no code without a test.
- `[const §X.1, §X.5, §X.6]` ABI Policy — the C ABI is a versioned, reentrancy-documented contract; **consumed unchanged** by this feature (no `include/fix/c_api.h` modification; the `0→1` freeze stays held).
- C-ABI contract headers exercised: `fix/c_api/{dict,engine,session,message,version,error}.h` (the 049/050/051/052 surface).

