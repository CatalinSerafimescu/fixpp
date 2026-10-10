# fixpp#544 — zero allocation on the Active inbound read and the `MemoryStore` write (B35)

> **Status: v0.5.1.** **Gate A CONVERGED — instance 2 round 2, 2026-10-09** (Codex P1=0 P2=3 P3=1; Opus post-judging P1=0 P2=0 P3=5; the five P3s applied as amendment v0.5.1 with no further round, per the triage's closing recommendation). Owner pre-authorised continuation on convergence (2026-10-09).
> - **Gate A loop instance 1:**
>   - round 1: BLOCK — Codex P1=3 P2=8 P3=1; Opus post-judging P1=2 P2=6 P3=12; rewritten to v0.2,
>     addressing RC-1..RC-3;
>   - round 2: BLOCK — Codex P1=1 P2=3 P3=2; Opus post-judging P1=1 P2=1 P3=6; rewritten to v0.3,
>     addressing O2-1, O2-2 and the P3s;
>   - **closed before round 3**, because the owner revised R-1 to **R-1′** on 2026-10-09, on probe q5 (§0).
> - **v0.4** implements R-1′: fixpp exports `ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE` (§2.4). It re-checks
>   §1–§7 against it.
> - Owner rulings **R-6** and **R-7** (2026-10-09) are in §0; their justification is §5.
> - Gate A instance 2 round 1: BLOCK — Codex P1=3 P2=2 P3=1; Opus post-judging P1=2 P2=3 P3=3; owner rulings R-8, R-9, R-10; rewritten to v0.5.
> - Gate A instance 2 round 2: CONVERGED — Codex P1=0 P2=3 P3=1; Opus post-judging P1=0 P2=0 P3=5; the P3s
>   applied as v0.5.1. This round's finding IDs are written I2R2 C-1..C-4 and I2R2 O4-1, so they cannot be
>   confused with instance-2 round 1's C-1..C-6.
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
> `b35_544_design_reconciliation.md`. The probes are in the parent's
> `research/G19-fix-fpml-iso20022/research/probes/` (`../research/probes/` from the library root):
> - the executor probe, `b35_544_executor_alloc_probe.cpp`;
> - Gate A round 1's p1–p5, in `b35_544_gate_a_r1/`;
> - Gate A round 2's q1–q3, in `b35_544_gate_a_r2/`, with the v0.3 rewrite's q4 and q5 beside them. q5 is
>   the evidence for R-1′ (§0).
> - Gate A instance 2 round 1's r1 and r2 (`r1_buffer_lifetime.cpp`, `r2_overlimit_send_evicts.cpp`, with
>   `r2_log.cpp`) and its header-closure script `closure.py`, in `b35_544_gate_a_i2r1/`, with the v0.5
>   rewrite's r3 (`r3_reentrant_send_inline.cpp`) beside them.
> - Gate A instance 2 round 2's s1 (`s1_holder_release.cpp`), in `b35_544_gate_a_i2r2/`.
>
> Each round's review and triage are listed under `## Gate A`. Each triage's Appendix A gives its probes'
> build line and recipe. "Round-1 triage p*N*", "round-2 triage q*N*", "instance-2 triage r*N*" and
> "instance-2 round-2 triage s1" below name those probes. r3's build line and rows are in its file header.
>
> **Independence (`[const §XVII.3]`).** The author of this note is the Opus orchestrator. Gate A's Codex
> review runs in a fresh Codex session, which has not seen the consult session.
>
> **Issue:** fixpp#544, including its three comments and the owner scope ruling of 2026-10-08. **Batch:** B35.
> **Tree:** branch `544-hot-path-zero-alloc` from `main` at `d51ce86d`. **Dependency:** asio **1.38.0**
> (`conanfile.py`). #544, L-497-1 and several header comments cite 1.36; every asio citation below is from 1.38.
>
> **Trigger (`[const §XVII.1]`).** This touches the executor model and public C++ headers:
> `include/fixpp/core/sync/async_mutex.hpp`, `include/fixpp/session/memory_store.hpp`,
> `include/fixpp/session/engine.hpp` and (R-9) `include/fixpp/session/session.hpp`.
>
> **Not BREAKING under `[const §X.7]`.** No C-ABI declaration, symbol or layout changes. §X.7 also counts
> a consumer that "is no longer ABI-compatible". R-1′ changes the asio inline code that `libfixpp_capi.a`
> carries (§2.4), so a C-ABI static consumer whose own asio TUs use another N becomes ODR-inconsistent.
> That hazard needs the consumer's own C++ asio TUs, which the C ABI never promised compatibility with; the
> same hazard already exists for any asio-version mismatch. It is disclosed as a usage obligation (§4), not
> as a C-ABI change (instance-2 triage O3-2).
>
> The C++ surface changes are disclosed in §4:
> - `SessionEntry::session_strand`'s type;
> - one `FIXPP_DETAIL_` macro, together with the `fixpp::sync::detail` names its expansion uses;
> - (R-1′) a PUBLIC compile definition, `ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE=<N>`, on `fixpp_core`. Every
>   C++ exported target that publishes the `include/` root inherits it (§2.4). A new installed guard header
>   comes with it, and so does an ODR obligation on every TU in the process that includes asio (§2.4);
> - (R-9) `Session`'s layout: it gains session-owned send scratch storage and a slot flag (§2.5). `Session`
>   is a public class in an installed header, so this is a C++ layout change, as `SessionEntry`'s is.
>
> All of them become visible to consumers of the installed package.
>
> **Amends (each with an erratum or annotation in the PR).** `.specify/2f-async-mutex.md` gains erratum
> **E-6** (§2.3). `.specify/2d-threading.md` gets an annotation on the strand's inner executor type (§2.1).
> **No constitution amendment** (R-6, §5).

---

## 0. Owner rulings (2026-10-09). Not reopened at Gate A; Gate A reviews whether this note implements them.

| # | Question | Ruling |
|---|---|---|
| R-1 | How the lock frame and the 2-slot cache are handled | **Revised by R-1′ (2026-10-09).** Original ruling, kept as history: **Internal frameless lock op.** A `fixpp::sync::detail` entry returns a `deferred` lock operation that hot-path callers `co_await` directly. The public `async_mutex::async_lock` signature is unchanged. Rejected: a PUBLIC `ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE`, and changing `async_lock`'s public return type. *R-1′ keeps the frameless lock op and the rejection of a public return-type change. It reverses the rejection of the public cache-size macro.* |
| R-1′ | The same question, given probe q5 (owner ruling, **2026-10-09**) | **Export the cache size, and keep the frameless lock op.** fixpp exports `ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE=N` as a PUBLIC/INTERFACE compile definition on its exported CMake targets, and in the Conan `package_info`, so that every TU that sees fixpp's asio sees the same N. The frameless lock op (§2.3) and the `deferred` changes stay: they reduce depth and are already designed. The cache size is added on top of them. **Why R-1 was revised:** `MemoryStore::store` runs on the outbound path. In an Active session the inbound read is always pending, and a pending read that inbound traffic re-arms holds one cycled frame on the same scheduler call (§1(b)). The store chain plus that frame exceeds the 2-slot default. **Evidence:** probe `b35_544_gate_a_r2/q5_cycled_read.cpp`, built with `g++ -std=c++23 -O2 -DASIO_STANDALONE -DASIO_RECYCLING_ALLOCATOR_CACHE_SIZE=<N> -I<asio 1.38 include>` for N = 2, 3, 4 and run with no arguments. The orchestrator ran it on 2026-10-09. At N=2, its row "wrapper → store_macro, cycled read child live" allocated through `aligned_alloc` on every iteration, while at N=3 and N=4 it read 0. Its controls read 0 at every N. The v0.4 rewrite re-ran it the same day at N = 2, 3, 4 and 8 with g++, with the same pattern; N=8 read like N=3 and N=4. Re-run the probe; do not cite its numbers. |
| R-2 | Which executors must reach zero | **The `io_context` fast path.** When the Engine's executor targets `asio::io_context::executor_type`, the session strand is `strand<io_context::executor_type>` and reads zero. Any other executor keeps today's path, which is disclosed. Rejected: a generic fixpp executor wrapping any caller executor. |
| R-3 | Platforms that must prove zero | Superseded by R-3′. |
| R-3′ | Platforms that must prove zero, given R-4 | **Three Release presets.** mallocnesia gates `linux-clang-release` and `linux-gcc-release`, and the CI mallocnesia step is widened to `linux-gcc-release`. `windows-msvc-release` gates the `operator new` half with the TU-local counter, and its `_aligned_malloc` half is disclosed. No sanitizer-hook or `_CrtSetAllocHook` instrument is built in B35. Debug, sanitizer and libc++ lanes run the cells as **diagnostics**. libc++ is disclosed: it has no Release preset. |
| R-4 | Debug (`-O0`) presets | **Zero is gated on Release presets only.** |
| R-5 | TLS per-record allocation | **Orchestrator default, not yet an owner ruling.** Measure W-A on a TLS rig in the RED step (§3). If OpenSSL allocates per record (asio sets `SSL_MODE_RELEASE_BUFFERS`, `asio/ssl/detail/impl/engine.ipp`), file a new issue; it is outside B35. |
| R-6 | Constitutional status of B35's conditional zero (owner ruling, **2026-10-09**) | **A `[const §VIII.5]` justification, recorded in this note, which serves as the plan in issue mode** (§5). Precedent: `.specify/2f-async-mutex.md` Erratum E-4 and B&L L-006-2. The zero holds only when the Engine's executor is `io_context::executor_type` **and** the handling thread stays inside one scheduler call (`run`-family). The residual configurations — R-2 fallback executors, consumer-driven bounded `run_for`/`run_one_for`/`poll` loops, and mixed-traffic eviction — are tracked as **open B&L limitations**, with **no constitution amendment**. |
| R-7 | Does R-6 cover `MemoryStore::store`, whose clause is `[const §XV.1]`'s MemoryStore sentence with no deviation provision? (owner ruling, **2026-10-09**) | **Yes — owner interpretation.** R-6's `[const §VIII.5]` justification extends to `[const §XV.1]`'s MemoryStore sentence for B35's conditional zero (the same two conditions, the same residuals tracked in B&L). Basis: §XV.1's own v0.2 amendment already records that the asio awaitable frame is opaque to any bound allocator. Recorded in this note only. No issue is filed and the constitution text is unchanged. |
| R-8 | R-1′'s Conan `package_info` half, given that no fixpp Conan package exists (instance-2 triage C-2; owner ruling, **2026-10-09**) | **Confirmed in its narrowed form.** The installed CMake package's PUBLIC/INTERFACE definition is B35's whole delivery, and the guard header's `static_assert` is the safety net. The Conan half applies only once a fixpp Conan package exists: a future recipe sets `cpp_info.defines` from the guard header and adds a `test_package` consumer leg. |
| R-9 | Every `Session::send` is an over-limit frame request that evicts a cached block (instance-2 triage O3-1; owner ruling, **2026-10-09**) | **B35 is widened** to bring `Session::send_impl`'s coroutine frame under asio's recycler limit. Its three 4096-B arrays, live across `co_await`s, move out of the frame into **session-owned scratch storage**, pre-allocated per session and never per message (`[const §XV.1]`). The existing write-path serialisation must guarantee that no two `send_impl` invocations use it at once; if it cannot, the note specifies the safe alternative. A **two-way window** gates it: the inbound Active pair interleaved with application sends, asserting 0 on the gated Release presets, with a fix-deleted arm that restores an over-limit send frame and must go RED. |
| R-10 | The guard for `send_impl`'s session-owned frame buffer, given that `send_impl` is not serialised across its suspensions and a send started inside `toApp` runs nested inline (probe r3; §2.5) (owner ruling, **2026-10-09**) | **Flag + fallback.** A strand-local in-use flag guards the session slot. A send that finds it taken (nested in `toApp`, or overlapping a suspended send) uses the fallback leaf, which allocates. That residual is disclosed under R-6. Rejected: a session send gate, because of its drain, deadlock and cancellation conditions. |

**Implemented forms.** Instance 1's triages judged no ruling technically unimplementable. Four forms are
narrowed: three from the round-1 triage (*Owner items* and O-1/O-2/O-3), and one from the round-2 triage
(C2-1). v0.4 added two forms for R-1′. v0.5 corrects one of them (R-8) and adds one for R-9, whose premise
the code does not satisfy. This note writes all of them:
- **R-1's frameless lock op, kept by R-1′,** is implemented through **one macro**, whose first part is the
  pre-check today's `co_await` performs (O-2, C-4; §2.3). Of the two wrappers v0.1 offered, the RAII one is
  not implementable.
- **R-1′'s Conan half: the narrowed form R-8 confirms (instance-2 triage C-2).** v0.4 said this "does not
  narrow the ruling". That was false: the Conan half has no artifact to apply to, so it is a narrowed form.
  - fixpp's `conanfile.py` declares requirements and options only. It has no `package()`, `package_info()`,
    `exports_sources` or `package_type`, and `conan/recipes/` holds civetweb only. Re-check with
    `grep -nE "def |package_type|exports" conanfile.py` and `ls conan/recipes`. So no fixpp Conan package
    exists, and nothing evaluates a fixpp `package_info`.
  - Until one exists, the installed CMake package is the whole delivery of the definition (§2.4), and the
    guard header's `static_assert` is the mechanical net.
  - **The condition for a future recipe.** A recipe that adds `package_info` sets
    `cpp_info.defines = ["ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE=<N>"]`, with N read from the guard header, and
    adds a `test_package` consumer leg that checks it.
  - §2.4's consumer witness is **not** that check. It runs `find_package(fixpp)` against fixpp's own CMake
    install (`tests/consumer/CMakeLists.txt`). A Conan consumer that uses `CMakeDeps` gets targets generated
    from `package_info`, not from `fixppConfig.cmake`, so the witness cannot see a missing `cpp_info.defines`
    (inferred, from how `CMakeDeps` generates targets).
- **R-1′ on fixpp's exported CMake targets** is a PUBLIC definition on `fixpp_core` (C-1; §2.4). Every C++
  export-set member that publishes the `include/` root links `fixpp_core` PUBLIC/INTERFACE, directly or
  transitively, so all of them inherit it. `fixpp::capi` and `fixpp::service` withhold it: they reach the
  closure only through `$<LINK_ONLY:>`, and their headers include no asio (§2.4, *The C ABI*).
- **R-9's "existing write-path serialisation" does not exist (§2.5).** No lock or strand keeps two
  `send_impl` invocations apart across their suspensions. A second send can start while the first is
  suspended in its store or its write, and a send started from inside `toApp` runs inline, nested inside the
  first (probe r3). So the frame buffer cannot be one shared slot by that guarantee. The implemented form,
  which R-10 rules:
  - the two build arrays are session-owned and used only inside a synchronous helper, which the strand
    alone makes exclusive;
  - the frame buffer is one session-owned slot, held by a strand-local flag from its first write until
    `store_then_emit` returns;
  - a send that finds the slot held takes a fallback leaf coroutine that owns its own buffer. That send is
    over the limit and allocates. This is an R-6 residual, disclosed as a condition (§4): when a send starts
    while the slot is held, because it is nested in `toApp` or overlaps a send suspended in its store or its
    write, that send allocates its frame and evicts one cached block.
  - The alternative that keeps that send at zero too, a session send gate, is rejected by R-10 (§2.5).
- **R-2's "reads zero"** holds only while the handling thread stays inside one scheduler call (O-1, O-4; §1(b)).
  R-6 records that condition.
