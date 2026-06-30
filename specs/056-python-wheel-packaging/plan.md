# Implementation Plan: Python Wheel Packaging (PY-005)

**Branch**: `056-python-wheel-packaging` | **Date**: 2026-06-30 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `specs/056-python-wheel-packaging/spec.md`

## Summary

Package the already-frozen Python binding (PY-001..004: `_fixpp` extension +
`fixpp.py` + `fixpp_oo.py`) into a **single stable-ABI (abi3) `manylinux_2_28`**
binary wheel `fixpp-<ver>-cp310-abi3-manylinux_2_28_x86_64.whl` covering **CPython
3.10–3.13+** (`[2m §1.1]` / `[arch §7.1]` as proposed-amended at Gate A), built and
install-tested in **Tier-1 CI** as a mandatory merge gate (one wheel, install-tested
on each of 3.10/3.11/3.12/3.13), and attached to GitHub releases (no PyPI,
`[const §IV.5]`). Technical approach: a `scikit-build-core` PEP 517 backend over the
existing CMake `FIXPP_BUILD_PYTHON` target, driven by `cibuildwheel` inside the
`manylinux_2_28` container (Conan `--build=missing` in-container, with
`with_otel=False` + static OpenSSL for a self-contained `.so`),
`-DPy_LIMITED_API=0x030A0000` (compile-only) + scikit-build-core's `wheel.py-api = "cp310"` for the abi3 wheel tag,
`auditwheel repair` for the portable tag; FIX dictionary XMLs shipped via a tiny
data package reachable by a new pure-Python locator helper; the public surface
stays the two existing top-level modules `fixpp` + `fixpp_oo`, shipped **flat**
(fixing the latent `install(... /fixpp)` namespace-dir layout that only worked
under the in-tree `PYTHONPATH=lib`). The abi3 wheel requires a bounded limited-API
rework of the one `fixpp_py_is_main_interpreter` helper in `fixpp.i` (preserving
sub-interpreter rejection) + `CMakeLists.txt` flags — **no C-ABI change, no
binding-behaviour change; the `0→1` freeze is untouched** (FR-012).

## Technical Context

**Language/Version**: Python packaging (PEP 517/518) over C++23 build; one abi3
(stable-ABI / `Py_LIMITED_API=0x030A0000`) wheel covering CPython 3.10–3.13+.
Native extension built by **SWIG 4.2** (limited-API heap-type mode) + Clang/GCC.
**Primary Dependencies** (build-time, lower-bound pins; exact versions verified
against PyPI at implement per the dependency rule): `scikit-build-core`,
`cibuildwheel`, `auditwheel` (Linux), **`swig>=4.2` (PINNED — a 4.0 runner
silently regresses the limited-API mode; raised 4.0→4.2 for abi3, see PKG-1 / D-3)**,
Conan 2.x, CMake ≥ 3.28, Ninja.
Runtime dependency: **none** — the extension statically links `fixpp_capi` +
`-static-libstdc++/-libgcc` (already in `bindings/python/CMakeLists.txt`) and the
wheel build sets `-o fixpp/*:with_otel=False` + static OpenSSL
(`-o openssl/*:shared=False`) so `_fixpp.so` carries no external `.so`
(`auditwheel show` external list empty — LAY-3).
**Storage**: bundled FIX dictionary XMLs as package data (FIX42/44/50SP2/FIXT11).
**Testing**: `pytest` functional subset against the installed wheel in a clean
venv; existing CTest sanitizer matrix unchanged.
**Target Platform**: Linux x86_64 `manylinux_2_28` (mandatory); Windows x86_64
best-effort/deferred (`[2m §2 non-goal #4]` / `[2m §1.1]`).
**Project Type**: single project — release-engineering layer over an existing
library + binding.
**Performance Goals**: N/A (packaging). Constraint: CI wheel job wall-clock kept
reasonable (Conan cache; one abi3 build + a 4-interpreter install-test).
**Constraints**: self-contained wheel (no external `.so`); manylinux_2_28 tag;
C-ABI-only consumption (`[arch §8]`); no C-ABI/binding-behaviour change (FR-012).
**Scale/Scope**: 1 abi3 wheel × Linux, install-tested on 4 interpreters
(cp310–cp313); +1 best-effort Windows lane.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

Article-by-article (only triggered articles listed):

