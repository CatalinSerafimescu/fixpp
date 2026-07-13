# Phase 1 Data Model: Native Orchestra Reader (FIX Latest)

The reader's ONLY output is an internal `detail::dict_metadata_handle` (wrapped by `Dictionary`). It introduces **no new runtime data structure** — it populates the existing one. This document specifies (A) the Orchestra→internal mapping, (B) the version-identity change set with blast radius, and (C) the new file-level entities (reader, error, vendored source).

## A. Orchestra (`fixr:repository`) → internal `dict_metadata_handle` mapping

| Orchestra construct (`fixr:` schema) | Internal target (`dictionary_internal.hpp`) | Mapping rule |
|---|---|---|
| `<fixr:repository version=…>` (root) | `dict_metadata_handle::version_` (`session_version`) | Root check + version resolver → `session_version::vlatest` (D-2); non-match → `unknown_version_error`. |
| `<fixr:datatypes>/<fixr:datatype name=…>` referenced by a field's `type=` | `FieldRef.type` (`field_data_type`) | `kOrchestraTypeTable` token→enum (D-3); collapse rows per spike (`LOCALMKTTIME`→LocalMktDate, `XID`/`XIDREF`→String, `TAGNUM`→Int); unknown → `orchestra_parse_error`. |
| `<fixr:fields>/<fixr:field id= name= type=>` | global tag→(name,type) map → `FieldRef` + `field_by_name_` | Interned into `name_pool_` as `NameSlice`; `id` = tag. |
| `<fixr:codeSets>/<fixr:codeSet>` + `<fixr:code value= …>` | per-field enumerated values (values + descriptions preserved) | Minimal model: flatten codeset into the field's value list (FR-002); shared-codeset naming abstraction dropped. |
| a field with `unionDataType=` | `FieldRef.type` = the codeset base type only | Drop the union second arm deterministically (edge case; no error). |
| `<fixr:components>/<fixr:component>` | `components_`, `components_by_name_`, `component_fields_` | Resolve `<fixr:componentRef>` transitively during expand; same side-table layout as QuickFIX loader. |
| `<fixr:group>` + `<fixr:numInGroup id=>` | `groups_` (`GroupRef`), `group_fields_`; `FieldRef.group_no_tag` on members; delimiter type = `NumInGroup` | Set `GroupRef{no_tag, first_field_tag, first_field_index, field_count, parent_group_no_tag}`; nested delimiter == parent ⇒ `group_delimiter_collision_error` (072 check). |
| `<fixr:message msgType= name= msgcat=>` | `messages_` (`MessageEntry{msg_type,name}`) + per-msg `FieldRef[]` run | 181 messages, bytewise-sorted by msg_type; `expand_field_list` analogue emits per-msg field runs. |
| `<fixr:message>` `<fixr:fieldRef presence="required">` | `required_fields_pool_` + `per_msg_required_offsets_` | Required-only tag list per message. |
| header / trailer message definitions | `header_node_` / `trailer_node_` analogue | Same handling as `parse_document` records. |
| `scenario=` variants | — | N/A for EP303 (zero present); reader treats base only. |

**Downstream (unchanged, populated-for-free)**: once `groups_` / `group_fields_` / `FieldRef.group_no_tag` / `type==NumInGroup` are correct, `Dictionary::as_table_view()` (`dictionary.cpp:296-451`) derives the dual-store bare + **context-keyed** `(msg_type, parent_path, no_tag)` group resolution (SC-003) with no reader involvement — same code path the validator's 8 `group_member_tags` callers use.

## B. Version-identity change set (blast radius) — grade-1 anchors

| # | Site | Edit | Forced? |
|---|---|---|---|
| B1 | `include/fixpp/dict/version_profile.hpp:32-43` | add `session_version::vlatest = 10` | new enumerator |
| B2 | `src/dictionary/version_registry.cpp:32-57` (`session_to_application`) | add `case session_version::vlatest: return application_version::v50sp2;` | **YES** — exhaustive `default`-free `switch`; `-Wswitch`+`-Werror` = CI break otherwise |
| B3 | Orchestra reader (`src/dictionary/orchestra_loader.cpp`) version resolver | map root `version="FIX.Latest_EP303"` → `session_version::vlatest` | new code |
| — | `render_appl_ver_id` (`version_profile.hpp:151-174`) | **NO CHANGE** (no `application_version::vlatest`) | avoided |
| — | `application_version` enum (`version_profile.hpp:49-59`) | **NO CHANGE** | avoided |
| — | `version_registry.hpp:64` `kTableSize=9` | **NO CHANGE** (no new `application_version` member ⇒ no `idx==9` overflow) | avoided |
| — | `version_profile.cpp:17-55`, `scalar_mappers.cpp:443-476` | **NO CHANGE** | avoided |

**Wire application version**: FIX Latest resolves to `application_version::v50sp2` (ApplVerID 9) via B2. No distinct ApplVerID exists (D-5). **ApplExtID(1156)=303 is out of scope — scheduled follow-on.**

**Deferred (NOT this feature)**: a `fixpp::vlatest` codegen namespace (`tools/codegen/.../ir.cpp:212-227`, `emit_dispatch.cpp:57-65`, `emit_builders.cpp:646`). `build_ir` throws on an unmapped session only if the codegen tool is pointed at the Orchestra XML — the runtime read path never invokes it.

## C. New file-level entities

| Entity | File | Shape |
|---|---|---|
| `dict::OrchestraLoader` | `include/fixpp/dict/orchestra_loader.hpp` | Stateless facade; `[[nodiscard]] Dictionary load(std::filesystem::path const&, std::pmr::memory_resource*)` + `load_from_string(std::string_view, std::pmr::memory_resource*)`; body wrapped in `trap_throw_or_throw<xml_oom_error>`. Mirrors `xml_loader.hpp:31-71`. |
| `OrchestraLoaderState` | `src/dictionary/orchestra_loader.cpp` (TU-local) | Build scaffold; parses `fixr:repository`, emits `FieldRef[]/GroupRef[]/ComponentRef[]/name_pool` into a `dict_metadata_handle`, `finalize()`. Parallel to `LoaderState`. |
| `kOrchestraTypeTable[]` | `src/dictionary/orchestra_loader.cpp` (TU-local) | `constexpr {std::string_view name; field_data_type value;}[]` + `resolve_*` linear scan. |
| `dict::orchestra_parse_error` | `include/fixpp/dict/error.hpp` | `: public xml_parse_error`; reuses `code()`; catch-discriminated (D-4). |
| Vendored source | `dictionaries/orchestra/OrchestraFIXLatest.xml` (+ `LICENSE`, `NOTICE`, `UPSTREAM.txt`) | Apache-2.0, pinned `236d4a405… / sha1 26f60db1…`, EP303. |

## Validation rules (from FRs)

- **FR-003**: `Dictionary::messages().size() == 181` after load.
- **FR-004**: `as_table_view()` builds without throw/truncate; depth-7 + reused-tag 555 context lookups non-empty.
- **FR-005**: `Dictionary::which_session_version() == session_version::vlatest`; `session_to_application(vlatest) == application_version::v50sp2`.
- **FR-006 / SC-002**: unknown Orchestra datatype ⇒ `orchestra_parse_error` thrown; proven RED on a synthetic input.
- **FR-008 / SC-006**: the nine QuickFIX dicts load through `XmlLoader` with unchanged message counts + group queries (regression pin).
- **FR-009**: malformed/truncated/wrong-format/dangling-ref ⇒ thrown parse error, never silent partial.
