# fixpp#563, fixpp#564 — a send started inside an application callback (B41)

> **Status: v0.13 (2026-10-14) — v0.12, with three additions to §3's last block: what each cell of
> §2.6 does at the head, as rehearsed in a scratch copy; the cells' names; and four corrections of
> rows.** It changes no statement of the mechanism and no ending, and no Gate A round reviewed it;
> the paragraphs below are v0.12's status and still hold.
>
> **Status: v0.12 (2026-10-13) — v0.7 with one amendment. Gate A, in rounds scoped to the amendment,
> converged at round 6, on v0.10. The owner signed the amendment off on v0.11. v0.12 records that
> sign-off and two rulings of the owner, and applies the analysis pass. No Gate A round has reviewed
> v0.11's edits or v0.12's.** v0.12 changes no statement of the mechanism and no ending that v0.11
> stated. It adds: the sign-off, asked and answered on 2026-10-13 at about 20:50 (§0); ruling R-10,
> by which `reset_on_disconnect` on this route keeps fixpp's behaviour, which is the head's and was
> the one ending v0.11 left with the owner; the owner's placement of fixpp#598 in batch B28; and
> what the analysis pass over the amendment asks of this note: two cells (NC-32, NC-33), one arm
> (NC-21 on the strand fixture), assertions and wording. The last section has one line per finding.
> With the sign-off and the analysis pass, Appendix A's four controls are met for the amendment; §0a
> says which obligations remain at implementation. By the wording recorded with the sign-off, the
> cells and arms v0.12 adds are rows to report to the owner, which is the orchestrator's to do, and
> they are not a new sign-off.
>
> The amendment is §2.6 and the passages that go with it: rulings R-8, R-9 and R-10 in §0, one
> bullet in §0a, one pointer in §2.1a, the last block of §3, the last blocks of §4, §5 and §7 (with
> a pointer on §4's last "Compatibility" bullet), the last block of §6, and the entries for v0.8,
> for rounds 4, 5 and 6, for v0.11, for v0.12 and for v0.13 in the last section. §0's R-8 bullet
> also names a ruling of another note, B28's R-24, which §2.6 follows. The amendment gives one
> ending to the condition that B&L `L-563-6` stated and that fixpp#590 tracks, on the owner's ruling
> of 2026-10-13 (R-8). That row is deleted since the implementation; B&L `B-563-5` has the ending.
> The rounds, each with its two reviews in the parent repository's
> `research/G19-fix-fpml-iso20022/research/reviews/`:
> - Round 4, on v0.8: Codex P1=3 P2=2 P3=1; after judging P1=2 P2=3 P3=8. Not converged.
>   `codex_563_564_4_callback-send_review.md`, `opus_563_564_4_callback-send_triage.md`.
> - Round 5, on v0.9: Codex P1=1 P2=4 P3=2; after judging P1=0 P2=2 P3=11. Not converged.
>   `codex_563_564_5_callback-send_review.md`, `opus_563_564_5_callback-send_triage.md`.
> - Round 6, on v0.10: Codex P1=0 P2=1 P3=2; after judging P1=0 P2=0 P3=9. Converged, and the judge
>   states the tally as unconditional. `codex_563_564_6_callback-send_review.md`,
>   `opus_563_564_6_callback-send_triage.md`.
>
> Round 6's judge asks for no change to the mechanism's four statements. One of its items (O-3) adds
> one test at an exit of the Logout step. v0.11 was one edit made after convergence, as v0.6's and
> v0.7's were. It applied round 6's nine P3 items from the judge's fix shapes and from two
> measurements made after v0.10 was written (the evidence files named `ph8d_*` and `ph8e_*`). The
> analysis pass ran over v0.11 at `9afe3bbd`; its record, with the decision on each finding, is
> `research/G19-fix-fpml-iso20022/decisions/speckit/563-564-callback-send-analyze-phase8.md` in the
> parent repository. v0.7's design is otherwise unchanged. §2.6 was a design when these paragraphs
> were written, with nothing of it in the pull request's tree. Since 2026-10-14 it is implemented
> on this branch: library `9a77b3e3` is the production commit, and the record's sections "Phase 8,
> first part" to "Phase 8, third part" have what was measured in the tree. Before that its
> outcomes were measured on scratch prototypes; §2.6 and §3 name each evidence file and say what
> was read and what was a prediction. The paragraphs below are v0.7's status, and they stand for
> everything else.
>
> **Status: v0.7 — Gate A converged at round 3 (2026-10-11); owner-signed-off 2026-10-11, for Gate A and
> for the plan; post-Gate-A `/analyze` applied.** Issue mode: this note is the plan; there is no
> `specs/<id>/` bundle. Branch `563-564-callback-send`. Round 3 ran on v0.5 and, after judging, left no P1
> and no P2 (Codex had raised two P2 findings; both were judged P3). v0.6 applied that round's P3 items,
> except the one the owner declined (Codex F-2). `/analyze` then ran over v0.6, and v0.7 applies its
> findings. Neither v0.6's edits nor v0.7's were reviewed by Codex. The design is implemented on this
> branch (2026-10-11 to 2026-10-13); the record has each phase. The last
> section, "Gate A", has the tallies, the reviews, and how each round and the `/analyze` pass were applied.
>
> **Read, not run, with the exceptions named here.** §1's measurements were run before the design, and the
> record has them. Of the design's own outcomes, what has been run is: §4's searches; the measurements
> of predictions P-4 and P-11 (2026-10-11; both agree); the measurements made on today's code before
> any production edit (2026-10-11), each with its start posted: P-1 for a close started in `toApp`, P-1a,
> P-1b, P-1c, P-2, P-3, P-5 and P-6, and an early reading of P-10 with a plain post; and the measurements
> made during implementation, each on a scratch build of its mechanism: P-7 and the first part of P-10
> (2026-10-11), then the rest of P-10, P-1 for a close started in `toAdmin`, P-8 and P-9 (2026-10-12).
> None disagrees. §3 has each result with its evidence file. The Gate A rounds, their rewrites, and the
> edits of v0.6 and v0.7 ran nothing else. Every other outcome below that depends on the order of
> handlers on the strand, or on what a sanitizer reports, is still a prediction from reading. §3 lists
> each row, under "Predictions to measure first", with the measurement that settles it and what follows
> if it disagrees.
>
> **How this design was reached.** Both issues were re-measured after B35, then widened by measurement
> (`Engine::send`, `toAdmin`, both store kinds, several io threads, a close started from a callback). The
> FIX session-layer documents and the send paths of QuickFIX C++, QuickFIX/J and QuickFIX/Go were read. One
> Fable consult derived the condition in §1; it reported `probable`, so the owner decided (§0).
>
> The records are in the parent repo:
> - `research/G19-fix-fpml-iso20022/decisions/speckit/b41-563-564-measurement.md`: every measurement quoted
>   here, with the cells' output, the engine comparison table and the owner's rulings. "The record" below
>   means this file.
> - `research/G19-fix-fpml-iso20022/research/reviews/fable_b41_callback_send_contract_consult.md`: the consult.
> - `research/G19-fix-fpml-iso20022/research/probes/b41_563_564/`: the deferral-cost probe p1 and its build
>   script; P-4's probe (`p4_post_cancellation.cpp`, `build_p4.sh`) with its output
>   (`p4_post_cancellation.output.txt`); P-11's hook (`p11_hook.patch`) with its runs
>   (`p11_runs.output.txt`); the temporary cells of the other measured predictions
>   (`p0b_measurements.patch`) with their runs (`p0b_runs.output.txt`); and the evidence of each
>   implementation phase, which the record names where it reports that phase (list the directory;
>   this bullet does not enumerate it).
> - `research/G19-fix-fpml-iso20022/research/reviews/analyze_563_564_callback-send.md`: the `/analyze`
>   report over v0.6, with the orchestrator's dispositions.
>
> The cells are on this branch: `tests/session/test_563_564_callback_send.cpp` (suite `CallbackSend`) and
> `tests/session/test_563_564_engine_callback_send.cpp` (suite `EngineCallbackSend`). They were
> measurement cells while this note was written, several RED or aborting by design; each now carries
> the assertions §3 gives it.

## 0. Owner rulings (2026-10-11). Not reopened at Gate A; Gate A reviews whether this note implements them.

- **Principle.** fixpp keeps QuickFIX's observable behaviour, callback semantics included. The internal
  implementation may differ. An earlier fixpp clause that departs from it
  (`specs/019-app-callbacks/spec.md` FR-006, "enqueued behind the current dispatch") is history for this
  decision, not a constraint.
- **R-1 — `Session::send`.** A send on a session waits, by one post onto that session's executor, only
  while that session has `toApp` or `toAdmin` on the stack. Otherwise it runs inline. So a send started in
  `fromApp`, `fromAdmin` or `onLogon` is numbered, and its `toApp` runs, inside the callback, as in the
  three QuickFIX engines. Chosen over deferring from every callback.
- **R-2 — `Engine::send`.** Its control-strand lookup is replaced by the published session snapshot, in
  this PR, so that a call from a session's own callback reaches `Session::send` nested by construction and
  no longer depends on `control_strand_` being idle. Chosen over documenting "nested or deferred" and over
  always deferring.
- **R-3.** The session closed from inside one of its own callbacks is to be measured under a memory
  checker. The existing ASan preset could not reach it (§2.5); the measurement moves to this PR's
  verification.
- **R-4 — `Session::close`.** A close in either mode, started while any callback of that session is on the
  stack, begins after the callback returns. So the close never runs inside the callback, and a requested
  Logout is never emitted inside the caller. The close then meets the arm that called the callback as a
  close from another thread does today: while that arm is suspended, or after it has finished. Chosen over
  a check after each inbound callback, and over leaving the arm to carry on as QuickFIX C++ and J do. It is
  QuickFIX/Go's shape.
  - **Its reading, fixed by the owner after Gate A round 1.** v0.3 added a consequence: "an arm never runs
    on a session that its own callback's `close()` closed". Round 1 showed that one post does not deliver
    it (§2.1a), and the owner withdrew the sentence. The rule is "after the callback returns". Chosen over
    waiting for the whole operation that called the callback, and over a split rule for outbound emits;
    both are in §5.
- **R-5 — `Engine::stop()` started from a callback is out of this PR** (owner, after round 1). The note
  claims nothing about it beyond one effect of OD-3 on that route (§2.1a), and adds no cell for it;
  `ApplicationEngineSend.SendDrainRaceNoUAF`, a shipped cell, is on that route, and §3 describes it as
  what it is. What was read about that route is in
  §2.1a, the limitation is in §4, and the follow-up is fixpp#579. Chosen over a post at `stop()`'s entry
  in this PR.
- **R-6 — store order is a requirement** (owner, after round 1). "Calls to `store()` complete in the order
  they were made" becomes a documented requirement of `MessageStore::store`: a doc comment in the header,
  a B&L row, and a cell whose store completes out of order and shows the consequence (§2.1, §3, §4).
  Chosen over disclosing it as a limitation of send ordering.
- **R-7 — same-thread re-entry is outside the contract** (owner, after round 2). A callback must not run
  the session's own executor. The exposed setup is one: a `direct_executor` session attested over a bare
  `io_context` whose callback runs that `io_context` before it returns. The default mode,
  `per_session_strand`, is immune (§2.1 has the condition). The PR adds one sentence to `session.hpp` and
  to `session_executor.hpp`, widens B&L `L-007-2` from concurrent entry to same-thread re-entry with the
  same disposition, and adds debug assertions on the routes (§2.1 step 4, §2.1a, §2.2). Chosen over
  refusing the send at run time and over a callback-exit latch; both are in §5.
- **OD-6 is withdrawn: this PR declares no C-ABI change and carries no version bump** (owner, after
  round 2). The ruling it replaces ("declared breaking") rested on a sentence of v0.4 that round 2 showed
  to be false by reading: outside the route R-5 sets aside, OD-3's stop check refuses no send that
  today's code admits (§2.4). OD-3 stays,
  as the hit path's replacement for the control-strand read of `stopped_`. The note carries one
  first-step measurement on the base tree (§3, P-11); if it disagrees, the C-ABI question returns to the
  owner before the PR proceeds. It was run on 2026-10-11 and agrees (§3). Chosen over keeping the bump and
  over measuring before deciding.
- **OD-5 — the `Engine::send` bench is in this PR and its manifest row is `paired`** (owner, after
  round 2), isolated from the socket write. A `paired` row cannot be removed afterwards, and CI does not
  compare it in the PR that adds it, so this PR's evidence is a local paired run (§3, "Cost"). The bench
  is single-threaded, because the selection rule in the header of `bench/ci-suite.txt` admits only
  single-threaded, CPU-bound binaries to the manifest, and so to its paired tier; as the PR's gated
  evidence it replaces the foreign-thread probe that the first OD-5 ruling promoted; and that probe is
  kept as a local base-against-candidate measurement, reported in the verification record whichever way
  it falls and not gated (the orchestrator's disposition of 2026-10-11; the owner was told and can
  overrule it). `L-023-2` closes for the send route; its establish-churn half is rewritten and stays open
  (§4). Chosen over an in-tree bench that is not paired, and over accepting two probes as the PR's
  evidence.
- **R-8 — one ending for a terminal close started in the `toAdmin` of a graceful close's Logout**
  (owner, 2026-10-13, on the Phase 7 report; the date is this bullet's, not the heading's). The
  condition is the one B&L `L-563-6` stated until §2.6 was implemented: on a `FileStore` with a live
  transport it had two endings, and timing chose. The owner was asked whether the pull request gives
  the condition one ending before it merges, or merges with the split stated. The first option, as
  it was put: "Fix here: this
  follows your rule. QuickFIX's ending is 'stored, not transmitted'. It means a production change in
  `session.cpp`'s Logout step, new cells and mutants, and the affected verification again. B28 is
  editing `Session::close` at the same time." The second: "Merge as is: the split stays in the shipped
  library until batch B50." The answer, in the owner's words: "Fix here".
  - **Its reading.** The one ending is QuickFIX's: the Logout is numbered, stored where there is a
    store, and not transmitted. Two standing rules of the owner give it: one condition has one
    observable ending, and fixpp keeps QuickFIX's observable behaviour while its internals may differ
    (the Principle above). §2.6 is the design. Chosen over merging with `L-563-6` stated.
  - **Where it is recorded.** The task list `decisions/speckit/563-564-callback-send-tasks.md` in the
    parent repository, "Owner rulings on the Phase 7 report (2026-10-13)", and the record's "Phase 7,
    seventh part". The task list has the answer and the ending, and the record has the answer. The
    two options are quoted here as the orchestrator gave them to this revision's author; neither
    file holds the question's full text.
  - **A ruling of another note that §2.6 follows: B28's R-24** (2026-10-13). With `reset_on_logout`
    set, a graceful close whose Logout was not sent makes no teardown reset: `logout_seen_` is set
    only where the Logout's emit returned success. It is recorded in
    `.specify/536-537-538-recovery-liveness.md`, §0, on the branch `536-537-538-recovery-liveness`
    (v0.16, its part "After ruling R-24"; commit `ee7309df`; head `902630b6` when first read; read
    again for v0.11 at `7a7ed633`, which is that note's v0.17). In `run_logout_phase1` at both heads
    the assignment of `logout_seen_` follows the early return for a Logout that was not sent, and
    runs only when `store_then_emit` returned success for the Logout (read:
    `git show 7a7ed633:src/session/session.cpp`; the two source files are the same at both heads:
    `git diff 902630b6 7a7ed633 -- src/session/session.cpp include/fixpp/session/session.hpp` prints
    nothing). At v0.16 that note said that it had the ruling as its orchestrator passed it on, with
    neither the question put to the owner nor the owner's words. Its v0.17 adds the question and the
    label of the option the owner chose. Nothing of either is quoted here. One of the reasons that
    note gives for the ruling is this note's v0.8, §2.6, so the two notes agree by one ruling and
    not by two independent findings. §2.6 states the rule as R-24's: the flag is set only where the
    Logout was sent. It answers the first owner question of Gate A round 4.
- **R-9 — the bound: a cut Logout's store call that is still waiting when the logout timeout
  expires** (owner, 2026-10-13, after Codex's review of Gate A round 5; the date is this bullet's).
  The question, as it was put: "The cut Logout's store call is still waiting when the logout timeout
  expires, for example because another write of the same session holds the store's lock that long.
  What should happen? 1. Bounded: the timer ends the close and that store call is cancelled. Nothing
  is stored for the Logout. Its number follows your B28 ruling R-16 once both are merged (taken
  back); on this branch alone it stays consumed. A terminal close and `Engine::stop()` never wait
  longer than the logout timeout for a store that is stuck at the lock. 2. Unconditional 'stored':
  that store call is never cancelled. The Logout is always stored, and a terminal close and
  `Engine::stop()` wait for the store however long it takes. QuickFIX's store is a blocking call, so
  it behaves this way." The owner's answer: "1".
  - **Its reading.** R-8's "stored" holds where the Logout's store call is not ended by the grace
    timer. Where that call is still waiting for the store's lock at the expiry, the timer ends the
    close, nothing is stored for the Logout, and its number follows B28's R-16 once both are merged.
    §2.6, "The bound", is the one statement of it, and every other place refers there. Chosen over
    never cancelling that store call.
  - **What the option's sentences do not cover.** §2.6, "The bound", lists it, point by point, each
    with its status. The option's last sentence is about a store that is stuck at the lock, and the
    points are outside it. One of them is measured, and it goes against the option's first sentence
    for its case: where the Logout's own write job is still in the file executor at the expiry, the
    timer does not end the close there. Both `close()` calls complete when the file executor has run
    that job (`ph8e_predictions.md`). The others are read, not run: a write already inside its
    system calls; `Engine::stop()` as a whole; the teardown reset with `reset_on_disconnect` set;
    the teardown that follows the join. They are not new questions: each is what the head does on
    its own routes.
  - **What the owner was told.** The task list that records the ruling (the next bullet) also
    records this, in its section "What the owner was told about the bound of R-9 (2026-10-13), and a
    correction owed to it". That section is the orchestrator's account of the session. It records
    four things, all on 2026-10-13, and no reply of the owner.
    - At about 18:30, the four points that are read: the bound holds for the step of
      `Engine::stop()` that joins the close, and not for `stop()` as a whole; with
      `reset_on_disconnect` the teardown reset waits for the store's lock with no timer; a write in
      progress, and a store that does not give its call up, are waited for; the timeout bounds the
      join and not the whole close.
    - At about 19:15, after the first run of the own-write scenes: "never wait longer than the
      logout timeout" is true for a store call waiting at the store's lock, and false when the
      Logout's own write job is still queued in the file executor, also with the Logout not cut. The
      same message said that on the cut route nothing is then transmitted and the store is left with
      a number it cannot read, and it named the issue of that store state (§7).
    - Then, before the re-run: that this store state may come from the test's gate, which let later
      jobs pass the job it kept (round 6's O-1), while the timing stands.
    - At about 20:05, with the re-run's result (`ph8e_predictions.md`): on a file executor of one
      thread that keeps order, the write runs before the cancellation's job, and the Logout is
      written and reads back, 40 of 40. The unreadable record needs the cancellation's job to run
      first: with two threads and the write's thread delayed, 40 of 40; with two threads let go
      together, 1 of 40. The timing is unchanged on every kind of executor.
    §2.6, "The bound", (a), states the corrected fact. Two bullets of "The bound", the ones after
    (e), are this note's readings; that section does not record them as told.
  - **Where it is recorded.** The task list `decisions/speckit/563-564-callback-send-tasks.md` in the
    parent repository, "Owner ruling during Phase 8 (2026-10-13): the cut Logout's store call at the
    logout timeout", which has the question and the answer.
- **R-10 — `reset_on_disconnect` on the route of §2.6 keeps fixpp's behaviour; and, by the same
  answer, the sign-off of the amendment and the placement of fixpp#598** (owner, 2026-10-13, at
  about 20:50; the date is this bullet's). The orchestrator put three questions to the owner, with a
  summary of §2.6 at v0.11: "1. Sign-off of the amendment, or what you want changed. I will not make
  the production commit before it. 2. `reset_on_disconnect` on this route: keep fixpp's behaviour
  (my recommendation) or match QuickFIX. 3. fixpp#598: fold into B28 or keep as its own batch." The
  owner's answers, in the owner's words: "1. Signed off." "2. Keep fixpp". "3. FOld into B28".
  - **R-10 is the second answer.** With `reset_on_disconnect` set, the teardown reset of
    `Session::close` runs on this route as it does on every close: after the Logout step, it empties
    the store and leaves both next numbers at their initial value. That is what the head does and
    what every prototype did; §2.6 does not touch the reset. QuickFIX C++ resets inside
    `disconnect()` and then stores the Logout under its old number. The difference is stated (§2.6's
    comparison table) and not removed. Chosen over matching QuickFIX's order. NC-33 (§3) pins the
    ruled ending.
  - **The sign-off is the first answer.** It is of the amendment as §2.6 states it at v0.11, library
    commit `9afe3bbd`. The task list records with it: what the analysis pass changes in the note is
    wording and rows, and is reported to the owner; a change of the mechanism or of an ending would
    need the sign-off again. v0.12 is inside that wording (§0a).
  - **The placement is the third answer.** fixpp#598 is batch B28's work. Batch B56 is kept in the
    batch plan as the record of the finding. B28 will change the store's catch of
    `operation_aborted`, which arms of this amendment read; §2.6, "With batch B28", names them.
  - **Where it is recorded.** The same task list, "Owner rulings on the amendment's sign-off summary
    (2026-10-13, about 20:50)", which has the questions and the answers.

The record's sections "Gate A round 1, and three owner rulings from it" and "Gate A round 2, and the
owner rulings from it" have R-4's reading, R-5, R-6, R-7 and the OD-5 and OD-6 rulings as the owner gave
them; the numbers R-5, R-6 and R-7 are this note's. Its section "Gate A round 3: converged, and the
owner's sign-off" has the Gate A sign-off and the plan sign-off (§0a). "B35's ruling R-10" below is a
ruling of another note (`.specify/544-hot-path-zero-alloc.md`), not one of these.
R-8, R-9 and R-10 are this note's numbers too, added in v0.8, v0.10 and v0.12; each bullet says
where its ruling is recorded. This note's R-10 is the ruling on `reset_on_disconnect`. The other
note's is always written "B35's ruling R-10" here.

OD-1, OD-3 and OD-4 are not owner rulings. They are the decisions v0.3 left open, which Gate A round 1
settled; §6 states them. OD-1 is the result of a deferred send that is cancelled before it starts
(used in §2.1), OD-3 is the stop check on `Engine::send`'s hit path (§2.3, §2.4), and OD-4 is the
statement that `Engine::send` from a callback of an unpublished session cannot be admitted (§2.3).

## 0a. Constitution triggers

- **`[const §XVII.1]`, Gate A.** The change touches the public C++ behaviour of `Session::send`,
  `Session::close` and `Engine::send`, the cancellation and executor model, and the message store contract
  (R-6). Gate A applied. It ran as the loop the last section records, and it has converged.
- **`[const §XI.7]` and Appendix A, the four controls.** Appendix A's rows this change falls under are
  threading (a new await on two entry points, a new route for `Engine::send`) and error semantics
  (`dispatch_aborted` returned by `Session::send` stops implying `Disconnected`, OD-1). They are discharged
  in issue mode as `.specify/495-493-486-dict-reify-copy.md` §0.2 did:
  - Codex Gate A: converged at round 3, on v0.5 (last section).
  - `/clarify`: by hand. The owner rulings of §0 are its record.
  - `/analyze`: run on 2026-10-11 by the spec-analyzer over v0.6, after Gate A converged and before
    implementation. v0.7 applies its findings.
  - Owner `/plan` sign-off: given 2026-10-11. The owner signed off the converged note at round 3 and
    confirmed the same day that the sign-off covers the plan; the record has both. Asked whether the Gate
    A sign-off also stands as the plan sign-off, the owner answered: "Yes, it covers the plan. Push the
    branch, then proceed with implementation" (the record, "Gate A round 3: converged, and the owner's
    sign-off").
- **`[const §VII.3]`, TDD.** Each cell lands in the PR's tree with its final assertions before the
  production edit it witnesses, and the verification record shows it RED there (an abort or a failed
  assertion on at least one of the two build types). The scratch builds of §3 are never the PR's tree. A
  cell that is green on both build types before the production edit is recorded as a pin of existing
  behaviour, by name.
- **`[const §VIII.5]`, `[const §XV.1]`, `[const §XI.6]`, allocation.** The path those articles scope
  (parse to `fromApp`; parse, validate, dispatch; `MemoryStore::store`) gains no allocation; B35's gates
  are re-run (§3). The deferred path allocates per send: its coroutine frame, the payload copy and, where
  the outer send holds the slot, the fallback leaf's frame (§2.1, "Cost"). Only a send started inside
  `toApp` or `toAdmin` takes it, which is outside `B-544-1`'s "sequential `Session::send`" and inside
  what `L-497-1` leaves open.
- **`[const §VIII.3]`, "No perf change merged without a benchmark in the same PR".** R-2 changes the route
  of every `Engine::send`, and §2.2 adds counter writes to every application callback (§2.1, "Cost", has
  the one statement of how many). The note claims no speed-up. The PR carries a bench for the route, with
  a `paired` manifest row (OD-5), and a local paired run of that bench and of the in-tree benches that
  cover the counters and the depth test (§3, "Cost"). `[const §VIII.2a]` applies to the new row: CI does
  not compare it in this PR, and it cannot be removed afterwards.
- **`[const §X.7]`: no C-ABI change is declared** (OD-6 withdrawn). `fixpp_session_send` reaches
  `Engine::send`. The C ABI's symbols, types and error codes do not change. §2.4 has why no call that
  succeeds today fails afterwards, §4 ("Compatibility") has the statement a consumer reads, and P-11 (§3)
  measured the reading on the base tree and agrees with it. R-1 and R-4 are not reachable through the C
  ABI's blocking calls, which deadlock inside a callback today and still do (`L-054-1`).
- **`[const §IX]`, coverage, sanitizers, static analysis.** §3 has the three sanitizers, the coverage
  measurement with the dispositions it fixes in advance, and the tools Article IX §4 names.
- **`[const §XVII.3]`, independence.** The note's author is the Opus orchestrator. v0.4 and v0.5 were
  written by Gate A's rewrite agent (`[const §XVII.5]`). v0.6's edits were applied after convergence by a
  sub-agent working from the judge's fix shapes, and v0.7's by a sub-agent working from the `/analyze`
  report; neither set was reviewed by Codex (last section). Codex has implemented nothing here, so its
  Gate A review is independent of all of them. If Codex later implements or fixes under escalation, that
  PR's Codex review comes from a separate Codex session.
- **`[const §XVII.7]`, the local build.** Before the PR is opened, the article's cycle (Conan install,
  configure, build, ctest) runs locally on `linux-clang-debug`, and the PR description carries the
  article's one-line confirmation. The owner approves each local build before it runs. §3 names the Linux
  presets its cells and gates run on. The MSVC leg is a local build and run in the sandbox, by
  `research/G19-fix-fpml-iso20022/msvc-local-build-procedure.md` in the parent repo: the B41 test binaries
  and every test target the PR touches, on `windows-msvc-debug`, a build with assertions, and on
  `windows-msvc-release`. Its result goes into the verification record.
- No wire format, parser, codegen or security surface changes. The session FSM gains no state; what
  changes is when a `close()` started inside a callback begins.
- **The amendment (§2.6), trigger by trigger.** The bullets above are v0.7's. For §2.6 alone, as of
  v0.12:
  - **`[const §XVII.1]`, public C++ API: touched.** No signature changes. What `Session::close` does
    with a graceful close's Logout changes on this route, against the head and against `main` (§2.6,
    "The behaviour change").
  - **`[const §XVII.1]`, session FSM: touched.** No state is added. On this route `LogoutSent` is no
    longer recorded: the session goes from Active to `Disconnected`, and v0.10 makes that write when
    the cut is decided, before the Logout is numbered and stored.
  - **`[const §XVII.1]`, concurrency, cancellation and executor model: touched.** v0.8 called it not
    determined. v0.9 changed `close()`'s join: for a Logout that was cut, a terminal close that joins
    does not cancel the grace timer, which narrows 093 plan OD-29 on this route, and the direct close
    of the transport after the join gains a condition (§2.6, "The mechanism", statement 4). What
    follows from it at the grace timer's expiry is the owner's ruling R-9 (§2.6, "The bound"). The
    amendment adds no await, no post and no lock.
  - **`[const §XVII.1]`, message store contract, wire format, parser, codegen, security: not
    touched.** `MessageStore` is not changed. `store_then_emit` is a private member of `Session`.
  - **So Gate A applies.** It ran as rounds scoped to the amendment. Round 4 reviewed v0.8 and round
    5 reviewed v0.9; neither converged. Round 6 reviewed v0.10 and converged (last section). v0.11's
    edits and v0.12's were not reviewed.
  - **`[const §XI.7]` and Appendix A, the four controls: met for the amendment.** The appendix's
    "Threading / concurrency" row is touched, as above. **Its "Session FSM" row is touched too**
    (round 6's F-3). On this route the amendment removes a transition, because `LogoutSent` is no
    longer recorded, and it moves the edge out of Active, because `Disconnected` is written earlier,
    when the cut is decided. It also leaves a number consumed that the peer has not received, which
    the existing resend path then meets. v0.10 called this row not determined, because its second
    column gives "state additions, recovery semantics, gap-fill rules" and §2.6 adds no state. That
    column is headed "Examples" and is not a definition. The appendix says that article-level
    trigger clauses must match it, and Article XVII §1's clause is "Touches the session FSM", which
    the bullet above already classes as touched. The four controls, each with its state:
    - `/clarify`: by hand, with R-8, R-9 and R-10 as its record. No question is with the owner.
    - `/analyze`: run on 2026-10-13 over v0.11 and over the task list's block for §2.6, by a
      read-only agent. Its record is `decisions/speckit/563-564-callback-send-analyze-phase8.md` in
      the parent repository: 23 findings, each with the orchestrator's decision. v0.12 applies the
      ones that name this note (last section). The ones that name the task list are the
      orchestrator's, and by the decision on finding D-2 no library commit of the implementation is
      made before both carry them.
    - Codex Gate A: converged at round 6, on v0.10.
    - The owner's sign-off: given on 2026-10-13 for the amendment at v0.11 (§0, R-10's bullet).
      v0.12 changes no statement of the mechanism and no ending that v0.11 stated. What it adds is
      two cells, one arm, assertions and wording. By the wording recorded with the sign-off these
      are to be reported to the owner and are not a new sign-off; the analysis pass's record says
      the same. The report is the orchestrator's.
  - **What remains owed, all of it at implementation.** The bullets below state each: an arm that is
    RED in the pull request's tree before the production edit, for each clause that changes
    behaviour (`[const §VII.3]`); B35's gates and the paired benches again on the tree that carries
    the change (`[const §VIII.3]`, `[const §VIII.5]`; §3, "What is run again"); and v0.7's bullets
    above on the local build cycle and the MSVC leg (`[const §XVII.7]`), which the tree with the
    change runs again. No Gate A round reviewed v0.11 or v0.12. Round 6's judge asked for v0.11's
    items as an edit after convergence, the one test at an exit among them, and v0.12 changes no
    statement of the mechanism. Status at 2026-10-14: the RED arms in the pull request's tree are
    in the record ("Phase 8, second part"), and so are a whole build and a full `ctest` of
    the tree that carries the change, on two presets ("Phase 8, third part"). B35's gates, the
    paired benches, the other sanitizers, coverage and the MSVC leg on that tree were not in when
    this status was written.
  - **`[const §X.7]`: no C-ABI change.** No symbol, type or error code changes. The condition needs a
    `close()` started inside `toAdmin`, and the C ABI's close is a blocking call that deadlocks inside
    a callback (`L-054-1`), as the bullet on `[const §X.7]` above says for R-4.
  - **`Session`'s layout grows by two members,** the count and the record of the cut (§2.6, "The
    mechanism"). It is a C++ layout change before the first release, like the ones §4
    ("Compatibility") lists.
  - **`[const §VII.3]`, TDD.** Each clause of §2.6 that changes behaviour has an arm that is RED in
    the pull request's tree before the production edit. §3's last block says of each cell and arm
    whether it is RED first, or a control or a pin that is green at the head, and why. Where an arm's
    state at the head was not run, the row says so.
  - **`[const §VIII.3]`, `[const §VIII.5]`.** `store_then_emit` is on every emit. It gains one
    parameter and one test, and its coroutine frame can grow by the parameter. A frame over asio's
    recycler limit allocates (the comment on `send_fallback_leaf_` in
    `include/fixpp/session/session.hpp`). So B35's gates and the send path's benches are run again on
    the tree that carries the change (§3's last block).
  - **`[const §XVII.3]`, independence.** The amendment's text, v0.8 to v0.12, was written by a
    sub-agent from the orchestrator's briefs, from the rounds' triages, from the analysis pass's
    record and from the evidence of Phases 8a to 8e. The prototypes were built and measured by
    implementer sub-agents. Codex wrote none of it.

## 1. What is wrong, and the one condition behind it

Measured at `22702dc9` (the record has each cell's output):

- On a build without `NDEBUG`, a `Session::send`, an `Engine::send` or a `Session::close` started inside
  any application callback aborts the process at the assertion in `callback_dispatch_scope`
  (`include/fixpp/session/session.hpp`). That is fixpp#564; the close case is new.
- On an `NDEBUG` build, a send started inside `toApp` or `toAdmin` runs nested:
  - at an emit that calls the callback before `assign_outbound()`, two frames carry one MsgSeqNum
    (fixpp#563; measured for the application send and for the Heartbeat that answers a TestRequest);
  - at an emit that assigns before the callback, the numbers differ but the nested frame is stored and
    transmitted ahead of the outer one, and a volatile store does not retain it (measured for a session
    Reject); a persistent store refuses it and the session disconnects.
- A send started inside `fromApp` works on an `NDEBUG` build.
- `Engine::send` from a callback runs nested on a single-threaded `io_context`. On three io threads the
  same call is nested or deferred depending on whether `control_strand_` is idle at that instant.
- A graceful `Session::close` started inside `toApp` or `toAdmin` does the same through its Logout: one
  number on two frames at a callback-first site, the Logout ahead of the outer frame at an assign-first
  site.
- On a session with no store the defect needs no suspension at all: the inner frame is transmitted while
  the starting callback is still on the stack.
- With the nested start wrapped in `asio::post`, every failing cell passes, on both store kinds and with no
  store.

**The condition.** An emit is one synchronous stretch of a strand handler: from `peek_outbound()`, through
the frame build, the callback and `assign_outbound()`, to the store call, whose first suspension is a post
in both `MemoryStore::store` and `FileStore::store`. `assign_outbound()` cannot suspend inside that stretch,
because no `SeqnumManager` method holds its mutex across a `co_await`. The order of assignment, the order of
store calls and the order on the wire are therefore the same unless a second send body enters the stretch,
and the only way in through an `Application` callback is a synchronous re-entry through the callback that
the stretch itself calls: `toApp` or `toAdmin`. The inbound callbacks are never inside such a stretch: at
every arm the peek follows the callback.

The stretch also runs code the application supplies through configuration and not through
`Application`: the synchronous part of a custom `MessageStore::store`, and a config-time `transport_send`.
A send started in either re-enters the stretch the same way. This PR does not guard that, today's guard
does not see it either, and nothing in the design depends on it (§2.1a, "Which callbacks count").

One emit has a shorter stretch. The SequenceReset-GapFill of a resend replay (`emit_gapfill_async` in
`replay_outbound_range_`) is numbered with the start of the gap, calls `toAdmin` and transmits; it assigns
nothing and stores nothing. Its NewSeqNo can be a peek taken before the `toAdmin`. The same argument
covers it, with the same scope: between its build and its transmit the only way in for another send body
through an `Application` callback is that `toAdmin` (the paragraph above has what configuration
supplies), so a send that waits for the callback to return is numbered at or after that NewSeqNo and
transmitted after the GapFill (§2.1's table has its row).

So the rule is not about which callback, and not about which emit site. It is: **no emit body on session S
may begin while S has an outbound callback on the stack.** R-1 is that rule for `Session::send`; §2.1a
applies it to the other entry point that begins an emit.

Re-derive the premises, do not trust this paragraph:
- the outbound callback invocations: `grep -n "application->toApp(\|application->toAdmin(" src/session/session.cpp`;
  each is inside a lambda passed to the stack-arena overload of `parse_and_dispatch_`;
- the lock scopes: every `co_await` in `src/session/seqnum_manager.cpp` is a lock acquisition or follows
  the end of its scope;
- the stores' first suspension: `MemoryStore::store` in `include/fixpp/session/memory_store.hpp` and
  `FileStore::store` in `src/session/file_store.cpp`. `FileStore::store` reads its executor before the
  post; that read does not suspend;
- the peek-then-callback order per site: `grep -n "peek_outbound()" src/session/session.cpp`. The search
  is keyed on the call, not on one declaration spelling, so that a peek written as an argument or inside
  a conditional is seen and classified instead of skipped. For each hit that is code and not comment,
  decide whether the value becomes a frame's MsgSeqNum. Where it does, read on to the first of
  `fire_to_admin_(`, `->toApp(` or `assign_outbound()`. Where it does not (an argument of a comparison,
  for example, or a GapFill's NewSeqNo), it is not an emit site;
- the GapFill emit: `emit_gapfill_async` in `src/session/session.cpp`, which calls `fire_to_admin_` and
  then its transmit, with no `assign_outbound()` and no store call between them.

## 2. Design

### 2.1 `Session::send`: one decision point (R-1)

**State.** For R-1 `Session` gains a strand-confined counter, the outbound-callback depth (§2.2 lists
every member the PR adds). It is a plain integer,
not an atomic: it is written and read only on the session's serialisation domain, like `fsm_state_`. It
exists in every build type.

**Writer.** One place: the stack-arena overload of `parse_and_dispatch_`, which is the only route by which
`toApp` and `toAdmin` are invoked (`fire_to_admin_`, `send_impl`, `inbound_business_reject_leaf_`). An RAII
scope increments the depth before the callback and restores the previous value after it, on every exit,
including a throwing callback. The order inside the overload is: its `callback_dispatch_scope`, then
§2.2's assertion on this depth, then the increment, then the callback (§2.2, "The members, and who reads
which"). The inbound overload (`fromApp`, `fromAdmin`) and the `onLogon`, `onLogout`
and `onCreate` sites do not write it. On a build with assertions the `onLogon` and `onLogout` sites read
it (§2.2).

Writing it in the overload, not at each call site, is deliberate: a future emit that calls `toApp` or
`toAdmin` gets the guard without being told about it. The implementer confirms the overload has no other
kind of caller (`grep -n "parse_and_dispatch_(" src/session/session.cpp`, read each).

**Reader.** `Session::send`'s body moves into a private coroutine that takes one argument: whether the
depth test applies. The public `send` becomes a plain function that returns that coroutine's awaitable
with the argument set to "apply", the shape `on_inbound_frame` has today, where it returns its arm's
awaitable; it adds no coroutine frame. The test sits after the Active check and before the frame slot is
taken:

- the argument says "skip", or the depth is zero: nothing changes. The send proceeds inline as today.
- otherwise: the send awaits the deferred path, a separate coroutine, and returns its result. That
  await sits outside the body's existing `try`, so nothing the deferred path returns is converted
  from `operation_aborted` into `Disconnected` there.

**The deferred path**, in order, in one coroutine frame that lives until the send has completed:

1. Copy the payload into storage that frame owns ("The payload", below).
2. Await one `asio::post` onto the session's executor (`asio::deferred`).
3. Apply asio's own cancellation test by hand ("Cancellation at the hop", below). If it holds, return the
   OD-1 error.
4. Assert that the outbound-callback depth is zero.
5. Return `session_invalid_state_for_send` if a `close()` has begun (`state_ == lifecycle::closing`); a
   close that has finished is refused by step 6's Active check, with the same error.
6. Await the private coroutine on the copy, with the argument set to "skip": the Active check, the slot
   holder, and today's conversion for a send in flight, and no depth test.

**When the post does not move the send out of the callback (R-7).** The post is what takes the send off
the callback's stack, and a post onto the session's executor is held back only while the handler that
called the callback is that executor's:

- **`per_session_strand`, the default, is immune where the session's entry points run on its strand,**
  which is where the reentrancy contract in `session.hpp` puts `on_inbound_frame()` and `send()`. The
  session's executor is a strand. While the callback runs in a handler of that strand, a post onto the
  strand waits until the handler returns, and a callback that runs the `io_context` runs other handlers,
  never this strand's.
- **A session under the Engine is immune whatever its `mode`.** `Engine::start()` makes a strand for each
  registered session and spawns the role loop onto it, and `Session::open()` adopts that strand as the
  session's executor (`engine_adopt_strand`) before it looks at `mode`. The C ABI's sessions are the
  Engine's.
- **`direct_executor` attested over an explicit strand is immune** for the same reason, and over a
  one-worker `thread_pool` because a handler cannot run the pool.
- **The exposed setup is one:** a `Session` that an application drives itself, in `direct_executor` mode
  attested over a bare `io_context` executor, with a callback that runs that `io_context` (`poll`,
  `run_one`, `run_for`) before it returns. The posted continuation then runs inside the callback.

The exposed setup is outside the contract: a callback must not run the session's own executor. The
attestation is defined as an executor on which the engine can keep accessing session state "as if it were
running under a strand" (`.specify/2d-threading.md`, the `direct_executor` attestation note), and a strand
never runs a second handler of the session inside the first. The shipped code is already broken there
beyond sends and closes: the callback's own run of the `io_context` can run the liveness loop's emit
inside `toApp`, which is fixpp#563 with no send involved, and can run the driver's next frame delivery
inside `fromApp`. On a build with assertions each of those aborts today at the next callback's
`callback_dispatch_scope`. No sentence names the case today; the PR adds one to `session.hpp` and to
`session_executor.hpp`, and widens `L-007-2` (§4).

A test that drives a raw `Session` from a bare `io_context` handler, as the cell fixture does with
`ExecKind::io_context`, is in the same position if its callback runs the `io_context`: the handler that
holds the callback is not the strand's. That driver has already left the reentrancy contract's
"session-strand" clause for `on_inbound_frame()` and `send()`, so this is not a second exposed setup; it
is outside the contract before R-7. NC-16 and NC-20 are the cells whose callbacks run the `io_context`,
and both use the `direct_executor` fixture; NC-16's fourth arm, the control, runs the same callback on
`ExecKind::strand`.

- **Step 4** is the diagnostic for that setup on a build with assertions. It costs one load.
- **Step 6 skips the depth test** because the alternative is worse. On a build without assertions step 4
  compiles out. A second depth test would find the depth still non-zero and defer again, and a
  continuation that posts itself again each time it runs keeps the callback's `poll()` from ever
  returning. Without the second test the send nests there, which is today's behaviour: undefined under
  `L-007-2`, as it is before this PR. The skip is an argument and not a member, so nothing is left set
  when the send returns early.

Step 5 is there because `fsm_state_` stays Active while a graceful close emits its Logout:
`run_logout_phase1` writes `LogoutSent` after the Logout's `store_then_emit` returns. Without the test, a
send deferred from that Logout's `toAdmin`, or started in the same callback after a graceful `close()`,
would pass step 6's Active check and be transmitted after our own Logout, every time. Two sends that
overlap a graceful close have the same window today, by timing; that window is not changed (§7). The test
reads `close()`'s lifecycle state, so it covers the Logout that `close()` emits. It does not cover the
confirming Logout of a logout the peer started, which is emitted with no `close()` begun (§7).

The deferred path has no Active test of its own. A session that left Active while the send waited is
refused by the Active check in step 6, with the same error.

**Why the wait is long enough.** The posted continuation runs only after the handler that held the
callback has yielded. Where the outer emit first yields depends on the branch `store_then_emit` takes, and
on every branch the outer frame's number is assigned, and its bytes are handed on, before that yield:

| Branch of the outer emit | Its first yield | What precedes that yield |
|---|---|---|
| a shipped store (`MemoryStore`, `FileStore`) | the post that begins the store call | the assign, and the store call itself |
| a `MessageStore` whose `store` completes without yielding, or no store at all (`store_` is null when no factory is configured) | the live write: the write gate if a write is in flight, else the transport's write, which holds the gate | the assign, the completed store call if there is one, and the gate request |
| the same, with a synchronous `transport_send_` | none: the emit completes inside the handler | everything |
| the GapFill of a resend replay (no assign, no store; §1) | the live write, as in the second row, or none with a synchronous `transport_send_` | the gate request, or the whole transmit |
| a failure exit (a veto, a throwing callback, a failed assign) | wherever the exit path yields | the outer frame is abandoned; nothing of it is stored or sent |

So the deferred send peeks after the outer assign; its store call follows the outer one; and its request for
the write gate follows the outer one, because the handler that completes a store goes on to request the gate
before it yields again. A deferred send is then exactly an overlapping send. Three premises carry the rest,
each already relied on by two sends that overlap today:

- **A store completes its `store()` calls in the order they were made.** The interface did not say so
  before this PR: the doc comment of `MessageStore::store` (`include/fixpp/session/message_store.hpp`)
  states the copy and the sequence check and nothing about concurrent calls. Both shipped stores do it, by
  a leading post and then a FIFO writer mutex (`MemoryStore::store`; `FileStore::store` takes its mutex on
  the strand before it offloads). R-6 makes it a requirement on every implementation (§4). A store that
  completes two calls out of order has its two frames transmitted out of order, whether the second send
  was deferred or merely overlapping; a cell shows that (§3). B&L `L-518-1` also names a store-order
  premise, but for `reset_to` against the teardown reset. It does not cover `store()`, and v0.3 was wrong
  to cite it for this.
- The write gate grants in arrival order (`async_mutex`'s FIFO grant).
- An uncontended `assign_outbound()` does not yield (§1).

Measured: `OverlappingSends` and `OverlappingSends_FileStore`, `OverlappingSends_NoStore` and its live-write
twin, and the post controls of the nested cells on both stores and with no store.

**Cancellation at the hop (OD-1).** What asio does, read in asio 1.38.0 (the version `conanfile.py` pins;
`asio/impl/awaitable.hpp`):

- A `co_await` inside an `asio::awaitable` tests for cancellation when the await begins. Both
  `await_transform` overloads, the one for an awaitable and the one for an asynchronous operation, throw
  `operation_aborted` when `throw_if_cancelled` is set and the thread's cancellation state already records
  a cancellation. Nothing tests when the coroutine resumes.
- `asio::post` installs no cancellation handler: neither `asio/post.hpp` nor
  `asio/detail/initiate_post.hpp` uses a cancellation slot. A cancellation emitted while a coroutine waits
  on a post is recorded in the cancellation state, the post completes normally, and the coroutine resumes.
  `MemoryStore::store`'s comment on its own leading post relies on the same pair of facts.
- What is recorded depends on the state's filter. A chain started by a plain `co_spawn` records terminal
  cancellation only: `asio/impl/co_spawn.hpp` builds the chain's state with the one-argument
  `cancellation_state` constructor, whose filter is `enable_terminal_cancellation`
  (`asio/cancellation_state.hpp`). A filter also decides what a chain passes on to a coroutine it spawns
  and awaits, so a `total` emission on a chain with the default filter never reaches an inner body, whatever
  that body enabled. `Engine::send`'s session-strand body enables total, for a caller whose own chain
  passes it. A thread that has taken any `async_mutex` lock is left at terminal only (the fourth part of
  the lock sequence, `include/fixpp/core/sync/async_mutex.hpp`).
- A recorded cancellation can be erased. The state's handler assigns the filtered value of each emission
  to its record; it does not accumulate (`asio/cancellation_state.hpp`, the call operator of the state's
  `impl`). So a recorded `terminal` cancellation is erased when a later emission that the filter reduces
  to nothing, a `total` under the default filter, arrives on the same signal before the coroutine's next
  await. P-4's probe measured it: with a `terminal` and then a `total` emission during the wait, the
  record reads `none` after the post and the next await does not throw (§3).

So a deferred send that is cancelled while it waits does not throw at the post. Left alone it would resume,
and the recorded cancellation would throw at its next await. If that await is inside `Session::send`'s
`try`, the `catch` there writes `Disconnected`, a conversion meant for a send in flight. If it is outside,
the exception leaves a function whose callers expect an `expected_t`. This send has consumed nothing: no
number, no store slot, no frame. So in step 3 the deferred path applies asio's test itself
(`co_await asio::this_coro::throw_if_cancelled()` is set, and
`(co_await asio::this_coro::cancellation_state).cancelled()` is not `none`) and returns the OD-1 error
with the session's state untouched. The lock sequence of `async_mutex` applies the same pair by hand
(`FIXPP_DETAIL_CO_AWAIT_LOCK` in `include/fixpp/core/sync/async_mutex.hpp`). A chain that turned
`throw_if_cancelled` off is therefore not refused at the hop, as asio would not throw for it; NC-4's fifth
arm pins that half of the test. Step 3 reads the record that asio's own test reads, so it also behaves as
asio does where a recorded `terminal` cancellation was erased by a later emission that the filter
dropped: that send is not refused at the hop, and its next await would not have thrown either. NC-4 needs
no arm for it.

- No test is needed before the post. The caller's own await of `Session::send` began with the same test,
  and nothing suspends between that await and the post, so the state cannot have changed in between.
- Between step 3 and step 6's first await nothing suspends, so nothing can be recorded there. A
  cancellation that arrives later meets a send in flight and takes today's conversion.
- The natural implementation, a bare post followed by the send, is the mutant "the cancellation test
  removed" of §3.

This is the hazard that made B35's ruling R-10 reject a session send gate
(`.specify/544-hot-path-zero-alloc.md` §2.5): there the new await sat on every send; here it sits only on a
send started inside an outbound callback. The sequence above was read first and then measured: P-4's
probe agrees with it (§3, prediction P-4).

**The payload.** `Session::send` takes a span, and its doc comment states no lifetime rule. Today the span
is consumed before the send's first suspension: `send_impl` begins with `build_send_frame_`, which copies
it into the frame buffer. So a callback may pass a span over a buffer of its own and start the send
detached. On the deferred path the build runs after the callback has returned, when that buffer may be
gone. So step 1 copies the payload before the post and step 6 sends the copy. The copy's storage comes
from the default allocator and is owned by a local of the deferred path's coroutine: a
`std::vector<std::byte>`, the container `Engine::send` uses for its own copy (§0a has the allocation
articles' disposition). The frame that owns the copy
awaits the inner send, so it outlives it. Decided by the orchestrator at round 1 (the alternative is in
§5). The cost is one allocation for the copy, on the deferred path only; a send that does not wait copies
nothing more than today. `Engine::send` already copies the payload twice before `Session::send` runs
(§2.3), so a send that reaches the deferred path through it is copied a third time (§7).

**The frame slot.** The holder is taken in step 6, after the hop. Which buffer the deferred send gets
depends on where the outer emit is by then (the table above):

- The outer send is suspended in its store or its write: it still holds the slot, and the deferred send
  takes `send_fallback_leaf_`, whose frame owns its buffer and so allocates, as a send nested in `toApp`
  does today.
- The outer send completed inside its handler (no store, synchronous sink): its holder has released the
  slot, and the deferred send takes the session's slot.
- A send deferred from `toAdmin` finds the slot free unless an application send is suspended at that
  moment.

Nothing in the design depends on which buffer it takes.

**Where the hop lives.** In the deferred path's own coroutine, which the send's body awaits only when the
depth test sends it there. On the path that does not wait, the body runs the same awaits in the same order
as today, so the position of its first cancellation check, which B35 measured, does not move. Its frame may
grow by the argument and by the storage one more await expression needs; B35's gates are re-run for that
reason (§3).

**Cost.**

- Every application callback, in every build type: two writes to a plain integer, an increment and a
  decrement of the depth of all callbacks (§2.2). `callback_dispatch_scope` compiles to nothing under
  `NDEBUG` today, so this is new work on the inbound path, `fromApp` included. An outbound callback makes
  two more, for the outbound depth. This is the one statement of the counters' cost; §0a and §4 point here.
- A send that is not started inside an outbound callback: a test of the argument, one integer load and a
  branch.
- A send that is: the deferred path's coroutine frame, one allocation for the payload copy, one post, and
  the fallback leaf's frame where the outer send still holds the slot. Probe p1 measured a post on the
  reply path at +210 to +270 ns on the Engine's session executor type, with no call to `operator new` or
  `aligned_alloc`. p1 had no payload copy in it.

B35's zero-allocation gates must stay green (§3). The counter writes and the depth test are not assumed
free: §3 measures them with the in-tree benches that drive those paths.

**What an application observes.**

| Send started in | Before (`NDEBUG`) | After (every build type) |
|---|---|---|
| `fromApp`, `fromAdmin`, `onLogon` | nested | nested: numbered, and its `toApp` run, inside the callback |
| `toApp`, `toAdmin` | nested; one number on two frames, or frames out of order | starts after the callback returns, over a copy of the payload taken before it returned; numbered after the frame whose callback started it; transmitted after it, on a store that meets R-6's requirement |
| `onLogout`, `onCreate` | refused with `session_invalid_state_for_send`: the session is not Active. `onLogout` runs after the state it leaves Active for has been written, and `onCreate` runs at the end of `open()`, before the session can be Active | unchanged |
| outside any callback | inline | unchanged |

On a build with assertions the left column was an abort in the `fromApp` row and in the `toApp` row. The
`onLogout` row and the last row were as on an `NDEBUG` build: a send that is refused at the Active check,
and a send outside any callback, enter no callback scope. NC-2 pins the `fromAdmin` and `onLogon` cases
of the `fromApp` row. The right column is stated for
every configuration inside the contract. In the exposed setup of R-7, a send started in `toApp` or
`toAdmin` by a callback that then runs the `io_context` nests on a build without assertions, as it does
today.

Two results are new for a send started in `toApp` or `toAdmin`:

- It returns `session_invalid_state_for_send` if, by the time it starts, the session has left Active or a
  `close()` has begun.
- Cancelled before it starts, it returns the OD-1 error, `dispatch_aborted`, and the session stays as it
  was. Until now `dispatch_aborted` from `Session::send` always came with `Disconnected`. After this PR a
  caller reads `state()` to tell the two apart (§4).

### 2.1a Every entry point that can begin an emit

The condition of §1 is about emit bodies, and `Session::send` is not the only way an application starts
one. The public coroutine entry points (`grep -n "asio::awaitable<" include/fixpp/session/session.hpp
include/fixpp/session/engine.hpp`, the public ones), each with what it can begin when an application starts
it from a callback of the same session:

| Entry point | Can it begin an emit inline? | Disposition |
|---|---|---|
| `Session::send` | yes | §2.1 |
| `Engine::send` | only through `Session::send` | §2.3; guarded by §2.1 |
| `Session::close`, graceful | yes: `run_logout_phase1` peeks, calls `toAdmin`, assigns and stores, unless a store's flush hook suspends first | below (R-4) |
| `Session::close`, terminal | no: it emits nothing, but it closes the session inline | below (R-4) |
| `Engine::stop` | no emit | out of this PR (R-5); "`Engine::stop()` started from a callback", below |
| `Session::open`, `Session::drive_reconnect` | the initiator's Logon | engine-driven; not an application entry point from a callback. `open()` is refused on a session that is already open. `drive_reconnect()` has no run-time refusal: it is public only so that `engine.cpp` can reach it, and calling it from anywhere else breaks the engine-only convention its doc comment states |
| `Session::on_inbound_frame` | reply emits | fed by the read pump; an application that feeds frames from inside a callback is outside the contract (contract C-3 I-3). On a build with assertions the frame aborts at its first parse over the session's parse buffer, whichever callback fed it; §2.2's nesting table has the frames whose arm parses nothing there, and its "not detected" rows the ones that pass |

**`Session::close`, both modes (R-4).** `close()` waits by one post onto the session's executor when the
depth of all callbacks of that session is non-zero, and then proceeds as it does today. The check is in
`close()` itself, not at the application's call, so it covers every caller, including the terminal close
that `send_impl` awaits when a nested send's `toApp` throws.

- **Which callbacks count.** The ones that run under `callback_dispatch_scope`: `fromApp`, `fromAdmin`,
  `toApp`, `toAdmin`, `onLogon`, `onLogout` and `onCreate` (`grep -n "callback_dispatch_scope cs"
  src/session/session.cpp`). A function the application supplies through configuration, not through
  `Application` (a logon validator, a store, a transport sink, a log sink), is not under the scope and is
  not counted. Starting a `close()` or a send from one of those is not changed by this PR.
- **Where the wait sits.** After `close()`'s existing
  `reset_cancellation_state(asio::disable_cancellation{})` and before its three-state test of `state_`. So
  the test runs on the state the close finds when it resumes: a close that finds the session already
  closed returns `session_already_closed`, and one that finds a close in flight joins it, as a repeated
  close does today. It follows that the wait is before the `state_ = lifecycle::closing` write.
- **Its shape.** One post, then an assertion that the depth of all callbacks is zero, for the reason of
  §2.1's step 4. Not a loop: in the exposed setup of R-7 a close that waited again each time it found the
  depth non-zero would keep the callback's `poll()` from returning, for step 6's reason. On a build
  without assertions the close runs inside the callback there, which is today's behaviour.

**What one post delivers, and what it does not.** The post is queued while the callback is on the stack.
It runs at the first point where the handler that called the callback yields. That is a position in the
strand's queue. It is not the end of the arm that called the callback. So:

- **The close never runs inside the callback.** Inside the callback, after the start, the session is as it
  was: `onLogout` has not run and no Logout has been emitted.
- **The arm can be suspended when the close runs.** After `fromApp` returns, an Active arm on a persistent
  store awaits `persist_inbound_advance_`, and any arm that answers the frame awaits its reply's emit. The
  close runs during that suspension, and the arm resumes on a closing or closed session. That is the
  position of a close from another thread today, and the session's existing handling of it applies
  (`close()`'s drains of the seqnum manager and the write gate; the Logon arms' superseded check).
- **An outbound emit can be cut.** The close's post is queued inside `toApp` or `toAdmin`. On a shipped
  store the outer emit's first yield after the callback is the post that begins its store call (§2.1's
  first-yield table), and that post queues behind the close's. The outer frame's number is assigned by
  then: before the callback at an assign-first site, after it at a callback-first one. So on a shipped
  store a terminal close runs while the outer frame's store call is suspended, and before the frame is
  transmitted. Of the table's other branches, no store was measured during implementation (the close
  cells' arms with no store; the record, "Phase 4, second part"): the outer frame is transmitted, on
  an Active and open session, before the close begins, also for a terminal close and on the live
  transport, and its operation returns ok. A store that completes without yielding was not measured.
  So the cut is a cost on a session with a store. The orchestrator kept R-4 as Gate A approved it
  (2026-10-12) on this comparison: after a `disconnect` inside `toApp` or `toAdmin`, QuickFIX C++ and
  QuickFIX/J leave the message numbered, stored and not transmitted, which is the shipped-store outcome
  here apart from when `onLogout` runs, and QuickFIX has no configuration without a store (read, not
  run: `research/G19-fix-fpml-iso20022/research/probes/b41_563_564/ph5_quickfix_send_during_peer_logout.md` in the parent repo, second section; it names the commits it read, QuickFIX C++ `386ce46e`, QuickFIX/J
  `149869e8`, QuickFIX/Go `eff945dd`). Measured for a close started in `toApp`, on the shipped stores, with the start posted
  (P-1; §3 names the evidence): the outer frame's store call begins before the posted close runs and
  completes after it; on a live transport the frame is retained under its number and not transmitted,
  and its send returns `dispatch_aborted`.
  - **With `reset_on_disconnect` set** (P-1a, measured; §3 names the evidence): the close is suspended
    in its teardown reset before it has drained the write gate, so the outer frame's write passes the
    gate and reaches a transport that is already closed. On a transport that refuses a write after its
    `close()`, nothing is transmitted and the send returns `dispatch_aborted`; the store ends empty,
    with both next numbers at their initial value. This is stated from the arm whose transport double
    refuses the write. A double whose `close()` does nothing reports the frame as transmitted, which is
    an artifact of the double.
  - A graceful close started there emits the outer frame first, then the Logout (measured with the start
    posted, on the cells' synchronous sink and on the live transport; P-1b).
  - NC-6 is the cell for a terminal close started in `toAdmin`. P-1 was measured there on 2026-10-12, at
    a callback-first site and at an assign-first one, and agrees (§3's P-1 row has it).
- **The close can be late, on a volatile store.** The read pump delivers every complete frame of one read
  with no yield between them, and an Active arm on a volatile store does not suspend after `fromApp`. So
  on a volatile store a `close()` started in `fromApp` for one frame begins only after the later frames
  of the same read have been delivered: their callbacks run, their replies are sent, and a send started
  there is admitted. Today's inline close stops them. Measured on a `MemoryStore` with two frames (P-3;
  §3 names the evidence): the second `fromApp` runs before `onLogout`. On a persistent store the bullet
  does not hold. Measured on a `FileStore` (the same row): the arm suspends in its persist, the close
  runs there, the second `fromApp` does not run, and the second frame's `on_inbound_frame` returns ok on
  the closed session. Disclosed in §4.

The owner chose this reading knowing these costs (§0, R-4). §2.5 has what it means for the state v0.3 said
it removed.

**The two modes.**

- **Graceful.** Measured without the wait: started inside `toApp` or `toAdmin`, its Logout takes the outer
  frame's number at a callback-first site and goes out ahead of the outer frame at an assign-first site,
  and the outer frame is then transmitted after our own Logout. With the start posted, the outer frame goes
  first and the Logout follows, both retained. On a `MemoryStore` the Logout's `toAdmin` runs before the
  outer frame is transmitted, while the outer emit is suspended in its store call; the wire order is
  still the outer frame, then the Logout (P-1b, measured; §3 names the evidence). The wait also keeps the
  Logout's `toAdmin` off the caller's stack in `fromApp`, where order was not at risk.
- **Terminal.** It begins no emit. Without the wait it closes the session under the callback's feet: the
  session is `closed_drained`, and `onLogout` has run, before the call returns (§2.5). With the wait it
  runs after the callback, at the positions above.

**The lifecycle callbacks.** R-4 says "any callback", and these three sit in different places. Each
disposition was read first and then measured with the start posted (P-5; §3 names the evidence and the
routes that were run). The measurement agrees in every arm, and every arm ends closed:

- **`onLogon`** runs inside the Logon arm, at the write of Active and before the arm's persist. A close
  started there begins after `onLogon` returns. Where the arm persists the inbound advance on a persistent
  store, the close runs while that persist is suspended; the arm's next statement is its superseded check
  (`logon_arm_superseded_`), which is already there for a close that ran during the persist, and the arm
  stops without starting the liveness loop. Where the arm does not suspend there (a volatile store, or a
  Logon that skips the persist), it finishes first, liveness loop included. A graceful close then sends its
  Logout after the Logon exchange.
- **`onLogout`** runs inside `record_state_transition_`, on the first transition out of Active. Where a
  `close()` made that transition, a close started in `onLogout` waits and then finds the first one in
  flight or finished: it joins it, or returns `session_already_closed`. A terminal close that joins a
  graceful one escalates it (093 plan OD-29). The wait is what lets that escalation take effect from
  inside the graceful close's own callbacks. Before this PR a terminal close started in the `toAdmin`
  of the graceful close's Logout did not escalate it on any store kind that was run, and one started
  in its `onLogout` did not with no store on a synchronous sink: the nested call arrived before the
  grace wait had begun, and both closes ended when `logout_disconnect_timeout_ms` elapsed. With the
  wait both escalated at once (measured; the record, "Phase 4, fourth part" and "Phase 4,
  addendum"). From `toAdmin` that was this branch's state until 2026-10-14: on a `FileStore` with a
  live transport, the end of the Logout step that the escalation brought about raced the completion
  of the Logout's store call on the file executor. The Logout was retained, and it was transmitted
  only where that completion came first (not seen without a stall; seen in every run with the
  session's thread stalled after the hand-off). In the other arms it was transmitted once (B&L
  `L-563-6`, a row since deleted). Since §2.6's production commit (library `9a77b3e3`) a terminal
  close started in that `toAdmin` cuts the Logout on every arm: it is numbered, stored where there
  is a store, and not transmitted, and the terminal close joins without ending the grace wait
  (§2.6; B&L `B-563-5`). From `onLogout` nothing changed: the terminal close escalates the
  graceful one.
  It is a change of behaviour (§4, "Compatibility"). Where a `Disconnected` write outside `close()`
  made the transition (`Session::send`'s, for one), no close is in flight, and the close started in
  `onLogout` is the session's first.
- **`onCreate`** runs at the end of `open()`, before the role loop connects or publishes. A close started
  there begins after `onCreate` returns. `open()` has no suspension after the callback, so it returns
  success, and the close runs at its caller's first suspension. The session never reached Active, so no
  Logout is sent and `onLogout` does not run. On a raw initiator `open()` transmits the Logon before it
  calls `onCreate` (measured, P-5), so a close started in `onCreate`, in either mode, ends the session
  after a Logon and with no Logout. This disposition is a raw-`Session` one, and NC-11 is its
  cell. Under the Engine an application has nothing to close with in `onCreate`: the callback receives a
  `SessionId`, and `Engine::lookup` finds no session until the role loop has published it, which is after
  `open()` returns.

What the three QuickFIX engines do, read: an application's `logout()` sets a flag (C++, J) or queues a
request (Go), and the engine's own loop generates the Logout later, so it is never emitted inside the
caller. `disconnect()` in C++ and J runs inline, `onLogout` included, and the inbound handler then carries
on with the frame. Go has no inline close: a stop is queued to the session's loop. R-4 follows all three
for a graceful close and follows Go for a terminal one; the departure from C++ and J is that `onLogout`
runs just after the callback, not inside it.

**Cancellation and the wait.** The wait must not be a way to skip the close, and by asio's sequence
(§2.1, "Cancellation at the hop") it cannot be one where it sits:

- `disable_cancellation` is the state's filter from `close()`'s reset on, so a cancellation emitted during
  the wait is not recorded, and the reset built a state that records none. The post has no cancellation
  handler. No lock precedes the wait, so nothing has replaced that filter. The close resumes and runs.
  P-4's probe measured this order on asio alone, in its arm that resets to `disable_cancellation` before
  the post (§3).
- This is the in-place disable, and it is enough here. 093's reset unit could not use it: its store
  operation takes a lock, and the lock sequence replaces the filter, so 093 plan OD-25 moved that operation
  onto its own awaitable thread bound to an empty slot. v0.3 cited OD-25 as the model for this wait; that
  cite pointed at the wrong mechanism and is withdrawn.
- A caller whose chain is already cancelled when it evaluates `co_await close()` throws before `close()`
  is entered. That is so today, with or without the wait, and is not changed. One existing path has it:
  `send_impl`'s `co_await close(terminal)` after a throwing `toApp`, on a chain already cancelled, leaves
  the session `Disconnected` and not closed. Noted in §7.
- **The `io_context` destroyed during the wait.** The frame is destroyed at the post, before the `closing`
  write, so the lifecycle is still open and a later `close()` runs normally. Had the wait been placed after
  that write, the lifecycle would stay `closing` and every later `close()` would join a close that never
  completes. One route differs: where `send_impl` awaits `close(terminal)`, the destroyed chain includes
  `Session::send`'s slot holder, which writes `Session` memory when it is destroyed. That is `L-544-2`'s
  rule (keep the `Session` alive until the send's frame has completed or been destroyed), now reachable
  from a wait that did not exist before (§4).
- **At the send's hop**, for comparison: the slot holder is taken after the hop (§2.1), so a deferred send
  destroyed at its post has written no `Session` memory. Through `Engine::send` the only destructor that
  runs is the send counter's guard, on a counter the frame keeps alive.

**`Engine::stop()` started from a callback (R-5: out of this PR).** v0.3 said this route was covered at
`Session::close`. It is not, and the claim and its cell are removed. What was read (Gate A round 1, finding
F-7; not run):

- `stop()` reaches `Session::close` in its step 4, after it has joined the role loops in step 3. The
  callback that started the stop runs inside a role loop, so it returned long before. R-4's wait never
  fires on this route.
- What reaches the session while the callback is on the stack is steps 1 and 2. `stop()`'s body begins
  with a `co_spawn` onto `control_strand_`, which runs inline when that strand is idle. Step 1 then
  `co_spawn`s onto the session strand, which is running in this thread, so the engine-stop flag is set and
  cancellation is emitted inside the callback. Step 2 `co_spawn`s the transport's close onto the same
  strand. On one io thread, then, the callback's `co_spawn` of `stop()` can return with the session's
  transport already closed.
- On several io threads it depends on whether `control_strand_` is idle at that instant, which is the
  timing dependence R-2 removes for `send` and leaves in place for `stop`.
- **This route is the one on which OD-3 refuses sends that are admitted today.** Where step 1 runs
  inside the callback, as above, the engine-stop flag is set ahead of send bodies that are already
  queued on the session's strand. Today those bodies passed their control-strand step before `stopped_`
  was written, and they run; after this PR the stop check of §2.3 refuses them with
  `session_invalid_state_for_send`. That holds for the body of a send that found its session in the
  snapshot; a body of the miss path that is already queued there is tested only for Active, as today.
  And on several io threads a send that the callback starts after it started `stop()` is refused
  where `control_strand_` is idle and admitted where it is busy; today it is refused in both cases
  (fixpp#579). It is a C++ route only. The C ABI reaches `Engine::stop` in
  `fixpp_engine_destroy` alone (`grep -n "stop()" src/capi/*.cpp`), a blocking call that deadlocks inside
  a callback (`L-054-1`).

Re-derive by reading `Engine::stop` in `src/session/engine.cpp` from its first `co_spawn` to its step 4.
The limitation row is in §4; the follow-up issue is fixpp#579. A shipped cell is on this route, found by
reading at round 2: `ApplicationEngineSend.SendDrainRaceNoUAF` (§3).

### 2.2 The debug guard keeps its meaning (fixpp#564)

`callback_dispatch_scope` asserts today that no callback of the session is on the stack when one is
entered, whatever the pair. That one assertion carries four meanings. v0.3 kept the first. v0.4 restored
the second and the third and missed the fourth.

| What the assertion does today | Why it matters | What carries it after this PR |
|---|---|---|
| It detects a second thread entering a session's callbacks: a `direct_executor` attested over an executor that does not serialise. This is the misuse its message names. | It is the debug diagnostic for a broken threading contract. | An **atomic owner token**. Entering a scope compares and swaps the token from "no owner" to the current thread. If the swap fails and the owner is another thread, the assertion fires. If the owner is the current thread, the entry is nested and passes. The scope that installed the token clears it on exit; a nested scope leaves it alone. The swap is the scope's first action and the clear is its last, so the depth is incremented after the swap and decremented before the clear. |
| It does that through an atomic exchange, so the detection is not itself a data race. | In the misuse, two threads read and write the guard's state at once. With a plain depth and a plain owner the second thread can read both as free and pass, and the death cell would be a TSan report on the guard. | The same token. A foreign thread that enters a scope reads and writes only the atomic there, and asserts before the scope touches a plain member. The assertions at the parse sites, which read a depth ahead of a scope, are the stated exception ("The members, and who reads which", below). |
| It makes a nested inbound parse abort. 093 contract C-3 I-3 depends on that ("No inbound parse nests inside another. Callbacks are synchronous, and the callback scope asserts it"), and so does the comment on the inbound overload of `parse_and_dispatch_`: one parse buffer per session is safe because no inbound parse runs inside another. | Without it, a `fromApp` that starts `on_inbound_frame` again rebuilds the parse resource over `inbound_parse_buf_` while the outer callback's `MessageView` still points into it: silent corruption where there was an abort. | **An assertion on the depth of all callbacks at each parse site over the session's buffer.** A parse site is a function that builds a memory resource over `inbound_parse_buf_` (`grep -n "inbound_parse_buf_" src/session/session.cpp`): the inbound overload of `parse_and_dispatch_`, and `validate_inbound_`, which the arms call before dispatch when the validate gate is on. Each asserts, before it builds the resource, that the depth of all callbacks is zero. |
| It makes every other same-thread entry abort: an inbound frame fed from `toApp`, `toAdmin`, `onLogon`, `onLogout` or `onCreate`, an outbound callback entered inside an outbound callback, and `onLogon` or `onLogout` entered inside another callback. | The first is a frame the application fed from inside a callback, which the contract does not allow from any callback (§2.1a); from `toApp` or `toAdmin` the nested arm's replies would begin emits inside the outer emit's stretch. The second is R-1's rule being broken: by the run of the `io_context` that R-7 excludes, or by a future emit that does not go through `Session::send`. The third is the session entering or leaving Active under a callback. The design relies on one case of it, leaving Active inside an inbound callback or `onLogon` (a nested send that disconnects; the table below). Inside `toApp` or `toAdmin` it happens inside an emit's stretch, and the outer emit then carries on; it is reached there only from outside the contract. Those are the routes on which a diagnostic is wanted. | The parse-site assertion of the row above, which reads the depth of all callbacks and so fires whichever callback is on the stack. And **an assertion in the stack-arena overload that the outbound depth is zero.** And **an assertion at the `onLogon` and `onLogout` sites that the outbound depth is zero**, which carries the third inside `toApp` and `toAdmin` only. Each of these two is evaluated after its site's `callback_dispatch_scope` has been constructed, and before the outbound depth is raised or the callback is called ("The members, and who reads which", below). |

**The inbound-parse mark of v0.4 is removed.** It was a member of its own, set by the inbound overload,
whose assertion fired on an inbound parse inside an inbound parse. Two reasons:

- The parse-site assertion implies it. `validate_inbound_` calls no application code, and the inbound
  overload calls application code only through its callback, under `callback_dispatch_scope`. So a parse
  over the
  buffer that begins while another is in progress begins with a callback of the session on the stack,
  and the depth of all callbacks is non-zero there. A mark kept beside the depth assertion would carry
  an assertion that no cell could show to be missing.
- The mark did not cover `validate_inbound_`. A frame fed from `fromApp` that the validate gate rejects
  is parsed over the buffer and never reaches the inbound overload.

**Every nesting on one thread, against the checks.** The "Depths" column gives the depth of all callbacks
and then the outbound depth, as the inner entry finds them. "Lifecycle site" means the scopes at
`onLogon`, `onLogout` and `onCreate`, which carry the token and the depth of all callbacks. The `onLogon`
and `onLogout` sites also assert that the outbound depth is zero; the `onCreate` site has no assertion of
its own. The stack-arena overload's check and the lifecycle sites' check are each evaluated after the
site's scope has been constructed, so on one thread the token has already passed when the depth is
read.

| Inner entry, inside what | Depths | Check at the inner entry | Result |
|---|---|---|---|
| **Legal: the design relies on it** | | | |
| `toApp`, inside `fromApp`, `fromAdmin` or `onLogon`: a send started there nests (R-1) | non-zero, zero | stack-arena overload: the outbound depth is zero | passes |
| `onLogout`, inside `fromApp`, `fromAdmin` or `onLogon`: a nested send wrote `Disconnected` inline (§2.5 lists the routes) | non-zero, zero | lifecycle site: the outbound depth is zero | passes |
| any callback of session B, inside a callback of session A, with no callback of B on the stack | zero, zero: B's own | B's checks read B's members | passes |
| **Outside the contract: must abort** | | | |
| a parse over the buffer, inside `fromApp` or `fromAdmin` (C-3 I-3) | non-zero, zero | parse site: the depth of all callbacks is zero | aborts |
| a parse over the buffer, inside `toApp` or `toAdmin` | non-zero, non-zero | the same | aborts |
| a parse over the buffer, inside `onLogon`, `onLogout` or `onCreate` | non-zero, zero | the same | aborts |
| `toApp` or `toAdmin`, inside `toApp` or `toAdmin` | non-zero, non-zero | stack-arena overload: the outbound depth is zero | aborts |
| `onLogon` or `onLogout`, inside `toApp` or `toAdmin` | non-zero, non-zero | lifecycle site: the outbound depth is zero | aborts |
| any callback, entered from a second thread while a first is inside | any | the token; for an inbound frame the parse-site assertion can fire first | aborts |
| **Outside the contract: not detected** | | | |
| `toApp` or `toAdmin` of an emit that is not a send, inside `fromApp`, `fromAdmin` or a lifecycle callback | non-zero, zero | stack-arena overload: the outbound depth is zero | passes |
| `onLogon` or `onLogout`, inside `fromApp`, `fromAdmin` or a lifecycle callback, on a route other than the legal row's | non-zero, zero | lifecycle site: the outbound depth is zero | passes |

The first "not detected" row has the depths of the first legal row, so no check on these counters can tell
them apart. It is
reached two ways, both outside the contract. One is the engine's own emit under the run of the
`io_context` that R-7 excludes. The other is the reply to a frame fed from inside the callback whose arm
parses nothing over the buffer before it emits: with the validate gate off, a frame above the expected
MsgSeqNum is answered by a ResendRequest and calls no receive callback. Today's assertion aborts both.
Inside `fromApp` or `fromAdmin` neither is inside an emit's stretch, because an inbound callback never is
(§1), so there neither can put one number on two frames. Where a deferred send or a waiting close is also
involved, step 4 or the close's assertion reports the run of the `io_context`.

The second "not detected" row has the depths of the second legal row, and the same holds for it. It too
is reached only from outside the contract. One route is a frame fed from inside the callback whose arm
writes `Disconnected` without a parse over the buffer and without an emit: with the validate gate off, a
frame whose BeginString is not the session's is disposed of that way, and so is one whose CompIDs are
not where the CompID check is on; `onLogout` runs inside the feeding callback. The other is a `Disconnected`
write the engine makes itself under the run of the `io_context` that R-7 excludes: the liveness loop
writes that state directly, without `close()`, so the close's assertion does not see it. Today's
assertion aborts both. Started in `toApp` or `toAdmin`, the
same routes disconnect the session inside an emit's stretch, and the outer emit then carries on. That
half is detected: it is the "must abort" row for `onLogon` and `onLogout`, carried by the lifecycle sites'
assertion. What stays undetected is the half inside an inbound or lifecycle callback; an inbound callback
is never inside an emit's stretch (§1).

The losses in the "not detected" rows are disclosed in the guard's B&L row (§4). A frame
fed from inside a callback whose arm neither parses over the buffer nor calls a callback is not detected
either; it is not detected today.

One nesting is absent from the legal rows on purpose. A nested send whose `toApp` throws does not run
`onLogout` inside the inbound callback after R-4: `send_impl` awaits a terminal close, which waits.

**The assertions' messages.** The token's assertion keeps today's text, which names the cross-thread
misuse. The parse-site assertion, the stack-arena overload's, the lifecycle sites', step 4's and the close's each get a text of
their own, and every death arm in §3 matches the text of the assertion it is written for, so that an arm
cannot pass on another assertion's abort.
`InboundFrameDispositionsI3.AnInboundParseReenteredFromACallbackDies` keeps its meaning. Its nested frame
is in sequence, because the Active arm advances the inbound counter before it calls `fromApp`, and its
fixture leaves the validate gate off, so the nested arm reaches the inbound overload and dies at the
parse-site assertion. The test's expected text, which is today's scope message, is edited to the
parse-site text, and the comment above it with it (§4).

Today's scope also clears the flag unconditionally in its destructor, so a nested scope's exit would clear
the outer scope's entry. The token's rule (only the scope that installed it clears it) replaces that.

**The members, and who reads which.** `Session` gains the owner token in place of `in_dispatch_`, and
two depths. Nothing else: v0.4's mark and its skip flag are both gone. The depths are plain integers,
written and read only on the session's serialisation domain, and maintained in every build type:

- **The depth of all callbacks.** `callback_dispatch_scope` increments it on entry and decrements it on
  exit. `close()` reads it (§2.1a). On a build with assertions the parse sites and the close's wait read
  it too.
- **The depth of outbound callbacks.** Written in the stack-arena overload (§2.1). The send's body reads
  it. On a build with assertions the stack-arena overload's assertion, the assertion at the `onLogon` and
  `onLogout` sites and the deferred path's step 4 read it too.

Neither depth is atomic, and the cross-thread check reads neither. Under the misuse the token detects, two
threads would race on them, as they already race on every strand-confined member of `Session`; a build
with assertions aborts at the token before it reaches them. An atomic depth for `close()` to read was
considered and not taken (§5).

The parse sites remain the stated exception to that last statement: their assertion reads a depth
ahead of a scope. `validate_inbound_` has no scope, and the inbound overload builds its resource before it enters
one, so a foreign thread that delivers a frame reads the depth of all callbacks before it reaches a
token. If it reads non-zero it dies at the parse-site assertion; if it reads zero it dies at the token.

**The stack-arena overload and the `onLogon` and `onLogout` sites are not exceptions.** At each of
them, the depth assertion is evaluated after that site's `callback_dispatch_scope` has been constructed,
and before the outbound depth is raised or the callback is called. So a foreign thread that enters one
of them while a first thread is inside dies at the token before it reads a plain depth. Decided by the
orchestrator on 2026-10-11. The text v0.7 first carried put the stack-arena overload's assertion ahead
of its scope and did not say on which side of the scope the lifecycle sites' assertion sits (the last
section, F1). The decision has had no review.

- **What the source shows.** Each of these sites already constructs its scope immediately before it
  invokes the callback. The stack-arena overload does so after it has framed and parsed the frame over
  its stack arena. `record_state_transition_` does so in its `onLogon` branch and in its `onLogout`
  branch, after the state has been written. Re-derive: `grep -n "callback_dispatch_scope cs"
  src/session/session.cpp`, and read from each hit to the callback it guards.
- **No assertion moves relative to the callback.** At each site the assertion still precedes the
  callback, and in the stack-arena overload it still precedes the raise of the outbound depth. What
  changes is that it follows the scope, and so, in the stack-arena overload, the overload's own parse.
  A frame that fails to parse there returns before the scope and calls no callback, so no nesting the
  assertion exists for is passed over.
- **The order in the stack-arena overload:** the scope, the assertion, the raise of the outbound depth,
  the callback. At the `onLogon` and `onLogout` sites: the scope, the assertion, the callback.

**What compiles out under `NDEBUG`:** the token's operations and every assertion this section, §2.1's
step 4 and §2.1a name. The members exist in every build type, as `in_dispatch_` does today, so the class
has one definition.

**`dispatch_app_callback`.** The guard it carries gets the token's shape. No code in `include/`, `src/`
or `bindings/` calls that function (`grep -rn "dispatch_app_callback" include src bindings` finds its
definition and a comment), so no application callback runs under it, and the depth `close()` reads does
not have to count it. If a caller is added, it must go through `callback_dispatch_scope`.

### 2.3 `Engine::send` through the snapshot (R-2)

Today `Engine::send` enrols in `send_counter_`, re-checks `stopped_`, hops onto `control_strand_` to read
`stopped_` and `registry_`, then hops onto the session strand. Each hop is a `co_spawn`, which starts with
a dispatch: the first runs inline only when the control strand is idle, and that is the timing dependence.

`reader_snapshot_` already publishes `SessionId → shared_ptr<Session>` for every published session, rebuilt
on the control strand after each control-plane mutation and read by `Engine::lookup` without the strand.

**The new path.**

1. Enrol in `send_counter_` and construct the guard, in the outer frame, before any await. Unchanged.
2. Re-check `stopped_` with `seq_cst`; if set, return `session_invalid_state_for_send`. Unchanged.
3. Load `reader_snapshot_` and look the id up in it. The hit path reads the snapshot itself and does not
   call `Engine::lookup`. On a build with assertions `lookup` returns a handle backed by a lease that
   `~Engine` asserts is released, and that lease is not something `stop()` drains. A send frame destroyed
   after the Engine (an `io_context` destroyed later, with sends suspended) would trip that assertion,
   where today's copy from `registry_` does not. The rule has no witness, and the scenario is one
   `~Engine` already forbids (§3).
4. **Hit:** copy the snapshot's plain `shared_ptr<Session>` as the keepalive and `co_spawn` onto that
   session's executor. There: enable total cancellation as today, apply the stop check of OD-3, check
   `state() == Active`, then `co_await session->send(payload)`. The stop check reads the session's engine-stop flag through
   `session_engine_access`, which has the flag's setter today and needs a reader added.
5. **Miss:** take today's control-strand path unchanged. It returns `session_invalid_argument` for an
   unregistered id and `session_invalid_state_for_send` for a registered id with no published session, and
   it finds a session published after the snapshot this call loaded.

**Why step 4 is nested by construction.** A callback of session S runs on S's executor. `co_spawn` onto an
executor that is already running in the calling thread starts its body inline, on one io thread or on many.
From any other thread the same `co_spawn` queues behind S's current handler, which is the serialisation
`Engine::send` promises. Nothing in step 4 consults a second strand.

The claim has a condition on the caller, and the B&L row states it: `Engine::send` is a coroutine, and it
nests when the application starts it from the callback in a way that runs its body at once (a `co_spawn`
onto an executor already running in that thread, the session's own or the `io_context`'s). An application
that posts first, or starts it on a strand of its own, gets a send that begins after the callback. That
send is still ordered.

**Why a miss cannot happen where nesting matters.** A send is admitted only in Active. A session reaches
Active on a frame its read pump delivers, and both role loops publish the session, and republish the
snapshot, before the pump or the first-frame delivery starts. A callback that runs before publication
(`onCreate`, the initiator's Logon `toAdmin`) is before Active, so a send there is refused on either path.
This is OD-4 (§6). §3 pins it with a cell per role (NC-1), because it is a property of two role loops, not
of this function.

**Why the miss path stays.** The snapshot holds only published sessions and `register_session` does not
republish. Telling "registered, not yet published" from "unknown" out of the snapshot would need every
registered id in it and a republish per registration, which is a rebuild of the map per call. The miss path
is cold: a send to a session that is not established.

**The payload.** `Engine::send` copies the caller's span into its frame, so a caller's stack buffer may die
before the send completes, and the session-strand body captures its own copy by value. Both stay as they
are. The body must not hold a view into the outer frame: frames can be destroyed out of order when an
`io_context` is destroyed with sends suspended, which is the class PR #572's Gate B fixed at its miss-path
drains.

**Cost.** One snapshot load and one keepalive copy replace a hop through the engine-wide strand. On the
fallback `atomic_shared_ptr` the load takes a shard lock (`include/fixpp/core/sync/detail/atomic_shared_ptr.hpp`).
This note claims no speed-up. No bench covers `Engine::send` today (B&L `L-023-2`); the PR adds one, and
§3 ("Cost") has what it times and how the difference is measured and reported.

### 2.4 What `stop()` relied on, and what holds it after R-2

023's comments say `Engine::send`'s registry read and `stopped_` check are "serialized with stop()'s
mutations on the control strand". Each guarantee, and its carrier on the hit path:

| Guarantee | Carried today by | Carried on the hit path by |
|---|---|---|
| A send never reads `registry_` while `stop()` clears it | the control-strand hop | the hit path does not read `registry_`; the snapshot is replaced atomically, never mutated |
| A send posted but not yet run is visible to `stop()`'s drain (the Engine outlives it) | the enrolment in the outer frame, before any hop | the same enrolment; unchanged |
| A send that starts after the drain saw zero queues no work | the `seq_cst` re-check after enrolment | the same re-check; unchanged |
| A send suspended on the session strand never outlives the Engine (the Session holds a reference to `EngineConfig`) | the drain before `stop()`'s terminal closes and its clear | the same drain; the guard spans the whole send |
| The Session outlives the send | a `shared_ptr` copied from `registry_` on the control strand | a `shared_ptr` copied from the snapshot, which keeps every published session until `stop()` republishes it empty, after the drain |
| `fsm_state_` is read only on the session strand | the Active check in the session-strand body | the same check; unchanged |
| A send whose session-strand body would start after `stop()`'s step 1 ran on that strand fails | the second `stopped_` read, on the control strand, which refuses every send whose control-strand step follows `stop()`'s write of `stopped_` | OD-3: the engine-stop flag, which step 1 sets on the session's strand and the body reads there |

The first six do not depend on the hop. The seventh did, and v0.4 misdescribed it. Traced through
today's `Engine::send` and `Engine::stop` (read first; P-11 then measured the refusal on the base tree
and agrees, §3):

- `stop()`'s control-strand body writes `stopped_` first, after a cancellation reset that does not
  suspend, and only then runs step 1, which sets the engine-stop flag on each session's strand.
- Today's `Engine::send` reads `stopped_` on the control strand and, in the same handler, spawns its body
  onto the session's strand. Nothing lets `stop()`'s body onto the control strand between the two: the
  one `co_await` between them is `reset_cancellation_state`, which does not suspend, and a `co_spawn`
  begins by dispatching onto its target, so the body is running or queued on the session's strand before
  the control strand is released.
- So a send's control-strand step either precedes `stop()`'s body, and then its session-strand body is
  ahead of step 1 on that strand unless step 1 runs inline there (the end of this section); or it
  follows the write of `stopped_` and is refused, with `session_invalid_state_for_send`.

Today, then, with `stop()` started outside the session's callbacks, no send's session-strand body starts
after step 1 ran on that strand: the control-strand read has refused it first. OD-3 refuses exactly the
sends whose body starts after step 1, with the same error. So on those routes OD-3 refuses nothing that
today's code admits, and the hit path keeps the seventh guarantee by a different carrier.

What R-2 changes is the other direction. A send that passed the admission re-check before `stop()` wrote
`stopped_`, and whose session-strand body is queued ahead of step 1's handler, is admitted on the hit
path. Today that send is refused whenever its control-strand step runs after `stop()`'s body has begun.
So a narrow class of sends that race `stop()` is refused today and can be admitted afterwards. Such a
send runs during teardown as a send does today whose control-strand step preceded `stop()`: it is
enrolled, so `stop()`'s drain waits for it. The Active check does not refuse it. P-11's runs read the
session's FSM state as Active at every reading, from before `stop()` to the seam after `stop()`'s send
drain, which is past steps 1 to 3 (§3). So for a send that has passed its admission re-check, what
refuses it after step 1 on today's code is the control-strand read of `stopped_`, where P-11 observed
the refusal, and not the Active check; on the hit path OD-3's check has that place (the table's last
row). Which side of `stop()` a racing send falls on depends on timing today and still does; the window in
which it is admitted is wider. A caller can observe the widening: a send that races `stop()` may succeed where it
failed before. Under `[const §X.7]` a change is breaking when a call a consumer could already make fails,
or returns a result other than the one documented. Neither holds: the outcome of a send that races
`stop()` depended on timing, and no documentation promises that it fails (re-derive: read the comments on
`fixpp_session_send` in `include/fix/c_api/session.h` and on `fixpp_engine_destroy` in
`include/fix/c_api/engine.h`).

One route is an exception: `stop()` started inside a callback of the session, where step 1 runs ahead of
bodies already queued (§2.1a, R-5). It is the only route on which OD-3 refuses a send that is admitted
today, and the C ABI cannot take it.

`stop()`'s step order, the publish and unpublish sites, the snapshot's publisher and `Engine::lookup` are
not changed.

### 2.5 A session closed from inside one of its own callbacks

Measured on an `NDEBUG` build before this change, with no nested send involved: a terminal
`Session::close` started in `fromApp` or `fromAdmin` runs inline to `closed_drained`, and `onLogout` fires,
before the call returns. The inbound arm then resumes on the closed session: it reports success, or
`session_already_closed` where it goes on to emit. With a FileStore the close reports completion before the
inbound arm has finished. A nested send whose `toApp` throws reaches the same state, because that branch
awaits a terminal close.

**What R-4 changes about that state, and what it leaves.** v0.3 said that R-4 "removes that state at its
source". It does not, and the sentence is withdrawn together with R-4's consequence (§0). Under R-4:

- **Removed:** the close running inside the callback. The callback returns to a session that is as it was
  when the callback started the close, and `onLogout` no longer runs with the callback on the stack.
- **Left:** the arm resumed on a session that closed meanwhile, wherever the arm suspends after its
  callback (§2.1a). On a FileStore the close's completion still precedes `on_inbound_frame`'s, with
  the start posted (P-2, measured; §3 names the evidence), as it did for today's inline close.

What is left is the position of a close that arrives from another thread while an arm is in flight. The
session already has handling for that position: `close()` drains the seqnum manager and the write gate, a
live write that finds the gate drained returns `dispatch_aborted`, and the Logon arms test
`logon_arm_superseded_` after each suspension. R-4 adds no handling for it and claims none. The close
cells of §3 assert what the arm does there, per store kind and per transport, so that the position is
witnessed and not assumed.

**Not a memory-safety defect, as far as reading goes.** Nothing that owns a Session can free it while one
of its inbound arms is on the stack:
- under the Engine, each role loop holds its own `shared_ptr` to the Session (`local_session` in
  `run_accept_loop` and `run_connect_loop`) across `run_read_pump`, and the arm runs inside the pump;
  `stop()` joins the role loops before its terminal closes and before it clears the registry;
- the C ABI's engine shell keeps its sessions for its own lifetime;
- a caller that drives a raw `Session` owns that obligation already (B&L `L-015-4`, `L-544-2`).

So a sanitizer cell would come back clean by construction and decides nothing about lifetime. The
sanitizer runs of §3 stay (R-3), as a net under paths that assertion builds could not reach before §2.2.

**What R-4 does not cover.** A nested send can leave the session `Disconnected` without calling `close()`,
because `Session::send` writes that state inline on some results of its body. Started in an inbound
callback or in `onLogon`, each of the following happens inside the callback: `onLogout` runs there, and
the arm resumes on a disconnected session.

- `assign_outbound()` reports the sequence number overflow. It needs a MsgSeqNum at its maximum.
- On a session with no store, a config-time `transport_send` throws. `store_then_emit` returns
  `dispatch_aborted` without having suspended.
- On a persistent store whose `store` completes without yielding, the store call fails. `send_impl` sets
  `disconnect_required` without having suspended. The shipped stores yield first, so with them the write
  comes after the callback has returned.
- The nested send's own `toApp` returns `dispatch_aborted`. `Session::send` treats that value as a
  converted cancellation and writes `Disconnected`; a comment at that test records it as a known
  residual.
- A cancellation is recorded on the nested send's chain before one of its awaits begins, for instance
  emitted from its own `toApp` on a slot the application bound to the send. That await throws, and
  `Session::send`'s `catch` writes `Disconnected`.

Each is read, not run. They are disclosed (§4), not changed. Re-derive the list by reading
`Session::send` from its `try` to its end: every `record_state_transition_(fsm_state::Disconnected)`
there is one of these routes when the code before it has not suspended.

### 2.6 One ending for a terminal close started in the `toAdmin` of a graceful close's Logout (R-8)

Added in v0.8, rewritten in v0.9 after Gate A round 4 and in v0.10 after round 5, and edited in
v0.11 after round 6 converged on v0.10 (last section). This section was written as a design, with
nothing of it in the pull request's tree. It is implemented since 2026-10-14: library `9a77b3e3` is
the production commit, byte-equal to `ph8f_mechanism.patch` (the record, "Phase 8, third part").
Where this section says "the head", it means this branch before that commit. Gate A reviewed
v0.10's form of it; v0.11's edits and v0.12's were not reviewed. The owner signed it off at v0.11
(§0, R-10's bullet). Its measurements were made on
2026-10-13 on prototypes, in a scratch copy of the tree at `f5e47486`, on a Debug build with
AddressSanitizer. The evidence is in the parent repository's
`research/G19-fix-fpml-iso20022/research/probes/b41_563_564/`. Open `ph8c_predictions.md` first for
the mechanism specified here; `ph8d_predictions.md` for what an application starts inside the moved
`onLogout`; `ph8e_predictions.md` for the Logout's own write job at the grace timer's expiry, which
corrects a part of `ph8c_predictions.md` and of `ph8c_readings.md` (both say so at their top);
`ph8b_predictions.md` for the prototype before; and `ph8a_four_way.md` for the three earliest. The
evidence names five prototypes. This section uses the names only where it cites them:

- **S** skips the Logout's transmit and changes nothing else (`ph8a_mechanism_S.patch`).
- **S2** is S, and the Logout step then leaves by itself after one post (`ph8a_mechanism_S2.patch`).
- **S3** is S, and the Logout step then records neither that a Logout was sent nor `LogoutSent`, and
  stays in its wait until `close()`'s join cancels it (`ph8a_mechanism_S3.patch`). v0.8 specified it.
- **S5** is S3 with its fourth statement replaced: the step leaves at once, having written
  `Disconnected` after the Logout's store call, and `close()` does not cancel the grace timer for a
  Logout that was cut (`ph8b_mechanism_S5.patch`). v0.9 specified it.
- **S6** is S5 with `Disconnected` written when the cut is decided, before the Logout is numbered and
  stored (`ph8c_mechanism_S6.patch`). S6 is the mechanism this section specifies. S, S3 and S5 are
  rejected (§5). The patches' comments say "PH8A prototype", "PH8B prototype" and "PH8C prototype";
  the implementation is written from this section and is not a copy of a patch.

**The condition.** The application starts a graceful `close()`. `Session::close` runs
`run_logout_phase1`, which builds the Logout and calls the application's `toAdmin` on it. Inside that
`toAdmin` the application starts a terminal `close()`, and that close's body begins while the callback
is on the stack. It finds the depth of all callbacks non-zero and takes `close()`'s wait (§2.1a). When
it resumes it finds the graceful close in flight and joins it (093 plan OD-29).

A terminal close that begins after that `toAdmin` has returned is not this condition, whatever started
it (§7).

**What the head does, per arm.** Measured (`ph8a_S_arms_by_stall.md`, the tables headed "unpatched
head"; B&L `L-563-6`, a row since deleted, stated the same). The stall settings are the Phase 4
probe's (`ph8a_stall_probe.patch`): none; the session's thread stalled right after it hands a job
to the file executor; every job of the file executor delayed.

- **In every arm** the escalation holds: both closes return success, `onLogout` runs once, the session
  ends `Disconnected` and `closed_drained`, and the logout timer is not what ends the wait.
- **Five arms transmit the Logout once,** under all three settings: no store and `MemoryStore`, each
  on the synchronous sink and on the live transport, and `FileStore` on the synchronous sink. There
  the Logout step records `LogoutSent`, and `onLogout` runs in that state. With `reset_on_logout` set
  the teardown reset fires on these arms.
- **`FileStore` on the live transport** retains the Logout under its number in every run. It does not
  transmit it with no stall or with the file executor delayed. It transmits it with the session's
  thread stalled. On this arm timing also decides whether `LogoutSent` is recorded and whether a
  configured `reset_on_logout` fires.
- **With the store's lock held by another call of the same session** when the Logout's store call
  reaches it: read (round 4's triage, O-1, "Inherited or created"), and since v0.13 rehearsed at the
  head in the scratch copy with NC-26's and NC-28's cells (§3): there the Logout is on the wire and
  the store does not hold it, `LogoutSent` is recorded, and the step is in a sleep on the session's
  clock. The cause that follows is the reading; the rehearsal did not show where the cancellation
  landed. The join's cancellation meets the store call at the lock, the lock's sequence clears the
  recorded cancellation, the Logout is transmitted ahead of the held frame, `LogoutSent` is
  recorded, and the close waits out `logout_disconnect_timeout_ms`.
- **A store of another kind** gets the ending that the hops of its own `store` call give it. Read,
  not run (`ph8a_readings.md`, its second section).

**The one ending (R-8), in three groups, each with what decides it.** The ending holds on every
store and on both transports, with `reset_on_disconnect` off. With it on, the three groups hold up
to the join, and the teardown reset of `close()` then empties the store, as on every close with that
setting (ruling R-10, §0; the comparison table, below). Groups 1 and 2 are by construction. Group 3
follows the Logout's store call. "The bound", below, is the one place where the store call does not
return by itself; it is the owner's ruling R-9 and is stated there once.

*Group 1: decided synchronously, when `toAdmin` returns.* No order of handlers and no store call's
outcome changes these. v0.10 adds the last two rows: v0.9 left the state until the Logout's store call
had returned (round 5's F-4).

| Observable | After this amendment |
|---|---|
| The Logout on the wire | not transmitted |
| A live write of the Logout | none is begun |
| `LogoutSent` | not recorded |
| `logout_seen_` | not set by this close, so a configured `reset_on_logout` does not fire for it. It is the rule of B28's ruling R-24: the flag is set only where the Logout was sent ("With batch B28", below) |
| The session's state | `Disconnected`, written by the Logout step before it takes the Logout's number and before the Logout's store call |
| `onLogout` | runs once, at that write, with the session `Disconnected`: right after `toAdmin` returns, and before the Logout is numbered and stored |

*Group 2: decided by the Logout step's exit and by `close()`'s join.* By construction, wherever the
step's awaits return by themselves.

| Observable | After this amendment |
|---|---|
| How the step ends | by itself, when the Logout's store call returns. It arms no wait and posts nothing |
| The logout timer | plays no part: the step arms no sleep on the session's clock, so there is no wait for the timer to end |
| The graceful close and the terminal close | both return the in-flight result, which is success |
| A live transport | `Session::close` calls its `close()` once after the join, because the Logout was cut, and its `close_async()` once in phase 2. On a TLS transport that pair is one abortive close, as at the head on every live arm of this condition (statement 4, fourth part) |
| At the end | `Disconnected`, `closed_drained` |

*Group 3: decided by the outcome of the Logout's store call.* The frame goes to the store as any
outbound frame does.

| The Logout's store call | The number | The store |
|---|---|---|
| completes with success | consumed: the next outbound number is the Logout's plus one | holds the Logout under its number. Measured on every arm with a store, under the three stall settings and in the held-lock scene (§3, P-12, P-16) |
| there is no store | consumed | nothing to hold |
| fails, on a persistent store | not consumed: `store_then_emit` sets the next outbound number to the store's own next number, which is the Logout's when the store wrote nothing (the reconcile of feature 059, a best-effort read). Read, not run | holds nothing |
| fails on a store that is not persistent, or the store's body throws | consumed: `store_then_emit` logs and proceeds, and with the transmit skipped it returns success. Read, not run | holds nothing |
| is ended by the grace timer ("The bound", below) | as that paragraph says for each place the step can be in | as that paragraph says |

Measured on S6, on all six arms under the three settings (§3, P-12). In the observables that
`ph8a_four_way.md` tabulates, this is what the head does today on a `FileStore` with a live transport
and no stall (that file's first column), apart from where `onLogout` runs. Two more things are outside
that statement. The head's split of the transport's close into one direct call and one `close_async()`
is inferred from a total of two; it was counted for S3, S5 and S6 only. And the Logout step ends
differently inside (read, not run): at the head an exception from the await of the live write ends
it; after this amendment it returns.

**The mechanism.** Four statements. Statements 1 to 3 are v0.8's, and both rounds' triages found them
sound. Statement 4 is v0.9's, with one part moved in v0.10: where `Disconnected` is written. The
prototype is `ph8c_mechanism_S6.patch`.

1. **A count on the session.** `Session` gains a member: the number of terminal closes that have
   taken `close()`'s wait. It only grows. It is a plain unsigned integer of 64 bits, written and
   read on the session's serialisation domain only, like the two depths of §2.2. `Session::close`
   increments it for a terminal close, inside the block of its wait (`if (callback_depth_ != 0)`)
   and before the post. A graceful close does not increment it. A terminal close that takes no wait
   does not. Its name is the prototype's, `waited_terminal_closes_` (the orchestrator's decision on
   the analysis pass's finding G-3).
2. **Two reads around the callback.** `run_logout_phase1` reads the count before it calls the Logout's
   `toAdmin`, and again when the callback has returned. `fire_to_admin_` is a plain call, and nothing
   suspends between the two reads. If the count moved, a terminal close took the wait inside this
   callback, and the Logout is *cut*. The test is "moved", not "non-zero".
3. **A frame that is stored and not transmitted.** `store_then_emit` gains a defaulted parameter that
   says whether to transmit. With it false the function stores the frame as it does today, with the
   store call's error handling unchanged, and returns success before Step 2. So neither the live
   write nor the synchronous sink runs, and the time of the last outbound frame is not updated. The
   test sits after the OD-25 `arm` test. Only the Logout step passes the parameter; every other
   caller behaves as today. Re-derive the callers:
   `grep -n 'store_then_emit(' src/session/session.cpp tests/support/session_test_access.hpp`.
4. **The cut Logout's step closes the session at once, leaves at once, and nothing cancels its store
   call.** Five parts.
   - **The record.** When the step finds that the count moved, it records the cut in a second new
     member of `Session`, at once: nothing suspends between `toAdmin`'s return and that write. The
     member is a flag, and its name is the prototype's, `logout_cut_`. It is written once and never
     cleared, because a `Session` has one life: `Session::open` refuses a session that is not
     `never_opened`, and a second graceful close joins the first and runs no Logout step
     (`ph8b_readings.md`, (c)).
   - **The state, at the decision point** (new in v0.10). Right after the record, in the same
     synchronous stretch, the step writes `Disconnected`: before `assign_outbound()` and before the
     Logout's store call. `record_state_transition_` calls `onLogout` on the edge out of Active, so
     `onLogout` runs there. Read, and no assertion fired in any run: the outbound callback depth is
     zero at that write, because the raise around `toAdmin` ends before `fire_to_admin_` returns, so
     the lifecycle sites' assertion of §2.2 holds. `assign_outbound()` and `store_then_emit` with no
     `arm` do not read the state, so the Logout is still numbered and stored, with the session
     `Disconnected` (`ph8c_predictions.md`, "The judge's trace of the moved write").
   - **The exit.** After the cut Logout's emit the step sets neither `logout_seen_` nor
     `LogoutSent`. It keeps its write of `Disconnected` "when the state is not that already", which
     does nothing on this route, and returns. It arms no wait and posts nothing. The value it
     returns is `dispatch_aborted`, as in the prototype (the orchestrator's decision on the analysis
     pass's finding B-2); `close()` discards it. This is the exit that B28's decision D-OUT-6 gives
     a Logout that was not sent. On B28's branch that exit returns what the Logout's emit left:
     `dispatch_aborted` where the emit threw, and the emit's own error otherwise (read at
     `7a7ed633`). **The step's exit after a failed `assign_outbound()` gets the same test** (round
     6's O-3): it writes `Disconnected` only when the state is not that already.
     `record_state_transition_` appends to the state history on every call, also when the state does
     not change. So without the test that exit would record a second visit of `Disconnected` on the
     cut route; `onLogout` would not run again, and the state would not change. On the cut route the
     exit is reached only where `assign_outbound()` returns an error. NC-32 (§3) drives it, with the
     outbound counter at its maximum.
   - **The join's waker.** In `Session::close`, a terminal close that joins a close in flight sets
     `close_escalated_`, as today. It does not call the grace timer's wake when the Logout was cut.
     So `close()`'s join does not cancel the cut Logout's step, and nothing cancels the Logout's
     store call before the grace timer expires. The step ends by itself when that call returns, and
     the step's arm of the join wins. For a Logout that was not cut the wake is called as today.
   - **The transport's direct close.** After the join, `close()` makes its direct `live->close()`
     when the timer's arm won or the Logout was cut. Today it makes it only when the timer's arm won.
     So a live transport sees one direct `close()` and one `close_async()` on every arm.
     - **What that pair is on TLS.** Read (round 5's triage, F-2, in `asio_tls_transport::close` and
       `close_async`; the first lines of both were read again for this revision): the direct
       `close()` marks the transport closed and closes the socket, and it skips the TLS shutdown
       while an SSL operation is suspended, which the read pump's read is on an established session.
       The `close_async()` after it finds the transport closed and sends nothing. So on TLS the pair
       is one abortive close, and the peer reads an OS-level error and no close-notify (B&L
       `B-348-1`). On the plain transport the two calls do not differ.
     - **The head does the same on every live arm of this condition.** Read: at the head the timer's
       arm wins there, because the escalation cancels the timer while the step is in its sleep or an
       exception leaves the step, so `close()` makes the direct call. Statement 4 keeps that call on
       the cut route, where the step's arm now wins. That is why the call is kept: no change against
       the head, and the same on every arm.
     - **Its original reason is absent on the cut route.** The direct call was made to unwind a
       write wedged behind the write gate. The cut step begins no write, and the join returns only
       when the step has retired.
     - **Not decided here: a clean close on this route.** Never making the direct call for a cut
       Logout would give a close-notify, by construction. It would be a change against the head that
       a TLS peer can see, which R-8 does not ask for, and it is one question for every
       escalation, cut or not: fixpp#596, batch B54. No cell of this amendment witnesses the TLS shutdown;
       the cells count calls on a transport double (§3, NC-21).

**Statement 4's part on the join's waker goes beyond the fix shape that round 4's judge gave.** That
shape (round 4's triage, RC-1) has the exit and the direct close and leaves the waker as it is. The
orchestrator added the part before v0.9. Round 5's judge reviewed it and found it sound: the route on
which the join's cancellation meets the cut Logout's store call at a lock for an instant is gone
(measured: NC-26's scene, and mutant D brings it back). What is left is the store call that has not
returned when the grace timer expires, and the owner has ruled it: R-9, "The bound", below.

Inside the callback one thing changes that did not before: the count. Nothing exposes it, so for the
application the session is still as it was (§2.1a, R-4). The state changes right after the callback
returns, not inside it.

**Why this is by construction.**

- **Group 1** is one comparison and two writes, made in one synchronous stretch when `toAdmin`
  returns. At the head the outcome is an order between two handlers on the session's executor: the
  completion of the Logout's store call, and the end of the Logout step that the escalation brings
  about. The comparison does not depend on how many hops a store's `store` call takes, on which
  thread completes it, on the transport kind, or on which handler reaches the executor first.
- **What the state's write covers.** Every reader that tests the session's state after `toAdmin` has
  returned finds `Disconnected`: an inbound frame goes to the arm that drains, `Session::send` is
  refused, and the liveness loop leaves at its next wake ("What arrives after the cut is decided",
  below). It does not cover an operation that passed its test of the state earlier and is suspended:
  that operation goes on, as it does at the head when any close begins (the same paragraph).
- **Group 2's exit** is a branch of the step on the same comparison. The step returns when its awaits
  return. They are, in order: `assign_outbound()`, which takes the sequence manager's lock; the
  Logout's store call; and, only after a store failure on a persistent store, the two awaits of the
  reconcile. No other handler has to arrive for the step to end, and no cancellation has to be
  delivered.
- **Group 2's "both closes return the in-flight result"** rests on an order that is forced (read;
  round 4's triage, F-1, checked for v0.9 in `Session::close` and in asio's `co_spawn.hpp` and
  `parallel_group.hpp`). The terminal close's post is made inside `toAdmin`, onto the session's
  executor. Nothing of the graceful close is queued behind it yet. The step's first suspension comes
  after `toAdmin` returns: a store's leading post, or, with no store, none before the step's end. A
  step that ends without suspending still cannot complete the join inside its own launch, because
  `co_spawn` posts its completion when the coroutine is still launching, and that post is behind the
  terminal close's. So the terminal close's first turn is ahead of every handler that can complete
  the join. In that turn it finds `closing` and takes the shared result's slot; from then on it
  returns the in-flight result. Measured: success and success on every arm and setting, the two arms
  with no store included (`ph8c_predictions.md`, Q1). The control that the cells can report the other
  result: with the wait made to post many times (mutant C), the terminal close returns
  `session_already_closed` on all six arms, and each cell is RED (`ph8c_mutant-C/`).
- **Group 3 is not by construction.** It follows the store call, and the tables above say how.

**What it depends on.**

- **The terminal close's body runs up to its wait before `toAdmin` returns.** That is the condition
  itself. It holds where the application starts the close so that the body begins at once: a
  `co_spawn` onto an executor that is running in this thread, the session's own or the
  `io_context`'s. Measured through its effect: the ending holds on the cells' `io_context` fixture
  and on their strand fixture (`ph8c_S6_arms_by_stall.md`), and it would not hold there if the count
  had not moved inside the callback. A close started by a post, from another thread, or onto an
  executor that is not running in this thread begins later, and is §7's case.
- **`close()` reaches the increment without suspending.** Before the block of its wait `close()` has
  one await, `reset_cancellation_state`, which does not suspend.
- **R-4's wait in `close()`, for the order in which the joining branch reads the record.** The
  mechanism as specified has R-4's wait (§2.1a). The record of the cut is written when `toAdmin`
  returns, and the terminal close reaches the joining branch one post later, so it finds the record
  and does not call the wake. That is what the design rests on, and it needs nothing of asio beyond
  a post being queued.
  - **With the wait's post removed, the ending did not change, at the asio version read.** Measured
    (mutant A): the six arms and every scene stay green, with the readings of the unmutated
    prototype (`ph8b_scenes-mutant-A.txt` on S5; `ph8c_predictions.md`, Q6, on S6). The reason is
    read, at asio 1.38.0, in `awaitable_operators.hpp`, `parallel_group.hpp` and `co_spawn.hpp`:
    `close()` awaits the step and the timer's wait with `||`, which starts its left operand first;
    the step's `co_spawn` begins with a dispatch, which runs inline because `close()` is running on
    that executor; so the step runs to its first suspension, and `toAdmin` with it, before the
    timer's wait is initiated. A wake called inside `toAdmin` then meets no pending wait and
    completes nothing.
  - **That is an observation at one asio version, not a property the design claims.** None of it is
    asio's documented contract, and fixpp pins none of it apart from those runs. The statement about
    `main` ("The behaviour change", below) rests on the same reading and carries the same
    reservation.
  - **What the ending does depend on is the waker's part itself,** for a close whose turn comes
    after the join has started both operands, which is every close that waited. Measured: with the
    test of the record removed from the wake and the rest unchanged (mutant D), the two held-lock
    cells are RED in every run: nothing is under the Logout's number, and both closes have completed
    while the other writer's persist is still held (`ph8b_predictions.md`, "Mutant D, result";
    `ph8c_predictions.md`, Q6, on S6). The two cells of the same scene in which `onLogout` starts an
    operation are RED under it too (`ph8d_predictions.md`). The six arms cannot see that mutant.
    Neither can a cell that holds the Logout's own write job with the order of the file executor's
    jobs kept: there the write runs, and the ending is the unmutated one (`ph8e_predictions.md`,
    "Mutant D on the `FileStore` arrival cells"; "The bound", (a), below). So the held-lock cells
    are the kill cells of the waker's part, and "the Logout is retained under its number" is the
    assertion that names the loss.
- **The callback does not run the session's own executor** (R-7). Where it does, a close that was
  queued earlier can begin inside the window and move the count (§7).

**The bound (the owner's ruling R-9, §0).** This is the one statement of it; §0a, §3, §4, §6 and §7
refer here.

*The rule.* Nothing cancels a cut Logout's store call before the grace timer expires. The grace timer
is `close()`'s own, of `logout_disconnect_timeout_ms`. It is armed before the Logout step starts, so
the time the application spends inside the Logout's `toAdmin` counts against it. When it expires, the
join cancels the step, once, and then waits for the step to retire. A terminal close that joined, and
`Engine::stop()`'s close of the session, wait with the graceful close for that. For a Logout that was
not cut, a terminal close that joins ends the grace wait at once (093 plan OD-29); for a cut Logout
it does not, and this narrows OD-29 on this route.

*What the ruling covers, measured.* The Logout's store call is still waiting for the store's lock at
the expiry, because another store call of the same session holds the lock that long. The timer ends
the join: the wait for the lock ends by the cancellation and the step leaves. The Logout is not
transmitted. Nothing is stored under its number, and the store's next outbound number is the Logout's
own. The session's next outbound number is the Logout's plus one on this branch; on a tree that
carries B28 it is taken back ("With batch B28", below). Both closes return success, and they complete
at the expiry: in the scene with a timeout of 300 ms, at 300 ms after the graceful close started in
every run, with the other writer's job held for another 600 ms (`ph8c_predictions.md`, "When the two
closes complete, against the timer"; §3, P-18). That reading was taken on a transport double whose
`close_async()` returns at once.

*What the option's sentences do not cover.* The option the owner chose begins "the timer ends the
close and that store call is cancelled", and ends: "A terminal close and `Engine::stop()` never wait
longer than the logout timeout for a store that is stuck at the lock." The mechanism keeps both for
a store call at the lock, as measured. Each point below is outside the last sentence, which is about
a store stuck at the lock. They are not new questions: each is what the head does on its own routes,
and no choice inside R-8 follows from them. Points (a) to (e) were told to the owner on 2026-10-13.
§0, R-9, "What the owner was told", says when and what, from the task list's section "What the owner
was told about the bound of R-9 (2026-10-13), and a correction owed to it". The two bullets after
(e) are this note's readings, and that section does not record them as told.

- **(a) The Logout's own write job is still in the file executor at the expiry. Measured, and
  against the option's first sentence for this case: the timer does not end the close there**
  (`ph8e_predictions.md`; §3, P-22). The case has two halves, and they do not have the same reach.
  - **The timing, on every kind of file executor that was run.** The closes do not complete at the
    expiry. They complete when the file executor has run the write's job: in the scene, not
    completed 600 ms past a 300 ms expiry, and completed 0 to 9 ms after the job was let go. The
    join waits for the cancelled step, and the step waits for its job to be run; a cancellation does
    not take a job out of the executor. The timing is the same on the route with the Logout not cut,
    a graceful close alone, whose code this amendment does not change. So the logout timeout does
    not bound a graceful close whose Logout's write job is in the file executor. §4 gives that a B&L
    row of its own (round 6's O-5).
  - **How a cancellation reaches the write.** Read, in asio 1.38.0's `impl/co_spawn.hpp`; the cells
    print the order as a numbered sequence of the executor's jobs. `FileStore::store` awaits its
    write through `offload_to`, which awaits a `co_spawn` onto the file executor. asio does not
    record a cancellation in the spawned coroutine when it is emitted. It hands a second job to the
    file executor, and the cancellation is recorded when that job runs. So what the write's job
    finds depends on which of the two jobs the executor runs first.
  - **What the store holds on a file executor that keeps the order of its jobs.** The write's job
    runs first. The write's body runs, the store's catch of `operation_aborted` does not fire, and
    the Logout is written and reads back under its number. Measured on a pool of one thread: 40 of
    40 runs, cut and not cut, and the record was unreadable in none. On the cut route that is R-8's
    own ending, reached late: numbered, stored, not transmitted. A strand over a pool keeps order
    too, by reading; it was not run.
  - **What the store holds on a file executor with more than one thread:** that, or, as a race, a
    counted and unreadable record. The unreadable record needs the cancellation's job to run before
    the write's body starts. Then the write's body never runs; the store's catch fires and takes the
    abort for a write that completed; the store's next outbound number is advanced past the
    Logout's; the call returns success; and a read of the Logout's number returns
    `store_io_failure`. Measured on a pool of two threads, with nothing reordered by the cell: in 40
    of 40 runs where the thread that takes the write's job is delayed before it starts it, and in 1
    of 40 where both threads are held and then let go together. On the route with the Logout not cut
    the Logout is then transmitted, for a frame the store counts and cannot read. On the cut route
    nothing is transmitted. The state is reached on both routes, so it does not come from the cut,
    and `src/session/file_store.cpp` is as on `main` (`ph8c_readings.md`). Its issue is fixpp#598,
    which the owner placed in batch B28 (§0, R-10's bullet). This amendment does not change it and
    does not present it as its ending.
  - **The host supplies the file executor.** The library creates none. `FileStoreFactory::make`, in
    `src/session/file_store_factory.cpp`, takes the executor from its configuration or from its
    caller, and returns `store_factory_failed` when both are empty. So how many threads it has is
    the host's choice.
  - **v0.10 stated the unreadable record as what the head does on this route, with no condition on
    the executor. That was too wide.** Its measurement (`ph8c_predictions.md`, "The Logout's own
    write job past a short timeout") held the write's job behind a test gate that let every later
    job pass, and the cancellation's job is such a later job. So the gate put the two jobs in the
    order the unreadable record needs, in every run (round 6's O-1). Both files of that evidence now
    say so at their top.
  - **Not run:** a write already inside its system calls at the expiry ((b), below); a pool of more
    than two threads; a strand over a pool; a pool that is busy, as opposed to one held by the cell;
    a restart of the store over a file with a counted and unwritten record; how often the race is
    lost outside this fixture; ThreadSanitizer.
- **(b) A write already inside its system calls at the expiry is waited for.** Read, not run
  (`FileStore::store` and `offload_to`; `ph8c_readings.md`, its case of a job that is running). The
  write is not interruptible there; the Logout is retained and its number stays consumed, and the
  closes complete when the write returns, which can be after the expiry. By the same reading the
  head waits for that write too. **A store of another kind that does not give its call up on a
  cancellation holds the join,** at the head too.
- **(c) `Engine::stop()` as a whole.** Read, not run (round 5's triage, F-1 and O-6, in
  `Engine::stop`). The bound holds for the step of `stop()` that closes the session and joins the
  close in flight. An earlier step of `stop()` joins the session's loops and drains `Engine::send`,
  with no bound. Whatever holds the store's lock that long is one of those coroutines, inside a
  write that cannot be interrupted, so `stop()` already waits for it there, with this amendment or
  without it. Under the Engine the bound therefore adds at most the Logout's own turn at the lock
  and its write.
- **(d) With `reset_on_disconnect` set,** the teardown reset of `Session::close` takes the store's
  lock after the join, and no timer watches that wait. With the lock still held, both closes wait
  for its holder, past the expiry. Read, not run.
- **(e) The timer bounds the join, not the whole close.** After the join the close makes its
  ordinary teardown: the transport's `close_async()` up to its own timeout, and the drains.
- **The step's other awaits at the expiry.** Read, not run (round 5's triage, O-1; corrected by
  round 6's O-3). `assign_outbound()` is not a place where the step can be waiting at the expiry.
  Every await of `src/session/seqnum_manager.cpp` is the manager's lock, and no function there
  awaits anything while it holds the lock (re-derive:
  `command grep -n co_await src/session/seqnum_manager.cpp`, and read each function to its end). On
  the session's one serialisation domain the lock is therefore free whenever a handler runs, and no
  step is queued at it. v0.10 listed such a step here as a case, while "What arrives after the cut
  is decided" already had the reading that excludes it. `assign_outbound()` ends the step early only
  where it returns an error, and that exit is statement 4's. A step in the reconcile's awaits after
  a store failure on a persistent store is cancelled there; what the number is then was not read to
  a conclusion.
- **A `toAdmin` that lasts longer than the timeout.** The timer has expired before its wait is
  initiated, so the wait completes as soon as it starts, and the join's cancellation meets the
  Logout's store call wherever it is by then: at the store's lock even if it is held only for an
  instant, or in its offload, which is (a). Read, not run. So "the lock held that long", above, is
  the measured input and not the only one.

*Who rules the number where the store call is ended at the lock.* B28's rulings R-15, R-16 and R-19:
on a tree that carries both changes the number is taken back. On this branch alone it stays
consumed, as measured. R-9's option says the same.

**What was measured, and its limits.** The bullets below are prototype measurements, made in a
scratch copy and not in the pull request's tree. §3's rows P-12 and P-16 to P-23 name the runs and
the files. What was measured after them, on the final patch and in the pull request's tree, is in
the record's sections "Phase 8, first part" to "Phase 8, third part"; §3's last block has the
status of each prediction.

- **The six arms** under the three settings, plain, with `reset_on_logout` set, and on a strand
  executor: the ending of this section in every run, 20 runs each and 300 on `FileStore` live with no
  stall (`ph8c_S6_arms_by_stall.md`; `ph8c_predictions.md`, Q1). The mock clock was not advanced in
  any run, and the largest real time of a run was far below the grace timer's 60 s. The one reading
  that differs from the prototype before: inside `onLogout` the next outbound number is the Logout's
  own, where it was the Logout's plus one. That reading is the evidence that `onLogout` runs before
  the Logout is numbered and stored. No lifecycle assertion fired in any run.
- **That the step's arm ends the join there is inferred, not observed** (`ph8b_predictions.md`, P2).
  Nothing outside `close()` can read which arm won, and no print was added.
- **The held-lock scene** (`ph8b_held_lock.md`; `ph8c_predictions.md`, Q5). A second writer of the
  same session, the inbound persist of an order from the peer, holds the `FileStore`'s lock in its
  offload while the cut Logout's store call reaches it. Green on S6 and on S5: after the held write
  is released both closes complete with the mock clock standing, and the Logout is retained under
  its number. RED on S3, consistent with round 4's O-1; no line of that output shows where the
  cancellation landed. On S6 the cell also asserts, fatally, that the held `on_inbound_frame` has
  not returned at the snapshot (round 5's O-3), which places the lock's holder; it held in every
  run.
- **What arrives while the cut Logout's own store call is held** (`ph8c_in_flight_frames.md`): "What
  arrives after the cut is decided", below.
- **The bound** in its measured cases: "The bound", above.
- **The Logout's own write job at the expiry, on four kinds of file executor**
  (`ph8e_predictions.md`): "The bound", (a), above, with what was not run.
- **What the application starts inside the decision-point `onLogout`** (`ph8d_predictions.md`):
  "What arrives after the cut is decided", below. Not run there: `Engine::send` and `Engine::stop()`
  from that `onLogout`; a graceful close started there with the store's lock held; the live
  transport double outside the scene of the terminal close; a real TLS transport; the stall
  settings; ThreadSanitizer; a release build; a callback that runs the session's executor inside
  `onLogout`, which R-7 excludes. The two mutants of that evidence ran five times per cell.
- **With the cells as they are at the head,** exactly five cells change result, and all five are this
  condition's own arms. S5 against S6, no original cell differs (`ph8c_predictions.md`, Q7;
  `ph8c_regressions_per_cell.tsv`).
- **After the change no observable of the six arms tells the two orders apart.** That both orders ran
  rests on the probe: its positive control flips, and its stall line is printed on both `FileStore`
  arms in every stalled run (`ph8c_probe-positive-control.txt`; `ph8c_predictions.md`, Q1).
- **Not covered** (`ph8c_predictions.md`, "Not covered"; `ph8c_in_flight_frames.md`, "Not
  covered"). 46 of the 63 session test binaries were not built; nothing outside `tests/session` was
  built; no ThreadSanitizer run and no release build was made. The arrival cells and the own-write
  cells use the synchronous sink only. No real TLS transport was run anywhere. One position of a
  suspended send cannot be driven without a seam in production code. The runs on S5 and the mutants
  ran five times per cell; the stall settings were not applied to the scenes. The held-lock scene
  and the bound ran on a `FileStore` only. The condition was not run on an Engine-managed session
  or under `Engine::stop()`, because the cells drive a `Session` themselves.

**What arrives after the cut is decided.** v0.8 said the FSM stays Active "until the waiting terminal
close runs", which was false. v0.9 wrote `Disconnected` after the Logout's store call, so the session
was Active during that call, and what arrived then was acted on after the application's terminal
close (round 5's F-4). The statement now:

- **Everything that tests the session's state after `toAdmin` has returned meets a closed session.**
  The step writes `Disconnected` before anything else can run on the session's executor. An inbound
  frame goes to the arm that drains: no callback, no reply, no number. A `Session::send` from another
  coroutine is refused with `session_invalid_state_for_send`. The liveness loop, woken, emits
  nothing. This holds for a frame whose handler was queued before the terminal close's post as well;
  v0.9 said that such a frame meets Active.
- **Measured, in the hold of the Logout's own store call** (`ph8c_in_flight_frames.md`; §3, P-20).
  On a `FileStore` and on a store double that holds its `store` call, on the synchronous sink, each
  of: a Logout from the peer, the same with `reset_on_logout` set, an application message, a
  TestRequest, a `Session::send` from another coroutine, and a wake of the liveness loop. The
  `FileStore` hold was run in two forms: behind the gate, and with the pool's one thread held, so
  that the order of the executor's jobs is kept (`ph8e_predictions.md`, the cells named
  `…OrderKept`). On S6 the two forms read the same. On S6, in every run: no `fromApp`, `fromAdmin`,
  `toAdmin` or `toApp`; neither next number moves; nothing is transmitted; the send returns
  `session_invalid_state_for_send`; with `reset_on_logout` set there is no reset; after the release
  both closes return success and the Logout is retained under its number.
- **On the prototype before (S5) each of those arrivals is acted on,** and those cells are RED there:
  a Logout from the peer draws a confirming Logout numbered one above the cut one, and with
  `reset_on_logout` set the teardown reset then fires; an application message reaches `fromApp`; a
  TestRequest draws a Heartbeat; the send is admitted, numbered and transmitted; the liveness wake
  emits a Heartbeat. In every case but the application message the peer is sent a frame numbered
  above a Logout it never received.
- **After the Logout's store call has returned** the same holds, measured on a `MemoryStore` and
  with no store (`ph8b_predictions.md`, P5, on S5; green on S6): a Logout from the peer draws no
  `fromAdmin`, no confirming Logout and no number.
- **Not covered by the write: an operation that passed its test of the state before the cut was
  decided and is suspended.** A reading, measured (`ph8c_in_flight_frames.md`, "A reading: a send in
  its own store call when the cut is decided"; §3, P-21): a `Session::send` that has its number and
  is suspended in its store call when the cut is decided keeps its number and is transmitted when
  its store call returns, on S5 and on S6; on S6 that is after the session was written
  `Disconnected`. The Logout takes the next number and is not sent. So the peer receives no number
  above one it did not get. It is what a send in flight does at the head when any close begins. The
  scene used the synchronous sink, which accepts a frame at any time; a live transport that refuses
  a write after its `close()` was not run, and the head was not run in this scene.
- **One position was not driven:** a send that has passed its test of the state and has not yet
  taken its number when the cut is decided. It cannot be driven without a seam in production code.
  Read: between that test and `assign_outbound()` the frame's build and `toApp` are synchronous, and
  the sequence manager's lock is never held across a suspension, so no handler of the session can
  run between the test and the number.
- **What the application starts inside that `onLogout`** (round 6's F-1). `onLogout` now runs at the
  decision point. Inside it the session is `Disconnected`, its lifecycle is `closing`, the Logout
  has no number yet, and a callback is on the stack, so R-4's wait applies to a close. Each
  operation has one outcome there:
  - **A terminal `close()`.** Measured (`ph8d_predictions.md`, scenes 1 and 4; §3, P-23): it takes
    the wait and joins; the count moves once more, after the step compared it; all three closes
    return success; the Logout is numbered and retained, and nothing is transmitted. Read: nothing
    reads the count again. That the close does not call the grace timer's wake is measured by its
    effect, and only with the `FileStore`'s lock held by another writer (scene 4): no close
    completes while the lock is held, and the Logout is retained after the release. Those cells
    cannot tell which close's wake it would have been (§3, NC-31's row).
  - **A graceful `close()`.** Measured (scene 2). It waits by one post and joins. The count does not
    move. It returns success, and the rest is as above. Not run with the lock held.
  - **A `Session::send`.** Measured (scenes 3 and 4). It returns `session_invalid_state_for_send`,
    with no `toApp` and no number, also with the lock held. Read: the send's body tests the state
    before it tests the callback depth, so the send is refused at once and nothing is deferred.
  - **`Engine::send`.** Read, not run (round 6's triage, F-1). Both of its bodies test the session's
    state on the session's strand and return `session_invalid_state_for_send`. The state never
    returns to Active, so when the body runs does not matter.
  - **`Engine::stop()`.** It is ruling R-5's excluded route, as from any callback (B&L `L-563-4`,
    fixpp#579). This note states no ending for it. Read, not run: the cut and the state were decided
    before `onLogout` ran, and a terminal close that `stop()` later makes of this session takes
    `close()`'s joining branch, where the record of the cut is set and the wake is not called.
  - **What is new at this position.** Not the refused send. At the head `onLogout` on this condition
    runs at `LogoutSent` or at `Disconnected` (measured; "The behaviour change", below), and a
    send's test of the state refuses it there too. Two things are new. A close started there begins
    before the Logout has a number and a store call; the close posts and then only joins, so the
    Logout is still numbered and stored (measured). And a terminal close started there does not wake
    the grace timer. On the route with no close in `toAdmin`, which this amendment does not change,
    a terminal close started in a graceful close's `onLogout` does wake it
    (`CallbackSend.OnLogoutOfGracefulCloseStartsClose_Terminal` is that route's cell; round 6's
    triage, F-1). The suppressed wake is the waker's part of statement 4 again, on the same branch
    as for the close started in `toAdmin`. Its kill cells are the held-lock cells (§3, mutant D).
  - **The order of the record of the cut and the state's write has no effect a caller can see.**
    Measured: with the two statements swapped, every cell of these scenes is green (mutant G; §3,
    "Specified without a witness").

**Compared with QuickFIX C++ and QuickFIX/J.** Read, not run (`ph8a_readings.md`, its first section,
which names the commits and the lines): `logout()`, then `disconnect()` inside that Logout's `toAdmin`.

| | QuickFIX C++ and QuickFIX/J | fixpp after this amendment |
|---|---|---|
| The number | consumed | consumed, where the store call completes (group 3) |
| The Logout in the store | stored, after `toAdmin` returns | stored, where the store call completes (group 3) |
| The Logout on the wire | not transmitted: `disconnect()` removed the responder | not transmitted |
| `onLogout` | inside `toAdmin`, before the Logout is stored | right after `toAdmin` returns, before the Logout is numbered and stored. R-4's departure is unchanged: just after the callback, not inside it (B&L `B-563-2`) |
| The engine's own "Logout sent" flag | ends true: it is set after the send call returns, sent or not | `logout_seen_` is not set |
| A reset on logout | does not fire: its one acting reader runs on receipt of a Logout from the peer, and none can arrive | does not fire |
| A reset on disconnect | QuickFIX C++, read by round 5's judge at `386ce46e`: `disconnect()` resets inside `toAdmin`; the Logout is then stored under the number stamped before the reset, and the sender number is incremented. It ends with a reset store that holds one frame under the old number, and the next sender number at 2. QuickFIX/J was not read again | **ruled (R-10, §0): fixpp keeps its own behaviour.** The teardown reset runs in `close()` after the Logout step, as today and as in every prototype, and leaves the store empty and both next numbers at 1. So after the close the store does not hold the Logout that the step stored. The order differs from QuickFIX C++'s; the difference is stated and not removed. NC-33 (§3) pins it. No prototype arm ran it |

The flags differ and the observable agrees. The engines' flag is their internal state. What an
application or a peer can observe on this sequence is that no reset on logout happens, in either
engine and here. What a flag left true does at those engines' next logon was not followed (the same
file says so), so the agreement is claimed for this sequence only.

**The behaviour change.** It is visible to an application that starts a terminal close inside the
`toAdmin` of its own graceful close's Logout, and to its peer. It goes under "Compatibility" in the
pull request.

- **Against the head.** What the head does on each arm is measured (`ph8a_S_arms_by_stall.md`, the
  tables headed "unpatched head", under the three stall settings). The state in which `onLogout` ran
  is measured. That this state is recorded after the Logout's emit has returned, or is written by
  `close()` after its join, is read in `run_logout_phase1` and in `Session::close`.
  - **Five arms: no store and `MemoryStore`, each on the synchronous sink and on the live transport,
    and `FileStore` on the synchronous sink.** Today, under every setting: the Logout is transmitted
    once; `onLogout` runs with the session at `LogoutSent`, which is after the Logout was numbered,
    stored where there is a store, and transmitted; `LogoutSent` is recorded; a configured
    `reset_on_logout` fires.
  - **The sixth arm: `FileStore` on the live transport.** Today, with no stall and with the file
    executor's jobs delayed: the Logout is retained and not transmitted; `onLogout` runs with the
    session `Disconnected`, which `close()` writes after its join, so after the Logout was numbered
    and stored; `LogoutSent` is not recorded; a configured `reset_on_logout` does not fire. That
    `onLogout` runs after the Logout took its number is inferred there from that state and from the
    retained Logout; the number was not read inside `onLogout` at the head, and §3's NC-21 calls
    that reading predicted. Today, with the session's thread stalled after the hand-off: as the five
    arms.
  - **With the store's lock held by another call of the session when the Logout's store call reaches
    it** (added in v0.13). Today, on a `FileStore` on either transport: the Logout is transmitted,
    and the store does not hold it. That is the scratch rehearsal of NC-26's and NC-28's cells at
    the head (§3), and it adds to the sixth arm above: on the live transport the head transmits in
    this case too, whatever the order. The cause is read, not measured ("What the head does",
    above). After this amendment: stored and not transmitted (measured on S6; §3, P-16), or, at the
    bound, neither stored nor transmitted (§3, P-18).
  - **After this amendment, on all six arms and in either order:** the Logout is not transmitted.
    `onLogout` runs once, with the session `Disconnected`, right after `toAdmin` returns and before
    the Logout is numbered and stored. `LogoutSent` is not recorded. A configured `reset_on_logout`
    does not fire.
  - **So:** five arms stop transmitting a Logout that they transmit today, and the sixth stops
    transmitting it in the one order in which it does today. On all six arms `onLogout` moves: it
    runs before the Logout is numbered and stored, where today it runs after. That holds for the
    `FileStore` arm on the live transport too. Where `LogoutSent` is recorded today it no longer is,
    and where `reset_on_logout` fires today it no longer does.
  - **What arrives after `toAdmin` has returned** meets a closed session, where at the head it met
    Active during the Logout's store call and `LogoutSent` after it ("What arrives after the cut is
    decided", above).
  - **What the application starts inside `onLogout`** on this route now starts before the Logout has
    a number, and a terminal close started there does not end the grace wait (the same paragraph,
    its bullet on that `onLogout`).
- **Against 093's OD-29.** For a cut Logout, a terminal close that joins does not end the grace wait.
  The difference shows only at the bound.
- **Against `main`.** Read, not run on `main`, on its `Session::close` (`git show
  origin/main:src/session/session.cpp`; round 4's F-5, checked for v0.9). `main` has no wait, so the
  terminal close's body runs inside `toAdmin`. It finds a close in flight, sets `close_escalated_`
  and calls the grace timer's wake. At that instant the timer's wait does not exist: `close()` awaits
  the step and the timer's wait as one group, the group launches the step first, and the step runs
  to its first suspension, which is after `toAdmin`, before the timer's wait is launched. A cancel of
  a timer with no wait pending does nothing. `close_escalated_` is tested only before the step
  starts. So the step goes on as if no close had been started: the Logout is transmitted,
  `LogoutSent` is recorded, and both closes end when `logout_disconnect_timeout_ms` elapses. Nothing
  in that reading depends on the store or on the transport, so it covers all six arms. The close
  enters no callback inside `toAdmin`, so the guard's assertion is not reached, and a build with
  assertions behaves there as a build with `NDEBUG`. The reading carries the reservation of "What it
  depends on": it is asio's order at the version read. In support, on this branch's code without the
  wait and on a build with `NDEBUG`, four arms were run and gave that ending (the record's "Phase 4,
  addendum"), and B&L `B-563-2` states it. So against `main` the escalation is this pull request's
  change, and "not transmitted" is this amendment's.
- **For the peer.** The connection closes with no Logout; on TLS, abortively, as at the head. The
  peer has not received the Logout's number. A later session meets that through the resend path,
  which this amendment does not change. Not run.

**Beside `L-563-1`, condition (2).** That condition is a terminal close that is the session's first
close, started in `toApp` or `toAdmin`. After this amendment the two conditions end differently on
some arms:

| Store, transport | `L-563-1` (2), a first close that is terminal: the outer frame | §2.6: the Logout |
|---|---|---|
| `MemoryStore` or `FileStore`, live transport | retained, not transmitted | retained, not transmitted |
| `MemoryStore` or `FileStore`, synchronous sink | retained, transmitted after `onLogout` | retained, not transmitted |
| no store, either transport | transmitted before the close begins | not transmitted |

The difference exists. Condition (2) is ruling R-4 as Gate A approved it. §2.1a ("An outbound emit
can be cut") records that the orchestrator kept it on 2026-10-12, and the comparison it was kept on.
Its split is by store kind and transport. The stall probe did not flip it: the record's "Phase 4,
third part" and "Phase 4, fourth part" report that no asserted order flipped on the `FileStore` arms
of the close cells. This amendment does not change condition (2), and R-8 does not rule on it.

The other Logouts that the engine emits through `toAdmin` do not read the count: the Logout that
confirms a logout the peer started, the Logouts of the 789 path, and a Logout sent before a
disconnect. A terminal close started in their `toAdmin` is the session's first close, which is
condition (2) (read: `ph8a_readings.md`, its fourth section).

**With batch B28.** Its note is `.specify/536-537-538-recovery-liveness.md` on the branch
`536-537-538-recovery-liveness`; the note is not on this branch. Read on 2026-10-13 in that branch's
worktree at `902630b6` (v0.16), and again for v0.11 at `7a7ed633` (v0.17). `src/session/session.cpp`
and `include/fixpp/session/session.hpp` are the same at both heads (§0, R-8, has the command). Read
it again before relying on this. B28 rewrites the same lines: `store_then_emit`'s declaration, which
is no longer `noexcept` there and throws when the store call was cancelled, and the Logout's emit
call in `run_logout_phase1`.

v0.8 named two differences of rule and left them to the pull request that merges second. Round 4
found that a design note may defer a shape that way and not an observable. Both are settled:

- **The step's exit.** B28's ruling R-17 has the close go to its terminal part at once when the
  Logout was not sent, "for every not-sent route" (the owner's words, as that note records them). A
  cut Logout is a Logout that was not sent. Statement 4's exit is R-17's exit, so the two pull
  requests share one rule.
- **`logout_seen_`.** B28's ruling R-24 (§0): the flag is set only where the Logout's emit returned
  success. That is what §2.6 does on its route. The rule is one, and it is R-24's; this note does not
  decide it. One of the reasons that note gives for R-24 cites this note's v0.8, so the agreement is
  not two independent findings.

*The Logout step on a tree that carries both changes, per route.* Each cell names what rules it. R-8
and R-9 are this note's. R-15, R-16, R-17, R-19 and R-24 are B28's, read from its note and not run
here. "Read" marks a cell that was read and not run. Round 5's judge and round 6's checked each cell
against `902630b6` and found them to hold; round 6's O-3 corrects one sentence beside the table,
below. **No tree carries both changes, so no row of this table can be run as a whole until both are
merged;** what was measured on this branch alone is marked. On all three rows the session is already
`Disconnected` when the store call is made, by statement 4.

**What a later write of `Disconnected` does there depends on which write it is** (round 6's O-3;
read on the two branches). `record_state_transition_` appends to the state history on every call,
also when the state does not change. B28's write in the step's exit for a Logout that was not sent
has the test "when the state is not that already", and does nothing. B28's write inside
`store_then_emit`, on its exit for a cancelled store call, goes through
`disconnect_unless_superseded_`, which calls `record_state_transition_` with no test of the state
when the caller passed no `arm`; the Logout step passes none. So on the second row a tree that
carries both changes records a second visit of `Disconnected`, unless that write is guarded.
`onLogout` does not run again and the state does not change: the effect is one more entry in
`fsm_visit_history()`. v0.10 said that B28's own write of `Disconnected` does nothing on these
routes. That holds for the first of the two writes only. The guard is an instruction to the pull
request that merges second, and the cell that waits for both asserts its effect (below).

| The cut Logout's store call | Transmitted | The number | The store | `logout_seen_` | The step's exit | Both closes |
|---|---|---|---|---|---|---|
| completes | no (R-8; measured here) | consumed (R-8; measured here) | holds it (R-8; measured here) | not set (R-24; measured here through the reset). On the merged tree this holds only if the cut route's exit stays ahead of B28's assignment of the flag, because the emit returns success here (the merge hazard, below); NC-23 pins it and is run again there | at once (R-17; statement 4; measured here) | success, success (measured here) |
| is ended by a cancellation while it waits for the store's lock, which on the cut route happens only at the grace timer's expiry (R-9; "The bound") | no (R-15; measured here) | taken back (R-16, R-19; read). On this branch alone: consumed (measured) | holds nothing (R-15; measured here) | not set (R-24; read) | at once: `store_then_emit` leaves by its exception there (R-16, R-17; read), after its own write of `Disconnected` (the paragraph above; read) | success, success (B28's decision D-OUT-6; read). On this branch alone: success, success (measured) |
| fails, on a persistent store | no (R-8; read) | set to the store's own next number by feature 059's reconcile (read) | holds nothing (read) | not set (R-24; read) | at once (R-17; statement 4; read) | success, success (read) |

The routes of R-17 that the table has no row for:

- **A cut Logout whose write fails: not applicable.** A cut Logout's transmit is skipped, so no write
  is made.
- **A Logout that could not be built is never cut.** `toAdmin` runs only where the build succeeded,
  so the count is not read. The route is B28's alone: nothing transmitted, no number, nothing stored,
  the flag not set (R-24), the step leaves at once (R-17), the close returns success. Read.
- **A Logout that is not cut and was not sent** (its store call ended by a cancellation, or failed on
  a persistent store, or its write failed) is B28's, and §2.6 does not change it: the number per R-16
  and R-19, or per 059's reconcile, or consumed; the flag not set (R-24); the step leaves at once
  (R-17); the close returns success. Read.
- **A Logout that is not cut and was sent** is as today on both branches: `LogoutSent`, the flag set,
  the wait.
- **A store call that `FileStore` reports as success though it wrote nothing** (fixpp#598; "The
  bound", (a): it needs the cancellation's job to run before the write's body starts, on a file
  executor with more than one thread) is on neither branch's table as an ending. As B28's code
  stands (read at `7a7ed633`) its rulings do not see it, because the store call does not report the
  cancellation. The owner placed that issue in batch B28 (§0, R-10's bullet), so B28 is where that
  catch changes.

On this branch alone, before B28 merges, a Logout that is not cut and was not sent keeps today's
behaviour: the step ignores the emit's result, records `LogoutSent` and waits. R-17 and R-24 are
B28's rulings, and §2.6 applies their rule to the cut route only.

*The merge hazard, and the rule for the pull request that merges second.* On this note's cut route
`store_then_emit` is called with the transmit skipped, and it returns success: the store call
succeeded, and nothing was sent on purpose. B28's condition, both for its early exit and for setting
`logout_seen_`, is the emit's result: the exit is taken when the emit did not return success, and the
flag is set after it. A cut Logout passes that test as if it had been sent. So on a tree with both
changes the cut route's own exit must stay ahead of B28's assignment of `logout_seen_` and of the
`LogoutSent` write: the step leaves at once when the Logout was cut or was not sent. If the cut
Logout fell through, the flag would be set for a Logout that was not sent, against R-24, and with
`reset_on_logout` set the teardown reset would fire. This is an instruction to whichever pull request
merges second. Its pin is NC-23 (§3): the `reset_on_logout` arms of this condition assert that no
reset happens. They must be run on the merged tree, and they must be green there.

*What is run again on the tree that carries both,* by the pull request that merges second:

- NC-23's arms, for the merge hazard above.
- NC-29, and NC-27's six `FileStore` hold arms. Batch B28 now carries the work of fixpp#598, which
  changes the catch of `operation_aborted` in `FileStore::store`, and these arms read the store
  through that path: NC-29 reads the catch's counter and the offload probe, and the hold arms hold
  the same write job. They are specified on a hold that keeps the order of the executor's jobs,
  where the write runs and the catch does not fire, so they are predicted to stay green. That is a
  prediction, and the merged tree is where it is run.
- The arms that use the gate's third mode: NC-26, NC-28, NC-30's `FileStore` arm, and NC-31's two
  arms with the lock held. Their check that the store's catch counter reads zero is written against
  the catch as it is at the head. B28's change can alter what that counter counts, or remove it. On
  the merged tree the check is derived again from B28's catch, and it is first shown able to report
  there, on a scene in which the catch fires.
- NC-28's and NC-24 (b)'s clause on the session's next outbound number, which B28's rulings change
  (the second row of the table).

*The cell that waits for both.* A cut Logout whose store call is ended by a cancellation at the
store's lock, on a tree that carries both changes: the second row above. Its assertions, named now:
not transmitted; the next outbound number is the Logout's own, in the manager and in the store;
nothing under that number; `logout_seen_` not set, read through `reset_on_logout`; both closes
return success; the transport's close counts; and the state history holds one visit of
`Disconnected` (round 6's O-3). The pull request that merges second adds it. Until then NC-28 (§3)
pins the same scene with this branch's number.

## 3. Tests and gates

Every cell runs alone (`--gtest_filter`), on a build with assertions and on a build with `NDEBUG`, with
the exceptions the cells' rows name. `linux-clang-debug` and `linux-clang-release` are the presets this
section names for the two, and where a row below names one of them the condition it means is that one:
assertions, or `NDEBUG`. Every cell also passes in a whole-binary run, which is how ctest runs it. A death
arm exists only on a build with assertions, because the assertion it dies at compiles out under `NDEBUG`;
it skips on a build with `NDEBUG`, as the I-3 test does today. NC-16's third arm exists only on a build
with `NDEBUG`. "Classification" is the cells' own
three-way witness: nested, deferred, never ran. A cell that expects one classification fails on either of
the other two.

**Mutant first.** Every cell or arm this revision adds or changes was specified from its mutant: the row
in "Arms that must go RED" states what the wrong implementation does in the cell, and the cell's assertion
is the one that outcome fails. The exceptions are these: controls (NC-9 (c),
NC-16's fourth arm, NC-4's second and fourth arms, NC-7's `total` arm, NC-11's positive-control arm);
arms that pin a disclosed
limitation or consequence as measured (NC-5 (f), NC-12, NC-14); and pins of a property this PR does not
introduce (NC-1, NC-2's `onLogon` arm, NC-18, and NC-17's assertion on the outer frame's bytes, which is
B35's property; NC-17's order assertion is in the first mutant row). Where a mechanism
has no cell that can fail, none is claimed (the list after the mutant table).

**Fixture additions the cells need.** At the commit Gate A reviewed, the fixture of
`test_563_564_callback_send.cpp` left the threading mode at its default and set the heartbeat interval to
zero. A tests-only commit made with the measurements of 2026-10-11, `be46718a`, adds part of what
follows; each addition has a default that leaves every existing cell as it was, and the commit's message
lists them. "On the branch" marks an addition that commit made. The new cells need:

- a threading-mode knob: `direct_executor`, attested, over the bare `io_context` executor (NC-16, NC-20).
  On the branch;
- a heartbeat-interval knob (NC-20, and NC-11's `onLogon` arm on a persistent store). On the branch;
- an `onCreate` hook on the cell application, which had hooks for `onLogon` and `onLogout` and none for
  `onCreate` (NC-11). On the branch;
- a peer-frame builder that takes the BeginString; `peer_frame` writes the session's own (NC-19's
  lifecycle arm, and NC-11's `onLogout` sub-arm that a `Disconnected` write outside `close()` fires). On
  the branch;
- test access to `callback_dispatch_scope`, so that a cell can enter and leave a scope without a session
  doing it (NC-9). It is added to `session_test_access` in `tests/support/session_test_access.hpp`, the
  one file in which that struct is defined (`brain/components/session.md`, "Test access to private
  `Session` state"). Not on the branch;
- the store double of NC-14. It is local to `tests/session/test_563_564_callback_send.cpp`, the file of
  the cell that uses it. `tests/support/hooked_store.hpp` is read first, and its `HookedStore` is used
  instead if it can hold one `store()` call and complete a later one first. Not on the branch.

Also on the branch from that commit, for arms this section already specifies: a role knob (NC-11's
"both roles"); a delivery of several frames by one coroutine with nothing between two frames (NC-12);
the transport kind as an argument of the helper that the inbound close cells share ("The arms every
close cell has"); the value `toApp` returns, set by the cell; and, on the live transport double, a count
of `close()` calls and of the writes begun after the first one, with a setting that makes such a write
fail as a write on a closed socket does. The setting was off by default when this was written. The
tests commit of the close cells turned the default on, after a comparison found no existing live-write
cell changed by it (the record, "Phase 4, first part", "T4.5a: the comparison").

Two facts about the fixture were found while measuring, and each binds the cells below (the record,
"Fixture facts found while measuring"):

- **A default-mode cell with a non-zero heartbeat interval feeds the peer's Logon on the session's own
  executor.** The fixture's `open_to_active()` feeds it on the fixture's executor, and the Logon arm
  spawns the liveness loop on the executor the arm runs on. In the default mode over the bare
  `io_context` the loop then runs off the session's strand. With a zero interval the loop returns at
  once and nothing shows. Measured in the early reading of P-10 (`p0b_runs.output.txt`, under its heading):
  with the Logon fed by `open_to_active()`, the loop's emit ran inside a `toApp` that polls; with the
  Logon fed on the session's executor, it did not. NC-11's `onLogon` arm on a persistent store and that
  arm's control are such cells. No helper for this feed is on the branch.
- **A live-transport cell whose subject is a write that can follow a `close()` uses the double that
  refuses a write after `close()`.** With the setting off, the double's `close()` changes nothing that
  a later write sees, and such a write counts as transmitted (P-1a). The third arm of
  `ToAppStartsClose_Terminal` is such a cell.

One addition is to another file's fixture: `ValidateGateFixture`
(`tests/session/test_validate_gate_inbound.cpp`) has no `Application` today, and NC-19's validate arm needs
one attached. The arm is written in that file, where the fixture is defined.

Death arms use gtest's death tests, which the session tests already use. Where an arm starts threads, it
starts them inside the death statement, and the TSan arm of NC-9 runs in the `threadsafe` death-test style
(`tests/dictionary/orchestra_loader_test.cpp` sets it the same way), so that the child is a fresh process.

**The arms every close cell has.** Round 1 found that the close cells on the branch cannot tell a correct
R-4 from a wrong one. The inbound ones record and print, and assert that the callback ran and that
`on_inbound_frame` completed (`run_callback_starts_close` says of itself "little is asserted"). The
outbound ones pass on a property of the fixture: its config-time sink transmits whatever state the session
is in. So every close cell, existing or new, runs in these arms unless its row fixes the transport or
the store. The close cells are, on the branch, `ToAppStartsClose_*`, `ToAdminStartsClose_*`,
`FromAppStartsClose_*`, `FromAdminStartsClose_Terminal` and `FromAppSendsReply_NestedToAppThrows*`, and,
among the new cells, NC-5, NC-6, NC-7, NC-8, NC-11, NC-12 and NC-13. NC-16's second arm starts a close
too, but it is a death arm and asserts only its death. The arms:

- **Transport:** the synchronous sink (`TransportKind::sync_capture`) and the live transport whose write
  suspends (`TransportKind::live_suspending_write`, the fixture's `SuspendingWriteTransport`).
- **Store:** `MemoryStore` and `FileStore`, and no store where the cell's subject allows it.

And in every arm each of these is an assertion, not a printed line:

- inside the callback, after the start: the session's FSM state and lifecycle, and that neither `onLogout`
  nor a Logout's `toAdmin` has run;
- the order of: the callback's return, `onLogout`, each transmit, the completion of the close, and the
  completion of the operation that called the callback;
- the session's FSM state at each transmit;
- what was transmitted, what the store retained, and the result of each operation.

The fixture sets `heartbeat_interval` to zero, so its liveness loop returns at once and a terminal close in
these cells has no suspension of its own; NC-11's `onLogon` arm on a persistent store sets the interval
through the knob, for the reason its row gives. Under the Engine it has several. NC-13 is the cell where the
transport and the liveness loop are real.

**Predictions to measure first.** Each row is an outcome this note states from reading. What has been
measured was measured on 2026-10-11, or on 2026-10-12 where a bullet says so, and the evidence is in the
parent repo's `research/G19-fix-fpml-iso20022/research/probes/b41_563_564/`:

- **P-4 and P-11.** Both agree. Their rows name their evidence files.
- **P-1 for a close started in `toApp`, P-1a, P-1b, P-1c, P-2, P-3, P-5 and P-6,** on today's code, each
  with its start wrapped in `asio::post`. None disagrees. The temporary cells are
  `p0b_measurements.patch`, which applies on the fixture commit `be46718a`. Every run is in
  `p0b_runs.output.txt`, under a heading that names the prediction. The cells ran on `linux-clang-release`
  and again on `linux-clang-asan`. Each of these rows has a "Measured 2026-10-11" statement in its third
  column, and those files are its evidence.
- **P-10, an early reading,** with a plain post in place of each continuation. It agrees, and its
  row says what it covered. The row's own measurement, on the mechanism, is in the three bullets below.
- **P-7, and P-10 at the guard** (§2.2), on a scratch build of the guard. Both agree. The mechanism and
  the cells are `p2a_scratch_mechanism.patch`; the edit that makes the token a plain member is
  `p2a_plain_token_mutant.patch`. Every run is in `p2a_runs.output.txt`, under a heading that names the
  prediction. P-7 ran on `linux-clang-tsan`, P-10 on `linux-clang-asan`.
- **P-10 at the send's deferred path,** on 2026-10-12, on a scratch build of that path. It agrees. The
  mechanism is `p3a_scratch_mechanism.patch` and the cells are `p3a_measurement.patch`. Every run is in
  `p3a_runs.output.txt`. The cells ran on `linux-clang-asan` and on `linux-clang-release`.
- **P-1 for a close started in `toAdmin`, P-8, P-9, and P-10 at the close's wait,** on 2026-10-12, on a
  scratch build of the wait and of the deferred send's step 5. None disagrees. The mechanism is
  `ph4a_scratch_wait_v2.patch` and `ph4a_scratch_step5_v2.patch` (the record says how they differ from
  the two patches without `_v2`), and the cells are `ph4a_measurement.patch`. Every run is in
  `ph4a_runs.output.txt`, under a heading that names the prediction. The cells ran on
  `linux-clang-release` and on `linux-clang-asan`.

Every row of the table has a measurement, and each row's third column has a "Measured" statement with
its date and its evidence file. The measurements of the last three bullets ran in a scratch copy of the
tree, and not in the pull request's.

A row measured with the start posted keeps its "If it disagrees" column. The post stands in for the
mechanism, and the same outcome was read again on the mechanism itself once it was implemented,
through the cells' own assertions (the record's Phase 3 and Phase 4 parts have each reading). The
column applied to that reading as well.

| Id | Predicted (read; not run unless the row says "Measured") | Measurement | If it disagrees |
|---|---|---|---|
| P-1 | A terminal close started in `toApp`, with the start posted. On the synchronous sink: the outer frame is transmitted after `onLogout`, with the session `Disconnected` at the transmit; it is retained; its send returns ok. On the live transport: the outer frame is retained under its number and not transmitted; its send returns `dispatch_aborted`. | `CallbackSend.ToAppStartsClose_Terminal` with the close's `co_spawn` wrapped in `asio::post`; read the session state at the transmit event. Then the same with `TransportKind::live_suspending_write`. For NC-6, the same measurement with the close started in `toAdmin`, at a callback-first site and at an assign-first one. **Measured 2026-10-11, for a close started in `toApp`: agrees** (`p0b_runs.output.txt`, heading "T0.5 (P-1)"; cells in `p0b_measurements.patch`). Synchronous sink: the outer frame is transmitted after `onLogout`, with the session `Disconnected`; it is retained; its send returns ok. Live transport: nothing is transmitted, the frame is retained under its number, and its send returns `dispatch_aborted`. Both hold on a `MemoryStore` and on a `FileStore`. Read at the store, through a recording wrapper over the `MemoryStore`: the outer frame's store call begins before the posted close runs and completes after it. **Measured 2026-10-12, for a close started in `toAdmin`: agrees** (`ph4a_runs.output.txt`, heading "T4.3a (P-1 for a terminal close started in toAdmin)"; cells in `ph4a_measurement.patch`). It was made on a scratch build of the wait, at a callback-first site (the Heartbeat reply) and at an assign-first one (the Reject), on a `MemoryStore` and on a `FileStore`. Synchronous sink: the outer admin frame is transmitted after `onLogout`, with the session `Disconnected` at the transmit; it is retained. Live transport, with the transport double's refusal of a write after `close()` off and on: the frame is retained under its number and not transmitted. Two readings this row did not predict. The result of the `on_inbound_frame` that called `toAdmin`: ok at both sites on the synchronous sink; on the live transport, `dispatch_aborted` at the callback-first site and ok at the assign-first one. Store against manager: the outbound numbers agree in every arm; on a `FileStore` the inbound numbers agree, except at the callback-first site on the live transport, where the store is one behind. On a `MemoryStore` the store's inbound reading does not move, so it is no check there. The `toApp` half and P-1a were read again on the mechanism in the same runs, and came out as they were measured with the start posted. | The row for that cell, §2.1a's "An outbound emit can be cut" and the B&L limitation are rewritten to what was measured, and the difference is reported to the owner before the cell is written, because R-4's reading was chosen on this cost. |
| P-1a | The same close with `reset_on_disconnect` set. The close's teardown reset and the outer frame's store call are then both in flight. Their order was not read to a conclusion. | The live-transport arm of P-1 with the knob set; read what the store holds and what the send returns. **Measured 2026-10-11** (`p0b_runs.output.txt`, heading "T0.6 (P-1a)"; cells in `p0b_measurements.patch`). The close is suspended in its teardown reset before it has drained the write gate, so the outer frame's write passes the gate and reaches a transport whose `close()` has already been called. In the arm whose transport double refuses a write after `close()`: nothing is transmitted, and the send returns `dispatch_aborted`; read at the end, the store holds no frame and both next numbers are at their initial value, in the manager and in the store, on a `MemoryStore` and on a `FileStore`. In the arm whose double's `close()` does nothing, the frame is reported as transmitted and the send returns ok. That is an artifact of the double, and no assertion is written from it. | Nothing to disagree with: the arm's assertions are written from the measurement, and the B&L limitation states the outcome. |
| P-1b | A graceful close started in `toApp` or `toAdmin`, on the live transport: the outer frame, then the Logout; both retained; both operations ok. (On the synchronous sink this was measured with the start posted.) | The existing graceful cells with the start posted and the live transport attached. **Measured 2026-10-11: agrees** (`p0b_runs.output.txt`, heading "T0.7 (P-1b)"; cells in `p0b_measurements.patch`), for a close started in `toApp` and in `toAdmin`, at a callback-first site and at an assign-first one: the outer frame, then the Logout, both retained, both operations ok. On a `MemoryStore` the Logout's `toAdmin` runs before the outer frame is transmitted, while the outer emit is suspended in its store call; the wire order is still the outer frame, then the Logout. | As P-1. |
| P-1c | A terminal close started in `fromAdmin` on a TestRequest. The Heartbeat reply's `toAdmin` runs before the close, on an Active session, with either store kind. On the synchronous sink the Heartbeat is transmitted after `onLogout`; on the live transport it is retained and not transmitted. In the live arm `on_inbound_frame` returns `dispatch_aborted`, and on a persistent store the inbound number is then not persisted; reading had not settled this, and it is stated from the measurement in the next column. | `FromAdminStartsClose_Terminal` with the start posted, in both transport arms. **Measured 2026-10-11: agrees** (`p0b_runs.output.txt`, heading "T0.8 (P-1c)"; cells in `p0b_measurements.patch`). The Heartbeat's `toAdmin` runs before the close, on an Active session, on a `MemoryStore` and on a `FileStore`. Live arm: nothing is transmitted, the Heartbeat is retained, and `on_inbound_frame` returns `dispatch_aborted`; on a `FileStore` the manager's next inbound number has advanced and the store's has not. | As P-1; `on_inbound_frame`'s result is asserted as measured. |
| P-2 | A terminal close started in `fromApp` on a FileStore: the close's completion still precedes `on_inbound_frame`'s; `on_inbound_frame` returns ok; the store has recorded the inbound number. | `FromAppStartsClose_Terminal_FileStore` with the start posted. **Measured 2026-10-11: agrees** (`p0b_runs.output.txt`, heading "T0.9 (P-2)"; cells in `p0b_measurements.patch`): the close completes before `on_inbound_frame` does, `on_inbound_frame` returns ok, and the store has recorded the inbound number. | §2.5's "Left" bullet and the cell's order assertion are rewritten to what was measured. |
| P-3 | Two application frames delivered with no yield between them, as `run_read_pump` delivers the frames of one read; a terminal close started in the first `fromApp`; `MemoryStore`. The second `fromApp` runs before `onLogout`. | NC-12's setup with the start posted. **Measured 2026-10-11: agrees** (`p0b_runs.output.txt`, heading "T0.10 (P-3)"; cells in `p0b_measurements.patch`): on a `MemoryStore` the second `fromApp` runs before `onLogout`. The same setup on a `FileStore`, which this row did not predict: the second `fromApp` does not run. The arm suspends in its persist, the close runs there, and the second frame's `on_inbound_frame` returns ok on the closed session. So the bullet and its B&L sentence hold on a volatile store only, and both are scoped to that. | If the second `fromApp` does not run, the "close can be late" bullet of §2.1a and its B&L sentence are deleted, and NC-12 asserts the measured order. |
| P-4 | asio's sequence at a post. A coroutine awaits `asio::post(ex, asio::deferred)`; a terminal cancellation is emitted on its slot while it waits. The exception surfaces at the coroutine's next await, not at the post. In a second arm the coroutine resets its cancellation state to `asio::disable_cancellation{}` before the post, as `close()` does; a terminal cancellation emitted while it waits surfaces at no later await. In a third arm the reset follows the post; no exception surfaces there either (the placement §3 lists as not witnessed). | A standalone probe beside p1 in the parent repo's `research/probes/b41_563_564/`, built against the pinned asio. **Measured 2026-10-11: agrees.** The probe is `p4_post_cancellation.cpp`, built by `build_p4.sh`; its output is `p4_post_cancellation.output.txt`. First arm (the probe's arms A, whose second await is a post, a timer wait or a nested awaitable): no exception at the post, the record reads `terminal` after it, and the second await throws `operation_aborted`. Second arm (the probe's arm S1): nothing is recorded and no await throws. Third arm (the probe's arms B, a reset to the default filter and a reset to total): the reset clears the record and no await throws. The positive control (a cancellation recorded before the first post throws at that post) and the null control (no emission: nothing recorded, nothing thrown) pass. The probe's other arms (C, D, E, S2, S3, S4) measured §2.1's statements about the filter, about `throw_if_cancelled`, about a chain that awaits a spawned chain, and about a recorded cancellation that a later emission erases; each agrees. | §2.1's "Cancellation at the hop" and §2.1a's "Cancellation and the wait" are wrong. The note returns to Gate A before either wait is implemented. |
| P-5 | The lifecycle dispositions of §2.1a: a close started in `onLogon`, in `onLogout` and in `onCreate`. | NC-11's setups with the start posted. **Measured 2026-10-11: agrees in every arm, and every arm ends closed** (`p0b_runs.output.txt`, heading "T0.11 (P-5)"; cells in `p0b_measurements.patch`). The routes that were run: `onLogon`, in both roles, both close modes and both store kinds; `onLogout` fired by a graceful close, with a terminal close started in it; `onLogout` fired by a `Disconnected` write of `Session::send`, whose `toApp` returned `dispatch_aborted`, with a close of each mode started in it; `onCreate`, in both close modes. The route NC-11 names for the `Disconnected` write outside `close()`, a frame with another BeginString, is not the one that was run then; it was measured when the cell was written (the record, "Phase 4, first part", the last row of its table). On a raw initiator the Logon is transmitted before `onCreate` runs, so a close started there ends the session after a Logon with no Logout. | §2.1a's bullet for that callback and NC-11's assertions are rewritten to what was measured. A disposition that leaves a session open after a close was asked for is reported to the owner. |
| P-6 | `B35SendSlot.Nested_ASendInsideToAppLeavesTheOuterFrameIntact` after R-1: the second send is deferred, starts while the outer send is suspended in its `MemoryStore` call, and takes `send_fallback_leaf_`. | That cell with its nested `co_spawn` wrapped in `asio::post`. **Measured 2026-10-11: agrees** (`p0b_runs.output.txt`, heading "T0.12 (P-6)"; cell in `p0b_measurements.patch`): the second send is deferred, starts while the outer send is suspended in its store call, and takes the fallback leaf's buffer. | NC-17's assertions follow the measurement; the cell's subject (the outer frame's bytes are what `toApp` saw) does not depend on which buffer the second send takes. |
| P-7 | NC-9's arm (d) under TSan. With the atomic token: the second thread's entry dies at the token's assertion and no ThreadSanitizer report is printed. With the token built as a plain member: ThreadSanitizer prints a data-race report on the token before the assertion fires. The second half rests on ThreadSanitizer's happens-before model as known, not on a run: a relaxed atomic load and store create no ordering between the two threads, where a mutex, a condition variable or a non-relaxed atomic would. | Arm (d) on `linux-clang-tsan`, once on the mechanism and once on a scratch copy with the token made plain. **Measured 2026-10-11: agrees, in both halves** (`p2a_runs.output.txt`, headings "T2.2 (P-7), first half" and "T2.2 (P-7), second half"; mechanism and cells in `p2a_scratch_mechanism.patch`, the plain token in `p2a_plain_token_mutant.patch`). It was made on a scratch build of §2.2's guard. With the atomic token the second thread's entry dies at the token's assertion and no ThreadSanitizer report is printed. With the token built as a plain member ThreadSanitizer prints a data-race report whose frames are the token's compare-and-swap. Each half was run with no `TSAN_OPTIONS` and with the `ctest` environment. On the plain token under `halt_on_error=1` the report ends the process before the assertion, so arm (d) fails there by both clauses of its matcher; with no options it fails by the second clause. | If the plain token draws no report, arm (d) cannot see the difference: it is deleted, its mutant row moves to the list of mutants with no witness, and the token's atomicity is recorded in the verification record as a property carried by reading the diff. If the atomic token draws a report, the two threads share more than the token, or a foreign thread touches a plain member before it asserts; that is a defect in §2.2's design and is fixed there. |
| P-8 | NC-8. Arm 1: a terminal close on the cell fixture, with no send in flight and `reset_on_disconnect` off, completes without suspending, so it has reached `closed_drained` before the waiting close resumes. Read through the places it could suspend: the default `close_async` returns `close()`'s result; both drains post only while the mutex has a holder, a waiter or a resumption in flight; the liveness wait posts only while the loop is alive, and with a zero heartbeat interval it is not. Arm 2: a graceful close that finds another graceful close in flight joins it, and the scripted flush hook has run once when both complete. | The two arms on a scratch build of the wait. **Measured 2026-10-12: agrees, in both arms** (`ph4a_runs.output.txt`, heading "T4.1 (P-8)"; cells in `ph4a_measurement.patch`). Arm 1: the lifecycle reads `closed_drained` at the return of the `co_spawn` that starts the other close, and at the waiting close's resumption; the callback's close returns `session_already_closed`; `onLogout` runs once. The observable is the lifecycle at that return, and not whether the other close's completion handler has run by the resumption: `co_spawn` posts the completion handler whatever the close did, so its position reads asio and not the close. The cell's first clause read the handler, which had not run, and printed a disagreement; the clause was changed to read the lifecycle, with the mechanism and the prediction unchanged, and the first run's lines are kept in the evidence file under the same heading. Arm 2: the lifecycle reads `closing` at the resumption; both closes return ok; the flush hook ran once; one Logout is transmitted. | Arm 1: if the first close does suspend, the waiting close joins it; the arm's assertion is rewritten to arm 2's oracle, and the mutant row names arm 2 alone. Arm 2: the assertion follows the measurement; if a second run of the body cannot be told from a join there either, the mutant has no witness and is listed as such. |
| P-9 | NC-5's two arms added at round 2. (e) A graceful `close()` and then a send started in one `fromApp`, `MemoryStore`: the send nests, its frame is transmitted, then the Logout, both retained, both ok. (f) A logout the peer starts; a send started in the confirming Logout's `toAdmin`: it is deferred, passes step 5 and the Active check, and is numbered and transmitted after our Logout on the synchronous sink. What that send returns was not read to a conclusion. | The two arms on a scratch build of R-1 and R-4. **Measured 2026-10-12: agrees, in (e) and in (f)** (`ph4a_runs.output.txt`, heading "T4.2 (P-9)"; cells in `ph4a_measurement.patch`). (e): the send's `toApp` runs inside `fromApp`; on the wire the send's frame, then the Logout; both retained; both ok. (f): the send is deferred; on the wire our Logout, then the send's frame; both retained. What the send of (f) returns, which this row did not predict: ok, on a `MemoryStore` and on a `FileStore`. On a `MemoryStore` its frame goes out after `onLogout`, with the session `Disconnected`; on a `FileStore` it goes out before `onLogout`, with the session still Active. | (e): the arm asserts the measured order, and §2.1a's "the close never runs inside the callback" is re-read against it. (f): §7's bullet and the B&L sentence are rewritten to what was measured; if the send is refused, the bullet is deleted. |
| P-10 | The setups of NC-16, NC-19 and NC-20. On the `direct_executor` fixture a callback's `poll` runs the deferred send's continuation (first arm), the waiting close's (second arm) and, after the callback has advanced the mock clock by the heartbeat interval, the liveness loop's emit (NC-20), each inside the callback. On a session in the default mode that the fixture drives from a strand of its own (`ExecKind::strand`), over which the session's strand is a second one, the same callback's `poll` does not run the continuation (NC-16's fourth arm). A frame fed from `onLogon` with the expected inbound number reaches the inbound overload (NC-19's `onLogon` arm). A frame with another BeginString, fed from `toApp` on an Active session, reaches the `onLogout` site with no parse over the buffer and no emit before it (NC-19's lifecycle arm). | Those arms on a scratch build, with a recorded line at the point each continuation runs. **Early reading, 2026-10-11: agrees** (`p0b_runs.output.txt`, heading "T0.13 (early reading of P-10)"; cells in `p0b_measurements.patch`). It was taken on today's code, with a plain `asio::post` onto the session's executor in place of each continuation. On the `direct_executor` fixture a callback's `poll` runs the posted handler inside the callback. In the default mode the handler runs after the callback returns, while a handler posted onto the bare `io_context` runs inside it; that last arm is the positive control. The row's own measurement was then made on the mechanism, in three places, each on a scratch build. **Measured 2026-10-11, at the guard: agrees** (`p2a_runs.output.txt`, heading "T2.3 (P-10, the halves of Phase 2)"; mechanism and cells in `p2a_scratch_mechanism.patch`), on `linux-clang-asan`. NC-20: on the `direct_executor` fixture, after `toApp` advances the mock clock by the heartbeat interval and polls, the liveness loop's `toAdmin` enters with `toApp` on the stack, and the process dies at the stack-arena overload's assertion; in the control, on the default strand fixture, the liveness `toAdmin` runs after `toApp` has returned. NC-19's `onLogon` arm: the fed frame reaches the inbound overload and dies at the parse-site assertion, in both roles. NC-19's lifecycle arm: the frame dies at the `onLogout` site's assertion, with no `fromApp`, `toAdmin` or transmit line between the feed and the abort. **Measured 2026-10-12, at the send's deferred path: agrees** (`p3a_runs.output.txt`, heading "T3.2 (P-10, the Phase 3 halves)"; mechanism in `p3a_scratch_mechanism.patch`, cells in `p3a_measurement.patch`), on `linux-clang-asan` and on `linux-clang-release`. On the `direct_executor` fixture the callback's `poll` runs the deferred send's continuation inside the callback: on the build with assertions the process dies at the deferred path's assertion, and on the build without them the send nests. In the default mode over a fixture strand the continuation runs after the callback has returned, whether the outer send was started on the session's executor or on the fixture's strand. A handler posted onto the bare `io_context` ran inside the callback in every run; it is the positive control of the poll. **Measured 2026-10-12, at the close's wait: agrees** (`ph4a_runs.output.txt`, heading "T4.3 (P-10, this phase's half)"; cells in `ph4a_measurement.patch`), on the same two presets. On the `direct_executor` fixture the callback's `poll` runs the waiting close's continuation with `fromApp` on the stack: on the build with assertions the process dies at the close's assertion, and on the build without them the close runs to `closed_drained` and `onLogout` fires inside `fromApp`. | If the continuation does not run inside the callback on the `direct_executor` fixture, the fixture is not the exposed setup and the arms are rebuilt before any death is trusted. If it does run inside the callback on the strand fixture, a strand did not hold back a post made under one of its handlers, the property §2.1's statement also rests on; R-7 was ruled on that statement, so the note returns to the owner. If the lifecycle arm's frame aborts or is disposed of before the `onLogout` site, the arm is rebuilt on a frame that reaches it; if none does, the lifecycle sites' assertion joins the list of mechanisms without a witness. |
| P-11 | OD-6's withdrawal (§2.4). On the base tree, one `Engine::send` parked between its admission re-check and its control-strand `co_spawn` while `stop()` runs its step 1 on the session's strand, then released: it returns `session_invalid_state_for_send`, and nothing of it is transmitted. | A hook at that point in a scratch copy of the base tree (`22702dc9`); the hook is not part of the PR. One send is parked there, `stop()` is run until the engine-stop flag is set on the session, and the send is released. **Measured 2026-10-11: agrees.** The hook and its cells are `p11_hook.patch`, applied to the tree at `53b365c4`, whose `src/` and `include/` are the base tree's (re-derive: `git diff --stat 22702dc9 53b365c4 -- src include`); the runs are `p11_runs.output.txt`. A send released after `stop()`'s step 1 had set the flag on its session returned `session_invalid_state_for_send`: its `toApp` did not run, the outbound number did not move, the store retained nothing of it and the peer received nothing. A send released when `stop()` had reached its send drain had the same outcome. Refusal site, in both: the control-strand read of `stopped_`. Control: a send released before `stop()` was started was sent, retained and received. The session's FSM state read Active at every reading, the one at the seam after `stop()`'s send drain included (§2.4). | If the send is transmitted, §2.4's reading is wrong and a send that succeeds today would be refused by OD-3. The C-ABI question returns to the owner before the PR proceeds. |

**Cells that must turn** (they exist on the branch). The transport and store arms above apply to every
row whose cells start a close, unless the row fixes the transport or the store.

| Cells | Must assert |
|---|---|
| `CallbackSend.ToAppSendsNested*`, `ToAdminStartsSend`, `ToAdminStartsSend_AssignFirstSite`, `…_FileStore`; `EngineCallbackSend.EngineSendFromToApp*` | deferred; distinct consecutive numbers in transmit order; each frame retained under its own number; store and manager agree; both outcomes ok |
| `CallbackSend.FromAppSendsReply`; `EngineCallbackSend.EngineSendFromFromApp*` | nested; pass on the debug build too |
| `EngineCallbackSend.MtEngineSendFromFromApp_Quiet`, `…_BusyControlStrand` | every reply nested, in both arms. **On a build with assertions, a run of `…_BusyControlStrand` counts only with its tally.** Measured 2026-10-11 on today's code (`p0b_runs.output.txt`, the runs of this cell on `linux-clang-asan`): the cell passed in some runs, each with every reply deferred, and aborted at the guard's assertion in the others, before it printed its tally; that assertion fires when a reply's `toApp` enters inside `fromApp`. So a pass there is not evidence that any reply nested. The cell prints, for the replies it drove, how many were nested and how many were deferred. A run counts as GREEN only when that line shows every driven reply nested and none deferred, and the line is recorded with the result. A run counts as RED, for the state before R-2 and for the mutant "the hit path replaced by the control-strand hop", only when the line shows a deferred reply. A run that aborts before it prints the line counts for neither and is run again |
| the `…_NoStore` and `…_NoStore_LiveWrite` nested cells | deferred; distinct consecutive numbers in transmit order |
| `CallbackSend.ToAppStartsClose_Graceful`, `ToAppStartsClose_Graceful_FileStore`, `ToAdminStartsClose_Graceful`, `ToAdminStartsClose_Graceful_AssignFirstSite`, `FromAppStartsClose_Graceful` | inside the callback after the start: Active, lifecycle open, the Logout's `toAdmin` not run. After it: the outer frame first where there is one, then the Logout; distinct consecutive numbers; both retained where there is a store; both operations ok. Live-transport arm: P-1b. Pass on the debug build too |
| `CallbackSend.FromAppStartsClose_Terminal`, `FromAppStartsClose_Terminal_FileStore` | inside `fromApp` after the start: Active, lifecycle open, `onLogout` not run. `onLogout` after `fromApp` returned. The session `closed_drained` at the end and the close's result ok. `MemoryStore`: `on_inbound_frame` completes before `onLogout`. FileStore: the order and results of P-2. Pass on the debug build too |
| `CallbackSend.FromAdminStartsClose_Terminal` | inside `fromAdmin` after the start: as the row above. The Heartbeat reply's `toAdmin` runs on an Active session, on either store kind: the TestRequest sub-arm emits its reply before it persists the inbound advance, so nothing suspends between `fromAdmin`'s return and that `toAdmin`. Then per transport arm: P-1c |
| `CallbackSend.ToAppStartsClose_Terminal` | inside `toApp` after the start: Active, lifecycle open, `onLogout` not run. Then per transport arm: P-1. The synchronous-sink arm asserts the FSM state at the transmit, so that it cannot pass for a reason that does not hold on a live transport. A third arm, the live transport with `reset_on_disconnect` set, asserts what P-1a measured; it uses the transport double that refuses a write after `close()` ("Fixture additions the cells need") |
| `CallbackSend.FromAppSendsReply_NestedToAppThrows`, `…_FileStore`, `…_FromAppRejects` | inside `fromApp` after the start: Active, lifecycle open, the send not completed. `onLogout` after `fromApp` returned. The send returns `app_callback_threw`. The session `closed_drained` at the end. `…_FromAppRejects`: the BusinessMessageReject's callback runs on an Active session, before the close, on either store kind (that sub-arm also emits before it persists); whether its frame is transmitted follows P-1's two arms |
| every control (`Sequential*`, `OverlappingSends*`, `TestRequestThenSend`, `ToAdminStartsSend_AssignFirstSite_Sequential`) | unchanged |

The cells' names and their "expected classification" asserts are re-decided when they are lifted: a cell
named `…Nested` that must report "deferred" is renamed.

**New cells.** The ids are for this note's tables; the cells get their names when they are written.

| Id | Drives | Asserts |
|---|---|---|
| NC-1 | `Engine::send` from `onLogon`, initiator and acceptor | nested, and the send succeeds. Pins §2.3's premise that a session is published before it can be Active |
| NC-2 | `Session::send` from `onLogon` and from `fromAdmin`. The admin message of the `fromAdmin` arm is the implementer's choice; what constrains it is that the session is still Active when its `fromAdmin` runs and stays so, as with the Heartbeat of NC-19's validate arm or the TestRequest of `FromAdminStartsClose_Terminal` | nested |
| NC-3 | a send to session B from a callback of session A, A's `toApp` included, on one io thread with B's strand idle. **Route:** `Engine::send` for B, or a `co_spawn` of B's `send` onto B's executor. Which of the two is the implementer's choice; what constrains it is the cell's fixture (a fixture of raw sessions has no Engine) and that B's send runs on B's executor. Not a direct `co_await` of B's `send` from A's strand, which would run B's send outside B's serialisation domain | B's send is not deferred: its `toApp` runs before A's callback returns |
| NC-4 | a deferred send cancelled while it waits. **Route:** `Session::send` started in `toApp` by a `co_spawn` onto the session's executor whose completion token is bound to a cancellation slot the cell owns (`asio::bind_cancellation_slot`). A detached spawn has no slot, and the cell would measure its own setup. **Emission:** `terminal`, which a plain `co_spawn` chain records, emitted from inside `toApp` after the `co_spawn` has returned, so the send is waiting on its post. Second arm: a `total` emission on the same chain. Third arm: the route through `Engine::send`, started the same way, with a `terminal` emission. Fourth arm: that route with a `total` emission. Fifth arm: the first arm's route with the send awaited from a wrapper coroutine that first turns `throw_if_cancelled` off, the slot bound to the wrapper's `co_spawn`, and a `terminal` emission | first and third arms: the send returns the OD-1 error, the session stays Active, nothing of it is transmitted, no number is consumed. Second and fourth arms: the chain's terminal-only filter drops the emission (in the fourth, at `Engine::send`'s outer frame, before the session-strand body's filter is consulted), and the send completes ok. These two show that the other arms' result comes from a recorded cancellation and not from the setup. Fifth arm: the cancellation is recorded and the send is not refused at the hop: its own `toApp` runs. What it returns after that is asserted as measured |
| NC-5 | (a) to (c): a `close()` and a send started in one `toApp`, in three orders: terminal close then send; graceful close then send; send then graceful close. (d) A send started in the graceful Logout's `toAdmin`. (e) A graceful `close()` and then a send started in one `fromApp`. (f) A logout the peer starts, with a send started in the confirming Logout's `toAdmin` | (a), (b), (d): the send returns `session_invalid_state_for_send`, nothing of it is transmitted, no number is consumed, and after a graceful close the Logout is the last frame transmitted. (c): the send's frame, then the Logout. (e): the close has not begun, so the send is admitted and nests: its `toApp` runs inside `fromApp`; then the order of P-9. (f): the order of P-9; this arm pins a disclosed limitation (§7), not a wanted behaviour |
| NC-6 | a terminal close started in `toAdmin`, at a callback-first site and at an assign-first one | inside `toAdmin` after the start: as `ToAppStartsClose_Terminal` inside `toApp`. Then per transport arm, as that cell: P-1, for the outer admin frame. NC-6's outer operation is `on_inbound_frame`, not a send, so the result of the operation that called `toAdmin`, and whether the store and the manager agree, are asserted as measured (P-1's measurement, run at both sites) |
| NC-7 | a close that waits, in each mode, cancelled meanwhile. The close is started in a callback by a `co_spawn` bound to a slot the cell owns; `terminal` and, in a second arm, `total` are emitted from inside the callback after the `co_spawn` has returned. In the graceful mode the cell ends the close's wait for the peer's Logout as NC-8's second arm does, by advancing the mock clock past `logout_disconnect_timeout_ms` | the session reaches `closed_drained` and the close's result is ok, in both arms |
| NC-8 | a close that waits while another close runs first. Before it runs the `io_context`, the cell queues the delivery of the frame and, behind it, a handler that starts the other close. That handler is then ahead of the post that the callback's close makes. **Arm 1:** the other close is terminal, on the synchronous sink, with no send in flight and `reset_on_disconnect` off, so that it cannot suspend (P-8); the callback's close is terminal. **Arm 2:** both closes are graceful, and the cell installs a counting hook with `set_close_flush_hook`. It ends the first close's wait for the peer's Logout by advancing the mock clock past `logout_disconnect_timeout_ms`, which ends `run_logout_phase1`'s sleep on the session's clock | Arm 1: the callback's close returns exactly `session_already_closed`; `onLogout` runs once; the lifecycle ends `closed_drained`. Arm 2: the callback's close joins the first: both return the same result, the Logout is transmitted once, and the hook has run once |
| NC-9 | the guard, through test access to the scope object, so that the guard's state is the only session state the cell's threads share. Builds with assertions only. (a) A first thread enters one session's scope and stays inside; a second thread enters after the first has told it so through an ordinary handshake (a promise or a condition variable). (b) One thread enters, enters a nested scope and leaves it; a second thread then enters while the outer scope is live. (c) One thread enters a nested scope, leaves both, and enters again. (d) On `linux-clang-tsan`: as (a), but the only ordering between the two threads is a `std::atomic<bool>` that the first stores and the second reads with `memory_order_relaxed`. The first thread stays inside the scope until the process dies. The main thread starts both and then joins them, so the death statement does not return first; a join orders nothing between the two threads | (a) and (b): death at the token's assertion. (c): no assertion; a control. (d): the death's output is matched by a gmock matcher that requires the token assertion's text and the absence of the string `ThreadSanitizer` (`AllOf(HasSubstr(…), Not(HasSubstr("ThreadSanitizer")))`); P-7. **What (a) proves and does not prove:** it proves that a second thread's entry while a first is inside is detected. It does not prove that the detection is free of a data race: the handshake orders the first thread's write before the second thread's read, for ThreadSanitizer as for the hardware, so a token built as a plain member passes (a) on every preset. Arm (d) is the only witness of the token's atomicity |
| NC-10 | the OD-3 check. The cell sets the session's engine-stop flag on the session's strand through `session_engine_access::note_engine_stop`, and then calls `Engine::send` for that session. This is a construction: the order it stands for (a send between its admission re-check and its spawn while `stop()` runs its first step) cannot be produced on demand, because no test hook sits between those two statements. The real interleaving is not driven. The cell calls C++ `Engine::send`, and no C-ABI twin is added: with OD-6 withdrawn the check changes nothing a C-ABI consumer can observe (§2.4), so there is no C-ABI effect to witness | the send returns `session_invalid_state_for_send` and nothing is transmitted |
| NC-11 | a close started in each lifecycle callback. `onLogon`: both roles, both close modes, a persistent and a volatile store; the arm on a persistent store sets a non-zero heartbeat interval through the fixture's knob and feeds the peer's Logon on the session's own executor ("Fixture additions the cells need"), and it has a positive-control arm, the same setup with no close started in `onLogon`. `onLogout`: fired by a graceful close, with a terminal close started in it; and fired by a `Disconnected` write outside `close()`, with a close started in it. **Route of that write:** with the validate gate off, the test body delivers to the Active session a frame whose BeginString is not the session's. The Active arm writes `Disconnected` at its BeginString test, with no `close()` and no emit; it is the frame NC-19's lifecycle arm feeds from `toApp`. `onCreate`: both close modes | inside the callback after the start: the state and lifecycle the callback entered with. Then, per callback, the disposition of §2.1a (P-5): for `onLogon` on a persistent store, no liveness loop is started and `on_inbound_frame` returns ok. The observable for "no liveness loop" is a count of `sleep_until` calls in flight, kept by a clock wrapper of the cells' fixture over `fixpp::core::mock_clock` (which has no such count; `inflight_count()` is a member of `system_clock_source`), read as `expect_close_from_on_logon_starts_no_liveness` in `tests/session/test_session_plaintext_roundtrip.cpp` reads its clock's: with a non-zero heartbeat interval a started loop parks a sleep while the session is Active, and the arm asserts that none is parked. A loop started on a session that has already left Active ends at its first test of the state and parks nothing, so the reading tells "not started" from "started" only where the session is still Active when the Logon arm resumes; elsewhere the assertion is kept and claims no more than that no sleep is parked. That reading is a zero, so it counts only beside its positive control: in the same setup with no close started, the count of parked sleeps is non-zero while the session is Active. The control is run with the arm, and the arm's zero is not reported without it (added by the orchestrator, 2026-10-11; not reviewed). For `onLogout` inside a graceful close, the terminal close joins it and the grace wait ends; for `onLogout` after a `Disconnected` write outside `close()`, the close started there is the session's first: it joins no other close and does not return `session_already_closed`; for `onCreate`, `open()` returns ok and the session is closed at the end with no `onLogout` |
| NC-12 | two application frames delivered by one coroutine with no yield between them, a terminal close started in the first `fromApp`. First arm: `MemoryStore`. Second arm: `FileStore` | First arm: the order of P-3, the second `fromApp` before `onLogout`. This arm pins a disclosed limitation, not a wanted behaviour. Second arm, as P-3's measurement found it on a persistent store: the second `fromApp` does not run, and the second frame's `on_inbound_frame` returns ok on the closed session. This arm pins where the limitation stops |
| NC-13 | Engine level: two Engines over loopback TCP with a non-zero heartbeat interval. A terminal and a graceful close started in `fromApp`, and in `toApp` of an `Engine::send`. The arm for a terminal close started in `fromApp` also runs on `linux-clang-asan`; it is R-3's measurement | inside the callback after the start: Active, lifecycle open. The session's role loop ends and the Engine stops cleanly afterwards. Graceful from `toApp`: the peer receives the outer frame and then the Logout. Terminal from `toApp`: the peer does not receive the outer frame (P-1). This is the cell in which an owner (the role loop, then `stop()`) frees the Session after the close |
| NC-14 | R-6. A test-double store that holds its first `store()` call and completes the second one first. Two overlapping sends; then the same with the second send started in the first one's `toApp` | the second frame is transmitted before the first, in both shapes. This cell shows the consequence of a store that breaks the requirement, as the B&L row states it; it is not a wanted behaviour |
| NC-15 | the payload copy, on `linux-clang-asan`. `toApp` allocates a buffer on the heap, starts a detached `Session::send` over it, and frees the buffer before it returns. The buffer is on the heap because a buffer on the stack is seen only with `detect_stack_use_after_return`, and the preset sets no `ASAN_OPTIONS` | no ASan report; the transmitted frame carries the payload's bytes |
| NC-16 | the exposed setup of R-7: the fixture's `direct_executor` knob, attested over the bare `io_context` executor. A callback starts a send (first arm, from `toApp`) or a close (second arm, from `fromApp`) and then runs the `io_context` (`poll`) before it returns, so the post's continuation runs inside the callback (P-10). The first two arms run on builds with assertions only. **Third arm,** `linux-clang-release` only: the first arm's setup, with the callback's `poll` loop bounded by a count of handlers. The count is the implementer's choice; what constrains it is that a correct run, whose loop ends when `poll` runs no handler, ends well below it, and that the mutant's run, which does not end by itself, reaches it. This arm pins behaviour outside the contract. It is there only so that the mutant "step 6 defers again" can go RED, and the cell's comment says so. **Fourth arm,** a control on both build types: the first arm's callback on a session in the default mode that the fixture drives from a strand of its own (`ExecKind::strand`). The session's strand is a second strand, made over that one, so the handler that holds the callback is the outer strand's and a post onto the session's strand executes through it. The arm shows a driver on a serialising executor. It is not §2.1's case, entry points on the session's own strand | first arm: death at step 4's assertion. Second arm: death at the close's depth assertion. Third arm: the loop ends below its bound and the send has run nested, as it does today; the cell fails if the bound is hit. Fourth arm: no death; the send is deferred, its `toApp` running after the outer `toApp` returned (P-10) |
| NC-17 | `B35SendSlot.Nested_ASendInsideToAppLeavesTheOuterFrameIntact`, rewritten. Today it asserts that the send started in `toApp` reached its own `toApp` inline, and skips on a build with assertions, citing fixpp#564 | the skip is removed. The second send's `toApp` runs after the outer `toApp` returned. The bytes the outer `toApp`'s view spanned are the bytes stored and transmitted for the outer frame; that assertion is a pin of B35's property, which this PR does not introduce, and the order assertion before it is the one R-1 changes (the first mutant row). Which buffer the second send took: P-6 |
| NC-18 | the miss path during `stop()`: the twin of `V15_SendCounterEnrolledBeforeControlHop_NoUAF`. Many `Engine::send` calls for an unregistered id, and for a registered id whose session is not published, queued while `stop()` runs; then the Engine is destroyed. How many sends and how many io threads is the implementer's choice, taken from V-15, its twin; what constrains it is that the cell claims nothing about any single send. Under ASan and TSan. As in V-15, whether a given send is on the control strand's queue when `stop()` drains depends on timing across several io threads; the cell drives many sends and claims the drain for those that are, not a forced interleaving | `stop()` completes; no sanitizer report; each send returns one of the miss path's two documented errors or `session_invalid_state_for_send` |
| NC-19 | a frame fed from inside a callback, on builds with assertions. The callback starts `on_inbound_frame` by a `co_spawn` that begins inline, as the I-3 test does from `fromApp`. **Dispatch arms,** validate gate off: fed from `toApp`, from `toAdmin` and from `onLogon`, each with an application frame that carries the inbound number the session expects at that moment, so that its arm reaches dispatch. **Validate arm,** on `ValidateGateFixture` (`tests/session/test_validate_gate_inbound.cpp`) with an `Application` attached: `fromAdmin` on a conformant Heartbeat feeds a Heartbeat with a tag the dictionary does not define for it, as `UnexpectedTag_Reason2` does. The validate gate runs before the sequence check, so that frame's number does not matter. **Lifecycle arm,** validate gate off: the `toApp` of a send on an Active session feeds a frame whose BeginString is not the session's. The Active arm tests BeginString before the sequence check, writes `Disconnected` there and emits nothing, so `onLogout` is entered inside `toApp` (P-10) | dispatch and validate arms: death at the parse-site assertion. In the validate arm the nested arm never reaches the inbound overload, so the death can only come from `validate_inbound_`'s assertion. The `onLogon` arm is kept only if its nested frame reaches a parse there (P-10's measurement run records it); the lifecycle scopes' count is also witnessed by NC-11. Lifecycle arm: death at the lifecycle sites' assertion. The nested arm reaches no parse site and enters no outbound callback, so the death can only come from that assertion |
| NC-20 | an outbound callback inside an outbound callback, on builds with assertions: NC-16's `direct_executor` fixture with a non-zero heartbeat interval. A send from the test body; its `toApp` advances the mock clock by the heartbeat interval and then runs the `io_context` (`poll`), so the liveness loop wakes and begins its emit inside `toApp` (P-10) | death at the stack-arena overload's assertion |

Not carried over from v0.3: the cell for `Engine::stop()` started from a callback (R-5); the cell "a
`toAdmin` inside a `toApp`'s deferred window" (its mutant has no path, below); a separate cell for a
graceful close in `fromApp` with no store and with a FileStore, which the store arms of the graceful row
now cover. Not carried over from v0.4: NC-13's arm for a close started in `onCreate` under the Engine. An
application has no handle to start it with (§2.1a), and NC-11 covers `onCreate` on a raw `Session`.

**Arms that must go RED** (each run in a scratch copy, never in the PR's tree). Every row has the trace
from the mutated statement to the assertion that fails. A row for which no trace could be written is not
in the table; those are listed after it.

| Mutant | Fails | Trace |
|---|---|---|
| the depth test removed from the send's body, or the public `send` passing "skip" | the classification assertion of the `toApp` and `toAdmin` send cells; NC-17's order assertion | The send started in the outbound callback runs inline. On a build without assertions its own `toApp` runs with the starting callback on the stack, and the witness records "nested" where "deferred" is asserted; at a callback-first site the "distinct numbers" assertion fails too. On a build with assertions the process aborts first, at the stack-arena overload's assertion, when the nested send reaches its `toApp`. NC-17's second send is started in `toApp` in the same way, so its `toApp` runs before the outer `toApp` has returned, where "after" is asserted |
| the outbound depth counted at the inbound overload too | the classification assertion of `FromAppSendsReply` and `EngineSendFromFromApp*`; NC-2's `fromAdmin` arm; NC-5 (e) | `fromApp` raises the outbound depth, the send started there takes the deferred path, its `toApp` runs after `fromApp` returned, and the witness records "deferred" where "nested" is asserted. The inbound overload calls `fromAdmin` too, so NC-2's `fromAdmin` arm fails the same way. In NC-5 (e) the deferred send then finds the close begun and is refused at step 5, where "admitted" is asserted |
| the cancellation test removed from the deferred path (a bare post, then the send) | NC-4, first and third arms | The recorded cancellation throws at the await of the inner send. With that await outside a `try`, the send completes with an exception where the OD-1 error is asserted. With it inside one that converts as `Session::send`'s does, `Disconnected` is written and "the session stays Active" fails |
| the deferred path's cancellation test reading the recorded cancellation alone, without `throw_if_cancelled` | NC-4, fifth arm | The chain turned throwing off and a `terminal` cancellation is recorded. The test holds on the record alone, and the send returns the OD-1 error at the hop. Its own `toApp` never runs, where the arm asserts that it does |
| the depth kept outside the `Session`, as one counter for the thread or for the engine | NC-3 | A's `toApp` raises the shared depth. B's send, started inline from A's callback, reads it as non-zero and takes the deferred path. B's `toApp` runs after A's callback returned, where "not deferred" is asserted |
| the `closing` test removed from the deferred path | NC-5 (b), graceful close then send | The close's post runs first and its Logout suspends in its store call with `fsm_state_` still Active. The send's post runs, `Session::send`'s Active check passes, and the send's frame is transmitted after the Logout. "Returns `session_invalid_state_for_send`" and "the Logout is the last frame" both fail |
| the payload copy removed | NC-15 | The deferred send's build reads the span after `toApp` freed the buffer: ASan reports a heap use after free |
| the depth-zero assertion removed from the deferred path | NC-16, first arm | The post's continuation runs inside `toApp` and the send goes on nested. It reaches its own `toApp`, where the stack-arena overload's assertion aborts the process with a different text. The arm matches step 4's text, so the death does not satisfy it |
| step 6 of the deferred path passing "apply" | NC-16, third arm | Step 4 is compiled out, the depth is still non-zero, the test applies, and each continuation defers again: the callback's loop hits its bound |
| the close's wait removed | every close cell: "inside the callback after the start, the lifecycle is open" | `close()` runs inline in the callback and writes `closing` before the callback's `co_spawn` returns. With §2.2's token a nested callback no longer aborts on the debug build by itself, so the cell fails at that assertion on both build types. A close inside `toApp` or `toAdmin` aborts first on a build with assertions: a graceful one where its Logout's `toAdmin` meets the stack-arena overload's assertion, a terminal one where `onLogout` meets the lifecycle sites' assertion |
| the close's wait limited to graceful mode | the terminal close cells and `…NestedToAppThrows*`: "inside the callback: Active, `onLogout` not run" | A terminal close runs inline to `Disconnected` and fires `onLogout` with the callback on the stack. Inside `toApp` or `toAdmin`, on a build with assertions, the process aborts there, at the lifecycle sites' assertion |
| the close reading the outbound depth where it must read the depth of all callbacks | `FromAppStartsClose_*`, `FromAdminStartsClose_Terminal`, `…NestedToAppThrows*`: the same assertion | In `fromApp` and `fromAdmin` the outbound depth is zero. In `…NestedToAppThrows*` the nested `toApp`'s scope has exited before `send_impl` awaits the close. The close runs inline in each |
| the depth of all callbacks maintained only on builds with assertions | every close cell, on `linux-clang-release` only: "the lifecycle is open inside the callback" | The scope compiles to nothing under `NDEBUG` today. If the counter goes with it, the depth reads zero in release and the close runs inline |
| the depth of all callbacks set to zero on a scope's exit where it must be decremented | `…NestedToAppThrows*`: "still Active inside `fromApp`" | The nested `toApp`'s scope exits and zeroes the depth while `fromApp` is on the stack. `send_impl`'s close reads zero and runs inline |
| the close's wait placed after the `closing` write | every close cell: "inside the callback after the start, the lifecycle is open" | The write happens before the post, inside the callback |
| the close's wait placed between the three-state test and the `closing` write | NC-8. Arm 1: "returns exactly `session_already_closed`". Arm 2: "the hook has run once" | The waiting close passed the test while the lifecycle was open. Arm 1: the other close completes during the wait; the waiting close then writes `closing` over `closed_drained`, runs the body a second time and returns success. Arm 2: the waiting close resumes at the first close's first suspension, which is after that close called the hook; it replaces the shared result and runs a graceful body of its own, so the hook runs a second time |
| the close's wait placed in front of `close()`'s reset, followed by asio's cancellation test and an early return (the shape the send's hop has) | NC-7, its `terminal` arm: "the session reaches `closed_drained`" | Before the reset the caller's terminal-only filter is in force, so a `terminal` emission is recorded. The test returns early and the close is skipped. The `total` arm stays green under this mutant, because that filter drops it |
| the depth-zero assertion removed from the close's wait | NC-16, second arm | The close's continuation runs inside `fromApp` and the close proceeds there. No other assertion lies on its path: inside `fromApp` the outbound depth is zero, so a Logout's `toAdmin` passes the stack-arena overload, and `onLogout` passes the lifecycle sites' assertion, which reads the same depth. No death where one is expected |
| the hit path replaced by the control-strand hop | `MtEngineSendFromFromApp_BusyControlStrand`: "every reply nested" | With the control strand busy the first hop does not run inline, and the cell reports deferred replies. Measured on today's code, which is this mutant (the record) |
| the OD-3 check removed | NC-10 | The session is Active and its flag is set; without the check the send is admitted and transmitted |
| the token's owner test removed | NC-9 (a) | The second thread enters without an assertion: no death |
| a nested scope's exit clearing the token | NC-9 (b) | The nested exit leaves "no owner" while the outer scope is live. The second thread installs itself and passes: no death |
| the token built as a plain member | NC-9 (d) | The second thread's read of the token has no ordering with the first thread's write that ThreadSanitizer can see. It prints a data-race report on the token, and the arm's matcher rejects output that contains one (P-7). Arms (a) and (b) stay green under this mutant |
| the inbound overload's parse-site assertion removed | `InboundFrameDispositionsI3.AnInboundParseReenteredFromACallbackDies`; NC-19's dispatch arms | The nested arm reaches the inbound overload with a callback on the stack, builds the resource over the buffer and calls the receive callback, whose scope passes the token on the owner thread: no death |
| `validate_inbound_`'s parse-site assertion removed | NC-19's validate arm | The validate gate parses the nested frame over the buffer and rejects it. The arm answers with a Reject, whose `toAdmin` enters the stack-arena overload inside `fromAdmin`, with the outbound depth at zero, and passes. The nested arm never reaches the inbound overload: no death |
| a parse-site assertion reading the outbound depth where it must read the depth of all callbacks | the I-3 test; NC-19's `onLogon` and validate arms | Inside `fromApp`, `fromAdmin` and `onLogon` the outbound depth is zero: no death. NC-19's `toApp` and `toAdmin` arms stay green under this mutant |
| a parse-site assertion that fires only while the inbound overload's own callback is on the stack, which is what v0.4's mark did | NC-19's `toApp`, `toAdmin` and `onLogon` dispatch arms | No inbound callback is on the stack when the nested frame arrives: no death. The I-3 test stays green under this mutant |
| the stack-arena overload's assertion removed | NC-20 | The liveness emit's `toAdmin` enters with `toApp` on the stack and passes the token on the owner thread. The Heartbeat's callback runs inside the outer emit's stretch: no death |
| the lifecycle sites' assertion removed, from both sites or from the `onLogout` site alone | NC-19's lifecycle arm | The nested arm writes `Disconnected` at the BeginString test, and `onLogout` is entered inside `toApp`, where its scope passes the token on the owner thread. The arm parses nothing over the buffer and emits nothing, so no other assertion lies on its path: no death |

Rows v0.3 had, and why they are gone:

- **"The scope's exit setting the [outbound] depth to zero".** With R-1 and R-4 in place, and inside the
  contract, no outbound callback of a session can run while another outbound callback of that session is
  on the stack: a send,
  an `Engine::send` and a close started there all wait. The mutant differs from the correct code only on
  a path that does not exist, so no behavioural cell can fail on it. The row and its cell are deleted.
- **"The Active re-check after the hop removed".** The deferred path no longer has an Active test of its
  own (§2.1); `Session::send`'s check covers it, and removing that check is not a mutant of this PR.
- **"The close's wait made cancellable".** Replaced by the narrower row above. Its other reading, the wait
  moved in front of the reset with no test after it, fails no cell: a cancellation recorded during the wait
  is erased by the reset. That placement is therefore not witnessed. The note still specifies "after the
  reset" (§2.1a), so that the statement which shields the rest of `close()` covers the wait too, and it
  claims no witness for that order.
- **"The guard's restore replaced by a clear", against the nested-then-sequential cell.** A clear never
  causes a spurious assertion in that cell; it causes a missed owner and a missed depth. The two rows
  above that replace it name the cells that observe those. The nested-then-sequential arm stays as
  NC-9 (c), a control.

A row v0.4 had, and why it is gone: **"the inbound-parse mark's assertion removed"**. The mark is removed
(§2.2); the parse-site rows above replace it.

Specified without a witness, and claimed as such:

- **The hit path reading `reader_snapshot_` and not `Engine::lookup`** (§2.3, step 3). The two differ only
  where a send's frame is destroyed after the Engine on a build with assertions. No cell constructs that.
  The lease-counter assertion round 3 offered as a witness is not taken (owner): the two are the same
  program in release, and differ in debug only when an Engine is destroyed while its own `stop()` is
  suspended, which the Engine's destructor already forbids.
- **The lifecycle sites' assertion at the `onLogon` site** (§2.2). NC-19's lifecycle arm reaches the
  `onLogout` site only. An arm for `onLogon` needs a Logon processed while an outbound callback of the
  session is on the stack, and no setup for that was read to a conclusion.
- **The close's wait after `close()`'s reset and not in front of it**, with no test after it: the third
  bullet of the v0.3 list above.
- **The side of its scope on which the stack-arena overload's assertion sits, and the lifecycle sites'**
  (§2.2: after the scope). On one thread both placements die at the same assertion, with the same text.
  NC-9, the cell with two threads, reaches the scope through test access and enters neither site. Listed
  with the orchestrator's decision of 2026-10-11.
- **The default mode's immunity where the session's entry points run on its own strand** (§2.1). NC-16's
  fourth arm is a neighbouring control: its handler is the fixture's outer strand's.
- **The sends R-2 admits that today's control-strand read refuses** (§2.4). Which side of `stop()` a
  racing send falls on is timing; no cell forces it.
- **OD-3's refusal on R-5's route** (§2.1a). The route is out of this PR; fixpp#579 inherits it.
- **The branch "the GapFill of a resend replay" of §2.1's first-yield table.** No cell starts a send in
  that GapFill's `toAdmin`: no setup for a send started inside a resend replay was read to a conclusion.
  The cells that start a send in `toAdmin` do so at the Heartbeat that answers a TestRequest and at a
  session Reject. The row rests on §1's reading of `emit_gapfill_async`.
- **The branch "a failure exit" of the same table:** a veto, a throwing callback or a failed assign of
  the outer emit, after its callback started a send. No cell drives it. The outer frame is abandoned
  there, so nothing of it is stored or sent that the deferred send could be ordered against.

A mutant that stays green is a finding about its cell, and the cell is fixed or deleted before the
verification record's verdict is written.

**Existing tests that pin this path and must stay green**, each with whether it still reaches the code it
was written for:

| Tests | After this PR |
|---|---|
| `InboundFrameDispositionsI3.AnInboundParseReenteredFromACallbackDies` (`tests/session/inbound_frame_dispositions_test.cpp`) | Still dies, now at the inbound overload's parse-site assertion. Its expected text is edited to that assertion's (§2.2, §4) |
| `EngineSessionStrand.V15_SendCounterEnrolledBeforeControlHop_NoUAF` | Stays green and stops driving its path. It sends to an Active, published session, so after R-2 its sends take the hit path and never queue on the control strand. Its subject is restated as sends queued on the session strand when `stop()` drains, and NC-18 is its twin on the path that keeps the control-strand hop |
| `EngineSessionStrand.V16_PostDrainLateSendFastFails_WithoutPosting` | Still reaches its code: it fails at the admission re-check, which is before any lookup and unchanged. It requires exactly `session_invalid_state_for_send` |
| the other send-versus-stop cells and the snapshot readers of `EngineSessionStrand`; its re-entrant sub-cell (V-9, sub-cell (a)) | The implementer classifies each cell by whether its sends target a published session (hit path) or not (miss path) and writes the classification in the verification record. The re-entrant sub-cell posts before it sends, so it still shows a send started after a callback and no longer crosses the control strand; its comments are in §4 |
| `ApplicationOutbound.EnginesSendWithUnknownIdReturnsInvalidArgument`, `EnginesSendOnRegisteredButNotEstablishedSession` | Still reach their code: both take the miss path, whose results they pin |
| `ApplicationEngineSend.SendFromForeignThreadCrossesWire`; the C-ABI send tests | Reach `Engine::send` from a foreign thread, now on the hit path |
| `ApplicationEngineSend.SendDrainRaceNoUAF` | Not a foreign-thread cell: it is on R-5's route. It sends from the thread that runs the `io_context`, and its `toApp` hook starts `engine.stop()` by a plain `co_spawn`, so `stop()`'s first steps run inside `toApp` (§2.1a). By reading, its result does not change: the send's session-strand body has started, and passed the stop check, before the hook runs, and the cell accepts a value, `session_invalid_state_for_send` or `dispatch_aborted`. Its `total` emission is dropped by the chain's terminal-only filter (§2.1), so it does not exercise a cancelled send. Its comments say "posts stop()" and describe the emission as cancelling the send; §4 lists them. fixpp#579 inherits the cell as the existing witness of its route, with both facts |
| `ApplicationEngineSend.ReentrantSendFromToAppNoDeadlock`, `ReentrantSendFromFromAppNoDeadlock`; `ApplicationStrand.ReentrantSendNoDeadlock`; `ApplicationOutbound.ReentrantSendFromCallbackDoesNotDeadlock`; the interop conversation cells; the re-entrant cell of `test_business_messages_roundtrip.cpp` | Unchanged in behaviour. Each posts before it sends, or sends from the test body, or posts a no-op, so none starts a send inside a callback and none exercises this contract. Their comments attribute the deferral to `Engine::send`; §4 lists those comments |
| `ApplicationStrand.NoConcurrentCallbacksForOneSession`; `SeamDirectExecutorReentrancy.CorrectAttestationDoesNotTripGuard`; the `SeamExecutorCompat` cells | Still reach the guard, in its token shape. Serial callbacks must not trip it |
| `B35SendSlot.Nested_ASendInsideToAppLeavesTheOuterFrameIntact` | Does not stay as it is: NC-17 |

**Sanitizers** (`[const §IX.2]`: all three run on the PR).
- TSan: the `EngineSessionStrand` suite, the two `Mt*` cells, NC-9's arm (d) and NC-18. The hit path reads
  the snapshot off the control strand from io threads, which is new for `Engine::send`.
- UBSan: every cell above on `linux-clang-ubsan`. The change adds integer counters, one depth
  incremented and decremented in pairs, the other saved and restored, and no arithmetic on untrusted
  input; a clean run is expected and is stated as what it drove.
- ASan: every cell above; of NC-13, the arm its row names, a terminal close started in `fromApp` (R-3);
  NC-15 and NC-18; and the cells of §2.5
  (`FromAppSendsReply_NestedToAppThrows*`, `FromAppStartsClose_*`, `FromAdminStartsClose_Terminal`), which
  the preset could not reach before §2.2. The disabled control cell in the test file shows the instrument
  reports on this binary. By §2.5 no owner frees the Session in the direct-`Session` cells, so a clean run
  there shows the absence of other faults on these paths and says nothing about lifetime; NC-13 is the
  cell with an owner.
- A clean sanitizer run is stated as what it drove, not as absence.

**Coverage** (`[const §IX.1]`). Measured on `linux-clang-coverage` with fresh per-binary profiles, over
the functions the PR changes in `src/session/session.cpp`, `src/session/engine.cpp`,
`src/session/session_engine_access.hpp` and `include/fixpp/session/session.hpp`, against the article's
line and branch thresholds for the touched
module. Every line or branch the diff adds that stays uncovered gets a disposition in the verification
record: tested, waived with its reason, or escalated to an issue. These dispositions are fixed in advance:

- The half of step 3's test in which a cancellation is recorded and `throw_if_cancelled` is off: tested,
  by NC-4's fifth arm. No arm of v0.4 reached it.
- The failing side of each assertion this note adds: reached only in a death arm's child process. Where
  the coverage run does not collect a child's profile, that side is waived as the violated precondition
  the death arm witnesses, and the record names the arm. The lifecycle sites' assertion at the `onLogon`
  site has no death arm (the list above); its failing side is waived on that statement.

**Static analysis and format** (`[const §IX.4]`): `clang-tidy`, the `clang-format` check, `cppcheck` and
`include-what-you-use`, over the files the PR touches, as `/speckit-verify` runs them.

**B35's gates.** The zero-allocation gates on the Active inbound read and on `Session::send`
(`tests/alloc_guard/`, the `test_544_*` cells, the mallocnesia population check) are re-run. A send they
measure is not started inside an outbound callback, so it does not take the deferred path; it does pay
the depth test, and the inbound read pays the counter writes. One `test_544_*` cell does start a send
inside `toApp` and changes: NC-17.

**Cost (OD-5).** The PR adds a bench, in a commit of its own before any production edit, and its manifest
row, in a later commit after the local run. Its evidence is that local paired run. "This PR's evidence",
below, fixes the order of the steps.

- **The bench.** Source `bench/session/engine_send_bench.cpp`. Target `engine_send_bench`, added in
  `bench/session/CMakeLists.txt` with `add_bench_with_test_include`, the helper that gives a bench the
  library root as an include root; this bench needs it for `tests/support/transport_double.hpp`. Its
  session also needs a dictionary: as the sibling benches that build a session do, the bench takes the
  FIX44 one from `tests/support/fix44_dictionary.hpp`, which needs a `FIXPP_DICT_DATA_DIR` compile
  definition on the target. The binary is `bin/engine_send_bench`.
- **Harness.** One `Engine` on one `io_context` that the bench thread runs, on a mock clock, as
  `session_send_bench.cpp` runs a `Session`. One registered initiator session whose
  `transport_factory_override` is `ScriptedReadTransportFactory`, a plaintext factory (the session takes
  the plaintext security profile that the factory's comment names), over a `ScriptedStream` that scripts
  the peer's Logon and has `hold_open` set, so the read pump waits after the Logon and reads no end of
  stream. `tests/session/engine_establishment_timeout_test.cpp` drives an
  engine-managed session over the same double. The session is published and Active with no socket, no
  TLS and no file.
- **What it times.** Each iteration starts one `Engine::send` on the `io_context` with a completion
  handler and polls until it completes. The payload is a NewOrderSingle body whose first field is not
  MsgType. `build_send_frame_` refuses it at its second test with `app_payload_malformed`, before the
  sequence number is read, the frame is built or any callback runs. The session stays Active, and nothing
  is assigned, stored or written.
- **Why the timed part is the route.** What runs per iteration is `Engine::send`'s own path (the payload
  copies, the enrolment, the admission re-check, the lookup, the spawn onto the session's executor, the
  Active check) and `Session::send` up to the refusal (its Active check, the depth test, the slot
  holder). The lookup is what this PR changes: the control-strand `co_spawn` and the `registry_` find on
  the base tree, the snapshot load and the stop check on the candidate. Nothing of the send path after
  the refusal is there to dilute that difference. One cost from outside the send path is in each
  iteration: the scripted read holds an `asio::steady_timer` while it waits, so the `io_context` has a
  reactor and each poll can include a zero-timeout kernel poll. It is equal on both trees and dilutes
  the difference a little; it is also a system call inside a row whose same-tree spread is checked
  ("The same-tree spread", below). The isolation is from the socket write and from everything else the
  send path does after the route. A send vetoed in `toApp` was the other way to stop before the write;
  it adds the frame build, the outbound parse and the callback to every iteration, which
  `session_send_bench`'s `to_app` case already times through `Session::send`.
- **Reach.** Before the timed loop, one well-formed send must complete with a value and reach the
  stream. After it, every timed send must have returned exactly `app_payload_malformed`, the session must
  still be Active, and the stream must hold no write after that first send's. On this path only
  `build_send_frame_` returns `app_payload_malformed`
  (`grep -n "app_payload_malformed" src/session/*.cpp`), so a send refused earlier, for an unknown id, a
  session that is not Active or a stopped Engine, is a miss. A miss is a `SkipWithError`, never a timing.
- **One thread.** The manifest's selection rule admits single-threaded, CPU-bound binaries only. So the
  send is started from the bench thread, outside any strand's handler and outside the `io_context`:
  that `co_spawn` is queued and runs at the bench thread's next poll, and the thread that starts the
  send is the one that runs it. The row times the route's CPU cost. It does not time the wake-up of an io
  thread by a send started on another thread, which is a scheduling measurement; `L-023-2` keeps that
  sentence (§4), and the foreign-thread probe below measures it locally, outside the gate.
- **The manifest row,** in the session block of `bench/ci-suite.txt`, under comment lines that name this
  note and say what the bench times:

  ```
  bin/engine_send_bench                       none:no-gb-baseline-recorded                     paired
  ```

- **This PR's evidence.** CI does not compare a `paired` row that is absent from the merge base's
  manifest: `[const §VIII.2a]` makes it a candidate-only addition, gated on execution and schema and
  left out of the timing comparison for that PR. So the comparison is local: A-B-A-B on
  `linux-clang-release`, compared min per tree. The bench source uses the public API and test-support
  headers that exist at the merge base (`tests/support/transport_double.hpp`,
  `tests/support/fix44_dictionary.hpp`), and `Engine::send`'s signature does not change, so the same file
  and the same target lines build in a base worktree. The order is fixed. (1) One commit, before any
  production edit, adds `bench/session/engine_send_bench.cpp` and its target in
  `bench/session/CMakeLists.txt`, and nothing else: no manifest row, no `MANDATORY_PAIRED` entry. (2) The
  production edits. (3) The local A-B-A-B run, with both same-tree spreads, and the foreign-thread probe
  on both trees ("The foreign-thread probe", below). (4) One commit with the manifest row, the
  `MANDATORY_PAIRED` entry and the erratum in `.specify/ci209-bench-gate.md`. A run at step 1 checks reach
  and gives a first look at the spread; it is not a leg of the comparison. If the bench source changes
  after step 1, step 3 is run again on both trees. The base worktree's path is padded to the candidate
  path's length, because
  a path of another length shifts code layout. The result goes into the verification record whichever
  way it falls; a slowdown beyond the budget of `[const §VIII.2]` needs the acceptance that article
  describes.
- **The local run's command.** Each leg runs the bench binaries with the Google Benchmark flags that
  `ci/run-bench-suite.sh` passes: by hand, or by that script given an explicit manifest as its last
  argument, a scratch file that lists the binaries of this run. The shipped manifest cannot serve: it has
  no row for `engine_send_bench` before step 4 and none for `session_send_bench`. Base and candidate
  alternate. With those flags each leg's JSON holds aggregate rows only, and "min per tree" is, for each
  benchmark, the minimum over a tree's legs of each leg's median `cpu_time`, the statistic `run_paired` in
  `tools/bench_compare.py` takes. `tools/bench_compare.py --paired` itself is not the instrument for this
  comparison: it leaves a `paired` row that is absent from the base manifest out of the timing
  comparison, which is why the comparison is local.
- **The same-tree spread.** The comparator fails a run in which a `paired` row's two measurements of one
  tree differ by more than its noise band, and CI takes no such reading for a row in the PR that adds
  it. So the local run also records the spread between the two base runs and between the two candidate
  runs for `engine_send_bench`, and compares each with the comparator's noise band
  (`PAIRED_NOISE_BAND_PCT` in `tools/bench_compare.py`). If either exceeds half the band, the bench is
  stabilised before the manifest row is committed. If it cannot be, the row's `paired` status returns to
  the owner before merge.
- **The counters and the depth test.** The same local run covers the in-tree benches that drive those
  paths: `bin/on_inbound_frame_bench`, whose NewOrderSingle cases deliver to a counting `fromApp`
  and so pay the counter writes on the inbound read; and `bin/session_send_bench`, whose two cases pay
  the depth test and whose `to_app` case pays an outbound callback's counter writes.
  `on_inbound_frame_bench`'s manifest status does not change; `session_send_bench` has no manifest row
  and is run by hand. These benches are read under the same rule as the route bench: A-B-A-B, min per
  tree, `[const §VIII.2]`'s budget, and the result goes into the verification record whichever way it
  falls. Probe p1 is re-run for the reply path: the nested arm must not be slower than the same arm built
  against the base tree, beyond the probe's own run-to-run spread.
- **The foreign-thread probe.** v0.4's second probe: it times `Engine::send` from a foreign thread, base
  against candidate. The first OD-5 ruling promoted it to a paired bench; the manifest's selection rule
  does not admit a binary of that kind ("One thread", above), so the paired bench above is the PR's gated
  evidence in its place (§0). The probe is kept as a local measurement, at step 3: it is run on the base
  tree and on the candidate, and its result goes into the verification record whichever way it falls. It
  is not gated: it has no manifest row and no pass criterion. Its source goes beside p1 in the parent
  repo's `research/probes/b41_563_564/`, outside the PR's tree.
- **The known cost of the ruling.** A `paired` row cannot be removed or downgraded once merged
  (`[const §VIII.2a]`).

**Local gates before the first labelled push:** every `ci-script-pins` step, the bench suite's own run
over the changed manifest (`ci/run-bench-suite.sh` on the candidate build), every Tier 1 source-audit job
(the `co_spawn` closure audit and its census reconciliation read `engine.cpp`'s spawn sites, which change),
`check_line_citations.py --shift-audit origin/main..HEAD` (023's bundle cites `engine.cpp` by line), and
`check-comment-claims.py`, which is in the parent repo's `.claude/scripts/` and is run with the library
as its `--root`. The `[const §XVII.7]` local build and the MSVC leg are in §0a.

**§2.6's rows.** Gate A round 6 reviewed v0.10's form of them. v0.11's edits to them, v0.12's and
v0.13's were not reviewed (last section). Everything above in this section is v0.7's and is not
changed by them. They are for the implementer of §2.6. The rule: each clause of §2.6 that changes
behaviour has an arm that is RED in the pull request's tree before the production edit, and the
record shows it RED there. Each row below says of each cell and arm which it is: **RED first**; a
**control**, green after the change, and at the head either green or stopped by its own path guard,
which the row then says; or a **pin**, of a ruled behaviour or of a stated limitation. Where an
arm's state at the head was not run, the row says "predicted". The scratch prototypes are not the
pull request's tree. Status at 2026-10-14: every cell of these rows ran at the cells commit
`5056543b`, in the pull request's tree and before the production change, on `linux-clang-asan` and
on `linux-clang-release`. The record has that run, with each arm's failing clauses in the evidence
files it names, and its comparison with the rehearsal below (the record, "Phase 8, second part").

**When a prediction about the head disagrees** (analysis C-4). The implementer runs every arm at the
head before the production edit. Where an arm does there otherwise than its row predicts, the result
goes to the orchestrator before the production edit, and the arm's label is corrected in a commit of
this note before the cells commit is called RED-first evidence. The rows of NC-21 (its sixth arm),
NC-24 (a), NC-27, NC-29, NC-32 and NC-33 refer to this rule; the prediction rows above them state
their own consequence.

**What the head does, as rehearsed (v0.13).** Before the cells are committed in the pull request's
tree, the fixture and the cells as written were run in a scratch copy: the tree at `d7bf5dea` with
the fixture and the cells and with no part of the mechanism, each cell alone, three runs; and then
with the final patch of the mechanism, each cell alone, twenty runs. Every cell gave one result in
its three runs at the head, and every cell was green in its twenty runs on the patch
(`ph8f_head_states_for_the_note.md`, which has per cell the state, the failing clauses and what was
read; `ph8f_chainV-results.txt`, the step list with the run counts). Each row below ends with what
its cells did at the head there, in the words "in the scratch copy". Where a row says "predicted" of
the head, its last sentences say what the rehearsal measured. No row says any longer that its state
at the head is open or that it was not run there. **No measured state contradicts a label.** Every
arm labelled RED first is RED at the head. Every arm labelled a control or a pin that was predicted
green is green. Every arm predicted to stop at its path guard stops there. The arms whose state at
the head was not determined are all RED: NC-24 (a) on the live transport, NC-24 (b)'s two arms,
NC-28 and NC-30's two arms. This rehearsal is not the RED-first evidence. The run that is, of the
cells commit `5056543b` in the pull request's tree, was made on 2026-10-14 on two presets
(the record, "Phase 8, second part"). In it the state and the set of failing clauses of every arm
are those of this rehearsal. The record says how that was established: by comparing the two runs,
and not by reading each row of this note against each line again.

- **The limits of this evidence.** A Debug build with AddressSanitizer only. The scratch copy, not
  the pull request's tree. Three runs per cell at the head. No release build and no ThreadSanitizer
  run was made there, and the session test binaries outside the ones present in the scratch tree
  were not built or run there. The complete regression pass runs in the main checkout, on the local
  commits, before anything is pushed. That departs from "measured in the scratch copy first"; the
  orchestrator decided it on 2026-10-14, for disk space. Status at 2026-10-14: the cells ran at
  the cells commit and at the production commit on `linux-clang-asan` and `linux-clang-release`,
  and the complete regression pass ran at `a09d9093` on both (the record, "Phase 8, second part"
  and "Phase 8, third part"). A ThreadSanitizer run was not in when this status was written.

*Predictions and measurements,* in the form of the table above. "Measured" here always means a
prototype, in a scratch copy at `f5e47486`, Debug with AddressSanitizer, 2026-10-13. S6 is the
prototype of §2.6's mechanism; S5 is the one before it, with `Disconnected` written after the Logout's
store call; S3 is v0.8's. Status at 2026-10-14, from the record. P-12 with no stall, P-16, P-17,
P-20 and P-23 were measured again on the final patch and agree; P-18's closes completed at 300 to
301 ms of a timeout of 300 ms; and the readings of P-21 and P-22 stand ("Phase 8, first part").
P-13, P-14, P-15 and the head halves of P-16, P-17, P-20 and P-23 were measured at the cells
commit, in the pull request's tree: each arm did at the head what its row says ("Phase 8, second
part"). Not in when this status was written: P-12 through the stall probe, and P-19's mutants.

| Id | Predicted (read; not run unless the row says "Measured") | Measurement | If it disagrees |
|---|---|---|---|
| P-12 | §2.6's ending on the six arms of the condition, under the three stall settings; the same with `reset_on_logout` set, and on the strand fixture. `onLogout` runs before the Logout is numbered and stored. With the head's cells unchanged, exactly the five arms that transmit the Logout change result. | **Measured on S6: agrees** (`ph8c_S6_arms_by_stall.md`; `ph8c_predictions.md`, Q1 and Q7; the probe's positive control is `ph8c_probe-positive-control.txt`; every cell of the regression pass is in `ph8c_regressions_per_cell.tsv`). 20 runs per arm and setting, and 300 on `FileStore` live with no stall. Inside `onLogout` the next outbound number is the Logout's own in every run; on S5 it is the Logout's plus one. The same matrix was measured on S5 and on S3 before it (`ph8b_S5_arms_by_stall.md`, `ph8a_S3_arms_by_stall.md`), with the same readings apart from that one. §2.6, "What was measured, and its limits", has what the pass did not build or run. | The implementation is measured again in the pull request's tree, through NC-21 to NC-33. If an arm ends otherwise there, the implementation differs from the prototype, or the prototype was read wrongly. The difference goes to the orchestrator before any assertion is changed. |
| P-13 | NC-22's two arms, at the head, on the clause "not on the wire". (At the reading inside `onLogout` both arms are RED at the head, as NC-21's arms are.) The arm that holds the submitting thread until the file pool is idle makes the order of the setting "the session's thread stalled", in which the head transmits the Logout: RED at the head. The arm that holds the Logout's store job until the session's executor has run dry makes the order of the setting "every job of the file executor delayed", in which the head does not transmit it: green at the head. That prediction is for the clause "not on the wire". The clause "retained" in that arm, at the head, holds only if the hold keeps the order of the executor's jobs: at the head a cancellation is emitted while the job is held, and what the store then holds depends on which job runs first (§2.6, "The bound", (a)). The hold is specified so (the fixture, below), and the setting it stands on reorders nothing. After the change both are green. | Both arms in the pull request's tree before the production edit, and again after it. Not run in the pull request's tree yet. The two settings they stand on are measured at the head (`ph8a_S_arms_by_stall.md`, "unpatched head"). **Rehearsed at the head in the scratch copy** (2026-10-14; `ph8f_head_states_for_the_note.md`): as predicted. The first arm is RED at "not on the wire". The second arm is green on that clause and RED at the reading inside `onLogout`; its Logout is retained under its number. | If neither arm is RED at the head, the gate does not make the order it claims. The arms are rebuilt before the production edit, because without a RED arm the `FileStore` cell on the live transport has no RED state. If both are RED, the reading of which order transmits is wrong, and §2.6's "What the head does" is corrected. |
| P-14 | NC-24's store doubles. (a) A double whose `store` takes more hops than the shipped stores: §2.6's ending in full, the Logout retained, the mock clock not advanced. No cancellation reaches its call, because nothing cancels a cut Logout's store call before the grace timer expires. (b) Two doubles that hold their call past a short `logout_disconnect_timeout_ms` and give it up when a cancellation reaches them. One reports the cancellation and leaves the caller's cancellation state alone. The other loses the recorded cancellation, as the lock sequence of the shipped stores does. Both end alike: both closes complete inside the cell's bound with the mock clock standing; not transmitted; the number consumed; nothing retained; both closes success. | Not run in the pull request's tree yet. Read: `store_then_emit`, and the cut exit of statement 4, which has no await after the store call. The shipped `FileStore`'s form of (b), at its lock, is measured: P-18. **Rehearsed at the head in the scratch copy** (2026-10-14; `ph8f_head_states_for_the_note.md`): NC-24's row has each arm. (a) is RED on both transports. (b)'s two doubles do not end alike at the head, and both arms are RED. | (a): the ending does not hold for a store with more hops, which is acceptance item 2 of the issue; §2.6 returns to the orchestrator. (b): if the two doubles end differently, a cancellation that is lost still changes the ending, which is round 4's O-1 returning; §2.6 returns to the orchestrator. |
| P-15 | NC-25's two new controls. (b) A second graceful `close()` started in the Logout's `toAdmin`: it waits, then joins the first; the Logout is transmitted once; both closes return the same result when the first one's wait ends. (c) A terminal close that took the wait before the Logout step read the count: the count is non-zero and does not move inside the Logout's `toAdmin`, and the Logout is transmitted once. | Not run in the pull request's tree yet. Read: `Session::close` and `run_logout_phase1`; for (c), the trace in NC-25's row (round 4's O-7). **Rehearsed at the head in the scratch copy** (2026-10-14; `ph8f_head_states_for_the_note.md`): (b) and (c) are green at the head, as NC-25 (a)'s arms are with their new clause. | (b): if the Logout is not transmitted, the count moves for a graceful close, which is a defect of the implementation. (c): if the Logout is not transmitted in the unmutated tree, the trace is wrong; the setup is corrected before the mutant "the count is non-zero" is run. |
| P-16 | The held-lock scene (NC-26): a second writer of the session holds the `FileStore`'s lock while the cut Logout's store call reaches it. With statement 4: while the lock is held no sleep is in flight on the session's clock and the session is `Disconnected`; after the release both closes complete with the mock clock standing, and the Logout is retained under its number. With v0.8's statement 4 (S3): RED. At the head: RED, by the reading of §2.6's "What the head does" (the Logout transmitted, `LogoutSent` recorded, the close waiting out the timeout). | **Measured on S6, S5 and S3: agrees** (`ph8c_predictions.md`, Q5, for S6; `ph8b_held_lock.md` for S5 and S3; cells in `ph8c_cell_edit_S6.patch`). S6: green, 20 of 20 on each transport, with the fatal assertion that the held `on_inbound_frame` has not returned at the snapshot. S5: green. S3: RED, 5 of 5 on each transport, at "the step is at the store's lock, not in a wait", at "both closes complete within the pump's bound with the mock clock standing" and at "the Logout is retained under its number". **In the pull request's tree that run was made at the cells commit `5056543b` (the record, "Phase 8, second part").** Two earlier runs of these cells did not drive the path and are evidence of that only (`ph8b_predictions.md`, "The scenes' first two runs did not test anything"). **Rehearsed at the head in the scratch copy** (2026-10-14; `ph8f_head_states_for_the_note.md`): RED at the head on both transports, as predicted: the Logout is on the wire and not retained, `LogoutSent` is recorded, and a sleep is in flight on the session's clock. | S6's shape RED in the pull request's tree: statement 4 does not remove the lost waker; §2.6 returns to the orchestrator. Head green: the reading of what the head does on this route is wrong. The cell is then a pin, and the clause it witnesses (nothing cancels the store call) has no arm that is RED in the pull request's tree, only the scratch mutants; that goes to the orchestrator before the production edit. |
| P-17 | A Logout from the peer delivered after the cut Logout's store call has returned and before the closes complete (NC-27, its first two arms): it meets `Disconnected`; no `fromAdmin`, no confirming Logout, no number. | **Measured: agrees on S6 and S5; the other result on S3** (`ph8b_predictions.md`, P5; `ph8c_predictions.md`, Q6, for the same cells on S6). S3: the state is Active, `fromAdmin` runs once, and a confirming Logout with the next number is transmitted. Arms: `MemoryStore` and no store, synchronous sink. **Rehearsed at the head in the scratch copy** (2026-10-14; `ph8f_head_states_for_the_note.md`): the two arms are RED at the head, at "the session is `Disconnected` when the arrival comes". | If the frame meets Active with statement 4, the step did not write `Disconnected`; the cell's delivery is checked first. |
| P-18 | The bound at the store's lock (NC-28; ruling R-9): the lock held past a short `logout_disconnect_timeout_ms`, live transport. Both closes complete at the grace timer's expiry, with success; nothing is under the Logout's number. | **Measured on S6** (`ph8c_predictions.md`, Q5 and "When the two closes complete, against the timer"), 20 of 20, with a timeout of 300 ms and the gate opened at about 900 ms: `onLogout` at 0 ms; the transport's direct `close()`, its `close_async()` and both closes' completions at 300 ms from the start of the graceful close (smallest 300, largest 300, in whole milliseconds); both closes success; the session `Disconnected` and `closed_drained`; nothing under the Logout's number and the store's next outbound number the Logout's own; the session's next outbound number the Logout's plus one; nothing transmitted; one direct `close()` and one `close_async()`. Positive control of the reading: under mutant D the same events are at 0 to 1 ms. The transport is a double whose `close_async()` returns at once; a real TLS transport's was not measured. **Rehearsed at the head in the scratch copy** (2026-10-14; `ph8f_head_states_for_the_note.md`): NC-28 is RED at the head; its row has what was read. | Not a prediction. NC-28 pins what was measured; a different result in the pull request's tree goes to the orchestrator. |
| P-19 | Both closes return success on the two arms with no store, where the step ends inside its own launch. The cells can report the other result. Each measured mutant has its kill cell among the final cells. | **Measured on S6's cells** (`ph8c_predictions.md`, Q6; `ph8c_failing_assertions.txt` has each failing assertion; five runs per cell; each mutant's binary has its own hash). Success and success on the six arms, 20 of 20 in every setting. The mutant table below has each result: A, no cell fails; C, the six arms and the two frame-after arms; D, the held-lock cells, and with them the six `FileStore` arrival arms in their gated form, whose RED came from the gate's reordering and is not a kill (the mutant table); E, which is S5, the twelve arrival arms and the reading inside `onLogout`; F, the twelve arrival arms, the two frame-after arms and that reading. | Not a prediction. |
| P-20 | What arrives while the cut Logout's own store call is held (NC-27, its arrival arms) meets a closed session: no callback, no number, nothing transmitted, the send refused, no reset with `reset_on_logout` set. With `Disconnected` written after the store call (S5): each arrival is acted on. | **Measured on S6: agrees, 20 of 20 on each of twelve cells; on S5: the other result, 5 of 5 each, the cells RED** (`ph8c_in_flight_frames.md`; `ph8c_scenes-S6/`, `ph8c_scenes-S5/`; `ph8c_arrivals_compact.txt`). The arrivals: a Logout from the peer, the same with `reset_on_logout` set, an application message, a TestRequest, a `Session::send` from another coroutine, a wake of the liveness loop; each on a `FileStore` and on a store double that holds its `store` call. Synchronous sink only. The fatal assertions of the path hold in every run on both prototypes. The six `FileStore` cells were behind the gate in those runs. They were run again on S6 with the order of the executor's jobs kept (`ph8e_predictions.md`; the cells named `Ph8eArrivalInTheHold_…_FileStore_OrderKept`): green, 20 of 20 each. S5 was not run on that form. **Rehearsed at the head in the scratch copy** (2026-10-14; `ph8f_head_states_for_the_note.md`): at the head the six `FileStore` arms are RED and each arrival is acted on; the six arms on the store double stop at their path guard. NC-27's row has what each arrival caused. | If an arrival is acted on with statement 4, the state was not written at the decision point, or the arrival was delivered before `toAdmin` returned; the cell's delivery is checked first. |
| P-21 | A reading: a `Session::send` that has its number and is suspended in its own store call when the cut is decided. | **Measured on S6 and S5** (`ph8c_in_flight_frames.md`, "A reading: a send in its own store call when the cut is decided"; 20 runs on S6, 5 on S5; the cells assert the path only). The send keeps its number and is transmitted when its store call returns, and returns success; the Logout takes the next number, is retained and is not sent. On S6 the transmit comes after the session was written `Disconnected`. On the store double both closes complete while the send is still held. Synchronous sink only. **Rehearsed at the head in the scratch copy** (2026-10-14; `ph8f_head_states_for_the_note.md`): NC-30's two arms are RED at the head: the Logout is transmitted while the send's store call is held. | Not a prediction. NC-30 pins the reading. |
| P-22 | A reading: the Logout's own write job still in the file executor past a short `logout_disconnect_timeout_ms`, with the Logout cut and with it not cut (a graceful close alone), on four kinds of file executor. Predicted before the run, from round 6's O-1 (`ph8e_predictions.md`, "Predictions"): on an executor that keeps order the write runs before the cancellation's job, and the Logout is written and reads back; behind the earlier gate the record is unreadable, as it was measured before; on two threads the cancellation's job can run first. | **Measured on S6: each held** (`ph8e_predictions.md`; `ph8e_own_write_compact.txt`, which has each run's numbered sequence of the executor's jobs; `ph8e_own-write-S6/`; cells in `ph8e_cell_edit.patch`). 20 runs per cell, cut and not cut; `FileStore`, synchronous sink; timeout 300 ms, the write's job let go at about 900 ms. **The timing, in every kind and on both routes:** no close has completed 600 ms past the expiry, and each completes 0 to 9 ms after the job is let go. **The store, by kind.** One thread with the order kept (the cell holds the pool's thread, and both jobs queue behind it): the write's body ran once, the store's catch of `operation_aborted` did not fire, and the Logout reads back under its number, 40 of 40. One thread behind the earlier gate, which lets later jobs pass the one it keeps: the cancellation's job ran first, the body never ran, the catch fired once, the store's files did not grow, the store's next outbound number is past the Logout's, and a read of the Logout's number returns `store_io_failure`, 40 of 40. Two threads, with the thread that takes the write's job delayed before it starts it and nothing reordered: that same unreadable record, 40 of 40. Two threads, both held and then let go together, with nothing reordered or delayed: the Logout readable in 39 runs and that unreadable record in 1, which was on the cut route. Cut, in every kind: nothing on the wire. Not cut, in every kind: the Logout is transmitted when the store call returns; where the body never ran, that is a frame the store counts and cannot read. **This row corrects v0.10's.** v0.10 had the second kind only (`ph8c_predictions.md`, "The Logout's own write job past a short timeout"), and it called that store state a defect at the head with no condition on the executor. Not run: a job already inside its system calls at the expiry; more than two threads; a strand over a pool; a pool that is busy, as opposed to held; a restart of the store over such a file; ThreadSanitizer. **Rehearsed at the head in the scratch copy** (2026-10-14; `ph8f_head_states_for_the_note.md`): NC-29's cut arm is RED at the head, at "nothing on the wire", and its not-cut arm is green. | Not a prediction. NC-29 pins the reading on the executor that keeps order. The unreadable record on two threads is that store defect's own issue, and its witness is that issue's (NC-29's row). |
| P-23 | What the application starts inside the decision-point `onLogout` (NC-31; round 6's F-1). A terminal close waits by one post and joins; the count moves once more, after the comparison; the close does not wake the grace timer; three closes return success; the Logout is numbered and retained and not transmitted. A graceful close: the same, and the count does not move. A `Session::send`: `session_invalid_state_for_send`, no `toApp`, no number. With the store's lock held by another writer, no close completes while it is held. The record of the cut and the state's write can be swapped with no difference a caller can see. Read: round 6's triage, F-1, whose trace the judge wrote before reading the run. | **Measured on S6: agrees, 20 of 20 on each of fourteen cells** (`ph8d_predictions.md`; `ph8d_scenes_compact.txt`; `ph8d_scenes-S6/`; cells in `ph8d_cell_edit.patch`). The cells: a terminal close on the three stores, each on the synchronous sink and on the live transport double; a graceful close, and a `Session::send`, on the three stores on the synchronous sink; the terminal close and the send with the `FileStore`'s lock held. Every cell asserts as its path that `onLogout` ran with the next outbound number still the Logout's own. The count of terminal closes that took the wait, read through the test access: 1 inside `onLogout` before the start; 2 after a terminal close was started there; still 1 for a graceful close and for a send. The record of the cut reads set inside `onLogout`. On the live transport: no live write, one direct `close()` and one `close_async()`. **Mutants, five runs per cell** (`ph8d_failing_assertions.txt`; `ph8d_mutant-D/`, `ph8d_mutant-G/`). Under D the two cells with the lock held are RED in every run, and the twelve others are green. Under G (`ph8d_mutant_G.patch`: the record of the cut written after the state's write) all fourteen are green; the one reading that differs is the record inside `onLogout`, which is printed and not asserted. Not run: `Engine::send` and `Engine::stop()` from that `onLogout`; a graceful close with the lock held; the live transport outside the terminal close's scene; a real TLS transport; the stall settings; ThreadSanitizer; a release build. **Rehearsed at the head in the scratch copy** (2026-10-14; `ph8f_head_states_for_the_note.md`): all fourteen cells of NC-31 stop at their path guard at the head. | If an operation ends otherwise in the pull request's tree, the implementation differs from the prototype or the trace is wrong; it goes to the orchestrator before any assertion is changed. If an arm that is specified as a control runs past its path guard at the head, the guard does not hold what it claims, and it is corrected first. |

*Fixture additions.* Each has a default that leaves every existing cell as it is. The scratch forms
are in `ph8c_cell_edit_S6.patch`, `ph8d_cell_edit.patch` and `ph8e_cell_edit.patch`; each of the two
later patches is the whole cell edit against the head.

- The live transport double counts its `close_async()` calls apart from its `close()` calls.
- A gate on the `FileStore`'s `file_io_executor`, as a wrapper around the file pool's executor, set
  in the cell file's fixture (the first comment on fixpp#590 describes it). It has three modes. Each
  says here whether a job handed later can pass a job that is held (round 6's O-1):
  - *NC-22's first mode* holds the submitting thread until the file pool is idle. It holds no job,
    so no job passes another.
  - *NC-22's second mode* holds the Logout's store job until the session's executor has run dry, and
    it keeps order: a job handed to the executor after the held one stays behind it. It is the hold
    of the next bullet, used on the live transport (analysis B-1).
    - **How it knows the Logout's store job.** It does not look at the job. The cell arms it inside
      the Logout's `toAdmin`, when the close's flush has gone through the pool, so the first job
      handed to the executor after the arming is the Logout's write. The wrapper counts the jobs
      handed once it is armed, and the arm asserts as its path that the holding job is running and
      that one job was handed (`ph8e_cell_edit.patch`: `CountingPoolExecutor`, and the hold's
      `holding()`).
    - **What "run dry" is.** A bounded settle, and not a test that nothing is left to run. The cell
      runs the session's executor one handler at a time (`poll_one`) until none is ready or a fixed
      number of turns is reached, and then lets the pool's thread go. A close that has joined posts
      itself again while it waits, so with the terminal close joined the executor always has a
      handler; the comment at the prototype's settle loop says so.
    - **Not built on the prototype for this scene.** The hold was built and measured on the
      synchronous sink (Phase 8e). NC-22's scene is a `FileStore` on the live transport, and the
      task that builds NC-22 runs this mode first.
  - *The third mode:* once armed it keeps back the first job handed to the executor and lets every
    later job through, and opening it posts the kept job. **Later jobs pass.** It is used where the
    job must be kept from before the close: the close's own flush is a later job and must get
    through, and with the pool's one thread held from before the close the flush waits and the path
    is not driven (`ph8b_held_lock.md`, "The fixture").
    - **The condition that makes it safe: no cancellation reaches the kept job's coroutine while the
      job is kept** (analysis F-1). A cancellation that did would be a second job on the executor,
      and this mode would let it pass the kept one (§2.6, "The bound", (a)). v0.11 gave as the
      reason that the Logout's store call hands no job in these scenes; that is true and is not the
      condition.
    - **Why it holds, by reading.** In every arm that uses this mode, the coroutine that holds the
      kept job was spawned by the cell with a completion handler that has no cancellation slot (the
      cell file's `record_into`). A cancellation reaches a spawned coroutine only through the slot
      of the handler it was spawned with (read, in asio's `co_spawn`). So nothing that
      `Session::close` or any other code emits can reach that coroutine, whatever is emitted and
      whenever. That covers NC-28 too, where both closes complete, and the second phase of `close()`
      emits on the session's root signal, while the gate is still shut. The prototype's cells spawn
      so. The cells as they are written must too: it is what the condition rests on.
    - **The kept job, per arm.** In NC-26, NC-28 and NC-31's two arms with the lock held it is the
      inbound persist of the peer's order: a `FileStore::next_seqnum` call made by
      `Session::persist_inbound_advance_`, inside an `on_inbound_frame` that the cell spawned. In
      NC-30's `FileStore` arm it is the send's own write: a `FileStore::store` call of a
      `Session::send` that the cell spawned. v0.11's list of this mode's users left that arm out.
    - **Each of these arms checks the condition by its effect: the store's counter of its catches of
      `operation_aborted` reads zero over the scene.** The store has one such counter; the catch in
      `FileStore::store` and the catch in `FileStore::next_seqnum` both feed it; the cell reads and
      resets it before the scene (`read_and_reset_catch_fired`). The counter can report: it read 1
      in every run in which the cancellation's job ran first (`ph8e_predictions.md`, its kinds B and
      C). No run has this check in these arms; it is predicted to hold, by the reading above.
    - **What a readback adds, and where it cannot tell.** The decision on finding F-1 also asks that
      the kept writer's record reads back. In NC-30 it does: the send's frame is retrieved under its
      number, and a retrieval reads the file (measured the other way: a record that was not written
      does not read back, `ph8e_predictions.md`). In NC-26, NC-28 and NC-31 the kept write is a
      counter advance and no frame. `FileStore::next_seqnum` advances its counter in memory before
      the offload and does not take it back, so the running store's next inbound number is the
      peer's order's plus one whether the write ran or the catch fired. Those arms assert that
      number as a consistency clause; it does not discriminate, and the catch counter is their one
      check that does. A readback that discriminates there would need a second store opened over the
      same files after the close. It is not specified here.
    - **This mode is not used to hold the Logout's own write.** v0.10 used it for that, in NC-27's
      `FileStore` hold arms and in NC-29. A cancellation of the store call reaches the write as a
      second job on the file executor, and the gate let that job pass the write (§2.6, "The bound",
      (a)).
- **A hold of the Logout's own write that keeps the order of the file executor's jobs** (NC-27's
  `FileStore` hold arms; NC-29). Inside the Logout's `toAdmin`, when the close's flush has gone
  through the pool, the cell posts a job that holds the pool's one thread. The Logout's write job
  and every job after it queue behind that job, and run in order when the cell lets the thread go.
  The scratch form is in `ph8e_cell_edit.patch`: its hold named `file_store_thread_held`, and its
  executor that records when each job is handed over, begins and ends.
- A store double that completes its `store` call in a `MemoryStore` behind it and then does not
  return until released, built on `tests/support/hooked_store.hpp`. It gives a hold up after a time
  of its own, and each cell that uses it asserts that it did not.
- A count of the sleeps in flight on the session's clock, which the clock wrapper of NC-11 keeps.
- A recording wrapper over the store that notes when a `store` call has been made and when it has
  returned, as P-1's measurement used. NC-26 and NC-28 read it.
- The time at which `onLogout` ran, at which the transport's two close calls were made, and at which
  each close completed. It is read on `std::chrono::steady_clock`, counted from an instant the cell
  reads on that clock right before it spawns the graceful close (NC-24 (b), NC-28, NC-29).
- The store's two test seams in `src/session/file_store.cpp`, the offload probe and the count of its
  catch of `operation_aborted`, read by NC-29.
- **Two read-only accessors in `tests/support/session_test_access.hpp`** (NC-31): the count of
  terminal closes that took `close()`'s wait, and the record of the cut. They are readings. A cell
  prints them and asserts effects; no cell asserts either value (below, "Specified without a
  witness"). **They land after the production commit, in a tests-only commit of their own, with the
  printed lines that use them** (analysis G-1): they read the two new members,
  `waited_terminal_closes_` and `logout_cut_`, which do not exist before the production commit, so
  the cells commit cannot hold them. No assertion depends on them, so the order of tests first is
  not touched. The mutant runs that read them come after that commit.
- A knob for `reset_on_logout`, and one for `reset_on_disconnect` (NC-33), where the fixture has
  none.
- The store doubles of NC-24, local to the cell file. `tests/support/hooked_store.hpp` and the
  double of NC-14 are read first and used if they can do it.

*Cells.* NC-21 is cells that exist and must turn. The others are new. The prototype's cells for
NC-26 to NC-31 are the ones named `Ph8b…`, `Ph8c…`, `Ph8d…` and `Ph8e…` in `ph8e_cell_edit.patch`;
they get their names when they are written. NC-32 and NC-33 are new in v0.12 and have no prototype
cell.

| Id | Drives | Asserts, and whether RED first |
|---|---|---|
| NC-21 | the six arms of `CallbackSend.ToAdminOfGracefulCloseStartsClose_Terminal` (both transports; `MemoryStore`, `FileStore`, no store), and a seventh arm on the strand fixture: `MemoryStore`, synchronous sink, the session's executor a strand over the `io_context` (the fixture's `ExecKind::strand`; analysis E-3). §2.6 cites the strand fixture as evidence, and before v0.12 no arm in the tree ran on it **Cells** (suite `CallbackSend`; v0.13): `ToAdminOfGracefulCloseStartsClose_Terminal`, and it with the suffixes `_LiveWrite`, `_FileStore`, `_FileStore_LiveWrite`, `_NoStore`, `_NoStore_LiveWrite` and, for the strand arm, `_Strand`. | The same assertions in every arm; no arm asserts less than another. Inside `toAdmin` after the start: as the close cells assert it ("The arms every close cell has"). Then: the Logout is not on the wire; no live write of it was begun; where there is a store, it holds the Logout under its number; the next outbound number is the Logout's plus one; both closes return success, and the terminal one returns the graceful one's result; `onLogout` ran once, with the session `Disconnected`, and the next outbound number read inside it is the Logout's own, which is the witness that it ran before the Logout was numbered and stored; `LogoutSent` was never the session's state; on a live arm the transport's `close()` was called once directly and its `close_async()` once, with no write begun after the close; the mock clock is not advanced, and the arm's real time is under a bound that is derived from the cell's `logout_disconnect_timeout_ms` and is well below it, so that the real grace timer cannot complete the close inside the pump's budget. **The count of the transport's calls is a pin of the call sequence on a double. It is not a witness of what a TLS peer reads** (§2.6, statement 4; fixpp#596). **RED first on five arms and on the strand arm**, at "not on the wire" (the strand arm: the head transmitted on that arm of the strand fixture in every run, `ph8a_S_arms_by_stall.md`, "unpatched head: the fixture on a strand executor"). **The `FileStore` arm on the live transport is RED first too, at the reading inside `onLogout`** (predicted: at the head `onLogout` runs there after the Logout took its number); its "not on the wire" clause is expected green at the head without a stall (`ph8a_four_way.md`, first column), and NC-22's first arm is that clause's RED state. In the pull request's tree the arms were run at the head at the cells commit `5056543b` (the record, "Phase 8, second part"); what they did in the scratch copy is at the end of this row. If the sixth arm is not RED at that reading there: this block's rule for a prediction about the head **At the head, rehearsed in the scratch copy (v0.13; `ph8f_head_states_for_the_note.md`):** all seven arms RED, as labelled. The five arms and the strand arm fail at "not on the wire", at the clauses on `onLogout` and at "`LogoutSent` was never the session's state". The sixth arm fails at the reading inside `onLogout` only: its Logout is retained and not on the wire, and the number read inside `onLogout` is the Logout's plus one. |
| NC-22 | the `FileStore` arm on the live transport, forced into each order by the gate. First arm: the gate holds the submitting thread until the file pool is idle, so the store call's completion is queued first. Second arm: the gate holds the Logout's store job until the session's executor has run dry, so that at the head the end of the Logout step comes first. This hold keeps the order of the executor's jobs: a job handed after the held one stays behind it (the fixture, above) **Cells** (suite `CallbackSend`; v0.13): `ToAdminOfGracefulCloseStartsClose_Terminal_FileStore_LiveWrite_StoreCompletionFirst` (the first arm) and `ToAdminOfGracefulCloseStartsClose_Terminal_FileStore_LiveWrite_StoreCompletionLast` (the second). | NC-21's assertions, in both arms. Each arm also asserts that the gate acted, through a count the gate keeps, so that an arm whose gate held nothing does not pass. **First arm: RED first at "not on the wire" (predicted, P-13). Second arm: predicted green at the head on that clause (P-13).** Both are green after the change **The second arm's path guard** (v0.13): the holding job is running, and at least one job, the Logout's write, is handed to the file executor between the arming and the settle. The arm prints how many jobs were handed and does not assert the number, because the number follows a condition. Where the join cancels the Logout step while the write's job waits, asio hands the cancellation of the store's offload to the file executor as a job of its own, behind the write's: more than one job is handed. That is the head, where the terminal close ends the grace wait. With this amendment the join does not cancel the step of a cut Logout before the grace timer expires, and until then the write's job is the only one. **At the head, rehearsed in the scratch copy (v0.13; `ph8f_head_states_for_the_note.md`):** as P-13 predicts. The first arm is RED, at "not on the wire" among others. The second arm is green on that clause and RED at the reading inside `onLogout` only; its Logout is retained under its number. |
| NC-23 | NC-21's six arms with `reset_on_logout` set **Cells** (suite `CallbackSend`; v0.13): `ToAdminOfGracefulCloseStartsClose_Terminal_ResetOnLogout`, and it with the suffixes `_LiveWrite`, `_FileStore`, `_FileStore_LiveWrite`, `_NoStore` and `_NoStore_LiveWrite`. | NC-21's assertions. The teardown reset did not fire: the store still holds the Logout, and the next outbound number is the Logout's plus one, in the manager and in the store. **RED first on the five arms that transmit** (measured at the head: `ph8a_S_arms_by_stall.md`, "unpatched head", the `reset_on_logout` tables). **On the sixth arm, `FileStore` on the live transport, the clause "the reset did not fire" is a pin** (analysis F-2): at the head with no stall the reset does not fire there either (measured; the same tables). Its kill is the mutant "`logout_seen_` set on the cut route". That arm is RED first at the head through NC-21's reading inside `onLogout` (predicted). v0.11 gave this arm's RED state to an arm with the knob set that NC-22 does not have. **These arms are also the pin of the merge hazard with B28** (§2.6, "With batch B28"): on a tree that carries both changes the cut Logout's emit returns success, and a step that then fell through to B28's assignment of `logout_seen_` would fire the reset. They are run again on the merged tree by the pull request that merges second **At the head, rehearsed in the scratch copy (v0.13; `ph8f_head_states_for_the_note.md`):** as labelled. The five arms that transmit are RED: the Logout is on the wire and the reset fired, so that nothing is retained and the next outbound number is 1. The sixth arm is RED at the reading inside `onLogout` only: its Logout is retained, and the reset did not fire. |
| NC-24 | the condition on the store doubles of P-14. (a) More hops, on both transports; how many is the implementer's choice, and the double records that its call was still suspended when the terminal close had its turn. (b) Two doubles that hold their call until a cancellation reaches them or the cell releases them, run with a short `logout_disconnect_timeout_ms`, on the live transport: one returns `store_cancelled` and leaves the caller's cancellation state alone; the other resets the caller's cancellation state before it returns, as the lock sequence of the shipped stores does. Each records that a cancellation reached it **Cells** (suite `CallbackSend`; v0.13): (a) `ToAdminOfGracefulCloseStartsClose_Terminal_StoreWithMoreHops` and `ToAdminOfGracefulCloseStartsClose_Terminal_StoreWithMoreHops_LiveWrite`; the control, `ToAdminOfGracefulCloseStartsClose_Terminal_StoreThatReturnsAtOnce`. (b) `CutLogoutOnAStoreThatHolds_ReportsTheCancellation_LiveWrite` and `CutLogoutOnAStoreThatHolds_LosesTheCancellation_LiveWrite`. **A control cell beside (a)'s two arms** (v0.13): the same store double, with a call that returns without suspending, on the synchronous sink. | (a): NC-21's assertions. **RED first on the synchronous sink (predicted): at the head that arm records `LogoutSent`, and transmits the Logout where the double's call completes. On the live transport: what it did at the head in the scratch copy is at the end of this row.** If the synchronous sink's arm is not RED at the head: this block's rule for a prediction about the head. (b): the same assertions in both doubles: both closes complete with the mock clock standing, not before the short timeout has elapsed, on the clock and from the start instant that NC-28 names, and before two timeouts have elapsed from it, with the double released by the cell not before three timeouts (NC-28's relation, fixed before the cells commit); the completion time is read and recorded; a cancellation reached the double; not on the wire; nothing under the Logout's number; the next outbound number is the Logout's plus one; both closes success. **A pin of the bound (R-9) for a store of another kind;** what it did at the head in the scratch copy is at the end of this row. Its second double is what sees a lost cancellation: a step that still waited after its store call would leave that arm's closes incomplete while the mock clock stands. (b)'s clause on the number is stated for this branch; B28's rulings change it (§2.6, "With batch B28") **(a)'s control** (v0.13). The two arms assert as their path that the double's call was still suspended when the terminal close had had its turn. The control asserts as its path that its call had returned by then. The ending is the same in all three: NC-21's assertions. So the reading can say "no", and the ending does not depend on it. The control is RED first at "not on the wire", as NC-21's `MemoryStore` arm on the synchronous sink is. **At the head, rehearsed in the scratch copy (v0.13; `ph8f_head_states_for_the_note.md`):** (a), the arm on the synchronous sink: RED, as predicted. The Logout is on the wire and `LogoutSent` is recorded; the store does not hold the Logout. (a), the arm on the live transport, which v0.12 left open: RED. The Logout is not on the wire, and the store does not hold it; the arm fails at "the store holds the Logout under its number" and at the reading inside `onLogout`. The control: RED, at "not on the wire" and the clauses NC-21's `MemoryStore` arm on the sink fails. (b), which v0.12 left open: both arms RED. With the double that reports the cancellation, both closes complete at once, before the timeout has elapsed: the lower side. With the double that loses it, the Logout is on the wire, and the closes complete only after the cell has released the double, past the upper side. |
| NC-25 | controls. (a) The six arms of `CallbackSend.ToAdminOfGracefulCloseStartsNoClose`; its live arms gain a clause on the transport's calls. (b) A second graceful `close()` started in the Logout's `toAdmin`; the cell ends the wait as NC-8's second arm does. (c) A terminal close that waited earlier. `Session::open` refuses a session that is not `never_opened`, so a session has one life and no cell can reopen it. The setup, settled (round 4's O-7): in one `fromApp` the callback starts a graceful close first and then a terminal close. Both post, in that order, and the terminal one moves the count. The graceful close resumes first. On a `MemoryStore` and with no store it has no flush hook, so it runs the Logout step in that handler; the step reads a count that is non-zero and does not move, so the Logout is not cut. With no store the Logout is transmitted before the step first suspends. On a `MemoryStore` the store's leading post is queued before the terminal close has its turn, and the grace timer's handler can reach the executor only after that turn, so the store call completes and the transmit begins first, on both transports. Read; the cell was then green at the head in the scratch copy (the end of this row). The arms: `MemoryStore` and no store. Not `FileStore`: its flush hook suspends the graceful close before the Logout step, the terminal close escalates first, and no Logout is emitted **Cells** (suite `CallbackSend`; v0.13): (a) `ToAdminOfGracefulCloseStartsNoClose`, and it with the suffixes `_LiveWrite`, `_FileStore`, `_FileStore_LiveWrite`, `_NoStore` and `_NoStore_LiveWrite`. (b) `ToAdminOfGracefulCloseStartsClose_Graceful`. (c) `FromAppStartsGracefulThenTerminalClose` and `FromAppStartsGracefulThenTerminalClose_NoStore`. | **Controls: green at the head and after.** (a): as today: the Logout on the wire, the session in its wait at `LogoutSent`, the graceful close not completed while the clock stands, then success; and on a live arm, where the step's own deadline ended the wait, the transport's `close()` was not called directly and its `close_async()` was called once (predicted green at the head; green there in the scratch copy, the end of this row). (b): the Logout is transmitted once; both closes return the same result. (c): the Logout is transmitted once. That clause is positive on purpose: an arm in which no Logout is emitted cannot tell the mutant "the count is non-zero" from the correct code **At the head, rehearsed in the scratch copy (v0.13; `ph8f_head_states_for_the_note.md`):** all green, as labelled: (a)'s six arms, its live arms with their new clause on the transport's calls; (b); and (c)'s two arms, where the Logout is transmitted once. |
| NC-26 | the held-lock scene, on both transports. A `FileStore` session, Active. The gate's third mode is armed. The peer's order arrives; its inbound persist takes the store's lock and its job is the one the gate keeps, so the lock stays taken while the pool stays free. The test body starts a graceful close; its flush goes through the pool and completes. The Logout's `toAdmin` starts a terminal close. The cell lets the session's executor run a bounded number of handlers, takes a snapshot, opens the gate, and pumps under a bound derived from `logout_disconnect_timeout_ms` and well below it. If that pump misses, the cell advances the mock clock past the timeout and pumps once more, so that a failing run ends **Cells** (suite `CallbackSend`; v0.13): `CutLogoutBehindAHeldStoreLock` and `CutLogoutBehindAHeldStoreLock_LiveWrite`. | That the path was driven, each a fatal assertion before the others: one job is kept at the gate; **the held `on_inbound_frame` has not returned at the snapshot**, which makes the kept job the other writer's persist and so places the lock's holder (round 5's O-3); `toAdmin` started its close; in the snapshot the Logout's `store` call has been made (the recording wrapper) and `toAdmin` has returned. In the snapshot, as assertions of the ending and not fatal, so that the later ones are still read: no sleep is in flight on the session's clock; the session is `Disconnected` and `onLogout` has run once; the Logout's `store` call has not returned; neither close has completed while the lock is held. After the gate is opened, with the mock clock standing: both closes complete within the pump's bound. Then: the Logout is retained under its number; the store's next outbound number is the Logout's plus one; not on the wire; no live write begun; both closes success; `onLogout` once, at `Disconnected`; the held `on_inbound_frame` returns ok; the store's counter of its catches of `operation_aborted` reads zero (the check of the gate's third mode; the fixture, above; predicted), and the store's next inbound number is the peer's order's plus one (a consistency clause, which does not discriminate); on the live arm one direct `close()` and one `close_async()`. **RED first (predicted at the head, P-16; measured RED on S3 and green on S5 and S6).** It is the kill cell of statement 4's part on the join's waker (mutant D) **At the head, rehearsed in the scratch copy (v0.13; `ph8f_head_states_for_the_note.md`):** both arms RED, as P-16 predicts. A sleep is in flight on the session's clock; the Logout is on the wire and is not retained; `LogoutSent` is recorded; the closes do not complete within the pump's bound once the lock is free. |
| NC-27 | what arrives after the cut is decided, on the synchronous sink. **Two arms after the store call:** `MemoryStore` and no store; the Logout's `toAdmin` starts the terminal close and posts, twice over, a handler that delivers a Logout from the peer, so that it runs after the step's store call has returned and before the closes complete. **Twelve arms in the hold of the Logout's own store call:** the hold is armed inside the Logout's `toAdmin`. On a `FileStore` the cell holds the pool's one thread, so that the Logout's write job waits and the order of the executor's jobs is kept (the fixture, above); v0.10 held that job behind the gate. The other six arms use the holding store double. The cell waits until the call is held, reads the session, starts the arrival, lets the executor run a bounded number of handlers, reads again, releases the hold and pumps to the end. The arrivals, each on both stores: a Logout from the peer; the same with `reset_on_logout` set; an application message; a TestRequest; a `Session::send` from another coroutine (the send arm); a wake of the liveness loop, by advancing the mock clock by one heartbeat interval and one millisecond (the liveness arm) **Cells** (suite `CallbackSend`; v0.13): after the store call: `FrameAfterTheCutLogoutsStoreCall` and `FrameAfterTheCutLogoutsStoreCall_NoStore`. In the hold: `ArrivalWhileTheCutLogoutIsStored_`, then the arrival (`PeerLogout`, `PeerLogout_ResetOnLogout`, `AppMessage`, `TestRequest`, `Send`, `LivenessWake`), then `_FileStore` or `_HoldingStore`. | The oracle is split by kind of arm (round 6's F-2). An arm asserts the common group and the groups of its own kind, and no other. **Common, all fourteen arms.** Path, fatal: `toAdmin` started its close. Then: the session is `Disconnected` when the arrival comes, and `onLogout` has run, with the next outbound number then the Logout's own; no `fromApp`, `fromAdmin`, `toAdmin` or `toApp` follows the arrival; neither next number moves at the arrival; nothing is transmitted; at the end both closes have returned success, and the mock clock was not advanced after the arrival; where there is a store, the Logout is retained under its number, nothing is above it, and the store's next outbound number is the Logout's plus one. **The two arms after the store call.** Path, fatal: a frame was delivered, after the Logout's store call had returned and before the closes completed. These arms have no hold and no release. **The twelve hold arms.** Path, fatal, tested in this order (v0.13): the store call was held; on the store double, the double did not give its hold up before the release; no close had completed before the arrival; the Logout had taken its number at the start of the hold; in the liveness arm, the liveness loop was asleep at the start of the hold. The order decides which guard an arm stops at. At the head the arm with the peer's Logout, `reset_on_logout` and the holding double stops at "no close had completed before the arrival", as the other five arms on the double do. By then its reset has fired too: the cell reads the next outbound number as 1 at the start of the hold (its printed reading, `ph8f_chainV/cells-on-head/`). So the guard on the number would fail as well; it is tested later and is not the one reported. Then: neither close completes in the hold; after the release both complete. **Per arrival, in the hold arms.** A Logout from the peer, an application message, a TestRequest: the inbound delivery returns in the hold. With `reset_on_logout` set: no reset, read after the release. The send arm: the send returns `session_invalid_state_for_send` in the hold. The liveness arm: after its wake the liveness loop has no sleep in flight; the wake is this arm's one advance of the mock clock, and no other arm advances it. **Which arms are RED first.** Measured on the prototypes: the twelve hold arms are RED on S5 and the first two on S3, and all are green on S6 (P-17, P-20). Those are scratch trees, and the rule asks for RED in the pull request's tree, at the head. Predicted there, and so measured in the scratch copy (the end of this row): **the six `FileStore` hold arms are RED at the head, at "the session is `Disconnected` when the arrival comes".** The head's terminal close cancels the grace timer and the join cancels the step. The step is waiting for its own write job, and a cancellation does not end that wait. So the step stays there with the session Active, and the arrival is acted on. What the store shows at the head after the release is not predicted here: at the head a cancellation is emitted in the hold, so it depends on the order of the executor's two jobs (§2.6, "The bound", (a)); the implementer records it. **The six hold arms on the store double are predicted to stop at their fatal guard "no close had completed before the arrival" at the head, which is not RED:** the head's join cancels the step, the cancellation ends the double's hold, and both closes complete before the arrival, as under mutant D. Those six are green after the change and show the ending on a store with no lock held; they are not RED-first witnesses. The first two arms are predicted RED at the head, where the frame meets `LogoutSent`. So the clause "the state is written when the cut is decided" has its RED-first arms in the six `FileStore` hold arms and in NC-21's reading inside `onLogout`. The implementer runs every arm on the head first and records what each does there. If an arm does otherwise than predicted: this block's rule for a prediction about the head. Not covered: a live transport in these arms **At the head, rehearsed in the scratch copy (v0.13; `ph8f_head_states_for_the_note.md`):** as predicted. The two arms after the store call are RED at "the session is `Disconnected` when the arrival comes". The six `FileStore` hold arms are RED at that clause, and each arrival is acted on. What the label did not predict, as read: in all six the Logout is on the wire at the head, and in five it is retained under its number at the end. A Logout from the peer draws a confirming Logout numbered above it, stored and transmitted; with `reset_on_logout` set the reset then fires and empties the store, which is the sixth. An application message reaches `fromApp` and consumes its inbound number. A TestRequest, and the wake of the liveness loop, each draw a Heartbeat numbered above the Logout, stored and transmitted. The send is admitted, numbered above the Logout, stored and transmitted. The six arms on the store double stop at their path guard "no close had completed before the arrival", which is not RED. |
| NC-28 | the bound at the store's lock, a ruled behaviour (R-9). NC-26's scene on the live transport, with `logout_disconnect_timeout_ms` short and the gate kept closed well past it **Cells** (suite `CallbackSend`; v0.13): `CutLogoutBehindAHeldStoreLock_PastTheLogoutTimeout_LiveWrite`. | **A pin of a ruled behaviour.** With the gate still closed and the mock clock standing: both closes have completed, with success; the session is `Disconnected` and `closed_drained`; `onLogout` ran once, when the cut was decided; not on the wire; nothing under the Logout's number, and the store's next outbound number is the Logout's own; the session's next outbound number is the Logout's plus one; one direct `close()` and one `close_async()`. **The completion time of each close is read and recorded, and asserted on both sides:** not before the timeout has elapsed on `std::chrono::steady_clock`, counted from an instant that the cell reads on that clock right before it spawns the graceful close (round 6's O-6), and before two timeouts have elapsed from that instant. The cell keeps the gate closed until at least three timeouts after the instant. So a close that waits for the held job completes at three timeouts or later and fails the upper side, and the upper side leaves one timeout of room for a late timer. **Both sides are fixed before the cells commit** (analysis F-3), from the timeout and from the prototype's readings (P-18: with a timeout of 300 ms, every completion at 300 ms, and the gate opened at about 900 ms). Those readings are from a Debug build with AddressSanitizer; ThreadSanitizer and the other lanes were not measured. The later runs confirm the bound. If a lane does not complete inside two timeouts, the cell's timeout is raised and the relation stays, in a tests-only commit with its reruns. The lower side is in real time: the grace timer is a real timer, and the session's mock clock, which stands in this cell, is not the clock that is read. `close()` arms the timer after the start instant, when its flush has completed, and a timer does not fire early, so on correct code the lower side holds. The prototype's cell read the time and asserted neither side; the lower side is what makes this cell see a cancellation that comes before the expiry (under mutant D the reading was 0 to 1 ms). After the gate is opened the held `on_inbound_frame` returns ok, the store's counter of its catches of `operation_aborted` reads zero (the check of the gate's third mode; the fixture, above; predicted), and the store's next inbound number is the peer's order's plus one (a consistency clause, which does not discriminate). The clause on the session's next outbound number is this branch's; the pull request that merges second changes it (§2.6, "With batch B28"). In the pull request's tree it was run at the head at the cells commit `5056543b` (the record, "Phase 8, second part"). **At the head, rehearsed in the scratch copy (v0.13; `ph8f_head_states_for_the_note.md`):** RED; v0.12 had left it open. The Logout is on the wire and is not retained; `LogoutSent` is recorded; a sleep is in flight on the session's clock; the closes have not completed with the gate still closed, and they complete past two timeouts. |
| NC-29 | the Logout's own write job still in the file executor past a short `logout_disconnect_timeout_ms`, on a file executor that keeps the order of its jobs. `FileStore` on the fixture's pool of one thread, synchronous sink. Inside the Logout's `toAdmin` the cell holds the pool's thread (the fixture, above), so the write's job, and after it the job of any cancellation, queue behind the cell's job in that order. Two arms: the Logout cut (the terminal close started in that `toAdmin`), and the Logout not cut (a graceful close alone, a route this amendment does not change) **Cells** (suite `CallbackSend`; v0.13): `LogoutsWriteHeldPastTheLogoutTimeout_Cut` and `LogoutsWriteHeldPastTheLogoutTimeout_NotCut`. | **The cell's form was decided again in v0.11** (round 6's O-1). v0.10's cell held the write's job behind a gate that let the cancellation's job pass it, and it pinned the unreadable record which that order produces. A cell whose fixture reorders the executor's jobs is not kept. **Chosen: the order-keeping executor only, and no arm on two threads.** On it the cell can fail on what this amendment states: on the cut arm, R-8's ending reached late; on both arms, the timing of §2.6, "The bound", (a). The unreadable record needs a second thread and is reached on both routes, so it is not this amendment's. Its deterministic witness is the fixture with two threads and the write's thread delayed, which that store defect's issue names for its first acceptance step (§7). An arm of it here would pin a state this amendment does not change, and the change made for that issue would have to rewrite the arm. Path, fatal: the cell's job holds the pool's thread, and the Logout's write job was handed to the executor while it is held. **With the thread still held well past the expiry and the mock clock standing, in both arms:** no close has completed. In the cut arm also: the session is `Disconnected`, `onLogout` ran when the cut was decided, and nothing is on the wire. **After the thread is let go, in both arms:** each close completes, with success, and its completion time is read and recorded on the clock NC-28 names; the write's body ran once and the store's catch of `operation_aborted` did not fire (the two seams); the Logout reads back under its number; the store's next outbound number and the session's are the Logout's plus one. Cut arm: nothing on the wire. Not-cut arm: the Logout is transmitted once, after its store call returned. **A pin of a stated limitation, in the form of NC-12 and NC-14:** the logout timeout does not bound a close in this scene (§4, the limitation row of round 6's O-5). Measured on S6: `ph8e_predictions.md`, kind A, 20 of 20 on each arm. In the pull request's tree it was run at the head at the cells commit `5056543b` (the record, "Phase 8, second part"). The not-cut arm is predicted green there, because the amendment does not change that route. The cut arm is predicted RED first at "nothing on the wire", as NC-21's `FileStore` arm on the synchronous sink is: the head transmitted the Logout on that arm with every job of the file executor delayed (`ph8a_S_arms_by_stall.md`, "unpatched head"). If either arm does otherwise at the head: this block's rule for a prediction about the head **At the head, rehearsed in the scratch copy (v0.13; `ph8f_head_states_for_the_note.md`):** as predicted. The cut arm is RED at "nothing is on the wire", and at "the session is `Disconnected`" and "`onLogout` ran when the cut was decided" in the hold; its Logout is retained and reads back. The not-cut arm is green. |
| NC-30 | a `Session::send` that has its number and is suspended in its own store call when the cut is decided. The hold is armed by the test body before the send, so the held call is the send's; then the graceful close starts and its Logout's `toAdmin` starts the terminal close. `FileStore` and the holding store double, synchronous sink **Cells** (suite `CallbackSend`; v0.13): `SendInItsStoreCallWhenTheLogoutIsCut_FileStore` and `SendInItsStoreCallWhenTheLogoutIsCut_HoldingStore`. | **A pin of what statement 4's state write does not cover** (§2.6, "What arrives after the cut is decided"). Path, fatal: the send's store call is held and the send has its number; the cut was decided with the send held. Then: the send keeps its number, is transmitted when its store call returns, and returns success; the Logout has the next number, is retained and is not on the wire; nothing numbered above the Logout is on the wire; nothing is transmitted while the send's store call is held, and one frame is transmitted, the send's, when that call has returned (the two clauses as the cells have them; v0.13). On the `FileStore` arm, whose hold is the gate's third mode: the store's counter of its catches of `operation_aborted` reads zero, and the send's frame is retrieved under its number (the two checks of that mode; the fixture, above; predicted). In the pull request's tree it was run at the head at the cells commit `5056543b` (the record, "Phase 8, second part"). The prototype's cells assert the path only. Not covered: a live transport that refuses a write after its `close()` **At the head, rehearsed in the scratch copy (v0.13; `ph8f_head_states_for_the_note.md`):** both arms RED; v0.12 had left it open. They fail at "nothing is transmitted while the send's store call is held" and at "one frame is transmitted, the send's, when its store call has returned": at the head the Logout is transmitted during the hold, ahead of the send's frame and numbered above it. On the `FileStore` arm the store does not hold that Logout; on the double it does. So this pin is also RED first, at those two clauses. The send itself does at the head what the row says: it keeps its number and is transmitted when its store call returns. |
| NC-31 | what the application starts inside the decision-point `onLogout` (round 6's F-1). The condition of NC-21; inside `onLogout` the application starts one more operation. **Six arms, a terminal `close()`:** the three stores, each on the synchronous sink and on the live transport. **Three arms, a graceful `close()`,** and **three arms, a `Session::send`:** the three stores, on the synchronous sink. **Two arms with the `FileStore`'s lock held by another writer,** which is NC-26's scene on the synchronous sink, with the gate's third mode: one starts the terminal close in `onLogout`, the other the send **Cells** (suite `CallbackSend`; v0.13): `OnLogoutOfTheCutLogoutStartsTerminalClose`, and it with the suffixes `_LiveWrite`, `_FileStore`, `_FileStore_LiveWrite`, `_NoStore`, `_NoStore_LiveWrite` and, with the lock held, `_StoreLockHeld`; `OnLogoutOfTheCutLogoutStartsGracefulClose`, and it with `_FileStore` and `_NoStore`; `OnLogoutOfTheCutLogoutStartsSend`, and it with `_FileStore`, `_NoStore` and, with the lock held, `_StoreLockHeld`. | The oracle is split by kind of arm, as NC-27's is. **Common, all fourteen arms.** Path, fatal: `toAdmin` started its terminal close; `onLogout` ran with the next outbound number still the Logout's own, so it ran before the Logout was numbered; inside it the session was `Disconnected`. Then: the Logout takes its number and nothing else takes one; where there is a store, the Logout is retained under its number, nothing is above it, and the store's next outbound number is the Logout's plus one; nothing is transmitted; no `toAdmin` and no `toApp` follows the cut Logout's `toAdmin`; `onLogout` ran once; `LogoutSent` was never the session's state; the graceful close and the close started in `toAdmin` return success; everything completes with the mock clock standing; at the end `Disconnected`, `closed_drained`. **The arms that start a close:** the close started in `onLogout` returns success. On the live arms no live write was begun, and the transport's `close()` was called once directly and its `close_async()` once. **The send arms:** the send returns `session_invalid_state_for_send`, and no `toApp` ran for it. **The two arms with the lock held.** Path, fatal: one job is kept at the gate, and the held `on_inbound_frame` has not returned at the snapshot. In the snapshot: neither the graceful close nor the close started in `toAdmin` has completed; in the terminal-close arm the close started in `onLogout` has not completed either; in the send arm the send has returned its refusal; no sleep is in flight on the session's clock. After the gate is opened everything completes with the mock clock standing; the store's counter of its catches of `operation_aborted` reads zero (the check of the gate's third mode; the fixture, above; predicted), and the store's next inbound number is the peer's order's plus one (a consistency clause, which does not discriminate). In these two arms the common clauses "the Logout is retained under its number" and "the store's next outbound number" are what mutant D breaks, with "no close completes before the held write is released". **Printed and not asserted:** the count of terminal closes that took the wait, and the record of the cut, each read inside `onLogout` and at the end through the fixture's two accessors. **The twelve arms with no lock held are controls: green after the change.** At the head their path does not exist, because `onLogout` does not run there before the Logout is numbered. Their path guard is predicted to stop them at the head. That is not RED, and they are not RED-first witnesses, as NC-27's six arms on the store double are not. What they show is forced by the four statements and by tests of the state that exist at the head (round 6's triage, F-1). **The two arms with the lock held are a second kill of mutant D** (measured; `ph8d_failing_assertions.txt`). They cannot tell whose wake it was: under mutant D the close started in `toAdmin` already cancels the grace timer, and the send arm fails as the terminal-close arm does. What they pin is that, with statement 4, neither that close nor a close started in `onLogout` ends the grace wait. At the head they are predicted to stop at the same path guard. Measured on S6: P-23. Not covered: `Engine::send` and `Engine::stop()` from that `onLogout` (§7); a graceful close with the lock held; a live transport outside the terminal-close arms **At the head, rehearsed in the scratch copy (v0.13; `ph8f_head_states_for_the_note.md`):** all fourteen arms stop at their path guard, as predicted for the twelve and for the two with the lock held: `onLogout` did not run with the next outbound number still the Logout's own. That is not RED. |
| NC-32 | the exit after a failed `assign_outbound()` on the cut route (analysis D-1). The condition of NC-21, with the session's next outbound number set to its maximum before the graceful close starts, through `seqnum_manager_test_access::set_counters` on `session_test_access::seqnum_mgr`, as `LogoutExchangeTest.ActiveInboundLogout_SeqnumOverflow_SurfacesError` does. The Logout's `toAdmin` starts the terminal close; `fire_to_admin_` comes before `assign_outbound()` in `run_logout_phase1`, so the cut is decided and then `assign_outbound()` returns `store_seqnum_overflow`. One arm: `MemoryStore`, synchronous sink. The step returns before any store call and any transmit, so the store and the transport do not enter **Cells** (suite `CallbackSend`; v0.13): `ToAdminOfGracefulCloseStartsClose_Terminal_OutboundNumberAtItsMaximum`. | Path, fatal: the counter was at its maximum when the close began; `toAdmin` ran and started its close. Then: **the state history, read through `fsm_visit_history()`, holds one visit of `Disconnected`**; nothing is on the wire after the Logon; `onLogout` ran once; `LogoutSent` was never the session's state; both closes return success (predicted: `close()` discards the step's value, which here is `store_seqnum_overflow`). The two accessors of the fixture are not used. **A pin, predicted green at the head, and not a RED-first arm.** At the head this exit is reached from Active and writes `Disconnected` once, and `close()`'s own write after the join has the test "when the state is not that already". The second visit exists only with this amendment's write at the decision point and without the test at the exit, so no tree before the production commit can be RED here. **Its RED run is on the scratch patch without the test**, which is the mutant "the test at the exit after a failed `assign_outbound()` removed": prototype S6 has no such test. Both runs were made in the scratch copy (the end of this row). In the pull request's tree it was run at the head at the cells commit `5056543b` (the record, "Phase 8, second part"). If the head disagrees: this block's rule for a prediction about the head **At the head, rehearsed in the scratch copy (v0.13; `ph8f_head_states_for_the_note.md`):** green, as predicted. Nothing is on the wire, and both closes return success. Its RED run was made as well: on prototype S6, which has no test at the exit, the cell is RED in 5 of 5 runs, a plain failure at "the state history holds one visit of `Disconnected`", where two visits were read; with the test it is green (`ph8f_chainQ-results.txt` and `ph8f_nc32-on-prototype/` for the RED; `ph8f_chainR-results.txt` for the green). |
| NC-33 | the condition with `reset_on_disconnect` set: the ending ruled by R-10 (§0; analysis E-1). Two arms, `MemoryStore` and `FileStore`, on the synchronous sink. The Logout's `toAdmin` starts the terminal close, as in NC-21 **Cells** (suite `CallbackSend`; v0.13): `ToAdminOfGracefulCloseStartsClose_Terminal_ResetOnDisconnect` and `ToAdminOfGracefulCloseStartsClose_Terminal_ResetOnDisconnect_FileStore`. | After both closes have completed: **the store is emptied, and both next numbers are their initial value, in the manager and in the store** (read: the teardown reset calls `reset_seqnums_to_one_durable`). The oracle names the store's next inbound number for both arms: the fixture's helper reads that number also when the `reset_on_disconnect` knob is set (v0.13). Then: the Logout is not on the wire; both closes return success. That the Logout was stored before the reset is not asserted here; NC-21 pins the store call with the setting off. **The clauses on the store and on the two numbers are a pin of a ruled behaviour, as NC-28 is for R-9.** Predicted green at the head: the teardown reset of `Session::close` runs on every close with this setting, and the amendment does not touch it. Their kill is the mutant "the teardown reset not made when the Logout was cut". **The clause "not on the wire" is R-8's, and it is predicted RED at the head on both arms**: with the setting off the head transmitted the Logout on these two arms under every stall setting (`ph8a_S_arms_by_stall.md`, "unpatched head"). With the setting on it was run at the head in the scratch copy (the end of this row), and on no prototype. In the pull request's tree it was run at the head at the cells commit `5056543b` (the record, "Phase 8, second part"). If either prediction disagrees: this block's rule for a prediction about the head; a reset clause that is RED at the head would mean the reading of the teardown reset is wrong. Not covered: a live transport; the reset's wait for the store's lock, which no timer watches (§2.6, "The bound", (d)) **At the head, rehearsed in the scratch copy (v0.13; `ph8f_head_states_for_the_note.md`):** as predicted, clause by clause. Both arms are RED at "not on the wire" and at the clauses on `onLogout` and `LogoutSent`. The clauses of the reset hold: nothing is retained, and the next numbers are 1. |

*Arms that must go RED,* each run in a scratch copy and never in the pull request's tree, as above.
The last column says whether the row's outcome was measured. "Measured on S6's cells" means the
prototype's final cells, five runs per cell (`ph8c_predictions.md`, Q6, with
`ph8c_failing_assertions.txt`; for the cells added after it, `ph8d_failing_assertions.txt` and
`ph8e_failing_assertions.txt`). A cell that is not in the pull request's tree yet is named by its
id; NC-22 to NC-33 are such cells. A row marked "not run" has its kill cell named and its run owed
at implementation. The measured mutants are run again there, against the cells as they are written.
Status at 2026-10-14: the cells NC-22 to NC-33 are in the pull request's tree since `5056543b`,
under the names their rows give. The run of the mutants against the cells as written was not in
when this status was written.

| Mutant | Fails | Trace | Run |
|---|---|---|---|
| the increment removed from `close()` | NC-21, every arm: five arms and the strand arm at "not on the wire", and the sixth arm at the reading inside `onLogout` (analysis F-6). NC-22, first arm. NC-23, every arm, its sixth at that same reading. NC-33, at "not on the wire". And every other arm that is RED at the head (each row's last sentences, v0.13): NC-24's arms and (a)'s control, NC-26, NC-27's `FileStore` hold arms and its two arms after the store call, NC-28, NC-29's cut arm and NC-30's two arms | The count never moves, the Logout is not cut, and the head's behaviour returns: the Logout is transmitted on five arms, and on the `FileStore` arm of the live transport in the forced order | not run as a mutant; it is the head, measured |
| the increment placed after the wait's post | the same cells | The count moves when the close resumes, after `toAdmin` has returned and after the second read. The comparison sees no move | not run |
| the comparison forced to "not moved" | the same cells | As the first row | not run |
| the comparison forced to "moved" | NC-25 (a) and (b) | Every graceful close's Logout is cut: nothing is on the wire, the session never reaches `LogoutSent`, and the close completes at once where the control asserts that it waits | not run |
| the comparison replaced by "the count is non-zero" | NC-25 (c) | The count is non-zero from the earlier terminal close. The Logout is cut where "transmitted once" is asserted | not run |
| the count incremented for a graceful close too | NC-25 (b) | The second graceful close takes the wait inside the Logout's `toAdmin` and moves the count. The Logout is cut where "transmitted once" is asserted | not run |
| the parameter ignored in `store_then_emit`, so Step 2 runs | NC-21, every arm, at "not on the wire" | The cut Logout is stored and then transmitted. With statement 4 no cancellation reaches the step, so the live write is begun on the `FileStore` arm too | not run |
| the transmit test placed before the store call, so a cut frame is not stored | NC-21, the four arms with a store: "the store holds the Logout under its number" | The function returns before Step 1 | not run |
| `logout_seen_` set on the cut route | NC-23, every arm | The teardown reset fires: the store is empty and the next outbound number is 1 | measured as prototype S (`ph8a_S_arms_by_stall.md`, its `reset_on_logout` tables); not run on S6 |
| `LogoutSent` recorded on the cut route, after the emit | NC-21, every arm: "`LogoutSent` was never the session's state" | The session goes from `Disconnected` to `LogoutSent` after the Logout's store call | not run. Prototype S recorded `LogoutSent` on this route and its arms read so (the same file, its plain tables) |
| mutant E: the `Disconnected` write left after the Logout's store call, and not made at the decision point. It is prototype S5 | NC-27, the twelve hold arms, at the state in the hold and at what each arrival then does. NC-21, every arm, at the reading inside `onLogout` only | The session is Active while the Logout's store call is in flight. Each arrival is acted on: a callback, a number, a frame on the wire, a reset. `onLogout` runs after the Logout took its number | measured on S6's cells (`ph8c_scenes-S5/`): RED on all twelve hold arms and on the six arms. The six `FileStore` hold arms were behind the gate in that run. Not run on the order-keeping hold. Predicted the same there: under this mutant nothing emits a cancellation in the hold, so the executor has no second job whose order could matter (read) |
| mutant F: both writes of `Disconnected` removed from the step | NC-27, the twelve hold arms and the two arms after the store call. NC-21, every arm, at the reading inside `onLogout` only | `close()` writes the state itself after the join. Until then the session is Active, also after the step has left: the frame after the store call meets Active, and a confirming Logout is built and transmitted. At the six arms' other assertions the mutant is not seen, because `onLogout` still runs once at `Disconnected` | measured on S6's cells (`ph8c_mutant-F/`), with the six `FileStore` hold arms behind the gate. Not run on the order-keeping hold; predicted the same there, for the reason in mutant E's row |
| the later write of `Disconnected`, in the exit, removed | none | On the cut route the state is already `Disconnected`, and that write does nothing | no witness; listed below |
| the test at the exit after a failed `assign_outbound()` removed | NC-32: "the state history holds one visit of `Disconnected`" | The cut is decided and `Disconnected` is written at the decision point. `assign_outbound()` then returns an error, and the exit writes `Disconnected` again: two visits | measured in the scratch copy (v0.13). It is prototype S6, which has no such test: NC-32 is RED on it, 5 of 5, at that clause, with two visits read (`ph8f_chainQ-results.txt`), and green with the test |
| the teardown reset not made when the Logout was cut | NC-33: "the store is emptied, and both next numbers are their initial value" | With `reset_on_disconnect` set the store keeps the Logout and the numbers stay advanced, against R-10 | not run |
| mutant G: the record of the cut written after the state's write, the two statements swapped | **none, and none should** | `onLogout` runs inside the state's write, and a close started there waits by one post before it reads the record. So both statements have run before any reader of the record can | measured on S6's cells (`ph8d_mutant-G/`): all fourteen cells of NC-31 green; listed below |
| the cut step keeps its wait (v0.8's statement 4) | NC-26: "no sleep is in flight", "both closes complete with the mock clock standing", "the Logout is retained". NC-27, its two arms after the store call | The join's one cancellation meets the store call at the lock and is lost; the step sleeps out the timeout | measured as prototype S3 (`ph8b_held_lock.md`; `ph8b_predictions.md`, P5) |
| mutant D: the test of the record removed from the wake, so a terminal close that joins cancels the grace timer for a cut Logout too; the wait kept | **NC-26, both transports**: "the Logout is retained under its number", and "neither close has completed while the lock is held". It is the kill cell of statement 4's part on the join's waker. **NC-31's two arms with the lock held**: "retained", "the store's next outbound number", and "no close completes before the held write is released". A second kill, which cannot tell whose wake it was (NC-31's row). NC-28: predicted RED at the lower side of its completion time (the prototype's reading under this mutant was 0 to 1 ms; the prototype's cell asserted no lower side and stayed green). **The held-lock cells are this mutant's only kill cells.** **NC-27's six `FileStore` hold arms stay green under it,** measured on the order-keeping hold: no close completes in the hold, and the Logout is retained, as without the mutant. **NC-27's six arms on the holding double stop at their fatal guard "a close had completed before the arrival": not a kill**, because under this mutant the path of those arms does not exist. NC-21, NC-23, NC-27's two arms after the store call, NC-30 and NC-31's twelve arms with no lock held stay green: they cannot see it. NC-29 was not run under it | The terminal close's turn comes after the join has started the timer's wait, so the cancel ends that wait, and the join cancels the step. Where the Logout's store call waits at a held lock, the cancellation is taken at once and the call returns without writing: that is the kill. Where the step waits for its own write job, the cancellation does not end the wait. With the order of the executor's jobs kept, the write then runs and the store call returns its result, so the ending is the one without the mutant. **v0.10 counted NC-27's `FileStore` hold arms as a kill of this mutant.** In their gated form they were RED under it, at "retained", with the read returning `store_io_failure`. That was the gate's reordering: it let the cancellation's job pass the write's job it kept (§2.6, "The bound", (a)). It is a kill in that fixture only, and it pins nothing about this mutant on an executor that keeps order | measured on S6's cells. `ph8c_mutant-D/`: NC-26's two cells RED; the gated arrival cells RED, by the gate's reordering. `ph8d_mutant-D/`: NC-31's two arms with the lock held RED, 5 of 5; its twelve others green. `ph8e_mutant-D/`: the arrival cells with the order kept green, 5 of 5 each; the gated ones RED again |
| the direct close made only when the timer's arm won, as today | NC-21, the three live arms: the transport's close counts | With statement 4 the step's arm wins on every arm, so no direct call is made: one `close_async()` and no direct `close()` | not run. Prototype S2, which keyed the call that way, showed one call in place of two on two live arms (`ph8a_S2_arms_by_stall.md`) |
| the direct close made on every route, cut or not | NC-25 (a), its live arms: "the transport's `close()` was not called directly" | A graceful close whose step ended by itself gets a direct close too | not run (round 5's O-5) |
| the wake suppressed for every Logout, cut or not | the `InboundFrameDispositionsOd29.*` cells, which pin a terminal close that ends the grace wait of a Logout that was sent; and `CallbackSend.OnLogoutOfGracefulCloseStartsClose_Terminal`, where the terminal close is started in that Logout's `onLogout` (analysis F-6) | The escalation of 093's OD-29 is lost for every graceful close: the terminal close waits out the timeout | not run (round 5's O-5) |
| the record of the cut never written | NC-26: "retained", as mutant D. NC-21's close counts, on the live arms where the step's arm wins the join | Mutant D and "the direct close made only when the timer's arm won" at once | not run. Which live arms lose the direct call was not read to a conclusion; S2 lost it on two |
| mutant A: the post of `close()`'s wait and its assertion removed | **no cell of §2.6 fails, and none should.** NC-21's arms, NC-26, NC-27 and NC-28 stay green with the unmutated readings | The close runs on inside `toAdmin`, takes the joining branch before the record of the cut is written, and calls the wake. At the asio version read the call completes nothing (§2.6, "What it depends on", with its reservation). It is a mutant of R-4's wait, not of a statement of §2.6. Its kill cells are v0.7's, in the row "the close's wait removed" above, where the close is the session's first; on this route the lifecycle is `closing` inside `toAdmin` with or without the wait (the record, "Phase 4, addendum") | measured on S6's cells (`ph8c_mutant-A/`), and on S5's before |
| mutant C: `close()`'s wait posts many times | NC-21, every arm, and NC-27's two arms after the store call: "the terminal one returns the graceful one's result" | The terminal close's first turn comes after the graceful close has finished; it finds `closed_drained` and returns `session_already_closed`. NC-26 and NC-27's hold arms stay green, because there the graceful close is still in flight at that turn. It is a control of the cells, not a mutant of a statement of §2.6: it shows that they can report the other result | measured on S6's cells (`ph8c_mutant-C/`) |

Specified without a witness, and claimed as such:

- **The transmit test after the OD-25 `arm` test and not before it.** The Logout step passes no
  `arm`, and the Logon arms pass no "do not transmit". The two orders differ in no cell.
- **The time of the last outbound frame not updated for a cut frame.** The session closes at once on
  this route, so no heartbeat cadence reads it afterwards.
- **The increment inside the wait's block and not before it.** A terminal close that takes no wait
  does not begin inside a callback, and the Logout step does not suspend between its two reads. So a
  count that such a close also moved would change no cell.
- **The record of the cut and the state's write before the step's first await, as one synchronous
  stretch.** The await that follows them is `assign_outbound()`, which does not suspend on a free
  lock. No cell holds that lock there, so no cell separates "before `assign_outbound()`" from "right
  after it". The reading inside `onLogout` does separate both from "after the store call".
- **The order of those two statements between themselves.** Measured green: with the record of the
  cut written after the state's write (mutant G, `ph8d_mutant_G.patch`), every cell of NC-31 is
  green, five runs each. `onLogout` runs inside the state's write, and a close started there waits
  by one post before it reads the record, so both statements have run before any reader of the
  record can (round 6's triage, F-1; `ph8d_predictions.md`, "Why G changes nothing a caller can
  see"). The one reading that differs is the record's value inside `onLogout`. The cells print it
  and do not assert it: an assertion there would pin an order that no caller can tell from the
  other.
- **The later write of `Disconnected`, in the step's exit.** It does nothing on the cut route and is
  kept so that the exit stays as B28's decision D-OUT-6 writes it.
- **The count's width.** A wrap between the two reads needs as many terminal closes started inside
  one `toAdmin` as the member can count, each holding a suspended frame. Not reachable (round 4's
  F-6), and no cell is written for it.

*The stall probe again.* After the change no observable of the six arms separates the two orders
(§2.6). So the probe of Phase 4 is run again on the implementation, in a scratch copy: the six arms
under the three settings, with its positive control, which must flip, and its stall line, which must
be printed on both `FileStore` arms in every stalled run. NC-22 forces the orders inside the pull
request's tree; the probe shows that the unforced arms were exercised in both.

*Size pins.* `Session` grows by two members. Before the first build, search for pins of its size and
of whatever holds one:

```bash
command grep -rnE 'static_assert\(.*sizeof|sizeof\([^)]*Session[^)]*\)' src include tests bench bindings
```

The search is trusted only while it still reports a pin known to be there
(`src/capi/decimal_assert.cpp` has one). A full build with `-k 0` then finds what the search missed.

*What is run again.* `store_then_emit` is on every emit. B35's zero-allocation gates on
`Session::send` and on the Active inbound read are run again on the tree that carries the change,
because the function's coroutine frame can grow by the new parameter (§0a). So are all three paired
benches of "Cost": `engine_send_bench`, `on_inbound_frame_bench` and `session_send_bench`, by the
method and the rule that the paired run of "Cost" fixed in advance (analysis C-3). There are two
reasons: `store_then_emit` gains a parameter on every emit, and `Session` grows by two members,
which every bench that holds a `Session` meets. v0.11 named two of the three, sorted them by whether
their timed part reaches `store_then_emit`, and left the growth out. The sanitizer runs cover NC-21
to NC-33 as they cover the other close cells; a ThreadSanitizer run and a release build of them are
owed, because the prototypes had neither. The cells that pin 093's OD-29 are run again:
`InboundFrameDispositionsOd29.EngineStopDuringAGracefulLogoutClosesAtOnce` pins a terminal close
that arrives while a Logout that was sent is in its wait, and it must stay green, because that
Logout is not cut. The cells that read `onLogout`'s position on a graceful close with no close in
its `toAdmin` must stay green too: the state's write at the decision point is made on the cut route
only. `CallbackSend.OnLogoutOfGracefulCloseStartsClose_Terminal` is one of them. Its Logout is not
cut, so a terminal close started in its `onLogout` still ends the grace wait. In the scratch
rehearsal its six arms, and the cells of `InboundFrameDispositionsOd29`, were green at the head and
on the final patch (`ph8f_head_states_for_the_note.md`, its last part).
The parent repository's task list names every run that is made again at the head that carries the
change (its Phase 8 block; the record's "Phase 7, seventh part" says why).

## 4. Disclosures, documentation and catalogue

Round 1 found v0.3's list short, because it was written from the rows its author had in mind. This list
is derived by search. The searches are recorded so that the next round, and the implementer on the day,
can run them again; the table below is what they returned and is the thing to distrust. What round 2
found missing is not reachable by a search for old wording; those rows are in the table, marked "(read)".

**The searches.** Run from the library root, in bash:

```bash
roots="brain .specify spec include src tests bindings"
skip="--exclude=563-564-callback-send.md --exclude=test_563_564_*"
g() { command grep -rnIiE "$1" $roots $skip; }      # the live tree
f() { command grep -rnIiE "$1" specs; }             # the frozen bundles, for the brain flags

# A. The route of Engine::send
g 'two[- ]hop|two .{0,14}hops|(→|->) ?control ?(→|->)|control[- ]strand hop|send.{0,60}control[_ -]strand|control[_ -]strand.{0,60}send|queue up on the control|keepalive from the registry|registry .{0,30}keepalive|posts onto'
# B. A send started inside a callback
g 'behind the current dispatch|enqueued? behind|not\*? run before|runs inline|ran inline|run inline|every send suspends|re-?entrant (send|call|engine)|nested (send|in .?toApp)|send nested|from inside (a |an |the )?(.?toApp|.?fromApp|application callback|callback)'
# C. The guard
g 'callback_dispatch_scope|in_dispatch_|dispatch_guard|concurrent session callback entry|callback scope|T019 note'
# D. The rows and issues this PR changes
g 'L-544-3|L-544-4|B-019-3|B-023-2|L-023-2|#563|#564'
# E. The engine-stop flag and its readers
g 'engine-stop flag|engine_stop_requested_|note_engine_stop'
# F. A close started inside a callback
g 'clos(e|es|ed|ing)[^.]{0,50}(inside|within) (a |an |the |its )?[^.]{0,20}callback|onLogout[^.]{0,60}(inside|inline)|(inside|inline)[^.]{0,60}onLogout'
# The same families over specs/, for the list of brain flags
f 'two[- ]hop|(→|->) ?control ?(→|->)|behind the current dispatch|enqueued? behind|callback scope|callback_dispatch_scope|every cross-thread entry point|dispatch.-that-runs|registry snapshot'
```

Some populations are not reachable by a search for old wording, and are found by reading:

- **The doc comment above each declaration whose behaviour changes.** `grep -n "close(\|> send(\|> store("
  include/fixpp/session/session.hpp include/fixpp/session/engine.hpp
  include/fixpp/session/message_store.hpp`, then read upward from each declaration.
  `include/fixpp/session/application.hpp`'s comments on every callback are read in full.
- **Documents that a new requirement makes incomplete without making any sentence of theirs false.** Read
  outward from the requirement. For R-6: the design document `.specify/api-contract.md` §9 names for
  `MessageStore`. For R-7: every place that defines the `direct_executor` attestation
  (`grep -rnI "already_serialized_executor" include spec .specify brain`, read each).
- **Statements that change because a row moves.** When a B&L row moves to the closed file, search for
  its id and for any range that spans it (`L-544-1`…`L-544-4` is written as a range on a brain page).
- **Statements made false by a result, not by a wording.** The `B35SendSlot` cell that asserts a nested
  send ran inline was found through family B's "ran inline"; any other test that asserts today's nesting
  in `toApp` or `toAdmin` fails when the implementation lands, and the full suite run finds it.

Before trusting an empty result from any family, check that it still reports the site named for it here:
A, `Engine::send`'s doc comment in `include/fixpp/session/engine.hpp`; B, the same comment's "enqueued
behind" sentence; C, the comment on the inbound overload of `parse_and_dispatch_` in
`src/session/session.cpp`; D, `brain/components/session.md`; E, `src/session/session_engine_access.hpp`;
F, the fixpp#518 block comment in `tests/session/test_session_plaintext_roundtrip.cpp`. Once the PR has
rewritten a site, its family is checked against the base tree instead (`git grep` at the merge base).

**What the searches and the reading returned, by file, and the action for each.**

| File | What its hits say | Action |
|---|---|---|
| `include/fixpp/session/engine.hpp` | `Engine::send`'s doc comment: "posts onto the session's strand", "captures a shared_ptr keepalive from the registry", "enqueued behind the current dispatch", "before the first hop" | Rewritten in place to §2.3's path and §2.1's rule |
| `include/fixpp/session/session.hpp` | `close()`'s doc comment, its PRECONDITION paragraph included ("the first close runs to completion … before any second close() call can reach the `closing` branch") | Rewritten in place: a close started in a callback waits (R-4), so another close can run first |
| the same | `send()`'s doc comment and the `send()` line of the reentrancy contract | Rewritten in place: R-1's rule, the payload copy, the two new results |
| the same | the comments on `callback_dispatch_scope` ("zero overhead" in release; "no two callbacks enter concurrently"), on `dispatch_app_callback`'s guard and its lifetime precondition, and on `in_dispatch_` | Rewritten in place to §2.2: the token, the two depths, the assertions, and what compiles out |
| the same | (read) the reentrancy contract says nothing about a callback that runs the session's executor | R-7's sentence added: a callback must not run the session's own executor |
| `include/fixpp/core/session_executor.hpp` | (read) the comment on choosing a threading mode lists "a single-thread `io_context`" as an executor the attestation fits, with no condition | R-7's sentence added there |
| `include/fixpp/session/session_config.hpp`, `include/fixpp/core/error.hpp`, `include/fixpp/core/trace_context.hpp`, `.specify/2l-tap.md`, `.specify/2j-controlplane.md` | (read) they name the attestation knob: when it is required, the error for its absence, an example with it off, and two design documents that mention it in passing | No change: none says what the attested executor may do inside a callback |
| `include/fixpp/session/session.hpp` | the comment on `send_fallback_leaf_` ("a send nested in toApp") and on the engine-stop flag ("`logon_arm_superseded` reads it") | Rewritten in place: a send deferred from `toApp`; a second reader, `Engine::send`'s hit path |
| `include/fixpp/session/message_store.hpp` | `store`'s doc comment says nothing about concurrent calls | R-6's requirement added |
| `include/fixpp/session/application.hpp` | the callbacks' comments say nothing about a send or a close started inside them | A sentence per callback kind added, from §2.1's and §2.1a's tables |
| `include/fix/c_api/session.h` | the blocking thunk "posts onto the same strand" | No change: it describes the C ABI's blocking call, which this PR does not change. The file is not reformatted |
| `src/session/engine.cpp` | the block comment above `Engine::send` ("two-hop design", "session→control→session") and the comments in its body | Rewritten in place. The file's header comment names this note as superseding 023's two-hop send, so a reader who starts at the code is routed here |
| the same | `stop()`'s step-1 comment: the engine-stop flag "which Logon arms test" | Rewritten in place: the second reader |
| `src/session/session.cpp` | the comment on the inbound overload ("callback_dispatch_scope asserts it") | Rewritten in place: it names the parse-site assertion, and `validate_inbound_`'s comment names it too |
| the same | `open()`'s comment at `onCreate` (the scope is dropped "so onLogout … can acquire its own … without hitting the re-entrancy assert") and the "T019 note" comments at the inbound sites | Rewritten in place: a nested scope is legal on one thread; what those sites now rely on is the depth being back to its entry value |
| `src/session/session_engine_access.hpp` | the engine-stop flag "Logon arms test" | Rewritten in place; the reader OD-3 needs is added beside the setter |
| `spec/behaviors-and-limitations.md` | `B-019-3` ("posts onto the per-session strand", "enqueued behind the current dispatch") and `B-023-2` ("`send` traverses caller→control→session") | Each rewritten in place |
| the same | `L-023-2`, whose title is "No dedicated `Engine::send` two-hop / establish-churn perf micro-bench" and whose text defines its subject as "the two-hop send (`caller→control→session`)" | Rewritten in place and kept open, for its establish-churn half only (OD-5). What goes: the two-hop send as a subject, and the sentence that a send micro-bench "would be dominated by TLS-loopback setup". What is added: the send route is measured by `bin/engine_send_bench`, a `paired` row, since this PR, on one thread; the wake-up of an io thread by a send started on another thread is not timed by a gated bench. What remains, as the row's subject and its follow-up: no bench times the snapshot's republish on session establishment and teardown, and the recorded fact that the snapshot atomic is not lock-free on the supported standard libraries (`B-023-3`). Status stays "follow-up" |
| the same | (read) `L-007-2`: a `direct_executor` attested over a non-serialising executor is undefined in release and asserts in debug | Widened in place (R-7): same-thread re-entry, a callback that runs the session's own executor, is named beside concurrent entry, with the same disposition and status |
| the same | `L-050-6`, which lists "`Engine::send`'s control-strand hop and session-strand hop" in the chain a C-ABI send awaits | That clause rewritten in place |
| the same | `L-544-3`, `L-544-4` | Moved to the closed file |
| the same | `L-497-1`: "a send started from inside `toApp`" takes the fallback leaf | That clause rewritten in place: a send deferred from `toApp`, on §2.1's condition |
| the same | `L-544-2`: the `Session` must outlive a send's frame | A sentence added: the rule is now reachable from `close()`'s wait where `send_impl` awaits a terminal close (§2.1a) |
| the same | `L-015-4`: names `dispatch_guard` and `in_dispatch_` | Rewritten in place to the members §2.2 leaves |
| the same | `B-093-11`, its "`Engine::stop()`" bullet: the flag the Logon arms test | A sentence added: the second reader |
| the same | `L-054-1`, `B-050-3`, `B-055-2` | No change: the blocking C-ABI calls from inside a callback still deadlock, and Python's refusal stays |
| `spec/feature-catalogue.md` | 019's row: "`Engine::send` is any-thread (posts onto the executor, registry `shared_ptr<Session>` keepalive)" | Rewritten in place; this PR gets its own entry |
| `brain/components/session.md` | the fixpp#544 section: "every send suspends after its frame is built", "a send started from inside `toApp`, which runs inline", and "Two pre-existing defects this work surfaced, not fixed by it" (`L-544-3`, `L-544-4`) | Rewritten in place. The first was already false for a session with no store and a synchronous sink (measured, §1) |
| the same | "What was rejected": the engine-stop flag is "read only inside `logon_arm_superseded_`" | Rewritten in place: the second reader |
| the same | (read) the fixpp#544 section's pointer to B&L: "`L-544-1`…`L-544-4`; what stays open is `L-497-1`" | Rewritten in place: `L-544-3` and `L-544-4` are in the closed file after this PR |
| the same | — | A new section for this decision: R-1 to R-7, the OD-5 and OD-6 rulings, what was rejected (§5), and the flags below |
| `.specify/2d-threading.md` | "Non-reentrant from inside a callback … the send does *not* run before `fromApp` returns"; and (read) its notes on the `direct_executor` attestation, which say the executor is used "as if it were running under a strand" and name no callback that runs it | An erratum appended at the end, for the first passage and for R-7's sentence. The document is cited by section elsewhere, so nothing is inserted |
| `.specify/2e-msgstore.md` | (read) the design document of `MessageStore`. Its `store` contract says concurrent calls "serialise against the per-instance writer mutex" and that under the single-domain discipline "contention is zero"; the same sentence is in its `MemoryStore` concurrency bullet | An erratum appended at the end: R-6's requirement, and that contention is not zero, because two sends of one session overlap in their store calls. §2.1's "the interface did not say so" is about the header; this document said how the shipped stores behave, not what an implementation must do |
| `.specify/544-hot-path-zero-alloc.md` | §2.5 and its "As built" section: a send started in `toApp` "runs inline"; `Engine::send` "makes two `co_spawn` hops, one through `control_strand_`"; fixpp#563 and fixpp#564 as open follow-ups | One erratum appended at the end, naming those passages by section. The note is a closed PR's plan and is not edited in place |
| `.specify/2i-capi.md`, `.specify/2j-controlplane.md`, `.specify/2m-pybind.md`, `.specify/2f-async-mutex.md` and its draft | C-ABI thunks that post onto the session strand; Python's rules for calls from a callback; the mutex's re-entrant drain | No change: none describes `Engine::send`'s route or a C++ send started in a callback |
| `tests/session/test_544_send_slot_cells.cpp` | the header comment ("runs inline … fixpp#563, out of scope"), the nested cell's skip citing fixpp#564, and its "ran inline" assertions | The cell is rewritten (NC-17) and the comment with it |
| `tests/session/inbound_frame_dispositions_test.cpp` | the I-3 cell's comment ("callback_dispatch_scope asserts that no second callback enters") and its expected message | The comment rewritten in place, and the expected message edited to the parse-site assertion's text (§2.2) |
| `tests/session/test_engine_session_strand.cpp` | V-15's comment and body comments (sends "queue up on the control_strand_", "the full two-hop"); the file header's "(session→control→session)" | Rewritten in place to V-15's restated subject (§3) |
| the same | (read) V-9's comments on its sub-cell (a): "Re-entrant Engine::send from inside fromApp", and that its post mirrors a T012 guarantee | Rewritten in place: the sub-cell posts and then sends to the other session; after R-2 that send takes the hit path |
| the same | lines marked "Pre-T012" or "Historical" | No change: they describe a past state and say so |
| `tests/session/test_application_strand.cpp` | Test 4's prose ("enqueued behind the current dispatch", "The spec says Engine::send posts onto the strand") and the comments that the scope "asserts !in_dispatch_" | Rewritten in place: the cell sends from the test body after the callback; the guard is the token |
| `tests/session/test_application_engine_send.cpp`, `tests/session/test_application_outbound.cpp` | the re-entrant cells' prose ("posted behind the current dispatch", "releases in_dispatch_", "must enqueue behind the current dispatch") | Rewritten in place: the deferral in these cells is the test's own post |
| `tests/session/test_application_engine_send.cpp` | (read) `SendDrainRaceNoUAF`'s comments: its hook "posts stop()", and its `total` emission "cancels Engine::send's inner coroutine" | Rewritten in place to what the cell does (§3): the hook starts `stop()` inline, and the emission is dropped by the chain's filter. The cell's assertions are not changed; fixpp#579 inherits it |
| `spec/coverage-index.md` | 023's entry records "send micro-bench → L-023-2" | That clause gets a pointer in place: the send route's bench was delivered by this PR, and `L-023-2` now tracks establish-churn. This PR gets its own entry |
| `bench/ci-suite.txt`, `bench/session/CMakeLists.txt` | — | The target of §3 ("Cost") and `bench/session/engine_send_bench.cpp`, which is new, in a commit of their own before any production edit; the manifest row in a later commit, after the local run (§3, "This PR's evidence") |
| `.specify/ci209-bench-gate.md` | (read) the bench gate's design record. Its census names the binaries of the paired tier and is already behind the manifest, and its promotion rule asks a binary added to that tier for a history of same-tree spread | An erratum appended at the end, in the commit that adds the manifest row: `engine_send_bench` joins the paired tier as a row born `paired` (`[const §VIII.2a]`, OD-5), on the local spread check of §3 ("Cost") and without that history. The census is re-derived by the commands the record gives, not copied |
| `ci/test-bench-gate.sh` | (read) `MANDATORY_PAIRED`, the floor that keeps a `paired` row of the shipped manifest from being downgraded | `engine_send_bench` is added to the list, in the commit that adds the manifest row |
| `tests/session/test_session_plaintext_roundtrip.cpp` | the fixpp#518 block comment: a close started by `co_spawn` in a callback "would run close() inline inside the callback" | Rewritten in place: after R-4 it would wait. The cells keep their `asio::post` and are unchanged in behaviour |
| `tests/session/test_business_messages_roundtrip.cpp`, `tests/interop/test_business_message_interop.cpp`, `tests/interop/conversation/conv_cell_test.cpp` | "re-entrant `Engine::send` from inside `fromApp`", with the note that the call is hoisted by `asio::post` | No change: each says that it posts |
| `tests/session/test_application_throw.cpp`, `test_direct_executor_reentrancy.cpp`, `test_executor_compat.cpp`, `test_019_application_compile_smoke.cpp`, `test_credential_store_redaction.cpp` | they name `in_dispatch_`, `dispatch_guard` or the scope | Read when §2.2's members are named; a comment that names a member which no longer exists is rewritten in place |
| `tests/session/CMakeLists.txt` | the comments on the two B41 targets ("reproduction", "measurement") | Rewritten when the cells are lifted. Its other hits describe the posting suites above and do not change |
| `tests/session/engine_reset_unit_stop_test.cpp`, `tests/session/inbound_frame_dispositions_test.cpp` (its engine-stop cells), `tests/support/session_test_access.hpp` | they set or read the engine-stop flag through the access classes | No change: none says who else reads the flag |
| `src/session/session.cpp` | "onLogout fires inside record_state_transition_" | No change: still true |
| `bindings/python/fixpp.i`, `include/fixpp/core/sync/async_mutex.hpp`, `tests/sync/`, `tests/wire/validator_per_version_test.cpp`, `tests/session/test_544_session_strand.cpp`, `test_persistent_seqnum_hydrate.cpp`, `test_file_store_offload_thread.cpp`, `test_cancellation_parse_to_fromapp.cpp` | hits on the patterns' words in another sense (the mutex's re-entrant drain, FIX `NoHops`, an executor's `blocking` property, Python's blocking-call rule) | No change |

**Frozen bundles are not edited.** `brain/components/session.md` flags each bundle the last search finds
in `specs/`, naming the passages below. The search also reports those bundles' plans, tasks, checklists
and quickstart, which repeat the same statements; the flag is on the bundle and covers them.

- `specs/019-app-callbacks`: FR-006, its "Re-entrant send from within a callback" edge case, research D6.
- `specs/023-engine-session-strand`: FR-012, contract C-0's "every cross-thread entry point enters the
  control-plane domain", C-2's "never an inline `dispatch`-that-runs", data-model E-0, research D0's send
  route. 023's research weighed "Lock-free registry snapshot for `send`" and deferred it "as a possible
  optimization … revisit only if the bench gate fails"; R-2 adopts it for a different reason, and the page
  says so.
- `specs/093-inbound-frame-dispositions`: contract C-3 I-3 and the research line that "the callback scope
  asserts against nesting". I-3 still holds. Its assertion's carrier changes from the callback scope to
  the depth assertion at the parse sites, and the flag says that.

**B&L, live file: new rows.**

- **`Session::send` started inside a callback (R-1):** the table of §2.1 as an application observes it;
  that the payload is copied when the send is deferred; the two new results, with the sentence that
  `dispatch_aborted` no longer implies `Disconnected` and a caller reads `state()`; and that a send
  started while the session answers the peer's Logout, in the confirming Logout's `toAdmin` or in the
  `fromAdmin` that follows it, is numbered after our Logout (§7), with the outcome written from P-9's
  measurement.
- **`close()` started inside a callback (R-4):** it begins after the callback returns; which callbacks
  count; the departure from QuickFIX C++ and J, that `onLogout` follows the callback.
- **A limitation of that rule**, stated as conditions, with each outcome written from §3's measurement
  and not from this note's prediction:
  - the arm that called the callback can resume on a closing or closed session, wherever it suspends
    after the callback (a persistent store; any arm that answers the frame);
  - a terminal close started in `toApp` or `toAdmin` can leave the outer frame retained and not
    transmitted, its send in error (P-1, measured for `toApp`);
  - with `reset_on_disconnect` set, the outer frame's write can reach a transport that is already
    closed. Where the transport refuses it, nothing is transmitted and the send returns
    `dispatch_aborted`, and the teardown reset leaves the store empty with both next numbers at their
    initial value (P-1a, measured for `toApp`);
  - frames already read with the frame whose callback asked for the close can be processed before the
    close begins, on a volatile store, which does not suspend the arm (P-3, measured on a
    `MemoryStore`). On a persistent store the arm suspends in its persist, the close runs there, and the
    later frames' callbacks do not run (measured on a `FileStore`, the same row);
  - a close started in `onCreate` on an initiator that an application drives itself ends the session
    after its Logon has been transmitted, with no Logout (P-5, measured). Under the Engine an
    application has nothing to close with in `onCreate` (§2.1a);
  - on a TLS transport a write can begin while the transport's `close_async` is suspended; what the
    transport does with it is not measured (§7).
- **`Engine::send` through the snapshot (R-2):** the hit path and the condition on the caller under which
  it nests (§2.3); the cold path and what it returns; OD-3's stop check, which replaces the control-strand
  read and is witnessed by a construction, not by the real interleaving (NC-10); the narrow class of
  sends racing `stop()` that is refused today and can be admitted afterwards (§2.4); and that on the
  fallback `atomic_shared_ptr` every `Engine::send` takes one shard lock for the snapshot load.
- **The guard (§2.2):** which nestings on one thread are legal; that entry from a second thread still
  asserts; that a parse over the session's buffer from inside any callback asserts, an outbound
  callback inside an outbound callback asserts, and `onLogon` or `onLogout` entered inside an outbound
  callback asserts; the nestings that asserted before and are no longer detected, each of them inside an
  inbound or lifecycle callback (§2.2's "not detected" rows); and the counter writes every callback now
  makes in every build type (§2.1, "Cost").
- **`L-007-2`, widened (R-7):** the row of the table above.
- **`MessageStore::store` (R-6):** calls complete in the order they were made; a store that completes two
  calls out of order has their frames transmitted out of order (NC-14). This binds every existing
  implementation of the interface.
- **A limitation: a nested send that disconnects inside an inbound callback or `onLogon`** (§2.5, each
  route it lists).
- **A limitation: `Engine::stop()` started from an application callback** (R-5). On one io thread it can
  close the session's transport before the callback returns; on several it depends on whether the control
  strand is idle. Sends already queued on that session's strand, which today are sent, are refused with
  `session_invalid_state_for_send` after this PR where they found their session in the snapshot; a send
  the callback starts after `stop()` has two endings on several io threads (§2.1a). Status: follow-up,
  fixpp#579.

`L-023-2` is not a new row: the table above has what goes, what is added and what remains.

The rows as they were written: `B-563-1` (R-1), `B-563-2` (R-4), `B-563-3` (R-2), `B-563-4` (R-6),
`B-564-1` (the guard), `L-563-1` (the limitation of R-4's rule), `L-563-3` (a nested send that
disconnects) and `L-563-4` (R-5). Two rows are not in the list above: `L-563-2`, the send started
while the session answers the peer's Logout, which the first bullet placed inside the R-1 row; and
`L-563-5`, an allocation failure while a send starts (§7). A third was added in Phase 7:
`L-563-6`, the Logout of a graceful close that a terminal close started in its `toAdmin`
escalates (§2.1a, the `onLogout` bullet).
That row was deleted when §2.6 was implemented, and `B-563-5` and `L-563-7` were written then (the
last block of this section).

**Issues.** The PR closes fixpp#563 and fixpp#564. Their acceptance item 3 (measure `Engine::send`, then
make the `engine.hpp` sentence match) is met by the record and this section. fixpp#579 is the follow-up
for `Engine::stop()` and stays open after this PR; the PR body and the commit messages name it only as a
follow-up.

**Compatibility.**

- `Session`'s layout changes: two depths, and the owner token in place of `in_dispatch_`. A C++ layout
  change before the first release. `Session::send` keeps its signature; it becomes a plain function that
  returns the awaitable of a private coroutine (§2.1).
- `MessageStore::store` gains a documented requirement (R-6). No signature changes.
- The threading contract gains a sentence (R-7). It states what `L-007-2` already made undefined for a
  false attestation, for a case no sentence named.
- The C ABI's symbols, types and error codes do not change, and no call that succeeds today fails
  afterwards (OD-6 withdrawn). The reason is §2.4's last row. Outside the route R-5 sets aside, OD-3
  refuses only sends that the control-strand read refuses today, with the same error. The sends it
  refuses and today's code admits are on that route, which the C ABI cannot take. And a send that races
  `fixpp_engine_destroy` from another thread
  has an outcome that depends on timing today and still does; the window in which it is admitted is
  wider. A consumer can observe that widening: such a send may succeed where it failed before. Under
  `[const §X.7]` a change is breaking when a call a consumer could already make fails, or returns a
  result other than the one documented. Neither holds: the outcome of a send that races
  `fixpp_engine_destroy` depended on timing, and no documentation promises that it fails (re-derive: read
  the comments on `fixpp_session_send` in `include/fix/c_api/session.h` and on `fixpp_engine_destroy` in
  `include/fix/c_api/engine.h`). No version bump, and no BREAKING clause. P-11 measured the reading on
  the base tree, and it agrees (§3).
- One change of behaviour that none of R-1 to R-7 names, found during implementation: a terminal
  close started in the `toAdmin` of a graceful close's Logout, or in that close's `onLogout`, ends
  the graceful close without the wait for the peer's Logout, where before it could leave both
  closes to end at `logout_disconnect_timeout_ms` (§2.1a, the `onLogout` bullet; B&L `B-563-2`). No
  call that succeeded before fails. On the `toAdmin` route the Logout that was transmitted before
  is now cut, on every store and transport: numbered, stored where there is a store, and not
  transmitted (R-8; §2.6, "The behaviour change"; B&L `B-563-5`). Until §2.6's production commit
  (library `9a77b3e3`) this branch retained that Logout on a `FileStore` with a live transport and
  left its transmit to timing (B&L `L-563-6`, a row since deleted). The block below lists what
  changed in the files of this section with that commit.

**What the implementation of §2.6 changes in the files of this section.** A list for the
implementer. When it was written none of it was done, and the rows and sentences above it
described the head. Status at 2026-10-14: the bullets are done on this branch, in the production
commit `9a77b3e3` and the commits beside it, except the last two, on the pull request's body and on
the issue, which are the orchestrator's. The comments in the code were read at that commit, and
what the reading found went to the orchestrator. The new rows are B&L `B-563-5` and `L-563-7`.
Gate A round 6 reviewed v0.10's form of the list; v0.11 added the limitation row and changed the new
behaviour row's contents and the bullet on the pull request's body, and those edits were not
reviewed. v0.12 named the three issues in the new behaviour row, gave it the ruled reset, and
changed the batch of the store defect's issue; not reviewed either.

- **B&L `L-563-6` leaves the live file. It is deleted, not moved to the closed file.** The headers of
  both files were read for the rule. The closed file holds rows that declare themselves resolved by a
  later feature, and it says of them that the limitation "no longer exists in shipped code". Neither
  header has a rule for a row that is written and overtaken inside one pull request, before it ships.
  `L-563-6` is that case, and the deletion is the orchestrator's instruction for this amendment. The
  id is not given to another row, so that the record and the evidence, which name it, stay
  unambiguous.
- **Every mention of `L-563-6` in the tree goes with it.** Re-derive the list:
  `command grep -rln 'L-563-6' brain spec .specify tests src include`. When this was written it
  returned the B&L file (the row itself, `B-563-2` and `L-563-1`), `spec/coverage-index.md`,
  `spec/feature-catalogue.md`, `brain/components/session.md` and this note. The same search for the
  issue's number also returns the cell file's comments. The record and the evidence in the parent
  repository name the row as history and are not edited.
- **B&L `B-563-2`:** its passage "A change of behaviour that follows from the wait" is rewritten. The
  escalation stays. On the `toAdmin` route the Logout is not transmitted on any store or transport,
  where on `main` it was transmitted and both closes waited out the timeout. Its sentence on where
  `onLogout` runs ("just after the callback, not inside it") stays as it is.
- **B&L, a new behaviour row for this ending,** with the next free id of this section's rows. It
  has: the condition, with "begins inside the callback" as part of it; §2.6's three groups, each
  with what decides it, the session's state and `onLogout`'s position among them; what arrives after
  the cut is decided, and the one thing the state's write does not cover; what an application starts
  inside the moved `onLogout`, per operation; the bound, in the words of §2.6's one statement of it
  and with ruling R-9 named, the points the ruling's last sentence does not cover included, and
  point (a) in its two halves with its condition on the file executor; that on TLS the transport is
  ended abortively, as at the head; the comparison with QuickFIX C++ and QuickFIX/J, with the flag
  and with the row on a reset on disconnect as ruling R-10 has it (fixpp keeps its own behaviour;
  the difference from QuickFIX C++ is stated); and what it does not cover (§7, the first bullet of
  its last block), with the three ways to that condition. That condition's issue is fixpp#594, batch
  B52. The row also names the three issues that §7 names for what this amendment meets and does not
  decide (analysis B-3): fixpp#596, on a clean TLS shutdown for every escalation (batch B54);
  fixpp#597, on what `closed_drained` promises about a store job of another coroutine (batch B55);
  and fixpp#598, on a `FileStore::store` call that reports success for a frame it did not write
  (batch B28, by the owner's placement).
- **B&L, a new limitation row: the logout timeout does not bound a graceful close whose Logout's
  write job is in the file executor** (round 6's O-5), with the next free id. It is a limit of every
  graceful close on a `FileStore`, not of §2.6's route, so it is a row of its own, and the new
  behaviour row points to it.
  - **Its sentence.** On a `FileStore` session `logout_disconnect_timeout_ms` does not bound a
    graceful `close()` while the Logout's write job is in the file executor. The close, and every
    close that joined it, completes when the file executor has run that job, however long after the
    timeout that is.
  - **Its evidence.** `ph8e_predictions.md`, the four cells with the Logout not cut, 20 runs each,
    one per kind of file executor: with a timeout of 300 ms no close had completed 600 ms past the
    expiry, and each completed 0 to 9 ms after the job was let go. Those runs were made on the
    prototype tree, on a route whose code the amendment does not change. The head itself was not
    run then. Since 2026-10-14 NC-29's not-cut arm has run in the pull request's tree, at the
    cells commit, before the production change, and at the production commit, and it is green at
    both (the record, "Phase 8, second part" and "Phase 8, third part"). A write already inside
    its system calls was not run (§2.6, "The bound", (b)). NC-29's not-cut arm pins the row.
  - **No issue is filed for the timing alone** (the orchestrator's decision, 2026-10-13). The timing
    is the stated behaviour of an awaited offload: `close()` waits for its Logout step, and an
    offloaded write is drained by awaiting it and is not abandoned (`offload_to`'s comment in
    `src/session/file_store.cpp`). This row is its record. The defect beside it, a store call that
    reports success for a frame it did not write, has its own issue (fixpp#598, in batch B28 by the
    owner's placement), and the "Measured" section of that issue states the timing as well. Round
    6's judge names, for this item, a line in that issue or an issue of its own; the first is what
    was done.
- **B&L `L-563-1`, condition (2):** its pointer to `L-563-6` becomes a pointer to the new row, with a
  sentence that the two conditions end differently on some arms (§2.6's table).
- **`spec/feature-catalogue.md` and `spec/coverage-index.md`:** this pull request's entries. The
  coverage row of `L-563-6` becomes the new row's, with NC-21 to NC-33.
- **B&L, the row of 093's OD-29** ("Every terminal close escalates an in-flight graceful close"). Its
  sentence that `Engine::stop()` during a graceful logout closes at once gains the exception of §2.6's
  bound: for a Logout that was cut, the terminal close does not end the grace wait. The row takes
  the bound's wording from §2.6's one statement of it and does not shorten it.
- **Every other place that states OD-29.** Re-derive: `command grep -rln 'OD-29' src include spec
  brain .specify tests`. When this was written it returned `src/session/session.cpp` (the comments at
  `close()`'s joining branch and at its join), `include/fixpp/session/session.hpp` (`close()`'s doc
  comment and the comment at `close_escalated_`), the B&L file, `brain/components/session.md`,
  `.specify/2d-threading.md` (its erratum for OD-29, which gains a line; done as a document-level
  amendment at that file's end, which names the errata and leaves the sites as they are), this
  note, and
  `tests/session/inbound_frame_dispositions_test.cpp`, whose cells are not changed and must stay
  green (§3, "What is run again"). The frozen bundle `specs/093-inbound-frame-dispositions` also
  names OD-29. It is not edited; `brain/components/session.md` flags it, as the list under "Frozen
  bundles are not edited", above, does for the other bundles.
- **`brain/components/session.md`:** its two mentions of the split, and the section for this
  decision, which gains R-8, the narrowing of OD-29, and what was rejected (§5's last block).
  **`brain/components/graceful-logout.md`:** its rows on `logout_seen_` and on the Logout step, which
  gains a route on which the Logout is stored and not sent and the step leaves at once; and its row
  on the race with the grace timer, which gains the case in which nothing cancels that timer.
- **Comments in the code.** At `logout_seen_`'s declaration in `include/fixpp/session/session.hpp`,
  whose site (a) is "local graceful-Logout-sent path". At its set site in `run_logout_phase1`, which
  says the flag is true "whenever the Logout frame was actually placed on the wire"; the change makes
  that sentence true on the cut route as well, and the comment says why the set is skipped there. At
  the teardown reset's predicate in `Session::close`, which names that set site. `store_then_emit`'s
  doc comment, for the parameter. The comment at `close()`'s direct `live->close()`, which gives one
  reason for the call and gains the second, with what the pair of calls is on TLS. The comment at
  the state's write when the cut is decided, which says why it is made there. `onLogout`'s doc
  comment in `include/fixpp/session/application.hpp`, if it says where the callback runs relative to
  a Logout. The comments on the two new members. `close()`'s doc
  comment and `toAdmin`'s in `include/fixpp/session/application.hpp` are read, and changed where they
  speak of this route. The comment at the cut route in `run_logout_phase1` names this section, as the
  comment at `close()`'s wait names §2.1a. Each comment states a condition, and none states a result
  of a run.
- **The cell file:** the comments that say the `FileStore` arm on the live transport asserts only
  what holds in both orders, and the ones that cite the issue. Those left the file with the cells
  commit. The comments on a graceful close's wait for the peer's Logout were corrected in a
  commit of comments after the production commit.
- **This note:** §2.1a's `onLogout` bullet and the "Compatibility" bullet above are corrected in
  place as statuses when the change lands, as the earlier phases did. Done on 2026-10-14; the
  last section lists the passages.
- **The pull request's body:** under "Compatibility", the whole of "Against the head" from §2.6's
  "The behaviour change", arm by arm, with the bound and the new limitation row.
- **The issue of this condition** is set to completed by hand when the pull request merges; its
  comment of 2026-10-13 says so. The pull request's body and its commit messages name the issue only
  as a reference.

## 5. Rejected alternatives

- **Defer a send from every callback** (the consult's K1). Sound, and one rule. Rejected by R-1: in
  `fromApp` it changes what a ported application observes (the reply's `toApp` and numbering move out of
  the callback; a reply can be numbered after an engine reply to the same frame) and adds a hop to the
  reply path.
- **Let a send nest in `toApp` and `toAdmin` and repair each emit site.** Restamping the outer frame after
  the callback sends a different MsgSeqNum than the one the callback inspected; assigning before the
  callback makes a `toApp` veto consume a number. Neither restores order at the sites that already assign
  first, where the nested frame reaches the store ahead of the outer one (measured). It would need an
  ordering mechanism at every emit site, and there is no QuickFIX behaviour to keep there: C++ and J put one
  number on two frames and Go deadlocks.
- **Refuse a send started in a callback.** It forbids the reply from `fromApp`, which all three engines
  support and their own examples use.
- **A session send gate** (an `async_mutex` around the whole send). Already rejected by B35's ruling R-10;
  nothing here changes its reasons.
- **Write the depth at each call site instead of in the overload.** An outbound emit added later would
  silently lack it.
- **Reuse `in_dispatch_` as the R-1 predicate.** It is set for inbound callbacks too, which is the K1
  behaviour, and it is a debug check today.
- **Guard `Session::send` only.** The first draft of this note. A graceful `close()` is a second emit body
  and reproduces the defect (measured); making same-thread nesting legal would have turned an abort into a
  silent duplicate on every build type.
- **Leave a terminal `close()` inline** (v0.2 of this note), with a check after each inbound callback, or
  with the arm left to carry on as QuickFIX C++ and J do. Rejected by R-4: the check is needed at every
  Active arm and has to decide what a half-processed frame counts as; carrying on leaves a session that
  reports closed while one of its arms still runs.
- **Make a graceful `close()` wait only inside an outbound callback.** The smallest change that restores
  order; it leaves the Logout's `toAdmin` running inside `fromApp`, which no QuickFIX engine does.
- **`Engine::send`: document "nested or deferred".** Rejected by R-2.
- **`Engine::send`: a post at the first hop**, so it is always deferred. Rejected by R-2: it departs from
  `Session::send` and from QuickFIX, and adds a post to every `Engine::send`.
- **Put every registered id in the snapshot** to drop the miss path. Rejected in §2.3: a republish per
  `register_session`.
- **Publish the snapshot once in `start()`** for the same purpose. It changes what a send before `start()`
  returns for a registered id.

Added at Gate A round 1:

- **R-4 read as "after the operation that called the callback has completed"** (Codex's counter-proposal
  to F-1: track the operation and release the close at a safe boundary). It would deliver v0.3's
  withdrawn consequence. Rejected by the owner: it needs a mechanism at every operation entry
  (`on_inbound_frame`, `Session::send`, the liveness emits, the Logon arms, `close()`'s own Logout); a
  terminal close would wait on the store and transport I/O that a terminal close exists to interrupt, so
  it needs a bound and an escalation on expiry; and the path where `send_impl` itself awaits
  `close(terminal)` would have to hand over instead of waiting on itself.
- **That reading for outbound emits only, and "after the callback returns" for inbound arms.** It would
  spare the frame that is numbered and never sent (P-1). Rejected by the owner: two rules to state and to
  test.
- **A "close requested" mark that the read pump or the Active arm tests before the next frame**, so that
  frames read with the one whose callback asked for the close are not processed (§2.1a, "The close can be
  late"). Not taken: the owner chose R-4's reading with that cost stated, and the mark is a second
  mechanism beside the post. It is a check at a frame boundary, not after a callback, so the rejection of
  "a check after each inbound callback" above does not cover it; it remains available if the limitation
  proves unacceptable in use.
- **A post at `Engine::stop()`'s entry, in this PR**, so that `stop()` never runs inside its caller.
  Rejected by R-5: it changes a function this note otherwise leaves alone. It is fixpp#579's candidate.
- **Disclose store order as a limitation of send ordering** and leave `MessageStore::store`'s contract as
  it is. Rejected by R-6: the ordering promise of §2.1 would then hold for the shipped stores only.
- **Order emission inside `Session`, with tickets or an ordered completion queue**, so that order does not
  depend on the store (Codex's second counter-proposal to F-2). Each send would wait for the previous
  send's store before it may request the write gate. That is the queuing B35's ruling R-10 rejected for a
  session send gate, and it is not adopted without reopening that ruling.
- **A lifetime rule for the payload in the header** ("a payload passed to a send started in `toApp` or
  `toAdmin` must outlive the returned awaitable") in place of the copy. Rejected by the orchestrator: the
  same application code would be correct in `fromApp` and undefined in `toApp`.
- **An atomic depth, read by the guard and by `close()`.** It would make the cross-thread check and the
  R-4 predicate one counter. Not taken: it puts an atomic read-modify-write on every callback of every
  build type, for a reader (`close()`) that is on the session's strand already. The token carries the
  cross-thread meaning with atomic operations on builds with assertions only (§2.2).

Added at Gate A round 2:

- **A callback-exit latch** (Codex's counter-proposal to round 2's F-1): after the post, a send or close
  whose callback is still on the stack suspends on a latch that `callback_dispatch_scope` releases when
  the depth returns to zero. Rejected by R-7, for the triage's four reasons. It is a third suspension,
  released from a destructor that runs between the callback's return and `assign_outbound()` at every
  callback-first site; it cannot resume the waiter inline there, because that begins an emit inside the
  outer stretch, so it has to post, and it needs waiter storage, a cancellation rule and a teardown rule
  on a path that exists only for a contract violation. R-1 as ruled is "by one post". It repairs sends
  and closes only: under a callback that runs the `io_context`, the engine's own emits and a nested
  frame delivery still enter the stretch. And the nearer alternative, deferring again, never lets the
  callback's `poll()` return (§2.1).
- **Refuse at run time:** after the post, a send whose callback is still on the stack returns an error on
  every build type. Rejected by R-7: it is one more result of `Session::send` to document and test; it
  has no answer for `close()`, which cannot be refused without leaving the session open; and the
  engine's own emits under such a callback stay broken.
- **Keep the inbound-parse mark beside the depth assertions.** The depth assertion at the inbound
  overload implies the mark's, so no cell could show the mark's assertion to be missing (§2.2).
- **One assertion at `on_inbound_frame`'s entry in place of the parse sites'.** It would also abort a
  frame fed from inside a callback whose arm parses nothing over the buffer. Not taken: `on_inbound_frame`
  is a plain function that returns its arm's awaitable, so an assertion in it runs when the awaitable is
  made, not when the frame is processed; and an assertion at the arm's first resumption would abort
  frames that pass today's guard, those whose arm calls no callback, and whether a shipped test feeds
  such a frame from inside a callback has not been measured. The parse sites are where the buffer is
  written, which is what the guard protects.
- **Extend step 5 to the confirming Logout,** by reading `logout_seen_`. It would refuse the send
  deferred from that Logout's `toAdmin` and leave the send nested in the same Logout's `fromAdmin`, which
  runs after the Logout was emitted and is not deferred. The case belongs with `Session::send`'s
  admission test, which this PR does not change (§7).
- **A strand-confined flag for step 6's skip** (v0.4). It was read at the depth test, which sits after
  the Active check, so a deferred send refused at that check left it set for the next send. Replaced by
  an argument (§2.1).
- **Isolate the bench by a `toApp` veto.** §3, "Cost", has why the refused payload was taken.
- **Declare OD-3 a breaking C-ABI change** (the first OD-6 ruling). Withdrawn by the owner on the
  corrected premise (§0, §2.4): a BREAKING paragraph would describe a failure no consumer can observe.

Added in v0.8 and rewritten in v0.9 and v0.10, for §2.6. Gate A round 6 reviewed v0.10's form. v0.11
and v0.12 each added one entry at the end; neither was reviewed. S, S2, S3, S5 and S6 are the
prototypes §2.6 names.

- **Merge with `L-563-6` stated,** and leave the one ending to a later batch. Rejected by R-8.
- **S: skip the Logout's transmit and change nothing else.** The Logout step then sets `logout_seen_`
  and records `LogoutSent` for a Logout that never left. Measured (`ph8a_S_arms_by_stall.md`):
  `onLogout` runs with the session at `LogoutSent` on every arm, and with `reset_on_logout` set the
  teardown reset fires on every arm, so the store is emptied and the next outbound number is 1. The
  session would come back at number 1 against a peer that saw no Logout. It is also against B28's
  ruling R-24 (§0).
- **S3: keep the cut step in its wait, and let `close()`'s join end it.** This is what v0.8
  specified. Rejected at round 4 (its triage, O-1): the join cancels the step once, and the lock
  sequence of the shipped stores clears that cancellation when it arrives while the Logout's store
  call is queued at the store's lock. The step then waits out `logout_disconnect_timeout_ms` with the
  session Active, and nothing is retained under the Logout's number: a second ending of R-8's
  condition, chosen by whether the lock is free. Measured on S3 in the held-lock scene
  (`ph8b_held_lock.md`): a sleep in flight on the session's clock, the closes ending only when the
  cell advances the mock clock past the timeout, nothing under the number. Where the cancellation
  landed is not shown by a line of the output; the result is consistent with the trace. The same wait
  contradicts B28's ruling R-17.
- **S5: write `Disconnected` after the cut Logout's store call.** This is what v0.9 specified.
  Rejected at round 5 (its triage, F-4): the cut is decided when `toAdmin` returns, but the session
  stays Active until the store call has returned, and on a `FileStore` that call is a real write on
  another thread. Whatever arrives in it is acted on after the application's terminal close, and the
  peer can be sent frames numbered above a Logout it never received. Measured on S5
  (`ph8c_in_flight_frames.md`): a Logout from the peer draws a confirming Logout numbered one above
  the cut one, and with `reset_on_logout` set the teardown reset fires; an application message
  reaches `fromApp`; a TestRequest draws a Heartbeat; a `Session::send` from another coroutine is
  admitted, numbered and transmitted; a wake of the liveness loop emits a Heartbeat. The same twelve
  cells are green on S6. S5 also put `onLogout` after the store call, farther from QuickFIX's
  position than S6's.
- **Accept both outcomes of an arrival during the Logout's store call, and document them** (one of
  Codex's two counter-proposals to round 5's F-4). Excluded by the owner's rule that one condition
  has one observable ending, while a way to make it one is in scope.
- **A separate state or gate that keeps arrivals out once the cut is decided** (Codex's
  counter-proposal to round 4's F-3, and its other one to round 5's F-4). v0.9 did not take it and
  said that the window was the Logout's store call alone, as at the head. That was true of the window
  and not of what leaves it (round 5's triage, "Against round 4"). v0.10 ends the window with the
  state the session already has: `Disconnected`, written at the decision point. No new state and no
  gate is added.
- **S2 as it was built: leave the step after one post, with the transport's direct close left keyed
  on which arm of the join won.** Measured (`ph8a_S2_arms_by_stall.md`): the transport's `close()` is
  reached twice on a `FileStore` with a live transport and once on the other two live arms. The split
  did not come from the step leaving. It came from the key: `close()` made its direct call only when
  the timer's arm won, and which arm wins follows the hops of the store call (round 4's O-3).
  Statement 4 keys that call on the cut as well, and the split is gone (measured on S5 and S6). S2's
  post is not kept: nothing needs it.
- **v0.8 rejected "the step leaves by itself". That rejection is withdrawn** (round 4's O-3). It
  stood on two premises, and both were wrong. One: that a step which leaves by itself needs the
  terminal close to capture the in-flight result's slot before its wait. The order of §2.6 ("Why this
  is by construction") is forced without it, and both closes returned success on every arm of S2, S5
  and S6 (measured). Two: that S2's split of the transport's close count was a cost of leaving; it
  was a cost of the key, above. The form that B28's ruling R-17 gives every Logout that was not sent
  was so set aside unmeasured. Round 4's prototype built it and measured it.
- **The terminal close captures the in-flight close's result slot before its wait** (v0.8's entry),
  **or registers as a waiter on it** (Codex's counter-proposal to round 4's F-1). Not needed, for the
  reason above. Mutant C shows what the capture would guard against: a terminal close whose first
  turn comes after the graceful close has finished returns `session_already_closed`. With one post in
  `close()`'s wait that order does not arise (read; §2.6).
- **Round 4's fix shape as written, with the join's waker left as it is.** The terminal close would
  still cancel the grace timer for a cut Logout, and the join would cancel the cut Logout's store
  call. A store call cancelled at a lock that is held only for an instant retains nothing, so
  "stored" would depend on whether the lock is free. Not taken: statement 4's part on the waker
  removes that route. Measured: mutant D, which is this alternative, brings the loss back in the
  held-lock scene. What is left, a store call that has not returned when the grace timer expires, is
  ruled by the owner (R-9).
- **Never cancel the cut Logout's store call, not even at the grace timer's expiry** (the second
  option of R-9's question; Codex's first counter-proposal to round 5's F-1 and F-3, with the store
  operation's lifetime owned apart from the close). The Logout would always be stored, and a terminal
  close and `Engine::stop()` would wait for the store however long it takes. Rejected by R-9.
- **No direct close of the transport on the cut route** (Codex's counter-proposal to round 5's F-2).
  It would give a TLS peer a close-notify where the head gives an abortive close on every live arm
  of this condition. Not taken here: it is a change against the head that R-8 does not ask for, and
  it is one question for every escalation (fixpp#596). "Leave the key as today" is not taken
  either: the direct call would then depend on which arm of the join won.
- **`close()` joins every store call of the session before it publishes `closed_drained`** (Codex's
  counter-proposal to round 5's F-5). Not this amendment's: the state exists at the head without it
  (§7; fixpp#597).
- **A cell that pins asio's order of starting the operands of `||`** (Codex's counter-proposal to
  round 5's F-7). Not taken: the design does not rest on that order. It rests on R-4's wait, and
  §2.6 claims the order only as an observation at one asio version.
- **A latch scoped to the Logout's `toAdmin` in place of the count, or a generation scheme that is
  safe against a wrap** (Codex's counter-proposal to round 4's F-6). Not taken: the wrap is not
  reachable (§3, "Specified without a witness").
- **A yield after `toAdmin`, then a read of `close_escalated_`.** The step would post once after the
  callback, so that the waiting close runs and sets the flag, and then read the flag. Not taken: the
  decision would be an order between two posts, where a count that is read synchronously needs no
  order. Read, not built.
- **Set the escalation's flag inside the callback,** from the terminal `close()` before its wait, so
  that the step reads `close_escalated_` when `toAdmin` returns. Not taken: `close()` would read the
  lifecycle and act on another close before its wait, where §2.1a puts the three-state test after the
  wait so that inside the callback the session is as it was (R-4). The count is also written inside
  the callback. It differs in that the Logout step is its only reader, and nothing that a close or an
  application reads changes. Read, not built.
- **Transmit on every arm,** by letting the Logout's store call and its write finish before the
  escalation takes effect. It is the other ending that acceptance item 1 of the issue names. Not
  taken (R-8): it is the opposite of QuickFIX's ending, and a write that hangs would then hold a
  terminal close, which exists to end such a write. The same reason is given above against R-4 read
  as "after the operation that called the callback has completed".
- **An arm of NC-29 on a file executor of two threads, to pin the counted and unreadable record**
  (v0.11). Not kept. That record is reached on both routes and is not this amendment's; the first
  acceptance step of its own issue is its witness (§3, NC-29's row; §7). v0.10's form of the cell,
  behind a gate that reorders the executor's jobs, is not kept either.
- **Match QuickFIX C++'s order of a reset on disconnect on this route** (v0.12): reset inside the
  disconnect, then store the Logout under its old number, so that the store ends with one frame and
  the next sender number at 2. Rejected by R-10: fixpp keeps its own behaviour, and the difference
  is stated.

## 6. Decisions, all settled

**Settled at round 1.** The triage judged each of v0.3's three; none is reopened unless a later round
finds the judgement wrong. OD-2 is not in the list: it was v0.2's question about an inbound arm whose
callback closed the session, and R-4 settled it at v0.3.

- **OD-1 — the result of a deferred send cancelled before it starts: `dispatch_aborted`, with no state
  change.** Sound, on two conditions that this revision writes in. The result has to be produced by an
  explicit test after the post, because the post does not throw (§2.1, step 3). And the B&L row has to
  say that `dispatch_aborted` from `Session::send` no longer implies `Disconnected` (§4). At the C ABI the
  value maps to `FIXPP_ERR_CANCELLED` (`src/capi/error.cpp`), and the C ABI cannot cancel a send, so
  nothing changes there. A new error value was the alternative; it would touch the error taxonomy and the
  C ABI's translation table.
- **OD-3 — the stop check on the hit path: the session's engine-stop flag, read on the session's strand.**
  Sound as a mechanism. `stop()`'s first step sets the flag on that strand before it emits cancellation,
  so the check is ordered with `stop()` by the strand, not by an atomic read that can go stale between the
  check and the send (the gap 093 plan OD-15 rejected for the same flag). It needs a reader on
  `session_engine_access` (§2.3). By reading it breaks no shipped cell: V-15 accepts any result, and V-16
  fails earlier, at the admission re-check. Its cell is a construction (NC-10). Its `[const §X.7]`
  disposition is settled below (OD-6).
- **OD-4 — `Engine::send` from a callback of an unpublished session cannot be admitted.** Holds for both
  role loops, by reading: each publishes the session before it delivers a frame, a refused publish
  delivers none, and the callbacks that run before publication (`onCreate`, the initiator's Logon
  `toAdmin`) are before Active. Re-derive by reading what follows each `publish_entry` call in
  `run_accept_loop` and `run_connect_loop`. NC-1 pins it per role.

**Settled by the owner after round 2.** The rulings are in §0; each is restated here with where the note
implements it.

- **OD-5 — how `[const §VIII.3]` is met: a bench for `Engine::send` in this PR, with a `paired` manifest
  row.** §3 ("Cost") has the source file, the target, what it times, the isolation, the row, how this
  PR's evidence is produced, and the foreign-thread probe that is kept outside the gate (§0). `L-023-2`
  closes for the send route and stays open for establish-churn (§4).
  The alternative v0.4 listed, two probes accepted as the PR's evidence, is gone.
- **OD-6 — withdrawn: OD-3 is not a breaking change, and this PR declares no C-ABI change.** v0.4 said
  that a send whose session-strand work starts after `stop()`'s first step "can be transmitted today and
  is refused after this PR". By reading, today's control-strand read refuses that send already (§2.4).
  OD-3 stays. P-11 measures the reading as a first implementation step; a transmitted send there returns
  the question to the owner. It was run on 2026-10-11, and no send was transmitted there (§3).
- **Same-thread re-entry (the triage's Q2-1) — outside the contract: R-7.** §2.1 has the setups and the
  contract sentence, §2.2 the assertions, §4 the widened `L-007-2`, and §5 the latch and the run-time
  refusal that were not taken.

No decision is open. `/analyze` ran over v0.6 and the owner's `/plan` sign-off is given (§0a). P-11,
which gated the rest of the implementation, is measured and agrees (§3).

**Settled by the owner on 2026-10-13: R-8 and R-9.** R-8: the condition of §2.6 has one ending,
QuickFIX's. R-9: where the cut Logout's store call is still waiting at the store's lock when the
logout timeout expires, the timer ends the close and nothing is stored for the Logout. §2.6 has the
design and the one statement of the bound, §3's last block the rows, and §5's last block what was
not taken. The paragraph above is v0.7's: the `/plan` sign-off it names is v0.7's. The amendment has
its own, below.

**Ruled by the owner for another note, and followed here: `logout_seen_` is set only where the Logout
was sent.** B28's ruling R-24 (§0).

**Decided by the orchestrator before v0.9, and reviewed since: nothing cancels a cut Logout's store
call before the grace timer expires** (§2.6, statement 4, the part on the join's waker). It was added
under the owner's standing instruction to continue, and reported to the owner. Round 5's judge found
the mechanism sound with it. What v0.9 recorded beside it as the orchestrator's decision, the ending
at the timer's expiry, is no longer that: the owner ruled it (R-9).

**Settled by the owner on 2026-10-13, at about 20:50: R-10, the sign-off of the amendment, and the
batch of fixpp#598** (§0). R-10: with `reset_on_disconnect` set, this route keeps fixpp's teardown
reset, which runs after the Logout step and leaves the store emptied and both next numbers at 1.
QuickFIX C++'s order is a reset inside `disconnect()`, then the Logout stored under its old number
and the next sender number at 2 (§2.6's comparison table). v0.9 recorded fixpp's order as the
orchestrator's decision; round 5's judge found that the choice is the owner's; v0.10 and v0.11
marked it as with the owner; the owner has now ruled it. The implementation leaves the teardown
reset as it is, and NC-33 pins the ending. The sign-off is of the amendment at v0.11. No decision of
the amendment is open.

## 7. What this does not decide

- A live send that interleaves with a resend replay. A deferred send can start while
  `replay_outbound_range_` is suspended, as any overlapping send can today. Read, not run; QuickFIX/Go
  added a lock for it. Not changed here; a candidate issue.
- A send that overlaps a graceful close without being deferred. It can be numbered and transmitted after
  our Logout, by timing, because `fsm_state_` stays Active until the Logout's emit returns. §2.1's step 5
  refuses that for a deferred send only. Not changed here for the others.
- A send started while the session answers a logout the peer began. The Active arm emits the confirming
  Logout, then calls `fromAdmin` on the peer's Logout, then persists, and writes `Disconnected` only
  after that; no `close()` has begun, so step 5 does not see it. A send started in the confirming
  Logout's `toAdmin` is deferred. Measured (P-9 (f), and NC-5 (f)'s arms; the record, "Phase 4, first
  part" and "Phase 4, third part"): on a `MemoryStore` and on a `FileStore` it is then admitted,
  numbered after our Logout, retained, transmitted after it, and returns ok; the two differ in where
  the frame goes out relative to `onLogout` (after it, with the session `Disconnected`, on a
  `MemoryStore`; before it, with the session still Active, on a `FileStore`). With no store it is
  refused with `session_invalid_state_for_send`: the session is `Disconnected` before the send starts,
  and nothing holds the message. A send started in that `fromAdmin` nests; by reading, it is numbered
  after our Logout and, where it is transmitted, transmitted after it, every time. Before this PR, on
  an `NDEBUG` build, the first took the Logout's own number (fixpp#563) and the second behaved as it
  does now. Refusing both is a change to `Session::send`'s admission test, which this PR does not
  make. The owner ruled a refusal of the first on 2026-10-12; the orchestrator did not implement it,
  under the owner's rule to keep QuickFIX's observable behaviour, because for a send started just
  after the callback, which is what a deferred send is, QuickFIX C++ and QuickFIX/J accept it and
  transmit it after the Logout, QuickFIX/Go stores it and drops it unsent, and none refuses (read, not
  run: `research/G19-fix-fpml-iso20022/research/probes/b41_563_564/ph5_quickfix_send_during_peer_logout.md` in the parent repo, first section). It stays a disclosed limitation: B&L `L-563-2`. NC-5 (f) pins the
  first as measured, per store kind.
- What a TLS transport does with a write that begins while its `close_async` is suspended. Not decided by
  reading. Under R-4 that order is the regular one for a terminal close started in `toApp` on a TLS
  session; the cells run over plain transports. By reading, `ApplicationEngineSend.SendDrainRaceNoUAF`
  drives that order over TLS, with an assertion that accepts a value or either of two errors (§3).
- `send_impl`'s `co_await close(terminal)` after a throwing `toApp`, on a chain that is already cancelled:
  the await throws before `close()` is entered, and the session is left `Disconnected` and not closed.
  Read at round 1; it predates this PR and is not changed by it.
- The C ABI's and Python's send from a callback (`L-054-1`, `B-055-2`).
- A session send queue, a bound on overlapping sends, or their back-pressure.
- `Engine::stop()`: neither the function nor what it does when an application starts it from a callback
  (R-5, fixpp#579), beyond the one effect of OD-3 on that route that §2.1a states.
- The further copies of the payload on `Engine::send`'s path: its own copy, then the session-strand
  body's, then the deferred path's when that is taken.
- A gated bench for what `Engine::send` costs across threads, and a bench for establish-churn. The new
  bench times the route on one thread, and the foreign-thread probe is a local measurement that is
  reported and not gated (§3, "Cost"). `L-023-2` stays open for establish-churn; the cross-thread wake-up
  is a sentence in that row, not its subject.
- What a callback that runs the session's own executor gets on a build without assertions (R-7,
  `L-007-2`).
- Whether `register_session` should republish the snapshot; two comments in `engine.cpp` and `engine.hpp`
  disagree with the code about it. Noted, not changed.
- One ending for an allocation failure while a send starts. On the deferred path a `std::bad_alloc`
  from its frame, from the payload copy or from the post leaves `Session::send` as an exception, with
  the session's state untouched; inside the send's body the same failure is `dispatch_aborted` with
  the session `Disconnected`; `Engine::send`'s own copies throw too. The owner ruled one ending
  (2026-10-12). It is not made here: the deferred path's failure points come before the hop, with
  `toApp` or `toAdmin` on the stack, and writing `Disconnected` there would run `onLogout` inside the
  outbound callback, which is the state this PR removes. Read, not measured. B&L `L-563-5`;
  fixpp#587.

Added in v0.8 and rewritten in v0.9 and v0.10, for §2.6. Gate A round 6 reviewed v0.10's form. v0.11
corrected the bullet on `FileStore::store` and added the items that round 6's judge lists as not
established. v0.12 rewrote the bullet on `reset_on_disconnect` after the owner's ruling. Neither
version's edits were reviewed. The bound is not in this list: it is a ruled behaviour
(R-9), stated in §2.6, "The bound", with what it does not cover.

- **A terminal close that does not begin inside the Logout's `toAdmin`,** while a graceful close's
  Logout is in its store call. There are three ways to it. The application posts the close from
  `toAdmin`, so that it begins after the callback, with the depth at zero. Or the close comes from
  another thread. Or the application starts `Engine::stop()` in that `toAdmin`: `stop()` reaches
  `Session::close` only after a hop through the control strand and the join of the role loops, so its
  terminal close begins later too (round 4's O-6; B&L `L-563-4` has what `stop()` started from a
  callback does). Under an `Engine` the third is the call an application is most likely to make
  there. Such a close takes no wait and moves no count, so §2.6 does not see it: the Logout is not
  cut, and the close joins and cancels the grace timer as today. On a `FileStore` with a live
  transport, timing then still decides whether the Logout is transmitted, and with it `LogoutSent`,
  `logout_seen_` and the reset on logout. Read, not run (`ph8a_readings.md`, its fourth section): it
  is the race in `Session::close`'s join that the head's measurement shows for §2.6's own condition
  (`ph8a_S_arms_by_stall.md`, "unpatched head"). No cell drives any of the three ways. By reading it
  exists on `main`, and it is not specific to a callback. Its issue is fixpp#594, batch B52. The
  evidence status of that issue: read, not run, for this condition; measured for §2.6's own
  condition.
- **`FileStore::store` reporting success for a frame it never wrote, where the cancellation's job on
  the file executor runs before the write's body starts.** That needs a file executor with more than
  one thread. The store then advances its next number and returns success, and the record cannot be
  read (measured on two threads; §2.6, "The bound", (a); §3, P-22). On an executor that keeps the
  order of its jobs the write runs first and the record reads back (measured on one thread).
  `src/session/file_store.cpp` is as on `main`, and the state is reached on the route with no cut
  too. Its issue is fixpp#598, which the owner placed in batch B28 on 2026-10-13 (§0, R-10's
  bullet). After round 6 the orchestrator changed that issue's title and rewrote its body (read on
  2026-10-13, after the rewrite). Its "Measured" section is the re-run's table of three rows: one
  thread with the order kept; two threads with the write's thread delayed; two threads let go
  together. Its "Reach" states the condition: a file executor with more than one thread; a pool of
  one thread keeps the order and does not reach it; a strand over a pool was not run. A paragraph
  headed "History of this text" records the first wording, which was too wide, and why. Its first
  acceptance step is a cell on two threads with the write's thread delayed. A third comment on the
  issue records the owner's placement in batch B28. This amendment does not decide it. On the cut
  route the bound meets it only on such an executor, where the grace timer expires before the
  Logout's write has started, or after a `toAdmin` longer than the timeout. No cell of this
  amendment pins the unreadable record (§3, NC-29's row). v0.10's bullet gave no condition on the
  executor; its measurement used a gate that reordered the two jobs (round 6's O-1). Not
  established, each with what would settle it: whether, on two threads, the emit of the cancellation
  also races the spawned coroutine's own read of its cancellation state, by a ThreadSanitizer run of
  a cancelled `FileStore::store` on a pool of two threads; a job already inside its system calls at
  the expiry, by a run that holds the write inside its body; more than two threads, and a strand
  over a pool, by the same scenes on those executors; a pool that is busy and not held by a cell; a
  restart of the store over such a file.
- **`reset_on_disconnect` on this route: ruled (R-10, §0), and no longer open.** fixpp keeps its
  teardown reset, which runs after the Logout step and leaves the store empty and both next numbers
  at 1. QuickFIX C++'s order is a reset inside `disconnect()` and then the Logout stored under its
  old number, with the next sender number at 2 (read by round 5's judge at `386ce46e`). What is left
  is not a decision. QuickFIX/J was not read again; a reading of `disconnect` and `persist` in
  `Session.java`, at the commit `ph8a_readings.md` names, would settle its half of the comparison.
  No prototype arm ran with the setting; NC-33 is the first run of it. And with the setting on, the
  reset takes the store's lock after the join with no timer (§2.6, "The bound", (d)); what would
  settle that is NC-28's scene with the setting on: the closes must still be incomplete while the
  gate is closed.
- **The session closed while a store job of another coroutine is in flight.** In the measurement of
  the bound at the store's lock the session reached `closed_drained` while a job was still kept at
  the gate. That job is the other writer's: the inbound persist of the peer's order. The cut
  Logout's store call had retired; nothing of the close was in flight (round 5's triage, F-5). The
  head and `main` reach the same state without this amendment: `close()` does not wait for a store
  call that another coroutine of the session has in flight, so any close that completes while an
  inbound persist or a send's store call is in its offload publishes `closed_drained` with that job
  in flight. Read, not run. Under the Engine the owners of those coroutines are joined before a
  session is freed; for a `Session` driven directly B&L `L-544-2` states the rule for
  `Session::send` only. What `closed_drained` promises about such jobs is fixpp#597, batch B55. What
  would settle it: the same scene under AddressSanitizer, with the `Session` destroyed before the
  gate is opened. No run did that.
- **What a TLS peer reads on this route.** The pair of transport calls is one abortive close on TLS,
  as at the head (§2.6, statement 4). Read from `Session::close` and `asio_tls_transport`; the cells
  count calls on a double. Whether the transport should get a clean TLS shutdown instead is
  fixpp#596, batch B54; that issue covers every escalation and not this route alone. What would settle the reading: an arm on a
  real TLS transport that reads the peer's outcome. What a TLS peer of QuickFIX observes on
  `disconnect()` was not read.
- **An operation that passed its test of the state before the cut was decided.** A send suspended in
  its own store call is transmitted when that call returns (measured as a reading; §3, P-21; NC-30).
  On a live transport that refuses a write after its `close()` it was not run. The position "passed
  the test, no number yet" was read and cannot be driven without a seam in production code (§2.6,
  "What arrives after the cut is decided").
- **The arrivals on a live transport.** NC-27's arms use the synchronous sink. With the session
  `Disconnected` nothing is emitted, so no arm depends on the sink; it was not run on the live
  transport.
- **R-7's setup.** An application that runs the session's executor inside the Logout's `toAdmin` can
  make a terminal close that was queued earlier begin inside the window. That close finds the depth
  non-zero, takes the wait and moves the count, and the Logout is cut, though this `toAdmin` did not
  start the close. It is outside the contract (R-7). Read, not run.
- **A `toAdmin` that starts a terminal close and then throws.** The Logout step returns
  `app_callback_threw` before the comparison, as it does today after any throw of that callback. The
  Logout gets no number and is not stored. Read, not run; not changed.
- **`Engine::stop()` and an Engine-managed session on the cut route.** By reading it is the same
  `Session::close`. Read, not run (round 5's triage, O-6, in `Engine::stop`): `stop()` closes the
  transport with `close_async()` before any `Session::close`; it then joins the role loops and
  drains `Engine::send`, so it already waits for whichever of those coroutines holds the store's
  lock; its terminal close of the session takes the joining branch. So under the Engine the bound
  adds at most the Logout's own turn at the lock and its write. Whether the Engine's role loop does
  anything on a session that is `Disconnected` while its transport is open was searched for by the
  judge in `engine.cpp`, with no reader of the state found, and not run. No arm ran any of it, and
  §3 specifies no Engine-level arm; what would settle it is an Engine-level arm with the read pump's
  persist held and `stop()` started.
- **How often the store's lock is held for the whole timeout.** Read: the inbound persist and an
  earlier frame's store call hold it across the offload, and the close's flush takes none. No
  measurement of a load shaped like production is in the evidence. It decides how often the bound is
  met, not what it does.
- **What QuickFIX's "Logout sent" flag, left true, does at the next logon.** Not followed (§2.6,
  "Compared with QuickFIX C++ and QuickFIX/J").
- **`reset_on_logout` after a Logout that was sent and never answered.** Not this amendment's: it is
  fixpp#595, which B28's note names for it (its ruling R-24, "What it does not change").
- **Whether `close()`'s own awaits of a lock stay shielded from its caller's cancellation,** given
  that the lock sequence resets the chain's cancellation filter. Noticed by round 5's judge while
  reading for F-1. It is at the head and outside this amendment, and it was not traced to a
  conclusion. What would settle it: a trace of `close()`'s awaits that take a lock, with a
  cancellation emitted on the caller's chain after each.
- **What was said to the owner in the session about the bound.** §0, R-9, "What the owner was told",
  gives it from the task list's section of that name. That section is the orchestrator's account of
  the session. Round 6's judge read the record and not the session, and so did the author of this
  revision. It records no reply of the owner. What would settle it: the owner's own word; no tracked
  file can.
- **`Engine::send` and `Engine::stop()` started in the decision-point `onLogout`.** Round 6's judge
  traced both; neither was run, in the `ph8d_*` cells either (§2.6, "What arrives after the cut is
  decided", its bullet on that `onLogout`). `stop()` from a callback is R-5's route. What would
  settle them: an Engine-level arm for the cut route, which §3 does not specify (the bullet on the
  Engine, above).
- **The second visit of `Disconnected` on a tree that carries B28** (§2.6, "With batch B28"). Read
  on two branches; no tree carries both. What would settle it: the cell that waits for both, on the
  merged tree.
- **The cells at the head, in the pull request's tree.** v0.12 listed NC-22's second arm and NC-27's
  `FileStore` hold arms here as predictions. The scratch rehearsal of 2026-10-14 ran every cell at
  the head (§3's last block, "What the head does, as rehearsed"): those arms did as predicted, and
  what the store shows after NC-27's hold was recorded. What was still to run then was the cells
  commit's own run in the pull request's tree, and the presets the rehearsal did not have (the
  same block, "The limits of this evidence"). That run was made on 2026-10-14, on
  `linux-clang-asan` and `linux-clang-release` (the record, "Phase 8, second part"). A
  ThreadSanitizer run was not in when this status was written.
- **The text of the issue, as Codex could not read it.** Codex's reviews of rounds 4 and 5 could not
  reach the issue. Both judges read it, with the command their "Method" names, and judged against
  it. Nothing is left open by it.

## Gate A

- Round 1 applied 2026-10-11: Codex P1=3 P2=4 P3=1; Opus post-judging P1=2 P2=12 P3=5; rewrite addresses root causes 1–5 and the stand-alone items N-2, N-7, N-8, N-9, N-10, N-11, N-12 and F-5. Reviews: research/reviews/codex_563_564_1_callback-send_review.md, research/reviews/opus_563_564_1_callback-send_triage.md (parent repo).
- Round 2 applied 2026-10-11: Codex P1=3 P2=4 P3=1; Opus post-judging P1=0 P2=7 P3=9; rewrite addresses root causes 1–4 and the stand-alone items F-5, F-7, F-8, N2-4, N2-6, N2-7, N2-8 and N2-9. Reviews: research/reviews/codex_563_564_2_callback-send_review.md, research/reviews/opus_563_564_2_callback-send_triage.md (parent repo).
- Round 3 (on v0.5) 2026-10-11: Codex P1=0 P2=2 P3=2; Opus post-judging P1=0 P2=0 P3=9. Converged. Codex's two P2 findings (the owner token's exit order; the snapshot read having no witness) were judged P3. Reviews: research/reviews/codex_563_564_3_callback-send_review.md, research/reviews/opus_563_564_3_callback-send_triage.md (parent repo).
- v0.6 applies the P3 items of round 3 after convergence, except Codex F-2, which the owner declined; these edits were not reviewed. Owner sign-off 2026-10-11.
- The assertion at the `onLogon` and `onLogout` sites that the outbound depth is zero (§2.2) was added after the final review, at the owner's choice. The triage offered it as optional under N3-2. It has had no review: neither reviewer saw its nesting rows, its death arm (NC-19's lifecycle arm) or its mutant row.
- v0.7 (2026-10-11) applies the findings of the `/analyze` pass over v0.6 (report: research/reviews/analyze_563_564_callback-send.md, parent repo) and records the P-4 and P-11 measurements; these edits were not reviewed.
- v0.7 also records, with no change of version, the measurements made on 2026-10-11 before any production edit: P-1 for a close started in `toApp`, P-1a, P-1b, P-1c, P-2, P-3, P-5 and P-6, and an early reading of P-10 (§3 has each with its evidence, research/probes/b41_563_564/p0b_runs.output.txt and p0b_measurements.patch, parent repo). None disagreed. It also records decisions of the orchestrator: at the stack-arena overload and at the `onLogon` and `onLogout` sites the depth assertion is evaluated after the site's scope and before its callback (§2.2), which replaces what the F1 line below says was left, and is listed in §3 as specified without a witness; and NC-11 has a positive-control arm (§3). The passages these changed are in the header, §2.1 ("Writer"), §2.1a, §2.2, §2.5, §3 and §4. None of this was reviewed.
- On 2026-10-12 v0.7 also records, again with no change of version, the measurements made during implementation: P-7, P-10 on the mechanism, P-1 for a close started in `toAdmin`, P-8 and P-9 (§3 has each with its evidence, research/probes/b41_563_564/p2a_runs.output.txt, p3a_runs.output.txt and ph4a_runs.output.txt, parent repo); none disagreed, and none of this was reviewed.
- On 2026-10-13, after sign-off and again with no change of version, statements that the implementation made stale were corrected in place as statuses (the header, P-5's row, NC-11's row, and two sentences of §3), and behaviours measured during implementation were recorded where their subject is: the branch with no store and the escalation the wait restores (§2.1a), the send started in the confirming Logout's `toAdmin`, per store kind, and the allocation failure (§7), with one bullet in §4 ("Compatibility") and the ids of the B&L rows (§4). No design text changed. None of this was reviewed.
- On 2026-10-13, in Phase 7 and again with no change of version, cells were added for the escalation the wait restores from the `toAdmin` of a graceful close's Logout (§2.1a, the `onLogout` bullet; B&L `B-563-2`): `CallbackSend.ToAdminOfGracefulCloseStartsClose_Terminal`, with the arms of the close cells, and its positive control `CallbackSend.ToAdminOfGracefulCloseStartsNoClose`. Until then a probe had measured that route on four of those arms and no cell covered it (the record, "Phase 4, addendum"). One arm the probe had not run differs from the others: on a `FileStore` with a live transport the escalation holds and the Logout is retained, and whether the Logout is transmitted is a race between the file executor's completion of its store call and the end of the Logout step that the escalation brings about. A first reading took that order for forced by the session executor's queue; the stall probe of Phase 4 refuted it (not transmitted without a stall, transmitted with the session's thread stalled after the hand-off). That arm's cell therefore asserts only what holds in both orders, and the other five assert that the Logout is transmitted once. The split is B&L `L-563-6`, a new row, and fixpp#590 is where the one ending is decided. §2.1a's `onLogout` bullet, §4's "Compatibility" bullet on this change of behaviour and §4's list of rows each gained a sentence for it; none of them had said that the Logout is transmitted on every store. Neither the cells nor these sentences were reviewed at Gate A.
- On 2026-10-13, in Phase 7, two sentences on `Engine::stop()` started from a callback were made exact (§2.1a, the bullet on OD-3's refusal on that route; §4, the limitation's bullet), after a reading recorded on fixpp#579: the refusal of bodies already queued holds for sends that found their session in the snapshot and not for a queued body of the miss path, and a send the callback starts after `stop()` has two endings on several io threads where today it has one. B&L `L-563-4` and one comment in `Engine::send` say the same. Read, not run; not reviewed at Gate A.
- On 2026-10-13, in Phase 7 and with no change of version, one sentence of §3 ("Cost (OD-5)", "One thread") was corrected: it said that each `co_spawn` of the bench runs as a dispatch on the bench thread. The bench thread starts the send from outside the `io_context`, so that `co_spawn` is queued and runs at the thread's next poll (the Phase 1 measurement, in the parent repository's `decisions/speckit/b41-563-564-measurement.md`). No other text of §3 changed. Not reviewed at Gate A.
- **v0.8 — the amendment for fixpp#590** (2026-10-13). Written after convergence and after the owner's sign-off of v0.7, on ruling R-8 (§0), by a sub-agent from the orchestrator's brief and from the Phase 8a evidence (research/probes/b41_563_564/, the files named `ph8a_*`, parent repo). It added §2.6 and the passages the header lists, and changed no other design text of v0.7. It was submitted to a Gate A round scoped to the amendment, which reviews whether §2.6 and its rows implement R-8 and does not reopen v0.7. That round is round 4, below.
- Round 4 (scoped, on v0.8) 2026-10-13: Codex P1=3 P2=2 P3=1; Opus post-judging P1=2 P2=3 P3=8. Not converged. The two P1 after judging are F-2 (§2.6 and B28 gave one class of event two rules) and O-1 (the cut step's wait can lose its only waker), and both come from v0.8's fourth statement (the triage's RC-1). Reviews: research/reviews/codex_563_564_4_callback-send_review.md, research/reviews/opus_563_564_4_callback-send_triage.md (parent repo). Before the rewrite the measurements the triage asks for were made on a prototype of the changed statement (research/probes/b41_563_564/, the files named `ph8b_*`, parent repo). v0.9 applies the round; "Round 4 (scoped, on v0.8) — applied in v0.9", at the end of this section, has one line per finding, the disagreement, and where v0.9 goes beyond or beside the triage's fix shape. v0.9 was written by a sub-agent from the triage, the orchestrator's brief and that evidence. It was submitted to round 5, below.
- Round 5 (scoped, on v0.9) 2026-10-13: Codex P1=1 P2=4 P3=2; Opus post-judging P1=0 P2=2 P3=11. Not converged. The two P2 after judging are F-1 (the bound: the owner ruled it during the round, and v0.9 recorded it as an orchestrator's decision and a limit, with two routes on which the ruling's last sentence does not hold) and F-4 (while the cut Logout's store call is in flight the session is Active). Reviews: research/reviews/codex_563_564_5_callback-send_review.md, research/reviews/opus_563_564_5_callback-send_triage.md (parent repo). Before the rewrite the measurements the triage asks for were made on a prototype with the state's write moved (research/probes/b41_563_564/, the files named `ph8c_*`, parent repo), and one more, the Logout's own write job at the expiry, which no reviewer has seen. v0.10 applies the round; "Round 5 (scoped, on v0.9) — applied in v0.10", at the end of this section, has one line per finding, the disagreements, and where v0.10 goes beyond or beside the triage's fix shape. v0.10 was written by a sub-agent from the triage, the orchestrator's brief and that evidence. It was the last rewrite this loop allows, and it was submitted to round 6, below.
- Round 6 (scoped, on v0.10) 2026-10-13: Codex P1=0 P2=1 P3=2; Opus post-judging P1=0 P2=0 P3=9. Converged; the judge states the tally as unconditional. Codex's one P2 (F-1: no witness of what an application starts inside the moved `onLogout`) was judged P3: the judge traced each operation to one outcome that the source forces, and the branch such a witness would cover has a measured kill cell. The judge asks for no change to the mechanism's four statements. Reviews: research/reviews/codex_563_564_6_callback-send_review.md, research/reviews/opus_563_564_6_callback-send_triage.md (parent repo). Two measurements were made during and after the round and are not in v0.10: what an application starts inside the decision-point `onLogout` (research/probes/b41_563_564/, the files named `ph8d_*`, parent repo), which the judge read after judging; and the Logout's own write job on four kinds of file executor (the files named `ph8e_*`), which the judge's O-1 asks for and which no reviewer has seen.
- v0.11 (2026-10-13) applies round 6's nine P3 items after convergence, from the judge's fix shapes, the orchestrator's brief and that evidence. It was written by a sub-agent. **These edits were not reviewed by a Gate A round.** "Round 6 (scoped, on v0.10) — converged; applied in v0.11", at the end of this section, has one line per item and what v0.11 adds beside the judge's list. Nothing of §2.6 is in the pull request's tree. Still owed for the amendment: `/analyze`, the owner's sign-off, and the owner's answer on `reset_on_disconnect` (§0a, §6).
- v0.12 (2026-10-13) — the owner's sign-off, two rulings, and the analysis pass applied. The owner signed the amendment off at v0.11 (library `9afe3bbd`), ruled that `reset_on_disconnect` on this route keeps fixpp's behaviour (R-10), and placed fixpp#598 in batch B28 (§0; the task list, "Owner rulings on the amendment's sign-off summary (2026-10-13, about 20:50)"). The analysis pass ran over v0.11 and the task list's block for §2.6: decisions/speckit/563-564-callback-send-analyze-phase8.md (parent repo), 23 findings with the orchestrator's decision on each. v0.12 applies the ones that name this note. It changes no statement of the mechanism and no ending that v0.11 stated. It was written by a sub-agent from that record and the orchestrator's brief. **These edits were not reviewed by a Gate A round.** "v0.12 — the owner's sign-off, two rulings, and the analysis pass applied", at the end of this section, has one line per finding. With it, Appendix A's four controls are met for the amendment (§0a). Nothing of §2.6 is in the pull request's tree.
- v0.13 (2026-10-14) — the head states as rehearsed, the cells' names, four row corrections. Before the cells commit, the fixture and the cells as written were rehearsed in a scratch copy at `d7bf5dea`, at the head and on the final patch (research/probes/b41_563_564/ph8f_head_states_for_the_note.md and ph8f_chainV-results.txt, parent repo). v0.13 writes into §3's last block what each cell did at the head there, the cells' names, and four corrections of rows. No measured state contradicts a label. Nothing of the mechanism and no ending changed. It was written by a sub-agent from that evidence and the orchestrator's brief. **These edits were not reviewed by a Gate A round.** "v0.13 — the head states as rehearsed, the cells' names, four row corrections", at the end of this section, lists them.
- On 2026-10-14, after the production commit of §2.6 (library `9a77b3e3`) and with no change of version, statements that the implementation made stale were corrected in place as statuses: the header; R-8's bullet in §0; the bullet of §0a on what remained owed; §2.1a's `onLogout` bullet; in §2.6, its opening, the sentence on what the head does, and the lead of "What was measured, and its limits"; in §3's last block, its opening, the end of "What the head does, as rehearsed" and its bullet on limits, the lead of the prediction table, the sentence on the run at the head in each row that had one, and the lead of the mutant table; in §4, the paragraph on the rows as they were written, the last "Compatibility" bullet, and the block "What the implementation of §2.6 changes in the files of this section"; and §7's bullet on the cells at the head. Each status cites the record's sections "Phase 8, first part" to "Phase 8, third part" (decisions/speckit/b41-563-564-measurement.md, parent repo) and no other measurement. The mutants of the phase, the stall probe on its three settings, and the runs under the other sanitizers, coverage, MSVC and the benches were not in when this was written, and no status states them. B&L `L-563-6` was deleted and `B-563-5` and `L-563-7` were written in the same phase; the note names `L-563-6` as history. No statement of the mechanism and no ending changed. None of this was reviewed at Gate A.

### Round 1 — disagreements

- **F-8 (Codex, P3), "the entry-point census is mislabeled": not applied.** Codex says
  `Session::drive_reconnect` is not public. It is: it is declared in `Session`'s public section in
  `include/fixpp/session/session.hpp`, under a comment that says "Declared public for accessibility from
  engine.cpp's run_connect_loop". §2.1a's table has every public awaitable of the two headers, and its
  disposition column already separates the entry points an application calls from the ones the engine
  drives. The table is unchanged except for the `Engine::stop` row (R-5) and a clause on the
  `on_inbound_frame` row.

Three findings are applied, but not with the fix Codex proposed. The reasons are in §5:

- **F-1:** Codex proposed tracking the operation that called the callback and releasing the close at a
  safe boundary. The owner chose "after the callback returns" (R-4's reading).
- **F-2:** Codex proposed, as one option, tickets or an ordered completion queue inside `Session`. That
  conflicts with B35's ruling R-10. Its other option, a documented requirement, is R-6.
- **F-3:** Codex cited Articles XI and XV of the constitution for "threading state must be race-free".
  Neither says that. The finding stands on the note's own claim that cross-thread entry still asserts,
  and is applied on that ground (§2.2).

### Round 1 — where this revision goes beyond or beside the triage's fix shape

Stated so that round 2 can judge each one:

- **N-3, the send's hop.** The triage asks for a read of the cancellation state after the post. §2.1
  applies asio's whole test there (`throw_if_cancelled` set and a cancellation recorded), so that a chain
  which turned throwing off is treated as asio treats it.
- **N-11.** The triage offers "test `state_ == closing` in the re-check, or disclose it". §2.1 takes the
  test, and drops v0.3's Active re-check from the deferred path, because the deferred path ends by
  awaiting `Session::send`, whose own Active check makes a second one unreachable (a mutant of it could
  not fail).
- **F-3.** The triage's shape is an atomic owner token and an atomic depth. §2.2 uses the token alone
  for the cross-thread check and keeps both depths plain; the reason is in §5.
- **N-2.** The copy lives in the deferred path's frame, and that path ends by awaiting `Session::send` on
  the copy, through an entry that skips the depth test, so the ordinary send's body is not duplicated and
  a send that was not moved out of the callback nests instead of waiting again (§2.1, step 6).
- **Found by §4's searches, in neither review:** `B35SendSlot.Nested_ASendInsideToAppLeavesTheOuterFrameIntact`
  asserts that a send started in `toApp` runs inline and would fail under R-1 (NC-17); the fixpp#518
  block comment in `tests/session/test_session_plaintext_roundtrip.cpp`; `L-497-1`'s and `L-015-4`'s
  clauses; and the release-build cost of the two counters (§2.1, "Cost").

### Round 2 — disagreements

Codex findings and counter-proposals that are not applied as proposed:

- **F-1 (P1), a callback-exit latch: not adopted.** Ruled outside the contract (R-7); the triage judged
  the finding P2 on that ruling. Reasons in §5.
- **F-2 (P1), "state that `L-023-2` is closed": applied in part.** The bench and its `paired` row are in
  §3. The row closes for the send route only; the ruling keeps its establish-churn half open.
- **F-3 (P1), declare the C-ABI break and bump the version: not applied.** The finding took v0.4's
  premise as given; the triage found it false (N2-1) and the owner withdrew OD-6 (§2.4, P-11).
- **F-4 (P2), a test-access counter for single execution of the close body: not added.** NC-8's two arms
  need no new seam.
- **F-5 (P2), a test-only Engine hook that exposes the session in `onCreate`: not added.** Judged P3; the
  arm is deleted (§2.1a).
- **F-6 (P2), a C-ABI witness for the stop check: not added.** There is no C-ABI effect to witness (NC-10).
- **F-7 (P2), a verification matrix: applied as lines in §3,** at the triage's P3.

### Round 2 — where this revision goes beyond or beside the triage's fix shape

Stated so that round 3 can judge each one:

- **N2-2, where the inbound assertion sits.** The triage puts it at the entry of the inbound overload.
  §2.2 puts it at every function that builds a resource over the session's parse buffer, the inbound
  overload and `validate_inbound_`, because the validate gate parses a nested frame over the buffer
  before dispatch and can end the arm before the overload is reached. NC-19's validate arm and its mutant
  row are the witness.
- **N2-2, one case stays undetected** at this round, and §2.2's table says so: an emit that is not a
  send, begun inside an inbound or lifecycle callback. The triage's two assertions do not catch it and
  nor do these. Round 3's N3-2 added a second undetected case (below).
- **F-1, which sessions are exposed.** The triage describes the read pump's next completion running
  inside `fromApp` under `direct_executor`. By source, every session under the Engine adopts a strand the
  Engine made, whatever its `mode` (`Engine::start()` sets `engine_adopt_strand`; `Session::open()` uses
  it first), so the read pump is never in that position. §2.1 states the exposed setup as a `Session` an
  application drives itself, and says why the cell fixture's `ExecKind::io_context` is in the same
  position; NC-16's control arm therefore uses `ExecKind::strand`.
- **N2-4: disclosed, not extended.** The triage offers both; the reason is in §5, and §7 adds the send
  nested in the same Logout's `fromAdmin`.
- **N2-3, NC-9.** Arm (a) keeps an ordinary handshake and is stated as proving detection only. Arm (d)
  is the TSan arm, with a relaxed flag as the only ordering and a matcher that rejects a ThreadSanitizer
  report; P-7 says what follows if a plain token draws no report.
- **F-4, NC-8's second arm.** Both closes are graceful: with a terminal waiting close the mutant's second
  run of the body calls no hook and emits no Logout, and the arm could not fail.
- **F-2, the bench's isolation.** The triage's example is a send that `toApp` vetoes. §3 takes a payload
  that `build_send_frame_` refuses, so that the timed part ends before the frame build, and runs it on
  one thread, as the manifest's selection rule requires.
- **F-7, NC-4's fifth arm** asserts only that the send's `toApp` runs, which is what reading settles.
- **N2-2, NC-20** produces the triage's "timer emit" by advancing the mock clock inside the callback.
- **N2-9, §2.5's second added route.** The triage calls it a nested send on a chain that is already
  cancelled. A chain cancelled before the send's awaitable is awaited throws before `Session::send` is
  entered, so §2.5 states the route as a cancellation recorded before one of the nested send's own awaits
  begins.
- **Not in the triage:** each assertion has a text of its own and each death arm matches it, with the
  I-3 test's expected text edited; NC-3's mutant row; the coverage waiver registered in advance for the
  failing side of each new assertion.

### Round 3 — how each P3 was applied

Edits after convergence, applied by a sub-agent from the judge's fix shapes, one line per finding. None
was reviewed.

- **Codex F-1:** §2.2's token row states the order: the swap is the scope's first action and the clear
  its last.
- **Codex F-2:** the lease-counter assertion is not taken (owner). The snapshot read stays on §3's list
  of mechanisms without a witness, with the triage's reason.
- **Codex F-3:** §2.1a's row keeps the refusal for `open()` and states `drive_reconnect()`'s engine-only
  convention, with no run-time refusal.
- **Codex F-4:** §2.4 and §4 ("Compatibility") say that the widening is observable and is not a breaking
  change. The Compatibility bullet's opening clause is scoped to match: no call that succeeds today
  fails afterwards.
- **N3-1:** §3 ("Cost") has the same-tree spread check against the comparator's noise band. §4's table
  has rows for the bench gate's design record and for `MANDATORY_PAIRED`.
- **N3-2:** §2.2 has the second undetected nesting; §2.1a's row and the guard's B&L row point at the
  "not detected" rows where they named one case. The optional assertion is taken (owner), after a read
  of the routes to the `onLogon` and `onLogout` sites: both assert that the outbound depth is zero.
  NC-19 gains a lifecycle arm, the mutant table a row and P-10 the arm's prediction; the `onLogon`
  site's half has no death arm and is listed as specified without a witness.
- **N3-3:** §3 ("Cost") says that `session_send_bench` has no manifest row, names the dictionary the new
  bench needs, and names the scripted read's timer as a cost inside the timed part.
- **N3-4:** the fixture list gains the `onCreate` hook (NC-11) and the `Application` on
  `ValidateGateFixture` (NC-19), and, for the lifecycle arm, a frame builder that takes the BeginString.
  NC-16's control arm and P-10 say that the fixture's strand is the outer one, not the session's own.
- **N3-5:** §1 scopes "the only way in" to `Application` callbacks and names the store's synchronous
  part and the config-time `transport_send`; §2.1a's bullet covers a send as well as a `close()`.

### v0.7 — how each /analyze finding was applied

Edits applied by a sub-agent from the `/analyze` report and from the orchestrator's dispositions at its
end, one line per finding. None was reviewed. The report's line numbers are v0.6's.

- **A1:** §2.1 ("The payload") says that `Engine::send` copies the payload twice before `Session::send`
  runs and that the deferred path makes a third copy, as §2.3 and §7 have it.
- **A2:** §2.3's step 3 says that the rule has no witness and that `~Engine` already forbids the scenario.
- **A3:** §7 says that `L-023-2` stays open for establish-churn.
- **A4:** §3's UBSan line describes each depth as §2.1 and §2.2 do.
- **B1:** §3 ("Cost") fixes the order: the bench's commit, the production edits, the local run, the
  manifest's commit. §4's rows agree.
- **B2:** a mutant that stays green is settled before the verification record's verdict is written.
- **B3:** NC-11 says "close modes".
- **B4:** §2.1's step 5 names the check that refuses a send after a close has finished.
- **B5:** §3's introduction to the predictions says which measurements run on today's tree and which on
  a scratch build.
- **B6:** §3's first paragraph keys the arms on assertions and on `NDEBUG`, and says that every cell also
  passes in a whole-binary run.
- **B7:** NC-2, NC-3, NC-16 and NC-18 state each open parameter as the implementer's choice, with what
  constrains it.
- **C1:** NC-11 names its observable for "no liveness loop" (the mock clock's parked sleeps, with a
  non-zero heartbeat interval) and the route of the `Disconnected` write outside `close()` (a frame with
  another BeginString). The fixture list follows.
- **C2:** NC-13 names its ASan arm, the terminal close started in `fromApp`; the ASan line agrees.
- **C3:** §3 ("Cost") puts the in-tree benches under the route bench's rule, names the local run's
  command, and says what p1's base is.
- **C4:** NC-6 asserts its outer operation's result, and whether the store and the manager agree, as
  measured.
- **C5:** NC-7 says how its graceful arm ends the wait.
- **C6:** §3 names the close cells and excepts a row that fixes the transport or the store.
- **C7:** the coverage scope includes `src/session/session_engine_access.hpp`.
- **C8:** §2.1's table has a row for `onLogout` and `onCreate`, written from `Session::send`'s Active
  check and from the order of the write and the callback in `record_state_transition_`.
- **C9:** §3 says where NC-9's test access, NC-14's store double and NC-19's validate arm go, and where
  `check-comment-claims.py` is.
- **D1:** the header, §0a and §6 record the Appendix A controls as done, with the owner's words on the
  plan sign-off.
- **D2:** §0a and the first line under "Round 3 — how each P3 was applied" say who applied each revision.
- **D3:** §0a has the `[const §VII.3]` disposition.
- **D4:** the text route. §0a has the allocation disposition, and §2.1 names the copy's storage. Flagged
  to the owner.
- **D5:** §2.4 and §4 ("Compatibility") state the article's conditions. The re-derivation was read
  against the comments in both C-ABI headers before it was written.
- **D6:** §0a's Gate A bullet is in the past tense, and §0a has a bullet for the `[const §XVII.7]` build
  and the MSVC leg.
- **E1:** R-3 is referenced in §2.5, in NC-13's row and on the ASan line.
- **E2:** the text route. The default mode's immunity is listed as specified without a witness, and
  P-10's last column is reworded.
- **E3:** "Mutant first" names every exception, and NC-2's `fromAdmin` arm joins the second mutant row.
- **E4:** the sends R-2 admits, and OD-3's refusal on R-5's route, are listed as specified without a
  witness.
- **E5:** the row of `ToAppStartsClose_Terminal` has P-1a's arm.
- **E6:** the controls row names `ToAdminStartsSend_AssignFirstSite_Sequential`.
- **E7:** §0 says what OD-1, OD-3 and OD-4 are and where each is used; §2.1, §2.1a and §2.3 reference
  NC-2, NC-6, NC-1 and OD-4; §6 says what OD-2 was.
- **F1:** the text route, in part. §2.2 narrows the cross-thread claim: the parse sites and the
  stack-arena overload's entry read a depth before a token is reached. The report's wording that puts
  the stack-arena overload's assertion and the lifecycle sites' after their scopes is not applied: it
  moves an assertion, and the disposition says that none moves. Settled afterwards by the orchestrator
  (the `## Gate A` list): both are evaluated after their scopes and before their callbacks. By source
  each scope is constructed immediately before its callback, so neither assertion moves relative to
  its callback, and §2.2 now says so.
- **F2:** the coverage waiver covers the assertion at the `onLogon` site.
- **F3:** §2.1 says that the lifecycle sites do not write the outbound depth and that the `onLogon` and
  `onLogout` sites read it.
- **F4:** P-4's row has its arms as the probe ran them.
- **F5:** §0's OD-5 bullet says that the paired bench is single-threaded and replaces the foreign-thread
  probe as the gated evidence. §3 ("Cost") keeps that probe as a local step. §4's `L-023-2` text, §6 and
  §7 agree.
- **F6:** §0's R-5 bullet and §4 ("Compatibility") are scoped to the route R-5 sets aside.
- **F7:** §2.1 says that NC-16's fourth arm runs on `ExecKind::strand`.
- **F8:** the header, §0's paragraph on the record, §1's GapFill paragraph, the paragraph under §2.1's
  table, §2.5, §4's limitation bullet, the round-2 line on N2-2 and this section's line on v0.6 are
  corrected.
- **The measurements:** the rows of P-4 and P-11 record each result and its evidence files. The header,
  §0, §0a, §2.1, §2.1a, §2.4, §4 and §6 say what has been run. §2.1 has the cancellation that a later
  emission erases. §2.4 no longer says that the Active check bounds a send admitted during `stop()`.
- **From the task-list derivation:** NC-17's order assertion joins the first mutant row, and its
  assertion on the outer frame's bytes is named a pin. The GapFill and failure-exit branches of §2.1's
  first-yield table are listed as specified without a witness. §4's ambiguous "the same" row names
  `include/fixpp/session/session.hpp`.

### Round 4 (scoped, on v0.8) — applied in v0.9

One line per finding. F-n are Codex's, with the judge's verdict; O-n are the judge's own. This section
and the two after it record v0.9 as round 5 then reviewed it. Where v0.10 changed a passage they name,
"Round 5 (scoped, on v0.9) — applied in v0.10", below, says so.

- **RC-1 (statement 4):** replaced. The cut step leaves at once, the direct close of the transport is
  keyed on the cut as well, and the terminal close's result needs nothing new (§2.6, "The
  mechanism", statement 4; "Why this is by construction"). The six measurements the triage asks for
  before the rewrite were made on prototype S5 (§3, P-12 and P-16 to P-19).
- **F-1 (P1, judged P3):** §2.6's table is three groups, each with what decides it (RC-2). The forced
  order of the terminal close's turn is stated as read, with mutant C as its measured control. The
  two rows that depended on which arm of the join won are now by construction, within the bound that
  §2.6 states. The arm the triage offers for the starved case is moot: the cut route has no wait.
- **F-2 (P1, confirmed):** the step's exit is now R-17's. `logout_seen_` follows B28's ruling R-24
  (§0), which answers the triage's owner question 1. §2.6, "With batch B28", has the table for a tree
  with both changes, the merge rule, and the assertions of the cell that waits for both.
- **F-3 (P1, judged P2):** the false sentence is corrected, and the statement is in §2.6, "What an
  inbound frame meets". The frame after the store call is measured (P-17, NC-27). What a Logout from
  the peer does during the store call is added to "The behaviour change", as read. §7 has what was
  not run.
- **F-4 (P2, judged P3):** the blanket sentence is withdrawn in §3's last block and in §0a. Each
  cell's row says RED first, control or pin.
- **F-5 (P2, judged P3):** the statement about `main` rests on a reading of `main`'s
  `Session::close`, for all six arms, checked for this revision; the runs on this branch's code are
  kept as support (§2.6, "The behaviour change").
- **F-6 (P3, the judge disagreed):** not applied (below). The count's width is named: 64 bits.
- **O-1 (P1):** RC-1. The held-lock scene is NC-26, measured RED on S3 and green on S5 (P-16). Its
  state at the head is a prediction to run. S3 is in §5 as rejected.
- **O-2 (P2):** NC-26 is the scene on the shipped `FileStore`, with a clause that no sleep is in
  flight and a clause that the closes complete with the mock clock standing. NC-24 (a) asserts that
  the mock clock is not advanced. NC-24 (b) is rewritten for statement 4, under which a cancellation
  reaches the store call only at the grace timer's expiry: two doubles, one that leaves the caller's
  cancellation state alone and one that loses the record as the shipped stores' lock does.
- **O-3 (P2):** §5's rejection of "the step leaves by itself" is withdrawn, with the two premises that
  were wrong. S2's split of the close count is put down to the key of the direct close. The mutant
  that v0.8 left without a trace is gone with the claim "the step gains no exit".
- **O-4 (P3):** the number is in group 3, with a row for a store failure on a persistent store and
  the reconcile.
- **O-5 (P3):** one sentence on who writes `Disconnected`: the step, before it returns (group 2).
- **O-6 (P3):** `Engine::stop()` started in that `toAdmin` is the third way in §7's first bullet and
  in §4's list for the new row. The orchestrator adds it to the issue.
- **O-7 (P3):** NC-25 (c) is settled in its row, with the triage's trace. P-15's fallback for (c) is
  dropped.
- **O-8 (P3):** fixpp's teardown reset stays as it is on this route; the difference from QuickFIX is
  stated in the comparison table and in §7. Decided by the orchestrator and reported to the owner
  (§6), in place of the owner's answer the triage asks for.
- **RC-2:** F-1's line. **RC-3:** F-4's and F-5's lines.
- **Owner question 1:** B28's R-24. **Owner question 2:** statement 4's third part, and §2.6, "The
  bound"; decided by the orchestrator (§6). **Owner question 3:** O-8's line.
- **"Not established", the triage's four items:** O-1 on the prototype is settled by P-16. The other
  three are carried in §7, each with what would settle it: how often the lock is held; the QuickFIX
  half of O-8; the condition on an Engine-managed session. **Codex's one item** (the issue's text) is
  in §7's last bullet.

### Round 4 — disagreements

- **F-6 (Codex, P3), a wrap of the count: not applied.** The judge disagreed: a wrap between the two
  reads needs as many terminal closes started inside one `toAdmin` as the member can count, each
  holding a suspended frame. Codex's fix (a latch scoped to the callback, or a generation scheme) is
  not taken (§5). The note names the member's width, as the judge allows.

Three findings are applied, but not with the fix Codex proposed. The reasons are in §5:

- **F-1:** Codex proposed a registered waiter on the close's result. The judge's trace makes it
  unnecessary.
- **F-3:** Codex proposed a state or a gate against Active inbound dispatch. The step's own
  `Disconnected` write ends the window after the store call, and what is left is R-4's.
- **F-5:** Codex proposed a run of the missing arm on `origin/main`. The judge found a reading of
  `main` that covers all six arms, and no run on `main` was made.

Codex's "combined cells before either merge" (F-2) cannot be required of this pull request, as the
judge says: no tree carries both changes. The note names the cell's assertions instead (§2.6).

### Round 4 — where this revision goes beyond or beside the triage's fix shape

Stated so that round 5 can judge each one:

- **Nothing cancels the cut Logout's store call** (statement 4, third part). The triage's RC-1 leaves
  the join's waker as it is and gives the remainder to the owner as question 2. v0.9 has a terminal
  close that joins not call the grace timer's wake for a cut Logout. It removes the route on which
  the join's cancellation meets the store call at a lock before the timer expires, and it costs the
  bound: a terminal close and `Engine::stop()` wait for the cut Logout's store call, the route
  returns at the timer's expiry for a call still waiting for the lock then, and 093's OD-29 is
  narrowed on this route. The orchestrator's addition; the judge has not reviewed it.
- **The third part does not depend on R-4's wait.** The orchestrator's brief for this revision said
  that it does, and withdrew the sentence after the run: with the wait's post removed (mutant A) every
  cell and scene stays green, because a wake called inside `toAdmin` meets a timer whose wait has not
  been initiated. §2.6 states that, with its reservation: it rests on asio's order of starting the
  operands of `||`, read at the pinned version.
- **A mutant the triage does not name:** the test of the record removed from the wake, with the wait
  kept (mutant D). It is the one that tests the third part. Measured: the held-lock cells are RED,
  and no other cell sees it.
- **The bound as a pinned cell** (NC-28) and as a B&L sentence, where the triage's measurement list
  has no item for a store call that does not return in time.
- **NC-26's recording wrapper.** The prototype's proof that the store call was at the lock is
  indirect on S5. The cell gains a direct clause: the Logout's `store` call has been made and has
  not returned.
- **NC-24 (b)** is two doubles past a short timeout, not the one double the triage's O-2 edits.
- **The owner questions.** The triage has three for the owner. One is answered by a ruling the owner
  gave to B28. Two were decided by the orchestrator and reported to the owner; §6 records them as
  that.
- **Found while applying the round, in neither review:** the B&L row of 093's OD-29 says that
  `Engine::stop()` during a graceful logout closes at once, and §2.6's bound is an exception to it
  (§4). A session that reaches `closed_drained` while a store job is still in flight was observed in
  the bound's measurement and is carried in §7.

### Round 5 (scoped, on v0.9) — applied in v0.10

One line per finding. F-n are Codex's, with the judge's verdict; O-n are the judge's own. Not reviewed.

- **RC-1, F-4 (P2, confirmed):** the cut step writes `Disconnected` at the decision point (§2.6,
  statement 4, "The state, at the decision point"). Group 1 gains the state and `onLogout`'s
  position. "What arrives after the cut is decided", the comparison table's `onLogout` row, §5's
  entries on S5 and on a gate, and §7 are rewritten from the measurements on prototype S6. NC-27
  gains the twelve hold arms, the send arm and the liveness arm among them. The mutant table has
  the two rows the triage asks for, as mutants E and F, measured.
- **RC-2, F-1 (P1, judged P2):** the owner's ruling is R-9 in §0, with the question and the answer.
  The bound is stated once, in §2.6, "The bound", with what it covers and the points it does not;
  §0a, §3, §4, §6 and §7 refer to it. §6 and §7 no longer call it the orchestrator's decision or a
  limit. NC-28 is the cell of a ruled behaviour, and the completion time of the closes is measured
  (P-18) and asserted.
- **F-2 (P2, judged P3):** statement 4's part on the direct close and group 2 say what the pair of
  calls is on TLS, that the head does the same on every live arm of this condition, why the call is
  kept, and that its original reason is absent on the cut route. The question over every escalation
  is fixpp#596. NC-21's count is named a pin of the call sequence.
- **F-3 (P2, judged P3):** the short forms of the bound are gone; every other place refers to §2.6's
  one statement.
- **F-5 (P2, judged P3):** §7's bullet says which job was in flight and that the head reaches the
  state without the amendment. The issue is fixpp#597.
- **F-6 (P3, confirmed):** the order of a reset on disconnect on this route is marked "with the owner
  (asked 2026-10-13)" in the comparison table, in §6 and in §7, with both endings and the judge's
  reading of QuickFIX C++. No decision is recorded.
- **F-7 (P3, confirmed):** the bullet on R-4's wait is headed by what the design rests on; the
  result of mutant A is stated as an observation at the asio version read, with the inline dispatch
  named; "the ending does not depend on the wait" is dropped.
- **O-1:** "The bound" lists the step's awaits on the cut route with what the timer's expiry does at
  each, says from when the timeout runs, and has the `toAdmin` that outlasts it. "Why this is by
  construction" names the step's awaits where it said "no handler other than the store call's".
- **O-2:** group 3 has a row for a failure on a store that is not persistent and for a throw of the
  store's body.
- **O-3:** NC-26 has the fatal assertion that the held `on_inbound_frame` has not returned at the
  snapshot; it is in the prototype's cell and held in every run. The measured mutants were run
  again on the prototype's final cells (P-19) and are run again on the cells as written.
- **O-4:** NC-24 (b) and NC-28 say what they assert: a bound, with the completion time read. NC-26
  is named as the cell that sees a cancellation before the expiry; NC-28 also gains a lower side
  (below).
- **O-5:** two mutant rows: the wake suppressed for every Logout, and the direct close made on every
  route. NC-25 (a)'s live arms gain the clause that is the second one's kill cell.
- **O-6:** §2.6, "The bound", (c), and §7 have the sentence on the Engine. The arm stays owed.
- **RC-3:** the lines of F-2, F-3, F-5, F-7 and O-1 to O-6.
- **The four facts the owner must be told** about the ruling's last sentence: told on 2026-10-13
  (the orchestrator), and stated in §0 and in §2.6, "The bound".
  **Marked in v0.11:** §0, R-9, "What the owner was told", now says when and what, from the task
  list's section of that name.
- **"Not established", the triage's items and Codex's:** each is in §7 or in §2.6, "The bound", with
  what would settle it. The triage's item on the Logout's own write at the expiry was measured for a
  job that is still queued (P-22); a job already inside its system calls stays read, not run.

### Round 5 — disagreements

Codex's findings that the judge re-judged, and Codex's counter-proposals that are not applied as
proposed. The reasons are in §5:

- **F-1 (Codex P1; judged P2):** Codex read the bound as a second ending of R-8's condition. The
  judge found that it needs an input from outside the condition, and that the owner has ruled it.
  Codex's two options were put to the owner as R-9's question; the first was chosen.
- **F-2:** Codex proposed no direct close on the cut route and a real-TLS cell. The judge found no
  change against the head. A clean TLS shutdown there is the question of fixpp#596, for every
  escalation.
- **F-3:** Codex proposed an independently owned lifetime for the store operation, so that a close
  can stop waiting. Not taken; R-9 rules the bound, and its reach is stated.
- **F-4:** Codex offered "accept and document both outcomes" beside a gate. The first is excluded by
  the owner's rule; the second is taken in the form of the state's write.
- **F-5:** Codex proposed that `close()` join every store operation of the session before it
  publishes `closed_drained`. The judge found the state at the head; it is fixpp#597.
- **F-7:** Codex proposed a compatibility cell that pins asio's launch order. Not added: the design
  rests on R-4's wait.

### Round 5 — where this revision goes beyond or beside the triage's fix shape

Stated so that round 6 can judge each one. **No reviewer has seen the first two.**

- **The Logout's own write job at the expiry was measured after the triage. It is outside the
  ruling's last sentence, and it goes against the option's first sentence for that case: the timer
  does not end the close there.** The triage lists it as bound case 2, read, not run. The
  measurement (P-22): the closes do not complete at the expiry; they complete when the file executor
  takes the job. §2.6, "The bound", (a), states it as measured. The owner was told on 2026-10-13.
  **Corrected in v0.11:** the closes complete when the file executor has run the job, which is not
  always when it takes it; and what the owner was told, with the later correction, is in §0, R-9.
- **A defect of the head met in that scene: fixpp#598.** `FileStore::store` reports success for a
  frame it never wrote when a cancellation reaches it before its write job starts. It is on the
  route with no cut as well, and on `main`. The note states what was measured, cites the issue, pins
  the reading in NC-29, and does not present it as this amendment's ending. Under mutant D the six
  `FileStore` hold arms of NC-27 reach the same state, which is why they are RED there.
  **Corrected in v0.11 (round 6's O-1):** v0.10 stated this too widely. That state needs the
  cancellation's job to run before the write's body starts, which a file executor of one thread does
  not allow; the measurement behind this bullet used a gate that reordered the two jobs. NC-29 no
  longer pins that state, and under mutant D the `FileStore` hold arms of NC-27 stay green on a hold
  that keeps order (§2.6, "The bound", (a); §3).
- **NC-28 asserts a lower side on the completion time.** The triage says a "not before" snapshot
  would race the timer. The cell does not take a snapshot: it reads the time at which each close
  completed and requires that it is not before the timeout has elapsed since the graceful close
  started. A timer does not fire early, and the measured smallest value equals the timeout. With it
  the cell also sees a cancellation that comes before the expiry. Not run with the assertion.
- **NC-21's sixth arm is RED first by itself.** The reading inside `onLogout` is RED at the head on
  every arm, the `FileStore` arm on the live transport included (predicted). NC-22 stays, for the
  clause "not on the wire".
- **NC-30** pins the send that is suspended in its own store call, which the triage asks for as a
  reading.
- **The later write of `Disconnected` is kept** in the step's exit, where the triage's shape says the
  exit "keeps its test and finds the state written". It does nothing on the cut route, and mutant E
  is therefore the previous prototype itself.
- **A `toAdmin` that lasts longer than the timeout** is stated in "The bound" for the offload as
  well as for the lock; the triage's O-1 names the lock.
- **Found while applying the round, in neither review:** at the head the reading inside `onLogout`
  is the Logout's number plus one on every arm, so the position of `onLogout` relative to the
  Logout's number is a change against the head on all six arms; §2.6, "The behaviour change", says
  so.

### Round 6 (scoped, on v0.10) — converged; applied in v0.11

Converged: Codex P1=0 P2=1 P3=2; after judging P1=0 P2=0 P3=9, and the judge states the tally as
unconditional. Reviews: `research/reviews/codex_563_564_6_callback-send_review.md` and
`research/reviews/opus_563_564_6_callback-send_triage.md` (parent repo). v0.11 applies the nine P3
items. **No Gate A round reviewed these edits.** A sub-agent made them from the judge's fix shapes,
from the orchestrator's brief, and from the evidence files named `ph8d_*` and `ph8e_*`. One line per
item. F-n are Codex's, with the judge's verdict; O-n are the judge's own.

- **F-1 (Codex P2, judged P3):** the judge's closure list is applied item by item. §2.6, "What
  arrives after the cut is decided", has the outcome per operation, each with its status, and what
  is new at that position. §3 has prediction row P-23, and the cells as a row of their own, NC-31:
  twelve arms as controls, which their path guard stops at the head, and two arms with the lock held
  as a second kill of mutant D. Mutant D's row has them. Mutant G is under "Specified without a
  witness", as measured green. The fixture list names the two accessors as readings. "What was
  measured, and its limits" has what was not run. `Engine::stop()` from that `onLogout` is named as
  R-5's route, and no ending is stated for it.
- **F-2 (P3, confirmed):** NC-27's oracle is split into the common assertions, the two arms after
  the store call, the hold arms, and the assertions of each arrival.
- **F-3 (P3, confirmed):** §0a marks Appendix A's "Session FSM" row touched, with the reason. The
  four controls are the same, and `/analyze` and the sign-off stay owed.
- **O-1:** the measurement the judge asks for was made (`ph8e_*`). §2.6, "The bound", (a), is in two
  halves: the timing, on every kind of executor that was run; and what the store holds, by kind of
  executor, with the gate named as the reason v0.10's statement was too wide and with where the host
  supplies the executor. P-22, NC-29, mutant D's row and trace, the fixture's gate (each mode says
  whether later jobs pass), P-13 and NC-22's second arm, NC-27's `FileStore` hold arms and their
  prediction at the head, the route list of "With batch B28", and §7's bullet are rewritten from it.
  The orchestrator changed the issue's title and rewrote its body, which now states the condition on
  the executor and records its first wording as history (§7).
- **O-2:** §0, R-9, has "What the owner was told", from the task list's section of that name: the
  two messages, the doubt told before the re-run, and the re-run's result. The other sentences that
  said "told" point there. §7 says that the section is the orchestrator's account.
- **O-3:** statement 4's exit has the test at the exit after a failed `assign_outbound()`. "With
  batch B28" says which of B28's two writes records a second visit of `Disconnected` on a tree with
  both changes, and the cell that waits for both asserts one visit. "The bound" no longer lists a
  step queued at the sequence manager's lock.
- **O-4:** "The behaviour change", "Against the head", is written arm by arm, the sixth arm
  included, with where `onLogout` runs today on each, from the measurements.
- **O-5:** §4 has a limitation row for the logout timeout and a write job in the file executor, with
  its evidence, and says why no issue is filed for the timing alone.
- **O-6:** NC-28's lower side names the clock and the start instant, and NC-24 (b) refers to them.
  The item's other two parts need no edit of their own: the judge found no "path not driven" stop
  counted as a kill, and NC-22's second arm is under O-1.
- **"Not established":** each item is in §7 with what would settle it, apart from the first, which
  `ph8e_*` settles: on a file executor of one thread, which keeps order, the Logout is written and
  reads back.

### Round 6 — disagreements

- **F-1:** Codex judged the missing witness P2 and proposed sub-arms of NC-21. The judge traced each
  operation to one forced outcome and found the branch's kill cell measured: P3. v0.11 writes the
  cells as a row of their own, NC-31, and not as sub-arms: NC-21 asserts the same in every arm, and
  these arms do not.
- **Codex's disposition audit** found the description of the store defect to agree with
  `FileStore::store`. The judge found that the audit had not checked how the cancellation reaches
  the spawned job (O-1). The re-run agrees with the judge.

### Round 6 — what v0.11 adds beside the judge's list

Stated so that a later reader can judge each one. None was reviewed.

- **The test of O-3 is a production statement.** The orchestrator's brief for v0.11 says that round
  6 changes no mechanism, and it does not list the test. The judge's fix shape for O-3 and item 6 of
  the closing recommendation do. v0.11 specifies it, in statement 4's exit, and lists it as
  specified without a witness. Accepted by the orchestrator on 2026-10-13, after v0.11's first
  commit: the exit after a failed `assign_outbound()` writes `Disconnected` only when the state is
  not that already, so that the state is recorded once on the cut route. No Gate A round reviewed
  it.
  **Marked in v0.12:** the test has a cell now, NC-32, and is no longer in "Specified without a
  witness" (the analysis pass's finding D-1).
- **NC-27's `FileStore` hold arms are specified on the hold that keeps order.** The judge asks that
  the text state the gate's condition. v0.11 changes the fixture, so that no arm that holds the
  Logout's own write depends on a gate that reorders. S6 and mutant D were measured on that form. S5
  and mutants E and F were not, and their rows say so.
- **NC-29's form was decided again:** the order-keeping executor only. The reasons are in its row
  and in §5.
- **NC-22's second mode is specified to keep order.** No prototype built it.
- **§0's bullet on B28's R-24, and "With batch B28",** name B28's head as read again for v0.11, and
  say what that note's v0.17 added.
- **Four sentences said "told to the owner", where the brief counts three:** in §0, in "The bound",
  and two in "Round 5". All point at the task list's section. The two bullets of "The bound" after
  (e) are no longer covered by "told": that section does not record them.
- **A strand is read, not run.** The brief names a strand beside one thread as an executor that
  keeps order. `ph8e_predictions.md` lists a strand over a pool as not covered, and §2.6 says so.
- **`CallbackSend.OnLogoutOfGracefulCloseStartsClose_Terminal`** is named in §3's "What is run
  again".
- **Round 5's entries above are kept as written and marked** where v0.11 corrects what they say.
- **A sweep for the old statement.** After the edit the note was searched for sentences that still
  state the store defect with no condition on the file executor, or that still count NC-27's
  `FileStore` hold arms as a kill of mutant D. The recipe: join the note's lines, split the text
  into sentences, and search for the phrases v0.10 used for both. Run it first on v0.10
  (`git show b9b8b788:.specify/563-564-callback-send.md`), where it must report hits, and on v0.7,
  where it must report none; then on the head, and read every hit that is left. A hit that is left
  must carry the condition on the executor, or be a dated record of what v0.10 said that is marked
  as corrected.

### v0.12 — the owner's sign-off, two rulings, and the analysis pass applied

**The sign-off and the rulings.** On 2026-10-13, at about 20:50, the owner answered three questions
that the orchestrator put with a summary of §2.6 at v0.11: the amendment is signed off;
`reset_on_disconnect` on this route keeps fixpp's behaviour; fixpp#598 goes into batch B28. §0 has
the questions and the answers as the task list records them, and numbers the second R-10. The
header, §0a and §6 record the sign-off. The comparison table's row, §5, §6 and §7 no longer say that
the reset is with the owner. The old batch of that issue is replaced by B28 throughout; §0 names it
once, as the record of the finding.

**The analysis pass.** One line per finding of
`decisions/speckit/563-564-callback-send-analyze-phase8.md` (parent repo), with the orchestrator's
decision as that file has it. No Gate A round reviewed these edits. Nine findings name the task list
only; they are the orchestrator's and are marked so.

- **D-1 (accepted):** a cell for the exit after a failed `assign_outbound()` on the cut route,
  NC-32. Statement 4's exit names it. The mutant row for the removed test names it as its kill cell,
  and the entry leaves "Specified without a witness". It is a pin, predicted green at the head; its
  RED run is on the scratch patch without the test.
- **G-1 (accepted):** the fixture's bullet on the two accessors says that they land in a tests-only
  commit after the production commit, and that no assertion depends on them.
- **D-2 (accepted):** the task list's. §0a cites its decision: no library commit of the
  implementation before both carry the remediations.
- **D-3 (answered by the record):** the task list's; nothing in this note.
- **E-1 (accepted):** the sign-off, R-10 and the placement, as above. (i) NC-33 pins the ruled
  reset, on a `MemoryStore` and a `FileStore`. (ii) "With batch B28" adds NC-29 and NC-27's
  `FileStore` hold arms to what is run again on the tree that carries both; with F-1's check, the
  arms that use the gate's third mode are added there too.
- **F-1 (accepted):** the fixture states the condition of the gate's third mode, why it holds in
  each arm that uses it, and NC-30's `FileStore` arm among them. NC-26, NC-28, NC-30's `FileStore`
  arm and NC-31's two arms with the lock held assert that the store's catch counter reads zero.
  NC-30's arm also retrieves the send's frame.
- **F-2 (accepted, the smaller option):** on NC-23's sixth arm the clause on the reset is a pin, and
  the arm is RED first through NC-21's reading inside `onLogout`.
- **F-3 (accepted):** NC-28's upper side is a relation fixed before the cells commit: under two
  timeouts, with the gate kept closed for three. NC-24 (b) uses it.
- **F-4, E-2, G-2, C-1, C-2, G-4, F-5:** the task list's; nothing in this note.
- **E-3 (accepted):** NC-21 has a seventh arm, on the strand fixture.
- **G-3 (decided):** the two members keep the prototype's names, `waited_terminal_closes_` and
  `logout_cut_`; statements 1 and 4 name them.
- **C-3 (decided):** "What is run again" names all three paired benches, with the two reasons.
- **B-1 (accepted):** the fixture says how the gate's second mode knows the Logout's store job, what
  "run dry" is, and that the prototype did not build that mode for NC-22's scene.
- **C-4 (accepted):** §3's last block has the rule for a prediction about the head that disagrees,
  and the rows of NC-21, NC-24 (a), NC-27 and NC-29 refer to it.
- **F-6 (accepted):** the mutant row for the removed increment names every arm of NC-21, the sixth
  at the reading inside `onLogout`. The row for the wake suppressed for every Logout names
  `CallbackSend.OnLogoutOfGracefulCloseStartsClose_Terminal`.
- **B-2 (decided):** the cut exit returns `dispatch_aborted`; statement 4's exit says so.
- **B-3 (accepted):** §4's new behaviour row names the three issues.

**What v0.12 adds beside the decisions.** None was reviewed.

- **NC-33 has a mutant row,** "the teardown reset not made when the Logout was cut", so that the pin
  has a named kill.
- **NC-33 is labelled clause by clause.** Its clauses on the reset are the pin of R-10, predicted
  green at the head. Its clause "not on the wire" is R-8's and is predicted RED at the head on both
  arms.
- **NC-32 has one arm,** `MemoryStore` on the synchronous sink, because the step returns before any
  store call or transmit.
- **The readback of F-1's decision cannot discriminate for a counter write.** The decision's row
  says that "the kept writer's record reads back". In NC-26, NC-28 and NC-31 the kept write is an
  inbound counter advance, and `FileStore::next_seqnum` advances the counter in memory before its
  offload. So the number in the running store is the same whether the write ran or not. Those arms
  keep the number as a consistency clause, and the catch counter is their one discriminating check.
  A second store opened over the same files would make the readback durable; it is not specified.
  Only NC-30 keeps a frame's write, and its retrieval reads the file.
- **Why the third mode's condition holds is given by structure, not by a list of emitters:** the
  cell's completion handler has no cancellation slot. A list of the places where `Session::close`
  emits would go stale.
- **The mutant row for the removed increment also names the other arms that are RED first,** beside
  the ones finding F-6 asks for, and mutant A's row no longer counts NC-21's arms.
- **B-2's "as batch B28's exit"** is narrowed. On B28's branch that exit returns what the emit left:
  `dispatch_aborted` where the emit threw, and the emit's own error otherwise. Statement 4 says
  both.
- **This note's R-10 and "B35's ruling R-10"** are two rulings. §0 says how each is written here.
- **The header says "no ending that v0.11 stated"** and not "no ending": R-10 decides the one ending
  v0.11 left with the owner, and it keeps the head's behaviour.
- **§0a lists what remains owed at implementation,** since the four controls are met: the RED-first
  arms, B35's gates and the paired benches, and v0.7's local build cycle and MSVC leg on the tree
  with the change.
- **The predictions of NC-32 and NC-33 are in their rows,** with no prediction row of their own.

### v0.13 — the head states as rehearsed, the cells' names, four row corrections

Not reviewed by a Gate A round. No statement of the mechanism and no ending changed. The source is
the scratch rehearsal of 2026-10-14 (`ph8f_head_states_for_the_note.md`; `ph8f_chainV-results.txt`),
made so that §3's rule for a prediction about the head is met before the cells commit is called
RED-first evidence.

- **The head states.** §3's last block has a paragraph on the rehearsal and a bullet on its limits.
  Each row of NC-21 to NC-33 ends with what its cells did at the head in the scratch copy, and the
  prediction rows P-13 to P-18 and P-20 to P-23 each gain a sentence. The six arms whose state was
  not determined are RED: NC-24 (a) on the live transport, NC-24 (b)'s two, NC-28, and NC-30's two.
  The arms that were predicted did as predicted. NC-25's controls, read and not run before, are
  green. No measured state contradicts a label.
- **More than the labels asked, carried from the file.** NC-27's `FileStore` hold arms: what each
  arrival caused at the head, stored and transmitted above the retained Logout. NC-24 (a): at the
  head the store double with more hops does not hold the Logout, on either transport. NC-30: at the
  head the Logout is transmitted during the hold, so that pin is RED first as well.
- **The cells' names.** Each row names its cells, as the tree will have them.
- **Four row corrections.** NC-22's second arm: its path guard, and the condition behind the number
  of jobs it prints. NC-24 (a): a control cell on the reading of its two arms. NC-27: the order in
  which the hold arms test their path guards, and which guard stops the arm with the peer's Logout,
  `reset_on_logout` and the holding double at the head. NC-33: the store's next inbound number in
  its oracle, for both arms.
- **The mutant table.** One row changes: the row for the removed increment lists every arm that is
  RED at the head, NC-24 (a)'s control among them. The control adds no kill: it fails under the
  mutants that fail NC-21's `MemoryStore` arm on the synchronous sink, and under no other. The row
  of the test at the exit is now measured, on prototype S6.
- **Beside the brief.** NC-30's row gains the two clauses its cells fail at the head, in the cells'
  words. NC-32's RED run on the prototype without the test is recorded, in its row and in its mutant
  row, from `ph8f_chainQ-results.txt`, which the brief does not name. §3's "What is run again" says
  that the cells kept green were green in the rehearsal.
- **Stale statements replaced in place** (the second commit of v0.13). The rows of §3's last block
  no longer say of the head that a state is open or that a cell was not run: each such clause now
  says that the run in the pull request's tree is the cells commit's task, and points at the row's
  end for the scratch copy. Recipe: search the rows `NC-21` to `NC-33` and `P-13` to `P-23` for the
  phrases v0.12 used; the search must report hits on `d7bf5dea` and none on the head.
- **Two places outside §3 said the same and are corrected:** §2.6's "What the head does", its bullet
  on the held lock, and §7's bullet on predictions at the head.
- **"The behaviour change" gains the held-lock route.** The rehearsal shows that with the store's
  lock held the head transmits a Logout that the store does not hold, on both transports. "Against
  the head" said of the sixth arm that it transmits today in one order only. The new bullet states
  the route; the cause stays a reading.
- **Three precisions:** NC-22's second arm says "until the grace timer expires"; NC-27's reset arm
  cites the number the cell printed; the control's effect on the mutant table is stated as above.
