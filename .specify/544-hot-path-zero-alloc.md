# fixpp#544 — zero allocation on the Active inbound read and the `MemoryStore` write (B35)

> **Status: v0.2 — Gate A round 1: BLOCK — Codex P1=3 P2=8 P3=1; Opus post-judging P1=2 P2=6 P3=12;
> rewritten to v0.2 addressing RC-1..RC-3.** Owner ruling **R-6** (2026-10-09) added to §0; its
> justification is §5. **Gate A is not converged; round 2 is pending.**
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
> `research/G19-fix-fpml-iso20022/research/probes/b35_544_executor_alloc_probe.cpp`. Gate A round 1's
> review and triage are listed under `## Gate A`; the triage's probes (p1–p5) and their build line are in its
> Appendix A.
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
> `[const §X.7]`. The C++ surface changes are disclosed in §4: `SessionEntry::session_strand`'s type, and one
> `FIXPP_DETAIL_` macro that becomes visible to consumers of the installed headers.
>
> **Amends (each with an erratum or annotation in the PR).** `.specify/2f-async-mutex.md` gains erratum
> **E-6** (§2.3). `.specify/2d-threading.md` gets an annotation on the strand's inner executor type (§2.1).
> **No constitution amendment** (R-6, §5).

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
| R-6 | Constitutional status of B35's conditional zero (owner ruling, **2026-10-09**) | **A `[const §VIII.5]` justification, recorded in this note, which serves as the plan in issue mode** (§5). Precedent: `.specify/2f-async-mutex.md` Erratum E-4 and B&L L-006-2. The zero holds only when the Engine's executor is `io_context::executor_type` **and** the handling thread stays inside one scheduler call (`run`-family). The residual configurations — R-2 fallback executors, consumer-driven bounded `run_for`/`run_one_for`/`poll` loops, and mixed-traffic eviction — are tracked as **open B&L limitations**, with **no constitution amendment**. |
| R-7 | Does R-6 cover `MemoryStore::store`, whose clause is `[const §XV.1]`'s MemoryStore sentence with no deviation provision? (owner ruling, **2026-10-09**) | **Yes — owner interpretation.** R-6's `[const §VIII.5]` justification extends to `[const §XV.1]`'s MemoryStore sentence for B35's conditional zero (the same two conditions, the same residuals tracked in B&L). Basis: §XV.1's own v0.2 amendment already records that the asio awaitable frame is opaque to any bound allocator. Recorded in this note only. No issue is filed and the constitution text is unchanged. |

**Implemented forms.** The triage judged no ruling technically unimplementable, but three are implementable
only in a narrowed form (Opus triage, *Owner items* and O-1/O-2/O-3). This note writes those forms:
- **R-1** is implemented through **one macro**, whose first part is the pre-check today's `co_await` performs
  (O-2, C-4; §2.3). Of the two wrappers v0.1 offered, the RAII one is not implementable.
- **R-2's "reads zero"** holds only while the handling thread stays inside one scheduler call (O-1, O-4; §1(b)).
  R-6 records that condition.
- **R-3′'s `windows-msvc-release` row** covers class (a) only: W-A and W-B with arm (a) (O-3; §3).

## 1. The three allocation classes and the mechanism for each

The mechanisms are re-derived from asio 1.38. #544's text, its comments and L-497-1 are leads.

- **(a) Executor type erasure** — `operator new` calls on every Active read. #544 counts them; that count is not
  restated here.
  - `any_io_executor` stores a target inline only if it fits its `object_type` in both size and alignment:
    `sizeof(Executor) <= sizeof(object_type)` and `alignment_of<Executor> <= alignment_of<object_type>`
    (`asio/execution/any_executor.hpp`, `object_type`). Otherwise `shared_target_executor` does `new impl<E>`.
  - A polymorphic query whose result is a class type (`execution::blocking`) `new`s the result
    (`query_fn_non_void`).
  - `strand<any_io_executor>` is too large to store inline, and its inner executor is erased. Each
    `prefer(outstanding_work.tracked)` in `detail/handler_work.hpp` therefore allocates, and so does the strand
    dispatch's `query(inner, blocking)` in `detail/impl/strand_executor_service.hpp`.
  - **Probe** (2026-10-09; g++, clang++ libstdc++, clang++ libc++; `-O2`; recipe in the probe file's header).
    It compares `prefer` and `query` on `any_io_executor{strand<any_io_executor>}` against
    `any_io_executor{strand<io_context::executor_type>}`. On that date it reproduced #544's sites and showed the
    second form allocating nothing. **Re-run the probe; do not cite its numbers.**
- **(b) Coroutine frames that miss asio's recycler. The recycler belongs to the scheduler call, not to the
  thread.**
  - `asio::awaitable` frames are allocated by `thread_info_base::allocate<awaitable_frame_tag>`
    (`asio/impl/awaitable.hpp`, `awaitable_frame_base::operator new`). There is no allocator customisation point
    for the frame.
  - A block is recycled only when `size <= chunk_size * UCHAR_MAX`. `chunk_size` is 4 unless
    `ASIO_HAS_IO_URING` is defined; no preset defines it.
  - There are **`ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE` (default 2) slots per purpose**
    (`asio/detail/thread_info_base.hpp`). A request that fits no cached block evicts one, then calls
    `aligned_new`. That is `std::aligned_alloc` on Linux and `_aligned_malloc` on MSVC (`asio/detail/memory.hpp`).
  - **Scope.** The cache lives in a `thread_info` object that each scheduler call declares on its own stack and
    pushes as the thread's call-stack context:
    - `scheduler::run`, `run_one`, `wait_one`, `poll` and `poll_one` (`asio/detail/impl/scheduler.ipp`,
      `thread_info this_thread;`);
    - the IOCP scheduler's equivalents (`asio/detail/impl/win_iocp_io_context.ipp`,
      `win_iocp_thread_info this_thread;`).
    - The cache is empty at the start of each call. `~thread_info_base` frees every cached block when the call
      returns.
    - `io_context::run_for`, `run_until` and `run_one_for` loop over `run_one_until`, which calls `wait_one` per
      handler (`asio/impl/io_context.hpp`). So a bounded run starts **every handler** with an empty cache.
    - With no scheduler call on the stack, `this_thread` is null and every request goes to `aligned_new`.
    - The same scope applies to every purpose: `cancellation_signal_tag`, `executor_function_tag` and the others.
      Erratum E-4's "per-thread" wording is therefore wrong in the same way (§4).
  - **So steady-state zero needs all three of these:**
    1. every cycled frame fits under the limit;
    2. the number of cycled frames live at once is at most the slot count;
    3. the handling thread stays inside **one** scheduler call across the messages.
  - Condition 3 is what the triage's p3 measured on 2026-10-09: the same workload under one `ioc.run()` against
    `run_one_for` and `poll_one` loops (triage Appendix A, recipe there).
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
  - `src/session/session_executor.cpp` (`any_io_executor{asio::make_strand(resolved_exec)}`), the non-Engine
    path. The Engine path does not reach it: `engine.cpp` hands the Session its strand through
    `entry.config.engine_adopt_strand`, with no second wrap.
