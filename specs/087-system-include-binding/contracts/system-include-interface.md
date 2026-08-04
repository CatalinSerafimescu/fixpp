# Contract — installed system include interface, per exported target

**Feature**: 087-system-include-binding · **Date**: 2026-08-04 · **Issue**: #234

**This file is the single authority for the 087 mechanism.** Anything in `spec.md`, `plan.md`, `tasks.md`,
`research.md` or `quickstart.md` that contradicts it is stale and must be corrected here first, then swept in
the *same commit*. (086 spent four Gate B rounds discovering that fixing a mechanism without sweeping its
specification merely relocates the defect.)

---

## 1. What this contract binds

For a consumer of the **installed** package that runs `find_package(fixpp REQUIRED)` and one
`target_link_libraries` line and nothing else, the **complete** set of include directories CMake supplies to
that consumer's compilation, each with its system/non-system classification.

| linked target | expected include set (relative to the install prefix) | count |
|---|---|---|
| `fixpp::capi` | `include/capi` *(system)* | **1** |
| `fixpp::service` | `include/service-iface` *(system)*, `include/capi` *(system)* | **2** |

Both sets are **closed** — exact equality, not containment (§3). Measured on Linux/clang and MSVC/Conan;
identical on both (`research.md` R4, R6).

### 1a. What this contract does NOT bind

- **Targets other than the two named.** `fixpp::fixpp` legitimately carries the whole tree plus six
  third-party roots; that is the umbrella's purpose, and 086's `probe_umbrella` already pins its reachability.
- **Compiler and SDK search paths.** These never appear in the observation — on Linux the compiler supplies
  its own, on MSVC they arrive via the `INCLUDE` environment variable from `vcvars64.bat`. Neither is
  CMake-supplied, so neither is in scope. *(Measured, not assumed — R2 and R6.)*
- **Header search ORDER as a behavioural claim.** The expected sets are written in observed order and compared
  as sets; ordering effects on header resolution are not asserted. Recorded as a limitation, not closed.

---

## 2. The instrument

**CMake File API, `codemodel-v2`.** For each target, the reply's `compileGroups[].includes[]` gives
`{path, isSystem}`.

Chosen because it reports **CMake's own model** of the include interface. It therefore needs no
compiler-specific command-line parsing (`-I` / `/I` / `-isystem` / `/external:I`) and no compiler invocation.

**Rejected alternatives**, with the reason each fails:

| alternative | why rejected |
|---|---|
| `$<TARGET_PROPERTY:tgt,SYSTEM_INCLUDE_DIRECTORIES>` via `file(GENERATE)` | **The vacuous form.** No documented *collected* consumer property exists, so it yields empty **by construction rather than by measurement** — this is precisely why 086 declined to write this leg |
| parse `compile_commands.json` | re-introduces per-compiler flag spelling, the exact thing this instrument avoids |
| invoke the compiler with `-v` and diff the search path | measures the *compiler*, not the *package interface*; maximally platform-specific |

### 2a. The query must precede configure

A reply exists only if `.cmake/api/v1/query/codemodel-v2` was present when CMake configured the sub-build.
The driver (`run_consumer_witness.cmake`) performs that configure, so **the driver writes the query file
first**. `tests/consumer/CMakeLists.txt` cannot: it executes *during* the configure it would be requesting a
reply for.

**Consequence, and it is the load-bearing one:** the realistic failure is that **no reply exists at all**, not
that a reply is partially populated. A missing reply MUST be `FATAL_ERROR` naming the file (§3, C-2). Reading
absence as "no includes" would reproduce the vacuity this feature exists to remove.

---

## 3. Comparison rules

- **C-1 Exact set equality.** For each leg, the observed set MUST equal the declared set exactly:
  - observed ∖ expected ≠ ∅ ⇒ **LEAK** — fail, naming each offending entry;
  - expected ∖ observed ≠ ∅ ⇒ **DROP** — fail, naming each missing entry;
  - same path, differing `isSystem` ⇒ **RECLASSIFIED** — fail.

  Containment is explicitly insufficient: it cannot detect a DROP, which is half of what 086's C-3 claims.
- **C-2 Absence is fatal.** A missing reply directory, a missing per-target reply, or a reply that does not
  parse MUST fail with a diagnostic naming the missing artifact. It MUST NOT be treated as an empty set.
- **C-3 Prefix-relative.** Both sides are compared with the install prefix stripped. The File API emits
  forward slashes on **both** platforms, so only the expected side needs constructing with `/`.
- **C-4 The expectation is a literal with an origin.** Declared in the tree with a rationale per member.
  Nothing may derive it from the observation it checks — such a comparison is satisfied by whatever the run
  produced, the same no-op shape as a `file(GENERATE)` nothing reads.
- **C-5 The reply is located by glob.** `target-<name>-*.json`; reply names carry a content hash and change
  between configures.
- **C-6 The carrier is required by name.** The probe target(s) this gate reads MUST appear in
  `run_consumer_witness.cmake`'s `_required_targets`, so deletion fails the build rather than silently
  reducing coverage. Ninja reports this as `ninja: error: unknown target '<name>'` — **measured**; the
  Makefile generators' "No rule to make target" phrasing never appears in this project.

---

## 4. Relationship to 086 C-3

086's C-3 says: *"nothing but the include path and the enumerated, unreachable definition set is withheld"*,
and records that of the four properties `$<LINK_ONLY:>` withholds, its instrument binds three —
`INTERFACE_SYSTEM_INCLUDE_DIRECTORIES` explicitly **not** among them.

**This contract binds the fourth.** On delivery, 086's C-3 scope note is amended to say the property is bound
by 087 rather than open, and `spec.md` FR-009a plus `checklists/abi.md` CHK006 follow (FR-011).

What the amendment must **not** claim: 086's §1 reachability matrix still covers system include directories
only at its two named header boundaries. 087 binds the *include set of the two consumer targets*; it does not
turn the reachability matrix into a general system-path assertion. The amendment states exactly which legs
are bound and by what instrument, and leaves the matrix's scope note intact.

---

## 5. Demonstrated-red obligations

The gate ships only with each of these observed, recorded with exit status and first diagnostic:

| # | cause | how induced | expected |
|---|---|---|---|
| 1 | **vacuity proof** | assert a deliberately wrong expectation *before* the correct one | red — proves the gate reads real data |
| 2 | **leak** | revert the C-ABI isolation | red; observed moves **1 → 7**, naming the six third-party roots |
| 3 | **drop** | remove an expected entry from the observed side | red — reachable only because C-1 asserts equality |
| 4 | **missing reply** | delete the reply file | red, naming the file — **not** "no includes" |
| 5 | **carrier deleted** | remove the carrier target | build fails by name |
| 6 | **service leg** | revert the service `$<INSTALL_INTERFACE:>` **alone** | red, **with same-run evidence the capi leg stayed isolated** — reverting capi reds both legs and proves nothing about service (086 FR-011e) |
| — | **controls** | all restored | green, both legs |
