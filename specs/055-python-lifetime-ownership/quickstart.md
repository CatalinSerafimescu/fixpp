# Quickstart — PY-004 Python OO lifetime/ownership layer (055)

The OO layer makes the binding memory-safe by construction: Python objects can never outlive the native handles they wrap. The flat `fixpp.*` functions still work; the classes below are the recommended surface.

## Deterministic lifetime with a context manager

```python
import fixpp

with fixpp.Engine(config) as engine:          # close() runs deterministically on block exit
    session = engine.open_session(...)
    session.register_application(MyApp())
    engine.start()
    session.send(make_order(session))
# engine.close() has run here: every session closed (its messages invalidated first),
# every callback released, native handles destroyed — in order. Nothing leaks.
```

If you skip the `with` / explicit `close()` and let GC reclaim the engine, you get a `DeprecationWarning` (explicit close is the supported teardown path; v1.x makes it an error).

## No use-after-free — post-close access raises, never crashes

```python
session = engine.open_session(...)
engine.close()
session.send(msg)            # raises fixpp.ObjectLifetime (1202) — NOT a segfault
```

```python
class MyApp(fixpp.Application):
    def fromApp(self, session, msg):
        self._stashed = msg          # DON'T: msg is a dispatch-window flyweight
# ... later, outside the callback:
app._stashed.get_string(35)          # raises fixpp.ObjectLifetime (1202) — closes L-053-1
```

To use an inbound message after the callback, copy out inside the window (e.g. read the fields you need, or `msg.clone()` where available) and hand the copy off.

## No deadlock from inside a callback

```python
class MyApp(fixpp.Application):
    def fromApp(self, session, msg):
        session.send(reply)          # raises fixpp.CallbackReentrantClose (1204) — does NOT deadlock
        session.close()             # likewise raises (1204)
```

All three blocking calls (`send` / `session.close` / `engine.close`) from inside a callback raise `CallbackReentrantClose` instead of hanging the session strand. **Reply pattern**: copy the message out, `queue.put(...)`, and `send()` from another (non-callback) thread.

## Cross-process safety

```python
import pickle
pickle.dumps(engine)        # raises TypeError: ... native handles cannot cross process boundaries
# (the common multiprocessing.Pool(...).map(callback, messages) footgun fails loudly, not as a UAF)
```

## Sub-interpreter rejection

```python
# Constructing an Engine from a PEP 554 sub-interpreter:
fixpp.Engine(config)        # raises fixpp.SubInterpreterRejected (1201). Use the main interpreter.
```

## What's NOT in this feature

- The 6-method `Application` (`onLogon`/`onLogout`/`toAdmin`/`fromAdmin`) and SWIG-director polymorphism — callback-surface completeness, a later feature (needs additive C-ABI; not a freeze concern).
- Value-typed Python config/decimal classes (Pythonic `EngineConfig`, etc.) — a later ergonomics feature.
- Wheel/manylinux packaging — that's PY-005.

## Run the tests

```bash
cd research/G19-fix-fpml-iso20022/library
# Build + run the python-bindings tests (per [const §VII.2]); ASan leg witnesses no-UAF (SC-001).
# Sanitizer presets run ONE AT A TIME; build parallelism -j2 (WSL2 OOM cap).
ctest --preset <python-bindings-asan>   # exact preset name resolved at implement
pytest bindings/python/tests/
```
