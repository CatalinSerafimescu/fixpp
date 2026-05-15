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

Generated headers live at:

```
build/<preset>/_codegen/include/fixpp/
  v42/     Messages.hpp  Fields.hpp  Validator.hpp  Reify.hpp  NormativeReferences.md
  v44/     ...
  v50sp2/  ...
  vt11/    ...
  _dispatch/
    reify_dispatch_fixt.hpp
    reify_dispatch_application.hpp
```

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
