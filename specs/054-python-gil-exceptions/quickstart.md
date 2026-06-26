# Quickstart — 054 typed exceptions + GIL discipline

Builds on the PY-001 round-trip. Assumes the in-tree `-DFIXPP_BUILD_PYTHON=ON` extension is importable.

## Typed exceptions (PY-003)

```python
import fixpp

# Catch a specific category by type, recover the exact code.
try:
    fixpp.dict_load_from_xml("/nope/missing.xml")
except fixpp.ValidatorError as e:        # dict block [200,299]
    print(e.code, e.name, e.message)     # e.g. 200 FIXPP_ERR_DICT_CONFIG "<strerror text>"
except fixpp.FixppError:                 # any fixpp error (root)
    pass

# The shipped 053 alias still works:
assert fixpp.Error is fixpp.FixppError
try:
    fixpp.session_config_set_sender(cfg, "has\x00nul")   # in-typemap conversion failure
except fixpp.FixppError as e:            # root (not a built-in TypeError)
    print(e)

# Introspect the mapping (single source of truth):
assert fixpp.exception_for_code(701) is fixpp.TransportError
assert issubclass(fixpp.TransportError, fixpp.FixppError)

# Forward-compat fallback (synthetic unmapped-block code):
assert fixpp.exception_for_code(99999) is fixpp.FixppError
```

## Exact-mapping coverage (the drift guard)

The `FIXPP_ERR_*` constants are NOT exposed as Python attributes, so the test parses `error.h` (the independent source) and pins the count so it can't pass vacuously:

```python
import re, pathlib
codes = {int(v): n for n, v in re.findall(
    r'#define\s+(FIXPP_ERR_\w+)\s+\(\(fixpp_error_t\)(\d+)\)',
    pathlib.Path("include/fix/c_api/error.h").read_text())
    if n != "FIXPP_ERR_OK"}
assert len(codes) == 47, f"expected 47 raisable codes, parsed {len(codes)}"   # non-vacuous
for code, name in codes.items():
    cls = fixpp.exception_for_code(code)
    assert issubclass(cls, fixpp.FixppError)
    assert cls is not fixpp.FixppError, f"{name} ({code}) hit the fallback — unmapped block"
assert set(fixpp._CODE_TO_NAME) == set(codes)   # maintained name dict matches the header
# RED until AppError covers [1400,1499].
```

## GIL discipline (PY-002)

The three blocking wrappers (`session_send`, `session_close`, `engine_destroy`) release the GIL; the inbound callback reacquires it. So a teardown on the main thread and a callback firing on the worker do not deadlock:

```python
# (loopback pair established, recv callback registered — see test_roundtrip.py)
# An inbound message is in flight; the worker needs the GIL for the callback.
fixpp.engine_destroy(engine)   # blocks, but releases the GIL → worker drains → returns
# Without the GIL release (the FIXPP_PY_GIL_RELEASE_CANARY build) this DEADLOCKS.
```

**Do NOT** call a blocking API from inside the callback:

```python
def on_msg(msg):
    val = fixpp.msg_get_string(msg, 58)   # OK: read in-window
    # fixpp.session_send(sess, frame)     # DEADLOCK (L-054-1) — copy out, send from another thread
```

## Running the gates

```bash
# Tier-1 matrix suite (typed exceptions + watchdog), green under none/asan/tsan:
ctest --preset linux-clang-asan-py -R python-bindings   # (and -tsan-py, -debug-py)

# The discriminating GIL-release canary (LOCAL ONLY — deliberate deadlock):
cmake --preset linux-clang-debug-py -DFIXPP_PY_GIL_RELEASE_CANARY=ON
# build, then: pytest bindings/python/tests/test_gil_release_canary.py
#   -> RED (subprocess hard-timeout) WITH the canary; GREEN without it.
```
