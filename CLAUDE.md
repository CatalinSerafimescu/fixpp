<!-- SPECKIT START -->
**Active feature:** `003-dictionary-codegen` — `fixpp-codegen` host tool + per-version typed messages (`fixpp::v42/v44/v50sp2/vt11`) + the `dict::reify` bridge (second Phase 4 feature of the `dictionary/` module, D-008).

For technologies to be used, project structure, build/test commands, and gate
status, read the current plan: [`specs/003-dictionary-codegen/plan.md`](specs/003-dictionary-codegen/plan.md).

Two `/plan` decisions, user-signed-off 2026-05-15: **F1** → `fixpp-codegen` is a C++23 host tool reusing 002's `Dictionary` IR (no new build-time dep; one XML truth). **R6** → vendor a frozen `wire::MessageView<Index>` contract stub in this PR so codegen output compiles + the full test suite runs ahead of 2b.

Companion artifacts in the same directory:
- [`spec.md`](specs/003-dictionary-codegen/spec.md) — feature specification (anchored to `.specify/2c-codegen.md` v1.3; carries /clarify Q&A 2026-05-15)
- [`research.md`](specs/003-dictionary-codegen/research.md) — Phase 0 research record (20 decisions D-1..D-20; F1/R6 resolved)
- [`data-model.md`](specs/003-dictionary-codegen/data-model.md) — 9 entities, invariants, error mapping, PMR accounting
- [`contracts/reify.hpp`](specs/003-dictionary-codegen/contracts/reify.hpp) — `[2c §4.8]` extract (`reify_as`/`reify`/`owning_message_handle`)
- [`contracts/version_registry.hpp`](specs/003-dictionary-codegen/contracts/version_registry.hpp) — `[2c §4.9]` shape only
- [`contracts/generated_message.hpp`](specs/003-dictionary-codegen/contracts/generated_message.hpp) — `[2c §4.7]` typed-message shape (the codegen golden)
- [`contracts/reify_dispatch.hpp`](specs/003-dictionary-codegen/contracts/reify_dispatch.hpp) — the two generated `_dispatch/` switch shapes
- [`contracts/wire_message_view_contract.hpp`](specs/003-dictionary-codegen/contracts/wire_message_view_contract.hpp) — the vendored FROZEN `[2b §4.3]`/`[2b §4.7]` stub (R6)
- [`quickstart.md`](specs/003-dictionary-codegen/quickstart.md) — configure(codegen) / test / bench / determinism / `/speckit-verify` / `/gate-a` / `/gate-b`

Previous feature (merged): [`002-dictionary-xml-loader`](specs/002-dictionary-xml-loader/plan.md) — `fixpp::dict::XmlLoader` + `Dictionary` runtime. Gate A + Gate B converged; PR #66 on `main`. Closed before that: [`001-core-decimal`](specs/001-core-decimal/plan.md) (Gate A/B converged 2026-05-12/13).
<!-- SPECKIT END -->
