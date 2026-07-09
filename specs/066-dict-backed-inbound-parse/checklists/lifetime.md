# Checklist: Sanitizer / Lifetime Requirements Quality

**Purpose**: Validate that the membership-ownership lifetime requirements (the `table_view` pointer bound by the Parser, the clone-owned copy, the reify owned copy) are specified precisely enough that an implementer cannot introduce a use-after-free, and that the sanitizer acceptance is measurable. (Unit tests for the requirements.)
**Created**: 2026-07-09
**Feature**: [spec.md](../spec.md) · **Focus**: sanitizer/lifetime, mechanism-(b) ownership
**Audited**: 2026-07-09 (see [audit-2026-07-09.md](./audit-2026-07-09.md)) — verdict PASS.

## Session `inbound_tv_` lifetime — Clarity & Completeness

- [x] CHK013 - Is the stable-address requirement for `inbound_tv_` specified precisely — what must be stable (the object's address across the session lifetime) vs the per-message Parser pointer that lives only for one parse — rather than an over-broad "never moves"? [Clarity, Spec §FR-002, Data-model §Entity-1] **PASS:** FR-002 (revised) + data-model Entity-1 (per-parse pointer vs session-lifetime address).
- [x] CHK014 - Is the requirement explicit that the per-message `Parser` binds `std::addressof(*inbound_tv_)` and that this is safe only under serialized dispatch (no concurrent parse reseating the optional)? [Completeness, Data-model §Entity-1] **PASS:** data-model Entity-1 (Parser binds `addressof`, serialized dispatch).
- [x] CHK015 - Is the `open()`-on-reconnect reseat behavior specified as a resolved fact (reconnect uses `drive_reconnect_attempt()`, not `open()`; no reseat-during-parse), not an open hazard? [Consistency, Data-model §Entity-1 (analyze 2026-07-09)] **PASS:** data-model Entity-1 — reconnect uses `drive_reconnect_attempt()`, not `open()` (analyze 2026-07-09).

## Mechanism-(b) copy lifetime — the load-bearing claim

- [x] CHK016 - Is the core lifetime claim stated as a requirement with its evidence — a copied `table_view` is a self-contained OWNING snapshot (no pointer into the `Dictionary`) and therefore safe to outlive the source session/`Dictionary` with no `shared_ptr` pin? [Measurability, Data-model §Reify (b), `table_view.hpp:185-192`] **PASS:** data-model §Reify(b) + source-verified owning snapshot (`as_table_view` `dictionary.cpp:296`, copy ctor `table_view.hpp:185-192`).
- [x] CHK017 - Is there a requirement that the mechanism-(b) accessor's precondition is explicit — it yields membership only when the source view is dict-backed (`opaque_dict_` non-null), and a dict-free source degenerates to an empty copy (clone/reify stays dict-free)? [Completeness, Data-model §Reify precondition] **PASS:** data-model §Reify precondition (`opaque_dict_` null → empty copy).
- [x] CHK018 - Are clone-owned and reify-owned `table_view` copies required to be genuinely OWNED (not aliasing the source view's borrowed `opaque_dict_`), so a handle outliving the session is defined-behavior? [Clarity, Spec §FR-007, Data-model §Clone/§Reify] **PASS:** data-model §Clone/§Reify (OWNED, not aliasing).
- [x] CHK019 - Is the clone/reify symmetry captured as a single requirement (both use the SAME accessor / same ownership model), preventing a half-fix where only one path is lifetime-safe? [Consistency, Contract §C4] **PASS:** Contract C4 unified mechanism (b) — one mechanism, no half-fix.

## Sanitizer acceptance — Measurability

- [x] CHK020 - Is the sanitizer acceptance requirement measurable (ASan/UBSan/TSan over the session + clone + reify membership-ownership paths), not just "validated under sanitizers"? [Measurability, Plan §Constitution-Check, Spec §SC-003] **PASS:** plan Constitution-Check + T015 (ASan/UBSan/TSan over session/clone/reify).
- [x] CHK021 - Does a requirement mandate a witness proving the copy OUTLIVES the source `Dictionary` (destroy the Dictionary, the copy still reads) — the direct pin for the no-dangling claim? [Coverage, Gap, Task T003] **PASS:** T003 (outlives-the-Dictionary witness).
- [x] CHK022 - Is any sanitizer finding on these paths required to be treated as a real defect (not a benign test artifact) absent a reproduction/caller analysis? [Consistency, Constitution Art IX / project policy] **PASS:** plan §Sanitizers + Constitution Art IX / project policy.
