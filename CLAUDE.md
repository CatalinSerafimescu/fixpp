<!-- SPECKIT START -->
**Active feature:** `003-dictionary-codegen` — `fixpp-codegen` host tool + per-version typed messages (`fixpp::v42/v44/v50sp2/vt11`) + the `dict::reify` bridge (second Phase 4 feature of the `dictionary/` module, D-008).

For technologies to be used, project structure, build/test commands, and gate
status, read the current plan: [`specs/003-dictionary-codegen/plan.md`](specs/003-dictionary-codegen/plan.md).

Two `/plan` decisions, user-signed-off 2026-05-15: **F1** → `fixpp-codegen` is a C++23 host tool reusing 002's `Dictionary` IR (no new build-time dep; one XML truth). **R6** → vendor a frozen `wire::MessageView<Index>` contract stub in this PR so codegen output compiles + the full test suite runs ahead of 2b.

**Re-`/plan` 2026-05-15 — Gate A round-1 root causes RESOLVED in-bundle** (was `blocked_on_replan`): **RC#1** `version_profile`/`resolve_application_version`/`field_traits`+`decode_field` are now 003-owned (`contracts/version_profile.hpp` additive edit + NET-NEW `contracts/field_traits.hpp`; spec §4.8 AC-VP*/AC-FT*; six `core::error` slots locked 23–28). **RC#2** decimal route re-derived from corrected `2c-codegen.md` **v1.4** (PMR-mandatory `decimal_t::parse(span,mr)`; AC-G4/AC-G4a). **RC#3** dict↔wire bridge edge resolved via the `arch §2.4` v0.2→v0.3 bridge-carve-out amendment + `check_layers.py` exemption (no module cycle). `/tasks` is gated only until the fresh `/gate-a 003-dictionary-codegen` converges.

Companion artifacts in the same directory:
- [`spec.md`](specs/003-dictionary-codegen/spec.md) — feature specification (anchored to `.specify/2c-codegen.md` **v1.4**; carries /clarify Q&A 2026-05-15 + RC#1/#2/#3 ACs)
- [`research.md`](specs/003-dictionary-codegen/research.md) — Phase 0 research record (D-1..D-23; F1/R6 + RC#1/#2/#3 resolved)
- [`data-model.md`](specs/003-dictionary-codegen/data-model.md) — 11 entities (incl. Entity 10 version_profile, Entity 11 field_traits), invariants, error mapping, PMR accounting
- [`contracts/reify.hpp`](specs/003-dictionary-codegen/contracts/reify.hpp) — `[2c §4.8]` extract (`reify_as`/`reify`/`owning_message_handle`)
- [`contracts/version_profile.hpp`](specs/003-dictionary-codegen/contracts/version_profile.hpp) — `[2c §4.3]` 003-owned (additive edit): `version_profile`/`resolved_message_version`/`resolve_application_version` + ApplVerID wire→C++ map (RC#1)
- [`contracts/field_traits.hpp`](specs/003-dictionary-codegen/contracts/field_traits.hpp) — `[2c §4.1.3]` 003-owned NEW: `field_traits<T>`/`decode_field<T>` (RC#1)
- [`contracts/version_registry.hpp`](specs/003-dictionary-codegen/contracts/version_registry.hpp) — `[2c §4.9]` shape only
- [`contracts/generated_message.hpp`](specs/003-dictionary-codegen/contracts/generated_message.hpp) — `[2c §4.7]` typed-message shape (the codegen golden; `price(mr)` v1.4)
- [`contracts/reify_dispatch.hpp`](specs/003-dictionary-codegen/contracts/reify_dispatch.hpp) — the two generated `_dispatch/` switch shapes
- [`contracts/wire_message_view_contract.hpp`](specs/003-dictionary-codegen/contracts/wire_message_view_contract.hpp) — the vendored FROZEN `[2b §4.3]`/`[2b §4.7]` stub (R6)
- [`quickstart.md`](specs/003-dictionary-codegen/quickstart.md) — configure(codegen) / test / bench / determinism / `/speckit-verify` / `/gate-a` / `/gate-b`

Previous feature (merged): [`002-dictionary-xml-loader`](specs/002-dictionary-xml-loader/plan.md) — `fixpp::dict::XmlLoader` + `Dictionary` runtime. Gate A + Gate B converged; PR #66 on `main`. Closed before that: [`001-core-decimal`](specs/001-core-decimal/plan.md) (Gate A/B converged 2026-05-12/13).
<!-- SPECKIT END -->
