---
type: Component Decision Map
title: QuickFIX compatibility — a runtime adapter was REJECTED; only config translation ships
description: Path B. The incompatibility is documented rather than bridged, and the rejection is pinned by a file-scope static_assert so the build fails if anyone re-introduces it.
status: stable
refs:
  - include/fixpp/session/quickfix_compat/cfg_loader.hpp
  - src/session/quickfix_compat/cfg_loader.cpp
  - .specify/2e-msgstore.md
  - .specify/architecture.md
  - specs/089-quickfix-interop-conversation/spec.md
  - tests/session/test_quickfix_compat_path_b_guard.cpp
refs_external:
  - research/G19-fix-fpml-iso20022/decisions/2e-msgstore.md
  - research/G19-fix-fpml-iso20022/decisions/architecture.md
codegraph_entry: [cfg_loader]
---

# QuickFIX compatibility

> ## ⚠️ The CODE is authoritative. This page is not.
>
> What it records is a **rejection**, and a rejection does not rot — it is history. The surviving
> surface is small; read the header.

## The decision, and what it rules out

QuickFIX's `MessageStore` is **synchronous**. `fixpp`'s is awaitable. The open question was whether to
ship a shim letting a synchronous QuickFIX-shaped store drop into the engine.

**Answer: no — "Path B".** v1.0 ships:

- a **documented incompatibility** plus a migration recipe, and
- `quickfix_compat::cfg_loader` — a **config-translation** surface,

and deliberately **no runtime adapter**. The disposition is recorded at `[arch §11]` row 3 as CLOSED,
resolved through `[2e §4.8.A]`.

> ⭐ **Why this matters to anyone extending the compat layer:** the boundary is *translation, not
> emulation*. Reading a QuickFIX config file and producing fixpp configuration is in scope. Making a
> QuickFIX object work inside the engine is the thing that was rejected — a synchronous store called
> from an awaitable path is exactly how you block a session strand.

## ⭐ The rejection is enforced by the compiler, not by a comment

`tests/session/test_quickfix_compat_path_b_guard.cpp` is a **file-scope `static_assert`** that a
QuickFIX-shaped synchronous store is **not constructible** into `fixpp::session::MessageStore`. Its own
comment puts it best: *"No runtime assertion is needed — the BUILD IS THE TEST."*

If someone later adds an implicit conversion that would let such a type bind, **compilation fails
before any test binary runs**.

This is the third place in this codebase where a decision is pinned that way — see the frozen error
range in [`errors.md`](./errors.md) and the trace-context size assert in
[`observability.md`](./observability.md). ⭐ **It is the local idiom for "this decision must not be
undone by accident", and it is worth reaching for instead of a comment**: a comment saying *"do not do
X"* is a claim nobody re-checks; a `static_assert` is the same claim checked on every build.

## ⭐ A SECOND rejection: nothing the peer EMITS can witness that a typed accessor ran

Different question, same boundary, and it cost three review rounds to settle — so it is written down
here to stop a fourth attempt.

**The question.** When fixpp checks its wire fidelity against a real QuickFIX peer, it is worth
asserting that the peer read a field through that message's *generated typed accessor*
(`FIX44::NewOrderSingle::get(FIX::Symbol&)`) rather than by walking the field map generically — the
typed accessor is a **schema-conformance** check the generic walk cannot make. The obvious way to
assert it is to have the peer emit some value that proves the accessor ran.

**The answer: there is no such value, and the reason is structural.** Both engines' generated
accessors hand back **the caller's own object**:

- QuickFIX-cpp, `include/quickfix/FieldMap.h` — `get(FIELD &field)` returns a reference to the field
  the caller passed in;
- QuickFIX-J, the generated `fix44/NewOrderSingle.java` — `get(Symbol value)` calls `getField(value)`
  and returns **`value`**.

So `getTag()` reports a member the caller's own constructor set *before* the call — not a function of
the invocation at all — and `getValue()` reports the wire string, which a generic enumeration already
has. Every candidate observable is reconstructible without ever invoking the accessor, because the
peer already holds the bytes, the dictionary and the script.

⚠️ **Three separate attempts proposed one anyway, and each looked sound until the next review.** A
datatype column (derivable from the dictionary the peer has loaded); a decimal spelling (both paths
produced the same text, so the arm was inert); and a value read back off the returned object (the
paragraph above). The pattern is worth recognising: *the peer's inputs are a superset of anything it
can emit*, so no emitted value discriminates how it computed them.

**What shipped instead — the same idiom as the section above.** A typed accessor **compiles only if
the field belongs to that message**, so the check moved to build time: the counterparty source calls
the per-message accessor, and a mutation naming a field the message does not declare must fail the
build. The BUILD IS THE TEST, again.

⛔ **Its honest scope, which is smaller than it first reads**: this proves *schema conformance of the
source*, not that the accessor executed at run time. Runtime invocation has no observable, per the
paragraphs above. Do not let a later edit quietly widen the claim.

⛔ **And do not pin an expected compiler-diagnostic string.** Four attempts did; all four were wrong,
for reasons that are properties of the *toolchain and the call site* rather than of the check: g++
appends ` const` to the reported signature only when the receiver is const-qualified, and quotes
identifiers differently by locale; clang words it *"no matching **member** function"* and puts the
field name only in candidate notes, never on the error line; javac names the field by its simple name.
Derive the expectation from the compiler the build host actually ships.

## Re-derive

```bash
ls include/fixpp/session/quickfix_compat/ src/session/quickfix_compat/   # the whole surface
grep -n static_assert tests/session/test_quickfix_compat_path_b_guard.cpp

# The second rejection — read the accessor bodies themselves; do not reason about them.
# (reference-engines/ is a vendored, GITIGNORED clone of the pinned upstream releases:
#  QuickFIX-cpp v1.16.0 and QuickFIX-J 3.0.1. Paths, never line numbers.)
grep -n 'get(FIELD &field)' reference-engines/quickfix-cpp/include/quickfix/FieldMap.h
grep -n -A3 'get(quickfix.field.Symbol' \
  reference-engines/quickfixj/**/generated-sources/quickfix/fix44/NewOrderSingle.java
```

If that directory ever contains more than config translation, this page's central claim is stale —
fix it here rather than working around it.

## Related

- [`message-store-quiescence.md`](./message-store-quiescence.md) — the awaitable store contract this
  rejection protects.
- [`config.md`](./config.md) — the native config loader the translation feeds.
- [`test.md`](./test.md) — the anti-vacuity doctrine the second rejection is an instance of: an arm
  whose forced defect is still satisfiable is not an arm.
- [`dictionary.md`](./dictionary.md) — why a datatype column proves dictionary-backed resolution and
  **not** accessor provenance: the peer loads the dictionary itself.
