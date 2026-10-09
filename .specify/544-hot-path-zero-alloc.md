# fixpp#544 — zero allocation on the Active inbound read and the `MemoryStore` write (B35)

> **Status: v0.1 — design decided by the owner, Gate A PENDING (round 1 not yet run).**
>
> **How this design was reached.** The owner replaced design→Gate A with two independent consults: Codex
> `gpt-5.6-sol` (read-only) and Fable (`fable-consult`). Both had the same brief, and the convergence criteria
> were written before either answer was read. They **did not converge**: they differed on how the lock frame
> is removed, on the public-header impact, and on how the executor fix is constructed. Per the owner's
> instruction, the owner then decided the design (§0), and `/gate-a` runs on this note.
>
> The records are in the parent's `research/G19-fix-fpml-iso20022/research/reviews/`:
> `b35_544_design_brief.md`, `b35_544_design_convergence_criteria.md`,
> `codex_544_design_consult_gpt-5.6-sol.md`, `fable_544_design_consult.md`,
> `b35_544_design_reconciliation.md`. The executor probe is
> `research/G19-fix-fpml-iso20022/research/probes/b35_544_executor_alloc_probe.cpp`.
>
> **Independence (`[const §XVII.3]`).** The author of this note is the Opus orchestrator. Gate A's Codex
> review runs in a fresh Codex session, which has not seen the consult session.
>
> **Issue:** fixpp#544, including its three comments and the owner scope ruling of 2026-10-08. **Batch:** B35.
> **Tree:** branch `544-hot-path-zero-alloc` from `main` at `d51ce86d`. **Dependency:** asio **1.38.0**
> (`conanfile.py`). #544, L-497-1 and several header comments cite 1.36; every asio citation below is from 1.38.
>
> **Trigger (`[const §XVII.1]`).** This touches the executor model and public C++ headers:
> `include/fixpp/core/sync/async_mutex.hpp`, `include/fixpp/session/memory_store.hpp` and
> `include/fixpp/session/engine.hpp`. No C-ABI symbol or layout changes, so it is not BREAKING under
> `[const §X.7]`.
>
> **Amends (each with an erratum or annotation in the PR).** `.specify/2f-async-mutex.md` gains erratum
> **E-6** (§2.3). `.specify/2d-threading.md` gets an annotation on the strand's inner executor type (§2.1).

---

## 0. Owner rulings (2026-10-09). Not reopened at Gate A; Gate A reviews whether this note implements them.

| # | Question | Ruling |
|---|---|---|
| R-1 | How the lock frame and the 2-slot cache are handled | **Internal frameless lock op.** A `fixpp::sync::detail` entry returns a `deferred` lock operation that hot-path callers `co_await` directly. The public `async_mutex::async_lock` signature is unchanged. Rejected: a PUBLIC `ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE`, and changing `async_lock`'s public return type. |
| R-2 | Which executors must reach zero | **The `io_context` fast path.** When the Engine's executor targets `asio::io_context::executor_type`, the session strand is `strand<io_context::executor_type>` and reads zero. Any other executor keeps today's path, which is disclosed. Rejected: a generic fixpp executor wrapping any caller executor. |
| R-3 | Platforms that must prove zero | Superseded by R-3′. |
| R-3′ | Platforms that must prove zero, given R-4 | **Three Release presets.** mallocnesia gates `linux-clang-release` and `linux-gcc-release`, and the CI mallocnesia step is widened to `linux-gcc-release`. `windows-msvc-release` gates the `operator new` half with the TU-local counter, and its `_aligned_malloc` half is disclosed. No sanitizer-hook or `_CrtSetAllocHook` instrument is built in B35. Debug, sanitizer and libc++ lanes run the cells as **diagnostics**. libc++ is disclosed: it has no Release preset. |
| R-4 | Debug (`-O0`) presets | **Zero is gated on Release presets only.** |
| R-5 | TLS per-record allocation | **Orchestrator default, not yet an owner ruling.** Measure W-A on a TLS rig in the RED step (§3). If OpenSSL allocates per record (asio sets `SSL_MODE_RELEASE_BUFFERS`, `asio/ssl/detail/impl/engine.ipp`), file a new issue; it is outside B35. |

