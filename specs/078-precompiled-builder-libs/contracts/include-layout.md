# Contract: Generated include layout + macro surface

**Feature**: 078-precompiled-builder-libs · Phase 1

Defines the file set the emitter produces per builder-bearing version and the
consumer-visible include/macro contract. Replaces the monolithic
`fixpp/<ns>/Builders.hpp`.

## Generated file set (per version `<ns>` ∈ {v44, v50sp2, vlatest})

Emitted into `${CMAKE_BINARY_DIR}/_codegen/include/fixpp/<ns>/` (the **build-tree**
slim layout that the library's own tests + in-tree consumers include; external
`install(TARGETS)`/header-export is deferred with the targets — see R3 /
cmake-targets.md, Gate A round 1 build-tree-only decision):

```
fixpp/<ns>/
├── groups.hpp                 # DATA-ONLY: shared G_<no_tag>[_<ord>]Args data structs
│                              #   (NO validator traits — R2)
├── validators/
│   └── traits.hpp             # shared group-plan inline writer_traits<T> + helpers (R2);
│                              #   #include "../groups.hpp" (traits specialize over the group
│                              #   Args structs — emit_builders.cpp:959-960 — so they need the
│                              #   complete data structs); included ONLY by the validator
│                              #   surface, never by builders
├── messages/
│   ├── <Msg>.hpp              # slim: #include "../groups.hpp"; <Msg>Args;
│   │                          #   extern build_<Msg> / validate_<Msg> decls
│   │                          #   (builder-inline macro → #include "<Msg>.builder.inl";
│   │                          #    validator-inline macro → #include "<Msg>.validator.inl")
│   ├── <Msg>.builder.inl      # inline build_<Msg> body (groups data only, no traits)
│   ├── <Msg>.validator.inl    # inline validate_<Msg> body + per-message top-level traits;
│   │                          #   #include "../validators/traits.hpp"
│   ├── <Msg>.builder.cpp      # external-linkage build_<Msg> def → fixpp_builders_<ver> (not a header)
│   └── <Msg>.validator.cpp    # external-linkage validate_<Msg> def + per-message traits
│                              #   → fixpp_validators_<ver> (not a header)
└── all.hpp                    # #include every messages/<Msg>.hpp + the builder_registry
                               #   aggregate (New-1)  (replaces Builders.hpp)
```

`vt11` emits none (0 application messages); `v42` emits none (deferred to #196).

## Consumer include contract

| Consumer intent | Include | Link |
|---|---|---|
| One/few messages, cheap compile (default) | `fixpp/<ns>/messages/<Msg>.hpp` | `fixpp_builders_<ver>` (+ `fixpp_validators_<ver>` iff `validate_<Msg>` is called) |
| Whole version, cheap compile | `fixpp/<ns>/all.hpp` | as above |
| Force-inline chosen builders (zero-overhead) | define `FIXPP_BUILDERS_HEADER_ONLY` (whole-TU) **or** the per-message override; include as above | link the non-inlined remainder |
| Force-inline chosen validators (zero-overhead) | define `FIXPP_VALIDATORS_HEADER_ONLY` **or** the per-message validator override | link the non-inlined validator remainder |

- **Declarations are free.** Including `<Msg>.hpp` (or `all.hpp`) in default mode
  costs declarations + `groups.hpp` only — never the function bodies (SC-001).
- **`all.hpp` stays slim by default (R5).** In link mode it is N declaration
  headers (plus the `builder_registry` aggregate); only under
  `FIXPP_BUILDERS_HEADER_ONLY` / `FIXPP_VALIDATORS_HEADER_ONLY` does it pull every
  `.builder.inl` / `.validator.inl` and reintroduce full-parse cost — by explicit choice.
- **`validate_<Msg>` decl ≠ validator code.** Declaring it is free; a send-only
  consumer that never calls it and never links `fixpp_validators_<ver>` carries
  zero validator machine code (SC-003).
- **Removed:** `fixpp/<ns>/Builders.hpp` no longer exists (FR-008, breaking; the
  tier is opt-in + not yet consumed in production).

## Macro surface

- `FIXPP_BUILDERS_HEADER_ONLY` — whole-TU builder-inline mode: every included
  message header pulls its `.builder.inl` (inline `build_` body) instead of the
  `extern build_` decl.
- `FIXPP_VALIDATORS_HEADER_ONLY` — whole-TU validator-inline mode: pulls every
  `.validator.inl` (inline `validate_` body + per-message traits) instead of the
  `extern validate_` decl. Independent of the builder mode, so a builder-inline
  TU never pulls validator traits (SC-003 on the inline path).
- Per-message override (exact spelling decided at `/implement`; e.g.
  `FIXPP_BUILDERS_HEADER_ONLY_<Msg>` / `FIXPP_VALIDATORS_HEADER_ONLY_<Msg>` or
  directly `#include ".../<Msg>.builder.inl"`) — force-inline a chosen subset per
  side while linking the rest. Mixing DIFFERENT messages is ODR-safe (R2/R2a):
  the shared group-plan traits are the single `inline` definition in
  `validators/traits.hpp`. The switch is a **program-wide per-message** control,
  however: the *same* message force-inlined in one TU and link-resolved
  (strong external) in another TU within one program is **unsupported** — an
  ODR violation under [dcl.inline]/4 (IFNDR, no diagnostic required) — and MUST
  NOT be relied upon. See quickstart.md Scenario 4d and spec.md Edge Cases.

## Invariants (testable)

1. Including one `<Msg>.hpp` does not transitively include any other message's
   `.inl`, any validator trait, or the monolith (compile-cost witness →
   compile-bench record).
2. A builder-only include graph (`<Msg>.hpp` / `<Msg>.builder.inl` /
   `<Msg>.builder.cpp`) never reaches `validators/traits.hpp` or any `validate_`
   symbol (SC-003) — enforced by the emitter invariant + `nm` witness + R2a.
3. Link mode and inline mode agree for the same inputs, every message, every
   version — the builder produces **byte-identical** wire bytes, the validator is
   **result-identical** (same success/error + offending tag) (SC-004 / FR-009).
4. `all.hpp` (default mode) does not resurrect the ~3.6 GiB include cost (R5 guard).
5. `fixpp/<ns>/Builders.hpp` does not resolve after the split (US4 AC2).
