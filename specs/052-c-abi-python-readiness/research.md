# Research: C-ABI Python-readiness (052)

Source-verified decisions resolving the spec's `/plan`-deferred unknowns. Each is grounded in a
file:line read (CodeGraph + direct), not reconstruction.

## D-1 — Inbound field iteration wraps the `OffsetTable` (FR-006 FEASIBLE)

**Decision:** `fixpp_msg_field_count` → the inbound view's offset-table `size()`; `fixpp_msg_field_at(i)`
→ the `i`-th offset-table entry `{tag, offset, length}`, resolved to a `(tag, value_ptr, len)` aliasing
the wire buffer. No re-parse; no new parsing logic.

**Rationale:** The inbound parsed message carries an `OffsetTable` (`include/fixpp/wire/offset_table.hpp`)
exposing `entries() → std::span<const entry>` (document/wire order, index-addressable) + `size()`; each
`entry` has `tag`, `offset`, `length` (`offset_table.hpp:85-87`). The parser surfaces it via
`MessageView<access_mode::Index>::offsets()` (`parser.hpp:193-194`). The span **includes framing tags**
(8/9/10 are present and merely exempted from "unknown" classification at `parser.hpp:264-269`) — which
satisfies the clarified contract (enumerate **every `OffsetTable` entry in wire order** — a multiset,
one entry per wire occurrence; the scalar typed getters read a first-occurrence subset, FR-007).

**D-1a — RESOLVED at Gate A (was an open implement-time detail):** the inbound (and clone) C-ABI
`fixpp_msg` view member is `const MessageView<access_mode::Index>*` (`capi_internal.hpp:227`; clones
back it with an `owned_view_` `MessageView<Index>` at `:262-263`, and `CapiApplication::fromApp`
receives `MessageView<access_mode::Index>` at `:103`). The inbound view **is** Index-mode →
`offsets()`/`entries()` are available; the Scan-mode fallback is **dead** and is not carried to
implement. No per-message-arena index build is needed.

**Alternatives considered:** a tag-ordered map iterator (rejected — wire order is what a FIX consumer
expects and what byte-agreement SC-002 checks); re-parsing in the C-ABI (rejected — the offset table
already exists, re-parse would violate the zero-global-heap rule).

## D-2 — `fixpp_dict_load_from_xml` wraps `XmlLoader::load`; construction thunk catches its throws

**Decision:** `fixpp_dict_load_from_xml(path, out)` calls `fixpp::dict::XmlLoader::load(path, mr)` with
`mr = std::pmr::get_default_resource()`, wraps the resulting `Dictionary` in
`std::make_shared<const Dictionary>(std::move(d))`, stores it in `fixpp_dict { shared_ptr<const Dictionary> dict; }`
(`src/capi/capi_internal.hpp:134-135`), and returns the handle. It is a **construction-time thunk**
(`guarded_call_construction`, `[2i §5.2]`): catches the `XmlLoader` throws and returns
`FIXPP_ERR_CAPI_CONFIG_INVALID`.

**Rationale:** `XmlLoader::load(std::filesystem::path const&, std::pmr::memory_resource*)`
(`xml_loader.hpp:57-58`) is **NOT `noexcept`** — it throws `dict::xml_parse_error`,
`dict::unknown_version_error`, `dict::xml_oom_error` on bad input. Catching `std::exception` (their base)
→ `CAPI_CONFIG_INVALID` matches `[2i]` line 652 ("dict_load" is a listed `CAPI_CONFIG_INVALID` source)
and the §5.2 construction whitelist (which already names `fixpp_dict_load_from_xml`). The ownership
model (`shared_ptr<const Dictionary>`, consumer destroys their handle after `set_dictionary` copies the
shared_ptr) is exactly what the L-050-1 seam already implements — this is "make the seam real."

**`mr` choice:** `std::pmr::get_default_resource()` (the production default when `selector_resolver`
does not supply one). The Dictionary's metadata is heap-pinned via its own `shared_ptr`, so the loader's
transient `mr` only backs parse-time scratch — the default resource is correct and avoids threading an
engine-owned arena into a pre-engine dict load.

**Alternatives considered:** an engine-owned `mr` (rejected — the dict can be loaded before any engine
exists; the loader must be engine-independent); returning `fixpp_dict_t*` directly per the `[2i §2]`
v0.1 sketch (rejected — FR-001a: adopt the as-built `fixpp_error_t`-return + out-param convention).

## D-3 — Endpoint setter writes `reconnect_endpoint`; readback wraps `acceptor_bound_endpoint`

**Decision:** `fixpp_session_config_set_tcp_endpoint(cfg, host, port)` sets
`cfg->cfg.reconnect_endpoint = fixpp::transport::Endpoint{host, port}` **and** the internal
`cfg->cfg.transport_send` no-op placeholder (mirroring the L-050-5 seam). `fixpp_session_acceptor_bound_endpoint(session, port_out)`
calls `session->engine->state_->engine_->acceptor_bound_endpoint(session->id).port`.

