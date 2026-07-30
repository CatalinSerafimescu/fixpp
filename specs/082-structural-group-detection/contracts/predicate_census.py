#!/usr/bin/env python3
"""082-structural-group-detection — predicate-equivalence census (NON-CIRCULAR ORACLE).

Independent raw-XML derivation of each dictionary's repeating-group set, computed
WITHOUT loading fixpp's Dictionary/table_view or the codegen IR. This is the source
FR-018 requires: the 063 census helper established the L-063-1 carve-out using the
very predicate this feature changes, so it cannot witness the change. This can.

Two predicates per dictionary:
  type set   = {tag : the <field> definition declares type NUMINGROUP}   (today's gate)
  struct set = {tag : a <group name=N> element exists, N -> tag}         (the fix)

Orchestra (fixr:) schema is handled separately: struct set comes from each
<fixr:group>'s <fixr:numInGroup id=...>, and the type set must resolve codeset
indirection (a field typed 'FooCodeSet' whose <fixr:codeSet> is type='NumInGroup'
IS a group count — tag 552 NoSides is exactly this).

Usage:  python3 predicate_census.py [--dict-dir DIR]
Exit:   0 always (reporting tool; the C++ pin asserts the sets).
"""
import argparse
import glob
import os
import xml.etree.ElementTree as ET

R = '{http://fixprotocol.io/2020/orchestra/repository}'


def census_quickfix(path):
    """<fix> (QuickFIX) schema. Returns (type_set, struct_set, empty_groups, num2name)."""
    root = ET.parse(path).getroot()
    name2num, num2name, type_set = {}, {}, set()
    for f in root.find('fields').findall('field'):
        n, nm = int(f.get('number')), f.get('name')
        name2num[nm], num2name[n] = n, nm
        if f.get('type') == 'NUMINGROUP':
            type_set.add(n)

    struct_set, empty_groups, undeclared = set(), set(), set()
    for g in root.iter('group'):
        nm = g.get('name')
        if nm not in name2num:
            undeclared.add(nm)
            continue
        t = name2num[nm]
        struct_set.add(t)
        if len(list(g)) == 0:
            empty_groups.add(t)
    return type_set, struct_set, empty_groups, undeclared, num2name


def census_orchestra(path):
    """<fixr:repository> (Orchestra) schema, WITH codeset resolution."""
    root = ET.parse(path).getroot()
    # codeSet name -> underlying datatype
    codeset_type = {cs.get('name'): cs.get('type') for cs in root.iter(R + 'codeSet')}

    num2name, type_set = {}, set()
    for f in root.iter(R + 'field'):
        tag, nm, ty = int(f.get('id')), f.get('name'), f.get('type')
        num2name[tag] = nm
        # A field typed by a codeSet inherits that codeSet's underlying datatype.
        resolved = codeset_type.get(ty, ty)
        if resolved == 'NumInGroup':
            type_set.add(tag)

    struct_set, empty_groups = set(), set()
    for g in root.iter(R + 'group'):
        n = g.find(R + 'numInGroup')
        if n is None:
            continue
        t = int(n.get('id'))
        struct_set.add(t)
        # members = children other than numInGroup/annotation
        members = [c for c in g if c.tag not in (R + 'numInGroup', R + 'annotation')]
        if not members:
            empty_groups.add(t)
    return type_set, struct_set, empty_groups, set(), num2name


def report(label, ts, ss, empty, undeclared, n2n):
    same = ts == ss
    print(f"{label:22s} type={len(ts):4d} struct={len(ss):4d}  {'EQUAL' if same else 'DIFFER'}")
    if not same:
        only_t, only_s = sorted(ts - ss), sorted(ss - ts)
        if only_t:
            print(f"   NUMINGROUP-typed but NOT a <group> ({len(only_t)}): "
                  f"{[(t, n2n.get(t)) for t in only_t]}")
        if only_s:
            shown = [(t, n2n.get(t)) for t in only_s][:60]
            print(f"   <group> but NOT NUMINGROUP-typed ({len(only_s)}): {shown}")
    if empty:
        print(f"   !! zero-member <group> elements: {[(t, n2n.get(t)) for t in sorted(empty)]}")
    if undeclared:
        print(f"   !! <group> with no <field> definition: {sorted(undeclared)}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dict-dir', default='dictionaries')
    args = ap.parse_args()

    for path in sorted(glob.glob(os.path.join(args.dict_dir, '*.xml'))):
        report(os.path.basename(path), *census_quickfix(path))
    for path in sorted(glob.glob(os.path.join(args.dict_dir, 'orchestra', '*.xml'))):
        report(os.path.basename(path), *census_orchestra(path))


if __name__ == '__main__':
    main()
