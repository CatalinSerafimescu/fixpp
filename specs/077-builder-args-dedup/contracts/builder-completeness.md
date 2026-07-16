# Contract: builder-tier completeness census (FR-010)

**Feature**: 077-builder-args-dedup

Re-instates and generalizes 076's descoped V-2 / V-2b builder-completeness
legs. Non-circular at 076's independence strength: the expected set is derived
from a **raw-XML / Orchestra walk independent of `emit_builders`**, **not** from
the same `ir(V).messages` the emitter consumes.

## C1 — Expected set (independent derivation)

For each builder-bearing version `V`:

```
expected(V) = { msg_type
                | (msg_type, category) ∈ raw_walk(V)
                ∧ is_application(category)
                ∧ in_scope(V, msg_type) }
```

`raw_walk(V)` is a **standalone parser** over the source dictionary
(`FIX42.xml` / `FIX44.xml` / `FIX50SP2.xml` for the legacy tiers; the
`<fixr:repository>` Orchestra document for vlatest) that reads each message's
`msgtype` and its `msgcat`/`category` **directly from the XML** — the same
raw-XML strength as 076's V-1 census (N-1), which pinned vlatest's message
universe mutation-RED ×4. It does **NOT** call the emitter's `build_ir()`
traversal, so it catches an app/admin misclassification or a `no_tag` scope
error that a check reading `ir(V).messages` would inherit silently (the
075/076 blind-corpus lesson: [[feedback_verification_corpus_built_from_the_read_it_checks_is_blind]]).

where `in_scope`:
- **v44** — `all`: exclude `{BE,BF,BW,BX,BY}`; `official`: the frozen 33.
- **v42 / v50sp2 / vlatest** — no exclusion (full application set).
- **vt11** — `expected = ∅` (admin-only; asserts empty, no file).

`is_application(category)` is applied to the raw-XML `msgcat`/`category` value —
the same fail-closed app/admin rule the loader uses, but evaluated on the
independent raw walk, **not** on the builder emitter's own IR construction.

## C2 — Actual set

```
actual(V) = { msg_type : fixpp::<ns>::build_<Msg> ∧ validate_<Msg> exist }
```

proven by a **census translation unit that takes the address of every expected
`fixpp::<ns>::build_<Msg>` and `validate_<Msg>`** (the `(msg_type, C++ identifier)`
pairs derived from the C1 raw walk). Because address-of is a compile-time ODR-use,
the census TU **fails to compile** if any expected entry point is missing — so
`actual(V)` measures the `build_<Msg>` entry points FR-010 names, NOT a textual
proxy for them. The emitted `builder_registry` array, parsed from the golden, is
retained as a **secondary consistency check** (its msg_type set must match the
entry-point set) — it catches a registry-vs-emission divergence but never
substitutes for entry-point existence (per
[[feedback_witness_asserts_named_postcondition_not_proxy]]).

## C3 — Assertion

- **C3a** — `actual(V) == expected(V)` as **exact-set equality** (not subset,
  not superset). SC-006. The address-of census pins `expected ⊆ actual` (every
  expected `build_<Msg>`/`validate_<Msg>` must compile); the secondary registry
  parse pins `actual ⊆ expected` (no emitted builder outside the expected set).
- **C3b** — The gate is **proven able to fail** through THE committed test-only
  mutation seam: a build-time `FIXPP_CODEGEN_DROP_BUILDER_MSGTYPE=<msgtype>`
  compile-define, compiled only in the census test target, that drops exactly one
  known in-scope message from the emitted builders/registry. The
  `builder_completeness_mutation_witness` test invokes that drop path in a
  subprocess and passes only if the census goes **RED** with the expected missing
  msg_type. NOT merely "documented in the test" prose (per research.md R5 /
  [[feedback_sanitizer_canary_must_be_proven_red]]).
- **C3c** — vt11: assert no `Builders.hpp` is emitted and `expected = ∅`.

## C4 — Structural-key safety (secondary)

The distinct-plan discovery doubles as the named-invariant pin: a future
dictionary bump that introduces a new structural variant of an existing
`no_tag` surfaces as a new `G_<no_tag>_<ordinal>Args` in the regenerated golden
diff — never a silent mis-share. (Guards the `no_tag`-is-not-unique finding of
research.md R2.)
