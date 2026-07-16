# Contract: deduplicated `Builders.hpp` shape

**Feature**: 077-builder-args-dedup

Governs the emitted per-version `<ns>/Builders.hpp` after structural-plan
deduplication. Supersedes the message-rooted-Args shape of 067/069 for the
`Args` struct *identity*; the serialize/validate *behavior* is unchanged.

## G1 — Shared group Args namespace

Each distinct structural plan (data-model Entity 1) is emitted exactly once as a
struct in `namespace fixpp::<ns>::groups`, mirroring the read tier's
`G_<no_tag>` flyweight namespace.

```cpp
namespace fixpp::v44::groups {
  struct G_802Args { /* PartySubID members */ };          // NoPartySubIDs, one plan
  struct G_453Args { /* PartyID… + span<const G_802Args> */ };
  struct G_555_1Args { /* NoLegs variant 1 */ };          // <-- variant-disambiguated
  struct G_555_2Args { /* NoLegs variant 2 */ };
  // …
}  // 89 structs for v44 (vs 730 message-rooted before)
```

- **G1a** — Name (exact rule): if a version has **exactly one** structural plan
  for a `no_tag`, the struct is `G_<no_tag>Args` (bare). If it has **≥2**
  distinct plans, **all** of them are ordinaled `G_<no_tag>_1Args` …
  `G_<no_tag>_kArgs` (NO bare name in the ≥2 case), ordinal by first-encounter
  over the bytewise-sorted message list × declaration-order `group_order`. The
  ordinal count is fixed per version, so naming is deterministic.
- **G1b** — A group reused with **identical** structure across N messages/paths
  yields **one** struct. A group merely sharing a `no_tag` but structurally
  different yields **distinct** structs (FR-002).
- **G1c** — Children emitted before parents (post-order); no referenced-but-
  undefined Args type (FR-011).

## G2 — Per-message top-level Args + builder

`<Msg>Args` remains per-message (not deduped) in `fixpp::<ns>`; its group
members reference the shared `groups::G_…Args` by qualified name.

```cpp
namespace fixpp::v44 {
  struct NewOrderSingleArgs {
    ::std::optional<::std::string_view> cl_ord_id{};
    // …
    ::std::optional<::std::span<const groups::G_453Args>> party_i_ds{};
  };
  inline ::fixpp::core::expected_t<::std::span<::std::byte>>
  build_NewOrderSingle(::std::span<::std::byte> out, NewOrderSingleArgs const& args) noexcept;
  inline ::fixpp::core::expected_t<void>
  validate_NewOrderSingle(NewOrderSingleArgs const& args) noexcept;
}
```

- **G2a** — `build_<Msg>` serialization order, framing/header-trailer exclusion,
  Length+Data coupling, and `group_end`-via-`bb` discipline are **unchanged**
  (only referenced type names change).
- **G2b** — `validate_<Msg>` stays a thin wrapper over
  `wire::validate_required<TopLevelArgs>`.

## G3 — `writer_traits` + helpers, once per plan

`writer_traits<groups::G_…Args>` and its `_required_` / `_count_` /
`_validate_entry_` helpers are emitted **once per distinct plan** in
`fixpp::wire`, post-order, before the `validate_<Msg>` wrappers. No duplicate or
ODR-conflicting definition across messages (FR-003).

## G4 — Version gating & determinism

- **G4a** — Emitted for v42/v44/v50sp2/vlatest; **not** for vt11 (empty output,
  no file — `write_file` empty-skip). vlatest gated by
  `FIXPP_CODEGEN_FIX_LATEST` (absent + no stale file when OFF, FR-012).
- **G4b** — Byte-deterministic across runs/machines/compilers (covered by
  `codegen_determinism_test`).
- **G4c** — v44 `--families all|official` retained; both goldens regenerated.

## G5 — Compile-resource acceptance

Each emitted `Builders.hpp` compiles as a single TU within low-single-digit-GB
RSS (no >21 GB / OOM). Generated source ~10 MB order for vlatest (SC-001/002).