## 1. The three allocation classes and the mechanism for each

The mechanisms are re-derived from asio 1.38. #544's text, its comments and L-497-1 are leads.

- **(a) Executor type erasure** — 5 `operator new` calls per Active read.
  - `any_io_executor` stores a target inline only if it fits its `object_type`: `sizeof(shared_ptr<void>) +
    sizeof(void*)` (`asio/execution/any_executor.hpp`, `object_type`). Otherwise `shared_target_executor`
    does `new impl<E>`.
  - A polymorphic query whose result is a class type (`execution::blocking`) `new`s the result
    (`query_fn_non_void`).
  - `strand<any_io_executor>` is too large to store inline, and its inner executor is erased. Each
    `prefer(outstanding_work.tracked)` in `detail/handler_work.hpp` therefore costs two `new`s, and the strand
    dispatch's `query(inner, blocking)` in `detail/impl/strand_executor_service.hpp` costs one.
  - **Probe** (2026-10-09; g++, clang++ libstdc++, clang++ libc++; `-O2`; recipe in the probe file's
    header):
    - `prefer` on `any_io_executor{strand<any_io_executor>}` costs 2 news; on
      `any_io_executor{strand<io_context::executor_type>}` it costs 0;
    - `query(strand<any>, blocking)` costs 1; `query(strand<ioc exec>, blocking)` costs 0.
    - This reproduces #544's count and shows the fix reaching zero. **Re-run the probe; do not cite its
      numbers.**
- **(b) Coroutine frames that miss asio's per-thread recycler.**
  - `asio::awaitable` frames are allocated by `thread_info_base::allocate<awaitable_frame_tag>`
    (`asio/impl/awaitable.hpp`, `awaitable_frame_base::operator new`). A block is recycled only when
    `size <= chunk_size * UCHAR_MAX`. `chunk_size` is 4 unless `ASIO_HAS_IO_URING` is defined; no preset
    defines it.
  - There are **`ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE` (default 2) slots per purpose**
    (`asio/detail/thread_info_base.hpp`). A request that fits no cached block evicts one, then calls
    `aligned_new`. That is `std::aligned_alloc` on Linux and `_aligned_malloc` on MSVC
    (`asio/detail/memory.hpp`).
  - Steady-state zero therefore needs two things:
    - every cycled frame fits under the limit;
    - the number of cycled frames live at once is at most the slot count.
- **(c) The `use_awaitable` adapter frame.**
  - `async_result<use_awaitable_t>::initiate` is itself a coroutine (`asio/impl/use_awaitable.hpp`, `initiate`).
    So every `co_await op(..., use_awaitable)` costs one extra awaitable frame on top of the awaiting frame.
  - `async_mutex::async_lock` is a coroutine that does exactly this. That is **two frames per lock**.
  - `asio::awaitable` can instead `co_await` an async operation directly: `await_transform` for
    `is_async_operation` → `awaitable_async_op`, in `asio/impl/awaitable.hpp`. That form initiates from the
    awaiting frame with **no** intermediate coroutine. `asio::deferred` produces such an operation.

## 2. Design

### 2.1 Class (a): the session strand over the concrete `io_context` executor (R-2)

- **Strand construction sites:**
  - `src/session/engine.cpp` (`entry.session_strand.emplace(asio::make_strand(exec_))`);
  - `src/session/session_executor.cpp` (`any_io_executor{asio::make_strand(resolved_exec)}`).
- Each site goes through one new internal helper, `make_session_strand(asio::any_io_executor const&) ->
  asio::any_io_executor`:
  - if `exec.target<asio::io_context::executor_type>()` is non-null, it returns
    `any_io_executor{asio::make_strand(*target)}`;
  - otherwise it returns `any_io_executor{asio::make_strand(exec)}`, today's behaviour, which is disclosed.