- Each site goes through one new internal helper, `make_session_strand(asio::any_io_executor const&) ->
  asio::any_io_executor`:
  - if `exec.target<asio::io_context::executor_type>()` is non-null, it returns
    `any_io_executor{asio::make_strand(*target)}`;
  - otherwise it returns `any_io_executor{asio::make_strand(exec)}`, today's behaviour, which is disclosed.
  - The test is **exact target type**. A work-tracked or custom-allocator `io_context` executor has a different
    target type and takes the fallback too. Arm (a) relies on this, and the B&L row says so (§4).
- The helper lives in `src/`. The Engine's `control_strand_` is not on the read path and is unchanged.
- **The socket type is unchanged.** Transports keep `asio::any_io_executor`. The type the erased executor
  stores changes, and its static queries resolve without allocating. No transport or plugin interface
  changes.
- **`SessionEntry::session_strand`** (`include/fixpp/session/engine.hpp`) changes from
  `std::optional<asio::strand<asio::any_io_executor>>` to `std::optional<asio::any_io_executor>`.
  - `SessionEntry` is a namespace-scope struct in an installed header, so **consumers can name it**: they can
    instantiate it, aggregate-initialise it, read the field, and take its `sizeof`. No public member of `Engine`
    takes or returns it; `Engine` holds it only in the private `registry_`, an `unordered_map`. Re-derive both
    with `grep -n "SessionEntry\|session_strand\|registry_" include/fixpp/session/engine.hpp`.
  - The change is therefore a **C++ source and layout change to `SessionEntry`** (its member type and `sizeof`).
    `sizeof(Engine)` does not change, because the entries live in the map's nodes.
  - The constitution states no C++ API stability promise; Article X covers the C ABI only. The change is
    disclosed in B&L (§4).
  - `engine.cpp`'s `entry.config.engine_adopt_strand = asio::any_io_executor{*entry.session_strand}` becomes a
    plain copy of the stored executor.
  - **`assert_transport_on_session_strand`** (`src/session/engine.cpp`) changes its parameter type from
    `const asio::strand<asio::any_io_executor>&` to `const asio::any_io_executor&`. It keeps comparing with
    `operator==`, which is target-type equality and then the strand's `impl_` identity.
  - Every in-tree user is updated. Re-derive the list with
    `git grep -nw session_strand -- src include tests bench`. The `-w` matters: without it, the recipe also
    matches the `per_session_strand` threading-mode enumerator, which is how v0.1 came to list
    `bench/threading/bench_threading.cpp`, a file that uses only the enumerator.
- **The equality of the session executors has to hold**: `session_executor`'s wrapped executor and the
  transport's executor must compare equal to `session_strand`. Both must come from the same helper call on the
  same session. Gate A checks that no site re-wraps (§6 risk 2).
- **MSVC.** On Itanium, `strand<io_context::executor_type>` meets both inline conditions (triage Appendix A p5;
  recipe there). MSVC applies EBO only to the first empty base, so on MSVC that is a prediction.
  - **RED task 1** compiles both of these with cl.exe in the sandbox:
    - `static_assert(sizeof(asio::strand<asio::io_context::executor_type>) <= sizeof(std::shared_ptr<void>) +
      sizeof(void*))`;
    - an `alignof` `static_assert` against `alignof` of the same `object_type`.
  - If either fails, the helper uses an internal fixpp executor in `src/` that meets both conditions. It holds
    an `io_context*` with the property bits in its spare low bits, forwards `execute` / `query` / `require` /
    `prefer` to a temporary `io_context::executor_type`, and uses `on_work_started/finished` for the tracked
    variant. It is public asio API only, with no `asio::detail`. This stays inside R-2's ruling.
- **Pins:**
  - the two `static_assert`s above, in the new test TU, on every compiler;
  - a **target-type oracle**: for an `any_io_executor e` holding the session strand,
    `e.target<asio::strand<asio::io_context::executor_type>>() != nullptr`.
    - It identifies the stored type only. It cannot tell inline storage from `shared_target_executor`.
    - The runtime proof of inline storage is the zero-allocation window itself (W-A), together with arm (a)
      reading > 0 on the fallback.
    - The undocumented `target_ == &object_` comparison is not used.

### 2.2 Class (b): `Session::on_inbound_frame` split so the Active path's frames fit and stay ≤2 deep

- `on_inbound_frame` becomes a **non-coroutine** function returning `asio::awaitable<…>`. It selects a
  per-state coroutine (`on_inbound_active_`, `on_inbound_logon_`, …) and returns that coroutine's awaitable,
  which the read pump `co_await`s. The dispatcher adds no frame. Legality: triage Appendix A p5.
- **Parameter lifetime.** asio awaitables start suspended, so a selected arm starts running only after the
  dispatcher has returned. **Every arm takes every parameter by value; the only reference it may hold is
  `*this`.** Today's coroutine already takes `std::span<const std::byte> frame` by value and has no locals
  before its `switch`, so the rule preserves current behaviour.
- Every scratch array and every synchronous parse, validate or reply-building block is hoisted out of the
  coroutine arms into **non-coroutine helpers**, so its storage is on the stack and not in a frame. The file
  already uses this shape (`validate_inbound_`). This covers the Reject/Logout/reply buffers in the Active and
  Logon arms; re-derive them with `grep -n "std::array<std::byte" src/session/session.cpp`.
