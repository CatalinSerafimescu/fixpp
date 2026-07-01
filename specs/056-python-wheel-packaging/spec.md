# Feature Specification: Python Wheel Packaging (PY-005)

**Feature Branch**: `056-python-wheel-packaging`  
**Created**: 2026-06-30  
**Status**: Draft  
**Input**: User description: "PY-005"

## Overview

PY-005 is the final feature of the Python-bindings workstream: it turns the
already-built, frozen Python binding (PY-001..004 — the SWIG extension, GIL
discipline, typed exceptions, and the pure-Python OO layer) into a **standalone,
pip-installable binary wheel** and wires its production into CI. It is
release-engineering only — it adds the packaging and install-verification layer
and changes **no** C-ABI surface and **no** binding *behaviour* (the `0→1` GA
freeze is already held and is independent of this feature). The one bounded
exception is a limited-API build adaptation required to ship a stable-ABI (abi3)
wheel: a ~10–30-line limited-API rework of a single `fixpp.i` helper plus
`CMakeLists.txt` build flags, explicitly permitted and behaviour-verified
(FR-012 / SC-007) — the C-ABI surface and binding behaviour stay frozen.

The constitution makes a Linux x86_64 wheel a **mandatory v1.0 deliverable**
(`[const §IV.3]`), distributed as a **GitHub release asset with no PyPI upload in
v1** (`[const §IV.5]`).

## Normative References

Per `[const §VI.5]`, the exact coverage-index entries that inform this spec:

- `[2m §1.1] Goals / platform matrix` — SWIG CPython extension wrapping only the
  C ABI; **CPython 3.10–3.13 single-interpreter** (covered by one stable-ABI
  wheel — see the abi3 decision below); **manylinux_2_28** wheel via
  `cibuildwheel` + `auditwheel repair`.
- `[2m §11] Hand-off` — CI wheel-build workflow.
- `[2m §10 Q2] Open question (deferred)` — PEP 740 wheel signing. The other
  deferrals cited in this spec live elsewhere: Windows wheel at
  `[2m §2 non-goal #4]` / `[2m §1.1]`; aarch64 at `[2m §10 Q9]`; PEP 703 nogil at
  `[2m §10 Q8]`; sub-interpreters at `[2m §6.1]`.
- `[arch §4.12] / [arch §8] AGPL boundary` — the binding (and its packaging)
  consume only `<fix/c_api.h>`; no engine-internal C++ headers.
- `[arch §7.1]`, as **proposed-amended at Gate A** (see *Proposed inherited-design
  amendments* in plan.md) — mandatory wheel name
  `fixpp-<ver>-cp310-abi3-manylinux_2_28_x86_64.whl` (the single stable-ABI wheel
  covering CPython 3.10–3.13+; replaces the per-version `cp310-cp310` form).
- `[const §IV.3] Distribution Model` — Python bindings ship as a CPython wheel;
  **Linux x86_64 wheel mandatory for v1.0**; Windows wheel best-effort.
- `[const §IV.5]` — v1.0 wheels are attached to GitHub releases; **no PyPI upload
  in v1**.
- `[const §IX.6] Two-tier CI` — Tier-1 (every PR, required to merge) runs the
  Python pytest gate.

## Clarifications

### Session 2026-06-30

- Q: How should the installed wheel expose the public Python API (two current modules `fixpp` + `fixpp_oo`)? → A: Ship as-is — two separate top-level modules; no restructure (respects the frozen PY-001..004 surface).
- Q: How should installed code locate the bundled FIX dictionaries, given the current repo-relative path breaks under an install? → A: Add a small additive pure-Python locator helper that resolves a bundled dict by name from package data.
- Q: Where should the wheel build + install-test run in CI? → A: Tier-1 mandatory merge gate — runs on every PR.

### Session 2026-06-30 (Gate A round 1)

