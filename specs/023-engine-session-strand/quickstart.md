# Quickstart: Running the Engine on a Multi-Threaded Executor

**Feature**: 023-engine-session-strand
**Date**: 2026-06-05

This feature makes a multi-threaded `io_context` a supported way to drive the engine.
The default per-session strand mode now serializes each session's full lifecycle
(including teardown) across worker threads. The **only** application-visible change is
that `Engine::lookup()` now returns a `std::shared_ptr<Session>` (was a raw `Session*`)
so the handle stays valid if the engine shuts down concurrently — `lookup()` and
`acceptor_bound_endpoint()` are now safe to call from any thread while the engine runs
(they read a lock-free immutable snapshot, no lock, no block).

## Using it (application perspective)

```cpp
asio::io_context ioc;
fixpp::session::Engine engine{ioc.get_executor(), engine_cfg};
// ... register_session(...) for each session (before start, single-threaded) ...
engine.start();

// Service the engine with a pool of threads — now supported.
std::vector<std::thread> pool;
for (int i = 0; i < N; ++i) pool.emplace_back([&]{ ioc.run(); });

// ... application runs; sends may be issued from any thread via engine.send(...) ...
// lookup()/acceptor_bound_endpoint() are also any-thread-safe now:
std::shared_ptr<fixpp::session::Session> s = engine.lookup(some_id); // keepalive handle
if (s) { /* safe even if stop() races registry_.clear() on another thread */ }

// Shutdown is safe under multi-threading: teardown is serialized per session.
asio::co_spawn(ioc, engine.stop(), asio::use_future).get();
ioc.stop();
for (auto& t : pool) t.join();
```

No new configuration: the default `threading_mode::per_session_strand` already
selects per-session serialization. Applications that explicitly opt into the expert
`direct_executor` mode remain responsible for their own serialization (unchanged,
out of scope).

## Verifying the fix (developer perspective)

### 1. Deterministic regression witness — control-plane race (RED → GREEN, V-8)

```bash
# A ONE-SIDED PARK delays one thread inside the listener/endpoint-table write (reachable
# before any peer connects) while stop() clears those tables on another thread — NO shared
# sync object between them (a bidirectional latch would create a happens-before edge and
# SUPPRESS the race). TSan reports the data race deterministically on every pre-change run.
cmake --build build/linux-clang-tsan --target engine_session_strand_test -j2
./build/linux-clang-tsan/bin/engine_session_strand_test \
  --gtest_filter='*ControlPlaneRace_StopVsAcceptPublish*'
# Pre-change engine: TSan reports the listener_endpoints_/listeners_ data race EVERY run (reliable RED).
# Post-change engine: clean (the control strand serializes them).
```

The downstream TLS-teardown BIO crash is covered as a symptom by the MT acceptance
test (§2), not a separate seam (a transport-level seam gates after the BIO touch —
research D6 / Gate A round 1).

### 1b. Re-entrant send (V-9) + executor binding (V-10) + snapshot readers (V-11)

```bash
./build/linux-clang-tsan/bin/engine_session_strand_test \
  --gtest_filter='*ReentrantSend_SessionControlSession*:*SocketExecutorIsSessionStrand*:*SnapshotReadersMtSafe*'
# V-9: send from inside a callback (session→control→session) completes, no deadlock, TSan-clean;
#      a re-entrant send after stop() has begun fast-fails cleanly (no half-cleared-registry race).
# V-10: asserts transport.socket().get_executor() == the session strand at every ctor site.
# V-11: lookup()/acceptor_bound_endpoint() called from a thread while the engine runs and stop()
#       clears concurrently → TSan-clean; a lookup() handle taken before clear() keeps its session alive.
```

### 2. Multi-threaded lifecycle acceptance (SC-001)

```bash
# The existing 3-thread harness must be clean under all three sanitizers.
for p in asan ubsan tsan; do
  cmake --build build/linux-clang-$p --target business_messages_roundtrip_test -j2
  ./build/linux-clang-$p/bin/business_messages_roundtrip_test \
    --gtest_filter='*SendFromInsideFromApp_NoDeadlockNoUAF*'
done
```

### 3. Cross-session parallelism (V-3) & single-threaded parity (V-4)

```bash
# New MT 2-session cell shows independent progress; single-threaded suite unchanged.
ctest --test-dir build/linux-clang-tsan -L 'session' --output-on-failure
ctest --test-dir build/linux-clang-debug -R '^session_' --output-on-failure
```

### 4. Perf gate — TWO-hop send path (V-6, Article VIII ±5%)

```bash
# Cover session throughput AND the send / send-from-callback path (the two-hop
# caller→control→session route); re-measure the baseline against the two-hop design.
cd build/linux-clang-release && ./bin/<session_throughput_bench> \
  --benchmark_format=json --benchmark_out=../../bench/results/session_strand.json
cd ../.. && python3 tools/bench_compare.py \
  bench/baselines/<session_baseline>.json bench/results/session_strand.json
```

### 5. One intended API/ABI change only (V-5)

```bash
nm -D --defined-only build/linux-clang-release/lib/libfixpp_capi.so \
  | awk '$2=="T"{print $3}' | grep -v '^fixpp_' || echo "no leaked symbols"
# abidiff vs the prior tagged ABI: expect EXACTLY ONE diff — Engine::lookup()'s return type
# (Session* → std::shared_ptr<Session>, FR-008/SC-004) — and NO other. acceptor_bound_endpoint()
# keeps its Endpoint-by-value signature; no c_api.h change.
```

## Done criteria

- **V-8** control-plane race witness (one-sided park): TSan RED pre-change, GREEN post-change.
- **V-9** re-entrant send (session→control→session) no deadlock under TSan ≥3 threads, and
  clean fast-fail when issued after `stop()` has begun.
- **V-10** transport socket executor == session strand (asserted, all ctor sites).
- **V-11** snapshot public readers (`lookup`/`acceptor_bound_endpoint`) MT-safe under TSan;
  `lookup()` handle keepalive across a concurrent `clear()`.
- MT lifecycle clean (ASan/UBSan/TSan); cross-session parallelism preserved.
- Single-threaded suite green, no rewrites; perf within ±5% on the two-hop send path.
- `nm`/`abidiff` show **exactly one** intended diff (`lookup()` return type) and no other.
- behaviors-and-limitations **L-019-3 lifted** — only after the FULL witness set
  (V-1, V-2, V-8, V-9, V-10, V-11) passes under a clean sanitizer matrix; multi-threaded
  operation documented as supported.