- **Target:** on the Active path, every cycled frame fits the recycler limit on the three Release presets, and
  at most two are live at once. After §2.3 that is `on_inbound_active_` → `SeqnumManager::check_inbound`,
  because the lock adds no frame.
  - §2.3 puts a `lock_frame` into each converted caller's frame. That frame grows, and must still fit (§6 risk 3).
  - Frame size is a per-compiler, per-`-O` RESULT, so no size is written in a comment.
  - The instrument is the gate itself: mallocnesia logs every `aligned_alloc(align, N)` it intercepts.
- **Behaviour is unchanged.** This is a pure restructuring: same states, same transitions, same replies.
  - The existing session suites are the behaviour oracle.
  - No test is edited to accommodate the split, except for names that change.

### 2.3 Class (c): `deferred` for hot-path awaited operations, and the frameless lock op (R-1)

- **`deferred` in place of `use_awaitable`** on every awaited operation inside a zero-gated window:
  - the plain transport read (`src/transport/asio_plain_transport.cpp`, `async_read_some` with
    `redirect_error(use_awaitable, ec)`);
  - `MemoryStore::store`'s leading `asio::post(…, use_awaitable)` (`include/fixpp/session/memory_store.hpp`).
  - The `redirect_error(deferred, ec)` / `as_tuple(deferred)` forms compile and complete against 1.38 (triage
    Appendix A p5). The PR's build is the check.
  - The TLS transport's read gets the same change for symmetry. Its zero is R-5's measurement.
- **The frameless lock op.** In `include/fixpp/core/sync/async_mutex.hpp`, namespace `fixpp::sync::detail`:
  - `lock_frame`: a caller-owned object holding today's `async_mutex_awaiter` and `result` locals.
  - `async_lock_op(async_mutex&, lock_frame&, std::pmr::memory_resource*)`: returns
    `asio::async_initiate<const asio::deferred_t&, void(expected_t<async_lock_guard>)>(<initiation>,
    asio::deferred)`.
  - **The initiation body moves with exactly two non-verbatim lines.** Today's lambda captures `bound_executor`
    (from `co_await this_coro::executor`) and `inherited_slot` (from `this_coro::cancellation_state` after the
    total reset). A deferred initiation has neither. It takes both from the supplied handler before
    `awaiter.store_handler(std::move(handler))`:
    - `asio::get_associated_executor(handler)`;
    - `asio::get_associated_cancellation_slot(handler)`.
    - The handler is an `awaitable_async_op_handler` bound to the awaiting thread, so these are the thread's
      executor and its cancellation slot after step 2 below (`asio/impl/awaitable.hpp`).
    - Everything else moves verbatim: the draining checks, the fast-path CAS, the waiter-pool record, the
      cancellation-slot install with its fail-closed `assign`, and the always-posted contended resumption. The
      048 reap/drain model (E-5) is unchanged.
  - `async_lock_op`'s completion handler type is `awaitable_async_op_handler<…>`. It must fit the awaiter's
    inline `slot_storage_`. The generic `static_assert`s inside `store_handler` already pin size and alignment
    for whatever handler is stored. They are kept, and the PR instantiates them for this type.
  - The header comment that says "8 B on asio/1.36.0" is replaced by a pointer to those `static_assert`s. It
    states no result.
- **The caller-side sequence: one macro, the only definition.** `reset_cancellation_state` exists only as a
  promise `await_transform`, so a destructor cannot restore the filter. That rules out v0.1's
  "inline function plus RAII restore" alternative. The sequence is one macro, defined once in
  `async_mutex.hpp`. It has five parts, in this order:

  ```cpp
  // Sketch of the single definition; names are plan-level. Callers supply the local names,
  // so two uses in one scope cannot collide.
  #define FIXPP_DETAIL_CO_AWAIT_LOCK(lf, out, mutex, mr)                                    \
      ::fixpp::sync::detail::lock_frame lf{mutex};                                          \
      /* 1. pre-check: the check today's co_await of an awaitable performs */               \
      if ((co_await ::asio::this_coro::throw_if_cancelled()) &&                             \
          !!(co_await ::asio::this_coro::cancellation_state).cancelled())                   \
          throw ::asio::system_error(::asio::error::operation_aborted, "co_await");         \
      /* 2. */ co_await ::asio::this_coro::reset_cancellation_state(                        \
                   ::asio::enable_total_cancellation{});                                    \
      /* 3. deferred op; executor and slot are taken from its handler */                    \
      auto out = co_await ::fixpp::sync::detail::async_lock_op(mutex, lf, mr);              \
      /* 4. */ co_await ::asio::this_coro::reset_cancellation_state(                        \
                   ::asio::enable_terminal_cancellation{});                                 \
      /* 5. today's post-grant record release, verbatim */                                  \
      ::fixpp::sync::detail::finish_lock(lf)
  ```

  - **Why part 1 exists (O-2).** Today, `co_await mutex_.async_lock()` is a `co_await` of an `awaitable`. Its
    `await_transform` throws `operation_aborted` when `throw_if_cancelled` is set and the thread's state is
    already cancelled (`asio/impl/awaitable.hpp`, `await_transform(awaitable<T, Executor>)`).
    `reset_cancellation_state` performs no such check, and it replaces the state, clearing `cancelled()`.
    - Without part 1, a terminal cancellation that lands while `store()` is suspended in its leading post is
      erased by part 2. The lock is acquired, part 4 leaves a clean state, and the one-shot signal is never
      re-delivered.
    - `store_then_emit`'s `catch (const asio::system_error&)` → `dispatch_aborted` branch
      (`src/session/session.cpp`) would then be unreachable for that window.
    - Part 1 throws the type that branch catches: `asio::system_error`, which is `std::system_error` in
      standalone asio, carrying `operation_aborted`. It uses public asio only; `asio::detail::throw_error` is
      not used in an installed header.
    - Triage Appendix A p1 reproduced both the erasure and the restoration by this pre-check (recipe there).
  - **Equivalence with today's `async_lock`, part by part.** The cancellation state is per awaitable thread:
    `awaitable_thread::reset_cancellation_state` writes `bottom_of_stack_.frame_->cancellation_state_`. Today's
    `async_lock` therefore already widens the filter to total and then restores it to terminal for the whole
    chain.
    - Part 1 is today's caller-side `await_transform` check.
    - Parts 2 and 4 are today's two resets.
    - Part 3's awaitable `co_await` performs its own `await_transform` check after part 2. That matches today's
      `co_await async_initiate(…, use_awaitable)` inside `async_lock`, which also ran after the reset.
    - Part 5 is today's tail.
    - At the SeqnumManager sites the lock is the first `co_await`, so part 1 repeats the check the caller's own
      `co_await` of the method has just performed, with no suspension in between. It is redundant there and
      harmless. At `MemoryStore::store` it is load-bearing.
  - **The restore stays unconditional, as today.** Part 4 resets to terminal whatever filter the caller entered
    with. A caller that entered with a wider filter leaves the lock with terminal. A cell pins this (§3), so
    that a change is a decision and not an accident.
  - **Lifetime (C-5).** `lock_frame` lives in the caller's frame, from the macro to the end of that frame's scope.
    That is longer than today's `async_lock` frame, never shorter. If the caller's frame is destroyed while it
    is suspended at part 3, the awaiter's destructor runs the same E-2 path that runs today when `async_lock`'s
    frame is destroyed. `finish_lock` leaves the awaiter in the state today's tail leaves it in
    (`record_ == nullptr`), so the destructor does not release a second time.
  - **Macro hygiene.** A macro in an installed header is not namespaced and cannot be `#undef`'d after use,
    because `memory_store.hpp`'s inline `store()` expands it in consumers' TUs. It is therefore prefixed
    `FIXPP_DETAIL_`, documented as not supported API, and disclosed (§4).
