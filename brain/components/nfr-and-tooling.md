---
type: Component Decision Map
title: nfr and tooling — why neither gets a subsystem page, and why nfr's status column cannot be trusted
description: Two catalogue families that are not subsystems. Recorded so the next person does not re-open the question, and so nfr's uniform backlog is not mistaken for fact.
status: stable
refs:
  - .specify/constitution.md
  - spec/feature-catalogue.md
  - cmake/Helpers.cmake
  - cmake/Codegen.cmake
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
- **The gcc presets NO LONGER set `FIXPP_WERROR=OFF`** (#439). Dropping that override was not a
  one-line change: five DEFAULT-ON classes fired, so `-Wall` was never the obstacle. `-Wattributes`
  dominated, by orders of magnitude, over every other class — the tree spells `[[clang::lifetimebound]]`,
  which GCC parses and cannot act on, and the codegen emitter writes it into every generated accessor, so
  the count is dominated by generated code and moves with each regeneration — and is suppressed by the **namespace-scoped** `-Wno-attributes=clang::` (GCC >= 13; the
  blanket `-Wno-attributes` would also swallow a misspelled attribute in any other namespace). That
  suppression is gated on `FIXPP_WERROR` for a **ccache** reason, stated at the site. A second class
  was pure rot: fifteen deprecation suppressions guarded on `__GNUC__` but spelled
  `#pragma clang diagnostic`, inert on GCC for their whole life — see failure-classes.md class 16,
  *a disabled gate rots everything written to satisfy it*.
- ⚠️ **`linux-gcc-debug` is built by NO CI lane** — it appears in `ci/` only as a fixture string. Its
  override was dropped too, but nothing in CI exercises that preset, so treat it as unverified.
  Re-derive: `grep -rn "linux-gcc-debug" .github/ ci/ tools/`

### The common strict flags reach every first-party target (#481)

**`fixpp_apply_common_flags_to_all_targets()`** (`cmake/Helpers.cmake`) applies
`fixpp_apply_common_flags` by the same deferred `BUILDSYSTEM_TARGETS` walk as `FIXPP_WERROR`, for the
same reason: a per-target call list misses the next target added without it. It fails configure on an
empty walk, and its STATUS line names how many targets it reached and which were exempt. It is not
gated on `FIXPP_WERROR`: its warning flags only raise warnings, and whether a warning fails the build
stays that option's decision. The two MSVC conformance switches below are not warnings, and that
option does not remove them.

- **Opt-out is per target, with the reason in `FIXPP_COMMON_FLAGS_EXEMPT`.** It is reserved for a
  target whose TUs contain no first-party code. A first-party warning is fixed at the site, and in
  generated code it is fixed in the codegen emitter, never in its output. A negative-compile probe
  needs no exemption, because added warnings cannot make a build that must fail succeed. The configure
  STATUS line names the exempt targets; re-derive the users with
  `git grep -n FIXPP_COMMON_FLAGS_EXEMPT -- '*.txt' '*.cmake'`.
- **The Python binding `fixpp_py` is not exempt, because its wrapper TU is not all SWIG's.** SWIG
  copies `bindings/python/fixpp.i`'s hand-written blocks, typemap bodies and `%inline` code into
  `fixppPYTHON_wrap.cxx`, so exempting the target would hide first-party warnings. SWIG's own runtime
  declares parameters it does not read, so `fixpp.i` suppresses `-Wunused-parameter` from a `%begin`
  push to a pop at the start of its first `%{` block, which covers SWIG's runtime section and nothing
  after it. A suppression covering a whole TU that mixes generated and first-party code is the
  rejected shape. `fixpp.i`'s comment holds the recipe that re-checks whether the suppression is still
  needed.
- **The GCC/Clang flags are warnings only, and every flag is `PRIVATE` and C++ only.** GCC and Clang
  get `-Wall -Wextra -Wpedantic` through a C++-only generator expression, because the tree has a C
  target. A consumer's own flags are untouched. The flags are prepended, so a target's own `-Wno-<x>`
  still wins on Clang, which applies warning flags in command-line order.
- **Two ctest build probes pin the mechanism**, in the `#481` block of `tests/core/CMakeLists.txt`.
  `build_flags_common_flags_reach` (registered only under `FIXPP_WERROR`) passes only if a target in
  that subdirectory gets `-Wextra`'s unused-parameter diagnostic, so it goes red if the deferred walk
  stops reaching nested targets. `build_flags_common_flags_order` fails if a target's own
  `-Wno-unused-parameter` stops winning, which catches a lost `BEFORE` on Clang only. Both are
  Clang/GNU-only; MSVC reach is not pinned.
- **`-fno-exceptions` was dropped, not opted out of.** The shipped library throws, including the typed
  errors of the public dictionary API, so that Phase-3 flag could never be wired as written. The codegen
  tool's comment about opting out of it, and the tool's own duplicate warning set, were deleted with it.
  Re-derive the condition that rules the flag out:
  `grep -rlE '(^|[^a-zA-Z_])throw[ (]|catch[ ]*\(' src include`
- **`/WX` lives only in `fixpp_maybe_werror`.** Promotion is `FIXPP_WERROR`'s decision on every
  toolchain. An unconditional `/WX` in the common function would make `-DFIXPP_WERROR=OFF` a no-op on
  MSVC.
- **The MSVC branch is `/W4` plus two conformance switches, which are not warnings and which
  `-DFIXPP_WERROR=OFF` does not remove.** Each holds only while its condition does.
  - `/permissive-`: redundant while the MSVC standard switch is `/std:c++latest`, which implies it; it
    is kept to say so explicitly. Re-check: compile `struct S{}; void f(S&); int main(){ f(S{}); }`
    with `cl /std:c++latest /c` and no `/permissive-`. C2664 means the standard switch implies it.
  - `/Zc:__cplusplus`: changes the value of `__cplusplus` in every TU, for first-party code and every
    dependency header alike. Safe for first-party code while it tests `__cplusplus` only for presence
    (`#ifdef`). Re-derive:
    `git grep -n -e __cplusplus -e _MSVC_LANG -- include src tests tools bench perf bindings`
- **The MSVC branch has three exceptions. Each holds only while its condition does.**
  - `/wd5030` (attribute not recognized): the tree spells attributes MSVC does not implement
    (`[[clang::lifetimebound]]`, `[[gnu::used]]`). This is safe only while a misspelled attribute stays
    a hard error where the attribute is understood, which is Clang's default-on `-Wunknown-attributes`
    under `-Werror`. Re-check that a misspelling still fails:
    `printf 'int& f(int& x [[clang::lifetimebond]]);\n' | clang++ -std=c++23 -Werror -fsyntax-only -x c++ -`
    That covers only attributes clang compiles. One spelled solely where clang never looks (an
    `msvc::` attribute, or one under an `_MSC_VER`-only arm) has MSVC's C5030 as its only report, and
    this suppression hides it; MSVC has no per-attribute form of the suppression. Re-derive:
    `git grep -n "msvc::" -- include src tests tools bench perf bindings`, then read the attributes
    inside each `git grep -n _MSC_VER` arm.
  - `/wd4324` (structure padded due to alignment specifier): the padding is what an `alignas` member
    asks for.
  - `_CRT_SECURE_NO_WARNINGS` and `_CRT_NONSTDC_NO_WARNINGS`, MSVC-only compile definitions through
    the same walk: the tree calls portable C/POSIX functions (`getenv`, `fopen`, `getpid`) on purpose.
    This is safe only while a use of a `[[deprecated]]` declaration still raises C4996 with both
    macros defined. That diagnostic is the friction the deprecated security-profile enumerators exist
    for. Re-check with one TU that calls a `[[deprecated]]` function and `getenv`, compiled with
    `cl /W4` with and without the two definitions. With them, only the deprecated call may warn.
    Without them, both must warn; that control shows the definitions are what silence the CRT call.
- **A deliberate `[[deprecated]]` use suppresses C4996 at the same scope as its GCC/Clang
  `-Wdeprecated-declarations` push**, in an `#elif defined(_MSC_VER)` arm, the form
  `is_insecure_plain_tcp()` in `include/fixpp/session/security_profile.hpp` uses (with the tidy
  suppression below). A site with only the GCC/Clang arm is clean on Linux and fails
  under MSVC `/WX`. Find a file missing its MSVC arm:
  `grep -rln -- '-Wdeprecated-declarations' src include tests | xargs grep -L 'disable : 4996'`
  Its output also names files that need no arm: the negative-compile probes and their CMake lines,
  which must keep warning, and users of the `FIXPP_SUPPRESS_DEPRECATED_*` macros, which carry the
  MSVC arm inside the macro.
  - clang-tidy's `readability-use-concise-preprocessor-directives` reports every such
    `#elif defined(_MSC_VER)` line and suggests `#elifdef`. The house answer is a suppression, not the
    rewrite: wrap each `#if … #endif` block in `NOLINTBEGIN/NOLINTEND` for that check, as
    `include/fixpp/core/sync/detail/atomic_shared_ptr_detect.hpp` does. `#elifdef` is C++23-only, which
    a public header cannot assume of its consumers, and the `src/` sites keep the header's spelling and
    point to it. A bare `#elif defined(...)` with no suppression is an open tidy finding, not a
    precedent.
- ⚠️ **LEAD — `_codegen_bootstrap` keeps the `FIXPP_WERROR` of its FIRST configure.**
  `cmake/Codegen.cmake` forwards `-DFIXPP_WERROR` only when it configures the bootstrap sub-build, and
  it configures it only while `_codegen_bootstrap/CMakeCache.txt` does not exist. A later
  `-DFIXPP_WERROR=OFF` on the outer tree therefore does not reach the codegen tool's own build. Before
  relying on `OFF` in a reused tree, re-derive the condition with
  `grep -n '_need_bootstrap_configure\|FIXPP_WERROR' cmake/Codegen.cmake`, then compare the two caches
  with `grep FIXPP_WERROR <build>/CMakeCache.txt <build>/_codegen_bootstrap/CMakeCache.txt`.
  Deleting `_codegen_bootstrap/` makes the next configure read the option again.

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
