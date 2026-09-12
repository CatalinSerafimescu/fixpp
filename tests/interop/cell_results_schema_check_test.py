# SPDX-License-Identifier: AGPL-3.0-or-later
#
# tests/interop/cell_results_schema_check_test.py — 016-interop-harness US4 (T028).
#
# Validates tests/interop/cell_results.yaml against the parent gate-evaluator's
# result schema (contracts/parent-harness-gate-contract.md). This is the in-repo
# half of the per-cell completeness contract: it proves the committed manifest is
# well-formed and that no matrix/corpus cell is silently absent, so the parent
# `interop-gate-evaluator` consumes a schema-conformant input.
#
# 089-quickfix-interop-conversation T026: also validates the STRUCTURE of the
# sibling witness_evidence.yaml artifact (data-model.md §6/§10/§11) — the full
# E-1c/W-3* completeness gates are implemented by later tasks (T066-T092);
# this file proves the three sections exist and are distinguishable from a
# missing/null section, and (T066-T068) implements the E-7a/E-7b/E-7c CHECK
# LOGIC itself, each proven against the CONSTRUCTED fixtures
# contracts/witness-evidence.md § Proof obligations prescribes for it.
# `test_schema_check_opens_no_artifact_path()` below runs the shape/
# `["cells"]`-signature checks against ONE config's per-run emission; the
# T095a block (further down this file) ranges the SAME check logic over the
# accumulated COMMITTED witness_evidence.yaml/cell_results.yaml pair.
#
# Run via ctest (registered in tests/interop/CMakeLists.txt) or directly:
#   python3 -m pytest -xvs tests/interop/cell_results_schema_check_test.py

import contextlib
import copy
import hashlib
import inspect
import os
import re
import sys

import pytest
import yaml

# PyYAML's pure-Python loader is 7-9x slower than libyaml's C loader, and YAML
# parsing dominates this file's wall clock (it re-reads four committed
# artifacts). Prefer the C loader; fall back when PyYAML was built without
# libyaml, which is a packaging property of the environment and not something
# this suite can assume. Nothing here asserts on yaml exception CLASSES, so the
# two loaders are interchangeable for our purposes.
try:
    _YamlLoader = yaml.CSafeLoader
except AttributeError:  # PyYAML built without libyaml
    _YamlLoader = yaml.SafeLoader

HERE = os.path.dirname(os.path.abspath(__file__))
MANIFEST = os.path.join(HERE, "cell_results.yaml")
WITNESS_EVIDENCE = os.path.join(HERE, "witness_evidence.yaml")
# 089 T082/W-2a: the census (E-1's witness_count join operand too) and the
# conversation script -- SC-009a's two independently-provenanced operands
# (contracts/witness-evidence.md W-2a; census.yaml's own header comment).
CENSUS = os.path.join(HERE, "conversation", "census.yaml")
SCRIPT = os.path.join(HERE, "conversation", "conversation_script.yaml")
PROBE_SCRIPT = os.path.join(HERE, "conversation", "probe_script.yaml")

REQUIRED_FIELDS = {"id", "config", "kind", "status", "matrix_disposition", "spec_ref"}
# data-model.md §5 "New — identity and run evidence (FR-013, FR-013a)": required
# CONDITIONALLY on kind: conversation only — an unconditional REQUIRED_FIELDS
# extension breaks the pre-existing `status: pass` rows already committed, none
# of which has an 089 run behind it (FR-020; contracts/witness-evidence.md E-1a).
CONVERSATION_REQUIRED_FIELDS = {
    "cell_id",
    "run_id",
    "run_timestamp",
    "script_digest",
    "counterparty_flavour",
    "counterparty_version",
    "counterparty_digest",
    "ledger_ref",
}
KINDS = {"happy", "thorny", "parity", "conversation"}
CONFIGS = {"normal", "asan", "ubsan", "tsan"}
# data-model.md §5 + spec.md FR-014a: "A run killed by ENOSPC is recorded as
# error:enospc (with aborted as the general class), never as pass, skip, n/a
# or fail." `_status_kind` splits on the first ":", so the closed-set token
# for `error:enospc` is `error`; `aborted` is its own bare token (the general
# infra-abort class FR-014a names alongside the ENOSPC-specific one).
STATUS_KINDS = {"pass", "fail", "skip", "known-limitation", "n/a", "error", "aborted"}
PRIORITIES = {"P1", "P2", "P3", "watch:P1", "watch:P2", "watch:info"}
# data-model.md §6/§10/§11: witness_evidence.yaml's three top-level sections.
# `witnesses` is pinned as the section key in data-model.md §6.
WITNESS_EVIDENCE_SECTIONS = ("witnesses", "runs", "validation_pairs")


class ArtifactPathOpened(Exception):
    """Raised by the artifact-path audit hook (see _artifact_path_guard below)
    when the schema check attempts to open anything other than the two
    committed, in-repo manifests. The check MUST open nothing else — it is a
    ctest provisioned on tier1/tier2/tier3-libcxx hosted runners that hold no
    run artifacts."""


# T030 fix round: a monkeypatch on builtins.open alone walks straight past
# pathlib (Path.open()/read_text()/write_text() bind their own C-level open,
# not builtins.open) and os.open() (same). Verified empirically: `sys.audit`
# tracing shows the "open" audit event fires for ALL THREE call paths —
# builtins.open(), pathlib's Path methods, and os.open() — so it is the one
# mechanism that sees every open regardless of call path.
#
# sys.addaudithook() cannot be uninstalled once added (CPython — there is no
# removehook), so this hook is installed ONCE, permanently, at import time,
# and is a no-op unless _ARTIFACT_GUARD_ACTIVE is True — which only the
# _artifact_path_guard() context manager below sets, for the duration of a
# single `with` block.
_ARTIFACT_GUARD_ACTIVE = False
_ARTIFACT_GUARD_ALLOWED_PATHS = set()


def _artifact_path_audit_hook(event, args):
    if event != "open" or not _ARTIFACT_GUARD_ACTIVE:
        return
    raw_path = args[0]
    if raw_path is None:
        return
    try:
        raw_path = os.fspath(raw_path)
    except TypeError:
        # Not path-like (e.g. an int fd via os.open(dir_fd=...)) — nothing to
        # check against a path allow-list.
        return
    if isinstance(raw_path, bytes):
        raw_path = os.fsdecode(raw_path)
    real_path = os.path.realpath(raw_path)
    if real_path not in _ARTIFACT_GUARD_ALLOWED_PATHS:
        raise ArtifactPathOpened(real_path)


sys.addaudithook(_artifact_path_audit_hook)


@contextlib.contextmanager
def _artifact_path_guard():
    global _ARTIFACT_GUARD_ACTIVE, _ARTIFACT_GUARD_ALLOWED_PATHS
    _ARTIFACT_GUARD_ALLOWED_PATHS = {
        os.path.realpath(MANIFEST), os.path.realpath(WITNESS_EVIDENCE),
        os.path.realpath(CENSUS), os.path.realpath(SCRIPT),
    }
    _ARTIFACT_GUARD_ACTIVE = True
    try:
        yield
    finally:
        _ARTIFACT_GUARD_ACTIVE = False
        _ARTIFACT_GUARD_ALLOWED_PATHS = set()


DEFERRED_TAGS = {
    # deferred:fixt-routing RETIRED 2026-06-12 (033 US3): the 8 FIXT.1.1
    # establishment cells are live (HP-*-fixt11-{fix50sp2,fix44}-logon-hb-logout).
    "deferred:fix8-revisit",
    "deferred:v1.1-mtls",
    # QuickFIX-cpp cannot emit a controllable too-low PossDup (Session::send()
    # strips 43/122; no public sendRaw/AllowPosDup) and never resends an
    # already-seen frame — so the 4 PD-QFcpp-* cells are by-design not runnable.
    # fixpp's receive-path PossDup tolerance is proven engine-independently via
    # the 4 PD-QFj-* cells + test_inbound_poss_dup_tolerance.cpp.
    "deferred:qfcpp-no-possdup-injection",
    # testrequest-echo + reject-invalid-admin are QFj-only at G1: the gtest
    # GTEST_SKIPs non-QFj (the inbound-silence / proxy_corrupt induction seams are
    # only configured in the QFJ parent harness, T020 [PARENT]). The 4 QFcpp
    # variants are by-design not run; the QFj variants are live (reject-invalid-
    # admin via the cp_corrupt_admin 55=BAD induction). Phase 9.H step 2.
    "deferred:qfj-only-at-g1",
    # (recovery-outbound was un-deferred to live in the 9.H follow-up via a
    # non-degenerate app-replay induction: fixpp sends a NewOrderSingle, QFJ rewinds
    # its expected-target seqnum + ResendRequests it, fixpp REPLAYS the stored NOS
    # with PossDup(43=Y) which reaches QFJ fromApp; idle-cadence likewise via a
    # count-tolerant cadence gate. No recovery-outbound/idle-cadence deferred tags
    # remain.)
}


def _status_kind(status):
    """Return the leading token of a status value (before any ':<reason>')."""
    return status.split(":", 1)[0]


def _load_cells():
    with open(MANIFEST, encoding="utf-8") as fh:
        doc = yaml.load(fh, Loader=_YamlLoader)
    assert doc.get("schema_version") == 1, "manifest must declare schema_version: 1"
    rows = doc.get("cells")
    assert isinstance(rows, list) and rows, "manifest must carry a non-empty `cells` list"
    return rows


@pytest.fixture(scope="module")
def cells():
    return _load_cells()


def _load_witness_evidence():
    with open(WITNESS_EVIDENCE, encoding="utf-8") as fh:
        doc = yaml.load(fh, Loader=_YamlLoader)
    assert doc.get("schema_version") == 1, \
        "witness_evidence.yaml must declare schema_version: 1"
    return doc


@pytest.fixture(scope="module")
def witness_evidence_doc():
    return _load_witness_evidence()


def _check_witness_evidence_sections(doc):
    # data-model.md §6/§10/§11: witness_evidence.yaml carries THREE sections.
    # A MISSING section is exactly the zero-pairs-green hazard T026 names
    # (contracts/witness-evidence.md E-7a): "an implementation emitting none
    # would satisfy the gates that range over runs and witnesses". Plain
    # `section in doc` cannot tell "present and []" from "present and None"
    # (a hand-edited `validation_pairs:` with no value) from "absent" — check
    # all three states explicitly.
    for section in WITNESS_EVIDENCE_SECTIONS:
        assert section in doc, \
            f"witness_evidence.yaml missing section {section!r}"
        assert isinstance(doc[section], list), (
            f"witness_evidence.yaml section {section!r} must be a list, "
            f"got {doc[section]!r} (present-but-null is not the same as "
            f"present-and-empty)"
        )


def test_witness_evidence_sections_present(witness_evidence_doc):
    _check_witness_evidence_sections(witness_evidence_doc)


def test_witness_evidence_missing_section_goes_red():
    # forced-miss (quickstart.md Step 4 rule 1/2): delete a section and
    # assert the checker names it, not merely "raises AssertionError".
    mutant = {"schema_version": 1, "witnesses": [], "runs": []}
    with pytest.raises(AssertionError, match="validation_pairs"):
        _check_witness_evidence_sections(mutant)


def test_witness_evidence_null_section_goes_red():
    # A hand-edited `validation_pairs:` with no value parses to None, not
    # `[]` — a third state between "present and empty" and "missing" that a
    # bare `section in doc` check cannot distinguish from a real empty list.
    mutant = {"schema_version": 1, "witnesses": [], "runs": [], "validation_pairs": None}
    with pytest.raises(AssertionError, match="validation_pairs"):
        _check_witness_evidence_sections(mutant)


# ── T066/T067/T068: E-7a / E-7b / E-7c (contracts/witness-evidence.md §
# Obligations) — the validation_pairs: gates, re-evaluated against the
# `runs:` ledger every time (both sections are committed; neither check
# opens an artifact). See the module header comment for why these are not
# yet wired into test_schema_check_opens_no_artifact_path()'s sweep over
# the REAL committed doc. ────────────────────────────────────────────────────

# census.yaml § role_flavour_combinations: C1..C4 — hand-kept here exactly as
# EXPECTED_IDS below is (this file's own established pattern), not derived,
# because deriving it would require opening a third committed file the
# artifact-path guard does not yet allow.
CONV_COMBO_IDS = ("C1", "C2", "C3", "C4")


