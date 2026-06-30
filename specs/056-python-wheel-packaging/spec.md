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
and changes **no** binding behaviour and **no** C-ABI surface (the `0→1` GA
freeze is already held and is independent of this feature).

The constitution makes a Linux x86_64 wheel a **mandatory v1.0 deliverable**
(`[const §IV.3]`), distributed as a **GitHub release asset with no PyPI upload in
v1** (`[const §IV.5]`).

## Normative References

Per Article VIII §5, the exact coverage-index entries that inform this spec:

- `[2m §1.1] Goals / platform matrix` — SWIG CPython extension wrapping only the
  C ABI; **CPython 3.10–3.13 single-interpreter**; **manylinux_2_28** wheel via
  `cibuildwheel` + `auditwheel repair`.
- `[2m §11] Hand-off` — CI wheel-build workflow.
- `[2m §10 Q2–Q5] Open questions (deferred)` — PEP 740 wheel signing,
  sub-interpreter support, **Windows wheel**, and aarch64 are all deferred.
- `[arch §4.12] / [arch §8] AGPL boundary` — the binding (and its packaging)
  consume only `<fix/c_api.h>`; no engine-internal C++ headers.
- `[arch §7.1]` — mandatory wheel name
  `fixpp-<ver>-cp310-cp310-manylinux_2_28_x86_64.whl`.
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
   runs, **Then** it produces the cp310–cp313 `fixpp-*.whl` artifacts whose
   platform tag is `manylinux_2_28_x86_64` (not a machine-local `linux_x86_64`
   tag).
2. **Given** a freshly produced wheel, **When** CI installs it into a clean
   environment and runs the functional binding subset, **Then** the subset passes
   against the installed package.
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

**Why this priority**: The Windows wheel is **deferred** per `[2m §10 Q3]` and
**best-effort**, not mandatory, per `[const §IV.3]`. It broadens reach but must
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
- **abi3 (stable-ABI) attempt fails**: if a single stable-ABI wheel covering
  CPython 3.x cannot be produced cleanly, the pipeline MUST fall back to
  per-CPython-version wheels rather than ship a wheel that imports incorrectly on
  an untested interpreter version.
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

- **FR-001**: The project MUST produce, for **each of CPython 3.10, 3.11, 3.12,
  3.13** (`[2m §1.1]` single-interpreter matrix), a binary wheel for Linux x86_64
  that installs `fixpp` via `pip install <wheel>` on a machine with only that
  CPython and pip — no compiler, SWIG, Conan, C++ toolchain, or system fixpp
  present. The mandatory named deliverable is
  `fixpp-<ver>-cp310-cp310-manylinux_2_28_x86_64.whl` and its cp311/cp312/cp313
  siblings (`[2m §1.1]`, `[arch §7.1]`).
- **FR-002**: The installed wheel MUST be self-contained: the extension module
  MUST resolve all native symbols from within the package, with no runtime
  dependency on a separately installed fixpp shared object or on a C++ standard
  library newer than the manylinux baseline.
- **FR-003**: The wheel MUST bundle the complete public Python import surface as
  the **two existing top-level modules, shipped as-is**: `fixpp` (the
  SWIG-generated extension module plus the SWIG-generated flat Python module) and
  `fixpp_oo` (the pure-Python OO layer PY-004 established as the user-facing API).
  This feature MUST NOT restructure or rename those modules (e.g. into a `fixpp`
  package) — the frozen PY-001..004 import surface is preserved.
- **FR-004**: The wheel MUST bundle the standard FIX dictionary data
  (FIX42 / FIX44 / FIX50SP2 / FIXT11) as package data so that a session requiring
  a dictionary works from a clean install with no user-supplied files.
- **FR-004a**: The package MUST provide a small additive pure-Python locator
  helper that resolves a bundled dictionary by name (e.g. `"FIX44"`) to a
  loadable form from package data, so installed code locates the bundled
  dictionaries without computing repo-relative or installation-specific paths.
  The helper is packaging glue (pure Python over package data) and introduces no
  C-ABI or binding-behaviour change.
- **FR-005**: Each Linux wheel MUST carry the portable **`manylinux_2_28_x86_64`**
  platform tag (`[2m §1.1]`, via `auditwheel repair`) so it installs on mainstream
  glibc distributions, not only on the build host.
- **FR-006**: CI MUST build the Linux x86_64 wheel from source as a **Tier-1
  mandatory merge gate** (runs on every PR), install the produced wheel into a
  clean environment, and run the **functional** binding test subset against the
  **installed** package (not against the build tree).
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
- **FR-010**: The **baseline and named deliverable is per-CPython-version wheels**
  for 3.10–3.13 (`cp3XX-cp3XX-manylinux_2_28_x86_64`, `[2m §1.1]`). A single
  stable-ABI (abi3) wheel MAY be attempted as an optimisation (collapsing the
  four to one), but only if it loads correctly on every targeted version;
  because abi3 changes the `[2m §1.1]`-mandated wheel name, per-version remains
  the required target and abi3 is an optional substitution, not the primary path.
