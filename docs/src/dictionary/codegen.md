# Dictionary Codegen (`003-dictionary-codegen`)

> **Status:** Implemented. Gate A converged. R6 stub in place; behavioural
> reify round-trip is deferred until `002-wire` (2b) lands.

## What This Is

`fixpp-codegen` is a host-only C++23 build tool that reads the QuickFIX XML
dictionaries (the single XML truth from `002-dictionary-xml-loader`) and emits
per-version C++ headers into the build tree at CMake configure-time. It produces
typed, flyweight message classes (`Messages.hpp`), constexpr field-reference
arrays (`Fields.hpp`), rule tables (`Validator.hpp`), arena-owned cross-strand
holders (`Reify.hpp`), and normative-citation Markdown (`NormativeReferences.md`).
Two shared dispatch headers (`_dispatch/`) are emitted once over the union of all
versions.

The tool itself is never linked into user-facing library targets. It is built
in the same CMake invocation (via `add_executable`) and invoked during configure
through a custom CMake script.

## CMake Targets

| Target | Description |
|--------|-------------|
| `fixpp-codegen` | The host-only code generator executable. |
| `fixpp_codegen_generate` | Custom target that runs `fixpp-codegen` on all four XML dictionaries and writes headers into `${CMAKE_BINARY_DIR}/_codegen/include/fixpp/`. |
| `fixpp_dictionary` | The user-facing library. **Depends on** `fixpp_codegen_generate` so generated headers are ready before any library TU compiles. |
| `fixpp_builders_<ver>` / `fixpp::builders::<ver>` | Precompiled **STATIC** library holding every `build_<Msg>` body for version `<ver>` ∈ {v44, v50sp2, vlatest} (078). Always built. |
| `fixpp_validators_<ver>` / `fixpp::validators::<ver>` | Precompiled **STATIC** library holding every `validate_<Msg>` body for version `<ver>` — a disjoint object set from the builder lib (078). Always built. |

Generated headers live at:

```
build/<preset>/_codegen/include/fixpp/
  v42/     Messages.hpp  Fields.hpp  Validator.hpp  Reify.hpp  NormativeReferences.md
                                      #   (NO builder/validator tier — v42 descoped, L-077-1/issue #196)
  v44/     ...  + groups.hpp  groups/  validators/  messages/  all.hpp   # typed builder/validator tier (078 split layout)
  v50sp2/  ...  + groups.hpp  groups/  validators/  messages/  all.hpp   # typed builder/validator tier (078 split layout)
  vt11/    ...                        # FIXT.1.1 (admin-only → no builder/validator tier)
  vlatest/ ...  + Manifest.txt + groups.hpp  groups/  validators/  messages/  all.hpp
                                      #   FIX Latest (EP303); only when FIXPP_CODEGEN_FIX_LATEST=ON
  _dispatch/
    reify_dispatch_fixt.hpp
    reify_dispatch_application.hpp
```

