# Phase 0 Research: Thin End-to-End Python Binding (PY-001)

Signatures below were re-verified against the shipped headers (`include/fix/c_api/*.h`) and the C-ABI test
support (`tests/capi/capi_loopback_support.hpp`) at **Gate A round 1 (2026-06-26)**. The re-verification
found **two** signatures wrong and corrected them: `fixpp_engine_create` is the **4-arg** form (see D-4) and
the stale `fixpp_dict_load_xml_path` is really `fixpp_dict_load_from_xml` (spec Assumptions). The rest were
confirmed against the headers — the D-4 table now matches `engine.h` (`:81-84`), `session.h` (`:100-167`,
`:262`, `:277`), `message.h`, and `dict.h` (`:45-46`, `:67`).

## D-1 — Two engines, not one (forced by the lifecycle contract)

**Decision**: The round-trip stands up **two C-ABI engines** — one acceptor-role, one initiator-role —
each with its own session, over a loopback TCP connection.

**Rationale**: `fixpp_session_open` MUST be called **before** `fixpp_engine_start` (session.h:179 —
"register-before-start"). The acceptor binds an **ephemeral** port (`set_tcp_endpoint(host, 0)`), and the
OS-assigned port is only readable **after** start via `fixpp_session_acceptor_bound_endpoint` (session.h:224,
"poll until non-zero"). A single engine therefore cannot both (a) open the initiator before start and
(b) know the acceptor's port (known only after start). Sequence: **engine A** (acceptor) open → start →
poll bound port; then **engine B** (initiator) configured with that port → open → start. This mirrors the
gold-reference `capi_loopback_support.hpp` two-C-ABI-engine pattern.

**Alternatives considered**: One engine hosting both sessions — rejected (ephemeral-port-after-start vs
register-before-start conflict). A fixed non-ephemeral port — rejected (flaky under parallel CI; port
collisions).

## D-2 — Establishment sequence & the poll-with-deadline rule

**Decision**: Order per round-trip: load dict → (A) engine-config → engine_create → session-config
(role=acceptor, comp_ids, begin_string `FIX.4.4`, dictionary, **security `insecure_plain_tcp`**,
**heartbeat 30**, **reset_on_logon=false**, reset_seqnum_policy `bilateral_lenient`,
tcp_endpoint `127.0.0.1:0`) → session_open(A) → register inbound callback → engine_start(A) → poll
`acceptor_bound_endpoint` until non-zero → (B) engine-config → engine_create → session-config
(role=initiator, **reversed** comp_ids, begin_string, dictionary, **security `insecure_plain_tcp`**,
**heartbeat 30**, **reset_on_logon=true**, reset_seqnum_policy `bilateral_lenient`,
tcp_endpoint `127.0.0.1:<port>`) → session_open(B) → engine_start(B) → poll `is_established` on both.

**Rationale (the CI-hang guard)**: every wait — the bound-port poll, the establishment poll, and the
callback-receipt wait — MUST use a **bounded deadline that fails the test** on expiry, never an unbounded
`while`/`Event.wait()`. Prior scar: live-I/O probes that block forever hang the whole CI job
(`feedback_fail_placeholder_red_test`). Concretely: a per-poll sleep with a wall-clock cap (e.g. ≤5 s
establish, ≤5 s receive), asserting non-timeout.

**Alternatives**: blocking on a native condition — rejected (no bounded-wait guarantee from Python).

## D-3 — First-establishment session config (mirror the gold reference)

**Decision**: Mirror `capi_loopback_support.hpp`'s `make_session_cfg` session knobs (it sets **three**, and
uses **FIX 4.2**), and add the one extra knob the FIX 4.4 round-trip needs, rather than guessing or partially
mirroring. The three knobs the gold reference sets that the round-trip MUST also set are:
- `fixpp_session_config_set_security(sc, FIXPP_SECURITY_INSECURE_PLAIN_TCP, NULL, NULL)` — **explicit
  plaintext** (`:163-165`). This is load-bearing, not optional: Article XII §5 (`constitution.md:187`) keeps
  the `unset` sentinel **rejected at `Session::open()`**, so a config with no security profile cannot
  establish at all.
- `fixpp_session_config_set_reset_on_logon(sc, role == FIXPP_ROLE_INITIATOR)` — **per-role** (`:167`): the
  initiator resets to seq 1 on logon, the acceptor does not, so a fresh pair logs on cleanly.
- `fixpp_session_config_set_heartbeat_seconds(sc, 30)` (`:162`).
- `fixpp_session_config_set_reset_seqnum_policy(sc, bilateral_lenient)` — **NOT in the gold reference**;
  the extra knob the FIX 4.4 pairing adds — accepts the one-sided 141=Y the per-role `reset_on_logon`
  asymmetry produces (`session.h:141-142`).