**Rationale:** `Engine::acceptor_bound_endpoint(SessionId const&) const → fixpp::transport::Endpoint`
(`engine.hpp:312`; `.port == 0` until the listener binds). The C-ABI `fixpp_session` already caches
`engine` + `id` and reaches the C++ engine via `state_->engine_` exactly as `fixpp_session_is_established`
does (`src/capi/session.cpp`; seam pattern `capi_loopback_support.hpp:82-86`). Port-only readback avoids
a host-string lifetime contract (the bind host is consumer-known). Setting `transport_send` internally
keeps the consumer from ever touching it (the engine's auto-derived plaintext factory replaces it at
connect/accept, `engine.cpp:1024`).

**Alternatives considered:** exposing `fixpp_endpoint_t` PoD (rejected — `[2i §7.8]` defers it; FR-005a
keeps it deferred, primitives only); a host+port readback struct (rejected — D-3 port-only per advisor;
no host-string lifetime contract).

## D-4 — SC-001 loopback: the reset-policy setter SHIPS (pinned at Gate A r1, user decision); the surface is a deterministic 7 symbols

**Decision (Gate-A r1 — user decision):** `fixpp_session_config_set_reset_seqnum_policy` **ships in this
feature** (FR-005b / data-model E-4), so the reviewed ABI surface is **deterministic: 7 symbols**, fixed
at Gate A. The SC-001 pure-public-C-ABI loopback self-test configures both sides' reset policy through
the **public** setter — it may set `bilateral_strict` + `reset_on_logon(true)` and confirm establishment,
or select `bilateral_lenient` directly (no longer depending on the L-050-5 internal cast). There is **no
+1 implement-time contingency** and no "symbol count decided by a test."

**Rationale (source logic — strict should also establish, and the public setter makes it moot):** under
`bilateral_strict`, a fresh endpoint always emits `141=Y` (`session.cpp:816-818`: `initr_reset_seqnum =
bilateral_strict || (any_reset_knob && seqnums_at_one)`); the acceptor mirrors it
(`session.cpp:2456-2459`) and the strict guard (`session.cpp:2187-2195`: reject only when
`!peer_sent_reset && bilateral_strict`) passes because the peer DID send `141=Y`. So strict +
both-emit-141=Y should establish. The previously-carried risk was that a **public** C-ABI consumer could
not set the policy at all (only the `reset_on_logon` bool), so SC-001 leaned on the strict-default
establishing while the test infra used `bilateral_lenient` via the L-050-5 internal cast
(`capi_loopback_support.hpp:69-76`) — a path a public consumer cannot use. **Pinning the setter removes
that risk entirely:** a public consumer can now select either policy, so SC-001 is no longer gated on the
strict-default establishing.

**Alternatives considered:** leaving the setter deferred and gating the symbol count on an implement-time
empirical RED/GREEN test (rejected at Gate A r1 — an ABI surface whose exported-symbol count is "decided
by a test" is under-determined for a review immediately preceding the `0→1` freeze; one trivial additive
setter makes the surface deterministic, per Codex P2#3 / Opus N-B).

## D-5 — No new `fixpp_error_t` codes; occupancy gate UNCHANGED

**Decision:** mint zero new codes. dict-load failure → `FIXPP_ERR_CAPI_CONFIG_INVALID`; bad endpoint →
`CAPI_CONFIG_INVALID`; NULL → `NULL_HANDLE`; destroyed → `INVALID_HANDLE`; `field_at` OOB →
`FIXPP_ERR_INDEX_OUT_OF_RANGE` (the existing group-cursor code). `tools/check_capi_occupancy.sh` and
`[2i §4.3]` are untouched — no amendment, no `EXPECTED`/`EXPECT_COUNT` edit.

**Rationale:** every failure mode maps cleanly to an existing code; introducing new codes would force a
permanent-at-GA `[2i §4.3]` slot decision for no semantic gain. Genuine simplification vs 051. (Gate A:
do not look for an occupancy diff.)

## D-6 — MINOR bump 0.4.0 → 0.5.0; additive symbols; no `[2i]` reopen

**Decision:** bump `FIXPP_C_ABI_VERSION_MINOR` 4→5; append the new symbols to
`tests/abi/golden/fixpp_capi_symbols.txt`; `abidiff` must report additive. GAP-002 recorded as a LOCAL
Gate-A deviation (clarify A) — `[2i]` is NOT reopened; no Article XX amendment, no `[2i]` co-update set
(contrast 051, which did reopen §4.3). Gate A reviews the recorded deviation prose only.

**New exported symbols (7):** `fixpp_dict_load_from_xml`, `fixpp_dict_destroy` (new header `dict.h`);
`fixpp_session_config_set_tcp_endpoint`, `fixpp_session_acceptor_bound_endpoint`,
`fixpp_session_config_set_reset_seqnum_policy` (`session.h`); `fixpp_msg_field_count`,
`fixpp_msg_field_at` (`message.h`) — **7** symbols; plus the PoD type `fixpp_msg_field_t` and the C11
enum `fixpp_reset_seqnum_policy` (no symbols). The umbrella `include/fix/c_api.h` aggregates the new
`dict.h` (FR-014). The reset-policy setter is **pinned** (D-4, Gate-A r1 user decision) — the surface is
deterministic at Gate A, no contingency.
