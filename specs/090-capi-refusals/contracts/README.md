# Contracts — 090 C-ABI refusals

**This is a library with a C ABI, so the C-ABI surface IS the contract.** There is no network
protocol, no RPC schema and no OpenAPI document to publish. Each file below is a **per-surface**
contract for the declarations this feature changes, in the house shape of
`specs/050-c-abi-session-send-recv/contracts/` and `specs/052-c-abi-python-readiness/contracts/`.

| file | surface | breaking? |
|---|---|---|
| [`msg-remove-tag.md`](./msg-remove-tag.md) | `fixpp_msg_remove_tag` (fixpp#447) | **BREAKING** |
| [`msg-clone.md`](./msg-clone.md) | `fixpp_msg_clone` (fixpp#458) + its `[C++ track]` half | **BREAKING** |
| [`session-config-byte-floor.md`](./session-config-byte-floor.md) | `fixpp_session_config_set_comp_ids`, `fixpp_session_config_set_begin_string` (fixpp#452) + the `[C++ track]` half | **BREAKING** |
| [`msg-index-bounds.md`](./msg-index-bounds.md) | the group/entry index surface gaining a **defined** refusal (D-2b / FR-004) | **NOT breaking** — see the file |
| [`version-and-freeze.md`](./version-and-freeze.md) | the version contract: **1.6 → 1.7**, MINOR **marked BREAKING**, `[const §X.7]`, no amendment | the bump itself |

⚠️ **The one thing to get right before reading any of these.** The design authority's §0a names
*"fold all five behavioural changes under `C-ABI 1.7 BREAKING`"* as the single most likely way to get
this work wrong. **Two tracks run through this feature:**

| track | governing authority |
|---|---|
| **C-ABI** | `[const §X.7]`'s BREAKING machinery, plus `[const §X.1]`'s mandatory Gate A and `[const §X.6]`'s four Appendix A controls |
| **`[C++ track]`** | `[const §XVII.1]` (*"Touches the public C++ API"*). **`[const §X.7]`'s machinery does not reach these** — they are not C-ABI symbols, they consume no error-code slot, they move no version macro, and they are **not in `tools/capi_freeze.sha256`'s manifest**. `[const §X.7]` is cited for them **to record that it is NOT engaged**, per `.specify/456-table-view-seal.md`'s precedent |

Every `[C++ track]` section below carries that label inline. An unlabelled section is C-ABI.

**The discriminator for "BREAKING".** `.specify/api-contract.md` §11 supplies the *definition* —
*"Making a call to a Stable-from-v1.0 C-ABI symbol fail where it used to succeed"* — and
`[const §X.7]` restates it without the documentation escape hatch: *"So is a call that used to
succeed and now fails, whatever the documentation said about it."* §11 also routes the *procedure*
to `[const §X.7]` before the first public release (a MINOR bump marked BREAKING, **no constitutional
amendment**); citing §11 for the procedure would be wrong.

⚠️ **Gate A RAN and did NOT converge.** It is `gate-a-waived` on two reasons. `/speckit-analyze` and
the user's `/plan` sign-off are **OWED**. Nothing in this directory asserts a Gate A pass.

**Design authority**: `.specify/447-458-452-capi-refusals.md` **v0.10**. These files restate its
decisions as contracts; they do not re-decide, re-derive or contradict anything in it.
