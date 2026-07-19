# Phase 1 Data Model: Orchestra runtime dictionary load

This feature is dispatch/entry-point plumbing; it introduces no persistent data structures. The "entities" are the small set of code artifacts and the one enum value it adds.

## E1 — `dict::load_any` (new shared helper)

- **Kind**: free function in namespace `fixpp::dict`.
- **Declaration**: `include/fixpp/dict/load_any.hpp`; definition `src/dictionary/load_any.cpp`.
- **Signature**: `[[nodiscard]] Dictionary load_any(std::filesystem::path const& path, std::pmr::memory_resource* mr);`
- **Precondition**: `mr != nullptr` (asserted, mirrors `XmlLoader`/`OrchestraLoader`).
- **Behavior**: sniff root element of `path` (one pugixml parse); dispatch `fix` → `XmlLoader{}.load(path, mr)`, `fixr:repository` → `OrchestraLoader{}.load(path, mr)`; any other/empty root or unreadable/malformed file → throw a `dict::` parse error.
- **Throws**: the loaders' existing types — `dict::xml_parse_error`, `dict::orchestra_parse_error`, `dict::unknown_version_error`, `dict::group_delimiter_collision_error`, `dict::xml_oom_error` — plus a `dict::xml_parse_error` for an unrecognized root. Never throws a non-`dict::` type across its boundary (unlike the codegen `ir.cpp` sniff).
- **Relationships**: called by E4 (C-API thunk) and E5 (TOML resolver). Wraps E-existing loaders (unchanged).

## E2 — `reason_class::conflicting_dictionaries` (new config-diagnostic value)

- **Kind**: appended enumerator on `enum class reason_class : std::uint8_t` in `include/fixpp/config/load_diagnostic.hpp` (currently 8 values, ending `invalid_or_contradictory_selector`).
- **Placement**: appended as the last enumerator (additive; does not renumber existing values — existing config diagnostics keep their meaning).
- **Meaning**: the resolved engine configuration names two dictionaries that collide on the shared `application_version::v50sp2` slot with different `session_version`s (the `{v50sp2, vlatest}` pair).
- **Surfacing**: carried in a `LoadDiagnostic{ reason = conflicting_dictionaries, location = <the dictionary table source loc> }`, returned to the config consumer through `LoadResult` from the `noexcept` `load_toml_config`.
- **Scope note**: config-layer C++ enum only. It is NOT a `fixpp_error_t` and does not appear in `error.h` (see research D-3). Not C-API-reachable.

## E3 — Collision-detection predicate (new, config layer)

- **Kind**: a small pure predicate used by the pre-check in `selector_resolver.cpp`.
- **Input**: the resolved `std::vector<std::shared_ptr<const Dictionary>>` (`bundle.engine.dictionaries`).
- **Rule**: map each dictionary via `session_to_application(d->which_session_version())`; report a collision iff two entries map to `application_version::v50sp2` with **different** `which_session_version()` values. (Same-session-version duplicates are benign last-writer-wins, matching `version_registry.cpp:100`.)
- **On true**: emit the E2 diagnostic and stop before constructing the Engine/registry (FR-007a). On false: proceed unchanged (single-dictionary and non-colliding multi-dictionary configs are unaffected — FR-008).
- **Invariant parity**: the predicate MUST match the exact condition of the `version_registry.cpp:89-98` abort so the config layer and the core backstop never disagree about what "collision" means.

## E4 — `fixpp_dict_load_from_xml` (existing C-API thunk, behavior widened)

- **Location**: `src/capi/dictionary.cpp:48`.
- **Change**: replace the hard-wired `dict::XmlLoader{}.load(...)` with `dict::load_any(path, std::pmr::get_default_resource())`.
- **Contract delta**: input with an Orchestra root now returns `FIXPP_ERR_OK` (was `FIXPP_ERR_CAPI_CONFIG_INVALID`); all other inputs keep their disposition via the existing `catch(...)`. No signature/symbol/error-code change.

## E5 — TOML `dictionary.path` resolver (existing, behavior widened + pre-check)

- **Location**: `src/config/selector_resolver.cpp` (loader call ~L359-365; dictionaries vector built ~L371).
- **Change (a)**: the `trap_throw_to_expected` loader lambda calls `dict::load_any(xml_path, opts.resource)` instead of `dict::XmlLoader{}.load(...)`.
- **Change (b)**: after `bundle.engine.dictionaries` is assembled, run E3; on collision push the E2 `LoadDiagnostic` onto the accumulator and do not proceed to Engine construction.
- **Contract delta**: an Orchestra `dictionary.path` now loads; a dual FIX50SP2+FIX-Latest config yields the `conflicting_dictionaries` diagnostic instead of a `std::abort`.

## Unchanged (explicitly not modified)

- `XmlLoader`, `OrchestraLoader` (`load`/`load_from_string`, root-reject invariants).
- `version_registry` (ctor + `std::abort` backstop retained).
- `error.h`, `version.h`, `capi_freeze.sha256`, the `nm` symbol golden — C-ABI frozen `1.5.0`.
- Codegen, read goldens, wire path, `bindings/`.
