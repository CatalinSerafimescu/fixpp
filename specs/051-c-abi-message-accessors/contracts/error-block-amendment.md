# Contract — `[2i §4.3]` session/app error-block amendment (Article XX)

This is the one contract that **reopens the signed-off `[2i]`** (CHK030). The review is **folded into this feature's Gate A** per Article XX (043 precedent). The artifact Gate A reviews is the actual `[2i §4.3]` diff + the co-update set below.

## The new block (Phase-4-owned `[1400,1499]`, D-6 — RULED at Gate A round 1)

Add to `include/fix/c_api/error.h`, in a **NEW dedicated Phase-4 session/app + message-construction block** at `[1400,1499]` (NOT in the cross-cutting `[0,99]` sentinel range — see talking point 1):

```c
/* ── Session/app + message-construction block [1400, 1499] — Phase-4-owned ──
 *    (added v0.4 / 051 per Article XX). 1400-1404 map five reachable C++
 *    core::error ordinals; 1405 is a pure C-ABI construction reject. */
#define FIXPP_ERR_SESSION_INVALID_ARGUMENT  ((fixpp_error_t) 1400)  /* core::error session_invalid_argument (119) */
#define FIXPP_ERR_SESSION_INVALID_STATE     ((fixpp_error_t) 1401)  /* core::error session_invalid_state_for_send (77) */
#define FIXPP_ERR_APP_DO_NOT_SEND           ((fixpp_error_t) 1402)  /* core::error app_do_not_send (129) */
#define FIXPP_ERR_APP_CALLBACK_THREW        ((fixpp_error_t) 1403)  /* core::error app_callback_threw (130) */
#define FIXPP_ERR_APP_PAYLOAD_MALFORMED     ((fixpp_error_t) 1404)  /* core::error app_payload_malformed (131) */
#define FIXPP_ERR_MSG_FRAMING_TAG_FORBIDDEN ((fixpp_error_t) 1405)  /* C-ABI: set_* of a framing tag (8/9/34/49/52/56/10) on an outbound accumulator */
/* [1406, 1499] reserved for Phase-4 session/app + message-construction growth. */
```

(The `119/77/129/130/131` in comments are the **C++ ordinals**, NOT the C-ABI codes. `FIXPP_ERR_MSG_FRAMING_TAG_FORBIDDEN` has **no** C++ ordinal — it is a pure C-ABI construction reject raised by the `set_*` framing-tag fail-fast path, never by `translate()`.)

`[1400,1499]` is the **15th and last** 100-wide block in the stated `≤1500 / 15-block` budget (`[2i §1.1]`). Spending it on a dedicated session/app domain — rather than relabelling the `[0,99]` boundary-sentinel block — is the GA-permanent trade-off ruled at 051 Gate A. Confirmed free in `[2i §1.1]`: it was the `[1400+]` "future expansion" reservation, distinct from the `[1300,1399]` post-v1.x wire-format growth block (`[const §XVIII.2]`) — no collision.

## Co-update set (one pass, or one site lags — FR-015/FR-016)