- Q: Per-version cp310–cp313 wheels, or a single abi3 stable-ABI wheel? → A: Ship
  **ONE** `fixpp-<ver>-cp310-abi3-manylinux_2_28_x86_64.whl` (stable ABI /
  `Py_LIMITED_API`, `cp310` = the minimum-interpreter floor, covers CPython
  3.10–3.13 + future 3.14+). Feasibility was confirmed empirically against the
  installed **SWIG 4.2.0** (its limited-API mode emits heap types via
  `PyType_FromSpec`; the generated wrapper compiles under
  `-DPy_LIMITED_API=0x030A0000` with a single limited-API violation in the one
  `fixpp_py_is_main_interpreter` helper, which needs a bounded ~10–30-line rework
  — see research D-3). This **restores the original abi3-first decision**
  (`remaining-work/python-bindings.md:61`); per-version wheels are retired to a
  documented fallback used only if the runtime import path proves flaky. The
  3.10 floor follows from `PyUnicode_AsUTF8AndSize` entering the limited API in
  3.10. (Mooted the round-1 cp-scope fork — all-four-mandatory vs cp310-only —
  by collapsing to one wheel.)

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Clean-machine pip install (Priority: P1)

A Python developer on a stock Linux x86_64 machine — CPython installed, but **no
C/C++ compiler, no SWIG, no Conan, no system fixpp library, no C++ toolchain** —
obtains the released wheel and runs `pip install fixpp-<version>-*.whl`. After
installation they `import fixpp`, build a dictionary, stand up two engines, open
a session, send a FIX 4.4 message, and read a field back from the receive
callback — entirely from Python, with nothing else installed.

**Why this priority**: This is the constitution-mandated v1.0 deliverable
(`[const §IV.3]`). Without a self-contained installable wheel the Python bindings
are not shippable, regardless of how correct the binding layer is. Every other
story exists to produce or guard this one.

**Independent Test**: On a container that has only CPython and pip (no compiler,
no Conan, no system fixpp), install the produced wheel and run the functional
binding test subset; the round-trip must succeed without network access to a
package index for anything but pip's own resolution of the local file.

**Acceptance Scenarios**:

1. **Given** a Linux x86_64 machine running one of CPython 3.10–3.13 with pip and
   **no** compiler/SWIG/Conan/system fixpp, **When** the user runs `pip install`
   against the released wheel file, **Then** installation completes successfully
   and `import fixpp` succeeds.
2. **Given** the wheel is installed, **When** the user runs the documented
   end-to-end example (dictionary load → two engines → session open → outbound
   send → receive callback → scalar field read), **Then** the round-trip
   completes and the read field value matches what was sent.
3. **Given** the wheel is installed, **When** the user constructs a session that
   needs a standard FIX dictionary (FIX44/FIXT11/FIX42/FIX50SP2), **Then** the
   dictionary loads from data bundled inside the package, with no external file
   supplied by the user.
4. **Given** the wheel is the only fixpp artifact present, **When** the extension
   module is loaded, **Then** it resolves all of its symbols from within itself
   (no dependency on a separately installed `libfixpp_capi`, a `libstdc++` newer
   than the manylinux baseline, or any other fixpp shared object).

---

### User Story 2 - CI builds, install-tests, and publishes the wheel (Priority: P2)

The release pipeline builds the wheel from source, verifies it installs and
passes the functional binding suite **against the installed wheel** (not against
the in-tree build directory), and attaches the resulting `.whl` to the GitHub
release. The wheel is normalised to a portable (manylinux) tag so it installs on
any mainstream glibc distribution.

**Why this priority**: A wheel that only the author can build, or that passes only
in the build tree, does not satisfy the deliverable. Automated production +
install-verification is what makes the P1 guarantee credible and repeatable, and
release attachment is the v1 distribution channel (`[const §IV.5]`).

**Independent Test**: Trigger the CI wheel job; confirm it emits a `.whl`
artifact, installs that artifact into a clean environment, runs the functional
subset green against the installed package, and (on a release event) the artifact
is attached to the release.

**Acceptance Scenarios**:

