---
type: Component Decision Map
title: Observability — the span layer is built and tested; the engine does not start spans
description: Trace context IS plumbed into Session and Engine. Spans exist, are tested, and have no engine-side call site. Those are two different states and the difference matters.
status: stable
refs:
  - include/fixpp/core/trace_context.hpp
  - include/fixpp/otel/session_spans.hpp
  - src/otel/session_spans.cpp
  - src/otel/providers.cpp
  - .specify/2k-log-otel.md
refs_external:
  - research/G19-fix-fpml-iso20022/decisions/2k-log-otel.md
codegraph_entry: [trace_context, current_trace_context, engine_trace_context, ParseSpan, StoreSpan]
---

# Observability (logging + OpenTelemetry)

> ## ⚠️ The CODE is authoritative. This page is not.
>
> One thing on it is a **measurement with a date**, which is exactly the kind of claim that rots.
> The re-derivation is given; run it before relying on the finding.

## Where to look

| You want | Go to |
|---|---|
| The trace-context type | `include/fixpp/core/trace_context.hpp` |
| The span wrappers | `include/fixpp/otel/session_spans.hpp` · `src/otel/session_spans.cpp` |
| Provider / exporter wiring | `src/otel/providers.cpp` · `src/otel/exporters.cpp` |
| Why any of it is shaped this way | `.specify/2k-log-otel.md` — ⚠️ that doc also owns the **engine-wide error enum**; see [`errors.md`](./errors.md) |

## ⚠️ Two states that look alike and are not — measured 2026-08-31

**Trace *context* is plumbed into the engine.** `Session` holds a
`session_local<fixpp::otel::trace_context>`, `Engine` exposes `engine_trace_context()`, and there is a
`current_trace_context()` awaitable.

**Spans are not started by engine code.** The span wrappers exist, are implemented, and are covered by
tests — and **every construction site is in `tests/otel/`**. Nothing under `src/` outside `src/otel/`
builds one.

> ⭐ **So "OTel shipped" is true and misleading at once.** The pipeline can be configured correctly and
> still carry no engine telemetry, because nothing on the engine paths emits. Anyone planning exporter
> work should settle *what gets instrumented* first; wiring an exporter to a source that emits nothing
> produces a green pipeline and an empty backend.
>
> ⚠️ **This is a measurement of a moving tree, not a property of the design.** One feature adding one
> span makes it false. **Re-derive before citing it:**
>
> ```bash
> grep -rn 'ParseSpan\|StoreSpan\|make_store_span' src/ | grep -v '^src/otel/'
> ```
>
> Empty ⇒ still true. Non-empty ⇒ this section is stale; fix it rather than working around it.

## A layering fact that explains a surprising file location

`trace_context` lives in **`include/fixpp/core/`** but in namespace **`fixpp::otel`**, with
`include/fixpp/otel/trace_context.hpp` as a forwarding header.

That is not sloppiness. `core/` is the leaf of the dependency graph and may not depend on `otel/`, but
`Session` — which is downstream of `core` and must hold a trace slot — needs the type. Putting the type
in `core` while keeping the `otel` namespace satisfies both. **Do not "tidy" it into `otel/`**; that
creates the edge the layering forbids, and `tools/check_layers.py` will say so.

The type is a fixed-size trivially-copyable struct, pinned by a `static_assert` — the same
freeze-by-compiler pattern used in [`errors.md`](./errors.md).

## OTel is an optional build

`FIXPP_BUILD_OTEL` is a CMake option. A build with it off is a supported configuration, so **nothing on
an engine path may hard-depend on OTel types.** That constraint is a large part of why the trace slot
holds a small POD rather than a provider handle.

## Related

- [`errors.md`](./errors.md) — `2k-log-otel.md` owns the error enum too, and both use the
  freeze-by-`static_assert` pattern.
- [`session.md`](./session.md) — where the trace slot lives.
