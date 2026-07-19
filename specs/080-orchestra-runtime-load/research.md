# Phase 0 Research: Orchestra runtime dictionary load for non-C++ consumers

All items below were resolved against the current code (surface map, 2026-07-19). No open `NEEDS CLARIFICATION` remains.

## D-1 — Dispatch mechanism: one shared `dict::load_any` helper

**Decision**: Add `dict::load_any(std::filesystem::path const&, std::pmr::memory_resource*) -> Dictionary` in a new `include/fixpp/dict/load_any.hpp` / `src/dictionary/load_any.cpp`. It sniffs the document's root element and dispatches: `fix` → `XmlLoader::load`, `fixr:repository` → `OrchestraLoader::load`, any other/empty root → throw a `dict::` parse error (fail-closed). Both the C-API thunk and the TOML resolver call it.

**Rationale**: FR-004 requires the dispatch rule to exist once. The only existing sniff logic is inlined in the codegen tool (`tools/codegen/fixpp-codegen/ir.cpp:547-577`, feature 076): it parses the file to read `first_child().name()`, dispatches on `"fix"` / `"fixr:repository"`, else throws bare `std::runtime_error`. That logic is **not** reusable at runtime — it is inside the codegen binary, not exported, and throws outside the `dict::*_error` family. So the shared helper is created fresh, lifting the ~15-line sniff shape but routing failures through the `dict::` error types the callers already expect.

**Alternatives rejected**:
- *New C-API symbol `fixpp_dict_load_from_orchestra` + a `format`/`kind` config key* — rejected by the user's standing "Option B — root-sniff" decision (2026-07-18); more public surface, and `error.h`/config schema churn.
- *Sniff logic duplicated at each call site* — violates FR-004; two copies drift.

## D-2 — Sniff strategy: parse-to-sniff, then dispatch to `loader.load(path)`

**Decision**: `load_any` performs one lightweight pugixml parse to read the root element name via `document_element()`, then calls the chosen loader's existing `load(path, mr)`. The loader re-opens/parses the file (two reads on a startup path). The root discriminant is read with `document_element()` (the first *element* child), **not** `first_child()`, so a leading non-element node (BOM-adjacent comment / PI / XML declaration) can never be mistaken for the root even if pugixml parse flags change — this keeps the FR-006 byte-identical classic load aligned with `XmlLoader`'s own `doc.child("fix")` discriminant (N-2 hardening, Gate A round 1).

**Rationale**: Keeps the loaders' path-based error messages (filenames in `dict::xml_parse_error` / `orchestra_parse_error`) intact. The sniff shape follows the 076 `ir.cpp` precedent, but pins the accessor to `document_element()` rather than `ir.cpp`'s `first_child()` for the robustness reason above. Dictionary acquisition is a one-time startup event, so a second file read is immaterial (Article VIII §3 does not apply — not a hot path).

**Alternatives rejected**:
- *Read file once → sniff → `loader.load_from_string`* — saves one read but drops the filename from loader error messages and diverges from the path-based call the TOML resolver uses today. Premature optimization on a non-hot path (Karpathy simplicity). If a future profile ever shows startup load cost matters, revisit then.
- *Cheap byte-scan for the first element without a full parse* — fragile against BOM / comments / processing instructions; a real parse is the robust discriminant.

**Malformed input**: if the sniff parse fails or the root is neither `fix` nor `fixr:repository`, `load_any` throws a `dict::` parse error → C-API `catch(...)` → `FIXPP_ERR_CAPI_CONFIG_INVALID` (unchanged disposition); TOML `trap_throw_to_expected` → a `LoadDiagnostic`. Fail-closed either way.

## D-3 — ★ Distinct collision error surface: config-layer `reason_class`, NOT the C-ABI (pivot)