1. **Given** a Tier-1 CI run (the mandatory merge gate), **When** the wheel job
   runs, **Then** it produces the single `fixpp-<ver>-cp310-abi3-*.whl` artifact
   whose platform tag is `manylinux_2_28_x86_64` (not a machine-local
   `linux_x86_64` tag) and whose ABI tag is `abi3` (not a per-version
   `cp3XX-cp3XX` tag).
2. **Given** the freshly produced abi3 wheel, **When** CI installs it into a
   clean venv on **each** of CPython 3.10/3.11/3.12/3.13 and runs the functional
   binding subset, **Then** `import fixpp` and the subset pass against the
   installed package on every interpreter (the cross-version import is the abi3
   feasibility witness — its failure is what trips the documented per-version
   fallback).
3. **Given** a GitHub release is published, **When** the release pipeline runs,
   **Then** the Linux x86_64 wheel is attached to the release as a downloadable
   asset (no upload to PyPI).
4. **Given** the wheel job fails to produce an installable, passing artifact,
   **When** CI evaluates the gate, **Then** the gate fails (a non-installable or
   test-failing wheel does not pass silently).

---

### User Story 3 - Best-effort Windows wheel (Priority: P3)

A Python developer on Windows x86_64 obtains a Windows wheel built by the same
pipeline and installs it the same way. The Windows binding is already built and
green under the MSVC C-ABI build; PY-005's Windows scope is the packaging glue
(producing and DLL-bundling the wheel), exercised on a best-effort CI lane.

**Why this priority**: The Windows wheel is **deferred** per
`[2m §2 non-goal #4]` / `[2m §1.1]` and **best-effort**, not mandatory, per
`[const §IV.3]`. It broadens reach but must
not block the mandatory Linux deliverable, so it is a separable lane that may
trail or be dropped from this feature without affecting the v1.0 deliverable.

**Independent Test**: On the best-effort Windows lane, build the wheel, install it
into a clean Windows Python environment, and run the functional subset against
the installed package.

**Acceptance Scenarios**:

1. **Given** the best-effort Windows lane runs, **When** the wheel is built and
   installed into a clean Windows Python environment, **Then** `import fixpp`
   succeeds and the functional subset passes against the installed wheel.
2. **Given** the Windows lane is unavailable or red, **When** the Linux gate is
   evaluated, **Then** the mandatory Linux deliverable is unaffected (Windows
   does not gate Linux).

---

### Edge Cases

- **Sanitizer/canary-only tests run against a release wheel**: the binding test
  directory contains tests that require an instrumented or special build
  (e.g. the GIL-release canary, which needs a canary compile define, and the
  TSan-lane suppressions artifact). A non-instrumented release wheel cannot
  satisfy these. The install-verification step MUST run only the functional
  subset; the sanitizer/canary tests remain gated by the existing CTest
  sanitizer matrix, not by the installed-wheel run.
- **abi3 (stable-ABI) wheel imports incorrectly on a targeted version**: the
  mandatory deliverable is the single abi3 wheel, validated by an actual
  `import fixpp` + round-trip on each of CPython 3.10/3.11/3.12/3.13. If that
  runtime cross-version import proves flaky (the abi3 wheel imports incorrectly
  on a targeted interpreter), the pipeline MUST fall back to per-CPython-version
  wheels rather than ship a wheel that imports incorrectly on an untested
  interpreter version. The `-fsyntax-only` limited-API check proves the API
  surface but NOT this runtime round-trip, so the per-version fallback trigger is
  the runtime witness, not the compile.
- **Missing bundled dictionaries**: a wheel that omits the standard FIX dictionary
  data would import but fail at runtime when a session needs a dictionary. The
  build MUST include the dictionary data as package data and this MUST be
  verified by the install test loading a dictionary from the package.
- **Stale / non-portable platform tag**: a wheel left with a raw `linux_x86_64`
  tag installs only on the exact build host. The pipeline MUST normalise it to a
  portable manylinux tag.