- **R-3′'s `windows-msvc-release` row** covers class (a) only: W-A and W-B with arm (a) (O-3; §3).
- **R-2's stored strand type** is `strand<session_inner_executor_t>` (C2-1; §2.1).
  - The fast-path test is unchanged: the exact target type is `io_context::executor_type`.
  - Where `strand<io_context::executor_type>` does not fit `any_io_executor`'s inline storage, the inner
    executor is a fixpp-owned executor over the same `io_context`. Round-2 triage q1 predicts that this is the
    MSVC case.
  - This stays inside R-2: the reconciliation row the owner ruled from names the contingency (row D-A1..3 of
    `b35_544_design_reconciliation.md`: "MSVC sizeof pin first, which may force a fixpp-owned 8-B inner
    executor"), and so does Fable's consult (its *MSVC caveat*).

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
  - There are **`ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE` slots per purpose** (`asio/detail/thread_info_base.hpp`).
    asio's default is 2. Under R-1′ fixpp exports a larger N (§2.4). The macro sizes every purpose's slots:
    `default_tag`, `awaitable_frame_tag`, `executor_function_tag`, `cancellation_signal_tag`,
    `parallel_group_tag` and `timed_cancel_tag`.
    - A request that fits no cached block evicts the **first occupied** slot of its purpose, even when an
      empty slot exists, and then calls `aligned_new`. That is `std::aligned_alloc` on Linux and
      `_aligned_malloc` on MSVC (`asio/detail/memory.hpp`).
    - An over-limit request also evicts, because no cached block can fit it.
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
    2. for each purpose, the number of cycled blocks of that purpose live at once is at most the slot count N.
       For `awaitable_frame_tag` that is the number of cycled frames; the other purposes are counted the same
       way (§6 risk 5);
    3. the handling thread stays inside **one** scheduler call across the messages.
  - Condition 2 is where R-1′ acts: it raises N. Conditions 1 and 3 do not change. A frame over the limit is
    never cached, whatever N is, and every scheduler call still starts with an empty cache.
  - **What counts as cycled (O2-1).** A frame is *cycled* if it is allocated and freed inside the window. A
    frame that lives for the whole window, such as a test driver or a session-lifetime loop, is never freed
    there and takes no slot. Condition 2 counts every cycled frame live on that scheduler call, including the
    frames of **other** coroutine chains that are suspended at that moment. The cache is per scheduler call,
    not per chain.
    - A pending transport read's frame counts when the read completes and is re-armed inside the window: it is
      then cycled.
    - A frame suspended across the whole window does not count.
    - The v0.3 rewrite's probes measured both cases on 2026-10-09, with g++ and clang++. They are variants of
      round-2 triage q3, built with that triage's build line: `q5_cycled_read.cpp` for a read re-armed every
      iteration, and `q4_suspended.cpp` for a read suspended throughout, both in `b35_544_gate_a_r2/`.
    - q5 is R-1′'s evidence. Built with `-DASIO_RECYCLING_ALLOCATOR_CACHE_SIZE=<N>`, its "store chain with a
      cycled read live" row allocates at the default N and reads 0 once N covers the read frame (§0, R-1′).
    - q5's "nest of CACHE_SIZE" rows nest a **fixed two** frames. Their label holds only at N=2, so they are
      not a slot-count pin at any other N. §3's (b-S) pair is that pin.
  - **Warm-up.** The recycler is first-fit, but a recycled block keeps its capacity
    (`thread_info_base::allocate` saves it in `mem[size]`, `deallocate` restores it to `mem[0]`). So a
    size-inverted nest converges after a few passes rather than thrashing. Round-2 triage q2/q2c measured this
    on asio's real `thread_info_base`, at the default slot count. To re-derive how many passes it takes at the
    exported N, build `q2c_random.cpp` with `-DASIO_RECYCLING_ALLOCATOR_CACHE_SIZE=<N>`. Run it with arguments
    `<depth> <warm>`, increasing `<warm>`, where `<depth>` is the largest live set the gated windows hold (§3).
    Its first argument is the maximum nesting depth it searches. §3 sets K from that recipe.
  - Condition 3 is what round-1 triage p3 measured on 2026-10-09: the same workload under one `ioc.run()`
    against `run_one_for` and `poll_one` loops (recipe in that triage's Appendix A).
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
    `any_io_executor{asio::make_strand(session_inner_executor_t{*target})}`. On a compiler where the alias is
    `io_context::executor_type` itself, that is `make_strand(*target)` (alias below);
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
- **The stored inner executor: one named alias (C2-1).** The fast-path test above is unchanged. What the fast
  path stores is `any_io_executor{asio::make_strand(session_inner_executor_t{…})}`.
  - `session_inner_executor_t` is one internal alias in `src/`, chosen at compile time by the inline-fit
    condition: `sizeof(S) <= sizeof(std::shared_ptr<void>) + sizeof(void*)` and `alignof(S) <=` the `alignof` of
    that `object_type`, where `S = asio::strand<asio::io_context::executor_type>`.
    - If both hold, the alias is `asio::io_context::executor_type`.
    - Otherwise it is an internal fixpp executor in `src/` that meets both conditions. It holds an
      `io_context*` with the property bits in its spare low bits, forwards `execute` / `query` / `require` /
      `prefer` to a temporary `io_context::executor_type`, and uses `on_work_started/finished` for the tracked
      variant. It is public asio API only, with no `asio::detail`.
  - On Itanium, `strand<io_context::executor_type>` meets both conditions (round-1 triage p5).
  - MSVC applies EBO only to the first empty base. `io_context::basic_executor_type` derives from two empty
    bases around one `uintptr_t` (`asio/io_context.hpp`, `class basic_executor_type`). Round-2 triage q1
    reproduces that layout with clang's MS-ABI record layout, and predicts that MSVC takes the fixpp executor.
    q1 is a proxy.
  - **RED task 1** compiles the fit condition with cl.exe in the sandbox, to learn which branch MSVC takes. cl.exe
    is the oracle.
- **Pins**, in the new test TU:
  - on every compiler, the two fit `static_assert`s against `asio::strand<session_inner_executor_t>`, so they
    hold on whichever branch the compiler took;
  - on every compiler except MSVC, `static_assert(std::is_same_v<session_inner_executor_t,
    asio::io_context::executor_type>)`. Without it, an asio layout change could move Linux onto the fixpp
    executor silently;
  - a **target-type oracle**: for an `any_io_executor e` holding the session strand,
    `e.target<asio::strand<session_inner_executor_t>>() != nullptr`.
    - It identifies the stored type only. It cannot tell inline storage from `shared_target_executor`.
    - The runtime proof of inline storage is the zero-allocation window itself (W-A), together with arm (a)
      reading > 0 on the fallback.
    - The undocumented `target_ == &object_` comparison is not used.
  - The fixpp executor's behaviour oracle is the three fast-path semantic cells: serialisation, executor
    equality and work tracking (§3 *Behaviour*). They are registered on the MSVC lanes too, because the fixpp
    executor is predicted to compile only there. Those cells exercise its `prefer(tracked)`, its `blocking`
    query and its equality.

### 2.2 Class (b): `Session::on_inbound_frame` split so the Active path's frames fit the limit

- `on_inbound_frame` becomes a **non-coroutine** function returning `asio::awaitable<…>`. It selects a
  per-state coroutine (`on_inbound_active_`, `on_inbound_logon_`, …) and returns that coroutine's awaitable,
  which the read pump `co_await`s. The dispatcher adds no frame. Legality: round-1 triage p5.
- **Parameter lifetime.** asio awaitables start suspended, so a selected arm starts running only after the
  dispatcher has returned. **Every arm takes every parameter by value; the only reference it may hold is
  `*this`.** Today's coroutine already takes `std::span<const std::byte> frame` by value and has no locals
  before its `switch`, so the rule preserves current behaviour.
- **Scratch storage leaves the arms by its lifetime (C-3).** v0.4 hoisted every scratch array into a
  non-coroutine helper. That is a use-after-return for every array whose bytes a later `co_await` reads.
  The class of a buffer is decided by **data flow**: where the last read of the storage, or of any span or
  view into it, falls relative to the arm's next `co_await`.
  - **Class 1: dead before the next suspension → a non-coroutine helper.** The storage, and every span or
    view into it, is last read before the arm's next `co_await`. It moves onto a non-coroutine helper's
    stack. The file already uses this shape (`validate_inbound_`).
  - **Class 2: read by a later `co_await` → a leaf coroutine that owns it.** Each such reply block (build,
    `assign_outbound`, `fire_to_admin_`, `store_then_emit`) becomes one private leaf coroutine. The leaf
    declares the buffer and performs the emit, and the arm `co_await`s the leaf directly.
  - **Why class 2 cannot use a helper.** `store_then_emit` takes a non-owning `std::span` frame. It suspends
    in the store, and then transmits **the caller's original span** through `live_write_serialized_(frame)`
    (`src/session/session.cpp`, `Session::store_then_emit`). A buffer owned by a helper that has returned
    dangles by then. Instance-2 triage r1, row `unsafe`, measured it on 2026-10-09: at -O2 the emitted bytes
    differed from the built ones on g++ and clang++. ASan with default options reported
    `stack-use-after-return` on both. With `detect_stack_use_after_return=0`, clang's ASan reported nothing.
  - **Keeping the buffers in the arm is not a fallback.** GCC 13 does not overlap the frame slots of locals in
    disjoint scopes. r1's row `mono` (six 512-B blocks inline in one coroutine), run on 2026-10-09, requested
    a frame several times the limit on g++ 13 and one under it on clang++. `linux-gcc-release` builds with gcc-13
    (`conan/profiles/linux-gcc-release`). So an Active arm that keeps its reply buffers is over the limit on
    that preset by construction, and W-A could not read 0 there.
  - **The leaf's own frame.**
    - It is cycled only on its reply path. W-A's Heartbeat and W-B's `fromApp` take no reply, so the gated
      chains do not change.
    - It must itself fit the limit (§1(b), condition 1). r1's row `leaf` (a leaf that owns one 512-B buffer
      and does the emit) requested a frame under the limit on both compilers, and was correct and ASan-clean
      under every option. Re-run r1; do not cite its numbers.
    - The §2.4 recipe counts the leaf on reply chains, which stay ungated (§3).
  - **Parameters.** The by-value rule above binds only where a **non-coroutine** returns an awaitable, which
    is the dispatcher. A leaf that the arm `co_await`s directly may take references to the arm's locals,
    because the arm's frame outlives that `co_await`.
  - **The recipe, and why it is only a lead.** Re-derive the arrays with
    `grep -n "std::array<std::byte" src/session/session.cpp`. For each one inside `on_inbound_frame`, list the
    `co_await`s between its declaration and the end of its enclosing block (comments stripped). That scan is
    lexical, and it over-reports. An array with no `co_await` in its block is class 1. An array with one is
    class 1 only if reading the data flow shows that nothing reads it, or a view into it, after that
    `co_await`. The instance-2 triage's scan found no class-1 array in today's `on_inbound_frame`; re-run it.
  - **Rejected: a `Session`-member scratch buffer for the inbound reply blocks.** Other chains on the strand
    emit while a reply is suspended in its write, such as `run_liveness_loop`'s Heartbeat and TestRequest
    emits. A shared buffer would need a per-chain exclusivity argument, and the leaf form needs none. §2.5
    makes that argument for `send_impl`, where the leaf form is not available: a 4096-B buffer is over the
    limit in any frame.
  - **Gates.** W-A on `linux-gcc-release` is the frame-size check. The ASan lane with default options is the
    lifetime check, **because** no session lane disables `detect_stack_use_after_return`. Re-check with
    `git grep -n ASAN_OPTIONS -- CMakePresets.json .github cmake tests ci`: the only setting is the python
    legs' `detect_leaks=0:halt_on_error=1` (`ci/derive-python-sanitizer.sh`). An override that sets
    `detect_stack_use_after_return=0` would blind clang's ASan to this class (r1). ASan sees only the reply
    paths the session suites exercise.
- **Target:** on the Active path, every cycled frame fits the recycler limit on the three Release presets. The
  Active chain is `on_inbound_active_` → `SeqnumManager::check_inbound`, because the dispatcher and, after
  §2.3, the lock add no frame. That chain, together with every cycled frame other chains hold on the same
  scheduler call at that moment, stays within the exported N (§2.4).
  - **Why the split stays under R-1′.** R-1′ relaxes v0.3's "at most two live" depth target, because the slot
    count is now N. It does not relax the limit. Today's coroutine keeps its scratch arrays in its frame, and
    #544 attributes the frame's excess over the limit to them (the grep above lists them). A frame over the
    limit is allocated on every message, and raising N cannot cache it (§1(b), condition 1). The per-state
    arms, the class-1 helpers and the class-2 leaves are what make the frames fit.
  - The non-coroutine dispatcher is kept. It is legal (round-1 triage p5), already designed, and removes one
    cycled frame from the Active chain, which is headroom under N.
  - §2.3 puts a `lock_frame` into each converted caller's frame. That frame grows, and must still fit (§6 risk 3).
  - Frame size is a per-compiler, per-`-O` RESULT, so no size is written in a comment.
  - The instrument is the gate itself: mallocnesia logs every `aligned_alloc(<align>, <size>)` it intercepts.
- **Behaviour is unchanged.** This is a pure restructuring: same states, same transitions, same replies.
  - The existing session suites are the behaviour oracle.
  - No test is edited to accommodate the split, except for names that change.

### 2.3 Class (c): `deferred` for hot-path awaited operations, and the frameless lock op (R-1)

- **What the frame-removing edits are worth under R-1′.** With the exported N (§2.4), no production window at its
  modelled depth needs these edits to read 0: one extra frame on a chain stays within N. They are kept because
  R-1′ keeps them. Each removes a cycled frame, and that frame is slot headroom for every chain that shares the
  scheduler call. So each edit is witnessed by an arm that fills the slots to exactly N, where one extra frame is
  visible (§3, arms (c)). That supersedes v0.3's "load-bearing at W-D's depth", which held only at N=2.
- **`deferred` in place of `use_awaitable`.**
  - **Witnessed:** `MemoryStore::store`'s leading `asio::post(…, use_awaitable)`
    (`include/fixpp/session/memory_store.hpp`). Arm (c-P) reverts it at the filled depth (§3).
  - **Headroom, witnessed incidentally:** the plain transport read (`src/transport/asio_plain_transport.cpp`,
    `async_read_some` with `redirect_error(use_awaitable, ec)`). The TLS transport's read gets the same change
    for symmetry; its zero is R-5's measurement.
    - Reverting the plain-read edit leaves W-A at 0: the read chain runs strictly before the Active pair, and
      two read frames are within N (round-2 triage O2-1, inferred).
    - The edit removes one frame from a chain that stays live for as long as the pump waits for input. That
      matters only when other coroutine work runs on the same scheduler call during the wait (§1(b), *What
      counts as cycled*).
    - No production window turns RED when it is reverted, so it is not one of B35's gated edits. Its registered
      witness is incidental: W-A's (b-S) twin runs a nest of N − r frames while the read is pending, and is
      expected to go RED on revert (inferred: the reverted read chain, r + 1 frames, plus N − r exceeds N, by
      q5's arithmetic; §3).
  - The `redirect_error(deferred, ec)` / `as_tuple(deferred)` forms compile and complete against 1.38 (round-1
    triage p5). The PR's build is the check.
- **The frameless lock op.** In `include/fixpp/core/sync/async_mutex.hpp`:
  - `detail::lock_frame`: a caller-owned object holding today's `async_mutex_awaiter` and `result` locals.
  - **The initiation stays a private member of `async_mutex` (C2-2).** Call it `lock_op_` (the name is
    plan-level). It takes `(lock_frame&, std::pmr::memory_resource*)` and returns
    `asio::async_initiate<const asio::deferred_t&, void(expected_t<async_lock_guard>)>(<initiation>,
    asio::deferred)`.
    - The initiation reads private state: `draining_`, `state_`, `active_holders_count_`, the waiter pool and
      `schedule_record_resume`. On the fast path it constructs `async_lock_guard{this}` through that class's
      private constructor, which befriends `async_mutex`.
    - As a member, `this` and every private access stay as they are today. A free function could make none of
      those accesses.
  - `detail::async_lock_op(async_mutex&, lock_frame&, std::pmr::memory_resource*)` is a thin forwarder to that
    member. `async_mutex` declares it a friend, beside today's `detail::async_mutex_awaiter` and
    `detail::waiter_record` friends.
  - `detail::finish_lock(lock_frame&)` is today's post-grant tail. It touches only the awaiter's `record_` and
    the record's `attached_awaiter_` / `release_ref`. Those are public members of the `detail::` structs
    `async_mutex_awaiter` and `waiter_record`, so the function needs no friendship. Re-check with
    `grep -n "^struct\|^class\|private:" include/fixpp/core/sync/async_mutex.hpp`.
  - **What changes in the moved initiation body.** Today's lambda captures `bound_executor` (from
    `co_await this_coro::executor`) and `inherited_slot` (from `this_coro::cancellation_state` after the total
    reset). A deferred initiation has neither. It takes both from the supplied handler before
    `awaiter.store_handler(std::move(handler))`:
    - `asio::get_associated_executor(handler)`;
    - `asio::get_associated_cancellation_slot(handler)`.
    - The handler is an `awaitable_async_op_handler` bound to the awaiting thread, so these are the thread's
      executor and its cancellation slot after step 2 below (`asio/impl/awaitable.hpp`).
    - **The condition.** The moved body may differ from today's lambda body only in three ways: the source of
      the executor and the slot, the capture list, and the names through which it reaches the caller-owned
      awaiter and `result` (now `lock_frame` members).
      - Everything else is verbatim: the draining checks, the fast-path CAS, the waiter-pool record, the
        cancellation-slot install with its fail-closed `assign`, and the always-posted contended resumption.
      - The 048 reap/drain model (E-5) is unchanged.
    - **The recipe.** Diff the PR's initiation body against today's lambda in
      `git show origin/main:include/fixpp/core/sync/async_mutex.hpp` (the lambda passed to `async_initiate` in
      `async_mutex::async_lock`). Every hunk must be one of the three kinds above.
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
    - Nothing can land between parts 2 and 3. Both `reset_cancellation_state` transforms are always ready
      (`await_ready()` returns `true` in `asio/impl/awaitable.hpp`), so part 2 never suspends, and part 3's
      pre-check runs in the same resumption. Today's `async_lock` has the same adjacency (round-2 disagreements,
      C2-4).
    - Part 1's condition has two conjuncts, `throw_if_cancelled()` and `cancelled()`, as asio's own check does.
      §3 gives each conjunct its own cell.
    - Part 5 is today's tail.
    - At the SeqnumManager sites the lock is the first `co_await`, so part 1 repeats the check the caller's own
      `co_await` of the method has just performed, with no suspension in between. It is redundant there and
      harmless. At `MemoryStore::store` it is load-bearing.
  - **The restore stays unconditional, as today.** Part 4 resets to terminal whatever filter the caller entered
    with. A caller that entered with a wider filter leaves the lock with terminal. A cell pins this (§3), so
    that a change is a decision and not an accident.
  - **Lifetime (round-1 C-5).** `lock_frame` lives in the caller's frame, from the macro to the end of that frame's scope.
    That is longer than today's `async_lock` frame, never shorter. If the caller's frame is destroyed while it
    is suspended at part 3, the awaiter's destructor runs the same E-2 path that runs today when `async_lock`'s
    frame is destroyed. `finish_lock` leaves the awaiter in the state today's tail leaves it in
    (`record_ == nullptr`), so the destructor does not release a second time.
  - **Macro hygiene.** A macro in an installed header is not namespaced and cannot be `#undef`'d after use,
    because `memory_store.hpp`'s inline `store()` expands it in consumers' TUs. It is therefore prefixed
    `FIXPP_DETAIL_`, documented as not supported API, and disclosed (§4). Its expansion names
    `detail::lock_frame`, `detail::async_lock_op` and `detail::finish_lock`, so those are disclosed with it.
- **Public `async_mutex::async_lock` is unchanged** in signature and behaviour. It is re-expressed as a thin
  coroutine over the macro, so there is one implementation. It keeps its frame, so consumers' cost is unchanged.
- **Call sites converted.** Only those inside a zero-gated window:
  - `SeqnumManager::check_inbound` (W-A, W-B) and `SeqnumManager::hydrate` (W-C) (`src/session/seqnum_manager.cpp`).
    The other SeqnumManager methods stay on the public `async_lock`.
  - `MemoryStore::store` (W-D). `next_seqnum`, `reset`, `reset_to` and `retrieve` stay on the public
    `async_lock`: no gated window reaches them.
  - FileStore, the outbound `write_gate_` and `SeqnumManager::assign_outbound` are **not** converted.
    FileStore is under the §XV.4 exemption. Under R-9, W-E gates `Session::send`'s chain (§2.5, §3), so the
    public `async_lock` frames of `assign_outbound` and of `write_gate_`, and the transport write's
    `use_awaitable` adapter, now sit on a zero-gated path. They stay unconverted: they are cycled frames that
    fit the limit, and they count in §2.4's live set. Converting them is headroom, not a gate, so B35 does
    not do it. B&L lists them.
- **Erratum E-6 to `.specify/2f-async-mutex.md`: a narrow supersession, for internal callers only.**
  - It supersedes E-1's "`async_lock(mr)` is the operation" and E-2's "`async_lock(mr)` coroutine frame"
    lifetime wording, for callers that use the macro.
  - E-1 and E-2's waiter-record, cancellation and lifetime invariants stay as they are.
  - It records that E-2's lifetime argument was re-run with the awaiter living to the end of the caller's frame.
  - It corrects E-4's "per-thread" recycler wording to the scheduler-call scope (§1(b)), without changing E-4's
    disposition.
  - It records that the recycler's slot count per purpose, `cancellation_signal_tag`'s included, is now fixpp's
    exported N (§2.4), not asio's default.
  - Its header names this note. The `brain/components/async-mutex.md` entry gets the same pointer.

### 2.4 Class (b): the exported cache size (R-1′)

