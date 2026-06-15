# Quickstart: F-f tail hardening bundle (039)

How to exercise each user story's witness. Build from a configured preset (e.g.
`linux-clang-debug`); run from the library submodule root.

## US1 — wire tag-overflow guard (MED, production change)

```bash
# Adversarial forged-tag aliasing witness (Index + Scan modes)
ctest --test-dir build/<preset> -R 'wire.*tag.*overflow|tag.*alias' -V
```
Expect: forged token `4294967330` (uint32-wraps to 34) is rejected in both modes; not queryable
under tag 34. `65535` decodes; `70000` rejects. Conforming-corpus wire tests unchanged (SC-002).

## US2 — C-ABI sentinel behavior pinned (no production change)

```bash
ctest --test-dir build/<preset> -R 'decimal_capi_error' -V
```
Expect: `fixpp_decimal_compare_checked` / `_equal_checked` of the `INT64_MIN` sentinel (valid
exponent) return `FIXPP_ERR_OK` with ordering/equal 0 (ratified behavior, AC-C6/D-12). Out-of-domain
*exponent* still returns `FIXPP_ERR_DECIMAL_INVALID`.

## US3 — coverage-waiver remediation (no production change)

```bash
ctest --test-dir build/<preset> -R 'seqnum.*set_next_outbound|seqnum.*lock_fail|os_file.*move' -V
```
Expect: `set_next_outbound` lock-fail branch executes (via `mutex_test_access`) → returns
`session_already_closed`; `OsFile` move-ctor exercised. 033 lines re-measured (coverage report).

## US4 — §XV.9 corpus-gate extension (no production change)

```bash
ctest --test-dir build/<preset> -R 'check_no_std_mutex_corpus' -V
```
Expect: the gate now lists the previously-uncovered session-side awaitable headers and PASSES on the
current tree (they are clean today).

## US5 — L-033-3 doc resolution (no production change)

```bash
grep -n 'L-033-3' spec/behaviors-and-limitations.md
```
Expect: resolved wording (no open placeholder) and the absent-`1137`-ack case documented.

## Full verification (pre-Gate-B)

`/speckit-verify` runs the 6-preset Tier-1 matrix (incl. TSan) + lcov DA/BRDA + the corpus gate.
US1 is the only production change; US2–US5 are test/build-gate/doc.