- **Wheel built but never install-tested**: building succeeds yet the artifact is
  broken on a clean machine. The gate MUST install into a clean environment and
  run the functional subset there, not validate only in the build tree.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The project MUST produce a **single stable-ABI (abi3) binary
  wheel** for Linux x86_64 that installs `fixpp` via `pip install <wheel>` on a
  machine with only a CPython 3.10–3.13 (or future 3.14+) interpreter and pip —
  no compiler, SWIG, Conan, C++ toolchain, or system fixpp present. The mandatory
  named deliverable is `fixpp-<ver>-cp310-abi3-manylinux_2_28_x86_64.whl` (the
  `cp310-abi3` tag: `abi3` = the CPython stable ABI; `cp310` = the minimum
  interpreter floor, covering 3.10–3.13+) — per `[2m §1.1]` and `[arch §7.1]` as
  proposed-amended at Gate A (see *Proposed inherited-design amendments* in
  plan.md). One wheel installs and imports on every targeted interpreter version.
- **FR-002**: The installed wheel MUST be self-contained: the extension module
  MUST resolve all native symbols from within the package, with no runtime
  dependency on a separately installed fixpp shared object or on a C++ standard
  library newer than the manylinux baseline. The wheel build sets
  `-o fixpp/*:with_otel=False` and links OpenSSL statically
  (`-o openssl/*:shared=False`) so `_fixpp.so` carries no external `.so` beyond
  the libc/libpython manylinux baseline. The self-containment witness is that
  **`auditwheel show`'s external-library list is empty** (LAY-3) — this is the
  recorded static-everything decision (Assumptions), not a vendored-deps wheel.
- **FR-003**: The wheel MUST bundle the complete public Python import surface,
  **shipped as-is** as the existing top-level modules. `import fixpp` is the
  user-facing surface: the SWIG-generated `fixpp` proxy already re-exports (via
  its `%pythoncode` glue) the flat functions, the `FixppError`/`Error` hierarchy,
  and the OO classes `Engine`/`Session`/`Message`/`Application`/`Dictionary` from
  the `fixpp_oo` implementation module. Both `fixpp` (+ `_fixpp*.so`) and
  `fixpp_oo` ship as top-level modules; this feature MUST NOT restructure or
  rename them (e.g. into a `fixpp` package) — the frozen PY-001..004 import
  surface is preserved.
- **FR-004**: The wheel MUST bundle the standard FIX dictionary data
  (FIX42 / FIX44 / FIX50SP2 / FIXT11) as package data so that a session requiring
  a dictionary works from a clean install with no user-supplied files.
- **FR-004a**: The package MUST provide a small additive pure-Python locator that
  resolves a bundled dictionary by name (e.g. `"FIX44"`) to a loadable form from
  package data, so installed code locates the bundled dictionaries without
  computing repo-relative or installation-specific paths. To stay consistent with
  the established public surface, the locator MUST be reachable through
  `import fixpp` (e.g. `fixpp.dictionary_path(...)`), re-exported via the same
  additive `%pythoncode` glue that already surfaces the OO classes — not as a new
  separate top-level public import name. It is packaging glue (pure Python over
  package data) and introduces no C-ABI or existing-binding-behaviour change.
- **FR-005**: The Linux wheel MUST carry the portable **`manylinux_2_28_x86_64`**
  platform tag (`[2m §1.1]`, via `auditwheel repair`) so it installs on mainstream
  glibc distributions, not only on the build host.
- **FR-006**: CI MUST build the single Linux x86_64 abi3 wheel from source as a
  **Tier-1 mandatory merge gate** (runs on every PR), install **that one wheel**
  into a clean environment on **each** of CPython 3.10/3.11/3.12/3.13, and run the
  **functional** binding test subset against the **installed** package (not
  against the build tree) on every interpreter. The build collapses to one wheel;
  the install-test stays a per-interpreter matrix (the cross-version import is the
  abi3 feasibility proof).
- **FR-007**: The functional install-verification subset MUST exclude tests that
  require an instrumented or special-define build (the sanitizer matrix and the
  GIL-release canary remain gated by the existing CTest sanitizer lanes, which
  this feature MUST NOT remove or weaken).