- **Public `async_mutex::async_lock` is unchanged** in signature and behaviour. It is re-expressed as a thin
  coroutine over the macro, so there is one implementation. It keeps its frame, so consumers' cost is unchanged.
- **Call sites converted.** Only those inside a zero-gated window:
  - `SeqnumManager::check_inbound` (W-A, W-B) and `SeqnumManager::hydrate` (W-C) (`src/session/seqnum_manager.cpp`).
    The other SeqnumManager methods stay on the public `async_lock`.
  - `MemoryStore::store` (W-D). `next_seqnum`, `reset`, `reset_to` and `retrieve` stay on the public
    `async_lock`: no gated window reaches them.
  - FileStore and the outbound `write_gate_` are **not** converted. FileStore is under the §XV.4 exemption,
    and the outbound path is outside B35. Both are listed in B&L.
- **Erratum E-6 to `.specify/2f-async-mutex.md`: a narrow supersession, for internal callers only.**
  - It supersedes E-1's "`async_lock(mr)` is the operation" and E-2's "`async_lock(mr)` coroutine frame"
    lifetime wording, for callers that use the macro.
  - E-1 and E-2's waiter-record, cancellation and lifetime invariants stay as they are.
  - It records that E-2's lifetime argument was re-run with the awaiter living to the end of the caller's frame.
  - It corrects E-4's "per-thread" recycler wording to the scheduler-call scope (§1(b)), without changing E-4's
    disposition.
  - Its header names this note. The `brain/components/async-mutex.md` entry gets the same pointer.

## 3. Tests and gates

**RED task 0, before any edit: take the baseline.**
- On this branch's base (`d51ce86d`), run W-A..W-D in the window shapes below, under mallocnesia
  (`tools/check_alloc.py`), on `linux-clang-release` and `linux-gcc-release`. Record the per-window counts in
  the verify record, with the command.
- The pump window has never been measured under the full counter.
- This baseline is **supporting measurement** only. It cannot attribute anything to §2.2, because classes (a)
  and (c) are present in the same window at the base. §2.2's recurrence arm is (b), planted in W-A's own window.

**The window rule (RC-1).** Warm-up and measurement for every gated window run inside **one** scheduler call on
the thread that runs the session's handlers. A window that spans several `run*` / `poll*` calls starts each one
with an empty cache, so it cannot read zero (§1(b)). Two shapes satisfy the rule:
- **Driver coroutine (W-C, W-D).**
  - One coroutine is `co_spawn`ed **before** arming. Its spawn and its completion lie outside the window, so
    its completion token does not matter.
  - Inside that coroutine, it runs a fixed **K** warm-up iterations of the operation and calls
    `alloc_guard_start()` itself. It then awaits **N** iterations directly, with no `co_spawn` and no
    `use_future` in between. It calls `alloc_guard_end()` and returns.
  - All of it runs under one `ioc.run()`.
  - The existing `W8_NoHeap_RehydratePath` and `perf_store_alloc_guard` shapes are **not reused verbatim**.
    Each spawns with `use_future` inside its window, and `use_future`'s handler `allocate_shared`s its promise
    (`asio/impl/use_future.hpp`). Each window is also a fresh scheduler call:
    - `run_batch` → `run_to_exhaustion_or_report` calls `ioc.run()` per batch;
    - `run_window_then_ready` drives `run_for` slices (`tests/support/pump_until_ready.hpp`).
- **Run thread (W-A, W-B).**
  - A dedicated thread holds one `ioc.run()` under a work guard for the whole test, from rig setup through
    teardown. **It is the only thread that drives the io_context.** If the test thread kept any `run*` or
    `poll*` call, handlers could land in short scheduler calls on that thread, which breaks the scope §1(b)
    depends on.
    - T044's rig reaches Active today through `run_until` on the test thread (`tests/session/plain_engine_rig.hpp`).
      In the new rig, reaching Active is waited on through an atomic bumped from a test-owned `onLogon`.
    - Session state read after the window, such as `next_inbound`, is read only after the run thread has been
      joined, or through a read posted to the session strand.
  - **The peer write.** The rig's peer keeps an async read in flight on its socket (`start_reading()`), run by
    the run thread. A synchronous `asio::write` on that same socket object from the test thread would be a
    concurrent operation on one asio object. So the test thread writes with a raw `::send` on the peer's native
    handle, which it captures before the run thread starts. The alternative is to issue the write from the run
    thread.
  - The test thread pre-builds every frame before arming. For the window it does four things in order: it
    arms; it writes N+1 frames through the peer socket, each after the previous one's completion signal; it
    waits for the N+1-th signal; it disarms.
  - Read cycles 1..N lie wholly inside the window. Cycle *i*'s tail, including the next read's initiation, must
    run before frame *i+1*'s read can complete.
  - **The completion signal must neither allocate nor race.** mallocnesia counts process-wide, so the waiting
    thread must not allocate.
    - `SeqnumManager::next_inbound_unsafe()` is a plain member read (`include/fixpp/session/seqnum_manager.hpp`).
      Polling it from the test thread while the run thread writes is a data race that the TSan diagnostic lane
      would report.
    - The signal is a `std::atomic` that a test-owned Application callback bumps on the session strand:
      `fromApp` for W-B, and `fromAdmin` for W-A.
    - The plan confirms that an in-sequence Active Heartbeat reaches `fromAdmin` (`grep -n "fromAdmin("
      src/session/session.cpp`). If it does not, W-A uses another test-owned hook on the strand.
  - The ported MSVC rig uses the same shape: the IOCP scheduler scopes its cache per call too (§1(b)).
