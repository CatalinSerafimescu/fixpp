# Contract: `dict::OrchestraLoader` public surface

The reader exposes one public class whose signatures are **source-compatible with `dict::XmlLoader`** so callers, tests, and (later) codegen `build_ir` can consume it identically. It targets the same `Dictionary`.

## Public interface (`include/fixpp/dict/orchestra_loader.hpp`)

```cpp
namespace fixpp::dict {

// Native reader for the official FIX Orchestra machine-readable standard
// (fixr:repository schema). Sibling to XmlLoader (QuickFIX-XML). Stateless.
class OrchestraLoader {
public:
  OrchestraLoader() noexcept = default;

  // Parse an OrchestraFIXLatest.xml file into an internal Dictionary.
  // Precondition: mr != nullptr (asserted).
  // Throws: orchestra_parse_error (malformed/unknown-datatype/wrong-root/dangling-ref),
  //         unknown_version_error (root version= not FIX Latest),
  //         group_delimiter_collision_error (nested delimiter == parent),
  //         xml_oom_error (PMR bad_alloc, via trap_throw_or_throw).
  [[nodiscard]] Dictionary load(std::filesystem::path const& path,
                                std::pmr::memory_resource* mr);

  [[nodiscard]] Dictionary load_from_string(std::string_view xml,
                                            std::pmr::memory_resource* mr);
};

} // namespace fixpp::dict
```

## Behavioral contract

| Aspect | Contract |
|---|---|
| **Output** | A fully-populated `Dictionary` (move-only) equivalent in shape to an `XmlLoader`-produced one: `messages()`, `field_ref()`, `required_fields()`, `component()/component_fields()`, `group()/group_first_field()/group_fields()`, `as_table_view()`, `which_session_version()` all work unchanged. |
| **Version identity** | `which_session_version() == session_version::vlatest`. Wire application version = `application_version::v50sp2` (via `session_to_application`). No distinct ApplVerID. |
| **Message count** | Exactly 181 for EP303 (`messages().size() == 181`). |
| **Groups** | Depth ≤ 7 (≤ `kMaxGroupContextDepth`=16); reused group tags disambiguated by parent-path context key in `as_table_view`. Nested delimiter collision fails closed at load. |
| **Datatypes** | Every Orchestra `type=` token maps via `kOrchestraTypeTable` to `field_data_type`; a genuinely-unknown token throws `orchestra_parse_error`. Union second arm dropped; codeset flattened to per-field enum values (values+descriptions kept). |
| **Memory** | All allocation on the supplied `mr`; PMR `bad_alloc` → `xml_oom_error`. `assert(mr != nullptr)`. |
| **Fail-closed** | Wrong root (`!= fixr:repository`), malformed/truncated XML, missing required attributes, dangling component ref, unknown datatype, or a QuickFIX-XML file fed here → thrown error, never a silent partial `Dictionary`. |
| **Additive** | Does not touch `XmlLoader`, the nine QuickFIX dicts, or the C-ABI. |

## Fail-closed test matrix (verification hooks)

| Input | Expected |
|---|---|
| Valid `OrchestraFIXLatest.xml` (EP303) | `load` OK, 181 msgs, `vlatest` |
| Synthetic Orchestra XML with an unknown `<fixr:datatype>` used by a field | `orchestra_parse_error` (SC-002, proven RED) |
| Orchestra XML with non-`fixr:repository` root | `orchestra_parse_error` |
| Orchestra XML with `version=` not FIX Latest | `unknown_version_error` |
| A QuickFIX `FIX44.xml` fed to `OrchestraLoader` | thrown error (wrong grammar), never mis-parse |
| Truncated / malformed XML | `orchestra_parse_error` |
| `mr == nullptr` | assertion (precondition, not a runtime path) |

## Error type addition (`include/fixpp/dict/error.hpp`)

```cpp
// Orchestra-format parse failure. Derives from xml_parse_error so every
// existing catch(xml_parse_error&) / catch(std::exception&) handler works
// unchanged; reuses code()==dict_xml_parse_failed; discriminated by catch
// type (the group_delimiter_collision_error precedent, 072). NO new
// core::error variant, NO C-ABI change.
class orchestra_parse_error : public xml_parse_error {
public:
  using xml_parse_error::xml_parse_error;
};
```