- **FR-008**: On a GitHub release event, the pipeline MUST attach the Linux
  x86_64 wheel to the release as a downloadable asset. It MUST NOT upload to PyPI
  in v1.
- **FR-009**: The CI wheel gate MUST fail when the wheel cannot be built, cannot
  be installed into a clean environment, or fails the functional subset — a
  broken artifact MUST NOT pass silently.
- **FR-010**: The **required target is the single stable-ABI (abi3) wheel**
  `cp310-abi3-manylinux_2_28_x86_64` (`[2m §1.1]` / `[arch §7.1]` as
  proposed-amended at Gate A), which collapses the per-version matrix to one wheel
  covering CPython 3.10–3.13+. Per-CPython-version wheels (`cp3XX-cp3XX`) are
  retired to a **documented fallback**, produced **only** if the abi3 wheel's
  runtime cross-version import (FR-006) proves flaky on a targeted interpreter.
  The **compile-time** SWIG limited-API feasibility fallback did not fire — abi3
  is established against the installed SWIG 4.2.0 at the API-surface level (research
  D-3). The **runtime** fallback trigger remains the CI install/import/round-trip
  witness across CPython 3.10–3.13 and MUST fire red before any per-version fallback
  is produced (`[verify at implement]`).
- **FR-011**: A Windows x86_64 wheel is **deferred** per `[2m §2 non-goal #4]` /
  `[2m §1.1]` and **best-effort** per `[const §IV.3]`. A Windows lane MAY be
  provided using the same source and the existing MSVC C-ABI build; it MUST be
  separable such that its absence or failure does not gate the mandatory Linux
  deliverable.
- **FR-012**: This feature MUST NOT change the C-ABI surface (`include/fix/c_api*.h`
  is byte-frozen — the `0→1` GA freeze is untouched) and MUST NOT change any
  binding *behaviour* delivered by PY-001..004 (the frozen behavioural suite,
  including the 055 sub-interpreter witness, stays green). A **bounded limited-API
  build adaptation is explicitly permitted and behaviour-verified**: the
  ~10–30-line limited-API rework of `fixpp_py_is_main_interpreter` in `fixpp.i`
  (which MUST preserve the sub-interpreter-rejection behaviour) plus
  `-DPy_LIMITED_API=0x030A0000` compile flags in `CMakeLists.txt` and the abi3
  wheel tag from scikit-build-core's `wheel.py-api = "cp310"` in `pyproject.toml`. The rework removes `PyInterpreterState_Main()` (not in the
  limited API), which is the runtime sub-interpreter-rejection mechanism (→ code
  1201); the proposed limited-API replacement captures the main interpreter at
  module init and compares `PyInterpreterState_GetID(PyInterpreterState_Get())`
  (both limited-API; `[verify at implement]` — research D-3). **CPython 3.10/3.11
  is the concentrated verify band**: the 055 sub-interpreter witness passed on 3.12
  via CPython's import barrier (3.12+ only) without ever exercising the runtime 1201
  check; 3.10/3.11 have no import barrier, so the reworked runtime check is
  load-bearing and previously-unwitnessed there, and the per-version sub-interpreter
  test on 3.10/3.11 (research D-8) is its realizing witness. Beyond that adaptation
  the feature adds only packaging, dictionary-bundling, and install-verification. (Re-scoped at Gate A — this no
  longer claims "zero binding-source change"; it claims zero C-ABI change + zero
  binding-behaviour change.)

### Key Entities

- **Binding wheel artifact**: the single distributable `fixpp-<ver>-cp310-abi3-*.whl`.
  Attributes: target platform/arch, platform compatibility tag (manylinux_2_28),
  Python ABI compatibility (**abi3 stable ABI**, cp310 floor; per-version is the
  fallback only), bundled contents (extension module + flat module + OO layer +
  dictionary data), self-containment (no external `.so`).
- **Bundled dictionary data**: the standard FIX dictionary files shipped inside
  the package, addressable from installed code via `fixpp.dictionary_path(...)`
  (FR-004a) so a session can load a dictionary with no external file.