- The helper lives in `src/`. The Engine's `control_strand_` is not on the read path and is unchanged.
- **The socket type is unchanged.** Transports keep `asio::any_io_executor`. The type the erased executor
  stores changes, and its static queries resolve without allocating. No transport or plugin interface
  changes.
- **`SessionEntry::session_strand`** (`include/fixpp/session/engine.hpp`) changes from
  `std::optional<asio::strand<asio::any_io_executor>>` to `std::optional<asio::any_io_executor>`.
  - `SessionEntry` is in an installed header, but it is held only in `Engine`'s private `registry_`, and no
    public member exposes it. Re-derive that with `grep -n "SessionEntry\|session_strand"
    include/fixpp/session/engine.hpp`, reading only the `public:` span.
  - This is a C++ layout change to `Engine`, not a supported-API change. Disclosed in B&L.
  - `assert_transport_on_session_strand` keeps comparing executors with `operator==`, which is target-type
    equality and then the strand's `impl_` identity.
  - Every in-tree user is updated (tests, `bench/threading/bench_threading.cpp`). Re-derive the list with
    `git grep -n session_strand`.
- **The equality of the session executors has to hold**: `session_executor`'s wrapped executor and the
  transport's executor must compare equal to `session_strand`. Both must come from the same helper call on the
  same session. Gate A checks that no site re-wraps.
- **MSVC.** Under Itanium, `io_context::executor_type` is a `uintptr_t` plus two empty bases, so it is 8 B and
  `strand<>` is 24 B. MSVC applies EBO only to the first empty base, so the size is a prediction there.
  - **RED task 1** compiles `static_assert(sizeof(asio::strand<asio::io_context::executor_type>) <=
    sizeof(std::shared_ptr<void>) + sizeof(void*))` with cl.exe in the sandbox.
  - If the assert fails, the helper uses an internal fixpp 8-B executor in `src/`. It holds an
    `io_context*` with the property bits in its spare low bits, forwards `execute` / `query` / `require` /
    `prefer` to a temporary `io_context::executor_type`, and uses `on_work_started/finished` for the tracked
    variant. It is public asio API only, with no `asio::detail`. This stays inside R-2's ruling.
- **Pins:**
  - the `static_assert` above, in the new test TU;
  - a runtime inline oracle: for an `any_io_executor e` holding the session strand,
    `e.target<asio::strand<asio::io_context::executor_type>>() != nullptr`. Gate A to check whether a
    stronger "stored inline" oracle exists through asio's public surface; Fable proposed
    `target_ == &object_`, whose members are public but undocumented.

### 2.2 Class (b): `Session::on_inbound_frame` split so the Active path's frames fit and stay ≤2 deep

- `on_inbound_frame` becomes a **non-coroutine** function returning `asio::awaitable<…>`. It selects a
  per-state coroutine (`on_inbound_active_`, `on_inbound_logon_`, …) and returns that coroutine's awaitable,
  which the read pump `co_await`s. The dispatcher adds no frame.
- Every scratch array and every synchronous parse, validate or reply-building block is hoisted out of the
  coroutine arms into **non-coroutine helpers**, so its storage is on the stack and not in a frame. The file
  already uses this shape (`validate_inbound_`). This covers the Reject/Logout/reply buffers in the Active and
  Logon arms; re-derive them with `grep -n "std::array<std::byte" src/session/session.cpp`.
- **Target:** on the Active path, every cycled frame fits the recycler limit on the three Release presets, and
  at most two are live at once. After §2.3 that is `on_inbound_active_` → `SeqnumManager::check_inbound`,
  because the lock adds no frame.
  - Frame size is a per-compiler, per-`-O` RESULT, so no size is written in a comment.
  - The instrument is the gate itself: mallocnesia logs every `aligned_alloc(align, N)` it intercepts.
- **Behaviour is unchanged.** This is a pure restructuring: same states, same transitions, same replies.
  - The existing session suites are the behaviour oracle.
  - No test is edited to accommodate the split, except for names that change.

