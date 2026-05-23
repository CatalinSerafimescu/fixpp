# F4 design call — codex second opinion (2026-05-23)

**Verdict:** Position A

**Rationale (≤300 words):**
- `specs/005-session-establishment-fsm/data-model.md:15-22` makes duplicate-`Logon` and out-of-scope admin distinct Active-state events, and the Active row at `:22` binds both to `session Reject`; `:26` removes any ambiguity for RR/SeqReset by saying they are a "defined, bounded transition" and "never ... a silent no-op."
- `specs/005-session-establishment-fsm/spec.md:150,156,172-173` repeats the same contract in binding requirements language: FR-001 requires every event in the alphabet, including duplicate `Logon` and out-of-scope admin, to map to an explicit transition; FR-017 says deferred admin receipt is "never undefined and never a silent no-op"; FR-018 defers recovery-dependent conformance, not the bounded-reject disposition.
- `src/session/session.cpp:953-971` documents that `2/4` "still get a Reject," but the actual `is_session_admin` set includes `"A"`, `"2"`, and `"4"`. Because the reject path is only `if (!is_session_admin)` at `:973-1001`, dup-Logon, RR, and SeqReset are silently accepted in Active. That is direct implementation drift, not spec silence. `include/fixpp/core/error.hpp:304-308` also codifies slot 75 as a bounded Reject for RR/SeqReset.
- `reference-engines/` is absent here, so reference engines not consulted. Without local OSS evidence, I would not override explicit 005 design text on "engine convention" grounds.
- The tests that would flip are `tests/session/fsm_matrix_witness_test.cpp:719-751` and `:823-854`; both currently pin the implementation bug (`0` Rejects) and should become `1` Reject. The implementation change is exactly `src/session/session.cpp:964-971` so `"A"`, `"2"`, and `"4"` no longer bypass the reject path.

**Disposition for 010:** ship-in-010 — `[const §XVII.1]` is triggered by design-artifact changes; Position A needs none. This is not a "trivial-feature skip" candidate anyway because it changes on-wire behavior and flips executable witnesses, so the right move is to close the 005 impl drift now rather than preserve it behind 010's scope note.

**Confidence:** high — I would change my mind only if a stronger 005 design artifact than `spec.md`/`data-model.md` explicitly authorized silent ignore for Active dup-Logon or RR/SeqReset.