- **The definition.** `ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE=<N>`, with N taken from **one source**: a constant
  in a new installed guard header (`include/fixpp/core/detail/asio_recycler_config.hpp`; the name is
  plan-level). CMake reads the constant from that header with `file(STRINGS … REGEX …)`. No other source or
  build file writes N as a literal.
- **How N is chosen: a condition, not a number.**
  - **Condition.** For every recycler purpose, N ≥ the largest number of cycled blocks of that purpose live at
    once on one scheduler call, at any instant on a zero-gated production path, plus headroom. "Live at once"
    counts every chain that shares the scheduler call (§1(b), *What counts as cycled*). That includes the
    pending inbound read during an outbound `store`, which is q5's case, and an outbound emit suspended in its
    write while an inbound message is handled. Under R-9 the outbound emit includes an application send
    through `Session::send`, whose chain is deeper than the liveness loop's emits (step 5).
  - **Headroom, as a condition.** At the largest gated instant, N leaves room for at least one more nested
    `co_await` on each chain live at that instant.
  - **The recipe that re-derives the live set.**
    1. List the long-lived coroutines of an Active session: those `co_spawn`ed once per connection or per
       Active entry and looping (the read pump in `src/session/engine.cpp`, `Session::run_liveness_loop`). Re-derive
       them with `git grep -n "co_spawn(" -- src/session src/transport`. They hold no slot.
    2. For each one, walk down its `co_await`s to every suspension point it can hold while a gated window's
       work runs. Count the cycled coroutine frames on that path, and count each `use_awaitable` adapter as one
       more. The walk uses `grep -n "co_await" src/session/session.cpp src/session/engine.cpp
       src/transport/asio_plain_transport.cpp include/fixpp/session/memory_store.hpp`.
    3. The live set at an instant is the sum over chains. Take the largest sum over every instant at which a
       gated chain requests a block.
    4. Count **r**, the pending read's own cycled frames, the same way. Walk from the pump's Active-branch
       `read_r = co_await transport.async_read_some(read_span)` (`src/session/engine.cpp`) into the
       transport. By reading the branch base, r is 1 after §2.3's plain-read edit: `asio_plain_transport::async_read_some`
       is one coroutine, and the `deferred` read adds no adapter. It is 2 without the edit. §3's W-A (b-S) pair
       and W-D-R are written in terms of r.
    5. Count **e**, the cycled frames of an application send suspended in its write, the same way (R-9). Walk
       from `Session::send` (`src/session/session.cpp`) through `send_impl`, `store_then_emit` and
       `live_write_serialized_` into the plain transport's `async_write`
       (`src/transport/asio_plain_transport.cpp`). Count its `use_awaitable` adapter, or, at the write gate,
       the public `async_lock` frame and its adapter in place of the write's two. The sender that calls
       `Session::send` is long-lived in W-E and holds no slot. A send that is itself `co_spawn`ed adds the
       spawn's entry-point frame and the spawned coroutine's frame. §3's W-D-W is written in terms of e.
       - **A send started from inside `fromApp` or `fromAdmin` stacks on the inbound arm.** It runs inline
         until its first suspension (§2.5, probe r3), so its frames count on top of `on_inbound_active_`'s, not
         beside them. That is the ordinary FIX shape of answering an order from `fromApp`. It is derived, not
         gated.
  - **The value the PR sets: N = 16** (v0.4 set 8).
    - Fable's consult proposes 8 as a floor (`fable_544_design_consult.md`, D-B(3a)).
    - **Why 8 no longer meets the condition.** v0.4 sized N on the liveness loop's emit chain:
      `store_then_emit` → `live_write_serialized_` → the transport write and its adapter. R-9 puts the
      `Session::send` chain on a gated path (W-E), and that chain adds `Session::send` and `send_impl` above
      `store_then_emit`. By reading the branch base with the recipe, the largest gated instant is now the
      inbound Active pair (`on_inbound_active_`, `check_inbound`) requested while an application send is
      suspended in its write: 2 + e. At N = 8 that instant leaves no headroom, so it fails the headroom
      condition above. This is inferred by reading; W-D-W (§3) gates it.
    - By the same reading, 16 meets the condition at that instant, and still leaves headroom when the send is
      itself `co_spawn`ed (step 5).
    - q5's case, the store chain plus a pending read, is smaller. So is W-E's own largest instant, a send
      chain plus the pending read.
    - Re-run the recipe at implementation time. The chain list above is a lead. The PR's verify record
      carries the derivation and its date. On every run, W-D-W's stand-in bracket re-checks the recycler's
      arithmetic for the derived e, and its real-chain twin (§3) checks that derived e against the production
      `Session::send` chain held in its write, in both directions (I2R2 C-1).
    - **What 16 does not reach.** `Engine::send`'s chain is deeper still: two `co_spawn` hops, each with an
      entry-point frame, a spawned lambda frame and a `use_awaitable` adapter, above `Session::send`
      (`src/session/engine.cpp`, `Engine::send`). That path allocates per call anyway (§2.5), so no zero is
      claimed for it. Whether the inbound pair stays at 0 while such a send is suspended depends on N against
      that chain. That is derived, not gated (§4).
    - Raising N does not reach O3-1. Instance-2 triage r2 measured the over-limit send's eviction identically
      at N = 8 and N = 16 on 2026-10-09: eviction is not a capacity effect. R-9's frame change (§2.5) is what
      reaches it.
  - **The pin.** (b-S) in W-D's driver harness (§3) instantiates a nest of exactly N trivial cycled frames,
    which must read 0, and a nest of N+1, which must read > 0. Both read N from the macro, so they track the
    exported value. Together they pin that the running recycler holds exactly the macro's slot count.
  - **Cost, as conditions.** asio's first-fit scan is linear in N per request. Each scheduler call can hold up
    to (purposes × N) cached blocks, each no larger than the limit plus one byte, and frees them on return
    (`~thread_info_base`). The existing benches are the check on the scan. No figure is written here.
