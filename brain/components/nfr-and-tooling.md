---
type: Component Decision Map
title: nfr and tooling — why neither gets a subsystem page, and why nfr's status column cannot be trusted
description: Two catalogue families that are not subsystems. Recorded so the next person does not re-open the question, and so nfr's uniform backlog is not mistaken for fact.
status: stable
refs:
  - .specify/constitution.md
  - spec/feature-catalogue.md
  - cmake/Helpers.cmake
  - .clang-tidy
codegraph_entry: []
---

# `nfr` and `tooling` — deliberately no subsystem page

> ## ⚠️ The CODE is authoritative. This page is not.
>
> SecondBrain is a **consultant**, not a source of truth. It points you at the right files and explains
> **why** a decision was taken and what was **rejected** — that half is historical and does not change
> retroactively. It does **not** establish what the code does today.
>
> **Anything here describing current behaviour is a LEAD TO CHECK, not a fact to cite.** Verify against
> source before you rely on it, and cite the source, not this page.
>
> This page exists because signed-off design documents rotted. **It has no immunity from that** — a page
> trusted instead of read becomes the next fossil, and it would be a worse one, because it is the page
> people come to for the fossil list.

## Why this page exists at all

The derived inventory (`tools/brain_inventory.py --census`) flags both families as having **no design
doc and no component page**. That is correct and, for these two, **intended**. This page records the
decision so the gap is not re-opened every time the census is run — and so the flag is read as
*"considered"* rather than *"missed"*.

## `nfr` — a cross-cutting requirement set, not a subsystem

`nfr` rows are quality requirements — language level, coverage floors, sanitizer cleanliness, no
exceptions on hot paths, allocator awareness, perf parity, benchmark gates, static analysis, fuzzing.
They constrain **every** subsystem and are owned by none, so a component page would have no component
to describe. Their real homes are `.specify/constitution.md` (the rules), the CI workflows (the
enforcement), and `bench/baselines/` (the perf gate).

### ⚠️ A `backlog` cell here is NOT evidence the capability is absent

The catalogue defines `backlog` as *not started*. For this family that definition is not being
honoured: rows sit at `backlog` while the practice they describe demonstrably ships — sanitizer legs
run in CI, `bench/baselines/` exists, `.clang-tidy` exists, the no-exceptions rule is constitutional.

**Adjudicated 2026-08-31 (user).** The question used to be open here in two readings — *stale status*
versus *an NFR is continuous and so never flips*. It is closed in favour of **stale status**, on two
independent grounds:

- The "never flips" reading is **refutable from the catalogue alone**: it predicts that no `nfr` row
  can ever read `done`. Run the recipe below and look for one.
- An out-of-repo planning tracker names a set of these rows as *delivered practice, never flipped*,
  and schedules the flip as a catalogue edit with **zero code**.

So the correct handling is not "distrust this column in an unknown direction" — it is:

> ⭐ **A `backlog` cell in this family is a lead, not a fact. Verify against the tree before
> concluding anything is missing, and never cite the cell as evidence of absence.** The rows most
> likely to look like a damning backlog are the ones most likely to be merely unflipped. The
> converse is not symmetric: a `done` cell went through the catalogue's own closure bar.

⚠️ **This is a property of the STATUS COLUMN, not of the `nfr` family.** `dictionary` carries the
same defect — see [`dictionary.md`](dictionary.md). Do not read "nfr is unreliable, the others are
fine".

**Recipe — derive the family's status breakdown yourself** (resolve columns *by name*; they have been
off-by-one here before):

