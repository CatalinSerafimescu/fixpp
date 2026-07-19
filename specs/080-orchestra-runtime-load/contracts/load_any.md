# Contract: `dict::load_any`

The single shared dictionary-layer dispatch helper (FR-003, FR-004).

## Interface

```cpp
// include/fixpp/dict/load_any.hpp
namespace fixpp::dict {
[[nodiscard]] Dictionary load_any(std::filesystem::path const& path,
                                  std::pmr::memory_resource* mr);
}
```

## Dispatch rule (sole discriminant = root element)

| Root element | Loader | Result |
|---|---|---|
| `fix` | `XmlLoader::load(path, mr)` | classic tag-value `Dictionary` (byte-identical to today — FR-006) |
| `fixr:repository` | `OrchestraLoader::load(path, mr)` | FIX-Latest `Dictionary` (equivalent to `OrchestraLoader::load` directly — FR-001/SC-001) |
| any other / empty / unreadable / malformed | — | throw a `dict::` parse error (fail-closed — FR-003) |

## Guarantees

- **G1 (single definition)**: this is the only runtime root-sniff/dispatch; both the C-API thunk and the TOML resolver call it (FR-004). The codegen `ir.cpp` sniff is a separate build-time concern and is out of scope.
- **G2 (fail-closed, `dict::` errors only)**: every failure path throws a member of the `dict::*_error` hierarchy — never a bare `std::runtime_error` and never a non-throwing wrong-`Dictionary`. Callers' existing `catch`/`trap` layers handle these unchanged.
- **G3 (loaders unchanged)**: `load_any` does not modify `XmlLoader`/`OrchestraLoader`; it never routes an Orchestra document to `XmlLoader`, so the 074 T022h reject invariant is preserved (FR-009).
- **G4 (precondition)**: `mr != nullptr` (asserted).

## Test obligations

- Orchestra file → returns a `Dictionary` equivalent to `OrchestraLoader::load` (parse/validate/read parity).
- Classic `<fix>` file → returns a `Dictionary` byte-identical to `XmlLoader::load` (existing goldens unchanged).
- Unrecognized root (e.g. a `<fixml>` or arbitrary XML) → throws a `dict::` error (not `std::runtime_error`).
- Malformed / unreadable file → throws a `dict::` error.