- **Warm-up is a fixed count K**, not "until two counts agree" (O-5). mallocnesia defines only
  `alloc_guard_start` / `alloc_guard_end`, and `alloc_guard_end` exits on a non-zero count, so a twin cannot
  compare counts. One full pass of the window's path inside the same scheduler call primes it. A K that is too
  small fails toward red.
- **Anything the harness adds inside a window fails toward red, never toward green.** A synchronous peer write
  that allocated would read > 0. The plan keeps such additions out of the window, or proves them
  allocation-free.

**Windows.**
- **W-A.** An Active inbound Heartbeat through the real Engine pump: T044's rig, re-shaped to the run-thread
  form above, with plain TCP and MemoryStore.
- **W-B.** The same rig with an application message and a registered `fromApp`, so `[const §XV.1]`'s parse →
  validate → **dispatch** runs. A Heartbeat never reaches dispatch.
  - `fromApp` runs synchronously inside `parse_and_dispatch_` (`src/session/session.cpp`). No
    `cancellable_dispatch` coroutine is on this path: `git grep -n cancellable_dispatch -- src include` finds
    only its definition and an error enumerator.
- **W-C.** `SeqnumManager::hydrate` on a MemoryStore-backed session, in the driver-coroutine form.
- **W-D.** `MemoryStore::store` steady state, in the driver-coroutine form, over pre-built frames.
- **FileStore `store()`** is bounded, not zero: at most one frame per offloaded I/O op, the `[const §XV.1]`
  §XV.4 exemption. FileStore `retrieve` is disclosed, not gated.

**Counters, and where each one binds (R-3′, R-4; MSVC narrowed per O-3):**

| Preset | Windows | Counter | Assertion | Fix-deleted arms run here |
|---|---|---|---|---|
| `linux-clang-release` (CI today), `linux-gcc-release` (CI widened) | W-A..W-D | mallocnesia `_mallocnesia` twins, `MALLOCNESIA_MAX_ALLOCS=0` | **0** | (a), (b), (c), (s) |
| `windows-msvc-release` | W-A, W-B. T044's rig is ported, and its `if(CMAKE_SYSTEM_NAME STREQUAL "Linux")` guard in `tests/alloc_guard/CMakeLists.txt` is lifted for the new cells | TU-local `operator new` counter (T044's) | **0** `operator new`; the `_aligned_malloc` half is disclosed | (a) only |
| every Debug preset, sanitizer and libc++ lanes | the same cells | diagnostic: counts are printed, nothing is asserted | — | — |

- **Why MSVC is class (a) only.**
  - On MSVC, asio's `aligned_new` calls `_aligned_malloc` (`asio/detail/memory.hpp`, the `ASIO_MSVC` branch).
    `ASIO_HAS_STD_ALIGNED_ALLOC` is defined only for clang and GCC (`asio/detail/config.hpp`).
  - An `operator new` counter therefore cannot see classes (b) and (c). Arms (b) and (c) cannot read > 0 there.
  - W-C and W-D contain no class-(a) content, so on MSVC they would assert zero on paths whose B35 defects the
    counter cannot see. They are **not registered on MSVC** as B35 evidence.
- **Release only (O-6).** `fixpp_add_mallocnesia_test` (`cmake/FixppMallocnesia.cmake`) has no build-type
  condition, and `FIXPP_MALLOCNESIA_SUPPORTED` is true on every non-sanitizer glibc build, Debug included.
  - The new twins are registered only for a Release build type, so R-4's Debug lanes stay diagnostic.
  - Alternatively they carry a non-asserting `ENVIRONMENT` on other build types; that choice is plan-level.
- A mallocnesia-run binary must not define the allocator names itself: `mallocnesia.c` refuses a process that
  pre-empts them. The TU-local counter and the mallocnesia twin are therefore **separate** binaries or
  registrations.
- New `_mallocnesia` registrations join the population that `tools/check_mallocnesia_population.py` checks.
- **CI: three coupled edits to `.github/workflows/tier1.yml` (C-10).** All three are needed. Widening only the
  steps makes the sentinel fail on `linux-gcc-release`.
  1. Both guarded steps (`mallocnesia_population` and `mallocnesia_gates`) get an `if:` that admits both Linux
     Release presets.
  2. The unguarded sentinel "Assert the allocation gates actually ran" requires `success` on both presets and
     `skipped` on every other.
  3. Its pin in `ci/test-tier1-python-policy.sh` is updated to match.

**Positive controls.** These already exist and must stay in the population:
- mallocnesia: `alloc_guard_aligned_new_positive_control_mallocnesia` and
  `alloc_guard_aligned_alloc_positive_control_mallocnesia`;
- the TU counter: T044's `TheCounterCountsAKnownAllocation`.
- Each control asserts the counter's own non-zero verdict, not CTest `WILL_FAIL` (`tools/check_alloc.py`).

**Fix-deleted arms. Each asserts > 0 on every preset in its row of the table above.**
- Each mallocnesia arm is registered with `--expect-violation --expect-entry <hook>`. `check_alloc.py` then
  requires at least one hit through that hook, so a hit from a different class cannot satisfy the arm alone.
- **(a)** A configuration arm with no production seam.
  - The rig's Engine is built on `any_io_executor{asio::prefer(ioc.get_executor(), outstanding_work.tracked)}`.
    That has a different target type, so `make_session_strand` takes the fallback.
  - W-A must then read > 0, and the target-type oracle must read false.
  - On Linux the expected entry is `malloc`: `operator new` from executor erasure.
  - On MSVC the TU counter must read > 0.
