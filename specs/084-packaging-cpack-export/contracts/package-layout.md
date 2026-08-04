# Contract — Package Layout, Metadata, and Attribution

**Feature**: 084-packaging-cpack-export · **Date**: 2026-07-31

What ships, where it lands, and what the package must legally carry.

---

## 1. Package model

**One dev-shaped package per configuration** (user decision, 2026-07-31). Each carries headers + static libraries + CMake package config + dictionaries + attribution.

**There is no separate binary-only runtime package before v1.0.** The anchor doc describes one as "shared libs, no headers", but every *compiled library* target is STATIC except `fixpp_capi_shared` — which is itself gated on `FIXPP_BUILD_TESTS` (`src/capi/CMakeLists.txt:114-115`), so a packaging build with tests OFF has **zero** SHARED targets. Such a package would hold one file or none. Adding shared variants of the core C++ targets is an explicit non-goal — *not* because any ABI freeze is being held (that freeze is **CLOSED**, `REMAINING-WORK.md:7`, and it governs the C ABI, not these targets) but because those targets have **no freeze mechanism at all**: no version script, no header-hash baseline, no symbol golden. See `spec.md` → Explicit non-goals.

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
| Public headers | standard include dir | `install(DIRECTORY include/)` — `CMakeLists.txt:321-324`, **unfiltered today: the entire `include/` tree**. The §2a sign-off adds exactly one filter — the two test-support subtrees (`PATTERN` exclusions this rule does not carry today) |
| Generated typed headers | standard include dir | `CMakeLists.txt:346-348`, filtered by the **7-pattern** exclusion set at `:349-355`. Note the destination is `${CMAKE_INSTALL_INCLUDEDIR}` — **the same as the row above** |
| Exported static libraries | standard library dir | New `install(TARGETS ... EXPORT)` |
| Package config | standard CMake package dir | New — `find_package(fixpp)` must locate it with no consumer hints |
| FIX dictionaries | data dir | **New (FR-018a)** — no install rule exists today |
| Attribution set | doc dir | **New (FR-018b)** |
| Debug symbol files | alongside libraries | **Windows only (FR-019)** |