def _conformance_run(cell_id, config, arm, has_validator):
    return {"run_id": f"{cell_id}-{config}-{arm}-run", "cell_id": cell_id, "config": config,
            "combo_id": cell_id.split("-")[1], "arm": arm, "kind": "conformance",
            "authoritative": True, "has_validator": has_validator}


def _conformance_pair(combo_id, config):
    off_cell, on_cell = f"CONV-{combo_id}-off", f"CONV-{combo_id}-on"
    return {"pair_id": f"{off_cell}~{on_cell}@{config}", "cell_pair": [off_cell, on_cell],
            "config": config, "off_run_id": f"{off_cell}-{config}-validation-off-run",
            "on_run_id": f"{on_cell}-{config}-validation-on-run", "kind": "conformance",
            "accepted_off": ["B-02"], "accepted_on": ["B-02"], "dispositions": [],
            "authoritative": True, "verdict": "identical"}


def _e7_fixture_complete():
    """The 16-pair kind:conformance inventory (4 combos × 4 configs) PLUS one
    admissible kind:validator-positive-control pair -- every E-7a/E-7b/E-7c
    predicate satisfied. Every negative fixture in this section starts from
    a (deep) copy of this and perturbs exactly the one thing its own arm
    names."""
    import copy
    runs = []
    pairs = []
    for combo_id in CONV_COMBO_IDS:
        for config in CONFIGS:
            off_cell, on_cell = f"CONV-{combo_id}-off", f"CONV-{combo_id}-on"
            runs.append(_conformance_run(off_cell, config, "validation-off", False))
            runs.append(_conformance_run(on_cell, config, "validation-on", True))
            pairs.append(_conformance_pair(combo_id, config))
    ctrl_off = {"run_id": "ctrl-off-run", "cell_id": "CONV-C1-off", "config": "normal",
                "kind": "validator-positive-control", "authoritative": True,
                "has_validator": False, "expected_verdict": "diverged"}
    ctrl_on = {"run_id": "ctrl-on-run", "cell_id": "CONV-C1-on", "config": "normal",
               "kind": "validator-positive-control", "authoritative": True,
               "has_validator": True, "expected_verdict": "diverged"}
    ctrl_pair = {"pair_id": "ctrl-pair", "cell_pair": ["CONV-C1-off", "CONV-C1-on"],
                 "config": "normal", "off_run_id": "ctrl-off-run", "on_run_id": "ctrl-on-run",
                 "kind": "validator-positive-control", "accepted_off": ["B-01"],
                 "accepted_on": [], "dispositions": [], "authoritative": True,
                 "verdict": "diverged", "expected_verdict": "diverged"}
    doc = {"schema_version": 1, "witnesses": [],
           "runs": runs + [ctrl_off, ctrl_on], "validation_pairs": pairs + [ctrl_pair]}
    return copy.deepcopy(doc)


def _runs_by_id(doc):
    return {r["run_id"]: r for r in doc.get("runs", []) if "run_id" in r}


def _authoritative_pairs(doc, kind):
    return [p for p in doc.get("validation_pairs", [])
            if p.get("kind") == kind and p.get("authoritative") is True]


def _check_e7a(doc):
    """E-7a: the `validation_pairs:` section MUST carry EXACTLY the 16-pair
    `kind: conformance` inventory (4 combos × 4 configs), scoped
    `kind: conformance ∧ authoritative: true` — set EQUALITY, not
    containment — AND exactly one such pair per (combo_id, config) slot (a
    slot claimed twice collapses under bare set equality, which is why this
    is a SEPARATE check from the equality below, not folded into it)."""
    runs = _runs_by_id(doc)
    slot_counts: dict[tuple, int] = {}
    for p in _authoritative_pairs(doc, "conformance"):
        off_run = runs.get(p.get("off_run_id"))
        combo_id = off_run.get("combo_id") if off_run is not None else None
        key = (combo_id, p.get("config"))
        slot_counts[key] = slot_counts.get(key, 0) + 1
    duplicated = {k for k, c in slot_counts.items() if c > 1}
    assert not duplicated, (
        f"E-7a: {duplicated!r} claimed by more than one authoritative "
        f"kind:conformance pair")
    observed = set(slot_counts)
    expected = {(c, cfg) for c in CONV_COMBO_IDS for cfg in CONFIGS}
    missing = expected - observed
    unexpected = observed - expected
    assert not missing and not unexpected, (
        f"E-7a: validation_pairs kind:conformance,authoritative:true slot set != "
        f"the 16-pair inventory; missing={missing!r} unexpected={unexpected!r}")


def _check_e7b(doc):
    """E-7b: AT LEAST ONE `authoritative: true` `kind: validator-positive-
    control` pair MUST exist, carrying `expected_verdict: diverged` AND
    `verdict: diverged`, whose off_run_id/on_run_id resolve to two DISTINCT
    `authoritative: true` control runs with OPPOSITE has_validator. Restates
    E-7's own distinctness/opposite-arm predicate rather than leaning on it
    (E-7 fires once, at construction; E-7c is what re-evaluates on every CI
    run — this is that host for the control half)."""
    runs = _runs_by_id(doc)
    for p in _authoritative_pairs(doc, "validator-positive-control"):
        if p.get("expected_verdict") != "diverged" or p.get("verdict") != "diverged":
            continue
        off_run = runs.get(p.get("off_run_id"))
        on_run = runs.get(p.get("on_run_id"))
        if off_run is None or on_run is None:
            continue
        if off_run.get("authoritative") is not True or on_run.get("authoritative") is not True:
            continue
        if off_run.get("run_id") == on_run.get("run_id"):
            continue
        if off_run.get("has_validator") == on_run.get("has_validator"):
            continue
        return  # an admissible control pair exists
    raise AssertionError(
        "E-7b: no admissible authoritative:true kind:validator-positive-control pair "
        "(expected_verdict: diverged, verdict: diverged, resolving to two distinct "
        "authoritative:true control runs with opposite has_validator)")


def _check_e7c(doc):
    """E-7c: for EVERY `authoritative: true` pair, BOTH referenced runs MUST
    STILL be `authoritative: true` — re-evaluated here, on every CI run,
    because `authoritative` is MUTABLE after a pair is written (a later
    retry supersedes a referenced run and E-7, which fired once at
    construction, does not re-run). For a `conformance` pair, each
    reference MUST additionally still be the run SELECTED for its
    (cell_id, config) slot -- i.e. the unique authoritative kind:conformance
    run for that slot, guarding independently of whatever else may or may
    not have kept E-1c's one-per-slot invariant true."""
    all_runs = doc.get("runs", [])
    runs = _runs_by_id(doc)
    for p in doc.get("validation_pairs", []):
        if p.get("authoritative") is not True:
            continue
        pair_id = p.get("pair_id")
        off_run = runs.get(p.get("off_run_id"))
        on_run = runs.get(p.get("on_run_id"))
        assert off_run is not None and on_run is not None, (
            f"E-7c: pair {pair_id!r} references a run_id absent from runs:")
        assert off_run.get("authoritative") is True and on_run.get("authoritative") is True, (
            f"E-7c: pair {pair_id!r} is authoritative:true but references a "
            f"SUPERSEDED run (off authoritative={off_run.get('authoritative')!r}, "
            f"on authoritative={on_run.get('authoritative')!r})")
        if p.get("kind") != "conformance":
            continue
        for run, label in ((off_run, "off"), (on_run, "on")):
            selected = [r for r in all_runs
                        if r.get("cell_id") == run.get("cell_id")
                        and r.get("config") == run.get("config")
                        and r.get("kind") == "conformance"
                        and r.get("authoritative") is True]
            assert run in selected, (
                f"E-7c: pair {pair_id!r}'s {label} reference is not the run "
                f"selected for its ({run.get('cell_id')!r}, {run.get('config')!r}) slot")


def test_e7a_sixteen_pair_inventory_equality():
    _check_e7a(_e7_fixture_complete())


def test_e7a_duplicate_slot_goes_red():
    # ⭐ SPURIOUS-HIT (T072 sibling / contracts/witness-evidence.md § Proof
    # obligations): two authoritative kind:conformance pairs for ONE
    # (cell_pair, config), opposite verdicts, every other gate satisfied —
    # set equality alone COLLAPSES the duplicate and reports complete.
    doc = _e7_fixture_complete()
    dup = dict(doc["validation_pairs"][0])
    dup["pair_id"] = dup["pair_id"] + "-dup"
    dup["verdict"] = "diverged" if dup["verdict"] == "identical" else "identical"
    doc["validation_pairs"].append(dup)
    with pytest.raises(AssertionError, match="claimed by more than one"):
        _check_e7a(doc)


def test_e7a_empty_section_goes_red_on_an_otherwise_complete_artifact():
    # ⭐ T071: the arm that closes "zero pairs is green" — forced against the
    # OTHERWISE-COMPLETE artifact (runs: populated), not a stub.
    doc = _e7_fixture_complete()
    doc["validation_pairs"] = []
    with pytest.raises(AssertionError, match="16-pair inventory"):
        _check_e7a(doc)


def test_e7a_fifteen_of_sixteen_goes_red_on_the_inventory_equality():
    # T072: 15 of 16 conformance pairs, the other 15 well-formed and E-7-
    # satisfying on every one -- equality, not containment. The empty
    # fixture alone cannot discriminate an existence check from a
    # completeness check.
    doc = _e7_fixture_complete()
    dropped = doc["validation_pairs"][0]["pair_id"]
    doc["validation_pairs"] = [p for p in doc["validation_pairs"]
                                if p["pair_id"] != dropped]
    with pytest.raises(AssertionError, match="16-pair inventory") as excinfo:
        _check_e7a(doc)
    assert "missing=" in str(excinfo.value) and "unexpected=set()" in str(excinfo.value)


def test_e7b_admissible_control_pair_required():
    _check_e7b(_e7_fixture_complete())


def test_e7b_sixteen_conformance_pairs_no_control_pair_goes_red():
    # ⭐ an OTHERWISE-COMPLETE artifact (16 valid kind:conformance pairs) and
    # NO kind:validator-positive-control pair at all. Assert E-7a stays
    # GREEN on this fixture -- it excludes control pairs from its equality
    # by design, so it reports complete over an artifact whose 16
    # conformance `identical` verdicts are, per data-model §10, inadmissible
    # without one.
    doc = _e7_fixture_complete()
    doc["validation_pairs"] = [p for p in doc["validation_pairs"]
                                if p["kind"] != "validator-positive-control"]
    doc["runs"] = [r for r in doc["runs"] if r["kind"] != "validator-positive-control"]
    _check_e7a(doc)  # control absence does not perturb E-7a
    with pytest.raises(AssertionError, match="no admissible"):
        _check_e7b(doc)


def test_e7b_control_pair_demoted_by_run_supersession_goes_red():
    # ⭐ the ONLY control pair present resolves, is well-formed, carries
    # expected_verdict/verdict: diverged -- but is authoritative: false
    # because one referenced run was superseded and no replacement pair was
    # built. Forced miss is the correct polarity (the defect IS an absence
    # of an ADMISSIBLE pair) but only against this otherwise-complete shape.
    doc = _e7_fixture_complete()
    for p in doc["validation_pairs"]:
        if p["kind"] == "validator-positive-control":
            p["authoritative"] = False
    with pytest.raises(AssertionError, match="no admissible"):
        _check_e7b(doc)


def test_e7c_stale_authoritative_pair_after_run_supersession_goes_red():
    # ⭐ T068's own fixture (contracts/witness-evidence.md § Proof
    # obligations): a pair STILL authoritative:true, one of whose referenced
    # runs was LATER superseded by a retry (its own replacement run row IS
    # present and authoritative), and NO replacement pair was constructed --
    # the producer re-promoted the run and never rebuilt the pair.
    doc = _e7_fixture_complete()
    stale_off_run_id = doc["validation_pairs"][0]["off_run_id"]
    stale_run = next(r for r in doc["runs"] if r["run_id"] == stale_off_run_id)
    stale_run["authoritative"] = False
    replacement = dict(stale_run)
    replacement["run_id"] = stale_run["run_id"] + "-retry"
    replacement["authoritative"] = True
    doc["runs"].append(replacement)
    # E-1c and E-7a both stay GREEN on this artifact -- exactly one
    # authoritative pair claims the slot and the 32-slot equality (E-1c,
    # not implemented in this file yet) is unaffected by a RUN-level retry
    # that never touched the pairs section; assert E-7a specifically, since
    # this file DOES implement it.
    _check_e7a(doc)
    with pytest.raises(AssertionError, match="SUPERSEDED run"):
        _check_e7c(doc)