**Rationale**: a fresh loopback with default seqnum expectations may not log on cleanly; the gold reference
chose this exact set deliberately. A wrong/missing knob (especially the security profile) manifests as a
**non-establishment that the D-2 deadline turns into a (correct) test failure** — but mirroring the proven
config in full avoids burning a cycle on it. All four setters already exist on the 052 surface (no C-ABI
change). Exact enum values to be read from `session.h` / the C-ABI test at implement time.

**Alternatives**: defaults — rejected (unproven for a cold loopback; risks a flaky establish).

## D-4 — SWIG: selective wrap + explicit out-param typemaps (the false-green guard)

**Decision**: Replace the blanket `%include "fix/c_api.h"` with a **selective** interface: `%include` only
what the round-trip needs (or `%ignore` the rest), and write an explicit typemap for **every** out-param.
The ~14 functions in scope and their out-param shape:

| Function | Out-param → Python | Typemap |
|---|---|---|
| `fixpp_dict_load_from_xml(path, **out)` | handle | stock `OUTPUT` (opaque ptr) |
| `fixpp_dict_destroy(d)` | — | plain |
| `fixpp_engine_config_create(**out)` / `_set_*` / `_destroy` | handle | stock `OUTPUT` |
| `fixpp_engine_create(cfg, uint16_t consumer_major, uint16_t consumer_minor, **out)` / `_start` / `_destroy` | handle | **hand** (Python `engine_create(cfg)` wrapper injects the version macros — see below) / stock `OUTPUT` |
| `fixpp_session_config_create(**out)` / `_set_comp_ids` / `_set_begin_string` / `_set_role` / `_set_dictionary` / `_set_security(kind, const char* cert, const char* key)` / `_set_reset_on_logon(bool)` / `_set_heartbeat_seconds(int)` / `_set_reset_seqnum_policy` / `_set_tcp_endpoint` / `_destroy` | handle | stock `OUTPUT`; `_set_security`'s `cert`/`key` use the T-3 config-`const char*` typemap (accept `None`→`NULL`), `_set_reset_on_logon`/`_set_heartbeat_seconds` are plain. The `FIXPP_SECURITY_INSECURE_PLAIN_TCP` enum constant is exposed to SWIG (alongside the `ROLE_*` / `RESET_SEQNUM_*` enum constants, per T-1's `%constant` version-macro pattern). |
| `fixpp_session_open(engine, cfg, **out)` | handle | stock `OUTPUT` |
| `fixpp_session_is_established(s, bool* out)` | `bool` | stock `OUTPUT` |
| `fixpp_session_acceptor_bound_endpoint(s, uint16_t* out)` | `int` | stock `OUTPUT` |
| `fixpp_msg_create_outbound(s, type, len, **out)` | handle | **hand** (str+len → one Python str) |
| `fixpp_msg_set_string(m, tag, val, len)` | — | **hand** (Python str → val+len) |
| `fixpp_msg_commit(m, const uint8_t** payload, size_t* len)` | `bytes` | **hand** (payload+len → `bytes`) |
| `fixpp_session_send(s, frame, len)` | — | **hand** (`bytes` → frame+len) |
| `fixpp_msg_get_string(m, tag, …out buf…)` | `str` | **hand** (out-buffer → Python str) |
| `fixpp_msg_destroy(m)` | — | plain |
| `fixpp_session_register_callback(s, cb, userdata)` | — | **hand** (Python callable → trampoline + INCREF) |

**`engine_create` version-macro injection (Gate A r1 correction)**: the real symbol is
`fixpp_engine_create(cfg, uint16_t consumer_major, uint16_t consumer_minor, fixpp_engine_t** out)`
(`engine.h:81-84`) — it records the consumer's ABI minor for Feature-A's forward-compat downgrade. The
Python `engine_create(cfg)` is therefore a **thin hand-wrapper** that calls
`fixpp_engine_create(cfg, FIXPP_C_ABI_VERSION_MAJOR, FIXPP_C_ABI_VERSION_MINOR, &out)`, where the two macros
(`= 0` / `= 5`) come from `version.h:32-33`. Those macros must be made visible to the SWIG layer (e.g. an
`%inline`/`%constant` exposure of `version.h`) so the wrapper can pass them. No `c_api.h` change.

**Rationale**: SWIG's blanket include **compiles** wrappers for everything, but without typemaps the
out-params are unusable from Python — the import smoke test stays green while the round-trip functions are
dead (false-green). The **e2e-first RED test forces every typemap to actually work end-to-end** — that is
the explicit reason the TDD ordering is load-bearing here, not ceremony. Keep it thin: stock `typemaps.i`
`OUTPUT` covers the scalar/handle out-params; only the callback, `commit→bytes`, `get_string→str`, and the
str+len setters are hand-written.

