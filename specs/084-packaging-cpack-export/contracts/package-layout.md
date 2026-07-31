# Contract — Package Layout, Metadata, and Attribution

**Feature**: 084-packaging-cpack-export · **Date**: 2026-07-31

What ships, where it lands, and what the package must legally carry.

---

## 1. Package model

**One dev-shaped package per configuration** (user decision, 2026-07-31). Each carries headers + static libraries + CMake package config + dictionaries + attribution.

**There is no separate binary-only runtime package before v1.0.** The anchor doc describes one as "shared libs, no headers", but every C++ target is STATIC except a single shared C-ABI library — such a package would hold one file. Adding shared variants is an ABI decision and an explicit non-goal (A-1 holds the `0→1` freeze).

| Platform | Toolchain | Configurations | Formats |
|---|---|---|---|
| Linux | clang | Release, Debug | DEB, RPM, TGZ |
| Linux | gcc | Release, Debug | DEB, RPM, TGZ |
| Windows | MSVC | Release, Debug | ZIP |

Windows is **ZIP only** for v1.0 (user decision). An installer-format seam is documented but not built (FR-016).

---

## 2. Installed layout

| Content | Location | Source |
|---|---|---|
| Public headers | standard include dir | `install(DIRECTORY include/)` — `CMakeLists.txt:321` |
| Generated typed headers | standard include dir | `CMakeLists.txt:346`, **denylist-filtered** at `:351-355` |
| Exported static libraries | standard library dir | New `install(TARGETS ... EXPORT)` |
| Package config | standard CMake package dir | New — `find_package(fixpp)` must locate it with no consumer hints |
| FIX dictionaries | data dir | **New (FR-018a)** — no install rule exists today |
| Attribution set | doc dir | **New (FR-018b)** |
| Debug symbol files | alongside libraries | **Windows only (FR-019)** |

**Excluded from every package** (FR-013): test executables, build-system scratch, and every denylisted generated artifact (`messages/`, `groups/`, `validators/`, `all.hpp`, `groups.hpp`).

---

## 3. Why dictionaries ship

The package exposes `fixpp::dict::load_any(path, ...)` — a public runtime dictionary loader taking a **filesystem path**. Today `dictionaries/` has **no install rule at all** (verified: zero matches), so a package would ship that API with none of its data.

**FR-018a** installs the dictionaries. **SC-014** proves the pairing is usable: a consumer must load a shipped dictionary through the public API using only paths inside the installed prefix — co-location is not sufficient evidence.

---

## 4. Attribution — two obligations, not one

The vendored dictionaries carry an upstream license imposing **two separate requirements**. Shipping the license file discharges only the first.

| # | Obligation | Discharged by | Status |
|---|---|---|---|
| 1 | Reproduce the copyright notice, conditions, and disclaimer in the distribution | Shipping `dictionaries/QUICKFIX_LICENSE.txt` | File exists |
| 2 | Include the upstream's acknowledgment sentence in end-user documentation | A **`NOTICE`** file carrying it verbatim | **Does not exist — this feature creates it** |

> **The trap this contract exists to prevent.** The upstream license file *states* requirement 2 as a condition; it does not *satisfy* it. A package shipping only the license text leaves obligation 2 undischarged while appearing fully attributed.

**Metadata**: the declared package license is the project's own (AGPL-3.0, constitution Article V §1). Package **descriptions** must state third-party engine compatibility as fact and must never imply endorsement or affiliation (FR-018c) — the upstream license forbids using its names to promote derived products. This constrains DEB/RPM description wording, which is otherwise easy to write carelessly.

**Scope limit — this contract does not close REMAINING-WORK item 15d.** Whether the acknowledgment clause is an additional restriction incompatible with AGPL-3.0 is pending counsel review. Shipping the dictionaries makes that question *live*; it does not answer it. Nothing here is legal clearance. Because 15d gates publishing and nothing is published, delivery is not blocked (spec Assumption 10).

---

## 5. Naming and provenance

Names encode product, version, platform, toolchain, configuration, and are unique across the matrix (FR-017).

Each artifact carries **provenance**: the configuration it was built from and the source revision (FR-021a).

> **Why provenance is load-bearing here.** The build strategy deletes each build tree after packaging (Assumption 5) while finished artifacts are deliberately preserved *past* that deletion (FR-021). Those two rules together maintain a directory of packages from earlier configurations and earlier source states — exactly the input that would let a witness report green against a package predating the change under test. Any witness must consume a current-build package and **fail on provenance mismatch**.

---

## 6. Staging

CPack stages into a directory **inside the build tree**, so deleting the tree removes the staged files automatically. `CMAKE_INSTALL_PREFIX` must **never** point at a system location (FR-020) — that is what makes the automatic cleanup hold, and violating it would silently pollute the host.

Finished artifacts are copied outside every build tree (FR-021).

---

## 7. Debug information — the platform asymmetry

| | Linux | Windows |
|---|---|---|
| Where debug info lives | **Inside** the archive members (DWARF) | **Separate** symbol files |
| Release | Never generated (no `-g`) — nothing to strip | Not shipped |
| Package consequence | Debug archives are large by construction | Debug packages **must** ship the separate symbol files or the libraries are undebuggable |

Measured: `libfixpp_core.a` is 13 KB with 0 debug sections in Release, 228 KB with 4 in Debug. Neither the archiver nor the linker strips anything — the compiler simply never emits debug information in Release.

**FR-019** requires the Windows symbol files. It has no Linux counterpart, and the exact artifact naming must be **verified against real toolchain output during implementation**, not assumed.

---

## 8. Verification stance

Every content guarantee in this contract is verified by **enumerating produced package contents** — never by reading install rules (FR-018d, SC-004, SC-013).

An install rule whose pattern matches nothing produces a package that is missing content while looking entirely correct in the build system. For the attribution set that failure mode is a legal deficiency, not a cosmetic one.
