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
# Run via ctest (registered in tests/interop/CMakeLists.txt) or directly:
#   python3 -m pytest -xvs tests/interop/cell_results_schema_check_test.py

import os
import re

import pytest
import yaml

HERE = os.path.dirname(os.path.abspath(__file__))
MANIFEST = os.path.join(HERE, "cell_results.yaml")

REQUIRED_FIELDS = {"id", "config", "kind", "status", "matrix_disposition", "spec_ref"}
KINDS = {"happy", "thorny", "parity"}
CONFIGS = {"normal", "asan-ubsan", "tsan"}
PRIORITIES = {"P1", "P2", "P3", "watch:P1", "watch:P2", "watch:info"}
DEFERRED_TAGS = {
    "deferred:fixt-routing",
    "deferred:app-messages",
    "deferred:fix8-revisit",
    "deferred:v1.1-mtls",
}


def _status_kind(status):
    """Return the leading token of a status value (before any ':<reason>')."""
    return status.split(":", 1)[0]


@pytest.fixture(scope="module")
def cells():
    with open(MANIFEST, encoding="utf-8") as fh:
        doc = yaml.safe_load(fh)
    assert doc.get("schema_version") == 1, "manifest must declare schema_version: 1"
    rows = doc.get("cells")
    assert isinstance(rows, list) and rows, "manifest must carry a non-empty `cells` list"
    return rows


def test_required_fields_present(cells):
    for c in cells:
        missing = REQUIRED_FIELDS - c.keys()
        assert not missing, f"cell {c.get('id')!r} missing required fields {missing}"


def test_ids_unique(cells):
    ids = [c["id"] for c in cells]
    dupes = {i for i in ids if ids.count(i) > 1}
    assert not dupes, f"duplicate cell ids (a dropped/duplicated cell): {dupes}"


def test_enum_fields_valid(cells):
    for c in cells:
        assert c["kind"] in KINDS, f"{c['id']}: bad kind {c['kind']!r}"
        assert c["config"] in CONFIGS, f"{c['id']}: bad config {c['config']!r}"
        sk = _status_kind(c["status"])
        assert sk in {"pass", "fail", "skip", "known-limitation", "n/a"}, \
            f"{c['id']}: bad status {c['status']!r}"


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


def test_per_cell_completeness_no_silent_absence(cells):
    # Guard the four by-design deferred axes are each present exactly once (a
    # dropped deferred row is indistinguishable from a real deferral otherwise).
    present_tags = {c["matrix_disposition"] for c in cells
                    if str(c["matrix_disposition"]).startswith("deferred:")}
    assert present_tags == DEFERRED_TAGS, (
        f"every deferred axis must have a present row; missing "
        f"{DEFERRED_TAGS - present_tags}, unexpected {present_tags - DEFERRED_TAGS}"
    )
    # The smoke cell (FR-022) must exist.
    ids = {c["id"] for c in cells}
    assert "HP-QFcpp-init-fix44-logon-hb-logout" in ids, \
        "the FR-022 smoke cell must be present in the manifest"
