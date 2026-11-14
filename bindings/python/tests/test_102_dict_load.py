"""bindings/python/tests/test_102_dict_load.py — 102 contract §6 class (d) (Python results).

102-dict-width-multichar-type, spec FR-007, tasks T021b. Each loader wrapper returns a
dictionary for an input the parent base refused:

- an Orchestra document whose component and group ids pass 65535, shaped as
  tests/support/dict_width_fixtures_102.hpp's header fixture (generator (c)): through
  `fixpp.dict_load_from_xml`, `fixpp_oo.Dictionary.load_xml`, and as the base of
  `fixpp.dict_load_from_xml_with_overlay` with an overlay that only restates its version;
- a QuickFIX XML base and overlay shaped as tests/dictionary/overlay_refusal_test.cpp's
  side-table start-push row (the overlay's early components push a later base component's
  side-table start past 65535), through `fixpp.dict_load_from_xml_with_overlay`.

The documents are built here, into pytest's `tmp_path`. A refusal fails the cell naming the
exception class and its `.code`. Each handle a cell gets is destroyed.

Run: PYTHONPATH=<build>/lib pytest bindings/python/tests/test_102_dict_load.py -v
"""

import pytest

import fixpp
import fixpp_oo

WRAP = 65536

# ── An Orchestra document with ids past 65535 (generator (c)'s shape) ────────────────────────

ORCH_OPEN = ("<fixr:repository xmlns:fixr='http://fixprotocol.io/2020/orchestra/repository' "
             "name='FIX' version='FIX.Latest_EP303'>\n")
ORCH_CLOSE = "</fixr:repository>\n"


def _ofield(fid, name, ftype):
    return f"<fixr:field id='{fid}' name='{name}' type='{ftype}'/>\n"


def _ofref(fid, presence=""):
    p = f" presence='{presence}'" if presence else ""
    return f"<fixr:fieldRef id='{fid}'{p}/>"


def _ocref(cid):
    return f"<fixr:componentRef id='{cid}'/>"


def _ogref(gid):
    return f"<fixr:groupRef id='{gid}'/>"


def _ocomp(cid, name, body):
    return f"<fixr:component id='{cid}' name='{name}'>{body}</fixr:component>\n"


def _ogroup(gid, name, nig, body):
    return (f"<fixr:group id='{gid}' name='{name}'><fixr:numInGroup id='{nig}'/>{body}"
            "</fixr:group>\n")


def orchestra_ids_past_65535():
    hc, hg = 70001, 70002
    hca, hga = hc - WRAP, hg - WRAP
    fields = (_ofield(8, "BeginString", "String") + _ofield(9, "BodyLength", "Length")
              + _ofield(10, "CheckSum", "String") + _ofield(35, "MsgType", "String")
              + _ofield(9201, "HdrT", "String") + _ofield(9202, "NoHdrN", "NumInGroup")
              + _ofield(9203, "HdrM", "String") + _ofield(9204, "HdrT2", "String")
              + _ofield(9205, "NoHdrN2", "NumInGroup") + _ofield(9206, "HdrM2", "String")
              + _ofield(9207, "OrdT", "String"))
    components = (_ocomp(1024, "StandardHeader",
                         _ofref(8, "required") + _ofref(9, "required") + _ofref(35, "required")
                         + _ocref(hc) + _ocref(hca))
                  + _ocomp(hc, "HdrHigh", _ofref(9201) + _ogref(hg))
                  + _ocomp(hca, "HdrAlias", _ofref(9204) + _ogref(hga))
                  + _ocomp(1024 + WRAP, "OrdinaryAt1024", _ofref(9207)))
    groups = (_ogroup(hg, "HgHigh", 9202, _ofref(9203))
              + _ogroup(hga, "HgAlias", 9205, _ofref(9206)))
    message = ("<fixr:message id='1' name='OrdMessage' msgType='F' "
               "category='SingleGeneralOrderHandling'><fixr:structure>"
               + _ofref(35, "required") + _ocref(1024 + WRAP)
               + "</fixr:structure></fixr:message>\n")
    return (ORCH_OPEN + "<fixr:fields>\n" + fields + "</fixr:fields>\n<fixr:components>\n"
            + components + "</fixr:components>\n<fixr:groups>\n" + groups
            + "</fixr:groups>\n<fixr:messages>\n" + message + "</fixr:messages>\n"
            + ORCH_CLOSE)