- **(b)** Two planted witnesses inside **W-A's and W-D's own windows** (Linux only; entry `aligned_alloc`):
  - a coroutine holding a buffer larger than the recycler limit across a `co_await asio::post(ex,
    asio::deferred)`. It must allocate at least once per iteration, which proves the counter sees frames and
    pins the limit;
  - `ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE + 1` nested trivial awaitables per iteration. These must allocate,
    which pins the slot budget.
  - Each witness reads the cache-size macro and the computed limit; it does not hardcode them.
  - Planted in W-A's rig, (b) is §2.2's recurrence arm. The class §2.2 guards against is a frame over the limit
    or a depth above the slot count, and (b) shows the gate sees exactly that class in that window.
- **(c)** W-D's driver coroutine, run through a **test-only replica of today's whole `store()` chain**, with
  **no production seam** (Linux only; entry `aligned_alloc`).
  - `MemoryStore::store` uses the macro, and nothing routes it through another lock. The arm therefore does not
    call `MemoryStore::store`, and no seam is added to `MemoryStore` to make it.
  - The replica is a store-shaped test coroutine. It does a `use_awaitable` post, then awaits a replica lock:
    a test coroutine that holds a `lock_frame` and awaits `async_lock_op(...)` through `use_awaitable`. That
    recreates today's `store → async_lock → use_awaitable initiate` chain.
  - It must read > 0, while the production W-D reads 0.
  - It does **not** use the public `async_lock`. Re-expressed over the macro, that has one frame fewer, and at
    W-D's depth it reads 0 after the fix (triage Appendix A p2).
- **(s) Scope control** (Linux; `--expect-violation --expect-entry aligned_alloc`).
  - It is the W-A workload driven by a `run_one_for` loop instead of the run thread, which is T044's current
    shape. It must read > 0 on the fully fixed tree.
  - It pins §1(b)'s scope. If a future asio moved the cache to the thread, (s) would go green and fail its
    `--expect-violation`.

**Superseded pins.**
- Delete `kBaseActiveReadAllocs` and its `EXPECT_LE` (`tests/alloc_guard/test_093_pump_active_read_alloc_guard.cpp`),
  and assert `== 0` on the Release presets in the run-thread rig.
- Resolve the cell's "DELIBERATELY NOT GATED" note in `tests/alloc_guard/CMakeLists.txt`. The run-thread rig no
  longer wraps `run_one_for`.
- Rewrite T044's header comment. It says "per-thread recycling cache"; the cache is per scheduler call (§1(b)).
- The cells that L-497-1 lists as "global-heap half unchecked" get their mallocnesia companions back where
  their window is one of W-A..W-D, in the window shapes above.

**Behaviour.**
- The full session, store, sync and transport suites run on the debug and sanitizer lanes as usual. TSan
  must stay clean across the strand change and the run-thread rigs.
- New semantic cells for the `io_context` fast path:
  - session serialisation, i.e. two handlers on one session never overlap;
  - executor equality between `session_executor`, the transport and `session_strand`;
  - work tracking keeps `run()` alive while a read is pending.
- **New lock cells for the macro (RC-2).** Each runs against the converted sites, not against the public
  `async_lock` alone.
  - **Pre-initiation cancellation (O-2).** A terminal cancellation emitted on the session's cancel slot while
    `MemoryStore::store` is suspended in its leading post makes `store_then_emit` return `dispatch_aborted`,
    with nothing transmitted. It must be **RED against the macro with part 1 deleted**.
  - **Seam #17, LateSignal/GrantOrCancel.** A total cancellation that arrives after the grant does not abort
    the caller's next `co_await`. This covers part 4's restore.
  - **E-5's drain.** A lock op that races `draining_` completes with `sync_lock_drained`, and the record's
    references balance. This covers part 5 and the moved initiation.
  - **Non-terminal entry filter.** A caller that entered with a wider filter leaves the macro with terminal,
    the same as today's `async_lock`.

## 4. Disclosures (B&L), documentation and catalogue

**B&L rows.** Each residual is stated as a **condition**, with no counts.
- **L-497-1.** Rewrite it to what remains. Move what B35 proves to `spec/behaviors-and-limitations-closed.md`,
  per the B&L workflow. What stays open:
  - **Executor condition (R-6 residual).** When the Engine's executor's target type is not exactly
    `io_context::executor_type`, class (a) allocates on every Active read. That includes any custom executor, a
    `thread_pool` executor, and an `io_context` executor carrying a property such as `outstanding_work.tracked`
    or a custom allocator.
  - **Driver condition (R-6 residual).** When the thread that runs the session's handlers drives the io_context
    through bounded calls (`run_for`, `run_until`, `run_one*`, `poll*`), each call starts with an empty cache,
    so coroutine frames allocate on every message. Cross-reference L-284-1, which names that topology.
    - The C-ABI engine is not affected: it passes `ioc_.get_executor()`, and its workers call `ioc_.run()`
      (`src/capi/engine.cpp`).
  - **Mixed traffic (R-6 residual).** A message whose path cycles a frame the cache cannot hold (Logon, Reject,
    resend) evicts a cached block. The next gated-path message re-primes the cache, so this is bounded, not zero.
  - Debug and sanitizer presets are not gated.
  - libc++ has no Release preset.
  - `windows-msvc-release` checks `operator new` only. Its `_aligned_malloc` half is unchecked, and W-C and W-D
    are not gated there.
  - FileStore `retrieve`.
  - The outbound path.
  - TLS, pending R-5's measurement.
- **L-006-2.** Rewrite "one-time per-thread … warm-up" to the scheduler-call scope. The warm-up is once per
  scheduler call, not once per thread. Its status is unchanged.
- **`SessionEntry::session_strand` type change.** A C++ source and layout change to a public, nameable struct
  (member type, `sizeof`, aggregate initialisation). It is not a C-ABI change.
- **`FIXPP_DETAIL_CO_AWAIT_LOCK`** (final name plan-level) becomes visible to every TU that includes
  `async_mutex.hpp` or `memory_store.hpp`. It is not supported API.

**Documentation and comment edits the PR must make (C-11, RC-1).**