> ### The shipped OBJECT-library files — KEEP, decided 2026-08-02 (user)
>
> Every package carries `lib/objects-<CONFIG>/fixpp_capi_objects/*.{o,obj}` — 11 files duplicating
> content already inside the capi archive. **Measured cost: ~21 MB in the MSVC Debug ZIP (about half
> of it), ~1.2 MB on Linux Release.** The Debug figure is far larger than the Linux one the cost was
> originally accepted on.
>
> **They are dead weight on the consumption path — no consumer links them.** Measured on both
> platforms: the `consumer_capi_witness` link line carries `libfixpp_capi.a` / `fixpp_capi.lib` and
> **zero** loose objects, with no reference to `objects-*` anywhere in either consumer build. The
> reason is a CMake asymmetry worth stating, because it is easy to get backwards: a **non-imported**
> OBJECT library propagates its objects through `target_link_libraries` (since 3.12); an **IMPORTED**
> one does not. Its objects arrive only via an explicit `$<TARGET_OBJECTS:…>`, which no consumer
> writes. So `fixpp::capi` propagates include directories and transitive links from
> `fixpp::capi_objects` while the archive supplies every symbol.
>
> **⚠️ They nonetheless CANNOT be deleted.** The generated `fixppTargets.cmake` ends with an existence
> check over `_cmake_import_check_files_for_fixpp::capi_objects`; removing the files makes
> `find_package(fixpp)` **`FATAL_ERROR` at configure time for every consumer**. Stripping them trades
> dead weight for a broken package.
>
> Both facts follow from `fixpp_capi_objects` being an export-set member, which is forced:
> `fixpp_capi` has no sources and links it (`src/capi/CMakeLists.txt:94-96` — `PUBLIC` when 084 wrote
> this, `PRIVATE` + a `$<BUILD_INTERFACE:>` `PUBLIC` entry since 086; the export-set consequence is
> unchanged either way, because `$<LINK_ONLY:>` entries are export requirements too), and
> `install(TARGETS)` on an OBJECT library **mandates** `OBJECTS DESTINATION`. The only way to drop
> them is to keep the OBJECT library out of the export closure — `fixpp_capi` owning the capi sources
> directly — at the cost of `fixpp_capi_shared` compiling its own copy. **Decided: not worth it for
> v1.0; keep and document.** Revisit if Debug package size becomes a distribution constraint.
>
> ### Reconciled by 086 — the C-ABI include path, and this section's citation set (FR-015)
>
> **The reasoning above stands; one of its premises no longer does.** This section was written when
> `fixpp_capi` had *no include directories of its own and reached everything through
> `fixpp_capi_objects`*, and `architecture.md` cited exactly that arrangement as the reason narrowing
> `fixpp::capi` would contradict D1 Option A. 086 narrowed it without contradicting D1: the **link** edge is
> kept and only the **usage requirements** are withheld, via `$<LINK_ONLY:>`. `fixpp_capi` now carries
> `$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/capi>`, and the export set is unchanged — **re-measured
> on a real generate run: 18 members, 11 shipped object files**, with the `_cmake_import_check_files_…`
> hazard above intact and exercised (`find_package` verified for all five by-name members).
>
> **The citation drift was NOT a constant, which is why no offset could be applied blind.** Re-verified as a
> **set** against the tree at 086, not spot-checked:
>
> | citation | was | now | why it moved |
> |---|---|---|---|
> | `fixpp_capi` STATIC | `:43` | `:44` | +1, from an insertion above it |
> | the `fixpp_capi_objects` link edge | `:45` | `:94-96` | 086 rewrote it; **the claim changed, not just the number** |
> | `fixpp_tap` PUBLIC edge | `:36` | `:37` | a **separate** error — this one was never explained by the +1 |
> | `fixpp_capi_shared` test gate | `:47-48` | `:114-115` | 086's comment block |
> | `WINDOWS_EXPORT_ALL_SYMBOLS` | `:70` | `:137` | 086's comment block |
> | `fixpp_capi_objects` OBJECT | `:11` | `:11` | already correct |
> | `architecture.md` §8 plugin-surface quote | `:523` | `:566` | 086's §7.4/§8 rewrite |
>
> Two `architecture.md` clause labels in this file — `:503` and `:509` — are **not** live line citations;
> they are the clause identifiers 084's reconciliation table uses, and they keep their original meaning. The
> §7.4 prose they name has since moved and been rewritten (086/FR-013); references to it are now by
> **section**, not by line, so this class of drift cannot recur here.
>
> ### Internal prefix — a PLATFORM ASYMMETRY, measured 2026-08-02
>
> The locations above are deliberately abstract ("standard include dir"). Concretely they sit under a
> **`/usr` prefix on Linux only**:
>
> | Generator | Internal layout |
> |---|---|
> | DEB | `./usr/lib/…` |
> | RPM | `/usr/lib/…` |
> | TGZ | `<package-name>/usr/lib/…` |
> | **ZIP (Windows)** | `<package-name>/lib/…` — **no `usr/`** |
>
> DEB and RPM install into the system tree and need the prefix. The archive generators do not: the
> consumer extracts them wherever they like and points `CMAKE_PREFIX_PATH` at that directory.
>
> **This was a defect until it was measured.** `CPACK_PACKAGING_INSTALL_PREFIX "/usr"` was set
> unconditionally, so the Windows ZIP shipped `fixpp-0.0.1-windows-msvc-release/usr/lib/fixpp_core.lib`
> — an FHS path that means nothing on Windows — with a POSIX permission set applied beneath it. Now
> guarded to `NOT WIN32`.
>
> **Consequence for any content check**: normalise the `usr/` component away before comparing, and
> never anchor a `GLOB_RECURSE` on it. A pattern with intermediate literal directories must match at
> an *exact depth*, so a `usr/`-anchored glob finds nothing on Windows and reports "the package
> carries no X" — a defect claim about the product, manufactured by the test. Archive naming is
> likewise toolchain-dependent: `libfixpp_core.a` / `.o` under GNU and Clang, `fixpp_core.lib` /
> `.obj` under MSVC.

