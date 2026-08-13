#!/usr/bin/env python3
"""082-structural-group-detection — v42 BUILDER-TIER PLAN CENSUS (design-time
derivation aid for research.md D-9a; see also contracts/predicate_census.py).

WHAT THIS IS. It replays `emit_builders`' own `(no_tag, recursive_signature)`
plan-interning rule over a QuickFIX-XML dictionary, so the distinct-plan count
and the exact `groups/<PlanName>.hpp` name set can be DERIVED rather than
approximated -- and, at /implement, so FR-016b's expected `v42` completeness set
can be derived BY CONSTRUCTION instead of transcribed from the first emitter run
(a transcribed set is a corpus built from the read it checks).

WHAT THIS IS NOT. Not a shipped test, and not the FR-018 oracle. It is a
design-time derivation of a count that 082 must predict before the emitter can
produce it (082 is not implemented; `v42` emits zero builders today).

SELF-VALIDATION -- run with `--validate`. The rule is checked against the three
SHIPPED builder tiers and their checked-in artifacts:
  v44    --families all      -> 83 msgs /  88 plans, name-for-name equal to
                                specs/078-.../contracts/golden/v44/groups/
  v50sp2 --families all      -> 156 msgs / 558 plans, likewise exact
  v44    --families official ->  33 msgs /  54 plans, equal to
                                tests/codegen/determinism_test.cpp's
                                kExpectedOfficialGroupPlanCount = 54
A rule that reproduces three shipped artifacts exactly is the basis for the
`v42` prediction. `vlatest` is Orchestra-schema and out of scope here (its IR
comes from `walk_orchestra_level`, a different walker).

DERIVED FOR v42 (research.md D-9a):
  --families all      -> 39 msgs / 28 plans over 17 tags / 226 emitted files
  --families official -> 25 msgs / 19 plans over 11 tags / 147 emitted files
  tag 384 NoMsgTypes yields NO plan: its only host is `Logon` (msgcat='admin'),
  outside emit_builders' is_application scope. So the read tier's 18 `class G_`
  (per TAG, admin included) and the builder tier's 28 plan headers over 17 tags
  are both correct -- different keyings, not a discrepancy.

Mirrors, line-for-line:
  src/dictionary/xml_loader.cpp  resolve_field_type / detect_length_pairs /
                                 expand_field_list / append_run
  tools/codegen/fixpp-codegen/ir.cpp   walk_level / collect_tags /
                                 populate_group_order / collect_top_fields
  tools/codegen/fixpp-codegen/gen_util.hpp   kind_of
  tools/codegen/fixpp-codegen/emit_builders.cpp  is_official /
                                 is_n002_n003_excluded / is_framing_tag /
                                 top_level_synthetic_members / resolve_level /
                                 compute_signature / PlanIntern /
                                 assign_plan_names
"""
import sys
from collections import OrderedDict
from pathlib import Path
from xml.etree import ElementTree as ET

# ── xml_loader.cpp kFieldTypeTable ────────────────────────────────────────────
FIELD_TYPE = {
    "INT": "Int", "LENGTH": "Length", "SEQNUM": "SeqNum", "NUMINGROUP": "NumInGroup",
    "DAYOFMONTH": "DayOfMonth", "PRICE": "Price", "QTY": "Qty", "AMT": "Amt",
    "PRICEOFFSET": "PriceOffset", "PERCENTAGE": "Percentage", "FLOAT": "Float",
    "CHAR": "Char", "BOOLEAN": "Boolean", "STRING": "String",
    "MULTIPLECHARVALUE": "MultiCharValue", "MULTIPLEVALUESTRING": "MultiStringValue",
    "MULTIPLESTRINGVALUE": "MultiStringValue", "CURRENCY": "Currency",
    "EXCHANGE": "Exchange", "COUNTRY": "Country", "MONTHYEAR": "MonthYear",
    "UTCTIMESTAMP": "UtcTimestamp", "UTCTIMEONLY": "UtcTimeOnly",
    "UTCDATEONLY": "UtcDateOnly", "UTCDATE": "UtcDateOnly",
    "LOCALMKTDATE": "LocalMktDate", "TZTIMEONLY": "TzTimeOnly",
    "TZTIMESTAMP": "TzTimestamp", "LANGUAGE": "Language", "DATA": "Data",
    "XMLDATA": "XmlData", "TAGNUM": "Int", "LOCALMKTTIME": "LocalMktDate",
    "XID": "String", "XIDREF": "String", "TIME": "UtcTimestamp",
    "DATE": "LocalMktDate",
}

