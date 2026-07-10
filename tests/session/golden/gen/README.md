# QuickFIX golden generators — 061-slim external shape-oracle anchor

These are the **offline** QuickFIX-cpp programs that authored the checked-in
body-only goldens in `tests/session/golden/*.fix`. They are the reproducible
source of the feature's ONE non-tautological anchor (FR-006 / contracts §C5) —
kept here so the goldens can be re-derived (and so FR-015a's generated builders
have the same reference). They are NOT compiled by the main build.

| Generator | → golden | MsgType |
|---|---|---|
| `qf_new_order_single.cpp`  | `../new_order_single.fix`  | D  |
| `qf_execution_report.cpp`  | `../execution_report.fix`  | 8  |
| `qf_order_cancel_reject.cpp`| `../order_cancel_reject.fix`| 9 |
| `qf_new_order_list.cpp`    | `../new_order_list.fix`    | E  |
| `qf_allocation_report.cpp` | `../allocation_report.fix` | AS |
| `qf_market_data.cpp` (067 T018 — R5 paired NoMDEntries(268) discriminator) | `../market_data_snapshot.fix` | W |
| `qf_market_data.cpp` (067 T018 — same program, second message) | `../market_data_incremental.fix` | X |
| `qf_mass_quote.cpp` (067 T018 — R5 deep-nested-group insurance) | `../mass_quote.fix` | i |

## Re-run recipe

Requires the QuickFIX-cpp reference engine (per `project_reference_engines_setup`),
at the parent-repo path `reference-engines/quickfix-cpp` (gitignored,
`libquickfix.so.17` = v1.16.0):

```bash
QF=/path/to/reference-engines/quickfix-cpp   # parent repo root
g++ -std=c++17 -I"$QF/include" qf_new_order_list.cpp -L"$QF/lib" -lquickfix \
    -Wl,-rpath,"$QF/lib" -o /tmp/qf_gen && /tmp/qf_gen
```

Each program serializes its exemplar seed via `FIX44::<Msg>` + `Message::toString()`,
then the body is stripped to body-only: remove header tags `8,9,34,49,52,56` and
trailer `10`, keep the leading `35=<MsgType>` + business fields **in QuickFIX's
exact emitted order** (root ascending-tag; group entries in FIX44 dictionary
`message_order`). Store as a single `> `-prefixed line (the `golden_diff.hpp`
direction-prefix convention). Full observed frames + per-level field order are
recorded in `../PROVENANCE.md`.
