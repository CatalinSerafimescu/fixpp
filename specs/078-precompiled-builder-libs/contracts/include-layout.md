# Contract: Generated include layout + macro surface

**Feature**: 078-precompiled-builder-libs · Phase 1

Defines the file set the emitter produces per builder-bearing version and the
consumer-visible include/macro contract. Replaces the monolithic
`fixpp/<ns>/Builders.hpp`.

## Generated file set (per version `<ns>` ∈ {v44, v50sp2, vlatest})

Emitted into `${CMAKE_BINARY_DIR}/_codegen/include/fixpp/<ns>/` (shipped on the
install include path):

```
fixpp/<ns>/
├── groups.hpp                 # shared G_<no_tag>[_<ord>]Args data structs
│                              #   + validator-scoped inline traits (R2)
├── messages/
│   ├── <Msg>.hpp              # slim: #include "../groups.hpp"; <Msg>Args;
│   │                          #   extern build_<Msg> / validate_<Msg> decls
│   │                          #   (macro-set → #include "<Msg>.inl")
│   ├── <Msg>.inl              # inline build_<Msg> / validate_<Msg> bodies
│   └── <Msg>.cpp              # external-linkage defs → the libs (not installed as a header)
└── all.hpp                    # #include every messages/<Msg>.hpp  (replaces Builders.hpp)
```

`vt11` emits none (0 application messages); `v42` emits none (deferred to #196).

## Consumer include contract

| Consumer intent | Include | Link |
|---|---|---|
| One/few messages, cheap compile (default) | `fixpp/<ns>/messages/<Msg>.hpp` | `fixpp_builders_<ver>` (+ `fixpp_validators_<ver>` iff `validate_<Msg>` is called) |
| Whole version, cheap compile | `fixpp/<ns>/all.hpp` | as above |
| Force-inline chosen messages (zero-overhead) | define `FIXPP_BUILDERS_HEADER_ONLY` (whole-TU) **or** the per-message override; include as above | link the non-inlined remainder |

- **Declarations are free.** Including `<Msg>.hpp` (or `all.hpp`) in default mode
  costs declarations + `groups.hpp` only — never the function bodies (SC-001).
- **`all.hpp` stays slim by default (R5).** In link mode it is N declaration
  headers; only under `FIXPP_BUILDERS_HEADER_ONLY` does it pull every `.inl` and
  reintroduce full-parse cost — by explicit choice.
- **`validate_<Msg>` decl ≠ validator code.** Declaring it is free; a send-only
  consumer that never calls it and never links `fixpp_validators_<ver>` carries
  zero validator machine code (SC-003/SC-005).
- **Removed:** `fixpp/<ns>/Builders.hpp` no longer exists (FR-008, breaking; the
  tier is opt-in + not yet consumed in production).

## Macro surface

- `FIXPP_BUILDERS_HEADER_ONLY` — whole-TU header-only mode: every included
  message header pulls its `.inl` (inline definitions) instead of `extern` decls.
- Per-message override (exact spelling decided at `/implement`; e.g.
  `FIXPP_BUILDERS_HEADER_ONLY_<Msg>` or directly `#include ".../<Msg>.inl"`) —
  force-inline a chosen subset while linking the rest. Mixing is ODR-safe
  (R2/R2a): shared traits are the single `inline` definition in `groups.hpp`.

## Invariants (testable)

1. Including one `<Msg>.hpp` does not transitively include any other message's
   `.inl` or the monolith (compile-cost witness → compile-bench record).
2. Link mode and inline mode produce **byte-identical** wire output for the same
   inputs, every message, every version (SC-004 / FR-009).
3. `all.hpp` (default mode) does not resurrect the ~3.6 GiB include cost (R5 guard).
4. `fixpp/<ns>/Builders.hpp` does not resolve after the split (US4 AC2).