def test_e7c_fresh_artifact_passes():
    _check_e7c(_e7_fixture_complete())


# ── T075/T078/T079/T080/T081/T082/T083/T084/T085 (Phase 7 / US5): E-1/E-1a/
# E-1c/E-4/E-5 and the W-* completeness gate — check LOGIC, proven against
# CONSTRUCTED fixtures (the E-7a/b/c pattern above). E-1a, E-1c, W-3a, W-3b,
# W-3c range over the COMMITTED witness_evidence.yaml/cell_results.yaml pair
# in the T095a block further down this file, not here: each of these gates'
# own defect is "an empty/short population is green", so they are proven
# against fixtures first and then re-run over the real accumulated artifact,
# never unconditionally against a single per-config emission. W-2a is
# DIFFERENT: its two real operands (census.yaml, conversation_script.yaml)
# are already fully authored content, not run output, so it IS wired below,
# now. ───────────────────────────────────────────────────────────────────

EIGHT_LOGICAL_CELL_IDS = frozenset(
    f"CONV-{c}-{a}" for c in CONV_COMBO_IDS for a in ("off", "on"))
THIRTY_TWO_SLOT_INVENTORY = frozenset(
    (cell, cfg) for cell in EIGHT_LOGICAL_CELL_IDS for cfg in CONFIGS)


def _load_census():
    with open(CENSUS, encoding="utf-8") as fh:
        return yaml.load(fh, Loader=_YamlLoader)


def _load_script():
    with open(SCRIPT, encoding="utf-8") as fh:
        return yaml.load(fh, Loader=_YamlLoader)


def _expand_business_steps_combo(doc, combo_id):
    """spec.md § The expansion rule, restricted to one combo_id — the SAME
    algorithm applied to census.yaml (W-2a's census operand) and to
    conversation_script.yaml (W-2a's script operand); both files share the
    business_steps[]/applicable_combos/occurrences shape (data-model §7)."""
    keys = set()
    for step in doc.get("business_steps", []):
        if combo_id not in step.get("applicable_combos", []):
            continue
        for occ in step.get("occurrences", {}).get(combo_id, []):
            keys.add((step["step_id"], step["direction"], int(occ)))
    return keys


def _cell_arm(cell_id):
    return cell_id.rsplit("-", 1)[-1]  # "CONV-C1-off" -> "off"


def _full_keys(doc):
    """The census's 100 completeness keys, in identity-2 shape (cell_id,
    script_step_id, direction, occurrence) — cell_id ≡ (combo_id, arm),
    arm-independent by construction (the expansion rule's 'for each arm' is
    a cross product over an identical per-combo step/occurrence set)."""
    keys = set()
    for combo_id in CONV_COMBO_IDS:
        combo_keys = _expand_business_steps_combo(doc, combo_id)
        for arm in ("off", "on"):
            cell_id = f"CONV-{combo_id}-{arm}"
            for step_id, direction, occ in combo_keys:
                keys.add((cell_id, step_id, direction, occ))
    return keys


def _full_keys_collapsed(doc):
    """⚠️ DELIBERATELY WEAKER than _full_keys — exists ONLY as a spurious-hit
    arm operand (T090). Drops combo_id, keeping only arm (contracts/witness-
    evidence.md W-3a note: 'the obvious remainder after dropping run_id and
    config ... collapses all four role×flavour combos into one set')."""
    return {(_cell_arm(cell_id), step_id, direction, occ)
            for cell_id, step_id, direction, occ in _full_keys(doc)}


def _witness_rows_for(doc, config, kind="conformance", authoritative=True):
    return [w for w in doc.get("witnesses", [])
            if w.get("config") == config and w.get("kind") == kind
            and w.get("authoritative") is authoritative]


def _project_identity2(rows):
    return {(w["cell_id"], w["script_step_id"], w["direction"], int(w["occurrence"]))
            for w in rows}


def _project_collapsed(rows):
    return {(_cell_arm(w["cell_id"]), w["script_step_id"], w["direction"], int(w["occurrence"]))
            for w in rows}


def _check_w1(doc):
    """W-1: one row per identity 1, carrying combo_id, cell_id, config,
    run_id, arm, authoritative AND kind as fields — W-3a/W-3b/W-3c filter on
    kind/authoritative, so a row missing one is silently DROPPED from the
    population it should have joined, and the gate measures a smaller set
    and reports complete. `verdict`/`mismatch` (gate-b fix round, FQ-1) are
    REQUIRED here too -- the RESULT the witness exists to record, not just
    its identity -- so a row missing one fails the SAME shape gate rather
    than surviving to be silently absent-therefore-fine at _check_w0_content
    below."""
    required = {"combo_id", "cell_id", "config", "run_id", "arm", "kind",
                "authoritative", "script_step_id", "direction", "occurrence",
                "verdict", "mismatch"}
    for w in doc.get("witnesses", []):
        missing = required - w.keys()
        assert not missing, f"W-1: witness row missing field(s) {missing!r}: {w!r}"


# closed set: this comparator never emits "skip" (data-model.md §4) and a
# committed conformance witness may never be recorded as anything but pass
# or fail.

# ── FR-008c: the recorded script_digest must equal the script ON DISK ─────────
#
# gate-b round 2. `script_digest` exists to detect that the script the cells
# EXECUTED differs from the script in the tree. Nothing compared the recorded
# value against the file, so the two drifted silently: a citation-cleanup commit
# rewrote two COMMENT lines in conversation_script.yaml, changing its bytes and
# therefore its SHA-256, while all 32 conformance rows kept the pre-edit digest.
# The evidence still described a real run -- but of a script blob no longer in
# the tree, so a fresh live run would have failed conv_cell_test's own digest
# assertion. A drift detector nothing reads is not a detector.
#
# ⛔ CONSEQUENCE, and it is the point: these script files are DIGEST-BOUND.
# Editing one -- even a comment -- invalidates the committed evidence and this
# gate reddens until the matrix is regenerated against the new bytes. That cost
# is the feature. The diagnostic below says so, because the file itself cannot:
# adding a warning comment to it would change the very bytes it warns about.
SCRIPT_BY_RUN_KIND = {
    "conformance": SCRIPT,
    "validator-positive-control": PROBE_SCRIPT,
}


def _file_sha256(path):
    with open(path, "rb") as fh:
        return hashlib.sha256(fh.read()).hexdigest()


def _check_script_digest_binding(witness_doc, cells_rows):
    runs = witness_doc.get("runs") or []
    assert runs, "script-digest binding: no runs to check -- vacuous"

    live = {kind: _file_sha256(path) for kind, path in SCRIPT_BY_RUN_KIND.items()}
    checked = 0
    for run in runs:
        kind = run.get("kind")
        assert kind in SCRIPT_BY_RUN_KIND, (
            "script-digest binding: run %r has kind %r, which names no script. "
            "A new run kind must declare which script it executes, or its digest "
            "is unbound." % (run.get("run_id"), kind))
        expected = live[kind]
        actual = run.get("script_digest")
        assert actual == expected, (
            "script-digest binding: run %r (kind=%s) records script_digest %r but "
            "%s currently hashes to %r. The committed evidence attests a script "
            "blob that is no longer in the tree -- regenerate the matrix against "
            "the current script, or restore the script to the bytes the evidence "
            "was produced from. Do NOT edit the recorded digest: that would claim "
            "a run happened against a script it never saw."
            % (run.get("run_id"), kind, actual, os.path.basename(SCRIPT_BY_RUN_KIND[kind]), expected))
        checked += 1
    assert checked == len(runs), "script-digest binding: checked %d of %d runs" % (checked, len(runs))

    # the manifest's conversation rows carry the same field and must agree
    conv = [c for c in cells_rows if c.get("kind") == "conversation"]
    assert conv, "script-digest binding: no kind:conversation manifest rows -- vacuous"
    for row in conv:
        actual = row.get("script_digest")
        assert actual == live["conformance"], (
            "script-digest binding: manifest row %r records script_digest %r but "
            "conversation_script.yaml currently hashes to %r."
            % (row.get("cell_id"), actual, live["conformance"]))

WITNESS_VERDICTS = {"pass", "fail"}


def _check_w0_content(doc, census_doc):
    """W-0 (gate-b fix round, FQ-1/Codex #1): gates the witness RESULT, not
    merely its shape/identity -- the thing 400 committed witnesses exist to
    record, which no other gate in this file reads (grep 'verdict\\|mismatch'
    over this file, pre-fix, hits only validation_pairs' `expected_verdict`
    control). Every row in the committed artifact with kind:conformance and
    authoritative:true must be verdict:pass with an empty mismatch list.
    ⚠️ The kind/authoritative filter is untested by any OTHER gate (every
    committed row happens to satisfy it), so the filtered population's SIZE
    is asserted against the census-derived figure BEFORE the per-row loop --
    the same silent-drop trap _check_w1's docstring names for W-3a/W-3b/
    W-3c, reproduced here by a typo'd `kind`/`authoritative` that would
    otherwise shrink the population this gate still reports complete over."""
    expected_count = len(_full_keys(census_doc)) * len(CONFIGS)
    rows = [w for w in doc.get("witnesses", [])
            if w.get("kind") == "conformance" and w.get("authoritative") is True]
    assert len(rows) == expected_count, (
        f"W-0: {len(rows)} authoritative kind:conformance witnesses in the committed "
        f"artifact, expected {expected_count} (census keys x configs) -- a row with a "
        f"mismatched kind/authoritative silently dropped out of this population")
    for w in rows:
        wid = w.get("witness_id")
        assert "verdict" in w, f"W-0: witness {wid!r} missing 'verdict' key"
        assert "mismatch" in w, f"W-0: witness {wid!r} missing 'mismatch' key"
        assert w["verdict"] in WITNESS_VERDICTS, (
            f"W-0: witness {wid!r} verdict {w['verdict']!r} not in the closed set "
            f"{sorted(WITNESS_VERDICTS)!r}")
        assert w["verdict"] == "pass", (
            f"W-0: witness {wid!r} verdict {w['verdict']!r} != 'pass' "
            f"(mismatch={w.get('mismatch')!r})")
        assert w["mismatch"] == [], (
            f"W-0: witness {wid!r} verdict is 'pass' but mismatch is non-empty: "
            f"{w['mismatch']!r}")


def _check_w2a(census_doc, script_doc):
    """W-2a: the SCRIPT-derived completeness-key set MUST equal the
    CENSUS-derived one, exactly — two files with independent provenance, the
    SAME expansion algorithm applied to each. ⛔ Neither side is derived from
    the other, or the equality agrees with itself and is not a second
    opinion (the defect this obligation exists to close)."""
    census_keys = _full_keys(census_doc)
    script_keys = _full_keys(script_doc)
    missing = census_keys - script_keys
    unexpected = script_keys - census_keys
    assert not missing and not unexpected, (
        f"W-2a: script-derived completeness keys != census; "
        f"missing={sorted(missing)} unexpected={sorted(unexpected)}")


def _check_w2a_script_derived_expected_WRONG(census_doc, script_doc):
    """⚠️ NOT a real check — exists ONLY as T087's third arm operand: the
    SAME signature and call shape as _check_w2a, but the WRONG
    implementation quickstart.md Step 4 warns against — the `expected` side
    is (re-)derived from the SCRIPT rather than the CENSUS (`census_doc` is
    accepted, matching the real signature, but never consulted), so the
    equality agrees with itself by construction and CANNOT redden on any
    script mutation. ⚠️ Unlike _check_completeness_union_only/_collapsed,
    this one has NO other input that could make it fail — that unfalsifiability
    IS the defect being demonstrated, not a gap in this arm's own proof."""
    expected = _full_keys(script_doc)  # ⛔ should be census_doc
    observed = _full_keys(script_doc)
    missing = expected - observed
    unexpected = observed - expected
    assert not missing and not unexpected, (
        f"W-2a (WRONG, script-derived expected): missing={sorted(missing)} "
        f"unexpected={sorted(unexpected)}")


