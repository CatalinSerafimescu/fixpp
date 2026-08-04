# Data model — 087 system include binding

**Feature**: `087-system-include-binding` · **Date**: 2026-08-04

Four entities. All are build-time artifacts; the feature introduces no runtime type and no C-ABI change.

---

## E1 — Include entry

One directory on a target's effective include path, as reported by the File API.

| field | type | source | notes |
|---|---|---|---|
| `path` | string, absolute | `compileGroups[].includes[].path` | **Forward slashes on both platforms**, including Windows (`C:/temp/...`) — measured, R6 |
| `isSystem` | boolean | `compileGroups[].includes[].isSystem` | Absent in JSON means `false`; **measured `true` for every entry today**, since imported targets' `INTERFACE_INCLUDE_DIRECTORIES` are SYSTEM by default |

**Validation**: `path` MUST be normalised to a **prefix-relative** form before comparison (I1). An entry whose
`isSystem` differs from its expected counterpart is a mismatch even when `path` matches (FR-003a).

---

## E2 — Observed include set

The E1 entries for one probe target, from one configure of the consumer sub-build. The reply lists them in an
order, and that order is **recorded as measurement metadata, not asserted** — the comparison canonicalises to
an unordered set (I2, contract §1a).

| field | type | notes |
|---|---|---|
| `target` | string | the probe target name; the reply is found by globbing `target-<name>-*.json` — names carry a content hash and MUST NOT be hard-coded |
| `entries` | list of E1 | **measured**, never defaulted (FR-002) |
| `present` | boolean | whether the reply existed and parsed at all — **a missing reply is FATAL, not empty** (FR-005) |

**Validation**: `present == false` MUST fail with a diagnostic naming the missing file. This is the entity's
most important rule: the realistic failure mode is that the File API query was never created, in which case
*no reply exists*, and treating that as "no includes" is the vacuity this feature exists to prevent.

### Measured values (research R4, R6 — identical on Linux and MSVC)

| probe target | links | entries |
|---|---|---|
| `probe_usage_requirements`, `probe_capi_positive`, `probe_capi_positive_c`, `probe_capi_negative`, `probe_capi_negative_service`, `consumer_capi_witness` | `fixpp::capi` | **1** |
| `probe_service_positive`, `probe_service_negative` | `fixpp::service` | **2** |
| `probe_umbrella`, `consumer_witness` | `fixpp::fixpp` | **7** |

---

## E3 — Declared expectation

The right-hand side of the comparison, **written in the tree with a stated rationale** (FR-003, US3). One per
leg.

| field | type | value |
|---|---|---|
| `leg` | enum | `capi` \| `service` — a **closed** set of exactly two; the carrier rejects a missing, duplicate or unknown leg (contract C-6.4) |
| `probe target` | string | the target whose reply supplies this leg's E2: `probe_usage_requirements` for `capi`, `probe_service_positive` for `service` (contract C-6.4) |
| `entries` | set of (prefix-relative path, isSystem) | see below — **unordered**; see I2 |
| `origin` | prose | why each member is there — a literal with a comment, never computed from the run it checks |

### The two expectations (measured, R4)

**`capi` leg — exactly 1:**

| prefix-relative path | isSystem | why it is there |
|---|---|---|
| `include/capi` | `true` | the C-ABI root 086 installs; `fixpp::capi`'s only `$<INSTALL_INTERFACE:>` |

**`service` leg — exactly 2:**

| prefix-relative path | isSystem | why it is there |
|---|---|---|
| `include/service-iface` | `true` | `src/service/CMakeLists.txt`'s own independently-declared `$<INSTALL_INTERFACE:>` |
| `include/capi` | `true` | inherited: `fixpp::service` links `fixpp::capi` (086 contract §2) |

**Closed set.** No toolchain or SDK root is a member, because none is ever observed — compiler built-ins reach
the compiler through its own search path (Linux) or the `INCLUDE` environment variable (MSVC), never through
CMake, so they do not appear in `compileGroups[].includes[]`. Measured on both platforms (R2, R6). This is
what makes an exact-equality expectation cheap to state and stable across compiler and SDK upgrades.

---

## E4 — Per-leg result file

The artifact `compare` writes for one leg before it terminates. `leg-set` consumes only these files; it does
not re-open replies.

| field | type | value |
|---|---|---|
| `leg` | enum | `capi` \| `service` — recorded by `compare`, then read back by `leg-set` |

