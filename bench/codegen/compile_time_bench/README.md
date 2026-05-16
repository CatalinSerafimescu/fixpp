# T046 — Compile-time bench known findings (NFR-003-2)

## v50sp2 exceeds the ≤ 3 s load-bearing ceiling

**Measured:** `v50sp2/Messages.hpp` + `v50sp2/Reify.hpp` syntax-only compile
takes approximately **8–9 s** on typical CI hardware (WSL2/Linux, Clang 19+).

**Root cause:** FIX50SP2 contains the largest standard message set (~400+
messages, ~120 kLOC generated `Messages.hpp`). The typed-message flyweight
pattern emits one inline accessor per field per message; preprocessor
expansion and template instantiation cost is proportional to message count.
v42 (~4 kLOC) and v44 (~30 kLOC) comfortably satisfy the ≤ 3 s ceiling.

**Classification:** Pre-existing structural characteristic of FIX50SP2 standard
message density, not a regression from this PR. See `spec.md §11 R2`.

**Status:** Recorded finding, not a blocker. `compile_time_bench.sh` emits
`STATUS=KNOWN_OVERAGE` for v50sp2 and exits 0; only unexpected overages for
v42/v44/vt11 cause a non-zero exit.

**Mitigation candidates (follow-up):**
1. Selective message emission (CMake allowlist flag — `[2c §2]` design).
2. Forward-declared flyweights (per-message TU split).
3. PCH / C++20 explicit module for the generated surface.