def _check_completeness_union_only(doc, census_doc):
    """⚠️ DELIBERATELY WEAKER than _check_w3a — exists ONLY as a spurious-hit
    arm operand (T091). Unions witness rows across ALL FOUR configs with no
    per-config split, so a config contributing ZERO keys leaves the union
    unchanged and this reports complete regardless."""
    expected = _full_keys(census_doc)
    union = set()
    for config in CONFIGS:
        union |= _project_identity2(_witness_rows_for(doc, config))
    missing = expected - union
    unexpected = union - expected
    assert not missing and not unexpected, (
        f"union-only completeness gate: missing={sorted(missing)} "
        f"unexpected={sorted(unexpected)}")


def _check_completeness_collapsed(doc, census_doc, config):
    """⚠️ DELIBERATELY WEAKER than _check_w3a — exists ONLY as a spurious-hit
    arm operand (T090). Projects onto (arm, script_step_id, direction,
    occurrence) for ONE config, dropping combo_id."""
    expected = _full_keys_collapsed(census_doc)
    observed = _project_collapsed(_witness_rows_for(doc, config))
    missing = expected - observed
    unexpected = observed - expected
    assert not missing and not unexpected, (
        f"collapsed completeness gate ({config!r}): missing={sorted(missing)} "
        f"unexpected={sorted(unexpected)}")


def _check_w3a(doc, census_doc):
    """W-3a (also T081/T083's operand): π(kind:conformance authoritative
    rows of config c) = π(same, config c') for every ordered pair of the
    four configs, AND each equals the census's 100 keys."""
    expected = _full_keys(census_doc)
    per_config = {config: _project_identity2(_witness_rows_for(doc, config))
                  for config in CONFIGS}
    # ⚠️ Pairwise agreement FIRST: with the census check ahead of it, every
    # config is already forced equal to `expected` individually, so the
    # pairwise loop could never fire (transitivity makes it unreachable).
    # Ordered this way, two IDENTICAL-but-wrong projections (e.g. every
    # config agreeing on a set that omits a step) still trip the census leg
    # below, and two DISAGREEING configs are caught here, by the clause
    # actually meant to catch them.
    ordered = sorted(per_config)
    for i, a in enumerate(ordered):
        for b in ordered[i + 1:]:
            assert per_config[a] == per_config[b], (
                f"W-3a: config {a!r} projection != config {b!r} projection")
    for config, keys in per_config.items():
        missing = expected - keys
        unexpected = keys - expected
        assert not missing and not unexpected, (
            f"W-3a: config {config!r} witness projection != census; "
            f"missing={sorted(missing)} unexpected={sorted(unexpected)}")


def _check_run_slot_completeness(doc):
    """E-1c ∧ W-3b ∧ W-3c — ONE predicate, stated three times in the
    contract (the run-ledger obligation, and the W-* table's own restatement
    split into an equality half and a one-per-slot half): exactly one
    kind:conformance run with authoritative:true per (cell_id, config), and
    that slot set equals the 32-slot inventory exactly — not merely is
    contained in it."""
    slot_counts = {}
    for r in doc.get("runs", []):
        if r.get("kind") != "conformance" or r.get("authoritative") is not True:
            continue
        key = (r.get("cell_id"), r.get("config"))
        slot_counts[key] = slot_counts.get(key, 0) + 1
    duplicated = {k for k, c in slot_counts.items() if c > 1}
    assert not duplicated, (
        f"E-1c/W-3c: {duplicated!r} claimed by more than one authoritative "
        f"kind:conformance run")
    observed = set(slot_counts)
    missing = THIRTY_TWO_SLOT_INVENTORY - observed
    unexpected = observed - THIRTY_TWO_SLOT_INVENTORY
    assert not missing and not unexpected, (
        f"E-1c/W-3b: runs: kind:conformance,authoritative:true slot set != "
        f"the 32-slot inventory; missing={missing!r} unexpected={unexpected!r}")


def _check_e1a(conv_rows):
    """E-1a: manifest row identity is (cell_id, config) — 32 rows exactly (8
    logical cells x 4 configs), retries never committed. Scoped to
    kind:conversation rows; happy/thorny/parity rows are untouched."""
    keys = [(r.get("cell_id"), r.get("config")) for r in conv_rows]
    dup = {k for k in keys if keys.count(k) > 1}
    assert not dup, f"E-1a: duplicate (cell_id, config) manifest rows: {dup!r}"
    observed = set(keys)
    missing = THIRTY_TWO_SLOT_INVENTORY - observed
    unexpected = observed - THIRTY_TWO_SLOT_INVENTORY
    assert not missing and not unexpected, (
        f"E-1a: kind:conversation manifest rows != 32-slot inventory; "
        f"missing={missing!r} unexpected={unexpected!r}")


def _check_e4(conv_rows):
    """E-4: each of the 4 configs emits its OWN row; folding one into
    another is a violation. ⚠️ Falls out of E-1a's (cell_id, config) set
    equality — restated with its OWN diagnostic (rule 2 of quickstart.md §
    Step 4) rather than relying on E-1a's message to discharge a DIFFERENT
    obligation ID."""
    by_cell = {}
    for r in conv_rows:
        by_cell.setdefault(r.get("cell_id"), set()).add(r.get("config"))
    for cell_id, configs in by_cell.items():
        missing = CONFIGS - configs
        assert not missing, (
            f"E-4: cell {cell_id!r} is missing a row for config(s) {missing!r} "
            f"-- folded into another config's row?")


def _ledger_slot_index(doc):
    idx = {}
    for r in doc.get("runs", []):
        if r.get("kind") == "conformance" and r.get("authoritative") is True:
            idx[(r.get("cell_id"), r.get("config"))] = r
    return idx


def _census_slot_witness_count(census_doc, combo_id):
    if combo_id not in CONV_COMBO_IDS:
        # fail CLOSED on an unrecognised combo rather than silently
        # returning the empty-set count (0) that _expand_business_steps_combo
        # gives for a combo_id it has never heard of -- a bogus cell_id
        # combined with a ledger witness_count of 0 must not satisfy E-1.
        return None
    return len(_expand_business_steps_combo(census_doc, combo_id))


# gate-b fix round, FQ-2 (Codex #3): the manifest row's OWN provenance
# fields, distinct from `ledger_ref` (which only names a SLOT). A row could
# previously carry a FORGED run_id/script_digest/counterparty_* while still
# resolving to the correct (cell_id, config) slot -- reproduced on the real
# committed pair (all five forged across all 32 rows, gates stayed green).
MANIFEST_LEDGER_JOIN_FIELDS = (
    "run_id", "script_digest", "counterparty_flavour", "counterparty_version",
    "counterparty_digest",
)


def _check_e1_and_e5(conv_rows, doc, census_doc):
    """E-1: status:pass requires a runs: ledger entry (terminal_state:
    completed, witness_count == the census figure for that slot) — the
    check OPENS NOTHING, both operands are already in the committed file.
    E-5: an infrastructure abort (ledger terminal_state aborted /
    error:enospc) MUST be recorded with that EXACT status token — never
    pass, skip, n/a or fail."""
    ledger_idx = _ledger_slot_index(doc)
    for row in conv_rows:
        cell_id, config = row.get("cell_id"), row.get("config")
        slot = (cell_id, config)
        status = row.get("status", "")
        sk = status.split(":", 1)[0]
        entry = ledger_idx.get(slot)
        if sk == "pass":
            # data-model §5: `ledger_ref` is the row's OWN pointer to its
            # ledger entry — a row whose ledger_ref names a DIFFERENT slot
            # than its own (cell_id, config) identity must not silently
            # resolve against ITS identity's entry instead.
            ledger_ref = row.get("ledger_ref")
            assert ledger_ref is not None and tuple(ledger_ref) == slot, (
                f"E-1: {row.get('id')!r} ledger_ref {ledger_ref!r} != this "
                f"row's own (cell_id, config) {slot!r}")
            assert entry is not None, (
                f"E-1: {row.get('id')!r} status:pass names no runs: ledger "
                f"entry for slot {slot!r}")
            assert entry.get("terminal_state") == "completed", (
                f"E-1: {row.get('id')!r} status:pass but ledger entry "
                f"terminal_state {entry.get('terminal_state')!r} != completed")
            combo_id = cell_id.split("-")[1] if cell_id else None
            expected_count = _census_slot_witness_count(census_doc, combo_id)
            assert expected_count is not None, (
                f"E-1: {row.get('id')!r} cell_id {cell_id!r} names an "
                f"unrecognised combo {combo_id!r}")
            assert entry.get("witness_count") == expected_count, (
                f"E-1: {row.get('id')!r} status:pass but ledger witness_count "
                f"{entry.get('witness_count')!r} != census figure "
                f"{expected_count!r} for slot {slot!r}")
            # gate-b fix round, FQ-2: the manifest row's OWN provenance
            # fields must agree with the resolved ledger entry FIELD-FOR-
            # FIELD -- previously only ledger_ref's (cell_id, config) tuple
            # was checked, so a row could carry a forged run_id/
            # script_digest/counterparty_* while still resolving to a
            # correct slot (reproduced on the committed pair: all five
            # forged across all 32 rows, every other gate stayed green).
            for field in MANIFEST_LEDGER_JOIN_FIELDS:
                assert row.get(field) == entry.get(field), (
                    f"E-1: {row.get('id')!r} manifest field {field!r} "
                    f"{row.get(field)!r} != ledger entry {slot!r}'s {field!r} "
                    f"{entry.get(field)!r}")
        elif entry is not None and entry.get("terminal_state") != "completed":
            assert status == entry.get("terminal_state"), (
                f"E-5: {row.get('id')!r} status {status!r} disagrees with "
                f"ledger terminal_state {entry.get('terminal_state')!r} -- an "
                f"infrastructure abort must be recorded with that EXACT "
                f"token, never fail/skip/n/a")


def _w_fixture_complete(census_doc):
    """The full complete artifact: runs: (32 conformance + 2 control, from
    _e7_fixture_complete), witnesses: (all 400 census-derived rows, 100 per
    config x 4 configs), validation_pairs: (16 conformance + 1 control).
    Every negative fixture below starts from a (deep) copy of this and
    perturbs exactly the one thing its own arm names."""
    import copy
    doc = _e7_fixture_complete()
    witnesses = []
    for combo_id in CONV_COMBO_IDS:
        for config in CONFIGS:
            for arm, suffix in (("validation-off", "off"), ("validation-on", "on")):
                cell_id = f"CONV-{combo_id}-{suffix}"
                run_id = f"{cell_id}-{config}-{arm}-run"
                for step_id, direction, occ in _expand_business_steps_combo(census_doc, combo_id):
                    witnesses.append({
                        "combo_id": combo_id, "cell_id": cell_id, "config": config,
                        "run_id": run_id, "arm": arm, "kind": "conformance",
                        "authoritative": True, "script_step_id": step_id,
                        "direction": direction, "occurrence": occ,
                        "verdict": "pass", "mismatch": [],
                    })
    doc["witnesses"] = witnesses
    return copy.deepcopy(doc)


def _e1a_fixture_complete():
    return [
        {"id": f"{cell}@{cfg}", "cell_id": cell, "config": cfg, "kind": "conversation",
         "status": "pass", "matrix_disposition": "live", "spec_ref": "FR-013"}
        for cell in EIGHT_LOGICAL_CELL_IDS for cfg in CONFIGS
    ]


@pytest.fixture(scope="module")
def census_doc():
    return _load_census()


@pytest.fixture(scope="module")
def script_doc():
    return _load_script()


def test_w1_required_fields_present():
    doc = _w_fixture_complete(_load_census())
    _check_w1(doc)


def test_w1_missing_field_goes_red():
    doc = _w_fixture_complete(_load_census())
    del doc["witnesses"][0]["authoritative"]
    with pytest.raises(AssertionError, match="missing field"):
        _check_w1(doc)


def test_w2a_real_census_matches_real_script(census_doc, script_doc):
    # SC-009a's end-to-end demonstration against the REAL committed
    # artifacts (not a constructed fixture — both files are already fully
    # authored content).
    _check_w2a(census_doc, script_doc)


def test_w3a_complete_artifact_passes(census_doc):
    _check_w3a(_w_fixture_complete(census_doc), census_doc)


