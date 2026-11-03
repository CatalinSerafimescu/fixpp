# Contract: witnesses and mutants

Every witness is a real `Session` driven with raw frames unless it says otherwise. RED-first: W-1 to W-9 run on `c0a828ef`'s behaviour
before the code. Shapes: **T** = S FIXT11, **A** = S FIX50SP2 + FIXT11 in the engine, **L** = S FIX Latest. Fields: 3115 (FIX Latest only);
pick a FIX 4.4-only pair by a recomputed set difference over the vendored XML, in the test.

| Id | Claim | Cells |
|---|---|---|
| W-1 | Spec's "today" table is fixed | {T, A, L} × {Logon, Heartbeat, TestRequest, NOS, NOS 1128=9, 1128=6, 1128=10}; FIX44 S with FIXT11 in the engine; each asserts acceptance, fromApp/fromAdmin count and Active |
| W-2 | A field follows the version | A field only version B defines: Reject when resolved to A, accepted when resolved to B, for FIX50SP2, FIX Latest, FIX 4.4, in T and A; exact 373 and 371 |
| W-3 | Overlay survives | merged FIX50SP2 S, plain base in engine: field accepted on own version, Required promotion enforced, rejected on another; merged FIX Latest S with no FIX50SP2 registered, resolved to FIX50SP2; FIXT11 S with an overlay header tag on an application message (accepted; bad type rejected) |
| W-4 | Refusals | unknown, resolvable-no-dictionary, empty, past-header; exact 373/371; fromApp count 0, control frame in the same run count 1; NextNumIn moved as a validation Reject; session Active after |
| W-5 | Initiator Logon | absent, unparseable, unserviceable: Reject 371=1137 and 373, Disconnected; non-validating twin records and is Active |
| W-6 | Validation off | every W-4 input delivered; `fromApp` view from S; admin frame with no transport; no refusal at `open()` |
| W-7 | One table per frame | T, validation on and off: application dictionary's custom pair whose data embeds SOH and `34=7`: scan's MsgSeqNum is the header's, value reads whole; a repeating group reads back |
| W-8 | `open()` matrix | validating: T, L, A with and without FIXT11, FIX44 with and without, partial FIXT11 (admin message removed), FIXT11 whose Logon lacks 1137; non-validating twins all open |
| W-9 | Pre-Logon application frame | both establishment arms, T and L: one ending, Disconnected, no Reject |
| W-10 | Accessor | C++ and C: 1128 present, negotiated, admin, non-FIXT, unresolved; a clone; agrees with `dict::reify`'s `version().application` on every delivered cell of W-1 |
| W-11 | Outbound 1128 kept | header 1128 on an application-supplied frame reaches the wire |
| W-12 | C ABI live | `capi_099_fixt_live_test`: setters, add-dictionary, real TCP peer, Active, routed Reject and accept; handle errors; `version_test` 1.14 |
| W-13 | Python live | raw TCP peer as 098's `_Peer`, FIXT; same arms |
| W-14 | TOML | `extra_paths` reaches the engine, front is `path`, session validates; bad path, non-array, non-string; absent key byte-identical |
| W-15 | Build count | `as_table_view_call_count` across M sessions on one engine: once per dictionary used, plus S's; a TSan arm opens sessions concurrently |
| W-16 | No heap on the routed path | modelled on `tests/perf/test_session_recovery_alloc_guard.cpp`; validated and not; a routed message to a non-own slot |
| W-17 | Non-FIXT unchanged | FIX.4.4 strict validation still rejects 1128; existing suite unmodified; `as_table_view()` output for every vendored file byte-equal to a `c0a828ef` golden digest computed in the test's recipe |
| W-18 | Live | per the owner's ruling on research R-13 |

**Mutants** (each turns the named witness RED, whole binary, unfiltered, `strings`-checked fresh):

| Id | Mutation | Witness |
|---|---|---|
| M-1 | selection returns `session_route_` | W-1, W-2 |
| M-2 | registry answers before the own-version rule | W-3 |
| M-3 | a refusal falls through to S | W-4 |
| M-4 | registry builds with plain `as_table_view()` | W-1 (FIX44), W-2 |
| M-5 | dispatch keeps `inbound_tv_` | W-7 |
| M-6 | frame scan keeps `inbound_tv_`'s hooks | W-7 |
| M-7 | `open()` check removed | W-8 |
| M-8 | tables built per session | W-15 |
| M-9 | admin frames use the application slot | W-1 |
| M-10 | initiator disposition removed | W-5 |
| M-11 | accessor never written | W-10 |
| M-12 | envelope dropped | W-3 |
| M-13 | peek reads past the header | W-7 (a `1128=` in a pair's data) |
| M-14 | `defines_fixt_admin` checks the version label | W-8 |
| M-15 | establishment arms validate application frames | W-9 |

A spurious-hit arm: a frame resolved to a non-own slot must be *rejected* for a field only S's table has, so a route that always says
"accepted" cannot pass (W-2, W-3).