# ── The side-table start-push documents (overlay_refusal_test.cpp's builders) ────────────────

XML_OPEN = "<fix type='FIX' major='4' minor='4' servicepack='0'>\n"
GROUPS = 700
BIG_FIELDS = 100
PER_GROUP = 1 + BIG_FIELDS


def _field_def(number, name, ftype):
    return f"  <field number='{number}' name='{name}' type='{ftype}' />\n"


def _fref(name):
    return f"   <field name='{name}' required='N' />\n"


def side_table_base():
    msg = "  <message name='SideTable' msgtype='A1' msgcat='app'>\n"
    fields = ""
    for i in range(GROUPS):
        msg += f"   <group name='NoSG{i}' required='N'>\n" + _fref(f"SGF{i}") + "   </group>\n"
        fields += (_field_def(10000 + i, f"NoSG{i}", "NUMINGROUP")
                   + _field_def(11000 + i, f"SGF{i}", "STRING"))
    msg += "  </message>\n"
    return (XML_OPEN + " <header>\n  <field name='BeginString' required='Y' />\n </header>\n"
            " <messages>\n" + msg + " </messages>\n <components>\n"
            "  <component name='E0'>\n" + _fref("BeginString") + "  </component>\n"
            "  <component name='E1'>\n" + _fref("BeginString") + "  </component>\n"
            "  <component name='Late'>\n" + _fref("BeginString")
            + "  </component>\n </components>\n <fields>\n"
            + _field_def(8, "BeginString", "STRING") + fields + " </fields>\n</fix>\n")


def _big_groups(first, count):
    return "".join(f"   <group name='NoSG{i}' required='N'>\n"
                   "    <component name='VenueBig' required='N' />\n   </group>\n"
                   for i in range(first, first + count))


def side_start_push_overlay():
    each = ((WRAP // 2) // PER_GROUP) + 1
    big = "  <component name='VenueBig'>\n" + "".join(
        _fref(f"VenueBigF{i}") for i in range(BIG_FIELDS)) + "  </component>\n"
    fields = "".join(_field_def(20000 + i, f"VenueBigF{i}", "STRING") for i in range(BIG_FIELDS))
    components = ("  <component name='E0'>\n" + _big_groups(0, each) + "  </component>\n"
                  "  <component name='E1'>\n" + _big_groups(each, each) + "  </component>\n")
    return (XML_OPEN + " <components>\n" + components + big + " </components>\n <fields>\n"
            + fields + " </fields>\n</fix>\n")


# ── The cells ────────────────────────────────────────────────────────────────────────────────

def _write(tmp_path, name, text):
    p = tmp_path / name
    p.write_text(text, encoding="utf-8")
    return str(p)


def _loads(call, *args):
    try:
        return call(*args)
    except fixpp.FixppError as e:
        pytest.fail(f"refused: {type(e).__name__} with code {getattr(e, 'code', None)}")


def test_dict_load_from_xml_loads_orchestra_ids_past_65535(tmp_path):
    path = _write(tmp_path, "orch.xml", orchestra_ids_past_65535())
    h = _loads(fixpp.dict_load_from_xml, path)
    assert h is not None
    fixpp.dict_destroy(h)


def test_oo_dictionary_load_xml_loads_orchestra_ids_past_65535(tmp_path):
    path = _write(tmp_path, "orch.xml", orchestra_ids_past_65535())
    d = _loads(fixpp_oo.Dictionary.load_xml, path)
    assert isinstance(d, fixpp_oo.Dictionary)
    d.destroy()


def test_overlay_wrapper_loads_a_side_table_push_past_65535(tmp_path):
    base = _write(tmp_path, "base.xml", side_table_base())
    overlay = _write(tmp_path, "overlay.xml", side_start_push_overlay())
    h = _loads(fixpp.dict_load_from_xml_with_overlay, base, overlay)
    assert h is not None
    fixpp.dict_destroy(h)


def test_overlay_wrapper_loads_an_orchestra_base_with_ids_past_65535(tmp_path):
    base = _write(tmp_path, "base.xml", orchestra_ids_past_65535())
    overlay = _write(tmp_path, "overlay.xml", ORCH_OPEN + ORCH_CLOSE)
    h = _loads(fixpp.dict_load_from_xml_with_overlay, base, overlay)
    assert h is not None
    fixpp.dict_destroy(h)
