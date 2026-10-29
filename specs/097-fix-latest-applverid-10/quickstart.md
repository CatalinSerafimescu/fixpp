# Quickstart: validating 097

Run from the library root of the tree that owns the branch (the main checkout), except the mutation
rows of §1.2, which run in the scratch worktree that contracts' preamble creates.

## §0 — Prerequisites

- The `linux-clang-debug` build tree (`build/linux-clang-debug`) is configured; the implementer builds
  only the binaries a step names (`cmake --build --preset linux-clang-debug --target <t> -j8`), and runs
  one full configure + build + ctest at the final head ([const §XVII.7]).
- Every 097 gtest witness carries the ctest label `097` ([const §VII.8]); select with `ctest --preset
  linux-clang-debug -L 097`. W-12's fuzz harness is not in that tree: it is built only under
  `FIXPP_BUILD_FUZZ` (`linux-clang-asan`), and its corpus replay is the ctest
  `fuzz_replay_appl_ver_id` there (label `fuzz`), §1.5.

## §1 — RED before GREEN (contracts, every W)

1. Before any production edit (the branch is code-identical to `origin/main`), build and run the
   witnesses that compile against unmodified code (plan phase 2), in `build/linux-clang-debug`.
   - RED for the stated reason: W-1's `"10"` arm, W-3's text arm, W-6 (a), and W-7 (b).
   - The death test (today's W-4) aborts with `FR-010`.
   - Characterization pins that pass (FR-013's list): W-5's FIX-Latest-alone arm, W-6 (c), W-7
     (a)/(d)/(e)/(f), and W-8's phase-2 arms ((ii) and W-6(b)/(c)'s frames: every family present, all
     counts 0).
   - Two more arms pass here for a different reason than at the head, W-6 (b) and W-7 (c): on
     `origin/main` `"10"` does not parse; at the head it parses and the registry does not serve it.
     They feed `1137=10`, so they sit inside FR-013's exception (i) and are not in its pin list. Their
     head reason is proved by M-5b (W-6 (b)) and M-7c (W-7 (c)).
   - Every W-7 control arm (a0)..(f0) shows its expected `origin/main` outcome: (a0) and (e0) are
     established; (d0) and (f0) are delivered to `fromApp` on an established session; (b0) and (c0)
     get the 1137 Reject (373=5, 371=1137), the same as their arms (b)/(c). A control that departs from its expected outcome is
     a fixture defect, fixed before any arm is read (plan phase 2's stop rule). Under validation, `kMinimalFix44Xml` rejects every Logon, which
     is why W-7 names its own session dictionaries.
   - Stop rule: plan phase 2.
2. At the implementation head, for each mutant M-1..M-9h in contracts, and for W-12's re-runs of M-1,
   M-1n and M-2, **in the scratch worktree** `fixpp-parallel` (contracts preamble; owner, T002; never the main checkout):
   - commit it on the scratch worktree's detached head;
   - rebuild the named binaries in that worktree's own build tree (on `/mnt/wsl/fixppbuild`; M-9c and
     M-9d in its OFF tree, W-12's rows in its ASan tree, each configured by the contracts preamble);
   - run each binary UNFILTERED and record rc and the failing message;
   - `git reset --hard "$H"` in the scratch worktree.

   A mutant whose rebuild failed proves nothing: gate the row on build rc 0 first. M-9c is the one
   exception, because its stated RED is the OFF build failing. Remove the scratch worktree after the
   last row.
3. Green: `ctest --preset linux-clang-debug -L 097 --output-on-failure` — all pass.
4. OFF tier (W-9(c)): one configure+build with `-DFIXPP_CODEGEN_FIX_LATEST=OFF` and `FIXPP_WERROR=ON`
   in a build tree on `/mnt/wsl/fixppbuild` (check `df -h /mnt/wsl/fixppbuild` first). Build rc 0,
   then run W-9(c)'s binary unfiltered, and confirm
   `grep -c "application_version::vlatest:" <OFF tree>/_codegen/include/fixpp/_dispatch/reify_dispatch_application.hpp`
   prints 0. The seeded positive is the same grep on the ON tree, which must print 1.
5. Fuzz (W-12, `[const §VII.7]`), in the main checkout's `build/linux-clang-asan` (check
   `df -h /mnt/wsl/fixppbuild` first):
   ```
   cmake --preset linux-clang-asan          # reconfigure: the corpus glob registers the new replay
   cmake --build --preset linux-clang-asan --target fuzz_appl_ver_id -j8
   ctest --preset linux-clang-asan -R '^fuzz_replay_appl_ver_id$' --output-on-failure
   build/linux-clang-asan/bin/fuzz_appl_ver_id -artifact_prefix=<scratch dir>/ tests/fuzz/corpus/appl_ver_id/*
   ```
   The replay passes; the last command exits 0 and names every seed file the directory holds (`ls`) in
   an `Executed` line. Compare names, not a total: libFuzzer's `-runs=0` replay adds an implicit empty
   input. The RED rows are §1.2's (contracts W-12). The ten-minute run is `/speckit-verify` Step 7,
   `--fuzz-duration=600`, because CI only replays the seeds.

## §2 — Registry and parse (US2, W-1/W-2/W-4/W-5)

`build/linux-clang-debug/tests/dictionary/<binary>` unfiltered: no `FR-010` text anywhere in its output
(`grep -c FR-010` = 0), and the W-4/W-5 tests pass. The grep is a secondary check: `BothDictsCoexist`
itself asserts that stderr captured around the constructions is empty (contracts W-4). Re-derive that the abort is gone:
`grep -rn "std::abort\|FR-010" src/dictionary/version_registry.cpp` → empty. Known positives, run
before trusting either empty result: the same pattern over the base copy,
`git show origin/main:src/dictionary/version_registry.cpp | command grep -c 'std::abort\|FR-010'`,
prints non-zero; and the `grep -c FR-010` over the test output reads non-zero on T011's record of
today's death test, whose pass is the abort.

## §3 — Golden regenerated by exactly the intended lines (W-9)

```
G=specs/076-fix-latest-typed-codegen/contracts/golden/vlatest_Messages.golden.hpp
GB=<scratch dir>/097-golden-base.hpp
python3 - "$G" "$GB" <<'EOF'
import pathlib, subprocess, sys
out = subprocess.run(["git", "show", f"origin/main:{sys.argv[1]}"], capture_output=True, check=True).stdout
assert len(out) > 0, sys.argv[1]
pathlib.Path(sys.argv[2]).write_bytes(out)
EOF
n0=$(command grep -c 'application_version::v50sp2;' "$GB")
git diff -U0 origin/main -- $G | command grep '^[-+][^-+]' | sort | uniq -c
```
The base copy is extracted with Python and size-checked, never with a shell `rev:path` built from a
variable: a shell hook can mangle that argument, and a mangled `n0` reads 0 (Gate A round 2, O097-104).
Expected: exactly two distinct changed-line shapes, `-…application_version::v50sp2;` and
`+…application_version::vlatest;`, each counted `n0` times; `git diff --stat origin/main -- $G` one
file. Seeded positive for the check: append one unrelated changed line in a scratch copy and confirm the
`uniq -c` output gains a third shape.

No other checked-in generated file holds a `fixpp::vlatest` class's version:
`git grep -l 'application_version::' -- specs | command grep golden` lists the 003 goldens
(`v42`, `v44`, `v50sp2`, `vt11`, none of them vlatest classes) and this one file. The 078 vlatest
builder/validator goldens carry no `application_version`, so `app_version_enum` cannot move them;
`DeterminismTest` and the full suite (W-10) would fail on any that did.

## §4 — The record (US4, FR-011, FR-012; contracts W-11)

### §4.1 Superseded statements

One union pattern. A broader `vlatest.*v50sp2` arm was tried and rejected: it matches every line that
merely lists both namespaces. The exclusions are in the command (Gate A round 2, G097-018), not in prose:
- pathspecs: frozen `specs/` bundles, `CLAUDE-history.md` (a changelog; history is not rewritten), the
  closed B&L file, and the constitution, which is scanned separately;
- the constitution is scanned from its `# fixpp Constitution` heading on (every Sync Impact Report
  entry precedes it), minus the live `> **Status:**` blockquote, whose "Prior: …" text is history.

```
P='no distinct ApplVerID|caps at 9|ApplExtID\(1156\)=303|L-074-1|L-080-1|session_to_application\(vlatest\)|vlatest *(->|→) *v50sp2|maps? to v50sp2|resolves? to (the )?(existing )?v50sp2|v50sp2 (registry )?slot|wire application version (is |IS )?(v50sp2|5\.0SP2)|no .?application_version::vlatest|ApplVerID 9 with no ApplExtID|session.negotiation (tiers?|remain)'
X=(':!specs/0[0-8]*' ':!specs/09[0-6]*' ':!specs/097*' ':!CLAUDE-history.md'
   ':!spec/behaviors-and-limitations-closed.md' ':!.specify/constitution.md')
OUT=<scratch dir>/097-sweep.txt
CR=<scratch dir>/097-current-rule.txt
git grep -n -i -E "$P" -- . "${X[@]}" > "$OUT"
awk '/^# fixpp Constitution/,0' .specify/constitution.md | command grep -v '^> \*\*Status:\*\*' \
  | command grep -i -E "$P" | sed 's|^|.specify/constitution.md (live): |' >> "$OUT"
# Current-rule list: fixed strings that match P but state today's rule, one per line, each with
# its reason in the verify record. The entry below is reify_dispatch_test.cpp's FR-003 message
# (it is about the value "9", which does map to v50sp2).
printf '%s\n' 'in-frame ApplVerID(1128)=9 must resolve to v50sp2' > "$CR"
command grep -vw '097' "$OUT" | command grep -v -F -f "$CR" | wc -l
```
- **Pass condition: the last line prints 0.** A hit line is *annotated* when it names `097` as a word
  (`-w`: a hit whose line number or text merely contains the digits `1097` is not annotated): a rewritten
  comment, a record that states the 097 rule, or an in-place `<!-- SUPERSEDED (097-… -->` comment on the
  same line (data-model §F). Every annotated hit is still read once and recorded as stating the current
  rule or flagging the supersession. A new current-rule entry is a review decision, recorded with its
  reason in the verify record.
- **Seeded positives**, before trusting an empty result:
  `printf 'session_to_application(vlatest) -> v50sp2\n' | command grep -c -i -E "$P"` must print 1, and
  `printf 'f:1097:session_to_application(vlatest) -> v50sp2\n' | command grep -vw '097' | wc -l` must
  print 1 (a line number that contains 097 does not count as an annotation).
- **Seeded negative**: the same pipeline over `origin/main` (`git grep -n -i -E "$P" origin/main -- .
  "${X[@]}"`, plus the awk over §4.2's extracted `$BASE/.specify/constitution.md`) must print a
  non-zero count. This proves the pass condition can fail.

The `session.negotiation (tiers?|remain)` arm (Gate A round 2) catches a residue statement written
from a superseded clause rather than from the canonical sentence (data-model §E.0).

The union pattern sees only the phrasings it lists; a paraphrase of the superseded claim escapes it.
What compensates: every annotated hit is read once; §4.2's positive-presence rows check each locus the
bundle edits; and a live-record hit that no data-model §C or §F edit names is treated as tasks T062
states.

The surfaces the sweep reaches include the library `CLAUDE.md` (its "Also open, no issue filed:
ApplExtID(1156)=303 …" line is deleted in plan phase 6, so it is gone at verify), `brain/`, the
`.specify/*.md` design docs, and the coverage index's Post-1.0 Gap Registry.

### §4.2 Positive-presence checks (an omitted addition is invisible to §4.1)

Extract the `origin/main` copies with Python, not a shell `rev:path` built from a variable, and
assert each is non-empty:
```
BASE=<scratch dir>/097-base
python3 - "$BASE" <<'EOF'
import pathlib, subprocess, sys
base = pathlib.Path(sys.argv[1])
for f in ["spec/feature-catalogue.md", "spec/coverage-index.md", ".specify/constitution.md",
          ".specify/2c-codegen.md", "spec/behaviors-and-limitations.md",
          "spec/behaviors-and-limitations-closed.md", "brain/components/dictionary.md",
          "tools/codegen/fixpp-codegen/emit_dispatch.cpp"]:
    out = subprocess.run(["git", "show", f"origin/main:{f}"], capture_output=True, check=True).stdout
    assert len(out) > 0, f
    p = base / f; p.parent.mkdir(parents=True, exist_ok=True); p.write_bytes(out)
EOF
```
The canonical residue sentence (data-model §E.0), for the rows that count it (single quotes are safe:
it holds no apostrophe):
```
CANON='For FIX Latest, only the EP-level field back-port into the legacy dictionaries and Extension Pack negotiation (DefaultApplExtID(1407) / ApplExtID(1156), which fixpp does not use to resolve a version or select a dictionary) remain post-1.0, both in the Article XVIII §2 v1.2 milestone.'
```
Run every check twice, with `R=.` (the head) and with `R="$BASE"` (the seeded negative). In the table, `\|` is the
Markdown escape for a literal `|`; type `|`. Use
`command grep`, because the shell's `grep` wrapper mis-reports exit codes. Each row gives the value
expected at the head; the `origin/main` run must give the "base" value, which proves the check can fail.

| Check | Command (prints a number) | head | base |
|---|---|---|---|
| S-025 version cell names FIX Latest | `awk -F'\|' '$2 ~ /^ S-025 $/ {print $6}' "$R/spec/feature-catalogue.md" \| command grep -c 'FIX Latest'` | 1 | 0 |
| S-026, S-043 and D-011 name 097 | `awk -F'\|' '$2 ~ /^ (S-026\|S-043\|D-011) $/' "$R/spec/feature-catalogue.md" \| command grep -cw '097'` | 3 | 0 |
| D-011's named edits, each (data-model §F) | for each `X` of `retired by 097: FIX Latest is ApplVerID 10, own registry slot`, `superseded by 097: FIX Latest is ApplVerID 10, its own application version`, `retired by 097: FIX Latest has its own registry slot`, `(count at 074)`, `OrchestraRegistry097.BothDictsCoexist`, `OrchestraRegistry097.OneWayFallback`: `awk -F'\|' '$2 ~ /^ D-011 $/' "$R/spec/feature-catalogue.md" \| command grep -cF "$X"` | 1 each | 0 each |
| No catalogue status changes (`[const §I.3]`; A-035..A-065 included; prints a digest, not a number) | `awk -F'\|' '$2 ~ /^ [A-Z]+-[0-9]+ $/ {print $2 "\|" $8}' "$R/spec/feature-catalogue.md" \| sort \| sha256sum` | the base digest | the comparand; seeded negative: the same command over a scratch copy of the base file with one row's status edited prints a different digest |
| A-035..A-065 sentence replaced | new: `command grep -cF "FIX Latest goes on the wire as ApplVerID 10 when the session's negotiated DefaultApplVerID is 10; a session configured 9 whose peer also sends 9 negotiates FIX50SP2 (097, B-097-1, B-097-4)" "$R/spec/feature-catalogue.md"`; old: `command grep -cF "FIX Latest goes on the wire as ApplVerID 9 with no ApplExtID(1156) (L-074-1, REMAINING-WORK row 4b)" "$R/spec/feature-catalogue.md"` | new = base's old; old = 0 | new = 0 |
| Coverage-index rows updated | `command grep -E '^\| (§4\.3\.7\|§5\.3\.3\|§5\.3\.5) \|\|^\| (DefaultApplVerID\(1137\) on Logon\|ApplVerID\(1128\) per message)' "$R/spec/coverage-index.md"` piped to `wc -l` (selection) and to `command grep -cw '097'` (hits) | hits = selection = 5 | hits 0, selection 5 |
| Constitution Article I §1 (bullet + milestone line) | `awk '/^## Article I —/,/^## Article II —/' "$R/.specify/constitution.md" \| command grep -c 'feature 097'` | 2 | 0 |
| Constitution Article XVIII §2 annotation | `awk '/^## Article XVIII —/,/^## Article XIX —/' "$R/.specify/constitution.md" \| command grep -c 'Amended v4.3, feature 097'` | 1 | 0 |
| Canonical residue, Article I §1 (bullet + milestone line) | `awk '/^## Article I —/,/^## Article II —/' "$R/.specify/constitution.md" \| command grep -cF "$CANON"` | 2 | 0 |
| Canonical residue, Article XVIII §2 | `awk '/^## Article XVIII —/,/^## Article XIX —/' "$R/.specify/constitution.md" \| command grep -cF "$CANON"` | 1 | 0 |
| Canonical residue, catalogue D-011 row | `awk -F'\|' '$2 ~ /^ D-011 $/' "$R/spec/feature-catalogue.md" \| command grep -cF "$CANON"` | 1 | 0 |
| Canonical residue, Gap Registry D-011 row | `awk '/^## Post-1.0 Gap Registry/,0' "$R/spec/coverage-index.md" \| command grep -F '(D-011)' \| command grep -cF "$CANON"` | 1 | 0 |
| Canonical residue, B-097-3 | `awk '/^- \*\*B-097-3 /,/^- \*\*B-097-4 /' "$R/spec/behaviors-and-limitations.md" \| command grep -cF "$CANON"` | 1 | 0 |
| Gap Registry D-011 target column | `awk '/^## Post-1.0 Gap Registry/,0' "$R/spec/coverage-index.md" \| command grep -F '(D-011)' \| command grep -cF 'v1.2 (EP-level field back-port; Extension Pack negotiation)'` | 1 | 0 |
| In-place 097 supersession comments | `command grep -c 'SUPERSEDED (097-fix-latest-applverid-10' "$R/<file>"` for each of `spec/feature-catalogue.md`, `spec/coverage-index.md`, `spec/behaviors-and-limitations.md` | 1 each | 0 each |
| `emit_dispatch.cpp` comments no longer state the old version set (G097-016; polarity reversed) | `command grep -cE 'three codegen application versions\|~470\|v42/v44/v50sp2' "$R/tools/codegen/fixpp-codegen/emit_dispatch.cpp"` | 0 | ≥ 1 |
| Status line (**run at the RATIFIED flip, not at verify**) | `command grep -c '^> \*\*Status:\*\* user-ratified \*\*v4.3\*\* .*Rides feature 097; its PR is titled in Article XX §2 form\. Prior: user-ratified \*\*v4\.2\*\*' "$R/.specify/constitution.md"` | 1 | 0 |
| Sync Impact Report header | `command grep -c 'Sync Impact Report — v4.2 → v4.3' "$R/.specify/constitution.md"` | 1 | 0 |
| L-074-1 / L-080-1 left the live B&L | `command grep -cE '^- \*\*L-0(74\|80)-1 ' "$R/spec/behaviors-and-limitations.md"` | 0 | 2 |
| …and arrived in the closed file | `command grep -cE '^<!-- L-0(74\|80)-1 — closed: resolved by 097' "$R/spec/behaviors-and-limitations-closed.md"` | 2 | 0 |
| New B&L rows | `command grep -cE '^- \*\*(B-097-[1-4]\|L-097-1) ' "$R/spec/behaviors-and-limitations.md"` | 5 | 0 |
| 2c notes (one per note data-model §C prescribes; Gate A round 3, G097-020) | `command grep -c 'Updated (097-fix-latest-applverid-10)' "$R/.specify/2c-codegen.md"` | 6 | 0 |
| Component page flags 074, 076 and 080 and points at 2c, each separately | for each `X` of `specs/074-orchestra-native-reader`, `specs/076-fix-latest-typed-codegen`, `specs/080-orchestra-runtime-load`, `2c-codegen`, `C-ABI`: `command grep -F "$X" "$R/brain/components/dictionary.md" \| command grep -cw '097'` | ≥ 1 each | 0 each |

In the "L-074-1 / L-080-1 left the live B&L" and the `emit_dispatch.cpp` rows the polarity is
reversed: there the base is the positive. A row whose base run gives the head value proves nothing and is rewritten before it is
trusted, except the "No catalogue status changes" row, whose head value IS the base value by design;
its seeded negative is what proves it can fail. The Status-line row is the one scheduled exception: before the RATIFIED flip it reads 0 on
both trees by design, so it is run, with its base, at the flip and recorded then.

### §4.3 Other checks

- New comments: `.claude/scripts/check-comment-claims.py --root . --base origin/main` (parent repo path)
  clean, or each hit adjudicated. No rewritten comment records a count or an enumerated value range
  (FR-012(c); the `tests/config` comments).
- Line citations: `python3 tools/check_line_citations.py --range origin/main..HEAD` → OK, and
  `--shift-audit origin/main..HEAD` → OK.
- Constitution: the Sync Impact Report reads v4.2 → v4.3, MINOR, and lists every item data-model §E.4
  names. Its Gate A / Gate B / owner sign-off dates are filled in that order before the RATIFIED flip.

## §5 — Interop sanity (optional, not a gate)

No live cell is added. A QuickFIX/J peer with `DefaultApplVerID=FIX.Latest` against a fixpp acceptor
holding the FIX Latest dictionary is the scenario W-6 (a) models in-process.
