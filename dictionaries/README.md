# `dictionaries/` — vendored QuickFIX-XML data dictionaries

Verbatim copies of the FIX data-dictionary XML files from upstream
[`quickfix/quickfix`](https://github.com/quickfix/quickfix) at the pinned SHA
recorded in `UPSTREAM.txt`. Consumed by `fixpp::dict::XmlLoader::load(...)` per
`specs/002-dictionary-xml-loader/research.md` D-2.

## Files

| File              | Upstream path | FIX version          |
|-------------------|---------------|----------------------|
| `FIX42.xml`       | `spec/FIX42.xml`       | 4.2 (codegen target) |
| `FIX43.xml`       | `spec/FIX43.xml`       | 4.3 (runtime-XML only) |
| `FIX44.xml`       | `spec/FIX44.xml`       | 4.4 (codegen target) |
| `FIX50.xml`       | `spec/FIX50.xml`       | 5.0 (runtime-XML only) |
| `FIX50SP1.xml`    | `spec/FIX50SP1.xml`    | 5.0 SP1 (runtime-XML only) |
| `FIX50SP2.xml`    | `spec/FIX50SP2.xml`    | 5.0 SP2 (codegen target) |
| `FIXT11.xml`      | `spec/FIXT11.xml`      | FIXT.1.1 session-layer |

Seven of the nine v1.0-supported versions (constitution §I.1) are bundled. Four
(`FIX42, FIX44, FIX50SP2, FIXT11`) are additionally codegen targets per
`/clarify` Q1 → B (spec.md §1); the other three (`v43, v50, v50sp1`) are
runtime-XML only (D-005/006, spec.md §10 F1) — data + headline tests, no
codegen namespace.

**FIX 4.0 / 4.1 (D-004) are NOT yet bundled.** They are runtime-XML-only in
scope (§I.1) but the loader's `[FIX50SP2 §3.3]` field-type vocabulary
fail-closes on their legacy `DATE` / `TIME` type names (the deliberate freeze,
research.md D-14). Bundling them requires first extending the loader to accept
those two legacy aliases — a design decision that reverses the freeze and so is
tracked as its own Gate-A'd loader feature, not a data-vendoring change.
Per-version codegen for all runtime-XML-only versions is separately deferred
post-v1.0 (constitution §XVIII.6), which does NOT exempt the data files.

## Pin rationale

Pinning to a specific upstream SHA gives the loader a stable, reproducible
input across CI machines and developer hosts. The XML files are part of the
test fixture set; refreshing the pin is a deliberate, reviewed change (the
diff against the prior pin is examined for grammar/schema drift).

## Refresh recipe

When upstream lands a fix that we want to pick up (e.g., a corrected
`<field>` declaration), refresh the seven **currently bundled** files together
at one new SHA. Do NOT add `FIX40.xml` / `FIX41.xml` to this loop — they are
D-004, deliberately not vendored until the loader accepts their legacy
`DATE`/`TIME` types (see above); expand the loop to all nine only after that
separate loader feature lands.

```bash
SHA="<new-sha>"
cd dictionaries
for f in FIX42.xml FIX43.xml FIX44.xml \
         FIX50.xml FIX50SP1.xml FIX50SP2.xml FIXT11.xml; do
  curl -sSL --fail \
    -o "$f" \
    "https://raw.githubusercontent.com/quickfix/quickfix/${SHA}/spec/${f}"
done
```

Then update `UPSTREAM.txt` to record the new SHA + date + (if any) the
upstream tag, and run `ctest --preset linux-clang-debug -L dictionary` to
confirm the loader still parses every file.

## License

The files are MIT-licensed verbatim from the upstream `quickfix/quickfix`
repository. The fixpp library itself is AGPL-3.0-or-later; vendoring MIT-
licensed test fixtures is compatible per `[const §V.1]` / `[const §V.3]`.
