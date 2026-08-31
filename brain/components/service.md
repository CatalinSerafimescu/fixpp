---
type: Component Decision Map
title: Service wrapper — a stub, deliberately, and the install-root defect that recurred inside it
description: src/service/ is an INTERFACE target with no implementation. The page exists so the emptiness reads as a decision, not a gap someone should fill.
status: stable
refs:
  - src/service/CMakeLists.txt
  - .specify/2j-controlplane.md
  - .specify/architecture.md
refs_external:
  - research/G19-fix-fpml-iso20022/decisions/2j-controlplane.md
  - research/G19-fix-fpml-iso20022/decisions/architecture.md
codegraph_entry: []
---

# Service wrapper

> ## ⚠️ The CODE is authoritative. This page is not.

## It is a stub, and that is the current disposition — not an oversight

`src/service/` holds a `CMakeLists.txt` and **no implementation**; `fixpp_service` is an **INTERFACE**
target. The intended design — a gRPC control plane plus an iceoryx2 data plane — lives in
`.specify/2j-controlplane.md` and `[arch §4.11]` / `[arch §8]`, and none of it is built.

⚠️ **Precisely: the public interface header exists, the implementation does not.**
`include/fixpp/service/control_plane_factory.hpp` is real and is part of the public C++ plugin surface
(it depends only on `core/`). So "the service is a stub" is a statement about `src/`, not about the
whole module — and `[arch §8]`'s own carve-out says the C-ABI-only rule constrains the *binary and its
default impls*, not these interface headers.

> ⚠️ **`[arch §11]` row 5 still dispositions the ControlPlane interface shape as "Phase 2".** That does
> not describe the tree; see `architecture.md` **Appendix Z-3**. Whether the service rows are deferred
> or formally dropped is a **live decision recorded outside this repo** — this page deliberately does
> **not** answer it. It records only that nothing is implemented.

**Why the page exists at all:** an empty module reads as an accident. This says it is not, so nobody
"discovers" the gap and starts filling it without the decision behind it.

## The one architectural rule that is already live

`[arch §8]`: the `fixppd` daemon and any default plugin implementations consume the engine **only
through the C ABI**, never through engine-internal headers. `tools/check_layers.py` encodes it —
`service` may include `capi` and its own interface headers, nothing else.

So whenever the service is built, it is a **C-ABI consumer like any other**, not a privileged insider.
That is the same licence-seam reasoning as [`c-api.md`](./c-api.md).

## ⭐ A defect that recurred here, recorded at the site

The CMake file carries an unusually emphatic warning, and it generalises beyond this module:

> The install root declared here is **not inherited** from `fixpp_capi`. Narrowing that target does not
> touch this one, and *"every other requirement of this feature can be satisfied while this line
> survives untouched — which is exactly how the SECOND instance of the same defect went unrecorded."*

⭐ **The transferable lesson: a fix applied to one target does not propagate to a sibling that declares
the same thing independently, and the sibling's survival is invisible to every check the fix passes.**
When narrowing an install root, an export set, or a visibility rule, enumerate every target that
declares one — do not assume inheritance.

## Re-derive

```bash
ls -R src/service/ include/fixpp/service/     # is there an implementation yet?
python3 tools/check_layers.py                 # the C-ABI-only rule, executable
```

## Related

- [`c-api.md`](./c-api.md) — the seam the service is required to consume through.
