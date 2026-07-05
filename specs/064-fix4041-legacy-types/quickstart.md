# Quickstart — FIX 4.0/4.1 dictionary loader legacy-type support (064 / D-004)

**Feature**: `064-fix4041-legacy-types` · **Date**: 2026-07-05

## Vendor the two dictionaries (verbatim, pinned SHA)

```bash
cd research/G19-fix-fpml-iso20022/library
SHA=$(grep -oE '@ [0-9a-f]+' dictionaries/UPSTREAM.txt | awk '{print $2}')   # 19ef6a4c…
for V in FIX40 FIX41; do
  curl -sSL "https://raw.githubusercontent.com/quickfix/quickfix/${SHA}/spec/${V}.xml" \
    -o "dictionaries/${V}.xml"
done
# Confirm only DATE/TIME are the out-of-vocabulary types (research R1 / confirm-at-implement #1):
for V in FIX40 FIX41; do
  echo "== $V =="; grep -oiE "type='[A-Za-z]+'" dictionaries/${V}.xml | sort -u
done
```

## Build + run the dictionary tests

```bash
# Configure/build the dictionary test target (per the repo's Conan+CMake preset flow;
# see feedback_conan_preset_build_infra_gotchas for the -of build/<preset> invocation).
conan install . -of build/clang-debug --build=missing -s build_type=Debug
cmake --preset clang-debug && cmake --build build/clang-debug --target dictionary_lookup_test -j2
ctest --test-dir build/clang-debug -R 'dictionary_(lookup|negative|xml_loader)' --output-on-failure
```

## What the tests assert (US1/US2/US3)

- **US1 — loadability + typing** (`tests/dictionary/xml_loader_test.cpp` + a focused case): load
  `dictionaries/FIX40.xml` and `FIX41.xml` → no `xml_parse_error`; `field_ref(...).type` for a `DATE`
  field (`TradeDate`) == `field_data_type::LocalMktDate`, and for a `TIME` field (`SendingTime`) ==
  `field_data_type::UtcTimestamp`.
- **US2 — pre-FIXT session lookup** (`tests/dictionary/lookup_test.cpp`): two new `VersionParam` rows
  (FIX 4.0, FIX 4.1) with session msgtypes (`A`, `0`) in `required_msg_types` (present, NOT forbidden) +
  an app msgtype; all pre-existing rows still pass.
- **US3 — fail-closed guardrail** (`tests/dictionary/negative_paths_test.cpp`): the AC-L8 witness
  `AC_L8_UnknownFieldTypeThrowsXmlParseError` (`type='UNKNOWN_TYPE'`) still throws
  `dict_xml_parse_failed`, unchanged; a companion asserts `DATE`/`TIME` now DON'T throw.

## Verify no surface change (SC-004)

```bash
# The diff must touch only these paths — no public header / ABI / wire / error file:
git diff --name-only main...HEAD | grep -vE \
  '^(src/dictionary/xml_loader\.cpp|dictionaries/|tests/dictionary/|specs/064-|spec/(behaviors-and-limitations|feature-catalogue)\.md)$' \
  && echo "!! unexpected path touched" || echo "OK — scope clean"
```

## Mutation intuition (SC-003)

- Delete the `{"DATE", …}` row → `FIX40.xml`/`FIX41.xml` load throws again (both use `DATE`).
- Delete the `{"TIME", …}` row → both throw again (both use `TIME`).
- Neither row → both dictionaries fail-close exactly as before the feature.