| File | Edit |
|---|---|
| `spec/behaviors-and-limitations.md` | L-497-1 rewritten as above; L-006-2's scope wording; new rows if the B&L workflow prefers one row per R-6 residual |
| `spec/behaviors-and-limitations-closed.md` | the resolved part of L-497-1 |
| `spec/feature-catalogue.md` | S-012's evidence says the global-heap half is unchecked. Re-point it to the W-D mallocnesia gate on the two Linux Release presets |
| `.specify/2f-async-mutex.md` | Erratum E-6 (§2.3), including E-4's scope correction |
| `.specify/2d-threading.md` | annotate the executor-model paragraph that says the engine never picks a concrete executor. The engine still uses the caller's executor, but it keeps its concrete type when that type is `io_context`'s |
| `brain/components/async-mutex.md` | E-6 pointer; the macro is the internal route |
| `brain/components/inbound-message-path.md` | the dispatcher/arm split; W-A/W-B as the gates of its zero-allocation invariant |
| `brain/components/session.md`, `brain/components/transport.md` | the concrete `io_context` strand target and the fallback |
| `brain/components/message-store-quiescence.md` | `MemoryStore::store`'s lock now goes through the macro; the siblings do not |
| `include/fixpp/core/sync/async_mutex.hpp` | the "8 B on asio/1.36.0" comment (§2.3); the E-4 recycler comment's "per-thread" |
| `tests/alloc_guard/test_093_pump_active_read_alloc_guard.cpp`, `tests/perf/test_store_alloc_guard.cpp` | header comments: "per-thread" becomes the scheduler-call scope |
| every other in-tree comment the recipe below finds that states the recycler's scope | the same correction |

The "per-thread" recipe:
`git grep -n -i "per-thread\|per thread" -- src include tests spec brain .specify/2f-async-mutex.md | grep -i "recycl\|cache\|first-touch\|thread_info"`.
Correct only hits that describe asio's recycler. Other per-thread state, such as log buffers, is out of scope.

## 5. Constitutional justification for the conditional zero (R-6; `[const §VIII.5]`)

In issue mode this note is the plan. R-6 records the deviation's justification here, as `[const §VIII.5]`
provides: "zero `new`/`delete` between parse and `fromApp` callback. Arena/PMR is the default; deviations require
justification in the relevant `/plan`."

**What the constitution asks.**
- `[const §VIII.5]`, quoted above.
- `[const §XV.1]`: no heap allocation per message on the in-memory hot path, which is parse → validate →
  dispatch and `MemoryStore`.
- `[const §XI.6]`: coroutine frames HALO-first, with a PMR fallback per awaiter where HALO does not fire.

**What B35 uses, and why not HALO or PMR.**
- B35 reaches zero through asio's frame and handler recycler (§1(b)). That is the mechanism every shipped
  `asio::awaitable` frame already goes through.
- An `asio::awaitable` frame has no allocator customisation point: `awaitable_frame_base::operator new` always
  goes to `thread_info_base` (`asio/impl/awaitable.hpp`). §XV.1's own v0.2 amendment records that "the Asio
  awaitable frame is opaque to the bound allocator".
- So no PMR fallback exists for awaitable frames on any path. The `[const §XI.6]` tension predates B35. B35 adds
  no frame mechanism; it removes frames.

**Why this is a deviation and not plain compliance.** The zero holds only when **both** of these hold:
1. **the Engine's executor is `io_context::executor_type`**, by exact target type (§2.1);
2. **the handling thread stays inside one scheduler call (`run`-family)** across messages (§1(b)).

Within those conditions:
- the first message in each scheduler call primes the cache, which is amortised and not per message;
- a message on an ungated path can evict a cached block, and the next gated message re-primes it.

**Precedent.** `.specify/2f-async-mutex.md` Erratum E-4 (user-authorized) sanctioned asio's cancellation
recycler as an allocator whose guarantee is met in steady state, with the first touch "amortized and … not a
hot-path event". B&L L-006-2 discloses that as wontfix. E-4's "per-thread" premise is corrected to the
scheduler-call scope by E-6 (§2.3). The precedent's substance — a steady-state guarantee over asio's recycler,
with the priming disclosed — is what R-6 applies here.

**Residual configurations.** These are tracked as **open B&L limitations**, with **no constitution amendment**
(§4):
- the R-2 fallback executors (condition 1 fails);
- consumer-driven bounded `run_for` / `run_one_for` / `poll` loops (condition 2 fails);
- mixed-traffic eviction.

**Scope of the clauses.**
- `[const §VIII.5]`'s text spans parse → `fromApp`, which W-A and W-B gate.
- W-C's zero comes from feature 025's W8 requirement (`specs/025-refresh-on-logon/data-model.md`, row W8),
  which anchors the re-hydrate apply step (`SeqnumManager::hydrate()`) on `[const §VIII.5]`.
- W-D's `MemoryStore::store` is reached through the outbound `store_then_emit`. Its clause is
  `[const §XV.1]`'s MemoryStore sentence, which has no deviation provision of its own. R-7 (§0) settles the
  clause question for W-D: R-6's justification extends to that sentence for B35's conditional zero.

**What this section does not do.** It does not amend the constitution. Article XX §1–§2 make any change to the
constitution's text an owner amendment, and R-6 rules that none is needed.

## 6. Risks Gate A should press on

1. **The macro's equivalence with today's `async_lock` (§2.3).**
   - Part 1 must match today's `await_transform` check exactly, in both its condition and its exception type.
     The O-2 cell is RED without it.
   - Seam #17, E-5's drain and the unconditional restore each have a cell (§3).
   - If a future asio changes `await_transform`'s check, the O-2 cell is the detector.
2. **Re-erasure.** Any accept, connect, reconnect, timer or SSL construction site that builds a fresh
   `strand<any_io_executor>` around the session silently re-introduces class (a). Arm (a) covers the Engine path
   only.
   - Recipe: `git grep -n "make_strand\|strand<" -- src include`. Classify every production hit as on or off
     W-A/W-B's handler path.
   - Hits known at v0.2:
     - the two §2.1 sites, which are converted;
     - `control_strand_`, which is off the read path;
     - `src/core/system_clock_source.cpp`'s per-timer strands over the engine executor. These are timer
       executors for `sleep_until`, not the session strand, and do not run on an inbound read. The rigs run on a
       mock clock, so no timer fires inside the windows. B35 does not change them.
   - Re-run the recipe at implementation time; the list above is a lead.
