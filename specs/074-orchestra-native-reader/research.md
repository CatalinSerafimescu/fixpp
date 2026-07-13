# Phase 0 Research: Native Orchestra Reader (FIX Latest)

Consolidated from three CodeGraph exploration passes (loader/Dictionary map, version-identity blast radius, vendoring/build layout) + the spike-and-plan doc + FIX-standard reconcile. All file:line anchors are grade-1 (read this session via CodeGraph); the ApplVerID standards claim is grade-2 (FIX-standard knowledge + direction doc; the 1128 codeset in the XML was not read directly — the file is not yet vendored).

---

## D-1 — Reader shape: sibling facade into the same `dict_metadata_handle`

**Decision**: New `dict::OrchestraLoader` mirroring `XmlLoader`'s stateless-facade shape, with an internal `OrchestraLoaderState` (parallel to `LoaderState`) that emits the identical `FieldRef[]` / `GroupRef[]` / `ComponentRef[]` / name-pool tables into a `detail::dict_metadata_handle`, returned as `Dictionary{handle}`.

**Rationale**: The existing loader is a facade (`XmlLoader`, `xml_loader.hpp:31-71`) over a build-scaffold (`LoaderState`) populating a heap-pinned `detail::dict_metadata_handle` (`dictionary_internal.hpp:80-200`) that `Dictionary` wraps by shared_ptr (`dictionary.hpp:194`). The downstream **read-path** consumers (`as_table_view` → `table_view` context machinery, validator, C-ABI read path) consume `Dictionary`, so hitting the same populate target = zero downstream code change (SC-004). Codegen `build_ir` is **not** a read-path consumer of a `vlatest` dictionary — it takes a path via `XmlLoader`, is not loader-polymorphic, and throws on the unmapped session (`ir.cpp:249-251,265-270`); typed FIX Latest codegen is a deferred follow-on (D-5). Public methods to match verbatim: `Dictionary load(path, std::pmr::memory_resource*)` and `Dictionary load_from_string(std::string_view, std::pmr::memory_resource*)`, both `[[nodiscard]]`, `assert(mr)`, wrapped in `core::detail::trap_throw_or_throw<xml_oom_error>` (`xml_loader.cpp:913-940`).