The monolithic single-file `Builders.hpp` (077) is **removed** (FR-008, 078) — replaced by
the split layout above. See [Precompiled Builder/Validator Libraries](#precompiled-buildervalidator-libraries-078)
below.

Nothing is written into the source tree (`AC-C4` / `AC-T2`).

### Running codegen manually

```bash
# Configure (runs codegen; "up-to-date" if nothing changed):
cmake --preset linux-clang-debug

# Rebuild just the tool after a tool-source edit:
cmake --build --preset linux-clang-debug --target fixpp-codegen
# Then reconfigure to re-run codegen:
cmake --preset linux-clang-debug
```

### Selecting which FIX versions are generated (CMake options)

**There is no per-version enable/disable flag for the legacy set.** The four
legacy versions — **v42, v44, v50sp2, vt11 (FIXT.1.1)** — are generated
*unconditionally* in a single tool invocation. Only **FIX Latest (`vlatest`,
EP303)** has a generation toggle. A consumer selects which version(s) to *use*
by which header directory it `#include`s (`#include <fixpp/v44/Messages.hpp>`);
a generated-but-unincluded version costs nothing at the consumer's compile
(the headers are header-only).

| CMake option | Default | Effect |
|---|---|---|
| `FIXPP_BUILD_CODEGEN_TOOL` | `ON` | Build the `fixpp-codegen` host tool. |
| `FIXPP_CODEGEN_FIX_LATEST` | `ON` | Generate the `fixpp::vlatest` (FIX Latest / EP303) read/reify/validator tier from `dictionaries/orchestra/OrchestraFIXLatest.xml`. `OFF` removes the `vlatest/` output dir entirely; the four legacy tiers are **byte-identical** either way. |
| `FIXPP_CODEGEN_V44_FAMILIES` | `all` | Breadth of the v44 **builder** tier (`Builders.hpp`): `all` = 83 `msgcat='app'` messages; `official` = the frozen 33 OFFICIAL MsgTypes. Read tier is unaffected (always universal). |

```bash
# Default: v42/v44/v50sp2/vt11 + vlatest; v44 builders = all 83 app messages.
cmake --preset linux-clang-debug

# Legacy-only build (drop the FIX Latest tier):
cmake --preset linux-clang-debug -DFIXPP_CODEGEN_FIX_LATEST=OFF

# Restrict the v44 builder tier to the frozen 33 OFFICIAL MsgTypes:
cmake --preset linux-clang-debug -DFIXPP_CODEGEN_V44_FAMILIES=official
```

> **Typed builders (`build_<Msg>`/`validate_<Msg>`) are emitted for `fixpp::v44`,
> `fixpp::v50sp2`, and `fixpp::vlatest`** (their full `is_application` sets:
> 83 / 156 / 173; `vlatest` gated by `FIXPP_CODEGEN_FIX_LATEST`). Each repeating
> group's input `Args` struct is **deduplicated** — emitted once per distinct
> `(no_tag, recursive structural signature)` plan into `fixpp::<ns>::groups` as
> `G_<no_tag>Args` (one plan) or `G_<no_tag>_1..kArgs` (≥2 plans) — which is what
> makes the FIX Latest builder tier compile as a single TU (577-ish shared plans
> vs 26,806 message-rooted structs; 077, resolving L-076-1). **`fixpp::vt11`**
> (admin-only) and **`fixpp::v42`** (descoped — FIX 4.2 types `NumInGroup` as
> legacy `INT`, so 0 typed groups materialize; L-077-1 / issue #196) emit NO
> builders; construct v42 messages via the runtime `wire::body_builder` /
> tag-keyed path. **Source-API note:** the v44 nested-group `Args` type names
> changed from message-rooted (`NewOrderListOrdersArgs`) to shared
> (`groups::G_73_1Args`) — a deliberate pre-1.0 break with no aliases; top-level
> `<Msg>Args` names are unchanged. **Since 078**, this dedup is unchanged but the
> emitted *packaging* is a precompiled per-version library + slim per-message
> headers rather than one monolithic `Builders.hpp` — see the next section.

## Precompiled Builder/Validator Libraries (078)

078 restructured 077's single-file `fixpp/<ns>/Builders.hpp` into a precompiled
per-version library layout, for each builder-bearing version `<ns>` ∈ {v44,
v50sp2, vlatest} (`vt11`/`v42` emit none):

| File | Contents |
|---|---|
| `groups/<PlanName>.hpp` | One header per deduped group plan — `#pragma once`, includes its child-plan headers only. Data-only (no validator traits). |
| `groups.hpp` | Umbrella `#include`ing every `groups/<PlanName>.hpp` — used by the validator surface and by any consumer that wants everything. |
| `validators/traits.hpp` | Shared group-plan `inline writer_traits<T>` specializations (validator-only; includes the umbrella `groups.hpp`). |
| `messages/<Msg>.hpp` | Slim declaration header: includes only the `groups/<Plan>.hpp` this message's `<Msg>Args` transitively needs, the `<Msg>Args` struct, and `extern build_<Msg>`/`validate_<Msg>` declarations (or a macro-gated `#include` of the `.inl` body — see inline mode below). |
| `messages/<Msg>.{builder,validator}.inl` | Inline body for header-only mode — same generated body as the `.cpp` below, differing only in linkage. |
| `messages/<Msg>.{builder,validator}.cpp` | External-linkage definition, compiled once into the per-version library. |
| `all.hpp` | Aggregator: every `messages/<Msg>.hpp` for the version, plus the `builder_registry`. The "give me everything" entry point; replaces `Builders.hpp` (FR-008 — the old include path is **removed**, not aliased). |

### Two always-built libraries per version

`fixpp_builders_<ver>` (`fixpp::builders::<ver>`) and `fixpp_validators_<ver>`
(`fixpp::validators::<ver>`) are always-built **STATIC** libraries with
disjoint object sets — one `.o` per message per side. A consumer opts in
**purely at link time**:

```cmake
target_link_libraries(my_app PRIVATE fixpp::builders::v44)   # builders only
target_link_libraries(my_app PRIVATE fixpp::validators::v44) # validators only
target_link_libraries(my_app PRIVATE fixpp::builders::v44 fixpp::validators::v44) # both — no duplicate-symbol clash
```

A builder-only link carries zero `validate_<Msg>` machine code, and vice versa
(the builder surface never includes `validators/traits.hpp`).

### Per-message header-only inline mode

Each side is independently selectable — all messages
(`FIXPP_..._HEADER_ONLY`) or a single message (`..._HEADER_ONLY_<Msg>`). The
selection is a **program-wide per-message** switch, not per-TU: a message
selected for inlining must be inlined in *every* TU of the program that
references it, and a message left in link mode must stay in link mode in
*every* TU of that program.

| Macro | Effect |
|---|---|
| `FIXPP_BUILDERS_HEADER_ONLY` | Every message's `build_<Msg>` referenced by a TU including this header is pulled inline from `.builder.inl` instead of resolved from the linked library. |
| `FIXPP_BUILDERS_HEADER_ONLY_<Msg>` | Only `<Msg>`'s `build_<Msg>` is inlined; other messages still link. |
| `FIXPP_VALIDATORS_HEADER_ONLY` | Every message's `validate_<Msg>` referenced by a TU including this header is inlined. |
| `FIXPP_VALIDATORS_HEADER_ONLY_<Msg>` | Only `<Msg>`'s `validate_<Msg>` is inlined. |

The builder and validator sides are independently gated — force-inlining
`build_<Msg>` never pulls validator machine code. Mixing **different**
messages is safe: a program may force-inline a chosen subset of messages
while linking the library for the rest, with no duplicate-symbol error (each
side's `.inl` and `.cpp` are the same generated body at different linkage).
Mixing modes for the **same** message across TUs of one program — force-inline
in one TU, link mode in another — is **unsupported**: it produces an `inline`
(weak) definition alongside the archive's non-`inline` (strong external)
definition of the identically-mangled symbol, an ODR violation under
[dcl.inline]/4 (IFNDR, no diagnostic required), and MUST NOT be relied upon.
See `specs/078-precompiled-builder-libs/quickstart.md` Scenario 4d for the
full contract.

### SC-001 — consumer compile cost is closure-bounded, not universally order-of-magnitude

Because 077's `<Msg>Args` embeds its group `Args` **by value**, the full
transitive group-plan closure must be a complete type at the include site —
the per-plan `groups/<PlanName>.hpp` split trims a message's include to only
its own closure, but cannot trim the closure itself. Measured (`clang
-fsyntax-only`, peak RSS, vs. the ~3.6 GiB monolith baseline):

| Version | Result |
|---|---|
| v44 (small group set) | ~0.21 GiB for all messages — **order-of-magnitude MET** (~17×) |
| v50sp2 / vlatest, median message | ~0.47 GiB (~7.9×) |
| v50sp2 / vlatest, common message (e.g. NewOrderSingle) | ~0.88 GiB (~4.2×) |
| v50sp2 / vlatest, group-densest message (e.g. TradeCaptureReport) | ~1.42 GiB (~2.6×) |

All figures are materially below the monolith, but on the large versions
**not** order-of-magnitude — the deduplicated group graph is densely
connected (a typical large-version message's closure spans ~100–400 of ~560
plans). A forward-declared / handle-based `Args` that would meet the
universal order-of-magnitude target is a distinct API change, deferred to a
follow-up (out of scope for 078). See L-078-1 in `spec/behaviors-and-limitations.md`.

### Determinism test

```bash
ctest --preset linux-clang-debug -R codegen_determinism_test
```

Runs the generator twice in separate temp directories and byte-compares all
outputs. The generated headers are byte-stable across runs, machines, and
compiler invocations (NFR-003-7 / AC-T1).

## Accessor Model

Each standard FIX message gets a lightweight flyweight class in namespace
`fixpp::<version>` (e.g. `fixpp::v44::NewOrderSingle`). The class:

- Holds a **const reference** to a `wire::MessageView<Index>` — zero copy,
  no ownership.
- Provides **per-field `[[nodiscard]] inline noexcept`** accessors that return
  `core::expected_t<T>`:
  - **String / date / timestamp fields** → `expected_t<std::string_view>`
  - **Char / MultiCharValue fields** → `expected_t<char>`
  - **Boolean fields** → `expected_t<bool>`
  - **Integer / length / sequence-number fields** → `expected_t<int32_t>`
  - **Price / Qty / Amt / Percentage / Float fields** → `expected_t<decimal_t>`
    via `decimal_t::parse(fv->bytes(), mr)` — **requires a `pmr::memory_resource*`
    argument** (RC#2 / v1.4 discipline; the PMR route, NOT `field_traits`).
- Provides `field_value(uint16_t tag)` — dynamic tag lookup returning
  `expected_t<wire::field_view>` (AC-G6).
- Provides `view()` — returns the bound `MessageView` (for groups).
- Static constexpr `msg_type_v` (`std::string_view`) and `version_v`
  (`dict::application_version`).
- `static_assert(sizeof(Msg) == sizeof(void*))` — the flyweight IS one pointer
  (AC-G7 / seam #18).

### Repeating groups

Group members live in `namespace fixpp::<version>::groups`. Each group is a
`G_<no_tag>` flyweight class with the same accessor discipline. The parent
message (or containing group) exposes a
`wire::group_view<groups::G_<no_tag>>` accessor via the `No...` field's
snake_case name (e.g. `no_legs()` → `group_view<G_453>`).

Group class emission order is dependency-sorted (sub-group before parent) by
a capped recursive DFS over the `group_no_tag` field on `FieldRef`. The cap
(`kMaxGroupDepth = 16`) prevents infinite loops from pathological component
reuse; any dropped edge's group remains reachable via `field_value` (AC-G6).

## Reify Bridge

### Flyweight vs. owning holder

| | Flyweight (`fixpp::v44::NewOrderSingle`) | Owning holder (`fixpp::v44::owning_NewOrderSingle`) |
|---|---|---|
| Storage | One pointer — view reference | `pmr::vector<byte>` + lazy `optional<MV>` |
| Copy | Trivially copyable | Move-only (copy deleted) |
| Lifetime | Tied to source `MessageView` | Cross-strand safe (owns arena copy) |
| Defined in | `Messages.hpp` | `Reify.hpp` |

`owning_<Msg>` is the entity that safely crosses strand/thread boundaries.
It owns a deep copy of the message bytes in a PMR arena and rebuilds the
`MessageView` lazily on first `view()` call. Move constructor and move
assignment reset both the source and destination `view_cache_` to `nullopt`
so a moved-from holder never exposes a dangling view (I-9 / AC-R4).

### `dict::reify` dispatch

`dict::reify(mv, profile, mr)` determines whether the frame is a FIXT.1.1
admin message or an application message, resolves the application version,
and dispatches to the correct `owning_<Msg>::from_view()` via the generated
dispatch switch in `_dispatch/`. The dispatch switch is fail-loud (I-11 / R3):
any unknown `MsgType` or version returns `dict_reify_unknown_msg_type`; it
never silently misdispatches.

```
                    dict::reify(mv, profile, mr)
                           |
              ┌────────────┴────────────┐
              │                         │
        FIXT admin                  Application
  (MsgType ∈ 0,1,2,3,4,5,A)     (resolve_application_version)
              │                         │
    _dispatch/reify_dispatch_fixt   _dispatch/reify_dispatch_application
              │                         │
       vt11::owning_<Msg>       v42/v44/v50sp2::owning_<Msg>
              │                         │
         from_view(mv, mr)        from_view(mv, mr)
              │
        owning_message_handle  (type-erased cross-strand holder)
```

### Trait specialisations

`Reify.hpp` also emits:

```cpp
template <>
struct fixpp::dict::owning_message_traits<fixpp::v44::NewOrderSingle> {
    using type = fixpp::v44::owning_NewOrderSingle;
};
static_assert(std::is_same_v<
    dict::owning_message_t<fixpp::v44::NewOrderSingle>,
    fixpp::v44::owning_NewOrderSingle>);  // AC-G7a
```

## R6 Status

**R6** defers behavioural work that requires the real `wire::MessageView`
frame bytes (which arrive with `002-wire` / 2b):

| Blocked item | R6 stub behaviour |
|---|---|
| All `get<Tag>()` / `field_value()` calls | Return field-absent (`unexpected{dict_field_not_found}`) |
| `owning_<Msg>::from_view()` deep copy | Allocates via `mr` (PMR-OOM trap fires); no bytes copied |
| `view()` rebuild | Emplaces a default-constructed `MV` (field-absent for every get) |
| `dict::reify` full round-trip | Switch shapes compile and fail-loud default present; factory stubs allocate then return `dict_xml_parse_failed` as placeholder |

The generated header shapes, trait specialisations, `static_assert` checks,
determinism properties, and PMR-OOM error paths **are all live now** and verified
by the test suite. The R6 flag will be removed when 2b swaps in the real bodies.

## Compile-Time Performance (NFR-003-2)

Measured with `bench/codegen/compile_time_bench/compile_time_bench.sh` using
`clang++ -std=c++23 -fsyntax-only`:

| Version | Result | vs. ≤3 s ceiling |
|---------|--------|------------------|
| v42 | ~0.8 s | PASS |
| v44 | ~1.1 s | PASS |
| vt11 | ~0.3 s | PASS |
| v50sp2 | ~6 s | **KNOWN_OVERAGE** |

v50sp2 exceeds the ≤3 s load-bearing ceiling due to its ~120 kLOC typed
surface (~470 messages × groups). This is a recorded finding, not a regression;
the benchmark exits 0 with `STATUS=KNOWN_OVERAGE`. Mitigation candidates
(forward-declaring group classes, split-header, PCH) are tracked for a
follow-up.

## Running the Bench Harnesses

### Typed accessor latency (NFR-003-1)

```bash
cmake --build --preset linux-clang-debug --target typed_accessor_bench
./build/linux-clang-debug/bench/codegen/typed_accessor_bench
```

> **R6 note:** All benchmarks currently measure the frozen stub (field-absent
> path only). Numbers are meaningless until 2b lands.

### Compile-time bench (NFR-003-2)

```bash
bash bench/codegen/compile_time_bench/compile_time_bench.sh \
  build/linux-clang-debug/_codegen/include \
  clang++ \
  include
```

### reify latency (NFR-003-3)

```bash
cmake --build --preset linux-clang-debug --target reify_bench
./build/linux-clang-debug/bench/dictionary/reify_bench
```

> **R6 note:** Uses `owning_<Msg>::from_view()` as a proxy for `reify_as`
> (template body not yet defined). Behavioural numbers deferred to 2b.

## Testing

```bash
# All codegen + dictionary + integration tests:
ctest --preset linux-clang-debug -L "codegen|dictionary|integration"

# Determinism only:
ctest --preset linux-clang-debug -R codegen_determinism_test

# TSan (cross-strand reify test):
ctest --preset linux-clang-tsan -L tsan
```

Expected baseline: 8/8 codegen, 18/18 dictionary, 2/2 integration.
