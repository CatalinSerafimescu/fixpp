<!-- SPECKIT START -->
**Active feature:** `002-dictionary-xml-loader` — `fixpp::dict::XmlLoader` + `Dictionary` runtime (XML data-dictionary loader, first feature of the `dictionary/` module).

For technologies to be used, project structure, build/test commands, and gate
status, read the current plan: [`specs/002-dictionary-xml-loader/plan.md`](specs/002-dictionary-xml-loader/plan.md).

Companion artifacts in the same directory:
- [`spec.md`](specs/002-dictionary-xml-loader/spec.md) — feature specification (anchored to `.specify/2c-codegen.md` v1.3; carries /clarify Q&A 2026-05-14)
- [`research.md`](specs/002-dictionary-xml-loader/research.md) — Phase 0 research record (20 decisions D-1..D-20)
- [`data-model.md`](specs/002-dictionary-xml-loader/data-model.md) — 7 entities, invariants, error mapping, PMR allocation accounting
- [`contracts/field_ref.hpp`](specs/002-dictionary-xml-loader/contracts/field_ref.hpp) — `[2c §4.1]` extract
- [`contracts/component_ref.hpp`](specs/002-dictionary-xml-loader/contracts/component_ref.hpp) — `[2c §4.2]` extract
- [`contracts/group_ref.hpp`](specs/002-dictionary-xml-loader/contracts/group_ref.hpp) — `[2c §4.2]` extract
- [`contracts/version_profile.hpp`](specs/002-dictionary-xml-loader/contracts/version_profile.hpp) — enum subset of `[2c §4.3]`
- [`contracts/dictionary.hpp`](specs/002-dictionary-xml-loader/contracts/dictionary.hpp) — loader-MVS subset of `[2c §4.3]`
- [`contracts/xml_loader.hpp`](specs/002-dictionary-xml-loader/contracts/xml_loader.hpp) — `[2c §4.5]` extract (load + load_from_string only; `load_overlay*` deferred per /clarify Q2 → A)
- [`contracts/error.hpp`](specs/002-dictionary-xml-loader/contracts/error.hpp) — `dict::xml_parse_error` / `unknown_version_error` / `xml_oom_error` + enum mates
- [`quickstart.md`](specs/002-dictionary-xml-loader/quickstart.md) — build / test / bench / TSan / coverage / `/speckit-verify` / `/gate-a` / `/gate-b`

Previous feature (closed): [`001-core-decimal`](specs/001-core-decimal/plan.md) — `fixpp::core::decimal<T>` + `fixpp_decimal_t`. Gate A + Gate B converged 2026-05-12 / 2026-05-13.
<!-- SPECKIT END -->
