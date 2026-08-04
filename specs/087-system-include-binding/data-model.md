# Data model — 087 system include binding

**Feature**: `087-system-include-binding` · **Date**: 2026-08-04

Three entities. All are build-time artifacts; the feature introduces no runtime type and no C-ABI change.

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

The ordered list of E1 for one probe target, from one configure of the consumer sub-build.

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
| `leg` | enum | `capi` \| `service` |
| `entries` | list of (prefix-relative path, isSystem) | see below |
| `origin` | prose | why each member is there — a literal with a comment, never computed from the run it checks |

### The two expectations (measured, R4)

**`capi` leg — exactly 1:**

| prefix-relative path | isSystem | why it is there |
|---|---|---|
| `include/capi` | `true` | the C-ABI root 086 installs; `fixpp::capi`'s only `$<INSTALL_INTERFACE:>` |

**`service` leg — exactly 2, in this order:**

| prefix-relative path | isSystem | why it is there |
|---|---|---|
| `include/service-iface` | `true` | `src/service/CMakeLists.txt`'s own independently-declared `$<INSTALL_INTERFACE:>` |
| `include/capi` | `true` | inherited: `fixpp::service` links `fixpp::capi` (086 contract §2) |

**Closed set.** No toolchain or SDK root is a member, because none is ever observed — compiler built-ins reach
the compiler through its own search path (Linux) or the `INCLUDE` environment variable (MSVC), never through
CMake, so they do not appear in `compileGroups[].includes[]`. Measured on both platforms (R2, R6). This is
what makes an exact-equality expectation cheap to state and stable across compiler and SDK upgrades.

---

## Invariants

- **I1 — prefix-relative comparison.** Both sides are compared with the staged prefix removed. Absolute paths
  embed a per-run, per-platform location (`/tmp/fixpp-stage-086` vs `C:/temp/fixpp-stage-087`) and would fail
  for reasons unrelated to the include interface.
- **I2 — exact set equality.** An entry observed but not expected fails (**leak**); an entry expected but not
  observed fails (**drop**); an entry on both sides with differing `isSystem` fails (**reclassification**).
  Containment is insufficient: it cannot see a drop, which is half of what C-3 claims.
- **I3 — the expectation is non-empty.** Unlike the three legs 086 already asserts, the expected set here has
  members, so an empty observation can never equal it. Emptiness fails by arithmetic rather than by a special
  case — the structural anti-vacuity property.
- **I4 — the expectation has an origin in the tree.** It is a literal with a rationale comment. Nothing may
  derive it from the observation it is compared against; such a comparison is satisfied by whatever the run
  produced.
- **I5 — the reply is located by glob, not by name.** `target-<name>-<config>-<hash>.json`; the hash changes.
- **I6 — the query precedes configure.** The driver writes `.cmake/api/v1/query/codemodel-v2` **before**
  configuring the sub-build. `tests/consumer/CMakeLists.txt` cannot do this for its own reply — it runs during
  that configure.

---

## Relationships

```text
E3 declared expectation ──compared, exact equality (I2)── E2 observed include set
   (literal, in-tree,                                        (measured per configure)
    per leg, with origin)                                          │
                                                                   └── list of E1 include entries
                                                                          (path, isSystem)
```

One E3 per leg; one E2 per probe target per configure; each E2 holds zero or more E1 — and **zero is a
failure**, never a pass.
