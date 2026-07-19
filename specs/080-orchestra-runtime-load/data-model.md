# Phase 1 Data Model: Orchestra runtime dictionary load

This feature is dispatch/entry-point plumbing; it introduces no persistent data structures. The "entities" are the small set of code artifacts it adds and edits.

## E1 — `dict::load_any` (new shared helper)

- **Kind**: free function in namespace `fixpp::dict`.
- **Declaration**: `include/fixpp/dict/load_any.hpp`; definition `src/dictionary/load_any.cpp`.
- **Signature**: `[[nodiscard]] Dictionary load_any(std::filesystem::path const& path, std::pmr::memory_resource* mr);`
- **Precondition**: `mr != nullptr` (asserted, mirrors `XmlLoader`/`OrchestraLoader`).
- **Behavior**: sniff root element of `path` (one pugixml parse); dispatch `fix` → `XmlLoader{}.load(path, mr)`, `fixr:repository` → `OrchestraLoader{}.load(path, mr)`; any other/empty root or unreadable/malformed file → throw a `dict::` parse error.
- **Throws**: the loaders' existing types — `dict::xml_parse_error`, `dict::orchestra_parse_error`, `dict::unknown_version_error`, `dict::group_delimiter_collision_error`, `dict::xml_oom_error` — plus a `dict::xml_parse_error` for an unrecognized root. Never throws a non-`dict::` type across its boundary (unlike the codegen `ir.cpp` sniff).
- **Relationships**: called by E4 (C-API thunk) and E5 (TOML resolver). Wraps E-existing loaders (unchanged).

## E4 — `fixpp_dict_load_from_xml` (existing C-API thunk, behavior widened)

- **Location**: `src/capi/dictionary.cpp:48`.
- **Change**: replace the hard-wired `dict::XmlLoader{}.load(...)` with `dict::load_any(path, std::pmr::get_default_resource())`.
- **Contract delta**: input with an Orchestra root now returns `FIXPP_ERR_OK` (was `FIXPP_ERR_CAPI_CONFIG_INVALID`); all other inputs keep their disposition via the existing `catch(...)`. No signature/symbol/error-code change.

## E5 — TOML `dictionary.path` resolver (existing, behavior widened)

- **Location**: `src/config/selector_resolver.cpp` (loader call ~L359-365).
- **Change**: the `trap_throw_to_expected` loader lambda calls `dict::load_any(xml_path, opts.resource)` instead of `dict::XmlLoader{}.load(...)`.
- **Contract delta**: an Orchestra `dictionary.path` now loads; all other dispositions are unchanged.

## Unchanged (explicitly not modified)

- `XmlLoader`, `OrchestraLoader` (`load`/`load_from_string`, root-reject invariants).
- `version_registry` (ctor + `std::abort` backstop retained).
- `error.h`, `version.h`, `capi_freeze.sha256`, the `nm` symbol golden — C-ABI frozen `1.5.0`.
- Codegen, read goldens, wire path, `bindings/`.