**Validation**: the file MUST be written **before** `compare` terminates, including on a red comparison. The
minimum schema is just `leg`; under the capi-first fail-fast carrier the final status, tokens and diagnostics
travel on `compare`'s own exit status and output rather than through this file.

---

## Invariants

- **I1 — prefix-relative comparison.** Both sides are compared with the staged prefix removed. Absolute paths
  embed a per-run, per-platform location (`/tmp/fixpp-stage-086` vs `C:/temp/fixpp-stage-087`) and would fail
  for reasons unrelated to the include interface. An observed entry outside that prefix remains a canonical
  absolute path and therefore fails as a `LEAK` against the closed prefix-relative expectation.
- **I2 — exact set equality, over an UNORDERED set, matched BY `path` IN TWO ORDERED STAGES.** Entry order is
  **not** part of the compared value and is not asserted. Both sides are sets of `(path, isSystem)` pairs, but
  **`path` alone is the match key** and the comparison is staged (contract C-1, which is the authority):

  1. Pair observed with expected **by `path`**. A matched pair whose `isSystem` values differ fails
     (**`RECLASSIFIED`**). Every path-matched pair is **removed before stage 2**, whether or not its
     `isSystem` agreed — so `isSystem` is only ever compared *within* a matched pair.
  2. Of the residue: observed-only fails (**`LEAK`**); expected-only fails (**`DROP`**).

  One comparison may report more than one of the three, and every non-empty class is named.

  > **Why the staging is part of the invariant.** This paragraph previously said only that *"both sides are
  > canonicalised to a set of `(path, isSystem)` pairs before comparison"*. Read literally, a flipped
  > `isSystem` (contract §5 demonstration #4) puts `(p,false)` in `observed ∖ expected` **and** `(p,true)` in
  > `expected ∖ observed` **and** satisfies the classification test — three tokens from one cause, with no
  > rule ordering them. Matching by `path` first is what makes #4 emit `RECLASSIFIED` and nothing else.
  > *(Corrected at Gate A round 2.)*

  Containment is insufficient: it cannot see a drop, which is half of what C-3 claims. *(Where `research.md`
  records observed ordering, that is a measurement note — recorded, not asserted; contract §1a says the same.)*
- **I3 — the expectation is non-empty.** Unlike the three legs 086 already asserts, the expected set here has
  members, so an empty observation can never equal it. Emptiness fails by arithmetic rather than by a special
  case — the structural anti-vacuity property. **This, not `E2.present`, is what catches a reply that exists
  and parses but yields zero include entries**: `present` ranges over reply existence and parse only, so a
  populated-but-empty observation passes it and is rejected here, as a `DROP`. The two guards are easy to
  conflate.
- **I4 — the expectation has an origin in the tree.** It is a literal with a rationale comment. Nothing may
  derive it from the observation it is compared against; such a comparison is satisfied by whatever the run
  produced. **I4 is a review-time invariant only** — no demonstration and no mechanised check would catch a
  future edit that reintroduced a computed expectation (US3's Independent Test is a human inspection). This is
  accepted; it is stated so I4 is not read as enforced by the gate. Contract C-4 records the same.
- **I5 — the reply is located by glob, not by name.** `target-<name>-<config>-<hash>.json`; the hash changes.
- **I6 — the query precedes configure.** The driver writes `.cmake/api/v1/query/codemodel-v2` **before**
  configuring the sub-build. `tests/consumer/CMakeLists.txt` cannot do this for its own reply — it runs during
  that configure.
- **I7 — `leg-set` reasons over per-leg results, not replies.** Exactly two E4 instances, naming the distinct
  known legs `capi` and `service`, are required for success. The carrier may fail-fast on the first red
  compare, but row #8 remains satisfiable because it invokes `capi` first and therefore reaches `leg-set`
  only on the green control path.

---

## Relationships

```text
E3 declared expectation ──compared, exact equality (I2)── E2 observed include set
   (literal, in-tree,                                        (measured per configure)
    per leg, with origin)                                          │
                                                                   └── list of E1 include entries
                                                                          (path, isSystem)

E2 observed include set + E3 + install-prefix ──compare── E4 per-leg result file
                                                        (written before termination)
```

One E3 per leg; one E2 per probe target per configure; each E2 holds zero or more E1 — and **zero is a
failure**, never a pass. One E4 per successful or red `compare` invocation; exactly two distinct E4s are
required on the green carrier path.