def test_w3a_pairwise_clause_fires_before_the_census_clause(census_doc):
    # The pairwise loop is ordered BEFORE the census-equality loop
    # specifically so it is reachable: with census checked first, every
    # config that survives is already == census, so two surviving configs
    # are already equal to each other by transitivity and the pairwise loop
    # could never fail. Add ONE extra witness to exactly one config: that
    # config now disagrees with its siblings (pairwise, checked first) AND
    # with the census (checked second) -- assert the diagnostic that fires
    # is the PAIRWISE one, proving this clause is not dead code.
    doc = _w_fixture_complete(census_doc)
    extra = dict(doc["witnesses"][0])
    extra["config"] = "asan" if extra["config"] != "asan" else "ubsan"
    extra["occurrence"] = 999  # a key no config's real projection carries
    doc["witnesses"].append(extra)
    with pytest.raises(AssertionError, match=r"config '.*' projection != config '.*' projection"):
        _check_w3a(doc, census_doc)


def test_run_slot_completeness_complete_artifact_passes():
    _check_run_slot_completeness(_w_fixture_complete(_load_census()))


def test_e1a_complete_manifest_passes():
    _check_e1a(_e1a_fixture_complete())


def test_e4_complete_manifest_passes():
    _check_e4(_e1a_fixture_complete())


# ── T086: forced-miss arms on the schema check (E-1 / E-5) ─────────────────
#
# "a falsified cell_results.yaml row (pass, no evidence)" is the pre-existing
# test_conversation_row_missing_new_field_goes_red (T027, above) — a
# kind:conversation row missing `ledger_ref` (or any other new evidence
# field) is already REQUIRED_FIELDS-rejected before it ever reaches E-1.

def _e1_manifest_row(cell_id, config, status, ledger_ref=None):
    return {"id": f"{cell_id}@{config}", "cell_id": cell_id, "config": config,
            "kind": "conversation", "status": status, "matrix_disposition": "live",
            "spec_ref": "FR-013", "ledger_ref": ledger_ref or (cell_id, config)}


def test_e1_ledger_ref_naming_a_different_slot_goes_red():
    # data-model §5's own field, distinct from the "no ledger entry at all"
    # arm below: the ledger entry for THIS row's (cell_id, config) DOES
    # exist and is well-formed, but the row's ledger_ref points somewhere
    # else -- presence is not agreement.
    census = _load_census()
    expected = _census_slot_witness_count(census, "C1")
    doc = {"schema_version": 1, "witnesses": [], "runs": [
        {"cell_id": "CONV-C1-off", "config": "normal", "kind": "conformance",
         "authoritative": True, "terminal_state": "completed",
         "witness_count": expected},
    ], "validation_pairs": []}
    rows = [_e1_manifest_row("CONV-C1-off", "normal", "pass",
                              ledger_ref=("CONV-C1-on", "normal"))]
    with pytest.raises(AssertionError, match="ledger_ref"):
        _check_e1_and_e5(rows, doc, census)


def test_e1_pass_with_no_ledger_entry_goes_red():
    census = _load_census()
    doc = {"schema_version": 1, "witnesses": [], "runs": [], "validation_pairs": []}
    rows = [_e1_manifest_row("CONV-C1-off", "normal", "pass")]
    with pytest.raises(AssertionError, match="E-1"):
        _check_e1_and_e5(rows, doc, census)


def test_e1_pass_naming_nonexistent_ledger_entry_goes_red():
    census = _load_census()
    doc = {"schema_version": 1, "witnesses": [], "runs": [
        {"cell_id": "CONV-C1-off", "config": "OTHER-SLOT", "kind": "conformance",
         "authoritative": True, "terminal_state": "completed", "witness_count": 12},
    ], "validation_pairs": []}
    rows = [_e1_manifest_row("CONV-C1-off", "normal", "pass")]
    with pytest.raises(AssertionError, match="E-1"):
        _check_e1_and_e5(rows, doc, census)


def test_e1_witness_count_mismatch_goes_red():
    census = _load_census()
    expected = _census_slot_witness_count(census, "C1")
    doc = {"schema_version": 1, "witnesses": [], "runs": [
        {"cell_id": "CONV-C1-off", "config": "normal", "kind": "conformance",
         "authoritative": True, "terminal_state": "completed",
         "witness_count": expected + 1},
    ], "validation_pairs": []}
    rows = [_e1_manifest_row("CONV-C1-off", "normal", "pass")]
    with pytest.raises(AssertionError, match="E-1"):
        _check_e1_and_e5(rows, doc, census)


def test_e5_enospc_abort_recorded_as_fail_goes_red():
    census = _load_census()
    doc = {"schema_version": 1, "witnesses": [], "runs": [
        {"cell_id": "CONV-C1-off", "config": "normal", "kind": "conformance",
         "authoritative": True, "terminal_state": "error:enospc", "witness_count": 0},
    ], "validation_pairs": []}
    rows = [_e1_manifest_row("CONV-C1-off", "normal", "fail")]
    with pytest.raises(AssertionError, match="E-5"):
        _check_e1_and_e5(rows, doc, census)


def test_e5_enospc_abort_recorded_correctly_passes():
    census = _load_census()
    doc = {"schema_version": 1, "witnesses": [], "runs": [
        {"cell_id": "CONV-C1-off", "config": "normal", "kind": "conformance",
         "authoritative": True, "terminal_state": "error:enospc", "witness_count": 0},
    ], "validation_pairs": []}
    rows = [_e1_manifest_row("CONV-C1-off", "normal", "error:enospc")]
    _check_e1_and_e5(rows, doc, census)  # must not raise


def test_e1_pass_row_backed_by_a_real_ledger_entry_passes():
    census = _load_census()
    expected = _census_slot_witness_count(census, "C1")
    doc = {"schema_version": 1, "witnesses": [], "runs": [
        {"cell_id": "CONV-C1-off", "config": "normal", "kind": "conformance",
         "authoritative": True, "terminal_state": "completed",
         "witness_count": expected},
    ], "validation_pairs": []}
    rows = [_e1_manifest_row("CONV-C1-off", "normal", "pass")]
    _check_e1_and_e5(rows, doc, census)  # must not raise


def test_e1_bogus_combo_with_zero_witness_count_goes_red():
    # ⚠️ fail-CLOSED guard: an unrecognised combo_id must not resolve to the
    # empty-set expansion (count 0) and let a witness_count: 0 ledger entry
    # satisfy E-1 -- a bogus cell_id combined with zero witnesses would
    # otherwise pass silently.
    census = _load_census()
    doc = {"schema_version": 1, "witnesses": [], "runs": [
        {"cell_id": "CONV-C99-off", "config": "normal", "kind": "conformance",
         "authoritative": True, "terminal_state": "completed", "witness_count": 0},
    ], "validation_pairs": []}
    rows = [_e1_manifest_row("CONV-C99-off", "normal", "pass")]
    with pytest.raises(AssertionError, match="unrecognised combo"):
        _check_e1_and_e5(rows, doc, census)


# ── T087: forced-miss arms on the completeness gate (W-3/W-2a) ─────────────

def test_w3a_delete_one_witness_goes_red(census_doc):
    doc = _w_fixture_complete(census_doc)
    del doc["witnesses"][0]
    with pytest.raises(AssertionError, match="W-3a"):
        _check_w3a(doc, census_doc)


def test_w2a_delete_a_business_step_from_script_goes_red(census_doc, script_doc):
    import copy
    mutant = copy.deepcopy(script_doc)
    mutant["business_steps"] = [s for s in mutant["business_steps"] if s["step_id"] != "B-01"]
    with pytest.raises(AssertionError, match="W-2a"):
        _check_w2a(census_doc, mutant)


def test_w2a_add_a_script_step_with_no_witness_goes_red(census_doc, script_doc):
    import copy
    mutant = copy.deepcopy(script_doc)
    extra = copy.deepcopy(mutant["business_steps"][0])
    extra["step_id"] = "B-EXTRA-NOT-IN-CENSUS"
    mutant["business_steps"].append(extra)
    with pytest.raises(AssertionError, match="W-2a"):
        _check_w2a(census_doc, mutant)


def test_w2a_self_referential_check_stays_green_on_both_mutants_above(census_doc, script_doc):
    # T087's third arm: the WRONG implementation (the `expected` side
    # re-derived from the SCRIPT instead of the CENSUS, same call shape as
    # the real _check_w2a) stays green on the exact two mutations that
    # redden the real _check_w2a above — proving the census operand is
    # load-bearing, not decorative. ⚠️ It cannot go RED on ANY script
    # mutation (its `expected`/`observed` sides are the same document) — that
    # unfalsifiability is what this arm demonstrates, not a gap in it.
    import copy
    deleted = copy.deepcopy(script_doc)
    deleted["business_steps"] = [s for s in deleted["business_steps"] if s["step_id"] != "B-01"]
    _check_w2a_script_derived_expected_WRONG(census_doc, deleted)  # must not raise

    added = copy.deepcopy(script_doc)
    extra = copy.deepcopy(added["business_steps"][0])
    extra["step_id"] = "B-EXTRA-NOT-IN-CENSUS"
    added["business_steps"].append(extra)
    _check_w2a_script_derived_expected_WRONG(census_doc, added)  # must not raise


# ── T088: a slot with no authoritative run ⇒ W-3b RED (equality, not
# containment) ──────────────────────────────────────────────────────────────

def test_w3b_slot_with_no_authoritative_run_goes_red():
    doc = _w_fixture_complete(_load_census())
    doc["runs"] = [r for r in doc["runs"]
                   if not (r["cell_id"] == "CONV-C1-off" and r["config"] == "normal"
                           and r["kind"] == "conformance")]
    with pytest.raises(AssertionError, match="E-1c/W-3b"):
        _check_run_slot_completeness(doc)


# ── T090: spurious-hit — one combo (both arms) of one config, none of the
# other three combos of THAT config; the other three configs stay complete.
# The COLLAPSED (arm-only) gate reports complete because C3's per-arm key
# set is the union-maximal one across all four combos; the correct
# identity-2 gate (W-3a) still sees the other three combos missing. ────────

def test_w3a_one_combo_of_one_config_is_red_but_collapsed_gate_is_a_spurious_green(census_doc):
    doc = _w_fixture_complete(census_doc)
    doc["witnesses"] = [
        w for w in doc["witnesses"]
        if not (w["config"] == "normal" and w["combo_id"] != "C3")
    ]
    with pytest.raises(AssertionError, match="W-3a"):
        _check_w3a(doc, census_doc)
    _check_completeness_collapsed(doc, census_doc, "normal")  # spurious GREEN


# ── T091: spurious-hit — drop one whole configuration; the union-only gate
# cannot see it (the other three configs' union already equals the census),
# W-3a (per-config) reddens on that config specifically. ───────────────────

def test_w3a_dropped_config_is_red_but_union_only_gate_is_a_spurious_green(census_doc):
    doc = _w_fixture_complete(census_doc)
    doc["witnesses"] = [w for w in doc["witnesses"] if w["config"] != "tsan"]
    with pytest.raises(AssertionError, match="W-3a"):
        _check_w3a(doc, census_doc)
    _check_completeness_union_only(doc, census_doc)  # spurious GREEN


# ── T092: spurious-hit — a second (retry) run for one slot; π drops run_id
# so the duplicate rows collapse and the completeness gate alone sees
# nothing; W-3c (via _check_run_slot_completeness) is what catches it. ─────

def test_w3c_retry_run_is_red_but_completeness_gate_alone_is_a_spurious_green(census_doc):
    doc = _w_fixture_complete(census_doc)
    original_run = next(r for r in doc["runs"]
                         if r["cell_id"] == "CONV-C1-off" and r["config"] == "normal"
                         and r["kind"] == "conformance")
    retry_run = dict(original_run)
    retry_run["run_id"] = original_run["run_id"] + "-retry"
    doc["runs"].append(retry_run)  # BUG: still authoritative:true, never demoted
    retry_witnesses = [dict(w, run_id=retry_run["run_id"])
                        for w in doc["witnesses"]
                        if w["cell_id"] == "CONV-C1-off" and w["config"] == "normal"]
    doc["witnesses"].extend(retry_witnesses)  # same keys, different run_id
    with pytest.raises(AssertionError, match="E-1c/W-3c"):
        _check_run_slot_completeness(doc)
    _check_w3a(doc, census_doc)  # spurious GREEN — the duplicate rows collapse


# ── T094: controls that must stay GREEN ─────────────────────────────────────

