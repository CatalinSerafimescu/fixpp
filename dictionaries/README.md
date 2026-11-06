# `dictionaries/` — vendored QuickFIX-XML data dictionaries

Verbatim copies of the FIX data-dictionary XML files from upstream
[`quickfix/quickfix`](https://github.com/quickfix/quickfix) at the pinned SHA
recorded in `UPSTREAM.txt`, with one exception: `FIXT11.xml`, below. Consumed by
`fixpp::dict::XmlLoader::load(...)` per `specs/002-dictionary-xml-loader/research.md` D-2.

**The exception, `FIXT11.xml` (099-fixt-appl-version-validation, FR-032).** It is the QuickFIX
C++ file plus MsgTypeGrp on Logon, so that a Logon carrying a Message Type Default validates:

- on Logon, as its last child after EncodedText, `<group name='NoMsgTypes' required='N'>` with
  RefMsgType(372), MsgDirection(385), RefApplVerID(1130), RefApplExtID(1406),
  RefCstmApplVerID(1131) and DefaultVerIndicator(1410), each `required='N'`. The existing empty
  `MsgTypeGrp` component and Logon's reference to it stay as they are;
- field definitions for NoMsgTypes(384) `NUMINGROUP`, MsgDirection(385) `CHAR` closed at `R` and
  `S`, and DefaultVerIndicator(1410) `BOOLEAN`. 384 and 385 go directly after 383. 1410 goes
  directly before 1409, out of numeric order, so that 1409 stays the last field: the `fields` hunk of
  the patch fixture `FIXT11.patch` must match its context block exactly once
  (`specs/099-fixt-appl-version-validation/research.md` R-17).

Nothing else differs from upstream.

## Files

| File              | Upstream path | FIX version          |
|-------------------|---------------|----------------------|
| `FIX40.xml`       | `spec/FIX40.xml`       | 4.0 (runtime-XML only) |
| `FIX41.xml`       | `spec/FIX41.xml`       | 4.1 (runtime-XML only) |
| `FIX42.xml`       | `spec/FIX42.xml`       | 4.2 (codegen target) |
| `FIX43.xml`       | `spec/FIX43.xml`       | 4.3 (runtime-XML only) |
| `FIX44.xml`       | `spec/FIX44.xml`       | 4.4 (codegen target) |
| `FIX50.xml`       | `spec/FIX50.xml`       | 5.0 (runtime-XML only) |
| `FIX50SP1.xml`    | `spec/FIX50SP1.xml`    | 5.0 SP1 (runtime-XML only) |
| `FIX50SP2.xml`    | `spec/FIX50SP2.xml`    | 5.0 SP2 (codegen target) |
| `FIXT11.xml`      | `spec/FIXT11.xml`      | FIXT.1.1 session-layer |

All nine v1.0-supported versions (constitution §I.1) are bundled. Four
(`FIX42, FIX44, FIX50SP2, FIXT11`) are additionally codegen targets per
`/clarify` Q1 → B (spec.md §1); the other five (`v40, v41, v43, v50, v50sp1`)
are runtime-XML only (D-004/005/006, spec.md §10 F1) — data + headline tests, no
codegen namespace.

**FIX 4.0 / 4.1 (D-004) are now bundled** (`064-fix4041-legacy-types`). They use
two pre-canonical legacy field-type names (`DATE`, `TIME`) that were outside the
loader's `[FIX50SP2 §3.3]` field-type vocabulary; `064` added two collapse-table
aliases (`TIME → UtcTimestamp`, `DATE → LocalMktDate`) so both dictionaries load,
completing the `[const §I.1]` all-nine-versions runtime-XML commitment. The
`field_data_type` enum is unchanged. Per-version codegen for all runtime-XML-only
versions is separately deferred post-v1.0 (constitution §XVIII.6), which does NOT
exempt the data files.

## Pin rationale

Pinning to a specific upstream SHA gives the loader a stable, reproducible
input across CI machines and developer hosts. The XML files are part of the
test fixture set; refreshing the pin is a deliberate, reviewed change (the
diff against the prior pin is examined for grammar/schema drift).

## Refresh recipe

When upstream lands a fix that we want to pick up (e.g., a corrected
`<field>` declaration), refresh all nine **currently bundled** files together at
one new SHA.

```bash
SHA="<new-sha>"
cd dictionaries
for f in FIX40.xml FIX41.xml FIX42.xml FIX43.xml FIX44.xml \
         FIX50.xml FIX50SP1.xml FIX50SP2.xml FIXT11.xml; do
  curl -sSL --fail \
    -o "$f" \
    "https://raw.githubusercontent.com/quickfix/quickfix/${SHA}/spec/${f}"
done
```

Then re-apply the `FIXT11.xml` exception above, with its placement, which the download
just overwrote:

```bash
python3 - <<'PY'
from pathlib import Path

GROUP = """   <group name='NoMsgTypes' required='N'>
    <field name='RefMsgType' required='N' />
    <field name='MsgDirection' required='N' />
    <field name='RefApplVerID' required='N' />
    <field name='RefApplExtID' required='N' />
    <field name='RefCstmApplVerID' required='N' />
    <field name='DefaultVerIndicator' required='N' />
   </group>
"""
FIELDS_384_385 = """  <field number='384' name='NoMsgTypes' type='NUMINGROUP' />
  <field number='385' name='MsgDirection' type='CHAR'>
   <value enum='R' description='RECEIVE' />
   <value enum='S' description='SEND' />
  </field>
"""
FIELD_1410 = "  <field number='1410' name='DefaultVerIndicator' type='BOOLEAN' />\n"


def insert(text, marker, before, addition):
    if text.count(marker) != 1:
        raise SystemExit(f"expected one {marker!r}, found {text.count(marker)}")
    return text.replace(marker, addition + marker if before else marker + addition)


p = Path("FIXT11.xml")
t = p.read_text(encoding="utf-8")
logon_end = t.index("</message>", t.index("msgtype='A'"))
line_start = t.rindex("\n", 0, logon_end) + 1
t = t[:line_start] + GROUP + t[line_start:]
t = insert(t, "  <field number='383' name='MaxMessageSize' type='LENGTH' />\n", False, FIELDS_384_385)
t = insert(t, "  <field number='1409' name='SessionStatus' type='INT'>\n", True, FIELD_1410)
p.write_text(t, encoding="utf-8")
PY
```

The recipe stops if upstream moved a field it anchors on. Then update `UPSTREAM.txt` to record the
new SHA + date + (if any) the upstream tag, and run `ctest --preset linux-clang-debug -L dictionary` to
confirm the loader still parses every file.

## License

The dictionary XML files are copied **verbatim** from the upstream
[`quickfix/quickfix`](https://github.com/quickfix/quickfix) repository at the SHA
pinned in `UPSTREAM.txt`, except `FIXT11.xml`'s addition described at the top of this
file. They are **runtime data** consumed by
`fixpp::dict::XmlLoader::load(...)` — not merely test fixtures.

**Upstream license:** the **QuickFIX Software License, Version 1.0**
(© 2001–2020 Oren Miller) — a permissive, BSD-4-clause-style license. **NOT MIT**
(an earlier version of this note said MIT; that was incorrect — the upstream
`LICENSE` at the pinned SHA is the QuickFIX Software License v1.0, reported by
GitHub as `spdx_id: NOASSERTION`). The verbatim license text is retained in
[`QUICKFIX_LICENSE.txt`](./QUICKFIX_LICENSE.txt) alongside these files, per
**Article V §4** (vendored third-party content carries file-level attribution;
compatibility verified at vendoring time).

Obligations honoured / to honour:

- **Retain** the copyright notice + license text — see `QUICKFIX_LICENSE.txt`
  (license clauses 1–2).
- **Acknowledgment (clause 3):** *"This product includes software developed by
  quickfixengine.org (http://www.quickfixengine.org/)."* This MUST also appear in
  fixpp's **end-user documentation** when the project is published (there is no
  top-level `NOTICE` file yet — add one, or place the acknowledgment in the
  top-level README, before release).
- **Name (clauses 4–5):** the name "QuickFIX" must not be used to endorse or
  promote fixpp, nor appear in fixpp's name.

The QuickFIX Software License v1.0 is permissive (redistribution + commercial use
permitted, not copyleft) and imposes no viral linkage (Article V §3).

**Open compatibility question — for counsel, NOT self-cleared:** the BSD 4-clause
*advertising* clause (clause 3) is the clause the FSF considers **GPL-incompatible**;
AGPL-3.0 (Article V §1, fixpp's license) inherits that view. For these XML **data**
files consumed at runtime — not linked code, not a derivative in the linking
sense — this is most likely not a violation, but only counsel can close it.
The **native-Orchestra** direction (Apache-2.0, SPDX-standard, no advertising
clause) removes this concern for FIX Latest and shrinks the QuickFIX-licensed
surface — see the parent repo's
`research/G19-fix-fpml-iso20022/remaining-work/orchestra-fix-latest-spike-and-plan.md`.
