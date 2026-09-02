---
type: Component Decision Map
title: Transport — a 5-method interface at its cap, and the two things pushed out to keep it there
description: The ≤5 pure-virtual rule is not decoration here. It is why TLS handshake is a separate sub-interface and why Listener::cancel does not exist.
status: stable
refs:
  - include/fixpp/transport/transport.hpp
  - include/fixpp/transport/listener.hpp
  - include/fixpp/transport/tls_transport.hpp
  - include/fixpp/transport/reconnect_policy.hpp
  - .specify/2h-transport.md
refs_external:
  - research/G19-fix-fpml-iso20022/decisions/2h-transport.md
codegraph_entry: [Transport, Listener, TransportFactory, ReconnectPolicy]
constitution: ["§XIV.1", "§XIV.2", "§XV.2"]
---

# Transport

> ## ⚠️ The CODE is authoritative. This page is not.
>
> `2h-transport.md` owns this subsystem in depth, so this page is a **supplement**: the shape of the
> interface and the two decisions that shape follows from. ⚠️ **`2h` carries an Appendix Z** — read it
> before citing `2h`, and note its §D.1/§D.2 amendments were applied *and then superseded*.

## The interface is exactly at its cap, with zero headroom

`Transport` has **five** pure-virtual methods; `[const §XIV.2]` caps a plugin interface at five. That
is not a coincidence — it is a budget that has already been spent, and it explains two things that
otherwise look arbitrary:

| Pushed out | To where | Why |
|---|---|---|
| TLS `async_handshake` | a separate `tls_transport` sub-interface | adding it to `Transport` would be a sixth method |
| `Listener::cancel()` | the concrete `asio_listener` impl only | `Listener` keeps **exactly one** pure-virtual (`async_accept`), preserving the headroom `Transport` has none of |

> ⭐ **So before adding a method here, the question is not "is it useful" but "what comes out".** The
> two precedents above are the shape of the answer: push it into a sub-interface, or onto the concrete
> implementation, and keep the abstract surface small.
>

### A THIRD way out, taken by `close_async()` (#348): a virtual WITH A DEFAULT

The cap counts **pure-virtual** methods — `[const §XIV.2]` says "≤5 **pure-virtual** methods", not
five methods. `close_async()` is a sixth method and the cap still holds at five, because it ships a
default body (`co_return close();`) rather than `= 0`. Re-derive rather than trusting this sentence:
count `= 0;` inside `class Transport` in `include/fixpp/transport/transport.hpp`.

That is not a loophole, it is the property that made the shape affordable. A defaulted virtual costs
existing implementors **nothing** — the library's two transports, `mock_transport` and the test
doubles all keep compiling untouched — whereas a sixth pure-virtual would have required a design-doc
justification reviewed at Gate A, and changing `close()`'s own signature would have broken every
implementor for a benefit only the TLS transport can deliver.

⚠️ **The cost is paid elsewhere, and it is real:** a defaulted virtual can ship with NO adopters, and
`close_async()` shipped with none. For one release the user-visible #348 defect (no TLS close-notify
reaches the peer) was **unchanged** while the capability existed. When you read that a branch "fixed
#348", check `git grep close_async -- src` for a production call site before believing it — that is
still the right check, and it is why this paragraph does not name the current adopters.

**Why adoption was not the one-liner it looked like, which is the part worth carrying forward.** The
stated obstacle — `Session`'s terminal close emits `cancellation_type::total` immediately before
closing, which would cancel an awaited shutdown — was **not real at all**: `Session::close()` opens
by disabling cancellation on its own frame, so that emission never reached the close it was said to
break. Verify at the head of `Session::close` before repeating either version. The awaited shutdown
was never the problem; the problem was
that at BOTH teardown sites an SSL operation is still suspended (the read pump is blocked in
`async_read_some`, and the total-cancel that precedes the close has not been delivered to it yet),
and `close_async()` had inherited `close()`'s rule of skipping the alert whenever that is so.
Adopting it unchanged would have compiled, passed every transport-level cell, and delivered nothing —
a capability that is *exactly* inert at the call sites it was written for.

The fix belonged in the transport, not at the call site: `close_async()` now CANCELS the suspended
op (`socket_.cancel()`, which does not touch SSL state), joins it, and only then writes the alert,
with an abortive fallback if it will not quiesce inside the budget. The generalisable shape is that
**an opt-in written against the easy state is not evidence about the state its adopters are in** —
the pre-existing cell that closed a transport with nothing in flight stayed GREEN under the mutation
that made the whole feature inert.

> ⚠️ `Listener::cancel()` living on the impl is a **deliberate** Gate A outcome that overrode a review
> objection. Do not "fix" it by promoting it to the base.

**Re-derive the counts** — they are the whole argument, and a count written here would rot:

```bash
awk '/^class Transport/,/^};/' include/fixpp/transport/transport.hpp | grep -c '= 0;'
awk '/^class Listener/,/^};/'  include/fixpp/transport/listener.hpp  | grep -c '= 0;'
```

## A fresh `Transport` per connection attempt — never reused

Every accept and every reconnect attempt mints a **new** `Transport`; the dead instance is destroyed
first, so at most one is live per session. Disclosed as **`B-012-2`**. This is why the connect path
and the reconnect path cannot drift apart — they share `drive_reconnect_attempt`.

## `ReconnectPolicy` is a schedule array, not a formula

It carries a vector of delays with the last entry as a plateau — **not** the
`{initial, max, multiplier, attempts, jitter}` shape it was first designed with. Two presets exist: a
default with jitter and a cap (thundering-herd defence), and a QuickFIX-compatible one with neither.

⭐ **The compat preset is the interesting half:** matching an incumbent's *absence* of jitter is a
deliberate interop choice, not an oversight. Read the header before changing either preset's values —
they are not reproduced here.

## Related

- [`engine-accept-path.md`](./engine-accept-path.md) · [`initiator-connect-path.md`](./initiator-connect-path.md) — the two flows that drive this interface.
- [`security-profile.md`](./security-profile.md) — the TLS side, and the two `SecurityProfile` types.
- [`plugin-factory-ownership.md`](./plugin-factory-ownership.md) — `TransportFactory`'s ownership, where the signed-off docs and the shipped header disagree.