**Excluded from every package** (FR-013): test executables, build-system scratch, every denylisted generated artifact — and, per the §2a sign-off, the two **test-support header** subtrees (`include/fixpp/core/test/`, `include/fixpp/transport/test/`), which FR-013's "test **executables**" wording did not previously reach. **The exclusion reference is the full 7-pattern set** at `CMakeLists.txt:349-355` — `_dispatch`, `vt11`, `messages`, `groups`, `validators`, `all.hpp`, `groups.hpp` — asserted as **set equality**, not as a subset (FR-009). *(The last five are the 078 tail; `_dispatch/` is the build-tree-private reify bridge and `vt11/` is FIXT.1.1, outside the public v42/v44/v50sp2 set. A "must be absent" check written from the 078 five would pass a package leaking either of the first two while looking coherent.)*

---

## 2a. Shipped-header disposition — every `include/<subtree>` (FR-012a, SC-009a)

`CMakeLists.txt:321-324` installs the **entire** `include/` tree unconditionally. "A shipped header with no exported library behind it" is therefore a **class**, not the single C-ABI instance D1 surfaces. An *exclude* disposition is a **deliberate change in delivered content** and must be recorded as such, never allowed to happen silently.

> ### ✅ CLOSED at Gate A sign-off (user, 2026-08-01) — **no row reads `OPEN`**
>
> All seven routed rows resolve. **D1 = Option A**: export the **static** `fixpp_capi`, so every shipped header has a library behind it. The FR-012a class resolves the same way — `fixpp_config_toml`, `fixpp_tap` and `fixpp_service` are exported, and `include/fixpp/otel/` ships — with **one** deliberate exclusion: the two test-support subtrees. Coherent with `.specify/architecture.md` §8, which already names `fixpp::capi` the C-ABI consumer target (`:503`) and `include/fixpp/service/` part of the public C++ plugin surface (`:523`).
>
> **⚠️ The export members this decision adds are DERIVED, not measured.** The eleven-member set in [`export-set.md` §2](./export-set.md) is the output of an executed generate run; these additions are the output of *reading* `target_link_libraries` — which is exactly the method round 1 caught being wrong in three places across a three-level cascade. Reading them out gives **at least six** new members (`fixpp_capi`, `fixpp_capi_objects`, `fixpp_config_toml`, `fixpp_log_otlp`, `fixpp_tap`, `fixpp_service`), two of which the reading only surfaced on a second pass. **Re-running the `install(TARGETS … EXPORT …)` + generate experiment once these are wired is an implementation obligation**, not an optional check — see `export-set.md` §2a.
>
> **One exclusion changes an install rule this bundle elsewhere calls unfiltered.** Excluding the test-support subtrees means `CMakeLists.txt:321-324` acquires `PATTERN … EXCLUDE` clauses where today it has none. Every description of that rule as "unconditional/unfiltered" describes the *current* state and must be read against this decision.