- **Public Python import surface**: `import fixpp` — the user-facing surface that
  re-exports (via its existing `%pythoncode` glue) the flat SWIG bindings, the
  exception hierarchy, the OO classes from the `fixpp_oo` implementation module,
  and the new dictionary locator. `fixpp` and `fixpp_oo` ship as top-level modules
  as-is; no package restructure.
- **Wheel CI gate**: the pipeline stage that builds, install-tests, and (on
  release) publishes the wheel; the gating signal for the deliverable.
- **Functional install-verification subset**: the binding tests that can run
  against a non-instrumented installed wheel (the round-trip, smoke, exception,
  lifetime, and OO behaviour tests) — distinct from the sanitizer/canary tests.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: On a clean Linux x86_64 environment containing only CPython and pip
  (no compiler/SWIG/Conan/system fixpp), `pip install <wheel>` followed by
  `import fixpp` succeeds with a zero exit status — for the single abi3 wheel
  installed on **each** of CPython 3.10/3.11/3.12/3.13 (the same wheel file on
  every interpreter).
- **SC-002**: On that same clean environment, the documented end-to-end FIX 4.4
  round-trip example runs to completion and the field read back equals the field
  sent — using only a dictionary resolved through `fixpp.dictionary_path(...)`
  (the bundled-dictionary locator), with no user-supplied dictionary file.
- **SC-003**: The functional binding subset passes (100% of its selected tests)
  when run against the installed wheel in a clean environment, on every Tier-1 PR
  run (the wheel build/install-test is a mandatory merge gate).
- **SC-004**: The single produced Linux wheel's tags are `cp310-abi3` (stable
  ABI, not a per-version `cp3XX-cp3XX` tag) and `manylinux_2_28_x86_64` (not a raw
  `linux_x86_64` tag), verifiable from the wheel filename/metadata — exactly ONE
  wheel, not four.
- **SC-005**: When a GitHub release is published, the Linux x86_64 wheel is
  present as a downloadable release asset, and no artifact is uploaded to PyPI.
- **SC-006**: A deliberately broken wheel (uninstallable, or failing the
  functional subset) causes the CI wheel gate to report failure rather than pass.
- **SC-007**: Compared with the pre-feature state, the C-ABI surface is byte-frozen
  (zero modifications to `include/fix/c_api*.h`; the existing ABI/header-occupancy
  check passes) and the binding **behaviour** is unchanged — the frozen PY-001..004
  behavioural suite runs unchanged and stays green, including the sub-interpreter
  witness after the limited-API helper rework, and a snapshot of the `import fixpp`
  public surface still resolves every existing name/class. The sub-interpreter
  witness is run on **each** of CPython 3.10/3.11/3.12/3.13, with **3.10/3.11 the
  concentrated verify band** — those versions have no import barrier, so the
  reworked runtime 1201 check (replacing `PyInterpreterState_Main()`) is the sole
  rejection mechanism and is load-bearing there (FR-012; research D-3/D-8). The only permitted
  binding-source change is the bounded limited-API build adaptation of FR-012
  (the `fixpp_py_is_main_interpreter` helper + `CMakeLists.txt` flags); the rest of
  the diff is confined to packaging, dictionary-bundling, CI, and install-test code.

## Assumptions

These reflect decisions already taken with the user (2026-06-25 / 2026-06-27,
recorded in `remaining-work/python-bindings.md` and the `project_python_bindings_v1_plan`
note); they are settled and are recorded here rather than re-opened:

- **Distribution model**: native pre-built binary wheels. The SWIG extension
  **statically links** `libfixpp_capi.a` and (on Linux) `-static-libstdc++
  -static-libgcc`, so the wheel is self-contained and the C++ stdlib never
  reaches Python. There is therefore **one Linux wheel** (manylinux/libstdc++);
  there is **no "libc++ wheel"** — Tier 3 (libc++) stays a compile-correctness
  lane, not a wheel target. The wheel matrix axis is OS × arch, not OS × stdlib.
