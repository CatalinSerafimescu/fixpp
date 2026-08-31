#!/usr/bin/env python3
"""check_workflows — mechanize three CI defects that have already bitten this repo.

Each rule below exists because the defect HAPPENED, was diagnosed by hand, and was
written down as a lesson. A lesson is recall; this is a gate. That is the whole
point of the exercise -- the ones that CAN become checks should stop being things
anyone has to remember.

  R1  `paths-ignore` under `pull_request` on a REQUIRED check.
      A path-skipped required check never reports a conclusion, so branch
      protection waits forever and the PR can never merge. The repo's own
      workflows carry comments saying "push: ONLY, never pull_request" -- this
      makes that comment enforceable.

  R2  `continue-on-error: true` on a job/step whose purpose is to fail.
      It makes `exit 1` INERT: the step reports failure while the job concludes
      success, and the gate silently observes instead of asserting.

  R3  an unquoted `#` inside a `name:` value.
      YAML truncates it at the comment marker, silently, so the step name in the
      UI is not the name in the file -- and any log-grep keyed on the full name
      finds nothing and reads as "the step did not run".

⚠️ These are LEXICAL/structural checks on the workflow files. They cannot tell you
a workflow is correct -- only that these three specific, recorded traps are absent.
"""
import glob, os, re, sys

try:
    import yaml
except ImportError:
    sys.exit("check_workflows: PyYAML missing. Refusing to report clean -- a parse "
             "I could not perform is not a parse that passed.")

# A required check is one branch protection waits on. Kept as a NAME LIST rather
# than an API call so the check runs offline and in a fork; re-derive with
# `gh api repos/<owner>/<repo>/branches/main/protection`.
REQUIRED_HINTS = ("tier1", "tier2", "tier3", "gate-a", "gate-b", "abi-golden")


def load(path):
    with open(path, encoding="utf-8") as f:
        raw = f.read()
    return raw, yaml.safe_load(raw)


def check(root):
    files = sorted(glob.glob(os.path.join(root, ".github/workflows/*.yml")) +
                   glob.glob(os.path.join(root, ".github/workflows/*.yaml")))
    if not files:
        print("check_workflows: found ZERO workflow files -- that is a broken path, "
              "not a clean repo.", file=sys.stderr)
        return None
    findings = []
    for p in files:
        rel = os.path.relpath(p, root)
        raw, doc = load(p)
        if not isinstance(doc, dict):
            findings.append(("parse", rel, "did not parse to a mapping"))
            continue
        on = doc.get(True) or doc.get("on") or {}      # PyYAML reads bare `on:` as True
        pr = on.get("pull_request") if isinstance(on, dict) else None

        # R1
        if isinstance(pr, dict) and "paths-ignore" in pr:
            if any(h in rel for h in REQUIRED_HINTS):
                findings.append(("R1", rel, "paths-ignore under pull_request on a "
                                            "REQUIRED check -- it can never report"))

        # R2
        for jname, job in (doc.get("jobs") or {}).items():
            if not isinstance(job, dict):
                continue
            if job.get("continue-on-error") is True:
                # A deliberately non-gating job is legitimate -- but it must SAY SO in
                # its own name, where the CI UI shows it. This repo already does that
                # ("windows-wheel (best-effort)"). Requiring the declaration keeps an
                # intentional one quiet while an accidental one still fires, and it
                # puts the intent where a reader of the checks list will see it.
                label = str(job.get("name", ""))
                if not re.search(r"best-effort|non-gating|advisory", label, re.I):
                    findings.append(("R2", rel, f"JOB '{jname}' is continue-on-error -- it can "
                                                "never fail the run. If that is deliberate, say so "
                                                "in the job's `name:` (e.g. \"... (best-effort)\")"))
            for st in job.get("steps") or []:
                if not isinstance(st, dict) or st.get("continue-on-error") is not True:
                    continue
                # NARROW, on purpose. `continue-on-error` on a cache-save or a
                # diagnostic step is correct practice, and the first version of this
                # rule flagged 12 such steps in this repo -- a check that cries wolf on
                # legitimate use gets disabled within a week, which is worse than not
                # having it. The recorded defect is specifically "continue-on-error
                # makes an EXIT 1 INERT", so flag only steps that deliberately fail.
                body = str(st.get("run", ""))
                if re.search(r"\bexit\s+[1-9]", body):
                    findings.append(("R2", rel,
                                     f"step '{st.get('name', '?')}' in job '{jname}' "
                                     "runs `exit N` under continue-on-error -- the failure "
                                     "is INERT"))

        # R3 -- an unquoted # inside a name: value truncates it.
        #
        # Done in code, not with a lookahead. `^\s*name:\s*(?!['"])` LOOKS correct and
        # is not: \s* can backtrack to match zero characters, the lookahead then sees a
        # space rather than the quote, and a properly-quoted name is flagged. The
        # self-test caught that on a seeded NEGATIVE; reading the pattern did not.
        for m in re.finditer(r"(?m)^[ \t]*name:[ \t]*(\S[^\n]*)$", raw):
            val = m.group(1)
            if val[0] in "'\"":
                continue                      # quoted: the # is inside the string, safe
            if "#" in val:
                findings.append(("R3", rel, f"unquoted '#' truncates name: {val[:60]!r}"))
    return findings