def test_control_run_on_an_occupied_slot_and_w3a_both_stay_green(census_doc):
    # _w_fixture_complete already carries a validator-positive-control run
    # occupying CONV-C1-off/on @ normal, the same slot a conformance run
    # already claims — plus a control WITNESS row on that same cell_id, so
    # W-3a's kind:conformance filter is exercised, not merely absent.
    doc = _w_fixture_complete(census_doc)
    doc["witnesses"].append({
        "combo_id": "C1", "cell_id": "CONV-C1-off", "config": "normal",
        "run_id": "ctrl-off-run", "arm": "validation-off",
        "kind": "validator-positive-control", "authoritative": True,
        "script_step_id": "PROBE-01", "direction": "fixpp-to-peer", "occurrence": 0,
    })
    _check_run_slot_completeness(doc)  # GREEN — the slot is claimed once
    _check_w3a(doc, census_doc)  # GREEN — π excludes the control row by kind


def test_required_fields_present(cells):
    for c in cells:
        missing = REQUIRED_FIELDS - c.keys()
        assert not missing, f"cell {c.get('id')!r} missing required fields {missing}"
        if c["kind"] == "conversation":
            missing_conv = CONVERSATION_REQUIRED_FIELDS - c.keys()
            assert not missing_conv, (
                f"cell {c.get('id')!r} kind:conversation missing new evidence "
                f"fields {missing_conv} (data-model.md §5, FR-013/FR-013a)"
            )


def test_t100_preexisting_pass_rows_stay_green_and_population_is_nonempty(cells):
    # 089 T100: a CONTROL (quickstart.md § Controls, not § Forced-miss arms --
    # it was filed there while its own expectation called it a control). The
    # pre-existing `pass` rows already committed in cell_results.yaml, none of
    # which has an 089 run behind it, must keep the schema check GREEN under
    # T027's conditional-field rule. Ranging over an EMPTY population would
    # satisfy the loop below trivially, so the population is re-derived from
    # the real committed fixture at run time (never hardcoded) and asserted
    # non-empty before the loop is trusted.
    preexisting_pass = [c for c in cells if c["kind"] != "conversation" and c["status"] == "pass"]
    assert preexisting_pass, (
        "no pre-existing non-conversation pass row found in the real manifest "
        "-- this control covers nothing"
    )
    for c in preexisting_pass:
        missing = REQUIRED_FIELDS - c.keys()
        assert not missing, f"cell {c.get('id')!r} missing required fields {missing}"


def test_conversation_row_missing_new_field_goes_red():
    row = {
        "id": "X@normal", "config": "normal", "kind": "conversation",
        "status": "pass", "matrix_disposition": "live", "spec_ref": "FR-013",
        "cell_id": "X", "run_id": "r1", "run_timestamp": "2026-09-11T00:00:00Z",
        "script_digest": "deadbeef", "counterparty_flavour": "quickfix-cpp",
        "counterparty_version": "1.0", "counterparty_digest": "sha256:abc",
        # ledger_ref deliberately omitted
    }
    with pytest.raises(AssertionError, match="ledger_ref"):
        test_required_fields_present([row])


def test_conversation_row_with_all_fields_present_passes():
    row = {
        "id": "X@normal", "config": "normal", "kind": "conversation",
        "status": "pass", "matrix_disposition": "live", "spec_ref": "FR-013",
        "cell_id": "X", "run_id": "r1", "run_timestamp": "2026-09-11T00:00:00Z",
        "script_digest": "deadbeef", "counterparty_flavour": "quickfix-cpp",
        "counterparty_version": "1.0", "counterparty_digest": "sha256:abc",
        "ledger_ref": ("X", "normal"),
    }
    test_required_fields_present([row])


def test_ids_unique(cells):
    # T029 (089-quickfix-interop-conversation): verified NOT to need replacing.
    # E-1a (contracts/witness-evidence.md) / data-model.md §5 "Validation
    # rules": manifest row identity is (cell_id, config) — 32
    # rows, retries never committed — and the shipped `id` field is RETAINED,
    # derived as "<cell_id>@<config>", so `id` stays unique over exactly those
    # rows. The manifest/ledger split (witness_evidence.yaml's `runs:` ledger
    # carries the per-run identity) removes the earlier round-1 assumption
    # that 8 ids had to serve 32 rows, which is why this check is left as-is.
    ids = [c["id"] for c in cells]
    dupes = {i for i in ids if ids.count(i) > 1}
    assert not dupes, f"duplicate cell ids (a dropped/duplicated cell): {dupes}"


def test_enum_fields_valid(cells):
    for c in cells:
        assert c["kind"] in KINDS, f"{c['id']}: bad kind {c['kind']!r}"
        assert c["config"] in CONFIGS, f"{c['id']}: bad config {c['config']!r}"
        sk = _status_kind(c["status"])
        assert sk in STATUS_KINDS, \
            f"{c['id']}: bad status {c['status']!r}"


def test_status_error_and_aborted_accepted():
    # FR-014a: "A run killed by ENOSPC is recorded as error:enospc (with
    # aborted as the general class), never as pass, skip, n/a or fail."
    for status in ("error:enospc", "aborted"):
        row = {"id": "x", "config": "normal", "kind": "conversation", "status": status}
        test_enum_fields_valid([row])  # must not raise


def test_status_outside_closed_set_goes_red():
    row = {"id": "x", "config": "normal", "kind": "conversation", "status": "bogus"}
    with pytest.raises(AssertionError, match="bad status"):
        test_enum_fields_valid([row])


def test_deferred_iff_status_na(cells):
    # The schema's core invariant: status n/a  <=>  matrix_disposition deferred:*.
    for c in cells:
        is_na = c["status"] == "n/a"
        is_deferred = str(c["matrix_disposition"]).startswith("deferred:")
        assert is_na == is_deferred, (
            f"{c['id']}: status n/a ({is_na}) must match deferred:* disposition "
            f"({is_deferred}) — a deferred cell carries status n/a and vice-versa"
        )
        if is_deferred:
            assert c["matrix_disposition"] in DEFERRED_TAGS, \
                f"{c['id']}: unknown deferred tag {c['matrix_disposition']!r}"
            assert c.get("deferred_reason"), \
                f"{c['id']}: deferred:* row MUST carry a deferred_reason"


def test_skip_only_on_live_cells(cells):
    # skip:<reason> is strictly FR-023 counterparty-unavailable on a LIVE cell —
    # never a way to express by-design deferral (that is matrix_disposition).
    for c in cells:
        if _status_kind(c["status"]) == "skip":
            assert c["matrix_disposition"] == "live", (
                f"{c['id']}: skip:* is only valid on a live cell; by-design "
                f"deferral must use matrix_disposition deferred:* + status n/a"
            )
            assert ":" in c["status"] and c["status"].split(":", 1)[1], \
                f"{c['id']}: skip status MUST carry a reason (skip:<reason>)"


def test_known_limitation_has_tracking_issue(cells):
    for c in cells:
        if _status_kind(c["status"]) == "known-limitation":
            assert c.get("tracking_issue_state"), (
                f"{c['id']}: status known-limitation:* REQUIRES a "
                f"tracking_issue_state (the open tracking issue) — FR-014"
            )


def test_thorny_rows_have_priority(cells):
    for c in cells:
        if c["kind"] == "thorny":
            assert c.get("priority") in PRIORITIES, \
                f"{c['id']}: thorny corpus row MUST carry a valid priority (FR-012)"


def test_corpus_p1_block_rule(cells):
    # FR-014 / SC-002: every P1 / watch:P1 corpus row must be pass OR
    # known-limitation with an open tracking issue — nothing else is admissible.
    for c in cells:
        if c["kind"] == "thorny" and c.get("priority") in {"P1", "watch:P1"}:
            sk = _status_kind(c["status"])
            if sk == "known-limitation":
                assert c.get("tracking_issue_state", "").startswith("open"), \
                    f"{c['id']}: P1 known-limitation must cite an OPEN tracking issue"
            else:
                assert sk == "pass", (
                    f"{c['id']}: a P1/watch:P1 corpus row must be pass or "
                    f"known-limitation+open-issue, got {c['status']!r}"
                )


EXPECTED_IDS = frozenset({
    # US1 happy-path live matrix cells (18)
    "HP-QFcpp-init-fix44-logon-hb-logout",
    "HP-QFcpp-acc-fix44-logon-hb-logout",
    "HP-QFj-init-fix44-logon-hb-logout",
    "HP-QFj-acc-fix44-logon-hb-logout",
    "HP-QFcpp-init-fix44-testrequest-echo",
    "HP-QFcpp-acc-fix44-testrequest-echo",
    "HP-QFj-init-fix44-testrequest-echo",
    "HP-QFj-acc-fix44-testrequest-echo",
    "HP-QFcpp-init-fix44-reject-invalid-admin",
    "HP-QFcpp-acc-fix44-reject-invalid-admin",
    "HP-QFj-init-fix44-reject-invalid-admin",
    "HP-QFj-acc-fix44-reject-invalid-admin",
    "HP-QFcpp-init-fix44-seqnum-recovery",
    "HP-QFcpp-acc-fix44-seqnum-recovery",
    "HP-QFj-init-fix44-seqnum-recovery",
    "HP-QFj-acc-fix44-seqnum-recovery",
    "HP-QFcpp-init-fix44-disconnect-reconnect-noreset",
    "HP-QFj-init-fix44-disconnect-reconnect-noreset",
    # G1 (018-interop-live-admin) NEW admin cells (4) — recovery_outbound + idle_cadence
    # (the other three G1 groups reuse-and-enrich existing QFj ids above).
    "HP-QFj-init-fix44-recovery-outbound",
    "HP-QFj-acc-fix44-recovery-outbound",
    "HP-QFj-init-fix44-idle-cadence",
    "HP-QFj-acc-fix44-idle-cadence",
    # Regression cell (runs green locally)
    "HP-down-peer-stop-watchdog",
    # G2 (020) business-message NOS→ExecRpt live cells (4)
    "BM-QFcpp-init-fix44-nos-execrpt",
    "BM-QFcpp-acc-fix44-nos-execrpt",
    "BM-QFj-init-fix44-nos-execrpt",
    "BM-QFj-acc-fix44-nos-execrpt",
    # 033 FIXT.1.1 establishment live cells (8) — was the deferred:fixt-routing
    # placeholder HP-fixt11-fix50sp2-cells; retired 2026-06-12 (033 US3 SC-004/006).
    "HP-QFcpp-init-fixt11-fix50sp2-logon-hb-logout",
    "HP-QFcpp-acc-fixt11-fix50sp2-logon-hb-logout",
    "HP-QFcpp-init-fixt11-fix44-logon-hb-logout",
    "HP-QFcpp-acc-fixt11-fix44-logon-hb-logout",
    "HP-QFj-init-fixt11-fix50sp2-logon-hb-logout",
    "HP-QFj-acc-fixt11-fix50sp2-logon-hb-logout",
    "HP-QFj-init-fixt11-fix44-logon-hb-logout",
    "HP-QFj-acc-fixt11-fix44-logon-hb-logout",
    # Deferred rows (2)
    "HP-fix8-happy-cells",
    "HP-mutual-mtls-cells",
    # US2 thorny corpus P1 (7)
    "C-001-qfj646-resend-abort",
    "C-002-qfj658-750-788-reorder-queue",
    "C-003-qfcpp-inbound-sequencereset-arms",
    "C-004-qfj750-logout-seqnum-mismatch",
    "C-005-qfj271-sequencereset-large-gapfill",
    "C-006-qfj603-unsupported-beginstring",
    "C-007-qfj721-non-logon-first-message",
    # US2 thorny corpus P2/P3 (3)
    "C-101-qfj626-resend-recomputes-checksum",
    "C-102-qfj557-generatereject-advances-seqnum",
    "C-103-qfj751-resendrequest-chunk-size",
    # US3 parity GAP-closure witnesses (3)
    "PARITY-qfj646-resend-abort-on-failing-write",
    "PARITY-replay-subsumes-reorder-queue",
    "PARITY-inbound-sequencereset-arms",
    # G3 021-inbound-possdup-origsendingtime live PossDup cells (8)
    "PD-QFcpp-init-fix44-poss-dup-replay-survives",
    "PD-QFcpp-acc-fix44-poss-dup-replay-survives",
    "PD-QFj-init-fix44-poss-dup-replay-survives",
    "PD-QFj-acc-fix44-poss-dup-replay-survives",
    "PD-QFcpp-init-fix44-malformed-dup-rejected",
    "PD-QFcpp-acc-fix44-malformed-dup-rejected",
    "PD-QFj-init-fix44-malformed-dup-rejected",
    "PD-QFj-acc-fix44-malformed-dup-rejected",
    # G3 slice 2 022-possresend-allowpossdup-send live cells (8)
    "APDS-QFcpp-init-fix44-allow-pos-dup-strip-send",
    "APDS-QFcpp-acc-fix44-allow-pos-dup-strip-send",
    "APDS-QFj-init-fix44-allow-pos-dup-strip-send",
    "APDS-QFj-acc-fix44-allow-pos-dup-strip-send",
    "PR-QFcpp-init-fix44-poss-resend-deliver",
    "PR-QFcpp-acc-fix44-poss-resend-deliver",
    "PR-QFj-init-fix44-poss-resend-deliver",
    "PR-QFj-acc-fix44-poss-resend-deliver",
    # G3 slice 3 024-reset-refresh-on-logon ResetOnLogon interop cells (4)
    "RL-QFcpp-init-fix44-reset-on-logon",
    "RL-QFj-init-fix44-reset-on-logon",
    "RL-QFcpp-acc-fix44-reset-on-logon",
    "RL-QFj-acc-fix44-reset-on-logon",
    # 030 received-141 inbound-advance acceptor cell (T028 / SC-001 live close-out).
    "RR-QFcpp-acc-fix44-received-reset",
    "RR-QFj-acc-fix44-received-reset",
    # G3 live feature cells registered at Item-1 (2026-06-11): 026 nanos (4) /
    # 027+031 NextExpectedMsgSeqNum (4) / 028 validation-compat non-regression (8).
    "NST-QFcpp-init-fix44-nanos-sendingtime",
    "NST-QFcpp-acc-fix44-nanos-sendingtime",
    "NST-QFj-init-fix44-nanos-sendingtime",
    "NST-QFj-acc-fix44-nanos-sendingtime",
    "NE-QFcpp-init-fix44-next-expected",
    "NE-QFcpp-acc-fix44-next-expected",
    "NE-QFj-init-fix44-next-expected",
    "NE-QFj-acc-fix44-next-expected",
    # 024 ResetOnLogon: acc cells pass live; init cells deferred (initiator
    # 141=Y-echo outbound-rebase bug, L-024-2).
    "RL-QFcpp-acc-fix44-reset-on-logon",
    "RL-QFj-acc-fix44-reset-on-logon",
    "RL-QFcpp-init-fix44-reset-on-logon",
    "RL-QFj-init-fix44-reset-on-logon",
    "VC-QFcpp-init-fix44-check-compid",
    "VC-QFcpp-acc-fix44-check-compid",
    "VC-QFj-init-fix44-check-compid",
    "VC-QFj-acc-fix44-check-compid",
    "VC-QFcpp-init-fix44-validate-seqnums",
    "VC-QFcpp-acc-fix44-validate-seqnums",
    "VC-QFj-init-fix44-validate-seqnums",
    "VC-QFj-acc-fix44-validate-seqnums",
})