- **`[const §IV.3]` (Distribution)** — PASS. Produces the mandatory Linux x86_64
  CPython wheel via SWIG-over-C-ABI; Windows best-effort/deferred. (Live `§IV.3`
  is the Linux-x86_64-wheel-mandatory clause.)
- **`[const §IV.5]` (built-not-published)** — PASS. Wheels attach to GitHub
  release assets; **no PyPI** upload. Plan adds no publish-to-index step.
- **`[const §VI.5]` (Normative References)** — PASS. Spec carries the section.
  (Corrected at Gate A from the mis-cited "Article VIII §5", which is allocator
  policy; Normative References is Art VI §5.)
- **`[const §VI.2]` (canonical Spec ref)** — PASS. Catalogue row PY-005 already
  cites `[2m §1.1, §11]`; no vague refs introduced. (Corrected at Gate A from the
  mis-cited "Article VIII §2", which is the benchmark budget; canonical Spec ref
  is Art VI §2.)
- **`[const §IX.6]` (Two-tier CI / Tier-1 every PR)** — PASS. Wheel build +
  install-test added as a Tier-1 required job; existing pytest/sanitizer jobs
  unchanged.
- **`[const §X]` (ABI Policy)** — PASS / not triggered. **No C-ABI surface change**:
  `include/fix/c_api*.h` is byte-frozen and the `abidiff` golden is unaffected
  (FR-012 / SC-007). The abi3 limited-API adaptation edits only the SWIG binding
  glue (`fixpp_py_is_main_interpreter` in `fixpp.i`) and `CMakeLists.txt` build
  flags — the C-ABI it consumes is untouched, so Article X is not engaged. The
  `0→1` freeze stays held.
- **AGPL boundary — `[const §V.1]` / `[const §IV.2]` + `[arch §8]`** — PASS.
  Packaging consumes only `<fix/c_api.h>`; `tools/check_layers.py` already scans
  `bindings/python/`. The wheel statically links `fixpp_capi` only — no engine
  C++ headers. (Corrected at Gate A from the mis-cited "Article VII §3", which is
  "TDD is mandatory"; the C-ABI linkage-isolation boundary is Art V §1 / IV §2 +
  arch §8.)
- **`[const §II.3]` (Platforms)** — PASS. Linux primary; Windows on-demand Tier-2.

**No violations.** (Re-walked line-by-line at Gate A round 1: every cited
`[const §x]`/`[arch §x]` number now resolves to the rule asserted — see the RC-1
citation sweep in the Gate A section.) Complexity Tracking table omitted (nothing
to justify).

## Project Structure

### Documentation (this feature)

```text
specs/056-python-wheel-packaging/
├── plan.md              # This file
├── research.md          # Phase 0 — toolchain/manylinux/abi3/layout decisions
├── data-model.md        # Phase 1 — wheel artifact + data package + locator
├── quickstart.md        # Phase 1 — build/install/test walkthrough
├── contracts/           # Phase 1 — packaging + CI + locator-API contracts
└── tasks.md             # Phase 2 (/speckit-tasks — NOT created here)
```

### Source Code (repository root = library submodule)

```text
bindings/python/
├── pyproject.toml            # NEW — scikit-build-core backend + project metadata
├── CMakeLists.txt            # EXISTING — reused; + abi3 compile flags (-DPy_LIMITED_API;
│                             #   tag via pyproject wheel.py-api), swig>=4.2, flat-layout install fix
├── fixpp.i                   # EXISTING — frozen behaviour; ONE bounded limited-API
│                             #   rework of fixpp_py_is_main_interpreter (FR-012, preserves
│                             #   sub-interpreter rejection) + additive locator re-export
├── fixpp_oo.py               # EXISTING (frozen) — OO API, shipped as-is
├── _fixpp_data/              # NEW — tiny data package holding the FIX XMLs
│   ├── __init__.py           # NEW — marks the data package importable
│   ├── FIX42.xml             # NEW (copied/symlinked from dictionaries/ at build)
│   ├── FIX44.xml
│   ├── FIX50SP2.xml
│   └── FIXT11.xml
├── fixpp_dict_data.py        # NEW — pure-Python locator helper (FR-004a)
└── tests/                    # EXISTING — functional subset selected for the wheel

.github/workflows/
└── tier1.yml                 # EXISTING — add a `python-wheel` Tier-1 job

CMakeLists.txt / bindings/python/CMakeLists.txt  # EXISTING — flat-layout install fix
```