- **Platform matrix** (`[2m §1.1]`, normative): **CPython 3.10–3.13,
  single-interpreter** (sub-interpreters / PEP 703 free-threaded builds deferred),
  **manylinux_2_28**, **x86_64**, via `cibuildwheel` + `auditwheel repair`. The
  mandatory wheel name is `fixpp-<ver>-cp310-abi3-manylinux_2_28_x86_64.whl` — a
  **single stable-ABI wheel** covering 3.10–3.13+ (per `[arch §7.1]` as
  proposed-amended at Gate A; restores the abi3-first decision, per-version
  retired to fallback — see the 2026-06-30 Clarifications).
- **OS scope**: Linux x86_64 is the mandatory v1.0 deliverable. **Windows is
  deferred** (`[2m §2 non-goal #4]` / `[2m §1.1]`) / best-effort (`[const §IV.3]`);
  **macOS and aarch64 are deferred** (aarch64 = `[2m §10 Q9]`, no Tier-4 CI).
  **Sub-interpreters** (`[2m §6.1]`), **PEP 703 nogil**
  (`[2m §10 Q8]`), and **PEP 740 wheel signing** (`[2m §10 Q2]`) are also deferred.
- **Distribution channel**: GitHub release assets; **no PyPI upload in v1**
  (`[const §IV.5]`).
- **Build tooling** (implementation detail, finalised at `/speckit-plan`): the
  intended toolchain is `cibuildwheel` (orchestrator) + a CMake-driven build
  backend (`scikit-build-core`) + `auditwheel repair` (Linux) / `delvewheel`
  (Windows, deferred). The spec constrains observable outcomes (manylinux_2_28
  tag, self-contained, install-tested), not the specific tools.
- **Python ABI**: the required target is a **single abi3 (stable-ABI / limited-API)
  wheel** covering CPython 3.10–3.13+ (FR-010), feasibility confirmed against the
  installed SWIG 4.2.0 (research D-3). Per-version wheels (3.10–3.13) are the
  documented fallback, produced only if the abi3 runtime import proves flaky. This
  restores the original abi3-first decision (`remaining-work/python-bindings.md:61`).
- **Prerequisite binding is complete and frozen**: PY-001..004 (053/054/055) are
  merged; the `0→1` C-ABI freeze is held and is **independent** of this feature
  (PY-005 touches no C-ABI). The dictionary loader, OO layer, GIL discipline, and
  exception translation are all in place and are consumed, not modified.
- **Existing Python CI job**: a `python-bindings` sanitizer matrix already builds
  and tests the binding in-tree under CTest; PY-005 adds a separate wheel
  build/install-test job and leaves the sanitizer matrix intact.

## Dependencies

- **C-ABI surface (CA-001..013)** and the Python binding layer **PY-001..004** —
  all merged; this feature builds on them and modifies neither.
- **Bundled FIX dictionary XMLs** (`dictionaries/FIX42.xml`, `FIX44.xml`,
  `FIX50SP2.xml`, `FIXT11.xml`) — exist in-tree and are bundled as package data.
- **CI infrastructure** — the existing Tier-1/Tier-2 pipeline (Conan + CMake
  presets; the MSVC C-ABI build for the Windows lane) hosts the new wheel job.

## Out of Scope

- Any change to the C-ABI, the SWIG interface behaviour, GIL handling, exception
  translation, or the OO/lifetime layer (delivered and frozen by PY-001..004).
- macOS and aarch64 wheels (deferred — aarch64 = `[2m §10 Q9]`, no Tier-4 CI).
- PEP 740 wheel signing / attestation (deferred — `[2m §10 Q2]`).
- Sub-interpreter (`[2m §6.1]`) and PEP 703 free-threaded
  (`[2m §10 Q8]`) support (deferred).
- PyPI publication (deferred past v1.0 — `[const §IV.5]`).
- A separate "libc++ wheel" (a category error — see Assumptions).
- New FIX dictionaries beyond the four already bundled.