# ── gen_util.hpp kind_of  (enum class TypeKind { String,Char,Bool,Int32,Decimal,Skip }) ──
KIND = {}
for t in ("Price", "Qty", "Amt", "PriceOffset", "Percentage", "Float"):
    KIND[t] = 4  # Decimal
for t in ("Char", "MultiCharValue"):
    KIND[t] = 1  # Char
KIND["Boolean"] = 2  # Bool
for t in ("Int", "Length", "SeqNum", "NumInGroup", "DayOfMonth"):
    KIND[t] = 3  # Int32
for t in ("String", "MultiStringValue", "Currency", "Exchange", "Country", "MonthYear",
          "UtcTimestamp", "UtcTimeOnly", "UtcDateOnly", "LocalMktDate", "TzTimeOnly",
          "TzTimestamp", "Language", "Data", "XmlData"):
    KIND[t] = 0  # String
KIND["DialectExtension"] = 5  # Skip

OFFICIAL33 = {"D", "E", "F", "G", "H", "8", "9", "q", "r", "AF", "AC", "t", "u", "V",
              "W", "X", "Y", "c", "d", "e", "f", "g", "h", "i", "b", "S", "R", "AG",
              "Z", "a", "J", "P", "AS"}
N002N003 = {"BE", "BF", "BW", "BX", "BY"}
FRAMING = {8, 9, 10, 34, 35, 49, 52, 56}


