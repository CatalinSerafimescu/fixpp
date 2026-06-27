"""Pickle-ban witnesses for PY-004 Phase 5 (055 / US3).

Every handle-bearing wrapper (and the Application callback base) must refuse to
pickle with a documented TypeError — a serialized native handle would
deserialize elsewhere as a dangling pointer that UAFs on first touch (SC-003,
FR-013; contracts C-5).
"""

import pickle

import pytest

import fixpp

# Instances are built via __new__ so the test needs no live native handle:
# pickle calls __reduce_ex__ before touching object state, so the ban fires
# first regardless.
WRAPPERS = ["Engine", "Session", "Message", "Application", "Dictionary"]


@pytest.mark.parametrize("name", WRAPPERS)
@pytest.mark.parametrize("protocol", range(pickle.HIGHEST_PROTOCOL + 1))
def test_handle_wrappers_refuse_to_pickle(name, protocol):
    cls = getattr(fixpp, name)
    obj = cls.__new__(cls)
    with pytest.raises(TypeError) as ei:
        pickle.dumps(obj, protocol=protocol)
    msg = str(ei.value)
    assert ("fixpp.%s" % name) in msg
    assert "not pickleable" in msg
    assert "native handles cannot cross process boundaries" in msg