**Structure Decision**: Single project. The wheel is built from
`bindings/python/` with `scikit-build-core` invoking the existing CMake target.
The two public modules (`fixpp`, `fixpp_oo`) ship as **top-level modules** (flat),
per the clarification; the dictionaries live in a separate small importable data
package `_fixpp_data/` so package-data + `importlib.resources` work without
restructuring `fixpp`/`fixpp_oo`. The locator helper `fixpp_dict_data.py` resolves
a dict by name from that package. See data-model.md for the exact layout.

## Complexity Tracking

> No Constitution Check violations — section intentionally empty.

## Gate A

- Round 1 applied 2026-06-30: Codex P1=3 P2=4 P3=1; Opus post-judging P1=3 P2=6
  P3=2; rewrite addresses RC-1 (citation sweep), RC-2 (flat+static `[2m]`
  amendment), RC-3 (named witnesses) + the abi3 pivot (USER DECISION at Gate A —
  restores abi3-first; per-version retired to fallback). The round-1 cp-scope P1
  (all-four-mandatory vs cp310-only) is **mooted** by the abi3 decision (one wheel
  covers 3.10–3.13+). No Codex finding was Disagreed — Opus confirmed all. Reviews:
  research/reviews/codex_056-python-wheel-packaging_gate_a_review.md,
  research/reviews/opus_056-python-wheel-packaging_gate_a_adversarial_review.md.