### 2.3 Class (c): `deferred` for hot-path awaited operations, and the frameless lock op (R-1)

- **`deferred` in place of `use_awaitable`** on every awaited operation inside a zero-gated window:
  - the plain transport read (`src/transport/asio_plain_transport.cpp`, `async_read_some` with
    `redirect_error(use_awaitable, ec)`);
  - `MemoryStore`'s leading `asio::post(…, use_awaitable)` in `store()` and its siblings
    (`include/fixpp/session/memory_store.hpp`).
  - The `redirect_error(deferred, ec)` / `as_tuple(deferred)` form must compile against 1.38. That is a
    compile check, not a claim.
  - The TLS transport's read gets the same change for symmetry. Its zero is R-5's measurement.
- **The frameless lock op.** In `include/fixpp/core/sync/async_mutex.hpp`, namespace `fixpp::sync::detail`:
  - `lock_frame`: a caller-owned object holding today's `async_mutex_awaiter` and `result` locals.
  - `async_lock_op(async_mutex&, lock_frame&, std::pmr::memory_resource*)`: returns
    `asio::async_initiate<const asio::deferred_t&, void(expected_t<async_lock_guard>)>(<today's initiation
    lambda>, asio::deferred)`.
  - The initiation lambda moves **verbatim**: the draining check, the fast-path CAS, the waiter-pool record,
    the cancellation-slot install, and the always-posted contended resumption. The 048 reap/drain model is
    unchanged.
  - `async_lock_op`'s completion handler type is `awaitable_async_op_handler<…>`. The awaiter's inline
    `slot_storage_` must fit it, re-pinned by a `static_assert` on that type.
  - The header comment that says "8 B on asio/1.36.0" is replaced by the `static_assert` and a procedure. It
    states no result.
- **The cancellation scope moves to the caller, in the same order as today's `async_lock`.** Every hot-path
  call site is written as:

  ```cpp
  fixpp::sync::detail::lock_frame lf{mutex_};
  co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation{});
  auto r = co_await fixpp::sync::detail::async_lock_op(mutex_, lf, mr);
  co_await asio::this_coro::reset_cancellation_state(asio::enable_terminal_cancellation{});
  fixpp::sync::detail::finish_lock(lf);   // today's post-grant record release, verbatim
  ```

  - The cancellation state is per awaitable **thread** (`awaitable_thread::reset_cancellation_state` writes
    `bottom_of_stack_.frame_->cancellation_state_`). Today's `async_lock` therefore already widens to total
    and then restores to terminal for the whole chain, and this sequence is identical in effect.
  - **Gate A should check this equivalence in particular**, including the `throw_if_cancelled` interaction
    and the window where a `total` arrives between the reset and the initiation.
  - The five lines are wrapped in one internal macro, or one `detail` inline function returning the
    `deferred` op plus an RAII restore, so the sequence cannot drift between sites. Which wrapper is a
    plan-level choice; the requirement is **one** definition.
- **Public `async_mutex::async_lock` is unchanged** in signature and behaviour. It is re-expressed as a thin
  coroutine over `async_lock_op`, so there is one implementation. It keeps its frame, so consumers' cost is
  unchanged.
- **Call sites converted.** Only those inside a zero-gated window:
  - `SeqnumManager`'s inbound and hydrate methods (`src/session/seqnum_manager.cpp`; re-derive from the W-A
    and W-C windows);
  - `MemoryStore::store` and the siblings on W-D's path.
  - FileStore and the outbound `write_gate_` are **not** converted. FileStore is under the §XV.4 exemption,
    and the outbound path is outside B35. Both are listed in B&L.
- **Erratum E-6 to `.specify/2f-async-mutex.md`.** The awaiter now lives in the caller's frame, as E-1
  intended, and not in an `async_lock` coroutine frame. Its header names this note. The
  `brain/components/async-mutex.md` entry gets the same pointer.

## 3. Tests and gates

