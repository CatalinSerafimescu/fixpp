# Checklist: Sanitizer / Lifetime Requirements Quality

**Purpose**: Validate that the membership-ownership lifetime requirements (the `table_view` pointer bound by the Parser, the clone-owned copy, the reify owned copy) are specified precisely enough that an implementer cannot introduce a use-after-free, and that the sanitizer acceptance is measurable. (Unit tests for the requirements.)
**Created**: 2026-07-09
**Feature**: [spec.md](../spec.md) · **Focus**: sanitizer/lifetime, mechanism-(b) ownership

## Session `inbound_tv_` lifetime — Clarity & Completeness

- [ ] CHK013 - Is the stable-address requirement for `inbound_tv_` specified precisely — what must be stable (the object's address across the session lifetime) vs the per-message Parser pointer that lives only for one parse — rather than an over-broad "never moves"? [Clarity, Spec §FR-002, Data-model §Entity-1]
- [ ] CHK014 - Is the requirement explicit that the per-message `Parser` binds `std::addressof(*inbound_tv_)` and that this is safe only under serialized dispatch (no concurrent parse reseating the optional)? [Completeness, Data-model §Entity-1]
- [ ] CHK015 - Is the `open()`-on-reconnect reseat behavior specified as a resolved fact (reconnect uses `drive_reconnect_attempt()`, not `open()`; no reseat-during-parse), not an open hazard? [Consistency, Data-model §Entity-1 (analyze 2026-07-09)]

## Mechanism-(b) copy lifetime — the load-bearing claim

- [ ] CHK016 - Is the core lifetime claim stated as a requirement with its evidence — a copied `table_view` is a self-contained OWNING snapshot (no pointer into the `Dictionary`) and therefore safe to outlive the source session/`Dictionary` with no `shared_ptr` pin? [Measurability, Data-model §Reify (b), `table_view.hpp:185-192`]
- [ ] CHK017 - Is there a requirement that the mechanism-(b) accessor's precondition is explicit — it yields membership only when the source view is dict-backed (`opaque_dict_` non-null), and a dict-free source degenerates to an empty copy (clone/reify stays dict-free)? [Completeness, Data-model §Reify precondition]
- [ ] CHK018 - Are clone-owned and reify-owned `table_view` copies required to be genuinely OWNED (not aliasing the source view's borrowed `opaque_dict_`), so a handle outliving the session is defined-behavior? [Clarity, Spec §FR-007, Data-model §Clone/§Reify]
- [ ] CHK019 - Is the clone/reify symmetry captured as a single requirement (both use the SAME accessor / same ownership model), preventing a half-fix where only one path is lifetime-safe? [Consistency, Contract §C4]

## Sanitizer acceptance — Measurability

- [ ] CHK020 - Is the sanitizer acceptance requirement measurable (ASan/UBSan/TSan over the session + clone + reify membership-ownership paths), not just "validated under sanitizers"? [Measurability, Plan §Constitution-Check, Spec §SC-003]
- [ ] CHK021 - Does a requirement mandate a witness proving the copy OUTLIVES the source `Dictionary` (destroy the Dictionary, the copy still reads) — the direct pin for the no-dangling claim? [Coverage, Gap, Task T003]
- [ ] CHK022 - Is any sanitizer finding on these paths required to be treated as a real defect (not a benign test artifact) absent a reproduction/caller analysis? [Consistency, Constitution Art IX / project policy]