**Reused verbatim** (no fork): `field_data_type` enum (`field_ref.hpp:29-64`), `dict_metadata_handle` + its builder (`dictionary_internal.hpp:82-199`), `Dictionary` (**RESOLVED**: add `friend class OrchestraLoader;` to `dictionary.hpp:188` — symmetric with the existing `friend class XmlLoader;`; `Dictionary`'s handle-ctor is private, `dictionary.hpp:192`, so this 1-line friend is required and is a **third edit site outside the reader** — see plan Structure Decision. Rejected the `build_handle_from_doc`-extraction alternative as larger blast radius), `as_table_view` (`dictionary.cpp:296-451`, version-agnostic — an Orchestra-loaded Dictionary gets context-keyed group resolution for free once `groups_`/`group_fields_`/`FieldRef.group_no_tag` are populated), `table_view` (`table_view.hpp`), `GroupRef`/`ComponentRef`.

**Alternatives considered**: (a) a `load()` overload on `XmlLoader` — rejected, conflates two grammars in one TU and one class, muddies the QuickFIX-vs-Orchestra provenance. (b) transpose-to-QuickFIX then reuse `XmlLoader` — rejected by the 2026-07-10 native-reader decision (the whole point).

**Walk order to reproduce** (Orchestra analogue of `parse_document`, `xml_loader.cpp:611-623`): root `fixr:repository` → version gate → global fields/datatypes → codesets → components → groups → messages → header/trailer → `finalize()` (sort, intern names, patch indices, `allocate_shared` the handle).

---

## D-2 — Root + version gate: `<fixr:repository version="FIX.Latest_EP303">` → `session_version::vlatest`

**Decision**: The Orchestra reader has its OWN root check + version resolver — it does **not** reuse the QuickFIX `kVersionTable` (that gate reads a `<fix major minor servicepack>` header; `xml_loader.cpp:140-171,283-319`). The Orchestra root is `<fixr:repository>` with the version in the `version=` attribute (`"FIX.Latest_EP303"`). The resolver maps that string → `session_version::vlatest`; anything else fails closed (`unknown_version_error`, reused as-is from `error.hpp:90-98`).

**Rationale**: Different grammar, different root element and attribute shape. A dedicated resolver keeps the QuickFIX gate untouched (FR-008 additive). `session_version::vlatest` is the stored dictionary identity, surfaced by `Dictionary::which_session_version()` (`dictionary.cpp:209-211`).

**Alternatives considered**: extend `kVersionTable` with an Orchestra row — rejected, that table is keyed on `<fix>` numeric attributes the Orchestra root doesn't carry.

---

## D-3 — Datatype-token table: Orchestra `<fixr:datatype>` names → `field_data_type`

**Decision**: A new constexpr `kOrchestraTypeTable[]` (mirroring `kFieldTypeTable`'s shape: `{std::string_view name; field_data_type value;}` flat array + a `resolve_*` linear scan + fail-closed `throw orchestra_parse_error(...)` on miss), emitting into the **same** `field_data_type` enum. Handles the collapse rows the spike catalogued (`LOCALMKTTIME`→LocalMktDate, `XID`/`XIDREF`→String, `TAGNUM`→Int) and drops the `unionDataType` second arm (minimal model, FR-002).

**Rationale**: Orchestra's datatype vocabulary (ISO/FIXML names under `<fixr:datatypes>`) differs from QuickFIX's uppercase tokens, but the emission target enum is identical. The spike proved all 32 EP303 tokens map (Deliverable #1); the 5 Orchestra-only datatypes (`Tenor`, `Reserved100Plus/1000Plus/4000Plus`, `Pattern`) are either unused or appear only as `unionDataType` arms that get dropped. Fail-closed on a genuinely-unknown token (FR-006) — proven RED with a synthetic input (SC-002).

**Reuse**: `field_data_type` (`field_ref.hpp:29-64`), the fail-closed pattern from `parse_global_fields` (`xml_loader.cpp:346-350`). Do NOT reuse `kFieldTypeTable` rows verbatim — the token spellings differ; mirror the *shape*.

---

## D-4 — Error strategy: derive from `xml_parse_error`, no `core::error` append

**Decision**: `orchestra_parse_error : public dict::xml_parse_error` (reuse inherited `code()` = `dict_xml_parse_failed`, discriminate by catch type). Reuse `unknown_version_error` as-is for the version gate. If the reader emits nested groups, apply the `072` load-time `group_delimiter_collision_error` check for consistency.

**Rationale**: The `error.hpp` header (`:16-26`) blesses exactly two patterns; deriving from `xml_parse_error` (the `group_delimiter_collision_error` precedent, `error.hpp:67-85`) is the low-friction path — every existing `catch (xml_parse_error&)` / `catch (std::exception&)` bad-dictionary handler keeps working with **zero `core::error` enum append, zero `error_message()` switch edit, zero `test_020` slot pin, zero C-ABI churn** (C-ABI frozen 1.5.0, `[const §X]`). A new `core::error` variant would only be justified if callers must route Orchestra failures by `code()` value rather than catch type — they don't.

**Alternatives considered**: append `core::error::orchestra_parse_failed` — rejected, unnecessary C-ABI/enum surface for no routing benefit.

---

## D-5 — Version identity change set + ApplVerID reconcile (blast radius)

**Decision** (per Clarifications 2026-07-13 reconcile): add **only** `session_version::vlatest`. Model FIX Latest's wire application version as the **existing** `v50sp2` (ApplVerID = 9). Do **not** add `application_version::vlatest`; do **not** change `render_appl_ver_id`. ApplExtID(1156)=303 is a scheduled follow-on.

**Rationale**: FIX ApplVerID(1128) enumerates only to 9 = FIX50SP2 (grade-2: FIX standard + direction doc "FIX Latest on the wire is still tagvalue over FIXT.1.1 + ApplVerID"); FIX Latest = FIX 5.0 SP2 application layer + backward-compatible Extension Packs signalled by ApplExtID(1156), not a new ApplVerID. A distinct `application_version::vlatest` would be a modeling error — `application_version` IS the wire-ApplVerID model (`render_appl_ver_id` → chars "2".."9", `version_profile.hpp:151-174`), so a member either renders to a duplicate "9" (non-injective, Gate-A bounce) or an invented token. Keeping identity distinct only at the `session_version` (dictionary/codegen) layer is standards-correct and injective.

**Exact touch set** (grade-1):
- `version_profile.hpp:32-43` — add `session_version::vlatest = 10`.
- `version_registry.cpp:32-57` — **FORCED** (`-Wswitch`+`-Werror`, exhaustive no-`default` switch): `case session_version::vlatest: return application_version::v50sp2;`.
- `version_registry.cpp:60-73` — **FR-010 interim fail-loud guard (Gate A r1).** `session_to_application(vlatest)→v50sp2` collapses FIX Latest and real 5.0SP2 onto the one `v50sp2` slot (idx 8) of the 9-slot `application_version`-keyed registry, whose ctor is documented **silent last-writer-wins** (`:71-72`) — reachable via shipped code (`build_version_registry(cfg)` at `engine.cpp:108` / `engine_config.hpp:211-214`). Co-registering both a FIX50SP2 and a FIX Latest dict MUST fail loud, not silently drop one. The ctor is `noexcept`, so the guard is **release-effective abort/fatal-in-ctor** OR an **error-returning `build_version_registry`** (mechanism at `/tasks`; the error-returning form ripples into `engine_config.hpp`/`engine.cpp`, the fatal-in-ctor form stays contained). Full ApplExtID(1156)=303-aware **re-keying** stays deferred per the spike RECONCILE (`orchestra-fix-latest-spike-and-plan.md` L145-146). Limitation recorded as L-074-1.
- NO edit to `render_appl_ver_id` (`version_profile.hpp:151-174`), `application_version` (`version_profile.hpp:49-59`), `version_registry.hpp:64` `kTableSize` (stays 9 — no new `application_version` member), `version_profile.cpp:17-55` wire-char parse, or `scalar_mappers.cpp:443-476` — all avoided by NOT adding an `application_version` member.
- Deferred (typed-codegen follow-on, NOT this feature): `fixpp::vlatest` namespace in `tools/codegen/.../ir.cpp:212-227` + `emit_dispatch.cpp:57-65` + `emit_builders.cpp:646`. The runtime read path never needs a codegen namespace (five existing `session_version` members are already runtime-XML-only with no namespace).

**Session FSM untouched** (confirms spec deferral): `Session` negotiates on `begin_string` string (`session_config.hpp:174,512-517`; `session.cpp:1065`) + `default_appl_ver_id` (`application_version`, `session_config.hpp:456-460`), not the `session_version` enum. `session_version` is a dictionary/codegen identity + `version_registry` build key only.

**Enum-census gate check (grade-1, this session)**: grep found **no** test/const enumerating the `session_version` set exhaustively (no `kAllSessionVersions`, no completeness census over all members), and the codegen goldens are per-namespace (`v42/v44/v50sp2/vt11`) keyed off the four *codegen* versions — `vlatest` gets no codegen namespace (emit is v44-gated, `emit_builders.cpp:646`), so it should not trigger a golden/determinism regen (consistent with the five existing runtime-XML-only members). **Guard anyway** (the 072 stale-golden precedent, `[[feedback_codegen_golden_exists_narrow_verify_misses_it]]`): a `/speckit-tasks` line item + a `/speckit-verify` check must confirm the enum add causes no golden/determinism/completeness-test regression — run the FULL dictionary+codegen ctest, not a narrow target.

---

## D-6 — Vendoring layout: separate `dictionaries/orchestra/` with its own Apache-2.0 provenance

**Decision**: `dictionaries/orchestra/{OrchestraFIXLatest.xml, LICENSE(Apache-2.0), NOTICE, UPSTREAM.txt}`. Reader tests reach it via a new CMake compile-definition `FIXPP_ORCHESTRA_DATA_DIR="${CMAKE_SOURCE_DIR}/dictionaries/orchestra"` (mirror of `FIXPP_DICT_DATA_DIR`, `tests/dictionary/CMakeLists.txt`), consumed as `std::filesystem::path{FIXPP_ORCHESTRA_DATA_DIR} / "OrchestraFIXLatest.xml"`.

**Rationale**: The existing `dictionaries/` is flat and license-per-source-family (9 QuickFIX dicts + `UPSTREAM.txt` + `QUICKFIX_LICENSE.txt`, all under QuickFIX-1.0). The Orchestra file carries a **different** license (Apache-2.0). A subdir keeps the QuickFIX-1.0 surface and the Apache surface distinct — which `dictionaries/README.md` itself frames as a goal — while reusing the exact `UPSTREAM.txt` field convention (`repo @ SHA tag= date=`). Article V §4 file-level attribution satisfied. The pending QuickFIX top-level `NOTICE` (row 15d) is a separate change, NOT in scope.

**Note**: the ~7.5 MB `OrchestraFIXLatest.xml` is not cached locally (spike scratchpad was disposable) — fetching it from `FIXTradingCommunity/orchestrations @ 236d4a405…` is a real implementation task requiring network. The sha1 `26f60db1c1f52d169d3b6825ac68800abf487fde` is **provisional / unverified** (carried from the spike's relabelled scratchpad file, not the official artifact). **Do not hard-code it as a hard-equality assertion before the first fetch** — the gating first task (FR-007 / SC-007) is to fetch the official file, **compute** its sha1, and **record** the true value into `UPSTREAM.txt` + the quickstart check; only then does the sha become an assertion.

---

## D-7 — Constitution amendment + Appendix-A controls

**Decision**: A MINOR constitution amendment widening the v1.0 supported version set (Article I §1 / XVIII.1) to include FIX Latest at the runtime/dictionary tier, **folded at Gate A** on this branch (not a standalone Article XX §2 PR). All four Appendix-A controls run: `/clarify` (done), `/analyze` (next), Codex Gate A (mandatory), user `/plan` sign-off (mandatory).

**Rationale**: v1.0 scope is locked to the nine versions; `session_version::vlatest` widens it, so Article XX requires amending the constitution rather than silently violating it. Row 4b promoted FIX Latest to v1.0-gating (user, 2026-07-13), so the widening is intended. **Precedent correction (Gate A r1):** the Gate-A-folded precedents (035 FileStore exemption / 043 SecurityProfile reopen / 068 test-infra / 069 v44-codegen reclassification) are all *within-scope reclassifications or additive knobs* — **none widened the supported FIX version set.** This is the **first amendment to add a NEW FIX version**, and it directly contradicts Article I §1 **line 42** ("FIX Latest … [is a] post-1.0 milestone"). So it is categorically larger than those precedents (a widening, not a reclassification) and must be scoped to the **read/dictionary tier only** so it does not trigger Article I §1's "100% of the official spec" (session+application) obligation. The feature touches the dictionary loader / multi-version coexistence (Appendix A "Codegen layout") + version identity, mandating the four controls.

**Proposed amendment draft text (for Gate-A review + user `/plan` sign-off — not yet applied to the constitution):**
- **Article I §1** — after the runtime-XML-scope bullet, add: *"**FIX Latest (read/dictionary tier only):** `dict::OrchestraLoader` natively ingests the official FIX Orchestra machine-readable standard (`OrchestraFIXLatest.xml`, EP303) into a runtime `Dictionary` under `session_version::vlatest`. This is a **dictionary/runtime-read tier** capability — it does NOT extend the 'v1.0 ships 100% of the official spec' session+application obligation to FIX Latest (typed codegen, live wire validation, ApplExtID(1156)=303 differentiation, and session negotiation are post-1.0)."*
- **Article I §1 line 42** — remove "FIX Latest" from the post-1.0 milestone list (or annotate: *"FIX Latest — read/dictionary tier delivered in v1.0 per feature 074; typed/wire tiers remain post-1.0"*).
- **Article XVIII.1 / Sync-Impact log** — record a MINOR bump: *"amends Article I §1 to add FIX Latest at the read/dictionary tier (feature 074) — the first version-set widening (distinct from the 069-style within-scope reclassification); Gate A folded into feature 074, user-signed-off <date>."*
- Bump: MINOR (additive version-set widening at the read tier; no banned-pattern/perf/config change). Rides feature 074's branch per the established Gate-A-fold deviation from Article XX §2's standalone-PR letter.

---

## D-8 — Verification: durable invariants, not the vanished transpose artifact

**Decision**: Anchor verification on durable reproducible invariants: 181-message count (SC-001), zero unknown datatypes + fail-closed proven RED on a synthetic unknown (SC-002), depth-7 `MassQuoteAck` (path `296→295→555→40241→41686→41680→41683`) + reused-tag 555 resolve non-empty via context-keyed lookup (SC-003), downstream surfaces unchanged (SC-004), distinct `session_version::vlatest` non-colliding (SC-005), nine legacy dicts no-regression (SC-006), pin+attribution present (SC-007). A libFuzzer harness over `load_from_string` (Article VII §7).

**Rationale**: The spike's known-good transposed dict lived in a disposable scratchpad worktree and is gone; the direct-Orchestra-parse differential the spike suggested (Deliverable #5) cannot depend on it. Test template = `tests/dictionary/xml_loader_test.cpp` (`Fix44Loads` / `Fix44Headlines` / `LoadFromStringEquivalent`): fixed-array `monotonic_buffer_resource`, load from the data-dir macro, assert non-empty `messages()`, spot-check known msg types + a group delimiter via `group(no_tag)->first_field_tag/field_count`.

**Alternatives considered**: differential vs a freshly re-run transpose — rejected as an SC (adds the QuickFIX transpose tool back as a test dependency, defeating the independence rationale); acceptable only as an optional local plan-level cross-check.
