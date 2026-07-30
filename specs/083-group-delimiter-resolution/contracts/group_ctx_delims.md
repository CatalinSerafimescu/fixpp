# Contract — Per-context delimiter path (loader → handle → table view)

**Feature**: `083-group-delimiter-resolution` | Satisfies FR-001..FR-005, FR-010, FR-015

## Surface

Three seams, all internal to the library. **No public C++ header signature and no C ABI symbol changes.**

| Seam | Direction | Contract |
|---|---|---|
| loader walk → record sink | write | one record per `(msg_type, parent_path, no_tag)` context reached during **message** expansion |
| metadata handle | store | immutable after load; PMR-allocated on the caller's resource |
| handle → `as_table_view()` | read | the delimiter fed to `set_group_first_ctx`, replacing the global lookup |

## Producer contract (both loaders)

**C-1.1** — A record is emitted for every group context reached during per-message expansion, and for no other. The component-cache and group-cache expansions are not message-scoped and MUST emit nothing.

**C-1.2** — The recorded delimiter is the **first member emitted at that group's level**, in document order. Because component members expand inline at the enclosing level and a nested group's count tag is emitted at the outer level before descent, this value is the FIX delimiter with no additional traversal. Implementations MUST NOT reintroduce a separate scan to compute it (research.md D-1).

**C-1.3** — `parent_path` is outermost-first and **excludes** the group's own `no_tag`, matching the existing context-store convention.

**C-1.4** — If a context's first emission never occurs, the delimiter is unresolvable and the FR-006 disposition applies (see `loader_tolerant_mode.md`). Recording `0` is not permitted.

**C-1.4a — No silent skip.** Both loaders currently **throw** on an unresolvable field/component/group reference rather than skipping it. This disposition MUST be preserved: C-1.2's "first emission is the delimiter" holds only if no leading child can be silently dropped. A skip would make the *second* child the captured delimiter, silently, and only on dictionaries containing such a reference.

**C-1.4b — The global lookup stays populated.** The bare `group_first_field(no_tag)` accessor MUST continue to return a non-zero value for every registered group, derived from this table as a first-seen projection. It is used as an *is-this-tag-a-group* **predicate**, including at C-ABI construction sites; leaving it unpopulated after the old scan is deleted would make the C ABI reject all groups. See research.md D-10 for the consumer list.

**C-1.5 — Symmetry.** The QuickFIX-XML and Orchestra loaders MUST implement C-1.1..C-1.4 with identical observable behaviour. Measured evidence that this is not optional: Orchestra has zero broken-scan and zero unregistered groups yet still exhibits 30 wrong-delimiter contexts, so the defect is present in both and a one-loader fix is a half-restructure.

## Store contract

**C-2.1** — Immutable after load; no mutation path is exposed.

**C-2.2** — Keyed identically to the existing group context store, so a key that resolves in one resolves in the other.

**C-2.3** — Load-time allocation only. No allocation on the parse/validate path.

## Consumer contract

**C-3.1** — `as_table_view()` sources the delimiter for `set_group_first_ctx` from this table, not from the global first-seen lookup.

**C-3.2** — The false comment asserting the per-context member set "stays exact regardless" of divergent delimiters is corrected in the same change (FR-011). It is not merely wrong; it is the reason the defect survived inspection.

**C-3.3** — `set_group_first_ctx`'s existing member injection is **retained**. Once C-1.2 holds, the injected tag is always already a declared member, so the call is a no-op. The pin asserts this rather than the code removing it (research.md D-5).

## Lookup-miss behaviour — unchanged, and load-bearing for tests

The context-keyed accessor falls back to the bare global store on a miss. This is deliberate and MUST NOT change: hand-built test fixtures never populate the context store and depend on the fallback.

**Consequence for any verification code** — a context miss returns the *global* member set, so a miss is indistinguishable from a wrong answer unless discriminated explicitly. Discriminate by comparing the returned span's data pointer against the bare span's. Omitting this inflated the originally reported defect count by 10 contexts.

## Post-conditions (measured)

| Property | Before | After |
|---|---|---|
| contexts with wrong delimiter | 335 measured (population excludes 30 unregistered) | 0 of 365 |
| contexts with polluted member set | 52 | 0 |
| contexts unregistered | 30 | 0 |
| FIX50SP2 registered groups | 502 | 505, matching codegen |
