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
# ⚠️ E-7a/E-7b/E-7c are NOT yet wired into test_schema_check_opens_no_
# artifact_path()'s unconditional sweep over the REAL committed
# witness_evidence.yaml: that file's `validation_pairs:`/`runs:` sections
# are still empty (no run has been promoted into the COMMITTED artifact —
# T061's live evidence went to a scratch ledger, per that task's own
# instruction), and E-7a's own defect is exactly "zero pairs is green" — so
# running it unconditionally against today's committed doc would redden the
# whole schema check for a population a LATER task (T092, the same one this
# comment already deferred W-3*/E-1c to) is responsible for populating. The
# logic exists and is proven now; wiring it to the live artifact waits for
# that population.
#
# Run via ctest (registered in tests/interop/CMakeLists.txt) or directly:
#   python3 -m pytest -xvs tests/interop/cell_results_schema_check_test.py

import contextlib
import inspect
import os
import re
import sys

import pytest
import yaml

HERE = os.path.dirname(os.path.abspath(__file__))
MANIFEST = os.path.join(HERE, "cell_results.yaml")
WITNESS_EVIDENCE = os.path.join(HERE, "witness_evidence.yaml")

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
        doc = yaml.safe_load(fh)
    assert doc.get("schema_version") == 1, "manifest must declare schema_version: 1"
    rows = doc.get("cells")
    assert isinstance(rows, list) and rows, "manifest must carry a non-empty `cells` list"
    return rows


@pytest.fixture(scope="module")
def cells():
    return _load_cells()


def _load_witness_evidence():
    with open(WITNESS_EVIDENCE, encoding="utf-8") as fh:
        doc = yaml.safe_load(fh)
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
    # E-1a (contracts/witness-evidence.md:51) / data-model.md §5 "Validation
    # rules" (lines 447-450): manifest row identity is (cell_id, config) — 32
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
    present_ids = {c["id"] for c in cells}
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