class Dict:
    def __init__(self, path):
        self.root = ET.parse(path).getroot()
        assert self.root.tag == "fix"
        major = int(self.root.get("major"))
        minor = int(self.root.get("minor"))
        legacy_char_is_string = (self.root.get("type", "FIX") == "FIX"
                                 and major == 4 and minor in (0, 1))
        # global <fields>
        self.by_name = OrderedDict()   # name -> dict(tag,type,length_pair_data_tag)
        self.by_tag = {}
        for f in self.root.find("fields").findall("field"):
            tag = int(f.get("number"))
            name = f.get("name")
            ts = f.get("type")
            ft = "String" if (legacy_char_is_string and ts == "CHAR") else FIELD_TYPE[ts]
            self.by_name[name] = {"tag": tag, "type": ft, "lpdt": 0}
            self.by_tag[tag] = name
        self._detect_length_pairs()
        self.components = OrderedDict()  # name -> (index, node)
        comps = self.root.find("components")
        if comps is not None:
            for i, c in enumerate(comps.findall("component")):
                self.components[c.get("name")] = (i, c)
        self.header = self.root.find("header")
        self.trailer = self.root.find("trailer")
        self.messages = []  # (msg_type, name, node, msgcat)
        for m in self.root.find("messages").findall("message"):
            self.messages.append((m.get("msgtype"), m.get("name"), m, m.get("msgcat")))
        # dict.messages() is bytewise-sorted by MsgType
        self.messages.sort(key=lambda r: r[0].encode())

    def _mark_pair(self, length_tag, data_tag):
        nm = self.by_tag.get(length_tag)
        if nm is not None and self.by_name[nm]["lpdt"] == 0:
            self.by_name[nm]["lpdt"] = data_tag

    def _detect_length_pairs(self):
        def scan(children):
            prev_tag, prev_is_len = 0, False
            for c in children:
                nm = c.get("name")
                info = self.by_name.get(nm)
                if info is None:
                    prev_is_len = False
                    continue
                is_data = info["type"] in ("Data", "XmlData")
                if prev_is_len and is_data:
                    self._mark_pair(prev_tag, info["tag"])
                prev_tag = info["tag"]
                prev_is_len = info["type"] == "Length"
        scan(self.root.find("fields").findall("field"))
        for cont in ("header", "trailer"):
            n = self.root.find(cont)
            if n is not None:
                scan(n.findall("field"))
        for m in self.root.find("messages").findall("message"):
            scan(m.findall("field"))

    # ── xml_loader.cpp expand_field_list (FieldRef side only) ────────────────
    def expand(self, parent, out, group_no_tag=0, comp_index=0):
        for child in parent:
            t = child.tag
            if t == "field":
                info = self.by_name[child.get("name")]
                req = child.get("required", "N") == "Y"
                out.append({"tag": info["tag"], "type": info["type"],
                            "required": req, "group_no_tag": group_no_tag,
                            "lpdt": info["lpdt"]})
            elif t == "component":
                idx, node = self.components[child.get("name")]
                self.expand(node, out, group_no_tag, idx + 1)
            elif t == "group":
                info = self.by_name[child.get("name")]
                greq = child.get("required", "N") == "Y"
                out.append({"tag": info["tag"], "type": info["type"],
                            "required": greq, "group_no_tag": group_no_tag,
                            "lpdt": info["lpdt"]})
                self.expand(child, out, info["tag"], comp_index)

    def message_fields(self, node):
        """header ++ body ++ trailer, then sort-by-tag + unique-by-tag (first wins).

        KNOWN MODELLING CAVEAT: xml_loader.cpp:877-885 uses `std::ranges::sort`
        (NOT stable) before `unique`, so which duplicate-tag entry survives --
        and therefore that tag's `rule`/`group_no_tag` -- is in principle
        implementation-defined. This function uses Python's stable sort. The
        three-tier self-validation is the evidence that the difference does not
        reach a plan signature: an unstable-sort divergence on any duplicated
        tag in FIX44 or FIX50SP2 would perturb a `required` bit and fork or
        merge a plan, which would show up as a name-set mismatch against the
        078 goldens (88 and 558 names, exact). It does not.
        """
        out = []
        if self.header is not None:
            self.expand(self.header, out)
        self.expand(node, out)
        if self.trailer is not None:
            self.expand(self.trailer, out)
        out.sort(key=lambda f: f["tag"])   # stable (Python) — see note in report
        deduped, seen = [], set()
        for f in out:
            if f["tag"] not in seen:
                seen.add(f["tag"])
                deduped.append(f)
        return deduped

    # ── ir.cpp walk_level ────────────────────────────────────────────────────
    def walk_level(self, node, parent_path, out_members, out_groups):
        for child in node:
            t = child.tag
            if t == "field":
                info = self.by_name.get(child.get("name"))
                if info is None:
                    continue
                out_members.append((info["tag"], False))
            elif t == "component":
                ent = self.components.get(child.get("name"))
                if ent is None:
                    continue
                self.walk_level(ent[1], parent_path, out_members, out_groups)
            elif t == "group":
                info = self.by_name.get(child.get("name"))
                if info is None:
                    continue
                no_tag = info["tag"]
                out_members.append((no_tag, True))
                entry = {"parent_path": tuple(parent_path), "no_tag": no_tag,
                         "members": [], "delimiter_tag": 0}
                self.walk_level(child, list(parent_path) + [no_tag],
                                entry["members"], out_groups)
                if entry["members"]:
                    entry["delimiter_tag"] = entry["members"][0][0]
                out_groups.append(entry)

    # ── ir.cpp collect_tags (header/trailer provenance) ──────────────────────
    def collect_tags(self, node, out):
        if node is None:
            return
        for child in node:
            t = child.tag
            if t == "field":
                info = self.by_name.get(child.get("name"))
                if info:
                    out.add(info["tag"])
            elif t == "component":
                ent = self.components.get(child.get("name"))
                if ent:
                    self.collect_tags(ent[1], out)
            elif t == "group":
                info = self.by_name.get(child.get("name"))
                if info:
                    out.add(info["tag"])
                self.collect_tags(child, out)


class Intern:
    def __init__(self):
        self.plans = []          # dict(no_tag, signature)
        self.key_to_index = {}

    def intern(self, no_tag, delim, sig):
        key = f"{no_tag}:{sig}"
        if key in self.key_to_index:
            return self.key_to_index[key]
        idx = len(self.plans)
        self.plans.append({"no_tag": no_tag, "delim": delim, "signature": sig})
        self.key_to_index[key] = idx
        return idx