def self_test():
    """Each rule seeded positive AND negative. A rule only ever run against a clean
    tree proves nothing: it would pass while unable to fire at all."""
    import tempfile, shutil
    fails = []
    d = tempfile.mkdtemp()
    wf = os.path.join(d, ".github/workflows")
    os.makedirs(wf)

    def write(n, s):
        open(os.path.join(wf, n), "w", encoding="utf-8").write(s)

    write("tier1.yml", "name: Tier 1\non:\n  pull_request:\n    paths-ignore:\n      - '**/*.md'\n"
                       "jobs:\n  a:\n    runs-on: x\n    steps:\n      - run: true\n")
    write("other.yml", "name: Other\non:\n  push:\njobs:\n  b:\n    runs-on: x\n"
                       "    steps:\n      - name: gate\n        continue-on-error: true\n"
                       "        run: |\n          echo bad\n          exit 1\n")
    write("third.yml", "name: Bench #1 gate\non:\n  push:\njobs:\n  c:\n    runs-on: x\n"
                       "    steps:\n      - run: true\n")
    write("declared.yml", "name: D\non:\n  push:\njobs:\n  d:\n    name: 'port (best-effort)'\n"
                          "    runs-on: x\n    continue-on-error: true\n    steps:\n      - run: true\n")
    write("undeclared.yml", "name: U\non:\n  push:\njobs:\n  e:\n    runs-on: x\n"
                            "    continue-on-error: true\n    steps:\n      - run: true\n")
    all_f = check(d)
    # Match the BASENAME exactly. `"declared" in "undeclared.yml"` is true, and that
    # substring collision made this very assertion report a false failure once.
    r2_files = {os.path.basename(f) for r, f, _ in all_f if r == "R2"}
    if "undeclared.yml" not in r2_files:
        fails.append("undeclared non-gating JOB did not fire")
    if "declared.yml" in r2_files:
        fails.append("a job that DECLARES itself best-effort was flagged -- false positive")
    got = {f[0] for f in all_f}
    for rule in ("R1", "R2", "R3"):
        if rule not in got:
            fails.append(f"{rule} did NOT fire on its seeded positive")
    shutil.rmtree(d)

    d = tempfile.mkdtemp()
    wf = os.path.join(d, ".github/workflows")
    os.makedirs(wf)
    write2 = lambda n, s: open(os.path.join(wf, n), "w", encoding="utf-8").write(s)
    write2("tier1.yml", "name: 'Tier 1 #ok'\non:\n  push:\n    paths-ignore:\n      - '**/*.md'\n"
                        "  pull_request:\njobs:\n  a:\n    runs-on: x\n    steps:\n      - run: true\n")
    clean = check(d)
    if clean:
        fails.append(f"clean tree produced findings -- false positives: {clean}")
    shutil.rmtree(d)

    if check(tempfile.mkdtemp()) is not None:
        fails.append("a directory with NO workflows did not fail closed")

    print(f"self-test: {7 - len(fails)}/7 pass")
    for f in fails:
        print("  FAIL", f)
    return 1 if fails else 0


def main():
    if "--self-test" in sys.argv:
        sys.exit(self_test())
    root = sys.argv[1] if len(sys.argv) > 1 else "."
    findings = check(root)
    if findings is None:
        sys.exit(2)
    if not findings:
        print("check_workflows: OK -- none of the three recorded traps present.")
        sys.exit(0)
    print(f"check_workflows: FAIL -- {len(findings)} finding(s):", file=sys.stderr)
    for rule, rel, msg in findings:
        print(f"  [{rule}] {rel}: {msg}", file=sys.stderr)
    sys.exit(1)


if __name__ == "__main__":
    main()