def test_per_cell_completeness_no_silent_absence(cells):
    # Assert the exact expected id set — a dropped OR surprise-added cell fails
    # with a clear diff (parent-harness-gate-contract.md:56 missing-row rule /
    # T028 claim in tasks.md:125).
    # 089 T095: EXPECTED_IDS is the NON-conversation inventory. The
    # kind:conversation rows are governed by _check_e1a's 32-slot
    # (cell_id, config) set equality instead -- a different population with a
    # different key (`<cell_id>@<config>`, 32 of them, accumulated one config
    # at a time), so folding them into this hand-kept id set would mean
    # restating that inventory in a second place and reddening this check for
    # every partially-complete sweep. Each keeps its OWN diagnostic.
    # ⚠️ Anti-vacuity: a filter that emptied this population would make the
    # set comparison below pass trivially, so the population is re-derived at
    # run time and asserted non-empty first (same shape as T100's control).
    non_conversation = [c for c in cells if c.get("kind") != "conversation"]
    assert non_conversation, (
        "the non-conversation population is EMPTY -- this check covers nothing; "
        "the kind filter is wrong, not the manifest"
    )
    present_ids = {c["id"] for c in non_conversation}
    missing = EXPECTED_IDS - present_ids
    unexpected = present_ids - EXPECTED_IDS
    assert not missing and not unexpected, (
        f"cell_results.yaml id set does not match the expected manifest; "
        f"missing={missing!r}, unexpected={unexpected!r}"
    )
    # Keep the deferred-axis sub-check: each deferred:* disposition must appear.
    present_tags = {c["matrix_disposition"] for c in cells
                    if str(c["matrix_disposition"]).startswith("deferred:")}
    assert present_tags == DEFERRED_TAGS, (
        f"every deferred axis must have a present row; missing "
        f"{DEFERRED_TAGS - present_tags}, unexpected {present_tags - DEFERRED_TAGS}"
    )


# T030 (089-quickfix-interop-conversation): "the check MUST NOT open any
# artifact path" (contracts/witness-evidence.md § "Two artifacts, and they
# must not be one" / plan.md's REQUIRED_FIELDS row). Both committed manifests
# — cell_results.yaml and witness_evidence.yaml — are IN the allow-list of
# _artifact_path_guard(); a run artifact under $FIXPP_INTEROP_EVIDENCE_ROOT,
# or anything else, is not.
def _discover_cell_checks():
    # T030 fix round, HOLE 2: derived by SHAPE — every module-level test_*
    # function whose parameters are exactly ["cells"] — not a hand-kept
    # tuple. A hand-kept list is blind to the next check added with that
    # same shape (an instrument keyed on an identifier is blind to copies).
    module = sys.modules[__name__]
    checks = [
        obj for name, obj in vars(module).items()
        if name.startswith("test_")
        and inspect.isfunction(obj)
        and list(inspect.signature(obj).parameters) == ["cells"]
    ]
    checks.sort(key=lambda fn: fn.__name__)
    return checks


def test_cell_checks_discovery_is_non_empty_and_finds_a_known_check():
    # An empty derivation must not pass vacuously (it would make
    # test_schema_check_opens_no_artifact_path below trivially green by
    # running nothing) — assert non-empty AND that a specific, known check
    # is present by IDENTITY.
    discovered = _discover_cell_checks()
    assert discovered, "shape-derived cell-check discovery found NOTHING"
    assert test_required_fields_present in discovered
    assert test_per_cell_completeness_no_silent_absence in discovered


def test_schema_check_opens_no_artifact_path():
    with _artifact_path_guard():
        cells_rows = _load_cells()
        for check in _discover_cell_checks():
            check(cells_rows)
        witness_doc = _load_witness_evidence()
        _check_witness_evidence_sections(witness_doc)
        # W-2a: unlike E-1a/E-1c/W-3a/W-3b/W-3c (which range over the
        # ACCUMULATED committed artifact in the T095a block, not this
        # per-config sweep), census.yaml and conversation_script.yaml are
        # already fully-authored content, so this gate is wired
        # unconditionally right here.
        _check_w2a(_load_census(), _load_script())


def test_artifact_path_guard_catches_planted_open(tmp_path):
    # T030 fix round, HOLE 1: builtins.open() call path.
    planted = tmp_path / "planted_open.jsonl"
    planted.write_text("not a committed manifest\n", encoding="utf-8")
    with pytest.raises(ArtifactPathOpened, match=re.escape(str(planted.resolve()))):
        with _artifact_path_guard():
            with open(planted, encoding="utf-8"):
                pass


def test_artifact_path_guard_catches_planted_path_read_text(tmp_path):
    # T030 fix round, HOLE 1: pathlib.Path.read_text() call path — this is
    # exactly the hole the coordinator's probe found (builtins.open-only
    # monkeypatch walked straight past it).
    planted = tmp_path / "planted_read_text.jsonl"
    planted.write_text("not a committed manifest\n", encoding="utf-8")
    with pytest.raises(ArtifactPathOpened, match=re.escape(str(planted.resolve()))):
        with _artifact_path_guard():
            planted.read_text(encoding="utf-8")


def test_artifact_path_guard_catches_planted_os_open(tmp_path):
    # T030 fix round, HOLE 1: os.open() call path.
    planted = tmp_path / "planted_os_open.jsonl"
    planted.write_text("not a committed manifest\n", encoding="utf-8")
    with pytest.raises(ArtifactPathOpened, match=re.escape(str(planted.resolve()))):
        with _artifact_path_guard():
            fd = os.open(str(planted), os.O_RDONLY)
            os.close(fd)


def test_artifact_path_guard_is_off_outside_guarded_block(tmp_path):
    # The audit hook is PERMANENT for the process once installed (no
    # removehook), so this proves it does not leak into ordinary test code:
    # a normal tmp_path write/read OUTSIDE any `with _artifact_path_guard():`
    # block must still succeed, via all three call paths.
    outside = tmp_path / "ordinary.txt"
    outside.write_text("fine\n", encoding="utf-8")
    assert outside.read_text(encoding="utf-8") == "fine\n"
    with open(outside, encoding="utf-8") as fh:
        assert fh.read() == "fine\n"
    fd = os.open(str(outside), os.O_RDONLY)
    os.close(fd)


# ===========================================================================
# 089 T095a — THE COMMITTED-ARTIFACT GATES
#
# E-1a, E-4, E-1c/W-3b/W-3c, W-3a and E-7a/E-7b/E-7c were each proven only
# against a CONSTRUCTED fixture (`_e7_fixture_complete()` and friends). A gate
# that never reads the committed artifact is not a gate — it cannot fail on
# anything a real sweep produces. T095's matrix populated both artifacts (32
# conformance runs + 8 control runs across 4 configs; 400 witness rows; 20
# validation pairs), so these now range over the REAL files, unconditionally.
#
# ⚠️ The parameter is `committed_cells`, NOT `cells`, and that is deliberate.
# `_discover_cell_checks()` selects every module-level `test_*` whose
# parameters are exactly ["cells"], and `emit_matrix.validate()` runs that set
# against ONE configuration's emission. A 32-slot gate with that signature
# would therefore redden every partial sweep (8 of 32 rows present after the
# first config). The split is the point: `validate()` checks a per-config
# emission, these check the accumulated committed artifact.
#
# Each gate is paired with a forced-miss arm that removes exactly ONE row from
# a deep copy of the real artifact and asserts the gate's OWN diagnostic —
# never a bare "it raised" (rule 2 of quickstart.md § Step 4).
# ===========================================================================


@pytest.fixture(scope="module")
def committed_cells():
    return _load_cells()


def _committed_conversation_rows(cells_rows):
    rows = [c for c in cells_rows if c.get("kind") == "conversation"]
    # Anti-vacuity: every gate below is a set comparison, and an EMPTY
    # population satisfies most of them trivially. Fail loudly here instead.
    assert rows, "the committed manifest carries NO kind:conversation rows -- these gates cover nothing"
    return rows


def test_t095a_e1a_over_the_committed_manifest(committed_cells):
    _check_e1a(_committed_conversation_rows(committed_cells))


def test_t095a_e1a_goes_red_with_one_committed_row_removed(committed_cells):
    rows = copy.deepcopy(_committed_conversation_rows(committed_cells))
    rows.pop()
    with pytest.raises(AssertionError, match="32-slot inventory"):
        _check_e1a(rows)


def test_t095a_e4_over_the_committed_manifest(committed_cells):
    _check_e4(_committed_conversation_rows(committed_cells))


def test_t095a_e4_goes_red_when_one_config_row_is_dropped(committed_cells):
    rows = copy.deepcopy(_committed_conversation_rows(committed_cells))
    victim = next(r for r in rows if r.get("config") == "tsan")
    rows.remove(victim)
    with pytest.raises(AssertionError, match="folded into another config"):
        _check_e4(rows)


def test_t095a_e1_and_e5_over_the_committed_ledger(committed_cells, witness_evidence_doc, census_doc):
    # E-1/E-5 were previously proven only against synthetic single-row docs
    # (T086 above) -- never against the REAL committed manifest+ledger pair,
    # so the manifest-field-agreement check added below (FQ-2) would
    # otherwise gate nothing on the artifact it exists to protect.
    _check_e1_and_e5(_committed_conversation_rows(committed_cells), witness_evidence_doc, census_doc)