def compute_signature(delim, plan, intern):
    sig = "D" + str(delim) + ";"
    for it in plan:
        if it["is_group"]:
            sig += ("G" + str(it["no_tag"]) + ":"
                    + ("1" if it["group_required"] else "0") + ":"
                    + ("1" if it["group_check_required"] else "0")
                    + ":{" + intern.plans[it["plan_id"]]["signature"] + "};")
        else:
            sig += ("S" + str(it["tag"]) + ":"
                    + ("1" if it["required"] else "0") + ":"
                    + str(it["kind"]) + ":"
                    + ("1" if it["coupled"] else "0"))
            if it["coupled"]:
                sig += ":" + str(it["data_tag"])
            sig += ";"
    return sig


def find_group_entry(group_order, path, no_tag):
    for g in group_order:
        if g["no_tag"] == no_tag and g["parent_path"] == tuple(path):
            return g
    return None


def resolve_level(group_order, field_by_tag, path, members, intern, level_required=True):
    level_tags = {m[0] for m in members}
    length_to_data, skip = {}, set()
    for tag, is_grp in members:
        if is_grp:
            continue
        f = field_by_tag.get(tag)
        if f is None:
            continue
        dt = f["lpdt"]
        if dt != 0 and dt in level_tags:
            length_to_data[tag] = dt
            skip.add(dt)

    plan = []
    for tag, is_grp in members:
        if not is_grp and tag in skip:
            continue
        if is_grp:
            nested = find_group_entry(group_order, path, tag)
            if nested is None:
                continue
            gf = field_by_tag.get(tag)
            if gf is None:
                continue
            own_required = gf["required"]
            child = resolve_level(group_order, field_by_tag, list(path) + [tag],
                                  nested["members"], intern, own_required)
            sig = compute_signature(nested["delimiter_tag"], child, intern)
            pid = intern.intern(tag, nested["delimiter_tag"], sig)
            plan.append({"is_group": True, "no_tag": tag,
                         "group_required": own_required,
                         "group_check_required": own_required and level_required,
                         "plan_id": pid})
            continue
        f = field_by_tag.get(tag)
        if f is None:
            continue
        if tag in length_to_data:
            dt = length_to_data[tag]
            d = field_by_tag.get(dt)
            if d is None:
                continue
            plan.append({"is_group": False, "tag": tag, "coupled": True, "data_tag": dt,
                         "kind": 0,
                         "required": (f["required"] or d["required"]) and level_required})
            continue
        k = KIND[f["type"]]
        if k == 5:
            continue
        plan.append({"is_group": False, "tag": tag, "coupled": False, "data_tag": 0,
                     "kind": k,
                     "required": f["required"] and level_required})
    return plan


def assign_plan_names(intern):
    by_no_tag = OrderedDict()
    for i, p in enumerate(intern.plans):
        by_no_tag.setdefault(p["no_tag"], []).append(i)
    names = {}
    for no_tag, idxs in by_no_tag.items():
        if len(idxs) == 1:
            names[idxs[0]] = f"G_{no_tag}Args"
        else:
            for k, i in enumerate(idxs):
                names[i] = f"G_{no_tag}_{k + 1}Args"
    return [names[i] for i in range(len(intern.plans))]