3. **Frame-size drift.** A new local or `co_await` temporary on the Active path can push a frame over the limit
   on one compiler only. The `lock_frame` that §2.3 adds to each converted caller's frame is such a growth, and
   `check_inbound`'s and `store`'s frames must still fit. The Release-preset gates are the only guard. No size is
   pinned.
4. **Slot budget.** A new nested `co_await` on the Active path or the W-B dispatch path can exceed two cycled
   frames. W-B's dispatch is synchronous `fromApp` (§3), so it adds no frame today. W-B's RED-task-0 baseline
   shows whether the path is already deeper. If it is, §2.2's split extends to it before the gate can pass.
5. **Other recyclers on the path.** The cancellation-slot emplacement in `reset_cancellation_state` and
   `inherited_slot.assign` uses `cancellation_signal_tag`; executor ops use `executor_function`. Both have
   `ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE` slots under the same policy and the same scheduler-call scope. The full
   counter decides.
6. **MSVC layout** (§2.1 RED task 1): the fixpp executor is the contingency.
7. **Harness additions inside a window** (§3) fail toward red. A window that reads > 0 is first checked for
   harness allocations, before the production path is blamed.

## 7. What this does not decide

- The outbound send path (`store_then_emit` → `write_gate_` → `async_write`), beyond `MemoryStore::store`.
- FileStore's lock call sites.
- A generic executor (rejected by R-2).
- Raising asio's cache size (rejected by R-1).
- A libc++ Release preset.
- Sanitizer and MSVC-debug allocation counters.
- TLS allocation beyond R-5's measurement.
- A constitution amendment (R-6 rules none).

---

## Gate A

- Round 1 applied 2026-10-09: Codex P1=3 P2=8 P3=1; Opus post-judging P1=2 P2=6 P3=12; rewrite addresses root causes RC-1, RC-2, RC-3 (+R-6). Reviews: research/reviews/codex_544_1_hot-path-zero-alloc_review.md, research/reviews/opus_544_1_hot-path-zero-alloc_triage.md.

### Round 1 — root causes

- **RC-1. The recycler scope is the scheduler call, not the thread.**
  - §1(b) restated.
  - §3's window rule: the driver-coroutine and run-thread shapes, a fixed K, and the (s) scope control.
  - §4's driver-condition row, and the "per-thread" corrections and their recipe.
- **RC-2. The caller-side lock sequence is one macro, the only definition, in five ordered parts.**
  - §2.3: the pre-check, the handler-sourced executor and slot, and the unconditional restore.
  - §3's lock cells, and arm (c)'s test-only replica.
- **RC-3. The constitutional disposition.**
  - R-6 in §0, and the justification in §5.
  - The residuals as B&L conditions in §4.

### Round 1 — disposition of every finding

| Finding | Judged severity | Where addressed |
|---|---|---|
| C-1 (constitutional status) | P2 + owner item | R-6 (§0), §5 |
| C-2 (in-window `use_future` scaffolding) | P2 | §3 window rule (driver coroutine; existing shapes not reused verbatim) |
| C-3 (arm (c) cannot turn RED) | P2 | §3 arm (c): test-only two-frame replica |
| C-4 (initiation not verbatim; RAII wrapper not implementable) | P2 | §2.3: two handler-sourced lines; the macro |
| C-5 (E-6 vs E-1) | P3 | §2.3 E-6 as a narrow supersession; lifetime paragraph |
| C-6 (`SessionEntry` is nameable) | P3 | §2.1 (and the `assert_transport_on_session_strand` parameter); §4 |
| C-7 (dispatcher parameter lifetime) | P3 | §2.2 by-value rule |
| C-8 (`cancellable_dispatch` not on W-B) | P3 | §3 W-B; §6 risk 4 |
| C-9 (§2.2 evidence is a one-time baseline) | P3 | §3: (b) planted in W-A's window; baseline is supporting |
| C-10 (CI sentinel) | P3 | §3 three coupled CI edits |
| C-11 (documentation delta) | P3 | §4 file table |
| C-12 (inline oracle) | P3 | §2.1 target-type oracle; `alignof` assert |
| O-1 (recycler per scheduler call) | P1 | §1(b); §3 window rule; arm (s) |
| O-2 (cancellation erased in `store()`) | P1 | §2.3 macro part 1; §3 O-2 cell; §6 risk 1 |
| O-3 (MSVC row) | P2 | §0 implemented forms; §3 table (class (a) only; rig ported) |
| O-4 (missing residual disclosures) | P2 | §4 executor and driver conditions; §5 |
| O-5 (warm-up criterion) | P3 | §3 fixed K |
| O-6 (twins register on Debug) | P3 | §3 Release only |
| O-7 (arm entry points) | P3 | §3 `--expect-entry` per arm |
| O-8 (recipe over-matches) | P3 | §2.1 `git grep -nw` |

### Round 1 — disagreements

**The triage disagreed with no finding** (triage Appendix B). Some of Codex's counter-proposals are not adopted.
In each case the finding is addressed by the triage's fix instead:
- **C-1, "pre-provision the frames so the first invocation also uses HALO/PMR/preallocated storage".** asio
  awaitable frames have no allocator customisation point (`asio/impl/awaitable.hpp`,
  `awaitable_frame_base::operator new`). The disposition is R-6's `[const §VIII.5]` justification (§5).
- **C-1, "obtain an owner-approved constitutional amendment".** The owner ruled no amendment (R-6).
- **C-6, "keep the public field and hold the concrete strand in an Engine-private sidecar".** The change is
  disclosed as a C++ source/layout change instead (§2.1, §4). The triage judged it a wording issue, because no
  public `Engine` member exposes `SessionEntry`.
- **C-7, an ASan cell for the dispatcher's lifetime.** Not needed. The by-value rule closes the hazard, and
  today's coroutine has no dispatcher locals before its `switch` (triage C-7).
- **C-8, restoring `cancellable_dispatch` as a deliberate executor change.** Out of B35's scope. W-B covers the
  production path, which is synchronous `fromApp`.
- **C-9, a test-only legacy monolithic dispatcher in a mutant library.** Replaced by arm (b), planted in W-A's
  own window. That arm proves the gate sees the class §2.2 guards against (triage C-9).
