# Contract: census-regrowth guard (`tools/check_no_raw_atomic_shared_ptr.sh`)

**Feature**: `046-atomic-shared-ptr` | FR-005 / SC-004.

> **REBASE CORRECTION (2026-06-22) — consumer set 4→3.** Authored for the original **four-consumer** design; 046 was rebased onto merged **048** (PR #144), which **removed** `async_mutex.hpp drain_latch_ptr_`. The as-built migrated consumer set is **three** (`pinset` `snapshot_`, `transport_factory` `cert_source_slot_`, `engine` `reader_snapshot_`). "four" below is the **historical design record**; the guard's contract (exact-set completeness, zero raw matches outside the primitive header) is unchanged and applies to the three as-built sites. Authoritative as-built: `spec/feature-catalogue.md` NFR-017.

## Purpose

After the four consumers are migrated, prevent any project header/source from re-introducing a raw `std::atomic<std::shared_ptr<…>>` (which silently re-breaks the libc++ build). Enforces **exact-set completeness**, not a one-time fix.

## Behavior

- **Scan scope**: project-owned `include/**` + `src/**` (and any other first-party C++ tree). **Exclude**: the primitive header `include/fixpp/core/sync/detail/atomic_shared_ptr.hpp` (the legitimate sole site of `std::atomic<std::shared_ptr>` in the native-alias branch), `tests/**` fixtures that intentionally probe raw forms (if any — none planned), third-party/Conan paths.
- **Detect**:
  - `std::atomic<std::shared_ptr` (any whitespace/`const` variation) — primary.
  - `std::atomic_load`/`atomic_store`/`atomic_exchange`/`atomic_compare_exchange*` applied to a `shared_ptr` (the free-function P0718-deprecated form).
- **Exit**: `0` if none found outside the allowed site; non-zero (listing each violation `file:line`) otherwise. The diagnostic names `fixpp::sync::atomic_shared_ptr` as the required replacement.
- **CI wiring**: same step set as `check_no_std_mutex_in_awaitable_headers.sh`.

## Acceptance (SC-004, mutation-tested)

- After migration: guard exits `0` (the four sites are gone; only the primitive header matches, and it is excluded).
- Inject a raw `std::atomic<std::shared_ptr<int>> x;` into a project header → guard exits non-zero naming that file:line (RED-proves the guard discriminates). Revert.
- `using`/`typedef` aliasing the raw form is a recorded out-of-grep-scope limitation (matches the existing corpus-gate limitation note).