> ### ⚠️ `OPEN` is NOT a disposition — read this before citing SC-009a
>
> *(Retained as the record of the round-2 repair. Every row below now carries `Export` or `Exclude`, so the criterion is satisfiable; the marker's semantics are kept because a future row added without a disposition must still fail.)*
>
> *(Added at Gate A round 2. The round-1 table used `OPEN` as if it were a cell value, and SC-009a checked only that every cell was **non-empty** — so SC-009a passed over a table in which **seven rows were undecided**. A gate that cannot fail on the defect it exists to catch is the `feedback_ci_gate_observes_not_asserts_witness_skips_into_green` / `feedback_completeness_gate_exact_set_not_subset` shape this bundle names correctly elsewhere and instantiated here.)*
>
> **FR-012a admits exactly two dispositions: `export` or `exclude`.** `OPEN` is neither. It marks a **routed, gate-blocking decision** — a row whose disposition this gate owes and has not yet given. Every `OPEN` row below therefore MUST name (i) **the decision it is routed to** — `D1`, or the `FR-012a` class decision — and (ii) **the options between which that decision chooses**. A row that reads `OPEN` without a named, existing open decision is not a routed row; it is an omission, and the table is malformed.
>
> **The table is not COMPLETE while any row reads `OPEN`.** SC-009a is tightened accordingly (`spec.md`): it asserts `disposition ∈ {export, exclude}` for every row, with `exclude` additionally carrying the delivered-content-change record — **so SC-009a cannot pass while any row reads `OPEN`**. That makes the criterion falsifiable without pre-deciding anything: closing the rows is the gate's job, and SC-009a is what proves the gate did it.
>
> **What this does NOT do.** It does not disposition the rows. D1 and the FR-012a class were deliberately routed to Gate A at round 1 and remain the gate's to decide; a rewriter filling these cells in would pre-decide them. The change here is to the *semantics of the marker and the strength of the check*, not to any row's answer.

> **Granularity rule — stated so the table's exhaustiveness is checkable.** Rows are at **module** granularity (`include/fixpp/<module>/`, plus `include/fix/`). A nested subtree — `core/sync/`, `core/sync/detail/`, `session/detail/`, `session/quickfix_compat/`, `transport/test/`, … — **inherits its module's disposition** unless it is broken out as its own row below. Only subtrees whose disposition differs from their parent's are broken out. A future emitter or a new nested directory therefore needs no new row *unless* its disposition differs — but if it does differ and no row exists, that subtree has **no** disposition in `{export, exclude}`, and SC-009a must be read as **failing**.

| Shipped subtree | Backing target | In export set? | Disposition |
|---|---|---|---|
| `include/fixpp/core/` (incl. `core/sync/`) | `fixpp_core`, `fixpp_sync` | Yes | Export |
| `include/fixpp/wire/` | `fixpp_wire` | Yes | Export |
| `include/fixpp/dict/` | `fixpp_dictionary` | Yes | Export |
| `include/fixpp/session/` | `fixpp_session` | Yes | Export |
| `include/fixpp/transport/` | `fixpp_transport` | Yes | Export — plus the FR-002b `FILE_SET` disposition |
| `include/fixpp/tls/` | `fixpp_tls` | Yes | Export |
| `include/fixpp/log/` | `fixpp_log` | Yes | **Export.** *(`fixpp_log_otlp` also exists when the OTel SDK is present and is **not** in the measured closure — research R2; it is not what backs this subtree. **It is nonetheless in the export set** as of the sign-off, pulled in by `fixpp_config_toml`'s PRIVATE edge — `export-set.md` §2a. Stated here because "not in the measured closure" reads as "not exported" and is no longer a safe inference.)* |
| `include/fixpp/otel/` | `fixpp_otel` — a STATIC library when the OTel SDK is present; an empty INTERFACE **stub** when `FIXPP_BUILD_OTEL=OFF` (`CMakeLists.txt:170`) | **Yes — always**, because this feature writes `install(TARGETS fixpp_otel EXPORT …)` unconditionally. *(Precision added at sign-off: the round-2 cell said "measured mandatory closure member". The PUBLIC edge from `fixpp_session` is declared under `if(FIXPP_BUILD_OTEL)` — `src/session/CMakeLists.txt:55-57` — so in an OTel-OFF tree nothing links `fixpp_otel` and it is not in the **closure**. Its membership is therefore held by the feature's own unconditional export, not by the closure; that is the form in which the claim is true in every configuration, and it is what SC-015 actually exercises.)* | **Export.** Decisive ground, independent of OTel-ON/OFF: `include/fixpp/session/engine.hpp:32` includes `<fixpp/otel/trace_context.hpp>` with **no `#if` guard** (the surrounding block, `:25-40`, is a flat include list), so excluding `include/fixpp/otel/` would break a public session header in **every** configuration. The stub case is latent for what ships (all in-scope packages are OTel-ON, Assumption 4) but SC-015 exercises it, and it resolves the same way: the headers ship, the target exists, and the OTel-OFF stub simply carries no link edges |
| `include/fix/` + `include/fix/c_api/` (12 files) — **and, since 086, the same 12 at `include/capi/fix/`** | `fixpp_capi` STATIC (`src/capi/CMakeLists.txt:44`) | **Yes** | **Export — D1 Option A** (user, 2026-08-01). Export the **static** `fixpp_capi`, the smallest coherent fix: the shipped header gets a library, the frozen C-ABI surface is untouched, and it matches `architecture.md` §7.4's *"`fixpp::capi` — the C-ABI consumer target … C-ABI consumers link this."* **Two consequences, both derived not measured** *(and both since re-verified against a real generate run by 086 — see the reconciliation note below §2a)*: (i) `fixpp_capi` has no sources of its own, so the **OBJECT** library `fixpp_capi_objects` (`:11`) is a forced export-set member with a *different install shape* (`install(TARGETS …)` on an OBJECT library requires an `OBJECTS DESTINATION`); demoting the edge to `PRIVATE` does not escape it, because `$<LINK_ONLY:>` entries are export requirements too (FR-008a/B1). **086 performed exactly that demotion and this prediction held**: the export set is still 18 members with `fixpp::capi_objects` among them, and the 11 object files still ship. (ii) `fixpp_capi_objects` links `fixpp_tap` PUBLIC (`:37`), which settles the `tap` row as a side effect. **Known limitation, recorded not silently omitted**: `fixpp_capi_shared` is gated on `FIXPP_BUILD_TESTS` (`:114-115`), so a packaging build with tests OFF has no shared C-ABI library and none is exported — the dynamic-isolation consumer of Article IV §2 is still unserved, and the `WINDOWS_EXPORT_ALL_SYMBOLS ON` hazard (`:137`) attaches only to that unexported variant |
| `include/fixpp/config/` (3 files) | `fixpp_config_toml` STATIC (`src/config/CMakeLists.txt:13`); `architecture.md:67` module 14, Public: Yes | **Yes** | **Export.** The sharpest case in the class closes properly: `include/fixpp/config/toml_config_loader.hpp:7-8` tells consumers to *"link fixpp::config_toml"*, and now the package provides it. **Two derived consequences**: `tomlplusplus` joins the `find_dependency` set (PRIVATE dep, `src/config/CMakeLists.txt:37` — and a static library does not link its private deps, so the consumer's final link must resolve it); and `fixpp_log_otlp` joins the export set in every in-scope configuration — `src/config/CMakeLists.txt:43-45` links it `PRIVATE` under `if(TARGET fixpp::log_otlp)`, that target is created under `if(TARGET opentelemetry-cpp::api)` (`src/log/CMakeLists.txt:38-40`), `src/log` is processed before `src/config` (`CMakeLists.txt:163` vs `:174`), and all six in-scope configurations are OTel-ON. This is the "each member adds its own closure level" hazard instantiated, and it is why the re-measurement obligation is not optional |
| `include/fixpp/tap/` | `fixpp_tap` INTERFACE (`src/tap/CMakeLists.txt:4`); `architecture.md:62` module 9, Public: Yes | **Yes** | **Export.** Settled twice over: directly by the FR-012a class decision, and as a side effect of D1 Option A (`fixpp_capi_objects` links it PUBLIC, `src/capi/CMakeLists.txt:37`). INTERFACE, so exporting it costs one include-interface rewrite (`src/tap/CMakeLists.txt:7-8`) and buys the consumer include-dir and transitive-dependency propagation the headers otherwise arrive without |
| `include/fixpp/service/` | `fixpp_service` INTERFACE (`src/service/CMakeLists.txt:7`), which links `fixpp_capi` (`:15`) | **Yes** | **Export.** Bound to D1 and follows it: with `fixpp_capi` exported, `fixpp_service`'s only link edge resolves inside the export set. `architecture.md` §8 `:566` is explicit that these headers *"are part of the public C++ plugin surface like every other interface listed in §6"*, so exporting the target that carries them is the disposition that agrees with the architecture rather than merely with the packaging |
| `include/fixpp/core/test/`, `include/fixpp/transport/test/` (`mock_clock.hpp`, `mock_transport.hpp`, plus their `.gitkeep`s) — **broken out: disposition differs from parent** | test-support headers | n/a | **Exclude — recorded as a deliberate change in delivered content.** These are the only rows this decision removes from what ships today. **Two different grounds, kept separate because the "no library behind it" rule bites only one of them**: `mock_clock.hpp` is backed by `fixpp_mock_clock` (`src/core/test/CMakeLists.txt:9-12`), which is built only under `FIXPP_BUILD_TESTS` — a packaging build with tests OFF ships that header with **no library in any configuration**, so it is the class's defect in its purest form. `mock_transport.hpp` is header-only (no `src/transport/test/` exists; `tests/session/CMakeLists.txt:745` calls it *"the public mock_transport test header"*), so the rule does not reach it; its ground is instead that test doubles are not product — FR-013 excludes test artifacts in spirit, and `spec.md` User Story 1 acceptance scenario 7 requires the real client not to reach into *"its test-support headers"*, which presumes they are not part of the consumable surface. **Mechanical consequence**: `CMakeLists.txt:321-324` gains `PATTERN` exclusions where it has none today, and `src/core/test/CMakeLists.txt` is the one file carrying the raw include path that **stays out** of the FR-002a edit list |
| `include/fixpp/core/sync/detail/`, `include/fixpp/session/detail/` — **broken out: same shape as the test headers above** | their parent module's target | Inherited | **Export (ship) — resolved per file, and three of the four are load-bearing.** Reached from a public header, therefore **must** ship: `core/sync/detail/atomic_shared_ptr.hpp` (included by `include/fixpp/transport/transport_factory.hpp:33`, `include/fixpp/tls/pinset.hpp:22`, `include/fixpp/session/engine.hpp:31`); `core/sync/detail/atomic_shared_ptr_detect.hpp` (included by `atomic_shared_ptr.hpp:20`); `session/detail/has_flush_for_session_close.hpp` (included by `include/fixpp/session/message_store.hpp:28`). The fourth — `session/detail/validate_compid_filesystem_safety.hpp` — is **not** reached from any public header; it is included only from `src/session/file_store_factory.cpp:44` and `src/session/quickfix_compat/cfg_loader.cpp:30`. It ships anyway, on its own ground: it ships today, no per-file exclusion mechanism exists, and adding one would be a delivered-content regression for zero benefit. Recorded separately so it does not silently inherit the other three's justification |
| `include/fixpp/session/quickfix_compat/` | `fixpp_session` | Inherited — export | **Export.** Ships as part of the session module's public surface. Recorded because the name reads like a compatibility shim and a reviewer will ask |
| `.gitkeep` files under a shipped `include/` subtree | their parent module's target | Inherited | **Export.** *(Cell given a literal disposition at the sign-off; it previously read "cosmetic noise", which is not one of FR-012a's two values and would therefore have **failed** SC-009a on the very table the sign-off closed.)* They land in every package harmlessly. Removing them stays a **candidate cleanup, not a requirement** — that is a note on the disposition, not the disposition. Note the population is now mixed: the `.gitkeep`s inside `include/fixpp/core/test/` and `include/fixpp/transport/test/` are **excluded**, carried by their subtree's `PATTERN` exclusion rather than by a rule of their own |
| `NormativeReferences.md` in the **generated** tree (research R1) | — | n/a | **Out of SC-009a's scope, and stated rather than left implicit**: SC-009a ranges over `include/<subtree>` — the hand-written tree installed by `CMakeLists.txt:321-324`. This file lands via the *generated*-header rule (`:346-356`), which has its own exclusion mechanism (the 7-pattern denylist, FR-009). Excluding it there remains a candidate cleanup, not a requirement |

---

## 3. Why dictionaries ship

The package exposes `fixpp::dict::load_any(path, ...)` — a public runtime dictionary loader taking a **filesystem path**. Today `dictionaries/` has **no install rule at all** (verified: zero matches), so a package would ship that API with none of its data.

**FR-018a** installs the dictionaries. **SC-014** proves the pairing is usable: a consumer must load a shipped dictionary through the public API using only paths inside the installed prefix — co-location is not sufficient evidence.

---

## 4. Attribution — all five upstream clauses, and what discharges each

*(Expanded at Gate A round 1. This section previously presented "two obligations, not one" as the complete model. The license has **five** clauses; the outcome was already correct, but a table that presents itself as the enumeration must actually enumerate.)*

Source: `dictionaries/QUICKFIX_LICENSE.txt`.

| Clause | Requirement | Discharged by | Status |
|---|---|---|---|
| 1 (`:9-11`) | **Source-form** redistribution retains notice + conditions + disclaimer | Shipping `dictionaries/QUICKFIX_LICENSE.txt`. **This clause applies** — the packages ship the dictionary XML, which *is* the redistributed material in source form | File exists |
| 2 (`:12-15`) | **Binary-form** redistribution reproduces notice + conditions + disclaimer in the accompanying materials | Same file | File exists |
| 3 (`:17-22`) | End-user documentation, *"if any"*, must include the acknowledgment sentence — *"Alternately, this acknowledgment may appear in the software itself, if and wherever such third-party acknowledgments normally appear"* | A **`NOTICE`** file carrying the sentence verbatim | **Does not exist — this feature creates it** |
| 4 (`:24-27`) | The names "QuickFIX" / "quickfixengine.org" must not be used to endorse or promote derived products | FR-018c — package **descriptions** state third-party engine compatibility as fact and never imply endorsement or affiliation | Constrains DEB/RPM description wording |
| 5 (`:29-31`) | Derived products may not be called "QuickFIX", nor may "QuickFIX" appear in their name | The product name `fixpp` (FR-017) | Satisfied; recorded so it is not satisfied *by accident* |

**Clause 3 — the verbatim anchor.** The sentence is `dictionaries/QUICKFIX_LICENSE.txt:19-20`. It spans two lines, is indented inside the clause, and is itself enclosed in quotation marks. **Neither the `NOTICE` content nor SC-013's check may be written from memory** — both derive from that anchor, whitespace-normalised (FR-018b). A "verbatim" requirement with no pinned reference text is unfalsifiable, and a grep written from memory silently never matches.

> **The trap this contract exists to prevent.** The upstream license file *states* clause 3 as a condition; shipping the file is not by itself the act of putting the sentence in end-user documentation. A `NOTICE` file is the **conservative** discharge, and it is the one this feature implements. *(Softened at Gate A round 1: clause 3 is conditional — "if any" — and offers an explicit alternative, so "shipping the license does **not** discharge it" was one reading of a conditional clause stated as fact, in a feature that elsewhere correctly disclaims legal clearance. The conservative implementation stands; the certainty does not.)*

**Metadata**: the declared package license is the project's own (AGPL-3.0, constitution Article V §1). Package **descriptions** must state third-party engine compatibility as fact and must never imply endorsement or affiliation (FR-018c) — the upstream license forbids using its names to promote derived products. This constrains DEB/RPM description wording, which is otherwise easy to write carelessly.

**Dependency metadata — the package is provider-agnostic by construction** *(FR-018e, decided at Gate A sign-off, user 2026-08-01; this REPLACES the round-1 vendor / distro-`Depends:` / Conan-only option table, which was a mis-framing rather than a live choice)*.

**The premise, verified.** `src/` links **only imported target names** — `OpenSSL::SSL`, `OpenSSL::Crypto`, `asio::asio`, `pugixml::pugixml`, `Crc32c::crc32c`, `tomlplusplus::tomlplusplus`, `opentelemetry-cpp::*`. Census across `src/`, `cmake/` and the root `CMakeLists.txt`: **zero** `find_library(…)` calls and **zero** occurrences of any `.conan2` path. So `install(EXPORT)` writes *target names* into `fixppTargets.cmake`, and each `find_dependency(X)` is a plain `find_package(X)` resolved against the **consumer's** `CMAKE_PREFIX_PATH`. Conan is how fixpp is *built*; it is not how anyone must *consume*. The round-1 framing inferred a consumption constraint from a build convention.

That leaves four obligations, all of them real:

| # | Obligation | Check |
|---|---|---|
| 1 | **The generated config must stay provider-agnostic.** It must never bake a build-host absolute path or a package-manager-specific config name into what ships | The installed `fixppConfig.cmake` and `fixppTargets*.cmake` contain **no path under the build host's package-manager cache** (`~/.conan2`, `$CONAN_HOME`) and no Conan-specific config filename. Asserted over the **installed** files, per §8 |
| 2 | **Declare a tested-against version per dependency**, sourced from `conanfile.py` rather than hand-copied, and distinguish ABI-stable from ABI-fragile | The table below. A consumer supplying an older or newer build of an ABI-fragile C++ dependency is in untested territory and must be told so |
| 3 | **Document availability honestly** | Which dependencies a consumer will likely already have, and which they will have to provide |
| 4 | **Prove it on a prefix the producing build's package manager did not fill** | **SC-016** |

**Tested-against versions** — read from `conanfile.py`, not transcribed from an anchor doc:

| Dependency | Pin | Cited | ABI character | Commonly distro-packaged? |
|---|---|---|---|---|
| OpenSSL | `openssl/3.6.2` | `conanfile.py:69` | **ABI-stable** — C ABI with an upstream compatibility policy | Yes |
| asio | `asio/1.38.0` | `conanfile.py:67` | **No ABI surface** — header-only; API/version-sensitive only | Yes |
| tomlplusplus | `tomlplusplus/3.4.0` | `conanfile.py:77` | **No ABI surface** — header-only | Yes |
| pugixml | `pugixml/1.15` | `conanfile.py:66` | **ABI-fragile** — compiled C++, no stated ABI-stability guarantee | Yes |
| Crc32c | `crc32c/1.1.2` | `conanfile.py:68` | **ABI-fragile** — compiled C++, no stated guarantee; small and low-churn | **Rarely** |
| opentelemetry-cpp | `opentelemetry-cpp/1.26.0` | `conanfile.py:94` — inside `requirements()`, under `if self.options.with_otel`, **not** in the `:63-78` block | **ABI-fragile, and the acute case** — compiled C++, no ABI-stability guarantee, the largest surface here (seven imported targets from `fixpp_otel`, `src/otel/CMakeLists.txt:36-45`, plus three more from `fixpp_log_otlp`, `src/log/CMakeLists.txt:47-52`), and the fastest-moving | **Rarely** |

*(Availability is an assessment, not a measurement — it is a statement about typical distributions, and a consumer on an unusual platform should verify it rather than rely on this row.)*

**What a consumer must expect to provide.** OpenSSL, asio, pugixml and tomlplusplus are commonly available from a platform package manager. **`Crc32c` and `opentelemetry-cpp` usually are not** — and `opentelemetry-cpp` is required by **every** artifact this feature ships (all six in-scope configurations are OTel-ON, Assumption 4). That is the honest headline for User Story 2: the package resolves against any provider, but two of its six dependencies will typically have to be supplied by the consumer.

**SC-016 is the check that proves it, and it now has a pass state.** It is no longer "expected to fail": it configures a consumer against the staged prefix **plus a dependency prefix the producing build's package manager did not fill** — no `conan_toolchain.cmake`, no `CMAKE_PREFIX_PATH` entry from the producer — and `find_package(fixpp)` must **succeed** and link. Its **red** leg is explicit, because a gate never proven red proves nothing: with one **named** dependency removed from that prefix, `find_package(fixpp)` must fail with *that dependency's* `find_dependency` diagnostic and no other. That is the discriminating form; the round-2 "assert which failure" refinement survives as the red leg rather than as the expected outcome.

**Scope limit — this contract does not close REMAINING-WORK item 15d.** Whether the acknowledgment clause is an additional restriction incompatible with AGPL-3.0 is pending counsel review. Shipping the dictionaries makes that question *live*; it does not answer it. Nothing here is legal clearance. Because 15d gates publishing and nothing is published, delivery is not blocked (spec Assumption 10).

---

## 5. Naming and provenance

Names encode product, version, platform, toolchain, configuration, **and format**, and are unique across the matrix (FR-017). **Format is part of the naming dimension, not only of uniqueness**: the three Linux formats of one configuration are distinct artifacts, and each must be **independently identifiable by name** — including the file extension where the format supplies one — never merely disambiguated by directory placement.

Each artifact carries **provenance**: the configuration it was built from, the source revision, **and** either worktree cleanliness or a content hash over the build inputs (FR-021a) — **plus the telemetry state it was built with** (FR-011 / `tasks.md` T062a: every shipped artifact must record `FIXPP_BUILD_OTEL=ON`, and the packaging step fails on any that does not, so the §1 development accelerator cannot reach a shipped package).

> **Why provenance is load-bearing here.** The build strategy deletes each build tree after packaging (Assumption 5) while finished artifacts are deliberately preserved *past* that deletion (FR-021). Those two rules together maintain a directory of packages from earlier configurations and earlier source states — exactly the input that would let a witness report green against a package predating the change under test. Any witness must consume a current-build package and **fail on provenance mismatch**.

> **Why configuration + revision alone is not enough** *(added at Gate A round 1)*. Two packages built from the same commit either side of an uncommitted edit are **indistinguishable** under that definition — and an uncommitted edit is the normal state of a working branch, so this is the *likely* staleness case, not the exotic one. The repository already runs `git -C <source> status --porcelain` and fails on non-empty output (`tests/codegen/codegen_build_graph_test.cmake:202-224`), so recording cleanliness reuses existing machinery rather than inventing a mechanism.

**Retention.** The artifact directory grows monotonically across four Linux configurations × three redundant formats (FR-015), each carrying the same ~4.6 GB payload, on the **same 64 GB volume** as the build tree and a 20 GB ccache. SC-008's budget is the whole-volume high-water mark; this contract therefore requires either a stated retention rule or placement of the artifact directory on different storage (spec Assumption 5).

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
