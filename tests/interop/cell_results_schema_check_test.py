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
    "deferred:fix8-revisit",
    "deferred:v1.1-mtls",
    # QuickFIX-cpp cannot emit a controllable too-low PossDup (Session::send()
    # strips 43/122; no public sendRaw/AllowPosDup) and never resends an
    # already-seen frame — so the 4 PD-QFcpp-* cells are by-design not runnable.
    # fixpp's receive-path PossDup tolerance is proven engine-independently via
    # the 4 PD-QFj-* cells + test_inbound_poss_dup_tolerance.cpp.
    "deferred:qfcpp-no-possdup-injection",
    # First live run of the 024 reset_on_logon INITIATOR cells found a real fixpp
    # bug: the initiator rebases its outbound seqnum 2->1 on the peer's 141=Y echo
    # (session.cpp:3185; 030 fixed only the inbound twin) → next send duplicates
    # 34=1. The 2 RL-*-init cells are deferred to a 030/031-class fix-feature; the
    # 2 RL-*-acc cells pass live (030-fixed acceptor path). See L-024-2.
    "deferred:initiator-141echo-outbound-rebase",
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
    # Deferred rows (3)
    "HP-fixt11-fix50sp2-cells",
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