```bash
python3 - <<'EOF'
import collections
lines = open('spec/feature-catalogue.md', encoding='utf-8').read().split('\n')
i, hdr = next((i, [c.strip() for c in l.strip().strip('|').split('|')])
              for i, l in enumerate(lines)
              if l.strip().startswith('|') and 'Status' in l and 'Category' in l)
ci, si = hdr.index('Category'), hdr.index('Status')
cnt = collections.Counter()
for l in lines[i + 2:]:
    if not l.strip().startswith('|'):
        continue
    c = [x.strip() for x in l.strip().strip('|').split('|')]
    if len(c) >= len(hdr):
        cnt[(c[ci], c[si])] += 1
for k in sorted(cnt):
    print(k, cnt[k])
EOF
```

`tools/brain_inventory.py --census` prints the same breakdown per family.

### Warnings as errors, and the lint/format sweep (#413, #416, #417)

**`FIXPP_WERROR` reaches every compiled target by enumeration, not by a call list.**
`fixpp_apply_werror_to_all_targets()` (`cmake/Helpers.cmake`) walks every directory's
`BUILDSYSTEM_TARGETS`, called **deferred** from the top-level `CMakeLists.txt`, and fails configure on
an empty walk. A per-target call list was rejected because that is exactly how the option went inert:
`fixpp_maybe_werror` existed, every preset set the option, and nothing called it. Deleting the option
was also considered and rejected; the user chose to wire it on every toolchain, MSVC `/WX` included.

- **Opt-out is per target, with the reason in `FIXPP_WERROR_EXEMPT`.** The deliberate case is a
  negative-compile WILL_FAIL probe: a failed build is its passing state, so a blanket `-Werror` would
  let any stray warning keep it green after the diagnostic it witnesses is gone.
- ⚠️ **LEAD — for most targets it promotes only the compiler's DEFAULT warnings.** The strict set in
  `fixpp_apply_common_flags` (`-Wall -Wextra -Wpedantic`) has no call site, and CMake adds no MSVC `/W3`
  under the project's minimum version; the codegen tool is the exception, setting its own `-Wall` set. Re-derive before relying on it:
  `grep -rn "fixpp_apply_common_flags\|-Wall" --include=CMakeLists.txt --include='*.cmake' .`
- The gcc presets keep `FIXPP_WERROR=OFF`; that preset decision was left as it was.

**Lint and format exclusions are policy, each for a reason — do not "finish" them.** `specs/` is never
formatted (generated byte-identity baselines); `include/fix/c_api*.h` is byte-frozen by
`tools/capi_freeze.sha256`; the QuickFIX golden generators SHA-1 their own `main.cpp`. A reformat also
detaches line-scoped suppressions (`NOLINT*`, `LCOV_EXCL_LINE`, `cppcheck-suppress`) from the code they
govern — pair every marker against the base after one. ⚠️ **The include sort can also break an order
dependency only libc++ exposes:** asio's `posix_thread.ipp` uses `std::terminate` without `<exception>`,
libstdc++ supplies it transitively and libc++ does not, so a TU that had `<exception>` above asio compiled
everywhere until `IncludeBlocks: Regroup` sorted it below — and only the Tier 3 libc++ legs saw it. Re-check a
reformat with a libc++ `-fsyntax-only` pass over the compile database (add `-stdlib=libc++` to each command),
and pin a load-bearing order inside `// clang-format off` / `// clang-format on` with the reason, since a plain
reorder is sorted back on the next format. `clang-tidy -fix` at scale is not
behaviour-neutral: one `readability-qualified-auto` rewrite put `auto*` on a `std::array` iterator,
which compiles only where that iterator is a raw pointer. And the `/_codegen/` alternative in
`.clang-tidy`'s `ExcludeHeaderFilterRegex` does **not** hold for `tests/` TUs that include the
generated validators (cause not found; the file carries the re-check procedure). The residuals are in
fixpp#436.

## `tooling` — genuinely future work

Two rows: a FIX session monitoring / protocol analyzer, and git-backed plain-text session
configuration. Both `backlog`, and here that reading is unremarkable — neither exists, neither is
claimed, and no design doc pretends otherwise. **No page is warranted; there is nothing to route to
yet.** Revisit if either acquires a feature bundle.