def run(path, ns, mode, structural):
    d = Dict(path)
    ht = set()
    d.collect_tags(d.header, ht)
    d.collect_tags(d.trailer, ht)

    # version-wide structural group-tag set (post-082 VersionIR::group_tags)
    per_msg = {}
    struct_tags = set()
    for msg_type, name, node, msgcat in d.messages:
        go = []
        d.walk_level(node, [], [], go)
        per_msg[msg_type] = go
        for g in go:
            struct_tags.add(g["no_tag"])

    intern = Intern()
    in_scope_msgs = []
    for msg_type, name, node, msgcat in d.messages:
        is_app = msgcat == "app"
        if mode == "official":
            ok = msg_type in OFFICIAL33
        else:
            ok = is_app and not (ns == "v44" and msg_type in N002N003)
        if not ok:
            continue
        fields = d.message_fields(node)
        field_by_tag = {}
        for f in fields:
            field_by_tag.setdefault(f["tag"], f)
        # collect_top_fields: group_no_tag == 0, tag order, dedup
        top_members = []
        for f in fields:
            if f["group_no_tag"] != 0:
                continue
            tag = f["tag"]
            if tag in FRAMING or tag in ht:
                continue
            if structural:
                is_grp = tag in struct_tags
            else:
                is_grp = f["type"] == "NumInGroup"
            top_members.append((tag, is_grp))
        resolve_level(per_msg[msg_type], field_by_tag, [], top_members, intern)
        in_scope_msgs.append(msg_type)

    names = assign_plan_names(intern)
    return in_scope_msgs, names, sorted(struct_tags)


REPO = Path(__file__).resolve().parents[3]
DICTS = REPO / "dictionaries"
GOLDEN078 = REPO / "specs" / "078-precompiled-builder-libs" / "contracts" / "golden"


def report(path, ns, mode, structural):
    msgs, names, struct_tags = run(path, ns, mode, structural)
    plan_tags = sorted({int(n[2:-4].split("_")[0]) for n in names})
    files = len(msgs) * 5 + len(names) + 3
    print(f"{ns} --families {mode} predicate={'structural' if structural else 'datatype'}")
    print(f"  messages in scope   : {len(msgs)}")
    print(f"  distinct plans      : {len(names)}   (== groups/<PlanName>.hpp count)")
    print(f"  tags with a plan    : {len(plan_tags)} {plan_tags}")
    print(f"  declared group tags : {len(struct_tags)} {struct_tags}")
    print(f"  emitted file set    : {files} = {len(msgs)}x5 + {len(names)} + 3")
    ord_tags = {}
    for n in names:
        body = n[2:-4]
        if "_" in body:
            ord_tags[int(body.split("_")[0])] = ord_tags.get(int(body.split("_")[0]), 0) + 1
    print(f"  ordinaled tags      : {dict(sorted(ord_tags.items()))}")
    return names


def validate():
    ok = True
    for ns, xml, mode, exp_msgs, exp_plans in (
        ("v44", "FIX44.xml", "all", 83, 88),
        ("v50sp2", "FIX50SP2.xml", "all", 156, 558),
        ("v44", "FIX44.xml", "official", 33, 54),
    ):
        msgs, names, _ = run(DICTS / xml, ns, mode, structural=False)
        tag = f"{ns} --families {mode}"
        if (len(msgs), len(names)) != (exp_msgs, exp_plans):
            print(f"FAIL {tag}: got {len(msgs)} msgs / {len(names)} plans, "
                  f"expected {exp_msgs} / {exp_plans}")
            ok = False
            continue
        gdir = GOLDEN078 / ns / "groups"
        if mode == "all" and gdir.is_dir():
            derived = {n + ".hpp" for n in names}
            golden = {p.name for p in gdir.iterdir()}
            if derived != golden:
                print(f"FAIL {tag}: plan-name set differs from golden; "
                      f"only-derived={sorted(derived - golden)} "
                      f"only-golden={sorted(golden - derived)}")
                ok = False
                continue
            print(f"OK   {tag}: {exp_msgs} msgs / {exp_plans} plans, "
                  f"name set == {gdir.relative_to(REPO)} exactly")
        else:
            print(f"OK   {tag}: {exp_msgs} msgs / {exp_plans} plans "
                  f"(no golden for official mode by design -- determinism_test.cpp:898-909)")
    return ok


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--validate":
        sys.exit(0 if validate() else 1)
    if len(sys.argv) == 1:
        if not validate():
            sys.exit(1)
        print()
        for mode in ("all", "official"):
            report(DICTS / "FIX42.xml", "v42", mode, structural=True)
            print()
        sys.exit(0)
    path, ns, mode, structural = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4] == "struct"
    names = report(path, ns, mode, structural)
    print("  PLANS:", " ".join(sorted(names)))
