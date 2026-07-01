# The tests/wheel/ suite is a deliberate, byte-faithful fork of this in-tree
# suite (same basenames: test_smoke.py, test_close_flow.py, ...) that runs the
# SHIPPED wheel out-of-repo. It is exercised only by the python-wheel-test CI
# job, which copies tests/wheel/ to a scratch dir and points pytest there — it
# is NEVER meant to be collected in place. Without this ignore, the in-tree
# `pytest bindings/python/tests/` (the python-bindings sanitizer matrix) recurses
# into wheel/ and aborts with import-file-mismatch on the duplicate basenames.
# The copied run is unaffected: cp -r copies only wheel/, not this conftest.
collect_ignore_glob = ["wheel/*"]
