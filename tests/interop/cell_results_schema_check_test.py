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
# E-1c/E-7a/E-7b/E-7c/W-3* completeness gates are implemented by later tasks
# (T066-T092); this file only proves the three sections exist and are
# distinguishable from a missing/null section.
#
# Run via ctest (registered in tests/interop/CMakeLists.txt) or directly:
#   python3 -m pytest -xvs tests/interop/cell_results_schema_check_test.py

import os
import re

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
# ⚠️ `witnesses` is not a literal key name given anywhere in the spec bundle
# (unlike `runs`/`validation_pairs`, named literally throughout) — see the
# NOTE in witness_evidence.yaml itself.
WITNESS_EVIDENCE_SECTIONS = ("witnesses", "runs", "validation_pairs")
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


@pytest.fixture(scope="module")
def cells():
    with open(MANIFEST, encoding="utf-8") as fh:
        doc = yaml.safe_load(fh)
    assert doc.get("schema_version") == 1, "manifest must declare schema_version: 1"
    rows = doc.get("cells")
    assert isinstance(rows, list) and rows, "manifest must carry a non-empty `cells` list"
    return rows


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