**RED task 0, before any edit: take the baseline.**
- On this branch's base (`d51ce86d`), run W-A..W-D (below) under mallocnesia (`tools/check_alloc.py`) on
  `linux-clang-release` and `linux-gcc-release`, and record the per-window counts in the verify record.
- The pump window has never been measured under the full counter.
- This baseline is the fix-deleted evidence for §2.2, which has no honest in-tree seam.

**Windows.** Each is warmed until two consecutive iterations report the same count, with a bounded number of
warm-up iterations. Each then measures N iterations, and `ioc.run*()` runs inside the window.
- **W-A.** An Active inbound Heartbeat through the real Engine pump: T044's rig, plain TCP, MemoryStore.
- **W-B.** The same rig with an application message and a registered `fromApp`, so `[const §XV.1]`'s parse →
  validate → **dispatch** runs. A Heartbeat never reaches dispatch.
- **W-C.** Hydrate on MemoryStore, warmed by one hydrate. This is the `W8_NoHeap_RehydratePath` shape.
- **W-D.** `MemoryStore::store` steady state, the `perf_store_alloc_guard` Test 1 shape: pre-built frames, one
  `co_spawn`, `ioc.run()`.
- **FileStore `store()`** is bounded, not zero: at most one frame per offloaded I/O op, the `[const §XV.1]`
  §XV.4 exemption. FileStore `retrieve` is disclosed, not gated.

**Counters and where each one binds (R-3′, R-4):**

