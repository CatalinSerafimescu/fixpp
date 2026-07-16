# Contract: builder-tier completeness census (FR-010)

**Feature**: 077-builder-args-dedup

Re-instates and generalizes 076's descoped V-2 / V-2b builder-completeness
legs. Non-circular: the expected set is derived from the IR application
predicate, **not** from the emitter's own message walk.

## C1 — Expected set (independent derivation)

For each builder-bearing version `V`:

```
expected(V) = { m.msg_type
                | m ∈ ir(V).messages
                ∧ m.is_application
                ∧ in_scope(V, m.msg_type) }
```

where `in_scope`:
- **v44** — `all`: exclude `{BE,BF,BW,BX,BY}`; `official`: the frozen 33.
- **v42 / v50sp2 / vlatest** — no exclusion (full `is_application`).
- **vt11** — `expected = ∅` (admin-only; asserts empty, no file).

`m.is_application` comes from `MessageIR::is_application` (msgcat / Orchestra
category), the same fail-closed predicate the loader validates — **not** the
builder emitter's traversal.

## C2 — Actual set

```
actual(V) = { entry.msg_type | entry ∈ builder_registry(V) }
```

parsed from the emitted/golden `Builders.hpp` `builder_registry` array.

## C3 — Assertion

- **C3a** — `actual(V) == expected(V)` as **exact-set equality** (not subset,
  not superset). SC-006.
- **C3b** — The gate is **proven able to fail**: a mutation dropping one
  in-scope message from the emitter makes the census go red (demonstrated in the
  test, per research.md R5 / the sanitizer-canary discipline).
- **C3c** — vt11: assert no `Builders.hpp` is emitted and `expected = ∅`.

## C4 — Structural-key safety (secondary)

The distinct-plan discovery doubles as the named-invariant pin: a future
dictionary bump that introduces a new structural variant of an existing
`no_tag` surfaces as a new `G_<no_tag>_<ordinal>Args` in the regenerated golden
diff — never a silent mis-share. (Guards the `no_tag`-is-not-unique finding of
research.md R2.)
