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


def _reachable_groups_quickfix(root, name2num):
    """Group tags transitively reachable from a <message> (component refs expanded).

    A <group> declared only inside an unreferenced <component> is in the struct set but
    is never registered, because both as_table_view() loops filter over a MESSAGE's own
    field run. Registration figures must therefore be reachability-restricted, not raw
    struct-set cardinality.
    """
    comps = {c.get('name'): c for c in root.iter('component')
             if c.get('name') and len(list(c))}
    out, seen_comp = set(), set()

    def walk(node):
        for ch in node:
            if ch.tag == 'group':
                nm = ch.get('name')
                if nm in name2num:
                    out.add(name2num[nm])
                walk(ch)
            elif ch.tag == 'component':
                nm = ch.get('name')
                if nm in comps and nm not in seen_comp:
                    seen_comp.add(nm)          # cycle guard; components are a DAG in practice
                    walk(comps[nm])
                    seen_comp.discard(nm)

    for m in root.find('messages').findall('message'):
        walk(m)
    # xml_loader.cpp:926-931 expands <header> and <trailer> into EVERY message's field
    # run, so a group declared there (e.g. NoHops(627)) is reachable from every message.
    for section in ('header', 'trailer'):
        node = root.find(section)
        if node is not None:
            walk(node)
    return out


def census_quickfix(path):
    """<fix> (QuickFIX) schema. Returns (type_set, struct_set, empty, undeclared, num2name, reach)."""
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
    return (type_set, struct_set, empty_groups, undeclared, num2name,
            _reachable_groups_quickfix(root, name2num))


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
    # Orchestra groups are reached via <fixr:groupRef>; every declared group in the
    # vendored FIX Latest repository is message-reachable, so struct == reachable there.
    return type_set, struct_set, empty_groups, set(), num2name, set(struct_set)


def report(label, ts, ss, empty, undeclared, n2n, reach):
    same = ts == ss
    # Effective registration = reachability-restricted, since both as_table_view() loops
    # filter over a message's own field run.
    before = ts & ss & reach   # today: nominated by datatype AND a real <group> AND reachable
    after = ss & reach         # after:              a real <group> AND reachable
    delta_add, delta_del = sorted(after - before), sorted(before - after)
    print(f"{label:22s} type={len(ts):4d} struct={len(ss):4d}  {'EQUAL' if same else 'DIFFER'}"
          f"   | registered: {len(before):4d} -> {len(after):4d}"
          f"  (+{len(delta_add)}/-{len(delta_del)})")
    if delta_add:
        print(f"   REGISTER: {[(t, n2n.get(t)) for t in delta_add][:40]}")
    if delta_del:
        print(f"   UNREGISTER: {[(t, n2n.get(t)) for t in delta_del]}")
    unreachable = ss - reach
    if unreachable:
        print(f"   note: declared <group> not message-reachable ({len(unreachable)}): "
              f"{[(t, n2n.get(t)) for t in sorted(unreachable)][:20]}")
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
