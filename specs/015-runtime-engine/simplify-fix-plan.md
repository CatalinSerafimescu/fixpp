# 015-runtime-engine — `/simplify` findings & dispositions

Pipeline step 11 (`[const §XVI.7]` — before `/speckit-verify`). 3 specialized Opus
review agents (reuse / quality / efficiency) reviewed the 015 production diff
(`git diff 6263291 HEAD` — base = merge-base with `main`). Orchestrator (Opus)
source-read every finding first-hand (agent severities are advisory only —
`[[feedback_simplify_pass_catches_9th_burn]]`) and triaged.

Scope reviewed: `error.hpp`, `engine.hpp`, `session.hpp`, `session_config.hpp`,
`src/session/engine.cpp`, `src/session/session.cpp`, `tests/support/identity_injecting_transport.hpp`.

## FIXED in-slice (submodule commit — see git log)

| ID | Finding | Fix |
|----|---------|-----|
| **Q-1** | **Detached outbound-write UAF window.** The rebound `transport_send_` fire-and-forget `co_spawn`s `raw->async_write` capturing a raw `Transport*`. These detached writes are **not** tracked by `Engine::stop()`'s join counter, so `registry_.clear()` could free the `Transport` while a write is still suspended in `async_write` → use-after-free (the 014 Gate-B UAF class; would surface under the verify ASan matrix). | Changed `reconnected_transport_` / `accepted_transport_` from `unique_ptr` → **`shared_ptr<Transport>`** (private members; 6 sites, no public/test surface change) and had the detached write capture a **shared keepalive** copy. An in-flight write now keeps the transport alive past `clear()`. Robust by construction, not scheduling-dependent. |
| **R-1** | The `transport_send_` rebind lambda was duplicated **byte-for-byte** across `install_reconnected_transport` and `attach_accepted_transport` (the symmetric-API pair — the half-restructure burn class). | Extracted one private `Session::make_live_send_(shared_ptr<Transport>)` helper; both attach sites call it. Single source of truth for the sync→async bridge (also carries the Q-1 keepalive). |
| **Q-2** | **First-frame deadline did not bound a handshaked-then-silent peer (FR-014/SC-011 gap).** `read_first_frame_bounded`'s timer callback only set a `timed_out` flag checked *between* reads; a peer that completes the TLS handshake then stalls blocks `async_read_some` forever — the deadline never fires it. (The existing `engine_firstframe` "silent peer" test passes via the *handshake* timeout, a raw-TCP probe that never reaches this read — so this path was untested.) | The timer callback now calls `transport.cancel()`, aborting the pending read; the read-error arm returns `transport_handshake_timeout` when `timed_out`. Normal path unaffected (frame-arrival cancels the timer first). |
| **R-3** | Dead `Engine::send_slots_` member (`unordered_map<SessionId, function<...>>`) — declared with a 9-line comment, referenced **nowhere** (the rebindable send-slot lives entirely inside each `Session::transport_send_`). Orphan from the 015 design evolution. | Removed the member + comment; left a one-line note explaining where the send-slot actually lives. |
| **Q-3** | Slot-121 (`session_unknown_acceptor_session`) comment claimed the accept loop "**logs this code**", but 015 has **no log surface** (FR-013) and the slot is referenced only at its definition — the comment overstated behavior. | Softened the `error.hpp` comment: the code *names* the connection-level disposition (close, no Session); surfacing it on a log/observability sink is **deferred** (FR-013). The reject path itself (close + no session) is correct. |

**Validation:** incremental `linux-clang-debug` build (exit 0) → full Tier-1 ctest
minus the `#132` git-cleanliness gate (uncommitted edits): **353/353 passed, 0 failed**.
All Q-1/R-1-affected (`engine_acceptor`, `engine_connect`, `engine_lifecycle`,
`engine_readpump`, `engine_seam_removal`, `compid_binding_*`, `reconnect_live_happy_path`,
`live_identity_binding`) and the Q-2 `engine_firstframe` window test green. `#132`
re-confirmed on the post-commit clean tree.

## DEFERRED — tracked follow-ups (carry into the `/speckit-verify` decision doc)

| ID | Finding | Why deferred |
|----|---------|--------------|
| **Q-4** | `Engine::engine_strand_` is constructed (`make_strand(exec_)`) but **never posted through**; the header comment claims "all registry mutation sequenced on the engine strand (E-5)", which the impl does not actually do (it uses `exec_` directly). Benign in 015's single-threaded-executor scope (all tests + the documented model drive one `io_context`). | Resolving it correctly is a **thread-safety design decision** (either honor E-5 by routing registry ops through the strand, or remove the strand and document the single-executor assumption) — outside `/simplify`'s minimal-change lane and coupled to data-model E-5. Not silently weakened by deleting the strand. → thread-safety hardening pass / Gate-B note. |
| **R-2** | The engine accept loop's anon-namespace `scan_first_frame_ids` re-implements a subset of `session.cpp`'s file-local `scan_frame_header` SOH scanner. | De-dup requires a **cross-TU lift** (promote a shared header + linkage) touching an established inbound-dispatch hot-path helper — risk > reward under `/simplify`. → tracked cleanup. |
| **Q-2-witness** | The handshaked-then-stall deadline path fixed by Q-2 has no dedicated test (needs a TLS client that completes the handshake then holds — the current firstframe harness uses a raw-TCP probe). | A witness fixture is more than a trivial add; the fix is small and clearly correct, and the deadline *disposition* is already covered. → add a handshake-then-stall witness in a follow-up. |
| **E-1..E-7** | Efficiency pass: first-frame buffer re-scan (E-1), `reversed_from_logon` strings on the cold no-match path (E-3), `ssl_cfg` once-per-session copy (E-4), per-frame `vector` copy in the rebound send (E-7). | **No in-scope win.** All are cold-path / once-per-session, already-optimal (sink params, loop refs), or correctness-required (E-7's copy outlives the caller's buffer — removing it is an outbound-queue *redesign*). Clarity > cycles for a correctness-first FIX engine. |

## Non-findings (explicitly checked clean)
- Cancellation resets present on all three loops (`run_accept_loop` / `run_connect_loop` / `run_read_pump`).
- `stop()` ordering: total-cancel → close transports → join-on-counter → clear (correct; idempotent).
- Both Logon gates are two-arm fail-CLOSED guards; `live_peer_id_` set strictly-before the gate; no fail-open path; no "lying event".
- `emit_initiator_logon_` extraction is behavior-preserving (caller owns the `LogonSent` transition).
- Read-pump closes+stops on all three arms (EOF/read-error, `wire_frame_too_large`, `on_inbound_frame` error); coalesced-frame drain loop correct.
- `drive_reconnect()` / `live_transport()` / `emit_initiator_logon_()` are justified, not redundant wrappers.
- Symmetric-API completeness (acceptor `attach_accepted_transport` vs initiator `install_reconnected_transport`): PASS — both set `live_peer_id_`, take ownership, rebind `transport_send_` (now via the shared `make_live_send_`); the only divergence is the intentional FSM-state difference.
