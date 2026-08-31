---
type: Component Decision Map
title: Plugin factory ownership — unique_ptr vs shared_ptr in EngineConfig / SessionConfig
description: Two signed-off design docs publish unique_ptr for three plugin-factory members that ship as shared_ptr. Feature 010 FR-001a flipped them and neither doc absorbed it.
status: stable
refs:
  - include/fixpp/core/engine_config.hpp
  - include/fixpp/session/session_config.hpp
  - .specify/2d-threading.md
  - .specify/2h-transport.md
  - specs/010-session-cfg-lifetime/spec.md
codegraph_entry: [EngineConfig, SessionConfig, MessageStoreFactory, TransportFactory]
constitution: ["§XIV.2"]
---

# Plugin factory ownership

> ## ⚠️ The CODE is authoritative. This page is not.
>
> SecondBrain is a **consultant**, not a source of truth. It points you at the right files and explains
> **why** a decision was taken and what was **rejected** — that half is historical and does not change
> retroactively. It does **not** establish what the code does today.
>
> **Anything here describing current behaviour is a LEAD TO CHECK, not a fact to cite.** Verify against
> source before you rely on it, and cite the source, not this page.
>
> This page exists because signed-off design documents rotted. **It has no immunity from that** — a page
> trusted instead of read becomes the next fossil, and it would be a worse one, because it is the page
> people come to for the fossil list.

## The question this answers

*"`SessionConfig::store_factory` is a `unique_ptr` per `[2d §4.5]` — so `SessionConfig` can't be
copied, right?"* **It can.** The design docs and the code disagree, and the code won.

## Current state — a LEAD, verify against source before citing

Three polymorphic plugin-factory members are declared in the **headers**, which are authoritative:

- `EngineConfig::default_transport_factory` — `include/fixpp/core/engine_config.hpp`
- `SessionConfig::store_factory` and `SessionConfig::transport_factory_override` —
  `include/fixpp/session/session_config.hpp`

**No type is reproduced on this page.** Copying it here would create the fourth copy of a fact that
already rotted in two places — read the headers.

## ⚠️ Two signed-off documents disagree with the headers

| Document | Says | Status |
|---|---|---|
| `.specify/2d-threading.md` §4.4 / §4.5 | `unique_ptr` for all three, *"unique ownership per `[arch §5.6]`"* | **SUPERSEDED.** Each affected line now carries an **in-place** marker pointing at the shipped header. Deliberately **not re-typed** — the block is cited by line number, and a retyped value is a fourth copy to keep in sync |
| `.specify/2h-transport.md` Appendix D §D.1/§D.2 | Proposes the `shared_ptr → unique_ptr` flip, with *"Before"* blocks *"quoted verbatim"* from `2d` | **APPLIED, THEN SUPERSEDED.** Amended 2026-08-29 (v0.3 → v0.4). See its Appendix Z |
| `.specify/2e-msgstore.md` Appendix D §D.1 | The **origin** of the flip for `store_factory` — argued `shared_ptr → unique_ptr` in round 1 | **APPLIED, THEN REVERSED IN CODE.** Amended 2026-08-29 (v0.5 → v0.6). ⚠️ **Its stale *"Before"* block matches shipped code better than its *"After"* does** — see below |

### ⚠️ The trap in `2e` §D.1, worth knowing by shape

A drop-in amendment block labels one half **"Before"** (stale) and the other **"After"** (the
resolution). In `2e` §D.1 those labels now **invert** the truth: *"Before"* shows `shared_ptr`, which
is what shipped; *"After"* shows `unique_ptr`, which did not. **A reader trusting the labels gets the
answer exactly backwards** — worse than an obviously-stale document, which at least announces itself.

The general shape: **once an amendment is applied and later reversed, a Before/After block is not
merely stale — it is actively inverted.** Any `Appendix D` block in these design docs carries that
risk once its amendment has landed.

## Why it is this way — the decision that won

**Feature 010, `FR-001a`** (`specs/010-session-cfg-lifetime/spec.md`) flipped `store_factory` to make
`SessionConfig` **copy-constructible** — the W-5 fix. The reasoning generalises, and the code applied
it to all three members:

> the binding design used `unique_ptr` for polymorphic ownership **through indirection**, NOT to
> forbid sharing; the factory is a **stateless** interface — one virtual `make()` returning a freshly
> minted `MessageStore` — so sharing a factory across Sessions is meaningful, and the per-Session
> uniqueness invariant (one store per Session) is unaffected.

**What was rejected:** the `unique_ptr` model that `2h` Appendix D argued *for*. That is the part
worth knowing — 2h did not overlook the question, it answered it the other way and the tree overruled
it. Call sites still assigning `std::make_unique<...>` keep working through the implicit
`unique_ptr<T>&& → shared_ptr<U>` move-conversion, which is why the divergence stayed invisible.

## Why nobody noticed

`2h`'s *"Before"* blocks claim to quote `2d` **verbatim, with line numbers**. Both halves failed
independently: the quoted text no longer exists in `2d` (the amendment *was* applied), and the line
citation points at the wrong block. **A "verbatim" claim reads as more authoritative than an ordinary
one and is checked less often** — the same shape as `2g-tls.md`'s reproduction of `[const §XII.5]`.

See [`engine-accept-path`](./engine-accept-path.md) for the other instance of a signed-off document
re-seeding a superseded model.