def test_t095a_manifest_ledger_join_goes_red_on_forged_provenance_field(
        committed_cells, witness_evidence_doc, census_doc):
    # RED proof (gate-b fix round, FQ-2, Codex #3 arm 2): forging any ONE of
    # the five provenance fields on a real manifest row, while its
    # ledger_ref still names the correct slot, must redden -- reproduced
    # GREEN on the unfixed tree (all five forged across all 32 rows at
    # once, 71/71 still passed).
    for field, forged in (
        ("run_id", "FORGED-run"),
        ("script_digest", "0" * 64),
        ("counterparty_flavour", "FORGED"),
        ("counterparty_version", "9.9.9"),
        ("counterparty_digest", "sha256:" + "f" * 64),
    ):
        rows = copy.deepcopy(_committed_conversation_rows(committed_cells))
        rows[0][field] = forged
        with pytest.raises(AssertionError, match=re.escape(f"manifest field {field!r}")):
            _check_e1_and_e5(rows, witness_evidence_doc, census_doc)


def _runs_by_id(doc):
    return {r["run_id"]: r for r in doc.get("runs", [])}


WITNESS_RUN_JOIN_IDENTITY_FIELDS = ("cell_id", "config", "combo_id", "arm", "kind", "authoritative")


def _check_witness_run_join(doc):
    """gate-b fix round, FQ-2 (Codex #3): a full relational join between
    witnesses: and runs:, not merely a shape/count coincidence. Every
    witness's run_id must resolve to EXACTLY one runs: entry, and that
    entry's own identity fields must agree with the witness's -- a witness
    naming a run that EXISTS but belongs to a DIFFERENT slot must not
    silently resolve against it. The witness_count on each run is then
    recomputed FROM THE WITNESS LIST and required to equal the ledger's own
    figure -- previously that count was checked only against the census
    figure, so a ledger entry could claim a witness_count consistent with
    the census while ZERO witnesses actually named it, and nothing would
    notice (reproduced on the committed pair: every witness run_id rewritten
    to NONEXISTENT, 71/71 still passed).
    ⚠️ `evidence_relpath`/`evidence_digest` are DELIBERATELY excluded from
    the identity fields above: they name an artifact in the gitignored
    parent phase-9-harness tree this submodule's CI does not check out (the
    same operand problem that keeps emit_fixpp_fixture.cpp out of CTest), so
    a gate over them would pass by absence of an operand rather than by
    agreement -- do not add one."""
    runs_by_id = _runs_by_id(doc)
    witness_counts = {}
    for w in doc.get("witnesses", []):
        run_id = w.get("run_id")
        witness_counts[run_id] = witness_counts.get(run_id, 0) + 1
        entry = runs_by_id.get(run_id)
        assert entry is not None, (
            f"E-1/join: witness {w.get('witness_id')!r} names run_id {run_id!r} "
            f"which does not exist in runs:")
        for field in WITNESS_RUN_JOIN_IDENTITY_FIELDS:
            assert w.get(field) == entry.get(field), (
                f"E-1/join: witness {w.get('witness_id')!r} field {field!r} "
                f"{w.get(field)!r} != resolved run {run_id!r}'s {field!r} "
                f"{entry.get(field)!r}")
    for run_id, entry in runs_by_id.items():
        recomputed = witness_counts.get(run_id, 0)
        assert recomputed == entry.get("witness_count"), (
            f"E-1/join: run {run_id!r} witness_count {entry.get('witness_count')!r} "
            f"!= {recomputed} witnesses actually naming it")


def test_t095a_witness_run_join_over_the_committed_ledger(witness_evidence_doc):
    _check_witness_run_join(witness_evidence_doc)


def test_t095a_witness_run_join_goes_red_on_unresolved_run_id(witness_evidence_doc):
    # RED proof arm 1: one witness run_id -> NONEXISTENT -- reproduced GREEN
    # on the unfixed tree (all 400 rewritten, 71/71 passed).
    doc = copy.deepcopy(witness_evidence_doc)
    doc["witnesses"][0]["run_id"] = "NONEXISTENT"
    with pytest.raises(AssertionError, match="does not exist in runs"):
        _check_witness_run_join(doc)


def test_t095a_witness_run_join_goes_red_on_spurious_hit_cross_slot(witness_evidence_doc):
    # RED proof arm 3 (the spurious-hit arm): forge a witness's run_id to a
    # run that EXISTS but belongs to a DIFFERENT slot, keeping the witness's
    # own cell_id/config correct -- must redden on the cross-field
    # disagreement, not silently resolve.
    doc = copy.deepcopy(witness_evidence_doc)
    victim = doc["witnesses"][0]
    other_run = next(r for r in doc["runs"]
                      if (r["cell_id"], r["config"]) != (victim["cell_id"], victim["config"]))
    victim["run_id"] = other_run["run_id"]
    with pytest.raises(AssertionError, match="!= resolved run"):
        _check_witness_run_join(doc)


def test_t095a_witness_run_join_goes_red_when_all_witnesses_for_a_run_are_deleted(witness_evidence_doc):
    # RED proof arm 4 (anti-vacuity): delete every witness naming one
    # run_id while leaving that run's witness_count intact -- must redden
    # on the RECOMPUTED count, not silently agree with the census-derived
    # figure alone.
    doc = copy.deepcopy(witness_evidence_doc)
    victim_run_id = doc["witnesses"][0]["run_id"]
    doc["witnesses"] = [w for w in doc["witnesses"] if w["run_id"] != victim_run_id]
    with pytest.raises(AssertionError, match="witness_count"):
        _check_witness_run_join(doc)


def test_t095a_w0_content_over_the_committed_ledger(witness_evidence_doc, census_doc):
    _check_w0_content(witness_evidence_doc, census_doc)


def test_t095a_script_digest_binding_over_the_committed_artifacts(
        witness_evidence_doc, committed_cells):
    # FR-008c (gate-b round 2): the recorded script_digest must equal the
    # script ON DISK, for every run and every conversation manifest row.
    _check_script_digest_binding(witness_evidence_doc, committed_cells)


def test_t095a_script_digest_binding_goes_red_on_a_drifted_script(
        witness_evidence_doc, committed_cells, tmp_path):
    # RED arm: simulate the exact drift that occurred -- the script file's
    # bytes change (a comment edit is enough) while the recorded digest stays.
    # Mutating the real file would race other tests, so redirect the constant
    # at a byte-modified copy and assert the gate names the drift.
    import cell_results_schema_check_test as mod
    original = mod.SCRIPT_BY_RUN_KIND["conformance"]
    drifted = tmp_path / "conversation_script_drifted.yaml"
    with open(original, "rb") as fh:
        drifted.write_bytes(fh.read() + b"\n# one added comment byte-changes the digest\n")
    mod.SCRIPT_BY_RUN_KIND["conformance"] = str(drifted)
    try:
        with pytest.raises(AssertionError, match="script-digest binding"):
            _check_script_digest_binding(witness_evidence_doc, committed_cells)
    finally:
        mod.SCRIPT_BY_RUN_KIND["conformance"] = original


def test_t095a_script_digest_binding_is_not_vacuous(witness_evidence_doc, committed_cells):
    # The gate must fail on an EMPTY run list rather than pass with nothing
    # checked -- the zero-equals-zero shape round 2 found one level up.
    empty = copy.deepcopy(witness_evidence_doc)
    empty["runs"] = []
    with pytest.raises(AssertionError, match="no runs to check"):
        _check_script_digest_binding(empty, committed_cells)


def test_t095a_w0_content_goes_red_on_a_failed_witness(witness_evidence_doc, census_doc):
    # RED proof arm 1 (gate-b fix round, FQ-1): flip ONE committed witness to
    # fail with a non-empty mismatch, leaving every count/projection key
    # untouched -- reproduced GREEN on the unfixed tree (all 400 flipped,
    # 71/71 still passed) before this gate existed.
    doc = copy.deepcopy(witness_evidence_doc)
    victim = doc["witnesses"][0]
    victim["verdict"] = "fail"
    victim["mismatch"] = [{"path": "44", "cls": "value_mismatch",
                            "sent_value": "190.5", "readback_value": "190.6"}]
    with pytest.raises(AssertionError, match=re.escape(victim["witness_id"])):
        _check_w0_content(doc, census_doc)


def test_t095a_w0_content_goes_red_on_missing_verdict_key(witness_evidence_doc, census_doc):
    # RED proof arm 2: delete the `verdict` key outright -- must redden on
    # the missing key, not pass by absence.
    doc = copy.deepcopy(witness_evidence_doc)
    del doc["witnesses"][0]["verdict"]
    with pytest.raises(AssertionError, match="missing 'verdict'"):
        _check_w0_content(doc, census_doc)


def test_t095a_w0_content_goes_red_on_non_closed_verdict(witness_evidence_doc, census_doc):
    # RED proof arm 3: a typo'd verdict token must redden on the closed set,
    # not silently compare unequal-to-"pass" alone (WITNESS_VERDICTS is
    # checked BEFORE the =="pass" comparison so this gets its own diagnostic).
    doc = copy.deepcopy(witness_evidence_doc)
    doc["witnesses"][0]["verdict"] = "passed"
    with pytest.raises(AssertionError, match="not in the closed set"):
        _check_w0_content(doc, census_doc)


def test_t095a_w0_content_goes_red_on_population_count_drift(witness_evidence_doc, census_doc):
    # RED proof arm 4 (the population arm): corrupt one row's `kind` with a
    # trailing space, leaving verdict:pass intact -- must redden on the
    # POPULATION COUNT (399 != 400), not silently pass with 399 rows checked.
    doc = copy.deepcopy(witness_evidence_doc)
    doc["witnesses"][0]["kind"] = "conformance "
    with pytest.raises(AssertionError, match=r"W-0: 399 .* expected 400"):
        _check_w0_content(doc, census_doc)


def test_t095a_run_slot_completeness_over_the_committed_ledger(witness_evidence_doc):
    _check_run_slot_completeness(witness_evidence_doc)


def test_t095a_run_slot_completeness_goes_red_with_one_run_removed(witness_evidence_doc):
    doc = copy.deepcopy(witness_evidence_doc)
    doc["runs"].remove(next(r for r in doc["runs"] if r.get("kind") == "conformance"))
    with pytest.raises(AssertionError, match="32-slot inventory"):
        _check_run_slot_completeness(doc)


def test_t095a_w3a_over_the_committed_ledger(witness_evidence_doc, census_doc):
    _check_w3a(witness_evidence_doc, census_doc)


def test_t095a_w3a_goes_red_with_one_witness_removed(witness_evidence_doc, census_doc):
    doc = copy.deepcopy(witness_evidence_doc)
    doc["witnesses"].pop(0)
    with pytest.raises(AssertionError, match="projection"):
        _check_w3a(doc, census_doc)


def test_t095a_e7a_over_the_committed_ledger(witness_evidence_doc):
    _check_e7a(witness_evidence_doc)


def test_t095a_e7a_goes_red_with_one_pair_removed(witness_evidence_doc):
    doc = copy.deepcopy(witness_evidence_doc)
    doc["validation_pairs"].remove(
        next(p for p in doc["validation_pairs"] if p.get("kind") == "conformance"))
    with pytest.raises(AssertionError, match="16-pair inventory"):
        _check_e7a(doc)


def test_t095a_e7b_over_the_committed_ledger(witness_evidence_doc):
    _check_e7b(witness_evidence_doc)


def test_t095a_e7b_goes_red_when_the_control_pairs_are_removed(witness_evidence_doc):
    # E-7b's whole point: 16 well-formed conformance pairs must NOT satisfy it.
    doc = copy.deepcopy(witness_evidence_doc)
    doc["validation_pairs"] = [p for p in doc["validation_pairs"]
                               if p.get("kind") == "conformance"]
    with pytest.raises(AssertionError, match="validator-positive-control"):
        _check_e7b(doc)


def test_t095a_e7c_over_the_committed_ledger(witness_evidence_doc):
    _check_e7c(witness_evidence_doc)


def test_t095a_e7c_goes_red_when_a_cited_run_is_superseded(witness_evidence_doc):
    # Trigger (2): a run is superseded but the pair citing it stays
    # authoritative — the demotion an implementer omits.
    doc = copy.deepcopy(witness_evidence_doc)
    victim = next(r for r in doc["runs"] if r.get("kind") == "conformance")
    victim["authoritative"] = False
    with pytest.raises(AssertionError, match="SUPERSEDED"):
        _check_e7c(doc)