| Site | Change |
|---|---|
| `include/fix/c_api/error.h` | +6 `#define`s above (1400–1405) in a new `[1400,1499]` block |
| `src/capi/error.cpp` `translate()` | re-point the **5** arms (119/77/129/130/131, currently `→ FIXPP_ERR_UNKNOWN`) to 1400–1404; the no-`default` switch stays exhaustive. **1405 gets NO `translate()` arm** (no C++ ordinal; raised only by the `set_*` reject path). |
| `src/capi/error.cpp` `k_strerror_table` | +6 static English strings (zero-alloc), incl. one for `FIXPP_ERR_MSG_FRAMING_TAG_FORBIDDEN` |
| `src/capi/error.cpp` `kIntroducingMinor` | **Replace the scalar `kIntroducingMinor = 2` with a PER-CODE introducing-minor lookup seeded from `error_codes_v1.txt`.** Existing codes keep minor 2; the 6 new codes get minor 4. `translate_for_consumer(code, consumer_minor)` does `introducing_minor(code) > consumer_minor → FIXPP_ERR_UNKNOWN`. **A literal scalar-bump to 4 would silently downgrade EVERY existing 0.2/0.3 code at `consumer_minor=3` — that is the defect this row forbids (Codex #5).** |
| `tools/abi_history/error_codes_v1.txt` | append 6 rows (1400–1405, introducing-minor 4); 1405 annotated as a C-ABI construction reject with no C++ ordinal |
| `tools/check_capi_occupancy.sh` | **Check A `EXPECTED` map gains +6 entries (1400–1405) — the NEW Phase-4-owned accounting term.** This block is Phase-4-minted *published* surface, so it enters **Check A only**. It does **NOT** enter Check B (`EXPECT_COUNT`, the 8 prior-doc `[2X §6.X]` source-domain counts) and does **NOT** change the prior-doc `97` total — those measure C++-side coalescing, which this block is not. |
| `.specify/2i-capi.md` | §1.1 magnitude-domain table (add the Phase-4 row) + §1.1 final-layout block (add the `[1400,1499]` row) + §1.1 reserved-blocks prose (mark `[1400,1499]` now spent) + §4.3 inline `#define` block — swept in ONE pass so the occupancy gate stays green. **The `[0,99]` count stays 11/8 and the prior-doc total stays 97 — UNCHANGED** (dedicated block = no sentinel-range contamination). |

## Gate A talking points (pre-stage so the review doesn't raise them cold)

1. **Why `[1400,1499]` (dedicated Phase-4 block) not `[11,99]`** — D-6 (rewritten): session/app are C-ABI-boundary **domain** failures (bad arg / bad state-for-send / business veto / callback failure / malformed payload), NOT boundary **sentinels** like `NULL_HANDLE`/`INVALID_HANDLE`. Placing them in `[11,99]` would permanently **relabel** the `[0,99]` sentinel block — which every future consumer reads as "C-ABI boundary sentinel." On the LAST pre-GA C-ABI feature the relabel is forever; a dedicated domain is correct taxonomy. The cost — spending the last reserved `[1400,1499]` block + a new Phase-4 occupancy accounting term — is bounded and acceptable. The alternatives (`[11,99]` sentinel-range contamination, or extending the 2d THREAD block — app codes in a *threading* block) are both worse. RULED `[1400,1499]` at Gate A round 1.
2. **Why a DISTINCT message-construction reject code (1405) for framing-tag set, not `SESSION_INVALID_ARGUMENT`** — setting tag 8/9/34/49/52/56/10 on an outbound *message accumulator* is a **message-construction** error, not a session-API argument error. Reusing `SESSION_INVALID_ARGUMENT` (a session-domain code mapping the C++ `session_invalid_argument` ordinal) is semantically wrong. `FIXPP_ERR_MSG_FRAMING_TAG_FORBIDDEN` is the pinned distinct code (INV-3 / message-write `set_*`).
3. **Why 6 distinct codes vs §4.3 "coalesce"** — 5 distinct consumer-remediation classes (bad arg / bad state / veto / threw / malformed) + 1 construction-reject class, not per-variant noise — the same test §4.3 applies to its per-doc groups.
4. **Per-code introducing-minor, not a scalar** — the as-built `kIntroducingMinor` is a single scalar `= 2`; the in-source comment already mandates the per-code lookup. The amendment REPLACES the scalar so a `consumer_minor=3` engine downgrades ONLY the new minor-4 codes, never the existing 2/3 codes. SC-004 witnesses both (an OLD code surviving + a NEW code downgrading at `consumer_minor=3`).
5. **Reachability** — all 5 mapped arms are end-to-end-witnessable from pure C now that the toApp hook ships (D-13); SC-004 asserts each live. 1405 is witnessed by the `set_*` framing-tag reject test.
6. **Closes** — L-050-4 (block published) + L-049-2 (session/app arms; log/otel stay deferred-by-design, `[1000,1099]` reserved) + the 050 FR-015/SC-005.