> **SUPERSEDED by Gate A round 1 (2026-07-19) — retained for audit (Article XX §1).** Gate A found the FIX50SP2 + FIX-Latest collision to be **unreachable via any config surface** (TOML resolves a single `[dictionary]` table → at most one dictionary; the C-API never populates the engine's multi-dictionary registry), so the entire collision leg — the new `reason_class` value and the config-layer pre-check — was **removed entirely**, not merely relocated off the C-ABI. The direct-C++ `version_registry` `std::abort` (074 L-074-1) remains the fail-loud backstop for the only surface that can express the pair. This decision is retained because its census is still the authoritative record of **why 080 makes no C-ABI change**; only its conclusion (add a `reason_class` value) is reversed.

**Decision**: The new distinct "both FIX50SP2 and FIX-Latest named" error (clarify Q1 = B) is a **new `reason_class` value** (`conflicting_dictionaries`) appended to `include/fixpp/config/load_diagnostic.hpp` (a `std::uint8_t` C++ enum surfaced to the config consumer via `LoadResult`/`LoadDiagnostic`). It is **not** a `fixpp_error_t` value and touches no C-ABI header.

**Rationale (evidence)**: Clarify Q1 was originally framed as a C-API `fixpp_error_t` append. A `version_registry`-construction **census** then showed that framing targets the wrong surface:
- There is exactly one runtime `version_registry` construction: `src/session/engine.cpp:108` → `core::build_version_registry(engine_cfg_)` → `version_registry{cfg.dictionaries}` (`engine_config.hpp:213`).
- Its input, `EngineConfig.dictionaries`, is populated with multiple dictionaries only by the TOML config path (`selector_resolver.cpp:371`). `fixpp_engine_create` (`engine.cpp:186-193`) builds its `EngineConfig` setting only executor/clock/application and **never** populates `dictionaries` — a C-API engine's registry is empty, so the FIX50SP2+FIX-Latest collision is **unreachable via the C-API**.
- Therefore the collision is a config-file-layer event; its natural, operator-visible distinct code is a `reason_class`, and adding one carries none of the C-ABI cost.

**Consequence**: no `error.h` edit → the `tools/capi_freeze.sha256` header byte-freeze and the GA "C-ABI immutable once MAJOR==1" contract stay intact; no `fixpp_error_t` value → the `nm` symbol golden and abidiff stay clean; `FIXPP_C_ABI_VERSION` stays `1.5.0`. The user's Q1=B intent (distinguish a dictionary-collision from a malformed document) is fully delivered by the distinct `reason_class`.

**Audit trail**: the original C-ABI-append framing is preserved and marked corrected in `spec.md` Clarifications (not silently deleted), per Article XX §1 (no silent violation) discipline.

**Alternatives rejected**:
- *New `fixpp_error_t` value in `error.h`* — wrong surface (collision not C-API-reachable), and would break the header byte-freeze + collide with the GA immutability contract for zero reachable benefit.
- *Reuse existing `reason_class::invalid_or_contradictory_selector`* — the user explicitly chose a **distinct** code (Q1=B) so operators can separate a dictionary-collision from a generic bad-selector/bad-document error.
- *Map to an existing `fixpp_error_t` via the translate layer* — only relevant if the collision were C-API-reachable; the census says it is not, so no mapping is needed (YAGNI). If a future feature exposes multi-dictionary config through the C-API, that feature adds the mapping then.

## D-4 — Collision pre-check placement: config layer, before registry construction (FR-007a)

> **SUPERSEDED by Gate A round 1 (2026-07-19) — retained for audit (Article XX §1).** With the collision leg descoped (see D-3), no config-layer pre-check is added at all; FR-007a is removed from the spec. This placement analysis is retained only as the record of the option that was considered and dropped.

**Decision**: The collision pre-check runs in `selector_resolver.cpp` over `bundle.engine.dictionaries` after they are resolved and **before** the Engine (hence the `version_registry`) is constructed. It detects two dictionaries whose `which_session_version()` differ but both map to `application_version::v50sp2` via `session_to_application` (i.e. the `{v50sp2, vlatest}` pair — the exact condition of the `version_registry.cpp:89-98` abort), and emits the `conflicting_dictionaries` diagnostic instead of proceeding.

**Rationale**: FR-007a / clarify Q2 = A. Detecting in the config layer leaves the core `noexcept` `version_registry` ctor — and its `std::abort` as a direct-C++ fail-loud backstop — untouched, matching "registry re-keying is out of scope." The collision condition is fully determinable from the resolved dictionary vector using the public `Dictionary::which_session_version()` accessor (`dictionary.hpp:90`).

**Alternatives rejected**:
- *Modify `version_registry` ctor to return an error* — touches a core noexcept path used by every engine (including empty-registry C-API engines); larger blast radius; contradicts Q2=A.
- *Let `build_version_registry` return its `expected_t` error* — `engine.cpp:108` currently `.value()`s it; converting that to a surfaced error reshapes the Engine-construction contract for a config-only concern. Config-layer pre-check is the smaller change.

## D-5 — Loaders and the T022h invariant are reused unchanged

**Decision**: `XmlLoader` and `OrchestraLoader` are not modified. `XmlLoader` continues to reject a `<fixr:repository>` root (074 T022h): for a non-`<fix>` root the reject fires FIRST at `xml_loader.cpp:742-745` — `parse_document`'s `doc.child("fix")`-missing guard (message "root `<fix>` element missing"). The `parse_version` guard at `:347-350` never sees a non-`<fix>` root (it only ever receives `doc.child("fix")`), so it is for-this-path dead. `load_any` never routes an Orchestra document to `XmlLoader`, so the invariant holds and is pinned by a regression test (FR-009).

**Rationale**: The widening is at the *dispatch/entry-point* layer only; the loaders keep their single-grammar contracts. This keeps the change surgical and the existing loader unit tests valid.

## Open items

None. All Technical Context unknowns resolved; `/analyze` and Gate A are process gates, not research unknowns.