- **Where the definition is attached.**
  - **Installed interface: a PUBLIC definition on `fixpp_core` (C-1).**
    `target_compile_definitions(fixpp_core PUBLIC ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE=<N>)`, with N read from
    the guard header. v0.4 attached it to the members that link `asio::asio`, and so left `fixpp::core` out.
    `fixpp::core` links no asio, but its installed headers include it (`clock.hpp`,
    `session_executor.hpp`, `engine_config.hpp` and `trace_context.hpp` among them).
    - **The selector is "publishes the `include/` root", not "links asio".** Every C++ member that publishes
      `$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>` exposes every installed header, the asio-including
      ones too. So link edges cannot tell which consumer TU sees fixpp's asio. Every such member links
      `fixpp_core` PUBLIC/INTERFACE, directly or transitively. One attachment therefore reaches the umbrella,
      every member, and every CMake consumer of any of them.
    - `fixpp_core`'s own TUs include no asio (`src/core/CMakeLists.txt` compiles `decimal.cpp` and
      `fix_time.cpp`; `system_clock_source.cpp` is compiled into `fixpp_session`), so the definition is inert
      there.
    - **The enumeration recipe.** Re-run it whenever a target, link edge or installed header changes.
      1. The members are `FIXPP_EXPORT_TARGETS` in `CMakeLists.txt`, plus the umbrella `fixpp` and the
         conditional `fixpp_log_otlp` appended after it.
      2. Each member's installed include root:
         `git grep -n "INSTALL_INTERFACE" -- CMakeLists.txt 'src/*CMakeLists.txt' cmake`.
      3. Each root-publishing member's PUBLIC/INTERFACE link set:
         `git grep -n -A3 "target_link_libraries" -- CMakeLists.txt 'src/*CMakeLists.txt'`.
      4. Which installed headers reach asio: run
         `python3 -I ../research/probes/b35_544_gate_a_i2r1/closure.py <module dirs, relative to include/>`
         (for instance `fixpp/core`) from the library root (I2R2 C-4). It prints each header that has an
         include chain to an `asio*` header, with the chain. The script lives in the parent repo, so a
         library-only checkout cannot run this step; the guard-header census ctest (*The mechanical guards*,
         below) is the in-repo mechanical check of the condition that matters.
    - **The condition the recipe checks.** Every member that publishes the `include/` root reaches
      `fixpp_core` through PUBLIC/INTERFACE links. A member that does not needs its own attachment. A member
      that publishes another root and reaches the closure only through `$<LINK_ONLY:>` withholds the
      definition: that is `fixpp_capi` (`include/capi`) and `fixpp_service` (`include/service-iface`, through
      `fixpp_capi`). That is correct, because their headers include no asio (*The C ABI*, below).
      Instance-2 triage C-1 holds the enumeration at `07b7ff30`, as a dated lead; the plan re-runs the recipe.
    - v0.4's single helper that coupled the `asio::asio` link with the definition is dropped. The two now
      have different selectors.
  - **In-tree, kept beside the `fixpp_core` attachment.** The same value is appended to the build tree's
    imported `asio::asio` target (`INTERFACE_COMPILE_DEFINITIONS`). Every in-tree TU that has asio's include
    path then gets it, including test-only targets that link `asio::asio` without a fixpp library:
    `fixpp_mock_clock`, and test executables under `tests/core`, `tests/otel` and `tests/sync`. Re-derive them
    with `git grep -n "asio::asio" -- '*CMakeLists.txt'`. Identical duplicates of the definition are harmless;
    the census below checks the value.
  - **Not on `fixpp::capi` or `fixpp::service`.** Their installed interfaces reach the closure only through
    `$<LINK_ONLY:fixpp::capi_objects>`, which withholds compile definitions as it already withholds
    `ASIO_STANDALONE` (`tests/consumer/run_consumer_witness.cmake`, leg 3's comment). That is the correct
    state, because the C ABI's headers include no asio (below).
- **The ODR obligation.**
  - The macro sizes `thread_info_base::reusable_memory_[max_mem_index]` and every purpose's slot range
    (`asio/detail/thread_info_base.hpp`). Header-only asio compiles `scheduler::run`'s `thread_info` and
    `thread_info_base::allocate`/`deallocate` as inline functions in every TU that uses them. So **every TU in
    the process that includes asio must see the same N.** That covers fixpp's own TUs, consumer C++ TUs that
    include asio or fixpp headers, and the python bindings.
  - **Failure mode of a mismatch.** Undefined behaviour (an ODR violation). The linker keeps one copy of each
    inline function, and which copy it keeps is unspecified. A scheduler call whose `thread_info` was laid out
    with a smaller N than the `allocate`/`deallocate` copy that indexes it reads and writes past
    `reusable_memory_`, into the rest of the object on that call's stack. The opposite mismatch makes two
    purposes share slots. Neither is diagnosed, and which one occurs can change with link order.
  - **Who inherits it, and who must define it.**
    - CMake consumers of `find_package(fixpp)` that link any `fixpp::` target except `fixpp::capi` and
      `fixpp::service` inherit the definition, through `fixpp_core`. That includes a consumer that links only
      `fixpp::core` and supplies asio itself: it gets the definition from fixpp, though not asio.
    - A CMake consumer that links only `fixpp::capi` or `fixpp::service` does not inherit it. Its fixpp headers
      include no asio, so it needs the definition only for its own asio TUs (below).
    - Non-CMake consumers (any build that uses the installed headers and archives directly) must define it
      themselves. New B&L row (§4).
    - **Static co-linking reaches C-ABI consumers too.** `libfixpp_capi.a` carries asio's inline symbols,
      compiled at fixpp's N, as weak/COMDAT definitions. Check with `nm -C <build>/lib/libfixpp_capi.a | grep
      "asio::detail::"`. A C-ABI consumer that links it into a process that has its own asio TUs at another N
      is in the same mismatch, although its own headers include no asio. The B&L row covers this case.
    - `fixpp_capi_shared` hides every non-`fixpp_*` symbol on POSIX (`src/capi/fixpp_capi.map`). It is
      test-only.
  - **The C ABI.** The C-ABI headers include no asio. `git grep -n "#\s*include" -- 'include/fix/*'` lists only
    `<stdbool.h>`, `<stddef.h>`, `<stdint.h>` and `fix/` headers. `git grep -n asio -- include/fix` finds two
    comments, in `include/fix/c_api/engine.h` and `include/fix/c_api/error.h`. So a C-ABI-only TU needs no
    definition. The static co-linking case above is the exception, and it concerns the consumer's own asio TUs.
  - **The python bindings.** The SWIG wrapper TU includes only `fix/c_api.h` and C headers
    (`bindings/python/fixpp.i`). `fixpp_py` links `fixpp_capi` in-tree, so the asio TUs inside the module are
    fixpp's own, built with the in-tree definition. The wheel is built from the same CMake tree, so it
    inherits it too. Nothing else in the wheel includes asio.
- **The mechanical guards.**
  - **Consumer and in-tree TUs that include a fixpp header: a `static_assert`.** The guard header includes
    `<asio/detail/thread_info_base.hpp>`, which defines the macro as 2 when no one else has. It then asserts
    `ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE == FIXPP_ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE`, the header's constant,
    with a message that names the obligation. So a missing definition and a wrong value both fail to compile,
    whichever of asio and fixpp the TU includes first.
    - The header never `#define`s the asio macro itself: that would hide a mismatch, not detect it.
    - It is a `static_assert`, not `#error`. The `[const §XV.9]` corpus gates in `tests/sync/CMakeLists.txt`
      preprocess public headers with `-E` and `-I` flags only. A `static_assert` is not evaluated there, but an
      `#error` would fire.
    - **Condition: every installed header that includes an asio header includes the guard header.** Recipe:
      the set difference of `git grep -l "#\s*include <asio" -- include` and
      `git grep -l "asio_recycler_config.hpp" -- include`, with the guard header itself removed from the first
      set, must be empty. The guard header includes asio and does not name itself, so without that exclusion
      the recipe would always report it. It is registered as a ctest beside
      the corpus gate, with a seeded positive: a fixture header that includes asio and not the guard must be
      reported.
    - A negative-compile ctest compiles one TU that includes a fixpp header with the definition omitted. It
      asserts that the compile fails **and** that the compiler output carries the guard's message token, so a
      failure for another reason cannot pass it.
  - **In-tree TUs that include asio and no fixpp header: a compile-command census.** A ctest reads the build's
    `compile_commands.json` (every preset sets `CMAKE_EXPORT_COMPILE_COMMANDS`). It requires every entry whose
    command carries asio's include directory to carry `ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE=<N>`, with N read
    from the guard header. Its positive control runs the same check over a copy of the file with one entry's
    definition stripped, and must report that entry.
  - **What no guard covers.** A consumer TU that includes asio but no fixpp header, linked into the same
    process. GCC and Clang have no cheap link-time check for it: asio's inline symbols carry no tag for the
    macro. MSVC's `#pragma detect_mismatch` would record a value only in TUs that include the guard header,
    and those are already checked at compile time, so it adds nothing. That case is the B&L row's obligation.
- **The consumer witness.** `tests/consumer/run_consumer_witness.cmake` asserts closed usage-requirement sets.
  - **Leg 3 is not re-scoped.** It checks `probe_usage_requirements`, which links only `fixpp::capi`. Its closed
    expectation stays **empty** for `COMPILE_DEFINITIONS`.
    - **Why it should stay green.** `ASIO_STANDALONE` already sits in the same closure, and it reaches that
      closure the same way the new definition will: through `fixpp_sync`'s PUBLIC `asio::asio` link, inside
      `$<LINK_ONLY:fixpp::capi_objects>`. Leg 3 is green today with it withheld. The new definition sits behind
      the same boundary, so it is withheld too. That is inferred from today's green leg; the witness run is
      the check.
    - If leg 3 goes red with the new definition, the definition leaked through `fixpp::capi`. That is a
      defect in the attachment, not in the leg.
    - **Disagreement with Fable, kept visible.** Fable's consult cites "250-256" and predicts that leg 3 "will
      fail until re-scoped". Those lines correspond to leg 3's `file(GENERATE)` producer in
      `tests/consumer/CMakeLists.txt` and its reader in `run_consumer_witness.cmake`. This note reads them
      differently, and the witness run decides.
  - **Two new legs for the C++ consumer.** `tests/consumer/CMakeLists.txt` writes, with `file(GENERATE)`, the
    observed `COMPILE_DEFINITIONS` of two probes: `probe_umbrella`, which links `fixpp::fixpp`, and a new
    `probe_core`, which links only `fixpp::core` (C-1). The driver passes in the expected N, read from the
    same guard header. Both legs use the same check, below.
    - `probe_core` is the leg that turns red if the attachment moves off `fixpp_core`. The umbrella leg alone
      cannot see that, because the umbrella would still inherit the definition from any member that carries it.
    - `run_consumer_witness.cmake` keeps the entries that match `^ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE(=|$)` and
      removes identical duplicates. The kept set must **equal** exactly `ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE=<N>`.
    - It goes red when the definition is missing (empty set), when it has the wrong value (a different entry),
      and when two different values arrive (two entries).
    - Its arms are run once and recorded in the verify record: delete the `fixpp_core` attachment, which
      turns both sets empty; and attach a different literal, which mismatches.
    - Independently, `consumer_witness.cpp` includes fixpp headers, so the guard's `static_assert` stops that
      build too.
- **Other dependencies that include asio: none.**
  - `conanfile.py` requires `asio/1.38.0` directly. Its recipe's `package_info` defines only `ASIO_STANDALONE`.
  - **Headers.** No other package in the Conan cache ships a header that includes asio. The scan, run on
    2026-10-09, matched asio's own package only:
    `for d in ~/.conan2/p/*/p/include; do grep -rlE '#\s*include\s*[<"](asio|boost/asio)' "$d" | head -1; done`.
  - **Binaries.** No package library carries an `asio::` symbol. The scan, run on the same date, found none:
    `find ~/.conan2/p -path '*/p/lib/*' \( -name '*.a' -o -name '*.so*' \) -exec sh -c 'nm -C "$1" 2>/dev/null | grep -q "asio::" && echo "$1"' _ {} \;`.
    Its positive control is fixpp's own `libfixpp_session.a` from any build tree, on which `nm -C … | grep -c
    "asio::"` is non-zero.
  - That covers OpenTelemetry, prometheus-cpp, civetweb (`conan/recipes/civetweb`), curl, gtest, benchmark
    and the rest. Re-run both scans whenever a dependency is added or bumped.
  - No build file sets another asio macro: `git grep -n "ASIO_" -- CMakeLists.txt cmake conanfile.py
    CMakePresets.json` finds no definition.

### 2.5 Class (b) on the send path: `Session::send_impl`'s frame (R-9)

- **The defect (instance-2 triage O3-1).** `Session::send_impl` (`src/session/session.cpp`) declares three
  `std::array<std::byte, 4096>`: `strip_buf`, `body_buf` and `buf`. They are in scope across its
  `co_await`s, so its frame is over the limit on every compiler. Every application send goes through it:
  `Session::send` awaits it, `Engine::send` reaches `Session::send` on the session strand, and the C ABI
  calls `Engine::send` (`src/capi/session.cpp`, `fixpp_session_send`).
  - An over-limit request fits no cached block, so it evicts the first occupied slot of its purpose before
    `aligned_new`. On free, a block over the limit is deleted, not cached (`asio/detail/thread_info_base.hpp`,
    `allocate` and `deallocate`).
  - Instance-2 triage r2 measured the consequence in a model on 2026-10-09. An inbound Active pair
    interleaved with an over-limit send allocated inside the inbound pair once per message, at N = 8 and
    N = 16, on g++ and clang++. With an in-limit send it read 0. r2 is sequential, so it shows the mechanism
    and its insensitivity to N, not production's rate. Re-run it; do not cite its numbers.
- **Lifetimes, by C-3's discipline (§2.2).** The decision is data flow, not lexical scope.
  - **`strip_buf` and `body_buf` are class 1.** Every view into them is last read when the body is copied
    into `buf`. Those views are `app_payload` (rebound to `strip_buf`), the `pv` views, `msgtype_field` and
    `rest_payload`. That copy happens before `toApp` runs and before `send_impl`'s first `co_await`, which is
    `close()` on the `app_callback_threw` path or `seqnum_mgr_.assign_outbound()`. The lexical crossing scan
    reports them as crossing only because they are declared at function scope.
  - **`buf` is class 2.** `toApp`'s parse reads it synchronously, and then `store_then_emit` reads it across
    the store's suspension and the write (§2.2, *Why class 2 cannot use a helper*).
  - A class-2 leaf is not available: a frame that owns 4096 B is over the limit by definition. That is why R-9
    asks for session-owned storage.
- **What serialises `send_impl`: nothing, across its suspensions (the R-9 premise, checked against the code).**
  - The session strand runs one handler at a time, so it serialises coroutines only **between suspensions**.
  - Every send suspends at least once after building `buf`. `MemoryStore::store` begins with `asio::post`
    (`include/fixpp/session/memory_store.hpp`), which always suspends, and §2.3's `deferred` edit is still
    a post. A send can also suspend in `assign_outbound`'s lock, in `write_gate_`'s lock and in the write.
  - `write_gate_` is acquired inside `live_write_serialized_`, after the store, and covers only the write.
    Taking it before `buf` is built would make `store_then_emit`'s own `live_write_serialized_` lock it a
    second time, and `async_mutex` tracks no owner, so a holder that locks again waits for itself (inferred
    from `include/fixpp/core/sync/async_mutex.hpp`, which counts holders and records no owner).
  - The seqnum mutex is held only inside `assign_outbound`.
  - **The paths that overlap two `send_impl` invocations:**
    - concurrent `Engine::send` or `fixpp_session_send` callers. Each `Engine::send` `co_spawn`s its own
      coroutine onto the session strand (`src/session/engine.cpp`, `Engine::send`, Step C). The second can
      run while the first is suspended in its store or its write;
    - a C++ consumer that `co_spawn`s `Session::send` more than once without awaiting each;
    - **a send started from inside a callback on the session strand, which runs inline.** asio's `co_spawn`
      enters through `co_spawn_dispatch` (`asio/impl/co_spawn.hpp`, `co_spawn_entry_point`). A strand's
      `dispatch` runs the function inline when the strand is already running on this thread, and an idle
      strand dispatches its invoker to the inner executor, which also runs inline inside the `io_context`
      (`asio/detail/impl/strand_executor_service.hpp`, `dispatch`).
      - Probe r3 (`b35_544_gate_a_i2r1/r3_reentrant_send_inline.cpp`) measured this on 2026-10-09 with g++
        and clang++. From inside a coroutine on the session strand, a synchronous callback started a nested
        send in three ways: `co_spawn` onto the session strand, `co_spawn` of `Engine::send`'s two-hop shape
        onto the `io_context` executor, and onto the control strand. In all three, the nested body ran inline,
        before the callback's `co_spawn` returned. Its control, `asio::post`, did not.
      - So a send that `toApp` starts runs nested inside the outer `send_impl`, while the outer `buf` is live
        and `toApp`'s `MessageView` points into it.
  - Liveness, admin and reply emits never touch `send_impl`'s storage: they build into their own buffers
    (`run_liveness_loop`, §2.2).
  - **So the existing serialisation cannot guarantee exclusive use of one session-owned `buf`.**
- **The implemented form (R-10: flag + fallback).**
  - **Storage.** It is session-owned and allocated with the `Session` object, so once per session and never
    per message. The Engine creates that object with `std::make_shared<Session>` once per accepted connection
    or connect loop, before `open()` (`src/session/engine.cpp`). That meets `[const §XV.1]`: pre-allocated
    per session at open, never per message. It holds:
    - the **build scratch**, two 4096-B arrays standing in for `strip_buf` and `body_buf`;
    - the **frame slot**, one 4096-B array standing in for `buf`;
    - the **slot flag**, a `bool` read and written only on the session strand.
    - Whether the storage is a member or one block owned by the `Session` is plan-level. Either way
      `Session`'s layout changes (Trigger; §4). Before growing the object, grep the whole repo for a size pin:
      `git grep -n "sizeof(Session)\|sizeof(fixpp::session::Session)\|sizeof(session::Session)"`.
  - **The build: one non-coroutine helper (class 1).** The excision and partition pass and the framing move
    into a non-coroutine helper. It writes the finished frame into a caller-supplied span and returns its
    length, or the same error the inline code returns today.
    - It uses the build scratch without the flag. It is synchronous and calls no user code: `toApp` runs
      after it returns. So on the strand nothing else can use the build scratch while it runs. That includes
      a send nested inside `toApp` (r3), because the outer send's build has finished by then.
    - The 020 checks and the scanner pass use no scratch. Whether they move into the helper is plan-level;
      their order and dispositions do not change.
  - **The frame slot: held by the flag (class 2).**
    - Before the helper writes the first byte of the frame, `Session::send` tests the flag. If the flag is
      clear, `Session::send` sets it through an RAII holder, and the helper builds into the slot. The holder
      lives in a frame that outlives `store_then_emit`'s return: `Session::send`'s, under the shape below.
    - The holder clears the flag when that invocation ends: every `co_return`, the throw path, and destruction
      of the frame while it is suspended. It is cleared **only after `store_then_emit` returns**, because the
      write reads the slot until then. A store failure or a vetoed `toApp` releases it when that
      invocation's `co_return` unwinds the holder's frame.
    - The release is by RAII on every exit, never by a manual clear. Condition: the holder is declared inside
      `Session::send`'s `try` block, so it is released before that block's `catch` runs.
    - Instance-2 round-2 triage s1 (`../research/probes/b35_544_gate_a_i2r2/s1_holder_release.cpp`) exercises
      an RAII holder in an `asio::awaitable` callee under an awaiting, catching wrapper, on four exits: a
      normal return, an early return before any suspension, a throw after a suspension, and destruction of the
      suspended frame by `~io_context`. It places the holder in the callee, not in the wrapper's `try`. To
      check the chosen shape, re-run s1 with the holder moved into the wrapper's `try`; do not cite its
      output. The veto-then-zero cell (§3) gates the non-success exit (I2R2 C-3).
    - The flag is a strand-local `bool`, not a lock. Nothing waits on it, it adds no suspension point, and
      `close()` and `Engine::stop` neither see it nor drain it.
  - **The fallback leaf, for an invocation that finds the flag set.** That invocation `co_await`s a private
    leaf coroutine. The leaf's frame owns a 4096-B frame buffer, and the leaf `co_await`s the same tail,
    `send_impl`, over that buffer: the helper into the leaf's buffer, `toApp`, `assign_outbound`,
    `store_then_emit`.
    - Its frame is over the limit by construction. A send that takes it allocates one frame through
      `aligned_new` and evicts one cached block, by O3-1's mechanism. That is bounded, not zero (§4).
    - **R-6 residual, as a condition.** When a send starts while the slot is held, because it is nested in
      `toApp` (r3) or overlaps a send suspended in its store or its write, it takes the leaf and allocates.
      R-10 discloses this under R-6.
    - The fallback buffer cannot be a conditionally declared local inside the frame that tests the flag
      (`Session::send`'s) or inside `send_impl`. A coroutine frame's size is fixed at compile time, so any
      local of either sizes its frame on every call. It must be a separate coroutine.
    - **One implementation of the tail (I2R2 C-2).** The condition: on the primary path, the send chain gains
      no cycled frame over today's `Session::send` → `send_impl` → (`assign_outbound` | `store_then_emit` →
      …); the fallback adds exactly the leaf. §2.4's e counts the primary path.
      - **The shape that meets it.** `Session::send` hosts the flag test and the holder, and `send_impl`
        becomes the tail, taking the frame span. Primary path: `Session::send` (holder) → `send_impl` (slot),
        which is today's depth. Fallback: `Session::send` → leaf (owns the buffer) → `send_impl` (leaf
        buffer), which adds exactly the leaf. Condition: every caller of `send_impl` is `Session::send` or the
        leaf; check it with `git grep -n "send_impl(" -- src include`.
      - No non-coroutine dispatcher is introduced on the send path. So `send_impl`'s span and
        `bool& disconnect_required` parameters fall under §2.2's clause for a callee `co_await`ed directly:
        the caller's frame outlives that `co_await` (I2R2 O4-1).
  - **`send_impl`'s frame after the change** holds no array. Its frame must fit the limit on the three
    Release presets (§1(b), condition 1); W-E is that check (§3).
  - **Behaviour is unchanged.** The validation, the field order, the dispositions, the seqnum ordering, the
    store-before-transmit ordering and the cancellation points are all the same. The existing session suites
    are the behaviour oracle. A new overlap cell (§3, *Behaviour*) covers what no allocation gate can see:
    a broken flag corrupts bytes silently.
- **What R-9 does not reach: the allocations of `Engine::send` and the C ABI.** R-9 removes the over-limit
  frame from `Session::send`'s chain. The two public entry points above it still allocate on every call, and
  B35 does not change them:
  - `Engine::send` copies the payload into a `std::vector<std::byte>` and makes two `co_spawn` hops, one
    through `control_strand_` (`src/session/engine.cpp`, `Engine::send`);
  - `fixpp_session_send` adds a `co_spawn` with `asio::use_future`, whose handler `allocate_shared`s its
    promise (`src/capi/session.cpp`; `asio/impl/use_future.hpp`).
  - So no send through those entry points is zero. W-E therefore drives `Session::send` directly (§3). Their
    chains also hold more cycled frames than W-E's, which bears on the inbound pair (§2.4, *What 16 does not
    reach*; §4).
- **Rejected by R-10: a session send gate.** This would be a session-owned
  `async_mutex`, taken through the frameless macro before the frame is built and held until
  `store_then_emit` returns. It would keep an overlapping send at zero too. R-10 rejects it for its drain,
  deadlock and cancellation conditions:
  - sends queue behind each other's store and write. With FileStore, a send's build waits for the previous
    send's durable store;
  - the gate must join `close()`'s and `Engine::stop`'s drain sequence, and its waiters must be cancellable by
    `root_cancel_`;
  - the `app_callback_threw` path `co_await`s `close()` inside `send_impl`. It would need the gate released
    first, or `close()` would wait on its own caller;
  - a cancellation pending on entry would now throw at the gate's part-1 pre-check, earlier than today's
    first `co_await`. A send that today returns a validation error or a `toApp` veto would then disconnect.
- **Also rejected:**
  - `write_gate_` taken early: it self-deadlocks (above).
  - A per-send leaf that owns `buf`: that is the over-limit frame itself.
  - A pool of K slots: the number of concurrent sends has no bound, so a pool still needs a fallback. No
    measurement shows overlap common enough to size K.
- **A pre-existing defect that r3 exposes, outside B35 (inferred; not reproduced at session level).**
  `send_impl` peeks the outbound MsgSeqNum before `toApp` and stamps the frame with the peeked value. It
  advances the counter with `assign_outbound` after `toApp`, and stores and transmits under the peeked value.
  - A send nested inside `toApp` runs inline (r3). It peeks the same value, assigns it, and reaches its store
    before the outer send resumes.
  - The outer send then assigns the next value, but stores and transmits under the value it peeked.
  - The result is two frames carrying one MsgSeqNum, and a counter that has advanced by two.
  - `include/fixpp/session/engine.hpp` documents that "Re-entrant calls from on-strand callbacks are enqueued
    behind the current dispatch". r3 contradicts that for every `co_spawn` form it tried.
  - **Filed as fixpp#563 (label `bug`; UNCONFIRMED; placed in batch B28).** B35 does not fix it. B35's
    overlap cell asserts each frame's bytes, not MsgSeqNum uniqueness, so it does not depend on the defect
    (§3).

## 3. Tests and gates

**RED task 0, before any edit: take the baseline.**
- On this branch's base (`d51ce86d`), run W-A..W-E in the window shapes below, under mallocnesia
  (`tools/check_alloc.py`), on `linux-clang-release` and `linux-gcc-release`. Record the per-window counts in
  the verify record, with the command.
- At the base, the slot count is asio's default. W-D-R and W-D-W have no base form, because the base has
  no harness for them. Their RED evidence is each bracket's arm (§3, *Windows*). W-E has a base form: its
  rig drives today's `Session::send`, whose frame is over the limit (§2.5).
- **The TLS diagnostic (R-5, C-5).** In the same step, run W-A's run-thread shape on a TLS rig under the
  full mallocnesia counter, on both Linux Release presets, before and after the change.
  - Record the interceptions by entry (`malloc`, `aligned_alloc`, …) and the stack that `tools/check_alloc.py`
    prints for each.
  - **Disposition.** A hit that originates in OpenSSL's per-record path, under asio's
    `SSL_MODE_RELEASE_BUFFERS` (`asio/ssl/detail/impl/engine.ipp`), goes to a new labelled issue, outside B35.
    A hit in fixpp or asio code on the TLS read path is a B35 defect, because §2.3 changes the TLS read.
  - It is diagnostic: it registers no gate. R-5 is an orchestrator default, not a ruling.
- The pump window has never been measured under the full counter.
- This baseline is **supporting measurement** only. It cannot attribute anything to §2.2, because classes (a)
  and (c) are present in the same window at the base. §2.2's recurrence arm is (b-L), planted in W-A's own window.

**The window rule (RC-1).** Warm-up and measurement for every gated window run inside **one** scheduler call on
the thread that runs the session's handlers. A window that spans several `run*` / `poll*` calls starts each one
with an empty cache, so it cannot read zero (§1(b)). Two shapes satisfy the rule:
- **Driver coroutine at the modelled caller depth (W-C, W-D; O2-1).**
  - One coroutine, the *driver*, is `co_spawn`ed **before** arming. Its spawn and its completion lie outside the
    window, so its completion token does not matter. It lives for the whole window, so it is not cycled
    (§1(b)).
  - In a production window, the driver awaits each iteration through exactly **one test-owned wrapper
    coroutine**, and the wrapper awaits the operation: driver → wrapper → operation. The (c) arms use more
    wrappers (the template below).
    - The wrapper is a named coroutine function with its own frame.
    - No lambda coroutine is used on this path. A lambda coroutine adds a cycled frame that nobody chose:
      round-2 triage q3's first run wrapped each call in one, and every row moved one column right (q3,
      *Control*).
    - Only non-coroutine forwarders may sit between the driver and the wrapper: functions that return an
      awaitable without being coroutines. Nothing sits between the wrapper and the operation.
  - **One template provides the driver and the wrapper.** W-C, W-D, W-D-R and their (c) arms all instantiate
    it. It takes the callee and a **wrapper depth D**: exactly D test-owned wrapper frames between the driver
    and the callee, with a non-coroutine forwarder at depth 0.
    - The production windows use D = 1, the modelled depth.
    - The (c) arms and their production twins use the **filled depth**: D chosen so that D plus the
      production callee's own cycled frames equals exactly N. Under the macro, `store()` and `hydrate()` each
      cycle one frame, so D = N − 1. The plan re-derives the callee's own count with the §2.4 recipe.
  - **The depth pin is therefore a construction, not prose (v0.4, at the exported N):**
    - at the filled depth, the production callee reads 0 and each single-edit replica reads > 0;
    - one wrapper fewer, and every (c) arm reads 0 and fails its `--expect-violation`;
    - one wrapper more, and the filled-depth production twin reads > 0.
    - The v0.4 rewrite measured this on 2026-10-09 with g++ and clang++ at N = 2 and N = 8.
      - The probe is q3's three store forms (macro; public lock; `use_awaitable` post), driven through a
        `padder<D, S>` template: D coroutine wrappers, with a non-coroutine forwarder at D = 0. It was built
        with q3's build line plus `-DASIO_RECYCLING_ALLOCATOR_CACHE_SIZE=<N>`.
      - At D = 1 and N = 8, all three forms read 0.
      - At D = N − 1, the macro form read 0 and both reverted forms allocated on every store.
      - At D = N, the macro form allocated too.
      - At N = 2, D = 1 is q3's production depth, and q3's rows reproduced.
      - The probe is `b35_544_gate_a_r2/q6_padded_depth.cpp`, filed beside q5 by the orchestrator from the
        rewrite's scratch copy. Re-run it; do not cite its numbers. v0.5 raises the exported N to 16
        (§2.4), at which q6 has not been run: the plan re-runs it there.
  - Inside the driver: a fixed **K** warm-up iterations, then `alloc_guard_start()`. It then awaits **M**
    iterations (M, not N, which is the cache size), with no `co_spawn` and no `use_future` in between, calls `alloc_guard_end()`, and returns.
  - All of it runs under one `ioc.run()`.
  - The existing `W8_NoHeap_RehydratePath` and `perf_store_alloc_guard` shapes are **not reused verbatim**.
    Each spawns with `use_future` inside its window, and `use_future`'s handler `allocate_shared`s its promise
    (`asio/impl/use_future.hpp`). Each window is also a fresh scheduler call:
    - `run_batch` → `run_to_exhaustion_or_report` calls `ioc.run()` per batch;
    - `run_window_then_ready` drives `run_for` slices (`tests/support/pump_until_ready.hpp`).
- **Which production chain each depth models.**
  - **The recipe.** List the call sites with
    `grep -n "co_await store_then_emit(\|ensure_hydrated_(\|seqnum_mgr_.hydrate(\|store_->store(" src/session/session.cpp`.
    - For each site, walk up the enclosing coroutines. Count the cycled frames between the operation and the
      nearest frame that lives for the whole session, such as `Session::run_liveness_loop`'s loop, which is
      `co_spawn`ed once per Active entry.
    - The window models the shallowest chain.
  - **W-D** models `run_liveness_loop` (long-lived) → `store_then_emit` (cycled) → `MemoryStore::store`. That is
    the shallowest chain to `store()`.
    - At this depth and asio's default N, round-2 triage q3 measured the production form at 0, and each of
      `store()`'s two edits reverted at one allocation per store (g++ and clang++). At the exported N, a
      reverted edit at this depth stays within the slots. The (c) arms therefore run at the filled depth
      instead (above).
    - Deeper chains, such as an inbound arm's reply through `store_then_emit`, add cycled frames and are not
      gated (the outbound path, §7). Within N they are expected to read 0 as well, but B35 does not claim it.
  - **W-C** models `ensure_hydrated_` → `SeqnumManager::hydrate`. That is feature 025's W8 apply-step proxy
    (`specs/025-refresh-on-logon/data-model.md`, row W8). The full production chain is deeper.
    - `ensure_hydrated_` is itself awaited from the Logon handling in `Session::on_inbound_frame` (after §2.2,
      the Logon arm) and from `Session::emit_initiator_logon_`. Both are cycled per Logon.
    - So at least three cycled frames are live at the lock, even with the macro. v0.3 placed that full Logon
      chain under the mixed-traffic residual, because it exceeded the default two slots. Under R-1′ it is
      within N if every frame on it fits the limit. It is still **not gated**, and B35 claims no zero for it
      (§4).
    - W-C's wrapper depth models W8's apply step. The hydrate conversion is witnessed at the filled depth by
      arm (c-H) (inferred by analogy with q3's store rows; the arm is the check).
  - **The concurrent read.** When one thread runs the session's handlers and the session is Active, the
    pump's transport read is pending whenever the liveness loop runs. That read's frame is then live on the same
    scheduler call, and it is cycled once inbound traffic re-arms it.
    - q5 models this. With a read re-armed every iteration, the store chain at W-D's depth allocates once per
      iteration at asio's default N, and reads 0 once N covers the read frame. Its controls read 0 at every N
      (§0, R-1′; §1(b)).
    - v0.3 left this interleaving ungated and disclosed it. Under R-1′ it is gated: **W-D-R** is W-D with that
      pending read live (*Windows* below).
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
    arms; it writes M+1 frames through the peer socket, each after the previous one's completion signal; it
    waits for the M+1-th signal; it disarms.
  - Read cycles 1..M lie wholly inside the window. Cycle *i*'s tail, including the next read's initiation, must
    run before frame *i+1*'s read can complete.
  - **The completion signal must neither allocate nor race.** mallocnesia counts process-wide, so the waiting
    thread must not allocate.
    - `SeqnumManager::next_inbound_unsafe()` is a plain member read (`include/fixpp/session/seqnum_manager.hpp`).
      Polling it from the test thread while the run thread writes is a data race that the TSan diagnostic lane
      would report.
    - The signal is a `std::atomic` that a test-owned Application callback bumps on the session strand:
      `fromApp` for W-B, and `fromAdmin` for W-A. In W-A's (b) arms and twins, the plant iteration bumps it
      instead (arm (b) below).
    - The plan confirms that an in-sequence Active Heartbeat reaches `fromAdmin` (`grep -n "fromAdmin("
      src/session/session.cpp`). If it does not, W-A uses another test-owned hook on the strand.
  - The ported MSVC rig uses the same shape: the IOCP scheduler scopes its cache per call too (§1(b)).
- **Warm-up is a fixed count K**, not "until two counts agree" (O-5). mallocnesia defines only
  `alloc_guard_start` / `alloc_guard_end`, and `alloc_guard_end` exits on a non-zero count, so a registration
  cannot compare counts.
  - **K is set by a recipe (O2-3).** One pass is not enough: a size-inverted nest needs more than one pass to
    converge (§1(b), *Warm-up*). K is at least the larger of two values:
    1. the smallest warm-up at which `q2c_random.cpp`, built with `-DASIO_RECYCLING_ALLOCATOR_CACHE_SIZE=<N>` and
       run with arguments `<depth> <warm>`, reports no allocating pattern. `<depth>` is the largest live set a
       gated window or twin holds, which is N for the (b-S) and filled-depth twins;
    2. the smallest K at which each gated window reads 0 on each gated preset, found by hand by raising K under
       `tools/check_alloc.py`.
  - Both values go in the verify record, with their commands. A K that is too small fails toward red.
- **Harness additions: which way they fail depends on what the registration asserts (O2-2).**
  - In a window that asserts 0, anything the harness adds fails toward red. A synchronous peer write that
    allocated would read > 0.
  - In an arm that asserts > 0 (`--expect-violation`), a harness allocation is a **spurious pass**: it fails
    toward green. `--expect-entry` cannot exclude it when the harness allocation goes through the same hook.
  - So every arm has an **in-boundary twin**. The twin uses the same harness and the same launch path, with the
    planted defect absent, and it is registered to read 0. The twin is what excludes a harness hit.
  - The plan keeps harness additions out of the windows, or proves them allocation-free through the twin.

**Windows.**
- **W-A.** An Active inbound Heartbeat through the real Engine pump: T044's rig, re-shaped to the run-thread
  form above, with plain TCP and MemoryStore.
- **W-B.** The same rig with an application message and a registered `fromApp`, so `[const §XV.1]`'s parse →
  validate → **dispatch** runs. A Heartbeat never reaches dispatch.
  - `fromApp` runs synchronously inside `parse_and_dispatch_` (`src/session/session.cpp`). No
    `cancellable_dispatch` coroutine is on this path: `git grep -n cancellable_dispatch -- src include` finds
    only its definition and an error enumerator.
- **W-C.** `SeqnumManager::hydrate`, in the driver-coroutine form at the depth above. It runs on W8's rig with
  a persistent store (`make_reconnect_initiator(…, /*persistent=*/true)` in
  `tests/session/test_refresh_on_logon.cpp`).
  - It does not run on a MemoryStore-backed session. `Session::ensure_hydrated_` returns before `hydrate` when
    the store is not persistent (`store_is_persistent_`), so production never calls `hydrate` there.
  - The window calls `hydrate` on the SeqnumManager directly, so the store does no work inside it.
- **W-D.** `MemoryStore::store` steady state, in the driver-coroutine form at the depth above, over pre-built
  frames.
- **W-D-R (new in v0.4; R-1′).** W-D with a pending inbound read live, which models q5 at production depth. It
  must read 0.
  - A long-lived test-owned *pump* coroutine is `co_spawn`ed before arming. It loops over a chain of r child
    coroutines, the innermost awaiting a test-owned timer. That chain is r cycled frames, standing in for the
    transport read after §2.3's plain-read edit (r from §2.4, recipe step 4).
  - Each iteration, the driver cancels the timer and yields twice with `asio::post(ex, asio::deferred)`, so
    the read completes and the pump re-arms it. The driver then awaits the store through the wrapper, as in
    W-D. q5's driver has this shape.
  - **The stand-in's frame count is r, taken from the production read chain** by §2.4's recipe: the pump in
    `src/session/engine.cpp` → `async_read_some` in `src/transport/asio_plain_transport.cpp`.
  - **The bracket, so that the zero is not vacuous.** It is the harness rule's twin pair, applied to the
    stand-in. In W-D-R's own harness, with the read live:
    - a (b-S)-style nest of N − r frames is the twin and must read 0;
    - a nest of N − r + 1 frames is the arm and must read > 0.
    - The arm proves the stand-in really holds a slot. Without a live, cycled read frame it would read 0
      (q5's "no read" control).
- **W-E (new in v0.5; R-9). Two-way: the inbound Active pair interleaved with application sends.** It must
  read 0.
  - **Rig.** W-B's rig: the run-thread shape, an application message and a registered `fromApp`. A long-lived
    test-owned *sender* coroutine is `co_spawn`ed on the session's executor before arming, like W-A's
    launcher. It waits on a strand-side signal that the test-owned `fromApp` sets. Each wake performs one
    `co_await session.send(payload)` over a pre-built payload, and then bumps the completion atomic. In W-E
    the sender bumps it, not `fromApp`. So the test thread writes frame *i+1* only after send *i* has
    completed.
  - **The wake must complete by post**, for instance a timer-cancel completion. Then the sender runs after
    the inbound arm has returned, beside it rather than on top of it. A send started inside `fromApp` itself
    would run inline, stacked on `on_inbound_active_` (r3). That shape is derived, not gated (§2.4 step 5;
    §4).
  - **Why `Session::send` and not `Engine::send`.** `Engine::send` and the C ABI allocate on every call
    (§2.5). mallocnesia counts process-wide, so a window through them could never read 0.
  - **The peer must drain.** The rig's peer keeps an async read in flight (`start_reading()`), so the writes
    complete. In W-E that read completes and re-arms once per send, inside the window. It is a callback with
    a fixed buffer and no coroutine frame. If it allocated, W-E would read > 0, which fails toward red (the
    harness rule above).
  - **What W-E gates.**
    - Every frame on the `Session::send` chain fits the limit, `send_impl`'s first among them (condition 1).
    - That chain plus the pending read fits N (condition 2).
    - It does **not** hold the inbound pair live while a send is suspended in its write. The send completes
      before the next frame is written. So W-E does not cover N's two-way sizing instant; W-D-W does.
  - Its fix-deleted arm is (e) below, and its twin is W-E itself.
- **The veto-then-zero cell (new in v0.5.1; I2R2 C-3). A non-success exit must release the slot.** It must
  read 0.
  - **Why.** A missed clear on a success path is caught by W-E: its first send would leave the flag set, and
    every later send would take the over-limit leaf. A missed clear on a non-success exit is silent: bytes
    stay correct, so the overlap cells pass, and W-E's sends all succeed. The RAII holder (§2.5) prevents it
    by construction; this cell pins it.
  - **Rig.** W-E's rig. Before arming, one `Session::send` is vetoed by the test `toApp`. The window is then a
    W-E-shaped run of sends, which must read 0: each must find the slot clear.
  - Optionally, the cell asserts the flag clear through `session_test_access` after a send that took the
    `app_callback_threw` path (`Session::send_impl`'s `toApp` throw arm).
  - **Mutant, which must turn it RED:** the RAII holder replaced by a manual clear that the veto path skips.
  - Cancellation and frame-destruction cells are not added: under RAII those exits are structurally the
    veto's (§2.5, instance-2 round-2 triage s1).
- **W-D-W (new in v0.5; C-4; real-chain twin added in v0.5.1, I2R2 C-1). The two-way sizing instant, in
  W-D-R's harness: a stand-in bracket that pins the recycler boundary, and a real-chain twin that pins e.** Each
  must read 0.
  - This is the instant §2.4 sizes N on: the inbound Active pair requesting blocks while an application send
    is suspended in its write.
  - **The stand-in bracket.** A long-lived test-owned *emitter* coroutine is `co_spawn`ed before arming. It
    loops over a chain of e cycled child coroutines, the innermost awaiting a test-owned timer. That chain
    stands in for the send chain suspended in its write. **e is taken from the production chain** by §2.4's
    recipe, step 5. If the recipe miscounts e, this bracket stays consistent with the wrong e, so it pins the
    recycler's arithmetic, not the production chain's depth. The real-chain twin below pins the depth.
  - Each iteration, the driver awaits an **Active-depth replica pair** through the shared template at D = 1.
    The wrapper stands in for `on_inbound_active_`, and the callee is a test-owned coroutine shaped like
    `check_inbound`, so the pair is two cycled frames. The driver then cancels the emitter's timer and yields
    twice with `asio::post(ex, asio::deferred)`, so the chain completes and re-arms.
  - **The bracket, shifted by e and the pair.** The replica callee awaits a nest of k trivial cycled frames.
    - k = 0 is the window.
    - k = N − e − 2 is the twin, and must read 0. It fills exactly N.
    - k = N − e − 1 is the arm, and must read > 0. It proves the emitter really holds e slots while the pair
      runs.
  - **The headroom condition, as a check.** §2.4 requires N − e − 2 ≥ 2: one more nested `co_await` per
    chain. A `static_assert` in the harness TU states it, from the macro and the harness's e. That makes it
    exactly as current as e, and e is re-derived by the recipe whenever the send chain gains a `co_await`
    (§6 risk 4).
  - **The real-chain twin (I2R2 C-1).** The same bracket, with the stand-in emitter replaced by the real
    `Session::send` chain held in its write by a write-blocking transport double. The tree already holds a
    public send in that state deterministically: `ControlledWriteTransport`'s `arm_block()`/`release()`
    (`tests/session/test_live_outbound_serialized.cpp`, used by `CloseCancelsBlockedPublicSend`), with the
    real Engine pump reachable through `SessionConfig::transport_factory_override`, as
    `ScriptedReadTransportFactory` uses it (`tests/support/transport_double.hpp`).
    - **Driver form, under one `ioc.run()`.** Each iteration the driver calls `arm_block()`; wakes a
      long-lived test-owned sender, which `co_await`s `session.send(payload)` until the send is parked in the
      write; awaits the replica pair and the nest of k; calls `release()`; and yields twice, so the send
      completes.
    - **Why it pins e in both directions.** If the real chain holds more than the derived e, the twin at
      k = N − e − 2 reads > 0. If it holds fewer, the arm at k = N − e − 1 reads 0 and fails its
      `--expect-violation`. So the derived e is a checked input. This is the launcher-and-shifted-bracket
      construction of W-A's (b-S) pair.
    - **Conditions on the double.** Its write chain must match the plain transport's frame for frame (one
      coroutine plus its `use_awaitable` adapter; compare the double's `async_write` with
      `asio_plain_transport::async_write` by reading). Its write must not allocate: any captured write needs
      pre-sized storage, unlike `ScriptedReadTransport::async_write`'s `emplace_back`; an allocating double
      fails toward red. `release()` must run on the run thread, as it does in the driver form: a post from a
      thread with no scheduler call on its stack bypasses the recycling cache (`asio/detail/thread_info_base.hpp`,
      `thread_info_base::allocate`).
    - The replica pair's "2" stays a model input, as in the stand-in bracket.
  - W-D-W covers C-4 at the gated depth. The deeper `co_spawn`ed and `Engine::send` shapes stay derived,
    not gated (§2.4; §4).
- **FileStore `store()`** is bounded, not zero: at most one frame per offloaded I/O op, the `[const §XV.1]`
  §XV.4 exemption. FileStore `retrieve` is disclosed, not gated.

**Counters, and where each one binds (R-3′, R-4; MSVC narrowed per O-3):**

| Preset | Windows | Counter | Assertion | Fix-deleted arms run here |
|---|---|---|---|---|
| `linux-clang-release` (CI today), `linux-gcc-release` (CI widened) | W-A..W-E, W-D-R, W-D-W, the veto-then-zero cell (I2R2 C-3) | mallocnesia `_mallocnesia` twins, `MALLOCNESIA_MAX_ALLOCS=0` | **0** | (a), (b-L), (b-S), (c-L), (c-P), (c-H), (e), (s), and the W-D-R and W-D-W brackets (W-D-W's stand-in and real-chain twin), each with its in-boundary twin |
| `windows-msvc-release` | W-A, W-B. T044's rig is ported, and its `if(CMAKE_SYSTEM_NAME STREQUAL "Linux")` guard in `tests/alloc_guard/CMakeLists.txt` is lifted for the new cells | TU-local `operator new` counter (T044's) | **0** `operator new`; the `_aligned_malloc` half is disclosed | (a) only |
| unsanitized Debug presets, including the unsanitized libc++ lane | the same cells | TU-local counter: printed, not asserted. mallocnesia: not registered (Release only, below) | — | — |
| sanitizer lanes | the same cells | **semantic execution, counter unavailable** (C2-5). mallocnesia is not registered under a sanitizer (`cmake/FixppMallocnesia.cmake`, the sanitizer condition), and T044's counter is compiled out under `FIXPP_SANITIZER_REPLACES_NEW` | — | — |

- **Why MSVC is class (a) only.**
  - On MSVC, asio's `aligned_new` calls `_aligned_malloc` (`asio/detail/memory.hpp`, the `ASIO_MSVC` branch).
    `ASIO_HAS_STD_ALIGNED_ALLOC` is defined only for clang and GCC (`asio/detail/config.hpp`).
  - An `operator new` counter therefore cannot see classes (b) and (c). Arms (b) and (c) cannot read > 0 there.
  - W-C and W-D contain no class-(a) content, so on MSVC they would assert zero on paths whose B35 defects the
    counter cannot see. They are **not registered on MSVC** as B35 evidence.
  - The same holds for W-D-R and W-D-W, which are slot brackets, for the veto-then-zero cell, which is
    W-E-shaped, and for W-E, whose B35 defect is
    `send_impl`'s over-limit frame, a class-(b) request that goes to `_aligned_malloc`. W-E's class-(a)
    content is W-A's and W-B's strand, which those windows already gate there.
- **Release only (O-6).** `fixpp_add_mallocnesia_test` (`cmake/FixppMallocnesia.cmake`) has no build-type
  condition, and `FIXPP_MALLOCNESIA_SUPPORTED` is true on every non-sanitizer glibc build, Debug included.
  - The new twins, arms and in-boundary twins are registered only for a Release build type, so R-4's Debug
    lanes stay diagnostic (C2-5).
  - A non-asserting registration on other build types is not used. A registered `_mallocnesia` member that
    cannot fail is the defect class that `tools/check_mallocnesia_population.py`'s verdict check guards
    against.
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

**Fix-deleted arms and their in-boundary twins. Each arm asserts > 0 on every preset in its row of the table
above; each twin asserts 0 in the same harness.**
- Each mallocnesia arm is registered with `--expect-violation --expect-entry <hook>`. `check_alloc.py` then
  requires at least one hit through that hook, so a hit through a different hook cannot satisfy the arm.
  - A hit through the **same** hook can, including one the harness makes. Any single interception satisfies
    the arm (`alloc_guard_end` verdicts on a count; `--expect-entry` checks only that the hook appears).
  - The in-boundary twin is what excludes it (the harness rule above).
- **(a)** A configuration arm with no production seam.
  - The rig's Engine is built on `any_io_executor{asio::prefer(ioc.get_executor(), outstanding_work.tracked)}`.
    That has a different target type, so `make_session_strand` takes the fallback.
  - W-A must then read > 0, and the target-type oracle must read false.
  - On Linux the expected entry is `malloc`: `operator new` from executor erasure.
  - On MSVC the TU counter must read > 0.
  - Its twin is W-A itself: the same rig on `ioc.get_executor()`.
- **(b) Planted witnesses** (Linux only; entry `aligned_alloc`). Each witness reads the cache-size macro and the
  computed limit (`chunk_size * UCHAR_MAX`, §1(b)); it does not hardcode them. The macro's value in the test TU
  is the exported N, and §2.4's guard pins that.
  - **(b-L) The limit, as a bracket.** A planted coroutine holds a buffer across a `co_await asio::post(ex,
    asio::deferred)`.
    - Arm: the buffer alone exceeds the limit, so the frame request does too. It must allocate at least once per
      iteration, which proves that the counter sees frames in that window.
    - Twin: the same coroutine with a smaller buffer, so that its frame request is at or under the limit. It
      must read 0.
    - Frame size includes compiler overhead, so the twin cannot be sized to the byte. Its buffer is derived per
      gated preset from the arm's mallocnesia log.
      - Each interception prints `aligned_alloc(<align>, <size>)`.
      - asio requests `chunks * chunk_size + 1` bytes for a frame of `chunks` chunks
        (`thread_info_base::allocate`).
      - The derivation and its command go in the verify record.
    - If the twin's frame grows past the limit, the twin reads > 0, which is loud. Re-derive the buffer when the
      compiler or asio changes.
  - **(b-S) The slot budget.** The plant is a nest of trivial cycled awaitables.
    - Arm: N + 1 nested, where N is `ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE`, the exported value (§2.4). It must
      allocate.
    - Twin: exactly N nested. It must read 0.
    - This pair is §2.4's pin on the exported N. If the exported value changes, the pair follows it. If the
      recycler's slot count stops matching the macro, the pair fails.
  - **Where each pair is planted.**
    - **W-D's driver harness: (b-L) and (b-S).** The driver awaits the plant directly, between stores. No other
      cycled frame is live while the plant runs, so the (b-S) boundary is exactly the slot count.
    - **W-A's run-thread rig: (b-L), and (b-S) shifted by r.**
      - A long-lived *launcher* coroutine is `co_spawn`ed before arming. It waits on a strand-side signal that
        the test-owned `fromAdmin` sets, and each wake runs one plant iteration.
      - The plant iteration bumps the completion atomic, not `fromAdmin`. So frame *i+1* is written only after
        the plant has finished, and its read cannot race the plant.
      - The twin uses the identical launcher and signal, so its 0 proves that the launch path allocates nothing.
      - The pump re-arms its read before the launcher resumes, so the transport read's frame is live, and
        cycled, while the plant runs (§1(b), *What counts as cycled*). A nest of exactly N plus that frame
        exceeds the slots. q5 measured that at asio's default N: one allocation per iteration with a cycled read
        live, and 0 without it.
      - **So W-A's (b-S) pair is shifted by r**, the pending read's cycled-frame count (§2.4, recipe step 4).
        The twin is a nest of N − r, which with the read chain fills exactly N, and must read 0. The arm is a
        nest of N − r + 1, and must read > 0.
        - By reading the branch base, r is 1 after §2.3's plain-read edit. If the plan's walk finds an
          intermediate coroutine on the Active read path, r grows and the pair follows it. A hardcoded N − 1
          would then go RED by construction, the trap v0.3's departure was written about.
        - The pair pins, in the real rig, that the pending read holds exactly r slots.
        - It replaces v0.3's departure from O2-2, under which W-A carried (b-L) only (round-2 disagreements,
          superseded there).
      - **The (b-S) twin is the plain-read edit's incidental witness.** Reverting §2.3's plain-read `deferred`
        edit adds the adapter frame to the read chain, so the twin's nest of N − r plus the read exceeds N. The twin is
        then expected to go RED (inferred, by q5's arithmetic). That is a harness dependency, not a production
        gate (§2.3).
      - The (b-L) twin's zero in W-A needs only the read chain plus one in-limit plant to fit N. At the exported
        N it does, whether or not the plain-read edit is reverted.
    - Planted in W-A, (b-L) is §2.2's recurrence arm for frame size: it shows that W-A's gate sees a frame over
      the limit in that window. The slot-budget half is pinned by (b-S), in both harnesses.
- **(c) Single-edit arms at the filled depth** (Linux only; entry `aligned_alloc`). These replace v0.2's
  two-defect replica, which carried both reverted edits at once. It reddened whichever edit was reverted, so it
  discriminated neither (round-2 triage O2-1).
  - **Why the filled depth (v0.4).** v0.3 ran these arms at the modelled depth, where asio's default two slots
    made one extra frame visible. At the exported N, one extra frame at that depth stays within the slots and
    reads 0, so an arm there could not fail. The arms therefore run at the filled depth: the template's D
    chosen so that the production callee's chain fills exactly N (*One template* above). There, one extra frame
    is the (N+1)-th.
  - Each arm instantiates the shared driver-and-wrapper template, at the filled depth, with a **test-only
    replica** of the production callee in which exactly **one** B35 edit is reverted. No production seam is
    added to `MemoryStore` or `SeqnumManager`.
  - **(c-L) `store()`'s lock reverted.** A store-shaped replica: the `deferred` leading post, then the real
    public `async_mutex::async_lock` instead of the macro. It must read > 0 (the v0.4 padded probe: one
    allocation per store).
  - **(c-P) `store()`'s leading post reverted.** A store-shaped replica: `asio::post(…, use_awaitable)`, then
    the macro. It must read > 0 (the same probe: one allocation per store).
  - **(c-H) `hydrate()`'s lock reverted.** A hydrate-shaped replica on the real public `async_lock`. It must
    read > 0 (inferred by analogy with (c-L); the arm is the check).
  - The public `async_lock` is the right mutant. Re-expressed over the macro, it is one coroutine frame. At the
    filled depth, that frame is the one over N, and removing it is what the conversion does.
  - **Twins.** Each (c) arm has two twins, and both must read 0:
    - its production callee at the same filled depth, through the same template. That is W-D's or W-C's
      callee at D = N − 1, where its production window uses D = 1;
    - the replica with no edit reverted, at the filled depth, so that an allocation in the replica itself
      cannot satisfy the arm.
  - **The replica condition.** A replica awaits the same operations, in the same order, as its production
    callee, except for the one reverted edit. To check, compare the replica's `co_await`s with
    `grep -n "co_await" include/fixpp/session/memory_store.hpp src/session/seqnum_manager.cpp`, read inside
    `MemoryStore::store` and `SeqnumManager::hydrate`.
- **(e) The over-limit send, restored (R-9)** (Linux; `--expect-violation --expect-entry aligned_alloc`).
  - Through `session_test_access` (the unconditional friend in `tests/support/`), the test sets the slot flag
    before arming and leaves it set. Every send in the window then finds the slot held and takes the fallback
    leaf (§2.5), whose frame owns a 4096-B buffer across its `co_await`s. That is the over-limit send frame R-9
    removes, on a real production path, with no replica.
  - It must read > 0. Its RED comes from the leaf's own over-limit frame request, so it shows that W-E's
    counter sees an over-limit frame on the send chain inside the window. It is **not** an eviction
    witness. r2 is the evidence for eviction (§2.5).
  - Its twin is W-E itself: the same rig and launch path, with the flag left to `Session::send`'s holder.
  - The test, not an invocation, owns the flag it set. The fallback path never writes the flag, and the
    primary path's holder clears only a flag it set itself, so the arm's flag stays set for the whole window.
- **(s) Scope control** (Linux; `--expect-violation --expect-entry aligned_alloc`).
  - It is the W-A workload driven by a `run_one_for` loop instead of the run thread, which is T044's current
    shape. It must read > 0 on the fully fixed tree.
  - It pins §1(b)'s scope. If a future asio moved the cache to the thread, (s) would go green and fail its
    `--expect-violation`.
  - Its twin is W-A itself, driven by the run thread.

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
  - They are registered on Linux and on `windows-msvc-release` and `windows-msvc-debug` (C2-1). On MSVC they
    are the fixpp executor's behaviour oracle (§2.1).
- **New overlap cells for the send slot (R-9; §2.5).** No allocation gate can see a broken flag: two sends
  sharing the slot corrupt bytes silently. So each cell asserts that every send stores, and transmits, exactly
  the frame it built. It compares the MemoryStore's stored bytes and the peer's received bytes with a frame
  built independently from the same payload, seq and mock-clock time.
  - **Interleaved.** A test coroutine on the session strand `co_spawn`s two `Session::send`s with distinct
    payloads, back to back. The second starts while the first is suspended in its store's leading post, which
    always suspends (§2.5).
  - **Nested.** Inside the test `toApp` for the first send, the cell snapshots the bytes of the frame that
    `toApp`'s `MessageView` spans. It then starts the second send with a `co_spawn` onto the session strand,
    which runs inline until its store's post suspends it (r3). After that `co_spawn` returns, the cell compares
    the frame's bytes with the snapshot.
    - This checks the slot where the flag protects it, during `toApp`. It reads neither the store nor the
      wire, so it does not depend on the pre-existing MsgSeqNum defect (§2.5). That defect makes both sends
      store under one seq, so a stored-bytes or wire check in this variant would test the defect, not the
      flag.
    - The stored-bytes and wire checks belong to the interleaved variant only. There the second send peeks
      after the first has assigned, so the defect does not arise.
  - **Mutants, each of which must turn a cell RED:** the flag test deleted, so both sends use the slot (both
    variants); and the holder released before `store_then_emit` returns (the interleaved variant).
  - They are registered wherever the session suites run, including the ASan lane with default options. Each runs against the converted sites, not against the public
  `async_lock` alone. Each names the mutant it must be RED against.
  - **Pre-initiation cancellation (O-2).** A terminal cancellation emitted on the session's cancel slot while
    `MemoryStore::store` is suspended in its leading post makes `store_then_emit` return `dispatch_aborted`,
    with nothing transmitted.
    - Mutant: the macro with part 1 deleted.
    - This cell runs with the default `throw_if_cancelled() == true`, so it witnesses only the `cancelled()`
      conjunct of part 1.
  - **`throw_if_cancelled(false)` (C2-4, window 1).** A standalone test coroutine runs
    `co_await this_coro::throw_if_cancelled(false)` and then calls `MemoryStore::store`. A terminal cancellation
    lands while `store()` is suspended in its leading post.
    - `store()` must complete as today's `async_lock` does at that point: no exception, and the record stored.
    - Mutant: part 1 with its `throw_if_cancelled()` read dropped.
    - It is standalone because no production caller sets `false`: `git grep -n "throw_if_cancelled(false" --
      src include` finds comments only. `MemoryStore::store` is public API, though, and the repo's own tests
      set it (`git grep -n "throw_if_cancelled(false" -- tests`).
  - **Seam #17, LateSignal/GrantOrCancel.** A total cancellation that arrives after the grant does not abort
    the caller's next `co_await`. Mutant: part 4 deleted.
  - **E-5's drain.** A lock op that races `draining_` completes with `sync_lock_drained`, and the record's
    references balance. This covers part 5 and the moved initiation. Mutant: the moved initiation's draining
    check deleted.
  - **Non-terminal entry filter.** A caller that entered with a wider filter leaves the macro with terminal,
    the same as today's `async_lock`. Mutant: part 4 restoring the entry filter instead of terminal.

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
  - **Mixed traffic (R-6 residual), restated as a condition under R-1′.** asio evicts a cached block of a
    purpose whenever a request of that purpose fits no cached block. Over-limit frames are such requests
    (§1(b)).
    - **The condition.** A gated-path message allocates when, at that moment, the cache holds fewer blocks
      that fit its live set than that live set needs. That happens after ungated requests (Logon, Reject,
      resend, or any frame over the limit) have evicted blocks since the cache last held enough.
    - The gated message then re-primes the cache. So this is bounded, not zero.
    - It also applies whenever the live set on the scheduler call exceeds N (§2.4's condition). N is sized
      so that no gated instant reaches that.
    - v0.3 listed two cases here, and R-1′ moves both:
      - **Outbound work during a pending read** is now gated by W-D-R (§3), and leaves this row.
      - **The full Logon chain** is within N when its frames fit the limit, but it is **not gated**. It stays
        here as "no zero claimed", not as "allocates".
    - **Application sends (R-9).** Before B35, every `Session::send` was an over-limit frame request, so a
      two-way session evicted a cached block on every send (instance-2 triage O3-1). After B35, the
      condition is narrower:
      - **A send that overlaps another send on the same session.** A send that starts while another send on
        that session is between its frame build and the end of its `store_then_emit` takes the fallback leaf
        (§2.5). Its frame is over the limit, so it allocates and evicts one cached block. That covers
        concurrent `Engine::send` or `fixpp_session_send` callers, a consumer that `co_spawn`s
        `Session::send` more than once without awaiting each, and a send started from inside `toApp`.
        Bounded, not zero.
      - **Sends through `Engine::send` or `fixpp_session_send`.** These entry points allocate on every call:
        the payload copy, the `co_spawn` hops, and the C ABI's `use_future` promise (§2.5). No zero is
        claimed for them. Their chains also hold more cycled frames than W-E's. When such a chain plus the
        inbound pair exceeds N, the inbound pair allocates too. That is derived, not gated: count it with
        §2.4's recipe, step 5.
      - **A send started from inside `fromApp` or `fromAdmin`.** It runs inline, stacked on the inbound arm,
        until its first suspension (§2.5, probe r3). Its frames count on top of the arm's. When that stack
        exceeds N, the inbound pair allocates. Derived, not gated: count it with §2.4's recipe, step 5.
      - Sequential sends through `Session::send`, started beside the inbound arm, are gated by W-E. The
        two-way sizing instant is gated by W-D-W (§3).
  - Debug and sanitizer presets are not gated.
  - libc++ has no Release preset.
  - `windows-msvc-release` checks `operator new` only. Its `_aligned_malloc` half is unchecked, and W-C and W-D
    are not gated there.
  - FileStore `retrieve`.
  - The outbound path beyond `Session::send` (W-E, §3): `Engine::send`'s and the C ABI's own allocations, and
    the unconverted public locks of `assign_outbound` and `write_gate_`, which are cycled frames counted in
    N rather than removed (§2.3).
  - TLS, pending R-5's measurement (§3, RED task 0).
- **L-006-2.** Rewrite "one-time per-thread … warm-up" to the scheduler-call scope. The warm-up is once per
  scheduler call, not once per thread. Its status is unchanged.
- **`SessionEntry::session_strand` type change.** A C++ source and layout change to a public, nameable struct
  (member type, `sizeof`, aggregate initialisation). It is not a C-ABI change.
- **`FIXPP_DETAIL_CO_AWAIT_LOCK`** (final name plan-level) becomes visible to every TU that includes
  `async_mutex.hpp` or `memory_store.hpp`. So do the names its expansion uses: `fixpp::sync::detail::lock_frame`,
  `detail::async_lock_op` and `detail::finish_lock` (C2-2). None of them is supported API.
- **New row: the asio cache-size ODR obligation (R-1′; §2.4).** It is a usage obligation, not a residual of
  the zero.
  - Every TU in a process that includes asio must be compiled with fixpp's
    `ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE` value. A mismatch is undefined behaviour in asio's recycler, and is
    not diagnosed at link time.
  - CMake consumers that link any `fixpp::` target except `fixpp::capi` and `fixpp::service` inherit the
    definition, through `fixpp::core` (§2.4).
  - Non-CMake consumers must define it themselves.
  - A Conan consumer has no fixpp package to inherit it from today (R-8). A future fixpp recipe carries it in
    `cpp_info.defines`.
  - So must a consumer's own asio TUs that include no fixpp header, including a C-ABI consumer that links
    `libfixpp_capi.a` into a process with its own asio code. The definition does not propagate through
    `fixpp::capi`.
  - A TU that includes a fixpp header is checked at compile time by the guard header's `static_assert`. A TU
    that includes only asio is not checked.
  - The row names the guard header as the place the value is defined, and gives no number.
- **The PUBLIC compile definition** on `fixpp_core`, which every C++ exported target that publishes the
  `include/` root inherits, and the new installed guard header, are C++ package changes (§2.4). They are not
  C-ABI changes.
- **`Session`'s layout change (R-9).** `Session` gains the session-owned send scratch storage and the slot
  flag (§2.5). It is a public class in an installed header, so this is a C++ layout change (`sizeof`). It is
  not a C-ABI change: the C ABI exposes `Session` only through an opaque handle.
- **Pre-existing, not B35's: a send nested inside `toApp` can reuse the outer send's MsgSeqNum** (§2.5,
  inferred from the code and probe r3). It goes to a new labelled issue, filed by the orchestrator, which the
  B&L row then names. `engine.hpp`'s "enqueued behind the current dispatch" sentence is part of that issue.

**Documentation and comment edits the PR must make (C-11, RC-1).**

| File | Edit |
|---|---|
| `spec/behaviors-and-limitations.md` | L-497-1 rewritten as above; L-006-2's scope wording; the new ODR-obligation row; new rows if the B&L workflow prefers one row per R-6 residual |
| `spec/behaviors-and-limitations-closed.md` | the resolved part of L-497-1 |
| `spec/feature-catalogue.md` | S-012's evidence says the global-heap half is unchecked. Re-point it to the W-D and W-D-R mallocnesia gates on the two Linux Release presets |
| `cmake/fixppConfig.cmake.in` | a comment next to the dependency block: the package carries the asio cache-size definition through `fixpp::core`, and a TU that includes asio outside the `fixpp::` targets must match it (§2.4) |
| `src/session/session.cpp` | `send_impl`'s header comment ("Stack buffer: 4096 bytes … no heap allocation on this path") is replaced by a pointer to this note's §2.5. It names the slot, the flag and the fallback, and states no size or count |
| `tests/consumer/CMakeLists.txt`, `tests/consumer/run_consumer_witness.cmake` | the two new C++-consumer legs, `probe_umbrella` and `probe_core` (§2.4). Leg 3's closed empty expectation stays as it is |
| `.specify/2f-async-mutex.md` | Erratum E-6 (§2.3), including E-4's scope correction |
| `.specify/2d-threading.md` | annotate the executor-model paragraph that says the engine never picks a concrete executor. The engine still uses the caller's executor, but it keeps its concrete type when that type is `io_context`'s, as `strand<session_inner_executor_t>` (§2.1) |
| `brain/components/async-mutex.md` | E-6 pointer; the macro is the internal route |
| `brain/components/inbound-message-path.md` | the dispatcher/arm split; the two lifetime classes (helper vs reply leaf) and why the v0.4 hoist was rejected; W-A/W-B as the gates of its zero-allocation invariant |
| `brain/components/session.md`, `brain/components/transport.md` | the concrete `io_context` strand target and the fallback; in `session.md`, `send_impl`'s session-owned scratch, the slot flag and the fallback leaf, and why the existing serialisation could not carry R-9 (§2.5) |
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
- a message on an ungated path can evict a cached block, and the next gated message re-primes it (§4, mixed
  traffic, as a condition);
- an outbound emit from the liveness loop that runs while a re-armed inbound read is pending shares the slots
  with that read's frame.
  - v0.3 recorded that its store chain then allocated in the steady state of an Active session that receives
    traffic. That was true at asio's default two slots (q5).
  - Under R-1′ the exported N covers that live set (§2.4), and W-D-R gates it (§3).
- an application send used to be an over-limit frame request on every call, and so evicted a cached block on
  every send of a two-way session (instance-2 triage O3-1). That was not an occasional eviction, and N could
  not reach it.
  - Under R-9, `send_impl`'s frame holds no array (§2.5), and W-E gates sequential sends through
    `Session::send` (§3).
  - What remains is a send that overlaps another send on the same session, plus the entry points that
    allocate on every call (`Engine::send`, `fixpp_session_send`). Both are conditions in §4's mixed-traffic
    row.

**R-1′'s cache size is a build precondition, not a third condition of the zero.**
- The exported N holds only if every TU in the process sees it (§2.4). That is an ODR obligation on the build,
  stated in its own B&L row (§4).
- It is not added to R-6's two conditions. R-6 is the owner's ruling, and this note does not widen it.
- Whether the obligation belongs among the zero's conditions is for the owner to decide.

**Precedent.** `.specify/2f-async-mutex.md` Erratum E-4 (user-authorized) sanctioned asio's cancellation
recycler as an allocator whose guarantee is met in steady state, with the first touch "amortized and … not a
hot-path event". B&L L-006-2 discloses that as wontfix. E-4's "per-thread" premise is corrected to the
scheduler-call scope by E-6 (§2.3). The precedent's substance — a steady-state guarantee over asio's recycler,
with the priming disclosed — is what R-6 applies here.

**Residual configurations.** These are tracked as **open B&L limitations**, with **no constitution amendment**
(§4):
- the R-2 fallback executors (condition 1 fails);
- consumer-driven bounded `run_for` / `run_one_for` / `poll` loops (condition 2 fails);
- mixed-traffic eviction, including the overlapping-send fallback and the sends through `Engine::send` and the
  C ABI (§4).

**Scope of the clauses.**
- `[const §VIII.5]`'s text spans parse → `fromApp`, which W-A and W-B gate.
- W-C's zero comes from feature 025's W8 requirement (`specs/025-refresh-on-logon/data-model.md`, row W8),
  which anchors the re-hydrate apply step (`SeqnumManager::hydrate()`) on `[const §VIII.5]`.
- W-D's and W-D-R's `MemoryStore::store` is reached through the outbound `store_then_emit`. Its clause is
  `[const §XV.1]`'s MemoryStore sentence, which has no deviation provision of its own. R-7 (§0) settles the
  clause question for W-D: R-6's justification extends to that sentence for B35's conditional zero.
- W-E's `Session::send` chain reaches the same `MemoryStore::store`, so R-7 covers it in the same way. W-D-W's
  stand-in bracket is a slot model of the same chain, and its real-chain twin runs that chain.
- **R-9's storage against `[const §XV.1]`.** The send scratch is allocated with the `Session` object, once per
  session and before `open()` (§2.5). That is pre-allocation per session, not a per-message allocation, so it
  is the form §XV.1 permits. The fallback leaf's frame is a per-message allocation on the overlapping send
  only. It is a residual under R-6's mixed-traffic condition, and is disclosed as such (§4).

**What this section does not do.** It does not amend the constitution. Article XX §1–§2 make any change to the
constitution's text an owner amendment, and R-6 rules that none is needed.

## 6. Risks Gate A should press on

1. **The macro's equivalence with today's `async_lock` (§2.3).**
   - Part 1 must match today's `await_transform` check exactly, in both its condition and its exception type.
     Each of its two conjuncts has a cell: the O-2 cell for `cancelled()`, and the `throw_if_cancelled(false)`
     cell for `throw_if_cancelled()` (§3).
   - Seam #17, E-5's drain and the unconditional restore each have a cell (§3).
   - If a future asio changes `await_transform`'s check, the O-2 cell is the detector.
2. **Re-erasure.** Any accept, connect, reconnect, timer or SSL construction site that builds a fresh
   `strand<any_io_executor>` around the session silently re-introduces class (a). Arm (a) covers the Engine path
   only.
   - Recipe: `git grep -n "make_strand\|strand<" -- src include`. Classify every production hit as on or off
     W-A/W-B's handler path.
   - Hits known at v0.3:
     - the two §2.1 sites, which are converted;
     - `control_strand_`, which is off the read path;
     - `src/core/system_clock_source.cpp`'s per-timer strands over the engine executor. These are timer
       executors for `sleep_until`, not the session strand, and do not run on an inbound read. The rigs run on a
       mock clock, so no timer fires inside the windows. B35 does not change them.
   - Re-run the recipe at implementation time; the list above is a lead.
3. **Frame-size drift.** A new local or `co_await` temporary on the Active path can push a frame over the limit
   on one compiler only. The `lock_frame` that §2.3 adds to each converted caller's frame is such a growth, and
   `check_inbound`'s and `store`'s frames must still fit. So must `send_impl`'s after §2.5, and each class-2
   reply leaf's (§2.2). The Release-preset gates are the only guard: W-A for the arms, W-E for `send_impl`.
   No size is pinned.
4. **Slot budget.** A new nested `co_await` on the Active path, on the W-B dispatch path, or on any chain that
   shares the scheduler call with them can push the live set past N (§2.4's condition). W-B's dispatch is
   synchronous `fromApp` (§3), so it adds no frame today. W-B's RED-task-0 baseline shows whether the path is
   already deeper. The (b-S) pair pins N itself. A change to a chain's depth is caught only when a gated
   window's live set reaches N + 1, so §2.4's recipe is re-run whenever a gated or slot-sharing chain gains a
   `co_await`. The `Session::send` chain is now such a chain: its count e sizes W-D-W, W-D-W's
   headroom `static_assert` reads that e, and W-D-W's real-chain twin goes RED when the derived e and the
   production chain disagree (§3).
5. **Other recyclers on the path.** The cancellation-slot emplacement in `reset_cancellation_state` and
   `inherited_slot.assign` uses `cancellation_signal_tag`; executor ops use `executor_function`. Both have
   `ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE` slots, the exported N, under the same policy and the same
   scheduler-call scope. §2.4's condition is per purpose. The full counter decides.
6. **MSVC layout** (§2.1 RED task 1). Round-2 triage q1 predicts that MSVC takes the fixpp executor, so on
   MSVC its correctness rests on the fast-path semantic cells registered there (§3). The non-MSVC `is_same` pin
   keeps Linux on `io_context::executor_type`.
7. **Harness additions** (§3). Inside a window that asserts 0 they fail toward red: a window that reads > 0 is
   first checked for harness allocations, before the production path is blamed. Inside an arm they fail toward
   green, and the arm's in-boundary twin is the guard.
8. **Window depth drift** (§3). A window shallower than its modelled caller chain cannot see the edits it gates
   (round-2 triage O2-1). Under R-1′ the edits are witnessed at the filled depth, not at the modelled depth.
   The shared driver-and-wrapper template pins both: one wrapper fewer at the filled depth, and the (c) arms
   read 0 and fail.
9. **The ODR obligation and its guards (§2.4).**
   - A TU that includes asio but not the guard header escapes the compile-time check. In-tree, the
     compile-command census covers it. In a consumer, nothing covers it, and the B&L row says so.
   - The guard header includes `<asio/detail/thread_info_base.hpp>`, an asio detail header. An asio bump that
     moves or renames the macro makes the guard fail to compile, which fails toward red.
   - The `[const §XV.9]` corpus gates preprocess headers without the definition. The guard is a
     `static_assert` so that they keep passing. A guard rewritten as `#error` would break them, loudly.
   - Fable's consult and this note read the consumer witness's leg 3 differently (§2.4). The witness run
     decides which reading is right.
   - A new export member that publishes the `include/` root without linking `fixpp_core` would escape the
     attachment. §2.4's enumeration recipe is the check, and `probe_core` pins the attachment itself.
10. **The send slot's exclusivity (§2.5).** The slot is correct only while every path that builds into the
    slot tests the flag (in `Session::send`, §2.5) before the first frame byte is written, and only while the
    holder lives until `store_then_emit` returns.
    - A broken flag corrupts bytes silently. No allocation gate sees it. The overlap cells, with their two
      mutants, are the only guard (§3).
    - A new synchronous user callback inside `send_impl` before the build would let a nested send reach the
      build scratch while the outer build is live (r3). The build helper's "calls no user code" condition is
      what keeps the build scratch flag-free. Re-check it whenever the helper changes.

## 7. What this does not decide

- The outbound send path beyond R-9: `Engine::send`'s and the C ABI's own allocations, and converting the
  outbound locks (`assign_outbound`, `write_gate_`), which stay as cycled frames counted in N (§2.3, §2.5).
- The pre-existing MsgSeqNum reuse by a send nested inside `toApp` (§2.5). It goes to its own issue.
- FileStore's lock call sites.
- A generic executor (rejected by R-2).
- A fixpp Conan recipe. Today there is none to carry R-1′'s `package_info` half (R-8; §0, implemented forms).
- A libc++ Release preset.
- Sanitizer and MSVC-debug allocation counters.
- TLS allocation beyond R-5's measurement.
- A constitution amendment (R-6 rules none).

---

## Gate A

- Round 1 applied 2026-10-09: Codex P1=3 P2=8 P3=1; Opus post-judging P1=2 P2=6 P3=12; rewrite addresses root causes RC-1, RC-2, RC-3 (+R-6). Reviews: research/reviews/codex_544_1_hot-path-zero-alloc_review.md, research/reviews/opus_544_1_hot-path-zero-alloc_triage.md.
- Round 2 applied 2026-10-09: Codex P1=1 P2=3 P3=2; Opus post-judging P1=1 P2=1 P3=6; rewrite addresses root causes 1, 2, 3, 4 of the round-2 triage (recorded below as RC-A, RC-B, RC-C, RC-D). Reviews: research/reviews/codex_544_2_hot-path-zero-alloc_review.md, research/reviews/opus_544_2_hot-path-zero-alloc_triage.md.
- Instance 1 closed 2026-10-09 after round 2 (rewrites 2/2): owner revised R-1 → R-1′ on probe q5; v0.4 written; Gate A instance 2 round 1 pending.
- Instance 2 round 1 applied 2026-10-09: Codex P1=3 P2=2 P3=1; Opus post-judging P1=2 P2=3 P3=3; rewrite addresses I2-RC1, I2-RC2, I2-RC3, I2-RC4 and R-8/R-9. Reviews: research/reviews/codex_544_i2-1_hot-path-zero-alloc_review.md, research/reviews/opus_544_i2-1_hot-path-zero-alloc_triage.md.
- Instance 2 round 2 2026-10-09: Codex P1=0 P2=3 P3=1; Opus post-judging P1=0 P2=0 P3=5 → CONVERGED. P3s C-1..C-4, O4-1 applied as v0.5.1. Reviews: research/reviews/codex_544_i2-2_hot-path-zero-alloc_review.md, research/reviews/opus_544_i2-2_hot-path-zero-alloc_triage.md.

### Instance 2 round 2 — disposition of every finding (v0.5.1)

The IDs are the instance-2 round-2 triage's, prefixed I2R2 so they cannot be confused with instance-2
round 1's C-1..C-6 below. No further review round: the triage's closing recommendation converges on severity
and prescribes these five as the amendment.

| Finding | Judged severity | Where addressed |
|---|---|---|
| I2R2 C-1 (N=16's witness is circular: W-D-W's stand-in takes e as an input) | P3 (Codex P2) | §3 W-D-W: the "a real rig cannot hold that write pending" sentences deleted; the real-chain twin over a held write added, the stand-in kept as the recycler-boundary pin; §2.4's re-check sentence says which cell checks what; §5; §6 risk 4; the instance-2 round-1 C-4 disagreement marked superseded |
| I2R2 C-2 (the send-tail example contradicts its own frame condition) | P3 (Codex P2) | §2.5: "for instance one tail coroutine" struck; the shape named (`Session::send` hosts the flag test and holder, `send_impl` is the tail); the holder's frame re-worded; §3 arm (e); §6 risk 10 |
| I2R2 C-3 (no cell observes the flag after a non-success exit) | P3 (Codex P2) | §2.5 RAII release with probe s1's recipe; §3 the veto-then-zero cell and its mutant; the Linux counters row; the MSVC exclusion |
| I2R2 C-4 (`closure.py`'s path is not runnable from the library root) | P3 | §2.4 recipe step 4: the runnable relative path, the parent-repo caveat, and the guard-header census ctest as the in-repo check |
| I2R2 O4-1 (§2.2's by-value rule would forbid a safe `bool&` under a dispatcher shape) | P3 | §2.5: no non-coroutine dispatcher is introduced on the send path, so `send_impl`'s parameters fall under §2.2's directly-`co_await`ed clause; §2.2's rule is unchanged |

### v0.5 — what instance 2 round 1 and R-8/R-9 changed (for instance 2's round 2)

- Header: the probe list gains `b35_544_gate_a_i2r1/` (r1, r2, `closure.py`, and the rewrite's r3). The
  Trigger gains `session.hpp`, the O3-2 sentence on `[const §X.7]`'s "ABI-compatible" clause, and
  `Session`'s layout change.
- §0: rulings R-8 and R-9. The Conan implemented form is restated as R-8's narrowed form, and its false
  "does not narrow the ruling" sentence is removed (C-2). The exported-targets form moves to `fixpp_core`
  (C-1). A new form records that R-9's "existing write-path serialisation" does not exist, and what is
  implemented instead.
- §2.2: storage is classified by data-flow lifetime. Class 1 goes to non-coroutine helpers, class 2 to leaf
  coroutines that own their buffers. The gcc-13 disjoint-scope fact, the parameter rule's scope and the two
  gates (W-A on `linux-gcc-release`, ASan with default options) are stated (C-3).
- §2.3: the outbound locks are on a gated path now, unconverted and counted in N.
- §2.4: the condition gains the `Session::send` chain (recipe step 5, e) and an explicit headroom condition.
  **N rises from 8 to 16**, because at 8 the two-way instant leaves no headroom. The definition attaches to
  `fixpp_core`, selected by "publishes the `include/` root", with the enumeration recipe. A `probe_core`
  consumer leg is added (C-1).
- §2.5 (new): R-9. The lifetimes of `send_impl`'s arrays; the serialisation finding, with probe r3; session
  scratch, the build helper, the slot flag and the fallback leaf; the send gate, rejected by
  R-10; the per-call allocations of `Engine::send` and the C ABI; and a pre-existing MsgSeqNum defect, for
  an issue.
- §3: W-E and arm (e) (R-9); W-D-W with its shifted bracket (C-4); the TLS diagnostic in RED task 0 (C-5); the
  overlap cells and their mutants; the MSVC and Linux table rows.
- §4: the mixed-traffic row gains the application-send conditions; the ODR row's inherit clause; R-8 for
  Conan consumers; `Session`'s layout; the pre-existing defect.
- §5 is updated for O3-1 and R-9, including `[const §XV.1]` for the send scratch. §6 updates risks 3, 4 and
  9, and adds risk 10. §7 drops the decided cache-size bullet (C-6) and narrows the outbound bullet.

### Instance 2 round 1 — root causes

The instance-2 triage numbers its root causes 1–4. Here they are I2-RC1..I2-RC4, so they cannot be confused
with instance 1's RC-1..RC-3 and RC-A..RC-D.
- **I2-RC1 (triage root cause 1). A storage location was prescribed without a lifetime split** (C-3).
  - §2.2's two classes, decided by data flow.
  - §2.5 applies the same split to `send_impl`.
- **I2-RC2 (triage root cause 2). Target coverage was decided from link edges, not from the shared include
  root** (C-1).
  - §2.4's `fixpp_core` attachment, with its selector and enumeration recipe.
  - The `probe_core` leg, and the B&L inherit clause.
- **I2-RC3 (triage root cause 3). Slot sizing was reasoned in frame counts, one direction at a time** (C-4,
  O3-1).
  - R-9 and §2.5.
  - §2.4's step 5 and headroom condition, and N = 16.
  - W-E, W-D-W and arm (e).
  - The mixed-traffic row's send conditions.
- **I2-RC4 (triage root cause 4). The ruling's text was checked against the artifacts, but the classification
  was not** (C-2).
  - R-8, and §0's narrowed Conan form with its `test_package` condition.

### Instance 2 round 1 — disposition of every finding

| Finding | Judged severity | Where addressed |
|---|---|---|
| C-1 (`fixpp::core` gets no definition) | P2 (Codex P1) | §0 exported-targets form; §2.4 `fixpp_core` attachment, selector, enumeration recipe, `probe_core` leg, inherit clause; §4 ODR row; §6 risk 9 |
| C-2 (R-1′'s Conan half) | P1 + owner item → R-8 | §0 R-8 and the narrowed Conan form; the consumer-witness sentence corrected (a `test_package` leg); §4 ODR row; §7 |
| C-3 (the hoist is a use-after-return) | P1 | §2.2 lifetime classes, leaf form, gcc-13 fact, parameter scope, gates; §2.5 for `send_impl`; §6 risk 3 |
| C-4 (N's two-way sizing case has no witness) | P2 | §2.4 recipe step 5, headroom condition, N = 16; §3 W-D-W with its shifted bracket and headroom `static_assert`; W-E does not cover it, and §3 says so |
| C-5 (R-5's TLS measurement missing) | P3 (Codex P2) | §3 RED task 0, the TLS diagnostic with its disposition; §4 |
| C-6 (decided item under "does not decide") | P3 | §7 bullet deleted |
| O3-1 (every send evicts a cached block) | P2 + owner item → R-9 | §0 R-9 and its implemented form; §2.4; §2.5; §3 W-E, arm (e), overlap cells; §4 mixed traffic; §5 |
| O3-2 (the "not BREAKING" verdict argued symbols and layout only) | P3 | Header Trigger: the "ABI-compatible" clause, answered in one paragraph |

### Instance 2 round 1 — disagreements

**Rejected by the triage, so not applied:**
- **C-1, "the same problem reaches `fixpp_log`, whose header closure can expose those core headers":
  refuted.** A transitive include walk over `include/` (`b35_544_gate_a_i2r1/closure.py`) finds asio
  reachable only from `fixpp/core`, `fixpp/otel` and `fixpp/config` headers. It finds none from `fixpp/log`,
  `fixpp/wire`, `fixpp/dictionary`, `fixpp/tap`, `fixpp/service` or `include/fix`. So Codex's proposed
  `fixpp::log` consumer-witness leg is not added. `fixpp::log` inherits the definition through `fixpp_core`
  in any case (§2.4).
- **C-1 at P1: judged P2.** The failure is loud: a `fixpp::core`-only consumer that includes an
  asio-bearing header fails the guard's `static_assert`. The fix is applied all the same.
- **C-5 at P2: judged P3.** R-5 is an orchestrator default, and the measurement is diagnostic, with no gate
  resting on it. The fix is applied all the same.

**Counter-proposals not adopted, with the finding addressed by the triage's fix instead:**
- **C-2, "expand B35 to create and gate an actual Conan package".** The owner chose the narrowed form instead
  (R-8).
- **C-3, "alternatively make the callee take ownership of a fixed-capacity buffer".** Not adopted for the
  inbound arms. The triage's leaf form needs no change to `store_then_emit`'s contract. Codex's first
  alternative, a per-reply leaf that owns the buffer, is the form adopted (§2.2).
- **C-4, "a production-shaped window that holds `live_write_serialized_` → `Transport::async_write`
  pending".** A real pending write needs a peer that stops reading until the socket buffers fill, which is
  neither deterministic nor cheap. The triage's model window is adopted as W-D-W, with e taken from the
  production chain (§3).
  - **Superseded by I2R2 C-1 (v0.5.1).** This entry's premise came from the instance-2 round-1 triage and was
    not checked against the test tree. W-D-W now has a real-chain twin over a held write (§3).

**Departures from the triage's prescribed fixes, each with its reason:**
- **O3-1 option (b), "W-E: W-A's rig with one app send per inbound message, asserting 0 on the inbound
  pair".** W-E uses W-B's rig and a long-lived sender that calls `Session::send` directly, and it asserts 0
  for the whole window. mallocnesia counts process-wide, so it cannot attribute a count to the inbound pair
  alone. `Engine::send` cannot be the sender, because it allocates on every call (§2.5).
- **O3-1 option (b), "by C-3's leaf form or caller-owned storage".** The leaf form cannot hold a 4096-B buffer
  under the limit. R-9 ruled for session-owned storage, and §2.5 implements it.
- **The triage's rejected alternative, a `Session`-member scratch buffer, stays rejected for the inbound
  arms** (§2.2). R-9's send slot is the case where that alternative is the only one left. §2.5 supplies the
  per-chain exclusivity argument the triage said it would need: the slot flag, with a fallback leaf.
- **R-9's "the existing write-path serialisation must guarantee" premise does not hold** (§2.5, probe r3).
  The implemented form is the slot flag with a fallback leaf. Its cost is a disclosed condition: an
  overlapping send allocates. The session send gate that would also keep that send at zero was rejected by
  R-10, for the four conditions it would have to meet (§2.5).
- **N = 16, not 8.** This is not a triage item. R-9 adds the `Session::send` chain to §2.4's gated live set,
  and by reading, the two-way instant then leaves no headroom at 8. The value follows the condition, and
  W-D-W's bracket and headroom `static_assert` check it (§2.4, §3).

### v0.4 — what R-1′ changed (for instance 2's round 1)

- §0: R-1 is shown as revised; R-1′ is a new row with q5's recipe; two implemented forms are added (no Conan
  recipe exists to carry `package_info`; `fixpp::capi` is not an asio-carrying target).
- §1(b): the slot count is the exported N, for every purpose; eviction is first-occupied-slot.
- §2.2: the "at most two live" target is replaced by §2.4's condition. The split is kept for the frame-size
  limit.
- §2.3: the frame-removing edits are headroom under N, and are witnessed at the filled depth.
- §2.4 (new): N = 8 and how it is derived; where the definition is attached; the ODR obligation and its
  failure mode; the guard header's `static_assert`, the header-inclusion check and the compile-command census;
  the C-ABI, python and other-dependency findings; and the consumer witness's new leg, with leg 3 kept.
- §3: the template takes a wrapper depth D; the (c) arms move to the filled depth; W-D-R is new, with its
  bracket; W-A gains a shifted (b-S) pair, which supersedes the O2-2 departure; K's recipe is built at N.
- §4: mixed traffic restated as a condition; the q5 case leaves it for W-D-R; a new ODR-obligation row.
- §5: the q5 bullet is updated; the cache size is recorded as a build precondition, not a third zero
  condition.
- §6: risks 4, 5 and 8 are updated, and risk 9 is new. §7: the cache-size entry is now decided by R-1′.

### Round 1 — root causes

- **RC-1. The recycler scope is the scheduler call, not the thread.**
  - §1(b) restated.
  - §3's window rule: the driver-coroutine and run-thread shapes, a fixed K, and the (s) scope control.
  - §4's driver-condition row, and the "per-thread" corrections and their recipe.
- **RC-2. The caller-side lock sequence is one macro, the only definition, in five ordered parts.**
  - §2.3: the pre-check, the handler-sourced executor and slot, and the unconditional restore.
  - §3's lock cells, and arm (c)'s test-only replica (superseded in v0.3 by the single-edit arms (c-L), (c-P) and
    (c-H); RC-A).
- **RC-3. The constitutional disposition.**
  - R-6 in §0, and the justification in §5.
  - The residuals as B&L conditions in §4.

### Round 1 — disposition of every finding

| Finding | Judged severity | Where addressed |
|---|---|---|
| C-1 (constitutional status) | P2 + owner item | R-6 (§0), §5 |
| C-2 (in-window `use_future` scaffolding) | P2 | §3 window rule (driver coroutine; existing shapes not reused verbatim) |
| C-3 (arm (c) cannot turn RED) | P2 | §3 arm (c): test-only two-frame replica. Superseded in v0.3 (O2-1): it discriminated neither edit |
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

### Round 2 — finding IDs

Codex's round-2 review reuses the IDs C-1..C-6, which round 1 also used. In this note, round 2's Codex
findings are **C2-1..C2-6**, in the same order, and an unprefixed C-*n* is round 1's. O2-1..O2-3 are the
round-2 triage's own findings, under their own IDs.

### Round 2 — root causes

The round-2 triage numbers its root causes 1–4. Here they are RC-A..RC-D, so that they cannot be confused with
round 1's RC-1..RC-3 or with the rulings R-1..R-7.
- **RC-A (triage root cause 1). Window depth was picked for measurability, not derived from production's caller
  chain** (O2-1).
  - §1(b): what counts as a cycled frame.
  - §3: the driver-and-wrapper template, the modelled chains and their recipe, and the single-edit arms
    (c-L), (c-P) and (c-H) with their twins.
  - §2.3: the plain-read `deferred` edit, which is headroom only.
  - §4 and §5: the full Logon chain and outbound work during a pending read, under mixed traffic.
  - §6 risk 8.
- **RC-B (triage root cause 2). Arms were specified one-sided** (O2-2, C2-4 window 1).
  - §3: the harness rule in both directions, an in-boundary twin for every arm, (b-L) as a bracket, and (b-S)
    at exactly the slot count.
  - §3: the `throw_if_cancelled(false)` cell, so each conjunct of part 1 has a cell.
- **RC-C (triage root cause 3). Alternatives were written as prose rather than as named entities** (C2-1, C2-2).
  - §0's fourth implemented form, and §2.1's `session_inner_executor_t` with its pins.
  - §2.3: the initiation as a private member with a friend forwarder.
  - §4: the `detail` names disclosed with the macro.
- **RC-D (triage root cause 4). Minor wording** (C2-5, C2-6, O2-3).
  - The §3 table rows, the header's probe paths, and K's recipe.

### Round 2 — disposition of every finding

| Finding | Judged severity | Where addressed |
|---|---|---|
| C2-1 (the MSVC fallback contradicts its pins and oracle) | P3 (Codex P1) | §0 fourth implemented form; §2.1 `session_inner_executor_t`, fit `static_assert`s and oracle against `strand<session_inner_executor_t>`, the non-MSVC `is_same` pin; §3 fast-path cells on the MSVC lanes; §6 risk 6 |
| C2-2 (the moved initiation loses private access) | P3 (Codex P2) | §2.3: a private member plus a friend forwarder, and a condition and diff recipe in place of "exactly two non-verbatim lines"; §2.3 hygiene and §4: the `detail` names |
| C2-3 (first-fit thrash at depth two) | rejected (refuted) | Round 2 — disagreements |
| C2-4 window 1 (`throw_if_cancelled(false)`) | P3 | §3 lock cell; §2.3 equivalence; §6 risk 1 |
| C2-4 window 2 (a `total` between parts 2 and 3) | rejected | Round 2 — disagreements; §2.3 equivalence |
| C2-5 (the diagnostic row promises counts) | P3 | §3 table: the sanitizer row reads "semantic execution, counter unavailable"; Release-only registration |
| C2-6 (probe path invalid from the library root) | P3 | header: parent-relative paths and the `../research/probes/` form |
| O2-1 (W-C and W-D are one cycled frame too shallow) | P1 | §1(b) cycled frames; §3 driver-and-wrapper template, modelled chains, arms (c-L), (c-P) and (c-H); §2.3 plain-read edit; §4 and §5 mixed traffic; §6 risk 8 |
| O2-2 (arm (b) is one-sided) | P2 | §3 harness rule; arm (b-L) with its bracket twin; arm (b-S) with its `CACHE_SIZE` twin; W-A launcher; in-boundary twins for every arm |
| O2-3 (one warm-up pass does not prime) | P3 | §1(b) warm-up; §3 K's recipe |

### Round 2 — disagreements

**Rejected by the triage, so not applied:**
- **C2-3, "fits the limit and depth ≤ 2 is not sufficient for asio's first-fit recycler": refuted by
  measurement.**
  - Codex is right that allocation is first-fit, that a miss evicts the first occupied block, and that
    deallocation fills the first empty slot (`asio/detail/thread_info_base.hpp`, `allocate` and `deallocate`).
  - What it missed: a recycled block keeps its **capacity**. `allocate` saves it with `mem[size] = mem[0]`, and
    `deallocate` restores it with `mem[0] = mem[size]`. In Codex's scenario, the larger block migrates into the
    outer role. After a short warm-up, both cached blocks are large enough and every request hits.
  - Round-2 triage q2 drives asio's real `thread_info_base` with Codex's exact pattern: after one warm-up iteration the
    next still allocates, the steady state reads 0, and the depth-3 and over-limit controls allocate. q2c's random search
    of depth-≤2 patterns finds none that allocates in steady state, while its depth-≤3 control does. Recipes are
    in that triage's Appendix A.
  - The real effect is how many warm-up passes it takes to converge. That is O2-3, and K's recipe (§3)
    addresses it.
  - Codex's counter-proposal, a required size-order check with padding, is not adopted.
- **C2-4 window 2, "a `total` signal arriving between part 2 and part 3": unreachable.**
  - Both `reset_cancellation_state` transforms are always ready (`await_ready()` returns `true` in
    `asio/impl/awaitable.hpp`), so part 2 never suspends.
  - Part 3's `await_transform` pre-check runs synchronously next, in the same resumption.
  - A `cancellation_signal::emit` must run on the strand that is executing this coroutine (inferred from
    asio's signal contract), so no signal can land between them.
  - Today's `async_lock` has the same adjacency: its `reset_cancellation_state(enable_total_cancellation{})`
    is followed directly by the `async_initiate(…, use_awaitable)` `co_await`.
  - The test-only synchronisation seam Codex proposes would test a state that no program can reach. It is not
    added.

**Counter-proposals not adopted, with the finding addressed by the triage's fix instead:**
- **C2-1, "a common oracle that accepts exactly the chosen concrete type".** It is adopted in substance,
  through the named alias: the oracle is `target<strand<session_inner_executor_t>>()`.
- **C2-5, "specify the exact non-failing mallocnesia threshold".** Not adopted. The twins register on Release
  only, because a non-failing `_mallocnesia` registration is a member that cannot fail (§3, *Release only*).

**Departures from the round-2 triage's prescribed fixes, each with its reason.** These are v0.3's, kept as
history. Each carries its v0.4 status under R-1′.
- **O2-2: the slot-budget twin is not planted in W-A.** *Superseded in v0.4:* W-A now carries a (b-S) pair
  shifted by the read chain's count r, with a nest of N − r as the twin and a nest of N − r + 1 as the arm (§3). The shift absorbs the read
  frame, which restores the triage's fix in W-A. The triage prescribes "a nest of exactly
  `ASIO_RECYCLING_ALLOCATOR_CACHE_SIZE`" as an in-boundary twin in each (b) harness.
  - In W-A, the launcher resumes after the pump has re-armed its read, so the transport read's frame is live
    while the plant runs. The W-A nest twin would then exceed the slots by construction.
  - (b-S) and its twin are therefore in W-D's driver harness only. W-A carries (b-L) and its twin (§3).
  - The pump's structure gives the live frame: the read pump in `src/session/engine.cpp` `co_await`s
    `transport.async_read_some`, which is a coroutine in `src/transport/asio_plain_transport.cpp`. The rewrite's
    q5 measured the consequence in a model: a nest of exactly the slot count allocates once per iteration while a
    re-armed read frame is live, and reads 0 without it (§1(b)).
- **O2-1: W-C's depth is a proxy, not production depth.** The triage writes that the wrapper "models …
  `ensure_hydrated_ → hydrate`".
  - In production, `ensure_hydrated_` is itself awaited from cycled Logon frames, so the full chain is deeper
    (§3, *Which production chain each depth models*).
  - W-C keeps the triage's depth as W8's apply-step proxy, and the full Logon chain is disclosed under mixed
    traffic (§4). *v0.4:* the departure stands. Under R-1′ the full chain is within N but still not gated.
  - W-C's rig is W8's persistent-store rig, not a MemoryStore-backed session: `ensure_hydrated_` skips
    `hydrate` on a non-persistent store.
- **O2-1: W-D's depth leaves out the concurrent read.** The triage calls the driver → wrapper → store column
  "production depth". *Superseded in v0.4:* the question below went to the owner, who answered it with R-1′.
  W-D-R gates the interleaving (§3).
  - With one handler thread, a liveness emit also runs while the pump's read frame is live. In q5's model,
    the store chain at that depth then allocates whenever the read was re-armed since the previous emit (§1(b)).
  - W-D keeps the triage's depth. The interleaving is disclosed under mixed traffic (§4).
  - Whether R-6's "mixed-traffic eviction" residual covers this case is for the orchestrator to decide. It is
    the steady state of an Active session that receives traffic, not an occasional eviction, and R-7 concerns
    exactly `MemoryStore::store`'s zero.
- **O2-1 fix 3: the plain-read `deferred` edit is kept as headroom, not witnessed.** This is the triage's
  second option. Witnessing it would need a test transport. §2.3 states that it is headroom only. *v0.4:*
  it is still headroom with no production gate. Its incidental witness moves from W-A's (b-L) twin, which no
  longer sees it at the exported N, to W-A's shifted (b-S) twin (§2.3, §3).

---

## As built (2026-10-10)

The sections above are the Gate-A-closed design and are not rewritten. This section states where the
implementation departs from them, each as a condition with its pointer. The records are in the parent repo:
the task list (`decisions/speckit/544-hot-path-zero-alloc-tasks.md`, § *Follow-ups found during
implementation*, which holds F-1..F-7 and the orchestrator's rulings) and the evidence file
(`decisions/speckit/544-hot-path-zero-alloc-evidence.md`, by phase), reached from this tree through
`.specify/decisions/`.

- **F-2: at asio's default slot count, removing a frame is not only headroom.** §2.3 says each frame-removing
  edit is slot headroom. That holds at the exported N. At asio's default N, the recycler is first-fit and
  evicts its first occupied slot, so a smaller or fewer-frame pattern can change its steady-state cycle and
  turn a zero window into an allocating one. An existing zero gate on the public `async_lock` did so between
  the frameless lock op and the exported N (evidence, Phase 2). The exported N is therefore part of what makes
  the zero hold; B&L `L-544-1` says why the value is fixpp's.
- **F-5: the guard's scope is narrower than §2.4 claims.** §2.4 says `consumer_witness.cpp`'s build is stopped
  by the guard's `static_assert`. It is not: that TU includes no header that reaches asio. The guard checks a
  translation unit that includes an installed fixpp header reaching asio; a translation unit that includes
  asio and no such header is not checked. `tests/consumer/run_consumer_witness.cmake`'s guard step compiles an
  asio-reaching consumer TU against the staged install without the definition and requires the guard's
  failure, with a twin through the imported target that must compile (negative arm on GCC and Clang front
  ends only). B&L `L-544-1` states the scope.
- **Phase 4 ruling A → a2 (the dispatcher).** §2.2's dispatcher was to select the per-state coroutine. As
  built, the non-coroutine `on_inbound_frame` always returns the Active arm, and the Active arm reads the FSM
  state on the strand at resume and hands a cold state to its own arm. Cold paths, which are ungated, cost one
  frame more. Reason: selecting the arm in the dispatcher would read the FSM state on the caller's thread.
- **Phase 4 ruling B → b3 (the cancellation check at a new boundary).** Every `co_await` boundary the split
  introduces (a leaf, a sub-arm, a2's cold hand-off) goes through one sequence:
  `FIXPP_INBOUND_SPLIT_AWAIT` reads `throw_if_cancelled`, sets it false and awaits the callee, and the callee's
  first statement, `FIXPP_INBOUND_SPLIT_ENTRY`, restores it (`src/session/session.cpp`). The first
  cancellation check therefore stays at the moved block's first original `co_await`, as before the split.
- **b3 on the send path: the naive-boundary mutant does not apply.** `Session::send`'s boundary into the
  fallback leaf goes through the same sequence. There, a cancellation already pending throws at the caller's
  `co_await` before anything observable runs, so deleting the sequence moves no check; the discriminating
  mutants are the leaf's entry-restore deleted and the sequence applied on the existing `send → send_impl`
  boundary (evidence, Phase 5).
- **R-10: the flag and the fallback.** The frame slot is held by `send_slot_in_use_`, through an RAII holder
  declared in `Session::send`'s `try`; a send that finds it held takes `send_fallback_leaf_`, which owns its
  buffer and so allocates. The holder writes `Session` memory when the send's frame ends, which adds a caller
  condition for direct `Session::send` callers (F-7, B&L `L-544-2`); production's only caller, `Engine::send`,
  meets it (evidence, Phase 6, T6.F7).
- **F-3: one `asio::detail` name in a `static_assert`.** The handler-size pin beside `detail::lock_op_handler_t`
  names `asio::detail::awaitable_async_op_handler`, because asio has no public name for it. It is a
  compile-time pin with no runtime dependency (2f Erratum E-6).
- **MSVC selects the fixpp executor.** §2.1's prediction holds: on `windows-msvc-*` `session_inner_executor_t`
  is the internal fixpp executor, and on GCC and Clang it is `io_context::executor_type` (evidence, MSVC
  session 1, "Which executor MSVC selected"). On MSVC the fast-path semantic cells are its behaviour oracle,
  and the counter checks the Active read's `operator new` half only.
- **Issues filed from this work, outside its scope:**
  - **fixpp#563** — a send nested in `toApp` can reuse the outer send's MsgSeqNum (§2.5; B&L `L-544-3`);
  - **fixpp#564** — a `Session::send` from inside an application callback trips `callback_dispatch_scope`'s
    `assert` on builds without `NDEBUG`, reproduced on the base (B&L `L-544-4`);
  - **fixpp#565** — TLS: OpenSSL's per-record allocations on the engine side remain on an Active TLS read
    (R-5's disposition; B&L `L-497-1`);
  - **fixpp#566** — the ccache flag surface does not see the guard header's N or the in-tree `asio::asio`
    definition append, so changing N alone does not rotate the ccache tag (F-4).