**Alternatives**: keep the blanket `%include` and "hope SWIG figures it out" — rejected (false-green). A
hand-written CPython C extension with no SWIG — rejected (Article IV §3 mandates SWIG).

## D-5 — Inbound callback trampoline: GIL + callable lifetime + non-owning msg proxy

**Decision**: A hand-written C trampoline placed in the `.i` `%{ %}` / `%inline` block (so it has the SWIG
runtime + type tables). On `register_callback`, store the Python callable as `userdata` and **`Py_INCREF`**
it; the trampoline matches `fixpp_recv_cb = void(*)(const fixpp_msg_t*, void*)` and, when fired from the
engine worker thread, does `PyGILState_Ensure()` → wrap the `const fixpp_msg_t*` as a **non-owning** SWIG
proxy via `SWIG_NewPointerObj(..., 0 /*own=0*/)` → call the Python callable → `PyGILState_Release()`. The
`Py_INCREF` is the load-bearing half; for the thin single-callback test the callable is **held until
interpreter exit**. DECREF-on-reregister / deregister / engine teardown requires a session-keyed registry and
is deferred to **PY-004** (consistent with `data-model.md` E-4).

**Rationale (three forced landmines)**:
1. **GIL (FR-007)**: the callback runs on an asio worker thread Python doesn't own; touching any Python
   object without `PyGILState_Ensure` corrupts the interpreter.
2. **Callable lifetime (FR-013)**: without `Py_INCREF`, a callable that goes out of Python scope is freed
   while the native session can still invoke it → UAF on the worker thread. This is the minimal slice of
   PY-004 the callback path forces.
3. **Borrowed message (FR-014)**: the `const fixpp_msg_t*` is valid only for the dispatch window
   (session.h callback contract). The proxy MUST be **non-owning** (`own=0`) and the test reads the field
   **inside** the callback. Stashing the msg for later → UAF (ASan-catchable; happy path hides it). Escape
   would require `fixpp_msg_clone` — out of scope for the thin test.

A director is the wrong tool (it's for C++ virtual classes; this is a flat C function pointer).

**Alternatives**: SWIG director — rejected (no C++ class here). Borrowing the callable without INCREF —
rejected (UAF). Owning msg proxy — rejected (double-free / UAF on the borrowed view).

## D-6 — Static link model (self-contained extension)

**Decision**: Link the **static** `fixpp_capi` archive into `_fixpp.so` (the archive must be `-fPIC` to go
into a shared MODULE), plus `-static-libstdc++ -static-libgcc` on Linux. PY-001 proves the **link
compiles and the extension imports/round-trips**; the **portability outcome** (runs on any glibc distro
with no toolchain) is verified at PY-005 with a clean-environment wheel.

**Rationale**: static-libstdc++ is safe here precisely because **no C++ type or exception crosses the
`extern "C"` boundary** — the C-ABI thunks catch-all and expose only PoD/`extern "C"`, so the C++ stdlib is
fully resolved inside the extension and never reaches Python. That is why there is no "two libstdc++ in one
process" footgun. Confirm `fixpp_capi` is built `-fPIC` (it is STATIC and also feeds a SHARED variant per
`src/capi/CMakeLists.txt`; verify the PIC property reaches the static archive used by the MODULE).

**Open verification (implement-time)**: whether the MODULE must link `fixpp_capi` (static, PIC) directly
or whether the `BUILD_TESTING`-only `fixpp_capi_shared` is currently what `bindings/python/CMakeLists.txt`
resolves. The current CMakeLists links `fixpp_capi`; PY-001 confirms the static archive links cleanly into
the MODULE and adds the static-stdlib flags.

**Alternatives**: dynamic `libfixpp_capi.so` next to the extension — rejected (FR-010 self-containment; a
runtime `.so` dependency defeats the wheel model). Defer static-stdlib entirely to PY-005 — rejected (one
CMake line; surfacing link/symbol issues early is cheap and on-theme for the freeze validator).

## D-7 — Dictionary & message-type / field choice

**Decision**: Load `dictionaries/FIX44.xml` via `fixpp_dict_load_from_xml`. The round-trip sends one FIX
4.4 **application** message carrying a single **scalar string** field and asserts it on receipt. Pick a
msg-type with **no required repeating group** and a simple scalar tag (candidate at implement time, e.g. a
type whose grammar admits a sparse body); avoid News/Email (carry `NoLinesOfText`).

**Rationale**: `fixpp_msg_commit` may enforce group grammar (`FIXPP_ERR_TYPE_MISMATCH` on a violated/empty
group — message.h:376); inbound dictionary validation defaults OFF (041), so a sparse, group-free message
round-trips. Confirm at implement time that `commit` does not enforce required-field presence for the
chosen type; if it does, switch to a type with no required group.

