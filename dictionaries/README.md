# `dictionaries/` — vendored QuickFIX-XML data dictionaries

Verbatim copies of the FIX data-dictionary XML files from upstream
[`quickfix/quickfix`](https://github.com/quickfix/quickfix) at the pinned SHA
recorded in `UPSTREAM.txt`. Consumed by `fixpp::dict::XmlLoader::load(...)` per
`specs/002-dictionary-xml-loader/research.md` D-2.

## Files

| File              | Upstream path | FIX version          |
|-------------------|---------------|----------------------|
| `FIX42.xml`       | `spec/FIX42.xml`       | 4.2 (codegen target) |
| `FIX44.xml`       | `spec/FIX44.xml`       | 4.4 (codegen target) |
| `FIX50SP2.xml`    | `spec/FIX50SP2.xml`    | 5.0 SP2 (codegen target) |
| `FIXT11.xml`      | `spec/FIXT11.xml`      | FIXT.1.1 session-layer |

The four files cover the four codegen-target versions per
`/clarify` Q1 → B (spec.md §1). The loader code path structurally accepts the
remaining five v1.0-supported versions (`v40, v41, v43, v50, v50sp1`) but no
XML is checked in for them; F1 (spec.md §10) tracks the runtime-XML-only
remainder.

## Pin rationale

Pinning to a specific upstream SHA gives the loader a stable, reproducible
input across CI machines and developer hosts. The XML files are part of the
test fixture set; refreshing the pin is a deliberate, reviewed change (the
diff against the prior pin is examined for grammar/schema drift).

## Refresh recipe

When upstream lands a fix that we want to pick up (e.g., a corrected
`<field>` declaration), refresh the four files together at one new SHA:

```bash
SHA="<new-sha>"
cd dictionaries
for f in FIX42.xml FIX44.xml FIX50SP2.xml FIXT11.xml; do
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