- Round 2 applied 2026-06-30: Codex P1=1 P2=3 P3=1; Opus post-judging P1=0 P2=2
  P3=4; rewrite completes the inherited-doc amendment census (P2a — coverage-index
  + api-contract + 2m §3.2/goal-5/§1.1 + arch §7.1 table row + CLAUDE.md), fixes the
  cibuildwheel selector (P2c) — corrected to the architecture-only build identifier
  `cp310-manylinux_x86_64` + `CIBW_MANYLINUX_X86_64_IMAGE=manylinux_2_28`; the round-2
  review's prescribed `cp310-manylinux_2_28_x86_64` was itself non-runnable (the
  `2_28` glibc baseline is an image option, not a selector component, and the
  abi3 wheel tag is produced by scikit-build-core's `[tool.scikit-build]
  wheel.py-api = "cp310"`; `-DPy_LIMITED_API`/SABI is compile-only — scikit-build-core
  does not auto-detect the limited ABI nor consume the setuptools `py_limited_api`
  Extension kwarg), names the limited-API sub-interpreter-rejection
  replacement + flags the 3.10/3.11 verify band (N1), splits the compile-vs-runtime
  fallback claim, and corrects the [const §III.1] cite. §VI.2 catalogue-format PASS
  challenged and held (consistent with merged siblings). Reviews:
  research/reviews/codex_056-python-wheel-packaging_gate_a_2_review.md,
  research/reviews/opus_056-python-wheel-packaging_gate_a_2_adversarial_review.md.
- Round 3 applied 2026-06-30 (post-exhaustion targeted fix, user-directed): Codex P1=0 P2=1 P3=0; Opus post-judging P1=0 P2=1 P3=0; the single residual P2 — abi3 wheel tag driven by the inert setuptools py_limited_api knob under the committed scikit-build-core backend — fixed by adding [tool.scikit-build] wheel.py-api = "cp310" as the tag-driver (-DPy_LIMITED_API reframed compile-only) and deleting the false plan.md:166 verification. Reviews: research/reviews/codex_056-python-wheel-packaging_gate_a_3_review.md, research/reviews/opus_056-python-wheel-packaging_gate_a_3_adversarial_review.md.

## Proposed inherited-design amendments (Gate A)

These amend documents **outside** the `specs/056/` bundle. Per the 043/051/054
precedent they are recorded here as **PROPOSED, user-ratified at Gate A**, and are
applied to the live `.specify/` docs at `/implement` (deferred-apply). The live
`.specify/2m-pybind.md` / `.specify/architecture.md` are **not** edited by this
bundle.

### A-1 — `[2m §1.1]` + `[arch §7.1]`: mandate the single abi3 wheel (abi3 pivot)

- **Current (live)**: `[2m §1.1]` mandates **CPython 3.10 only** as the per-version
  `cp310-cp310` wheel; `[arch §7.1]` names
  `fixpp-<ver>-cp310-cp310-manylinux_2_28_x86_64.whl (mandatory)`.
- **Proposed**: change the mandated wheel to the single stable-ABI
  `fixpp-<ver>-cp310-abi3-manylinux_2_28_x86_64.whl` covering CPython 3.10–3.13+
  (one wheel replaces the per-version form). Rationale: feasibility established
  against the installed SWIG 4.2.0 (limited-API heap-type mode; the generated
  wrapper compiles under `-DPy_LIMITED_API=0x030A0000` with a single limited-API
  violation in `fixpp_py_is_main_interpreter`, fixed by a bounded ~10–30-line
  rework — research D-3). Restores the original abi3-first intent
  (`remaining-work/python-bindings.md:61`); per-version is the documented fallback.
  **User-ratified at Gate A.**
- **Same-doc + table-row sites** (Gate A round 2): `[2m §1.1]` and `[arch §7.1]`
  also surface as the `2m-pybind.md` §3.2 build-output **mirror**, the `2m-pybind.md`
  §1.1 version-range prose, and the `architecture.md` §7.1 build-output **table
  row** — all enumerated with exact from→to in **A-3** (the exhaustive census).
  A-1's amendment text must hit those, not only the loose prose.

### A-2 — `[2m §1 goal 5]` + `[2m §4.1]`: record the as-built flat-module + static-link surface (RC-2)

- **Current (live)**: `[2m §1 goal 5]` bundles the engine as **`fixpp/_fixpp.so`**
  plus **auditwheel-vendored** OpenSSL (`[const §XII.1]`) + **mimalloc**
  (`[arch §5.2]`); `[2m §4.1]` lays out a full `fixpp/` **package** (`__init__.py`
  + `_fixpp.so` + `application.py` + `config.py` + `enums.py` + `errors.py` +
  `decimal_.py` + `message.py` + `py.typed`).
- **Proposed**: record the frozen PY-001..004 as-built v1.0 surface — **flat
  top-level modules** (`_fixpp*.so` + `fixpp.py` + `fixpp_oo.py` +
  `fixpp_dict_data.py`, no `fixpp/` package) and **static-link / no-vendored-deps**
  self-containment (the `.so` statically links `fixpp_capi` +
  `-static-libstdc++/-libgcc`, `with_otel=False` + static OpenSSL, `auditwheel show`
  external list empty). The bundle's flat+static layout is an **inherited-design
  correction**, not a silent packaging detail. **Also corrects the stale
  `[2m §1 goal 5]` "mimalloc per `[arch §5.2]`"** — mimalloc is absent from
  `conanfile.py`/`CMakeLists.txt` entirely; drop it from goal 5. (abi3 does NOT
  change the flat-vs-package layout — only the ABI tag changes.) **User-ratified
  at Gate A.**
- **mimalloc-absent verified** (Gate A round 2): the deletion above was confirmed
  by `grep -rni mimalloc conanfile.py bindings/python/CMakeLists.txt CMakeLists.txt`
  → **no match** (exit 1); there is no mimalloc require/pin to keep. The `cp310-cp310
  → cp310-abi3` name-token flip inside this same `[2m §1 goal 5]` sentence is
  enumerated in **A-3**.

### A-3 — Normative-index + same-doc-mirror census (Gate A round 2, abi3 pivot)

These complete the RC-2 inherited-doc amendment census. They are the output of an
**exhaustive** `grep -rn "cp310-cp310\|3.10 mandatory\|3.10 only\|3.10 wheels only"
.specify/ spec/`, plus a `3.10`-not-in-`3.10–3.13`-range sweep **and** a
`best-effort \| 3.11 / 3.12 / 3.13` enumerated-version sweep (to catch a per-version
mandate phrased without a `3.10` token) — the terminating move. The census found
**exactly** the wheel-matrix-mandate sites below and nothing beyond them. The other
`3.10` hits are section numbers (`§3.10`) or interpreter-floor / runtime-model
references (`2m-pybind.md:121` nogil, `:956` `tomllib`, `:1043`/`:1045`
GIL/main-interpreter model) that **stay accurate under abi3** because `cp310` remains
the limited-API floor tag — reviewed-and-held, not amended. The `best-effort` hits
outside this set are **Windows** wheels (`2m-pybind.md:112/:181/:340`,
`constitution.md:73`, `[2m §10 Q9]` aarch64), FIX-version codegen, TLS/HSM, and
message-store — all unrelated to the CPython interpreter-version wheel matrix and
**unchanged by the abi3 pivot** (Windows stays deferred/best-effort regardless). After A-1/A-2/A-3
apply at `/implement`, **no live normative source names `cp310-cp310` or the
per-version mandatory/best-effort split.** All deferred-apply; the live docs are **not**
edited by this bundle.

| # | Site | From (live) | To (abi3) |
|---|---|---|---|
| 1 | `spec/coverage-index.md:618` — PY-005 "PY-bindings supplemental" (the `[const §VI.4]` bidirectional-traceability source of truth — **normative index**) | `[arch §7.1]` mandatory wheel name `fixpp-<ver>-cp310-cp310-manylinux_2_28_x86_64.whl` | `fixpp-<ver>-cp310-abi3-manylinux_2_28_x86_64.whl` (single stable-ABI wheel covering CPython 3.10–3.13+) |
| 2 | `.specify/api-contract.md:268` — §10 row 2m (**normative API-contract index**) | "CPython 3.10 mandatory; 3.11 / 3.12 / 3.13 best-effort per `[2m §1]` / `[arch §7.1]`; single-interpreter" | "single abi3 (stable-ABI) wheel covering CPython 3.10–3.13+ (cp310 floor) per `[2m §1.1]` / `[arch §7.1]`; single-interpreter" |
| 2b | `.specify/api-contract.md:316` + `:334` — Root-cause-#3 / N-P2-2 **audit-log companions** quoting the old "cp310-only mandatory; 3.11 / 3.12 / 3.13 best-effort" text | the historical resolution quotes (a past fix's wording) | **annotate** each with a forward-pointer ("superseded by the abi3 pivot — see PY-005 / `[arch §7.1]` amendment"), do **not** rewrite the quote — the audit trail must not be falsified (non-blocking, deferred-apply) |
| 3 | `.specify/2m-pybind.md:149,151` — §3.2 "From `[arch §7.1]` — Build outputs" **same-doc mirror** | `fixpp-<ver>-cp310-cp310-manylinux_2_28_x86_64.whl (mandatory)`; "The `cp310-cp310-manylinux_2_28_x86_64` ABI tag locks the v1.0 wheel." | `fixpp-<ver>-cp310-abi3-manylinux_2_28_x86_64.whl (mandatory)`; "The `cp310-abi3` stable-ABI tag locks the v1.0 wheel (one wheel covers 3.10–3.13+)." |
| 4 | `.specify/2m-pybind.md:23` — §1 goal 5 name token | `fixpp-<ver>-cp310-cp310-manylinux_2_28_x86_64.whl` (+ "mimalloc per `[arch §5.2]`" — dropped per A-2) | `fixpp-<ver>-cp310-abi3-manylinux_2_28_x86_64.whl` |
| 5 | `.specify/2m-pybind.md:30` — §1.1 "Python version range" prose | "v1.0 ships **CPython 3.10 only** as the mandatory wheel … CPython 3.11 / 3.12 / 3.13 wheels are built best-effort … only `cp310` is mandatory" | "v1.0 ships a **single abi3 (stable-ABI) wheel** covering CPython 3.10–3.13+ (the `cp310-abi3` tag: `cp310` = Py_LIMITED_API floor). Per-version `cp3XX-cp3XX` wheels are retired to a documented fallback used only if the runtime cross-version import proves flaky." |
| 6 | `.specify/2m-pybind.md:110` — non-goal #2 (PyPy context) | "v1.0 ships CPython 3.10 wheels only" | "v1.0 ships a single abi3 wheel covering CPython 3.10–3.13+" (PyPy-out rationale unchanged) |
| 7 | `.specify/architecture.md:468` — `[arch §7.1]` build-output **table row** | `\| Python wheel \| fixpp-<ver>-cp310-cp310-manylinux_2_28_x86_64.whl (mandatory) \| best-effort \|` | `\| Python wheel \| fixpp-<ver>-cp310-abi3-manylinux_2_28_x86_64.whl (mandatory) \| best-effort \|` |
| 8 | library `CLAUDE.md` "Active work" line (**non-normative status pointer**) | "per-version manylinux_2_28 wheels (CPython 3.10–3.13, `[2m §1.1]`) via cibuildwheel …; abi3 + Windows … deferred" | "single abi3 manylinux_2_28 wheel covering CPython 3.10–3.13+ via cibuildwheel …" (status pointer, refreshed at `/implement`) |

**User-ratified at Gate A.** Items 1–2/2b/7 are the normative-index + table-row sites
Codex's round-1 enumeration missed; items 3–6 are the same-doc `2m-pybind.md` mirrors A-1/A-2
did not reach by section number; item 8 is the live non-normative pointer.