- **FR-011**: A Windows x86_64 wheel is **deferred** per `[2m §10 Q3]` and
  **best-effort** per `[const §IV.3]`. A Windows lane MAY be provided using the
  same source and the existing MSVC C-ABI build; it MUST be separable such that
  its absence or failure does not gate the mandatory Linux deliverable.
- **FR-012**: This feature MUST NOT change the C-ABI surface, the SWIG binding
  behaviour, or any binding-layer logic delivered by PY-001..004; it adds only
  packaging, dictionary-bundling, and install-verification.

### Key Entities

- **Binding wheel artifact**: the distributable `fixpp-*.whl`. Attributes:
  target platform/arch, platform compatibility tag (manylinux), Python ABI
  compatibility (abi3 or per-version), bundled contents (extension module + flat
  module + OO layer + dictionary data), self-containment.
- **Bundled dictionary data**: the standard FIX dictionary files shipped inside
  the package, addressable from installed code via the locator helper (FR-004a) so
  a session can load a dictionary with no external file.
- **Public Python import surface**: the two top-level modules shipped as-is —
  `fixpp` (the flat SWIG bindings) and `fixpp_oo` (the pure-Python OO API that is
  the intended user-facing layer). No package restructure.
- **Wheel CI gate**: the pipeline stage that builds, install-tests, and (on
  release) publishes the wheel; the gating signal for the deliverable.
- **Functional install-verification subset**: the binding tests that can run
  against a non-instrumented installed wheel (the round-trip, smoke, exception,
  lifetime, and OO behaviour tests) — distinct from the sanitizer/canary tests.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: On a clean Linux x86_64 environment containing only CPython and pip
  (no compiler/SWIG/Conan/system fixpp), `pip install <wheel>` followed by
  `import fixpp` succeeds with a zero exit status.
- **SC-002**: On that same clean environment, the documented end-to-end FIX 4.4
  round-trip example runs to completion and the field read back equals the field
  sent — using only a dictionary resolved through the package's bundled-dictionary
  locator helper, with no user-supplied dictionary file.
- **SC-003**: The functional binding subset passes (100% of its selected tests)
  when run against the installed wheel in a clean environment, on every Tier-1 PR
  run (the wheel build/install-test is a mandatory merge gate).
- **SC-004**: Each produced Linux wheel's platform tag is `manylinux_2_28_x86_64`,
  verifiable from the wheel filename/metadata (not a raw `linux_x86_64` tag), for
  each of cp310/cp311/cp312/cp313.
- **SC-005**: When a GitHub release is published, the Linux x86_64 wheel is
  present as a downloadable release asset, and no artifact is uploaded to PyPI.
- **SC-006**: A deliberately broken wheel (uninstallable, or failing the
  functional subset) causes the CI wheel gate to report failure rather than pass.
- **SC-007**: Compared with the pre-feature state, the C-ABI surface and the
  binding-layer behaviour are unchanged (zero modifications to C-ABI headers and
  to PY-001..004 binding logic); the feature's diff is confined to packaging,
  dictionary-bundling, CI, and install-test code.

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
  mandatory wheel name is `fixpp-<ver>-cp310-cp310-manylinux_2_28_x86_64.whl`
  (and its cp311/cp312/cp313 siblings) per `[arch §7.1]`.
- **OS scope**: Linux x86_64 is the mandatory v1.0 deliverable. **Windows is
  deferred** (`[2m §10 Q3]`) / best-effort (`[const §IV.3]`); **macOS and aarch64
  are deferred** (`[2m §10 Q4]`, no Tier-4 CI). Sub-interpreters and PEP 740 wheel
  signing are also deferred (`[2m §10 Q2/Q5]`).
- **Distribution channel**: GitHub release assets; **no PyPI upload in v1**
  (`[const §IV.5]`).
- **Build tooling** (implementation detail, finalised at `/speckit-plan`): the
  intended toolchain is `cibuildwheel` (orchestrator) + a CMake-driven build
  backend (`scikit-build-core`) + `auditwheel repair` (Linux) / `delvewheel`
  (Windows, deferred). The spec constrains observable outcomes (manylinux_2_28
  tag, self-contained, install-tested), not the specific tools.
- **Python ABI**: the baseline is **per-version wheels** (3.10–3.13) matching the
  `[2m §1.1]` named deliverable; a single abi3 wheel is an optional optimisation
  that may be attempted but is not the required target (FR-010).
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
- macOS and aarch64 wheels (deferred — `[2m §10 Q4]`, no Tier-4 CI).
- PEP 740 wheel signing / attestation (deferred — `[2m §10 Q2]`).
- Sub-interpreter and PEP 703 free-threaded support (deferred — `[2m §1.1]`,
  `[2m §10 Q5]`).
- PyPI publication (deferred past v1.0 — `[const §IV.5]`).
- A separate "libc++ wheel" (a category error — see Assumptions).
- New FIX dictionaries beyond the four already bundled.