| Preset | Counter | Assertion |
|---|---|---|
| `linux-clang-release` (CI today), `linux-gcc-release` (CI step widened) | mallocnesia `_mallocnesia` twins of W-A..W-D, `MALLOCNESIA_MAX_ALLOCS=0` | **0** |
| `windows-msvc-release` | TU-local `operator new` counter (T044's) over W-A..W-D | **0** `operator new`; the `_aligned_malloc` half disclosed |
| every Debug preset, sanitizer and libc++ lanes | the same cells | diagnostic: counts are printed, nothing is asserted |

- A mallocnesia-run binary must not define the allocator names itself: `mallocnesia.c` refuses a process that
  pre-empts them. The TU-local counter and the mallocnesia twin are therefore **separate** binaries or
  registrations.
- New `_mallocnesia` registrations join the population that `tools/check_mallocnesia_population.py` checks.

**Positive controls.** These already exist and must stay in the population:
- mallocnesia: `alloc_guard_aligned_new_positive_control_mallocnesia` and
  `alloc_guard_aligned_alloc_positive_control_mallocnesia`;
- the TU counter: T044's `TheCounterCountsAKnownAllocation`.
- Each control asserts the counter's own non-zero verdict, not CTest `WILL_FAIL` (`tools/check_alloc.py`).

**Fix-deleted arms. Each asserts RED (> 0) on every Release preset where the window is gated:**
- **(a)** A configuration arm with no production seam. The rig's Engine is built on
  `any_io_executor{asio::prefer(ioc.get_executor(), outstanding_work.tracked)}`. That has a different target
  type, so `make_session_strand` takes the fallback. W-A must then read > 0, and the inline oracle must read
  false.
- **(b)** Two planted witnesses inside a gated window:
  - a coroutine holding a buffer larger than the recycler limit across a `co_await asio::post(ex,
    asio::deferred)`. It must allocate at least once per iteration, which proves the counter sees frames and
    pins the limit;
  - `ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE + 1` nested trivial awaitables per iteration. These must allocate,
    which pins the slot budget.
  - Each witness reads the cache-size macro and the computed limit; it does not hardcode them.
- **(b′)** The §2.2 split: the RED-task-0 baseline above.
- **(c)** W-D's shape run through the **public** `async_lock`, which keeps its coroutine frame and the old
  `use_awaitable` post, at the same depth. It must read > 0, while the production W-D reads 0. No production
  seam is needed.

**Superseded pins.**
- Delete `kBaseActiveReadAllocs = 5` and its `EXPECT_LE`
  (`tests/alloc_guard/test_093_pump_active_read_alloc_guard.cpp`), and assert `== 0` on the Release
  presets.
- Resolve the cell's "DELIBERATELY NOT GATED" note in `tests/alloc_guard/CMakeLists.txt`.
- Correct T044's header comment about the per-thread cache: B15 already corrected it once, so re-check it
  against 1.38.
- The cells that L-497-1 lists as "global-heap half unchecked" get their mallocnesia companions back where
  their window is one of W-A..W-D.

**Behaviour.**
- The full session, store, sync and transport suites run on the debug and sanitizer lanes as usual. TSan
  must stay clean across the strand change.
- New semantic cells for the `io_context` fast path:
  - session serialisation, i.e. two handlers on one session never overlap;
  - executor equality between `session_executor`, the transport and `session_strand`;
  - work tracking keeps `run()` alive while a read is pending.

## 4. Disclosures (B&L), documentation and catalogue

- **L-497-1.** Rewrite it to what remains:
  - a non-`io_context` executor takes the allocating path (new row, cross-referencing L-284-1);
  - mixed traffic (Logon, Reject, resend) is bounded, not zero, under asio's first-fit/evict policy;
  - Debug and sanitizer presets are not gated;
  - libc++ has no Release preset;
  - `windows-msvc-release`'s `_aligned_malloc` half is unchecked;
  - FileStore `retrieve`;
  - the outbound path;
  - TLS, pending R-5's measurement.
  - Close what B35 proves, in the B&L-closed file per the B&L workflow.
- **`SessionEntry::session_strand` type change.** Disclosed as a C++ layout change to `Engine`, not a C-ABI
  change.
- **Catalogue S-012's evidence text.** Today it says the global-heap half is unchecked. Re-point it to the
  W-D mallocnesia gate on the two Linux Release presets.
- **`.specify/2d-threading.md`.** Annotate the executor-model paragraph that says the engine never picks a
  concrete executor. The engine still uses the caller's executor, but it now keeps its concrete type when that
  type is `io_context`'s.
- **`[const §XI.6]`** ("HALO-first; PMR fallback per-awaiter"). B35 reaches zero through asio's per-thread
  recycler, the mechanism the shipped code already relies on, not through HALO or PMR. **Gate A decides**
  whether §XI.6 needs an annotation. This note does not amend the constitution.

## 5. Risks Gate A should press on

1. **The cancellation-scope equivalence in §2.3** (the five-line sequence vs today's `async_lock`), including
   seam #17 (LateSignal/GrantOrCancel) and E-5's drain.
2. **Re-erasure.** Any accept, connect, reconnect, timer or SSL construction site that builds a fresh
   `strand<any_io_executor>` around the session silently re-introduces class (a). The (a) arm covers the
   Engine path only. Enumerate every `make_strand` and `strand<` in `src/` mechanically.
3. **Frame-size drift.** A new local or `co_await` temporary on the Active path can push a frame over the
   limit on one compiler only. The Release-preset gates are the only guard. No size is pinned.
4. **Slot budget.** A new nested `co_await` on the Active path, on the W-B dispatch path or in
   `cancellable_dispatch`, exceeds two cycled frames. W-B's RED-task-0 baseline shows whether the dispatch path
   is already deeper. If it is, §2.2's split extends to it before the gate can pass.
5. **Other recyclers on the path.** The cancellation-slot emplacement in `reset_cancellation_state` and
   `inherited_slot.assign` uses `cancellation_signal_tag`; executor ops use `executor_function`. Both have two
   slots under the same policy. The full counter decides.
6. **MSVC layout** (§2.1 RED task 1): the 8-B fixpp executor is the contingency.

## 6. What this does not decide

- The outbound send path (`store_then_emit` → `write_gate_` → `async_write`).
- FileStore's lock call sites.
- A generic executor (rejected by R-2).
- Raising asio's cache size (rejected by R-1).
- A libc++ Release preset.
- Sanitizer and MSVC-debug allocation counters.
- TLS allocation beyond R-5's measurement.