**Alternatives**: FIXT.1.1 + FIX 5.0 SP2 — deferred to the planned follow-on (two dicts + `1137`
negotiation; not thin). A repeating-group message — deferred (Q2: scalar-only).

**Implement-time choice (T002, 2026-06-26)**: **MsgType `D` (NewOrderSingle, `msgcat='app'`)**, scalar
string field **`ClOrdID` (tag 11, `type='STRING'`)** — verified against the bundled `dictionaries/FIX44.xml`
(`NewOrderSingle msgtype='D'` at :326; `field number='11' name='ClOrdID' type='STRING'` at :3753). This
mirrors the 050 gold reference (`capi_loopback_support.hpp::make_app_payload` sends `35=D` + `11=…`).
Sparse-body commit is accepted for the missing required fields/components: `fixpp_msg_commit`'s only
grammar enforcement is **group shape** (`FIXPP_ERR_TYPE_MISMATCH` on an empty / non-delimiter-first group
instance — `message.h:376`); it does **not** enforce required scalar-field or required-component presence,
and inbound dictionary validation defaults OFF (041). `D` carries no required repeating group (its required
`Instrument` is a scalar component-block, not a `NoXxx` group), so a body of just `35=D\x01 11=<value>\x01`
commits, sends, and — being an application MsgType (`is_admin_msgtype("D")==false`) — routes to the
acceptor's receive callback.

## D-8 — Send path: outbound builder, not raw bytes

**Decision**: Build the outbound message through the C-ABI outbound builder
(`fixpp_msg_create_outbound` → `fixpp_msg_set_string` → `fixpp_msg_commit` → `fixpp_session_send` →
`fixpp_msg_destroy`) rather than hand-assembling a `35=…\x01…` byte payload in Python.

**Rationale**: `fixpp_session_send` takes a committed byte span (session.h:248), and the builder is the
public, dictionary-checked way to produce it. As the freeze validator, PY-001 should exercise the **outbound
construction surface** (create/set/commit) that a real Python consumer uses — that is where an ergonomic or
ownership flaw would surface while it's still free to fix at a MINOR. The builder is `requires-session-lock`
reentrancy — called from the initiator's consumer thread, not from inside a callback.

**Alternatives**: hand-built byte payload — thinner but skips the outbound builder (less validator value)
and re-implements framing rules in Python; rejected.

## D-9 — SC-004 sanitizer evidence

**Decision** (amended post-Gate-A by the SC-004 update): SC-004 is satisfied by passing the round-trip
under an **AddressSanitizer build of the extension AND a ThreadSanitizer leg over the GIL-trampoline worker
path**, both **wired into the Tier-1 `python-bindings` CI job** (not local-only). This satisfies constitution
Article IX §2 for the binding's trampoline every PR. A local ASan pre-PR run remains useful but is no longer
the gate of record.

> **Superseded note**: the original D-9 decision (local-only ASan; CI stays non-sanitized; "TSan not
> required for PY-001"; CI-sanitized Python deferred to PY-002) was overturned by the SC-004 amendment.
> The IX §2 obligation applies to the binding's worker-thread trampoline now, not at PY-002.

**Rationale**: the GIL trampoline + callable-lifetime + borrowed-msg path is the single riskiest surface in
PY-001 — ASan catches the FR-013/FR-014 UAF classes and TSan covers the FR-007 worker-thread GIL race.
Python-under-ASan needs `-fsanitize=address` on the extension, the ASan runtime preloaded, and
`ASAN_OPTIONS=detect_leaks=0` (CPython has known benign leaks). The TSan leg is a **separate, mutually
exclusive** build (`-fsanitize=thread`) and needs a CPython suppressions file (the interpreter itself is not
TSan-instrumented — suppress interpreter-internal reports, keep trampoline reports live). UBSan is omitted
for the extension leg (CPython C-API aliasing generates UBSan noise; ASan+TSan cover the riskiest surfaces) —
record that omission as a verify-doc waiver.

**Alternatives**: local-only sanitizer (original D-9) — rejected by the SC-004 amendment (IX §2 binds the
trampoline in CI). No sanitizer at all — rejected (would leave the riskiest surface unwitnessed; Gate A
would press it).

---

## Resolved unknowns

No `NEEDS CLARIFICATION` markers remain. The three clarify decisions (FIX 4.4; scalar-only; CI-once +
local-stress) and the nine research decisions above fully constrain the design. Implement-time confirmations
to perform (not blockers): exact `reset_seqnum_policy`/`role` enum values from `session.h`; the chosen
msg-type's grammar vs `commit`; and the `fixpp_capi` PIC/static-link resolution into the MODULE.
