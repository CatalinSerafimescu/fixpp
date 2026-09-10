# Quickstart — validating 089 end to end

**Purpose**: prove the feature works against a live QuickFIX peer, and prove each assertion can fail.
Not an implementation guide — see [contracts/](./contracts/) for interfaces and
[data-model.md](./data-model.md) for record shapes.

---

## ⛔ Step 0 — disk preflight. Every time. Before every build.

```bash
ci/disk-preflight.sh <preset>
```

**Do not skip this and do not substitute `df -h .`.** Measured 2026-09-10 on this machine:

| Reading | Value |
|---|---|
| `df -k /` (inside the WSL2 VHD) | 87,050,032 KiB ≈ **83.0 GiB** |
| `df -k /mnt/e` (the Windows host drive backing it) | 17,766,184 KiB ≈ **16.9 GiB**, 93 % used |

The first number is the one that will let you start a 34 GiB build that cannot finish. The second is the
one that binds. See [contracts/disk-preflight.md](./contracts/disk-preflight.md).

If the verdict is `reclaim-first`:

```bash
rm -rf build/linux-clang-ubsan    # 1.5 G — cheapest useful, try first
rm -rf build/linux-clang-tsan     # 25 G
rm -rf build/linux-clang-asan     # 34 G
```

⛔ **Never `build/linux-clang-debug/`** — standing user rule, 2026-09-10.

If the verdict is `stop` even after reclaim: **stop and report.** Do not start a partial build, and do
not record any resulting cell as a `pass` or a `skip`.

---

## Step 1 — prerequisites

| Need | Check | Note |
|---|---|---|
| Counterparty image | pull **by digest** | ⚠️ not `:latest` — FR-016b. The published image predates this feature and must be rebuilt first |
| QuickFIX-cpp counterparty binary + `libquickfix.so` | extracted from the image | needs `LD_LIBRARY_PATH` |
| QuickFIX-J shaded jar | extracted from the image | ⚠️ `interop-smoke.yml` does **not** extract it today — the QF-J cells need it |
| TLS fixtures | `phase-9-harness/tls/gen-interop-certs.sh` | generated at run time from fixpp's committed CA; never baked into the image |
| FIX 4.4 dictionary, peer side | `INTEROP_QFCPP_SPEC_DIR` for C++; **classpath** for Java | ⚠️ the two resolve differently — see research R-5 |

---

## Step 2 — the shortest real signal

Run **one** cell, one config, and read the result off the artifact rather than off an exit code:

```bash
python3 ../phase-9-harness/tools/run_interop_cell.py <cell-id> \
    --config normal --build-root "$PWD/build" --out /tmp/cell.yaml
```

Then confirm, in order:

1. **The peer announced itself.** The readback stream's first line is a `hello` with an
   `engine_version` and a `readback_protocol`. **No hello ⇒ the cell must have FAILED**, not skipped.
2. **`dictionary_enabled` is true** for a cell whose witnesses include repeating groups. Under
   `UseDataDictionary=N` both engines flatten groups, so a group-free readback would compare clean
   against a group-free intent — a false green.
3. **Witnesses exist for both directions**, including the fixpp-acceptor direction that today asserts
   only counters.
4. **The cell row carries evidence** — run id, timestamp, counterparty version *from the hello record*,
   image digest, artifact path.

---

## Step 3 — the 24-run matrix, in sequence

⚠️ **All three configs cannot be resident at once.** `asan 34 + tsan 25 + debug 27` = 86 G against 83.0
GiB free inside the VHD. Run one config at a time:

```
for config in normal asan-ubsan tsan:
    ci/disk-preflight.sh          → must say proceed
    build that config
    run its 8 cells               (4 role×flavour × 2 validation arms)
    PERSIST results + evidence outside the build tree     ← before reclaim
    reclaim that config
```

⚠️ **Persist before reclaim.** Deleting a build tree that still holds the only copy of a run's evidence
loses the run.

⚠️ **Evidence accumulates; it is not rewritten per config.** The witness completeness gate runs **once,
after the last config, over the union**. A per-config rewrite would leave the gate passing over a third
of the evidence.

**TSan is bring-up, not a config flip.** It has never been run on the paired live matrix. A TSan arm that
skips, dies in setup, or produces no witnesses is a **failure** (FR-022), and it must produce the **same
witness set** as the `normal` arm (SC-011).

---

## Step 4 — prove the instruments can fail

A green run proves nothing until each assertion has been shown RED. Minimum set:

| Force | Expect |
|---|---|
| A wrong field value in one sent message | that witness RED, naming the path and `value_mismatch` |
| Omit a required field | RED, classified `missing` |
| Add a field the intent does not declare | RED, classified `spurious` — ⚠️ the class a subset comparison cannot see |
| A message the peer should Reject | the cell fails; ⚠️ tolerate QuickFIX-cpp *disconnecting* instead of rejecting — only QF-J 3.0.1 is confirmed to `Reject(35=3)` |
| **Counterparty with the readback channel removed** | **every** affected cell RED — zero pass, zero skip (SC-005a). Jointly exercises FR-016a and FR-016c |
| A falsified `cell_results.yaml` row — `pass`, no evidence | schema check RED (E-1) |
| Delete one witness | completeness gate RED (W-3) |
| `ci/disk-preflight.sh` — all 7 arms | see [contracts/disk-preflight.md](./contracts/disk-preflight.md); ⚠️ A-3 and A-4 only pin the rule **as a pair** |

**Every RED cell must assert its OWN diagnostic, not merely a non-zero exit.** In PR #255 two cells
reddened via a *different* check than the one they were written for and stayed green under a mutation
that reverted the rule they were meant to pin.

---

## What success does NOT mean

This feature closes **zero catalogue rows**. The narrow set — 35=D/8/F/G/9 — is `A-001`, `A-003`,
`A-004`, `A-006`, `A-007`, **all already `done`**. Success is that the fidelity machinery exists, is
proven able to fail, and emits evidence a catalogue flip can stand on. The 10 QuickFIX-route rows close
in the breadth follow-on; the 31 FIX-Latest rows close under row 4b's differential baseline.
