// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/session/file_store.cpp
//
// fixpp::session::FileStore — single append-only log implementation.
//
// Anchor: .specify/2e-msgstore.md v0.5 §4.3 / §6.3 / §6.3.5.
// FR-009 (on-disk format) / FR-011 (commit policies) / FR-012 (torn-write) /
// I-12 (record layout) / I-13 (cancellable_dispatch) / I-14 (torn-tail scan).
//
// On-disk record layout (16-byte header + payload + 8-byte aligned padding):
//   kind      : uint8_t  (RecordKind enum: frame=0, sentinel=1, counter=2)
//   dir       : uint8_t  (direction_t: inbound=0, outbound=1; 0xFF for sentinel)
//   reserved  : uint8_t[2]  (zero; reserved for future fields)
//   seq       : uint32_t (little-endian; 0 for sentinel/counter records)
//   len       : uint32_t (payload byte count, little-endian)
//   crc32     : uint32_t (Castagnoli 0x1EDC6F41 over header+payload)
//   payload   : len bytes
//   padding   : (8 - (16 + len) % 8) % 8 bytes (8-byte alignment)
//
// CRC32 covers: kind | dir | reserved(2) | seq(4) | len(4) | payload_bytes
// (all 12 non-crc32 header bytes + all payload bytes)
//
// Sentinel record payload (32 bytes):
//   magic     : uint64_t  (0x46495850535354 = "FIXPPST\0" LE, fixed)
//   version   : uint8_t   (1)
//   padding   : uint8_t[3]
//   session_triple_hash : uint32_t  (FNV-1a hash of sender + "\x00" + target)
//   reserved  : uint8_t[20] (zero)
//
// Counter record payload (8 bytes):
//   next_inbound  : uint32_t (little-endian)
//   next_outbound : uint32_t (little-endian)
//
// Restart algorithm:
//   1. Unlink stale <live>.log.reset.tmp if present.
//   2. Read and verify the sentinel at file offset 0. Mismatch → factory_failed.
//   3. Scan records forward. For each: verify CRC32. CRC32 mismatch at tail
//      → truncate (ftruncate/SetEndOfFile) to last good record + fdatasync/
//      FlushFileBuffers. Emit store_factory_failed if mid-file record corrupted.
//   4. Rebuild index from surviving frame records; set counters from
//      the last counter record (or from the index if no counter record found).
//
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fixpp/session/file_store.hpp>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

// Linux / POSIX headers (Tier-1)
#ifndef _WIN32
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#endif

// Windows headers (Tier-2 — compilation only on Linux Tier-1; runtime is T057)
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <crc32c/crc32c.h>

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/error.hpp>  // asio::error::operation_aborted — T012 durable catch
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/sync/async_mutex.hpp>  // T041: per-instance writer mutex
#include <fixpp/session/direction.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>

// T012 catch-fired diagnostic counter — compiled unconditionally (like
// g_store_offload_probe / install_store_offload_probe); declaration in
// file_store.hpp gated by FIXPP_TEST_HOOKS so production callers cannot reach it.
// The increment sites are unconditional (the library is compiled without
// FIXPP_TEST_HOOKS, so gating them would make them unreachable from tests).
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::atomic<int> g_catch_fired{0};

// gate-b/r1 A.2 + #6 fault-injection seam: forces asio::system_error(operation_aborted)
// to be thrown at the outer co_await of reset()'s offload, AFTER the pool lambda has
// completed, exercising the durable-commit catch body (C3). Compiled unconditionally
// (like g_catch_fired); declaration in file_store.hpp gated by FIXPP_TEST_HOOKS.
// The arm/throw logic inside reset() is unconditional so the check fires when the
// library is linked into test binaries that call arm_force_abort_after_reset_lambda().
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::atomic<bool> g_force_abort_after_reset_lambda{false};
// gate-b/r1 A.1 fault-injection seam: hook called just before reopen in reset()'s
// offload lambda; returns false to force the reopen to fail (tests the poison path).
// Compiled unconditionally; the call site inside the lambda is also unconditional.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::atomic<bool (*)(void) noexcept> g_post_rename_reopen_fail_hook{nullptr};

// T015 retrieve pread-attempt counter — counts each call to read_frame_payload()
// inside retrieve()'s walk loop. Compiled unconditionally (like g_catch_fired);
// the increment in the loop is gated by FIXPP_TEST_HOOKS (gate-b/r1 D: not needed
// on the production read path). Exposed via read_and_reset_retrieve_pread_count().
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::atomic<int> g_retrieve_pread_count{0};

// gate-b/r2 R#1a flush-ran witness counter — incremented AFTER raw_datasync() returns
// true inside flush_for_session_close()'s offload lambda (cold graceful-close path).
// Compiled UNCONDITIONALLY (library is compiled without FIXPP_TEST_HOOKS; a gated
// increment would be unreachable from tests that link the library — the #D regression).
// Declaration in file_store.hpp gated by FIXPP_TEST_HOOKS so production callers
// cannot reach it. The increment is on the cold graceful-close path (C3 carve-out),
// never the per-send hot path, so there is no §XV.1/§VIII.5 concern.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::atomic<int> g_flush_datasync_count{0};

namespace fixpp::session {

// ── Constants ─────────────────────────────────────────────────────────────────

static constexpr std::uint64_t kSentinelMagic = 0x46495850535354ULL;  // "FIXPPST\0"
static constexpr std::uint8_t kFormatVersion = 1;
static constexpr std::size_t kHeaderSize = 16;  // 16-byte record header
static constexpr std::size_t kSentinelPayloadSize = 32;
static constexpr std::size_t kCounterPayloadSize = 8;
static constexpr std::size_t kAlignment = 8;

// ── 035-filestore-io-offload: file-local offload helper ──────────────────────
//
// offload_to — run a single blocking syscall on pool_ex; resume the caller on
// its own (session-strand) executor.  This is the nested-co_spawn idiom:
// co_spawn pins the inner body to pool_ex; use_awaitable resumes the outer
// coroutine on the spawner's associated executor (the session strand).
//
// FINAL NO-REAPER SHAPE (data-model §2 / contracts C1 / Decision 5):
//   co_spawn defaults to terminal-only cancellation and FILTERS
//   cancellation_type::total.  That is intentional here: teardown drains by
//   AWAITING completion (Engine::stop() / T016), not by cancelling in-flight
//   syscalls.  An implementer MUST NOT bolt a cancellation-slot/flag reaper
//   onto this helper.
//
// CALLABLE CONTRACT (for the call sites built in Phase 3+):
//   'fn' MUST capture ONLY by value the raw POD syscall arguments:
//     - the OsFile fd/handle VALUE (int on Linux / HANDLE on Windows)
//     - an offset (std::int64_t or similar)
//     - a std::span<…> into an already-populated buffer
//   'fn' MUST NOT capture the OsFile RAII object by value (double-close risk)
//   nor any impl_ field by reference (strand-confinement violation).
//   All impl_ field mutations happen in the OUTER coroutine, on the strand,
//   before or after the co_await — never inside 'fn'. (data-model §3)
//
// Idiom precedent: src/transport/asio_tls_transport.hpp:45–51.
// [feedback_asio_post_resume_bounces_to_spawn_executor]

namespace {

template <class Fn>
asio::awaitable<std::invoke_result_t<Fn>> offload_to(asio::any_io_executor pool_ex, Fn fn) {
    co_return co_await asio::co_spawn(
        pool_ex,
        [fn = std::move(fn)]() -> asio::awaitable<std::invoke_result_t<Fn>> {
            co_return fn();  // raw blocking syscall runs HERE, pinned to pool_ex
        },
        asio::use_awaitable);
}

// ── Raw syscall helpers for the offload lambda ────────────────────────────────
//
// These operate on a raw fd (Linux) / HANDLE (Windows) captured BY VALUE in the
// lambda — the mutex is held by the outer coroutine so the fd is stable.
// They mirror OsFile::pwrite_all / OsFile::datasync without capturing the RAII
// object (which would cause a double-close if the outer OsFile destructs while
// the lambda captures a copy of it).

#ifndef _WIN32

// pwrite_all with EINTR loop — mirrors OsFile::pwrite_all.
[[nodiscard]] bool raw_pwrite_all(int fd, const void* buf, std::size_t n, off_t offset) noexcept {
    const auto* p = static_cast<const char*>(buf);
    std::size_t remaining = n;
    while (remaining > 0) {
        auto r = ::pwrite(fd, p, remaining, offset);
        if (r < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        p += r;
        offset += r;
        remaining -= static_cast<std::size_t>(r);
    }
    return true;
}

// fdatasync wrapper — mirrors OsFile::datasync.
[[nodiscard]] bool raw_datasync(int fd) noexcept { return ::fdatasync(fd) == 0; }

#else  // _WIN32

[[nodiscard]] bool raw_pwrite_all(HANDLE h, const void* buf, std::size_t n,
                                  std::int64_t offset) noexcept {
    const auto* p = static_cast<const char*>(buf);
    std::size_t remaining = n;
    std::int64_t off = offset;
    while (remaining > 0) {
        OVERLAPPED ov{};
        ov.Offset = static_cast<DWORD>(off & 0xFFFFFFFF);
        ov.OffsetHigh = static_cast<DWORD>((off >> 32) & 0xFFFFFFFF);
        DWORD written = 0;
        if (!WriteFile(h, p, static_cast<DWORD>(std::min(remaining, std::size_t(0xFFFFFFFF))),
                       &written, &ov)) {
            return false;
        }
        p += written;
        off += written;
        remaining -= written;
    }
    return true;
}

[[nodiscard]] bool raw_datasync(HANDLE h) noexcept { return FlushFileBuffers(h) != 0; }

#endif  // _WIN32

// ── Store offload probe hook (test seam) ──────────────────────────────────────
//
// g_store_offload_probe — a function pointer called at the START of the
// offloaded lambda (before the first pwrite) to record the executing thread-id.
// Production value: nullptr (no-op, zero overhead). Tests install a probe before
// calling store() and read the result after. This avoids FIXPP_TEST_HOOKS in the
// library TU (file_store.cpp is compiled without the flag) while still giving
// the test full observability of the offload thread. Pattern: runtime hook rather
// than compile-time flag, consistent with production-safe instrumentation.
//
// The probe signature: void(std::thread::id) — called with the executing tid.
// Thread-safe: written ONCE by the test (before pool spawns) and read under the
// offload lambda (pool thread). The atomic store/load provides the necessary
// synchronization without a separate mutex.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<void (*)(std::thread::id) noexcept> g_store_offload_probe{nullptr};

}  // namespace
// (anonymous namespace closed — we remain in fixpp::session for the rest of the file)

// ── file-store probe API (used by tests via file_store.hpp #ifdef FIXPP_TEST_HOOKS) ──
void install_store_offload_probe(void (*probe)(std::thread::id) noexcept) noexcept {
    g_store_offload_probe.store(probe, std::memory_order_relaxed);
}

// Read and reset the T012 operation_aborted catch-fired counter.
// Returns the count since the last reset; compiled unconditionally (declaration
// in file_store.hpp gated by FIXPP_TEST_HOOKS). Production builds never call it.
int read_and_reset_catch_fired() noexcept {
    return g_catch_fired.exchange(0, std::memory_order_acq_rel);
}

// Read and reset the T015 retrieve pread-attempt counter.
// Returns number of read_frame_payload() calls in retrieve()'s walk loop since
// the last reset. Used by MidWalkReset tests to confirm the generation guard
// fires BEFORE the second pread (count==1 not count==2 on a 2-frame walk).
// Compiled unconditionally; declaration in file_store.hpp gated by FIXPP_TEST_HOOKS.
int read_and_reset_retrieve_pread_count() noexcept {
    return g_retrieve_pread_count.exchange(0, std::memory_order_acq_rel);
}

// gate-b/r2 R#1a: read and reset the fdatasync-ran counter for flush_for_session_close().
// Returns the number of successful fdatasync calls since the last reset. Used by the
// SessionGracefulCloseFlushesFileStore witness to discriminate a skipped flush from a
// genuine fdatasync. Compiled unconditionally; declaration in file_store.hpp gated by
// FIXPP_TEST_HOOKS.
int read_and_reset_flush_datasync_count() noexcept {
    return g_flush_datasync_count.exchange(0, std::memory_order_acq_rel);
}

// gate-b/r1 A.2: arm the one-shot flag so the NEXT reset() simulates abort after durable.
// Compiled unconditionally; declaration in file_store.hpp gated by FIXPP_TEST_HOOKS.
void arm_force_abort_after_reset_lambda() noexcept {
    g_force_abort_after_reset_lambda.store(true, std::memory_order_relaxed);
}

// gate-b/r1 A.1: install (or clear) the post-rename reopen-fail hook.
// Compiled unconditionally; declaration in file_store.hpp gated by FIXPP_TEST_HOOKS.
void install_post_rename_reopen_fail_hook(bool (*hook)() noexcept) noexcept {
    g_post_rename_reopen_fail_hook.store(hook, std::memory_order_relaxed);
}

// ── Record kinds ──────────────────────────────────────────────────────────────

enum class RecordKind : std::uint8_t {
    frame = 0,
    sentinel = 1,
    counter = 2,
};

// ── On-disk record header (packed) ────────────────────────────────────────────

#pragma pack(push, 1)
struct RecordHeader {
    std::uint8_t kind;
    std::uint8_t dir;
    std::uint8_t reserved[2];
    std::uint32_t seq;    // little-endian
    std::uint32_t len;    // payload bytes, little-endian
    std::uint32_t crc32;  // Castagnoli over header[0..11] + payload
};
static_assert(sizeof(RecordHeader) == kHeaderSize);

struct SentinelPayload {
    std::uint64_t magic;                //  8 bytes
    std::uint8_t version;               //  1 byte
    std::uint8_t pad[3];                //  3 bytes  → 12 total
    std::uint32_t session_triple_hash;  //  4 bytes  → 16 total
    std::uint8_t reserved[16];          // 16 bytes  → 32 total
};
static_assert(sizeof(SentinelPayload) == kSentinelPayloadSize,
              "SentinelPayload must be exactly 32 bytes");

struct CounterPayload {
    std::uint32_t next_inbound;
    std::uint32_t next_outbound;
};
static_assert(sizeof(CounterPayload) == kCounterPayloadSize);
#pragma pack(pop)

// ── CRC32 computation ────────────────────────────────────────────────────────

// Compute CRC32c over the record header (minus the crc32 field itself)
// and the payload bytes.
static std::uint32_t compute_record_crc32(const RecordHeader& hdr, const std::uint8_t* payload,
                                          std::uint32_t len) noexcept {
    // CRC32 over: kind | dir | reserved(2) | seq(4) | len(4) | payload
    // That is the first 12 bytes of the header (excluding the crc32 field itself)
    // followed by len payload bytes.
    const std::uint8_t* header_start = reinterpret_cast<const std::uint8_t*>(&hdr);
    // Compute over header bytes [0..11] (kind+dir+reserved+seq+len = 12 bytes)
    std::uint32_t crc = crc32c::Extend(0, header_start, kHeaderSize - 4);
    // Extend over payload
    crc = crc32c::Extend(crc, payload, len);
    return crc;
}

// ── FNV-1a session hash ───────────────────────────────────────────────────────

static std::uint32_t fnv1a_32(std::string_view s) noexcept {
    std::uint32_t h = 2166136261U;
    for (unsigned char c : s) {
        h ^= c;
        h *= 16777619U;
    }
    return h;
}

static std::uint32_t session_triple_hash(std::string_view sender,
                                         std::string_view target) noexcept {
    std::uint32_t h = fnv1a_32(sender);
    h ^= fnv1a_32("\x00");
    h ^= fnv1a_32(target);
    return h;
}

// ── Padding ────────────────────────────────────────────────────────────────────

static std::size_t record_padding(std::size_t payload_len) noexcept {
    const std::size_t total = kHeaderSize + payload_len;
    const std::size_t aligned = (total + kAlignment - 1) & ~(kAlignment - 1);
    return aligned - total;
}

static std::size_t record_disk_size(std::size_t payload_len) noexcept {
    return kHeaderSize + payload_len + record_padding(payload_len);
}

// ── OS file abstraction (Linux + Windows) ─────────────────────────────────────

#ifndef _WIN32

struct OsFile {
    int fd{-1};

    OsFile() = default;
    ~OsFile() {
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
    }

    OsFile(const OsFile&) = delete;
    OsFile& operator=(const OsFile&) = delete;
    OsFile(OsFile&& o) noexcept : fd(o.fd) { o.fd = -1; }
    OsFile& operator=(OsFile&& o) noexcept {
        if (this != &o) {
            if (fd >= 0) ::close(fd);
            fd = o.fd;
            o.fd = -1;
        }
        return *this;
    }

    [[nodiscard]] bool valid() const noexcept { return fd >= 0; }

    // 035-filestore-io-offload: raw fd accessor for the offload lambda.
    // The lambda captures fd_value() BY VALUE (int) — NOT the OsFile object —
    // to avoid a double-close. The fd is stable for the lifetime of the OsFile;
    // the offload holds the mutex so no concurrent close/move can occur.
    [[nodiscard]] int fd_value() const noexcept { return fd; }

    // Open or create for read-write
    [[nodiscard]] bool open(const char* path) noexcept {
        fd = ::open(path, O_RDWR | O_CREAT, 0644);
        return fd >= 0;
    }

    // T042: Open for write-only, create/truncate (for .reset.tmp file)
    [[nodiscard]] bool open_wronly_creat(const char* path) noexcept {
        fd = ::open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
        return fd >= 0;
    }

    // Take advisory exclusive lock (non-blocking — fail immediately if held)
    [[nodiscard]] bool try_lock() const noexcept { return ::flock(fd, LOCK_EX | LOCK_NB) == 0; }

    // Release advisory lock
    void unlock() const noexcept {
        if (fd >= 0) ::flock(fd, LOCK_UN);
    }

    // pwrite: write at explicit offset (no file-position side-effect)
    [[nodiscard]] bool pwrite_all(const void* buf, std::size_t n, off_t offset) const noexcept {
        const auto* p = static_cast<const char*>(buf);
        std::size_t remaining = n;
        while (remaining > 0) {
            auto r = ::pwrite(fd, p, remaining, offset);
            if (r < 0) {
                if (errno == EINTR) continue;
                return false;
            }
            p += r;
            offset += r;
            remaining -= static_cast<std::size_t>(r);
        }
        return true;
    }

    // pread: read at explicit offset
    [[nodiscard]] ssize_t pread_all(void* buf, std::size_t n, off_t offset) const noexcept {
        auto* p = static_cast<char*>(buf);
        std::size_t total = 0;
        while (total < n) {
            auto r = ::pread(fd, p + total, n - total, offset + static_cast<off_t>(total));
            if (r < 0) {
                if (errno == EINTR) continue;
                return -1;
            }
            if (r == 0) break;  // EOF
            total += static_cast<std::size_t>(r);
        }
        return static_cast<ssize_t>(total);
    }

    // fdatasync: flush data to disk
    [[nodiscard]] bool datasync() const noexcept { return ::fdatasync(fd) == 0; }

    // Truncate to given size
    [[nodiscard]] bool truncate(off_t new_size) const noexcept {
        return ::ftruncate(fd, new_size) == 0;
    }

    // Get current file size
    [[nodiscard]] off_t file_size() const noexcept {
        struct stat st{};
        if (::fstat(fd, &st) != 0) return -1;
        return st.st_size;
    }
};

#else  // _WIN32

struct OsFile {
    HANDLE h{INVALID_HANDLE_VALUE};

    OsFile() = default;
    ~OsFile() {
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
            h = INVALID_HANDLE_VALUE;
        }
    }

    OsFile(const OsFile&) = delete;
    OsFile& operator=(const OsFile&) = delete;
    OsFile(OsFile&& o) noexcept : h(o.h) { o.h = INVALID_HANDLE_VALUE; }
    OsFile& operator=(OsFile&& o) noexcept {
        if (this != &o) {
            if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
            h = o.h;
            o.h = INVALID_HANDLE_VALUE;
        }
        return *this;
    }

    [[nodiscard]] bool valid() const noexcept { return h != INVALID_HANDLE_VALUE; }

    // 035-filestore-io-offload: raw HANDLE accessor for the offload lambda.
    [[nodiscard]] HANDLE fd_value() const noexcept { return h; }

    [[nodiscard]] bool open(const wchar_t* path) noexcept {
        h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, nullptr);
        return h != INVALID_HANDLE_VALUE;
    }

    // T042: Open for write-only, create/truncate (for .reset.tmp file)
    [[nodiscard]] bool open_wronly_creat(const wchar_t* path) noexcept {
        h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                        CREATE_ALWAYS,  // create/truncate
                        FILE_ATTRIBUTE_NORMAL, nullptr);
        return h != INVALID_HANDLE_VALUE;
    }

    [[nodiscard]] bool try_lock() noexcept {
        OVERLAPPED ov{};
        return LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, MAXDWORD,
                          MAXDWORD, &ov) != 0;
    }

    void unlock() noexcept {
        if (h != INVALID_HANDLE_VALUE) {
            OVERLAPPED ov{};
            UnlockFileEx(h, 0, MAXDWORD, MAXDWORD, &ov);
        }
    }

    [[nodiscard]] bool pwrite_all(const void* buf, std::size_t n, std::int64_t offset) noexcept {
        const auto* p = static_cast<const char*>(buf);
        std::size_t remaining = n;
        std::int64_t off = offset;
        while (remaining > 0) {
            OVERLAPPED ov{};
            ov.Offset = static_cast<DWORD>(off & 0xFFFFFFFF);
            ov.OffsetHigh = static_cast<DWORD>((off >> 32) & 0xFFFFFFFF);
            DWORD written = 0;
            if (!WriteFile(h, p, static_cast<DWORD>(std::min(remaining, std::size_t(0xFFFFFFFF))),
                           &written, &ov)) {
                return false;
            }
            p += written;
            off += written;
            remaining -= written;
        }
        return true;
    }

    [[nodiscard]] SSIZE_T pread_all(void* buf, std::size_t n, std::int64_t offset) noexcept {
        auto* p = static_cast<char*>(buf);
        std::size_t total = 0;
        std::int64_t off = offset;
        while (total < n) {
            OVERLAPPED ov{};
            ov.Offset = static_cast<DWORD>(off & 0xFFFFFFFF);
            ov.OffsetHigh = static_cast<DWORD>((off >> 32) & 0xFFFFFFFF);
            DWORD read_bytes = 0;
            if (!ReadFile(h, p + total,
                          static_cast<DWORD>(std::min(n - total, std::size_t(0xFFFFFFFF))),
                          &read_bytes, &ov)) {
                if (GetLastError() == ERROR_HANDLE_EOF) break;
                return -1;
            }
            if (read_bytes == 0) break;
            total += read_bytes;
            off += read_bytes;
        }
        return static_cast<SSIZE_T>(total);
    }

    [[nodiscard]] bool datasync() noexcept { return FlushFileBuffers(h) != 0; }

    [[nodiscard]] bool truncate(std::int64_t new_size) noexcept {
        LARGE_INTEGER li;
        li.QuadPart = new_size;
        if (!SetFilePointerEx(h, li, nullptr, FILE_BEGIN)) return false;
        return SetEndOfFile(h) != 0;
    }

    [[nodiscard]] std::int64_t file_size() noexcept {
        LARGE_INTEGER sz{};
        if (!GetFileSizeEx(h, &sz)) return -1;
        return sz.QuadPart;
    }
};

#endif  // _WIN32

// ── Index entry ────────────────────────────────────────────────────────────────

struct IndexEntry {
    seqnum_t seq{};
    direction_t dir{};
    std::int64_t file_offset{};  // offset of record header in the log file
    std::uint32_t len{};         // payload byte count
};

// ── FileStoreImpl ─────────────────────────────────────────────────────────────

struct FileStoreImpl {
    FileStore::Config cfg;
    OsFile file;
    std::int64_t write_pos{0};  // append position
    seqnum_t next_inbound{seqnum_min};
    seqnum_t next_outbound{seqnum_min};
    std::uint32_t expected_hash{0};
    bool open_ok{false};
    std::string log_path_;  // T041/T042: stored at open_log() for reset()

    // T041/US3: per-instance writer mutex (FR-015 / FR-018 / I-01 / [SYN §3.2 Q6b]).
    // NO std::mutex — uses fixpp::sync::async_mutex, FIFO-fair, cancellable.
    // Declared before index/counter members so destruction order is correct:
    // mutex still valid when entries are accessed during drain.
    fixpp::sync::async_mutex mutex_;

    // RC#6: PMR-backed reusable scratch buffer for store() frame deep-copy.
    // Replaces per-call `std::vector<std::byte>` (global operator new on every
    // store()). Uses cfg.store_resource if non-null, else get_default_resource().
    // Reserved to max_frame_bytes at open_log() time (after cfg is set).
    // assign() does NOT reallocate after reserve() when frame ≤ max_frame_bytes
    // (guaranteed by the frame-size check in store() above).
    // [const §VIII.5]: zero global-heap allocation between parse and fromApp.
    std::pmr::vector<std::byte> store_scratch_;

    // N4: PMR-backed reusable scratch buffer for retrieve() frame reads.
    // Separate from store_scratch_ because retrieve() releases the writer mutex
    // before the per-frame disk reads, enabling concurrent store() calls from
    // within the visitor body — two threads could be in store() and retrieve()
    // simultaneously, so they must use distinct scratch buffers.
    // Initialised and reserved at open_log() time alongside store_scratch_.
    // read_frame_payload() resizes into this buffer (no global-heap alloc per frame).
    std::pmr::vector<std::byte> retrieve_scratch_;

    // 035: monotonic epoch bumped by reset() on the strand at/after the live-handle
    // swap (file_store.cpp:1149); snapshotted by retrieve() under the mutex at
    // index-snapshot time and re-checked before each per-frame pread in the walk.
    // A mismatch means a reset() ran during a visitor.on_frame() suspension —
    // retrieve() returns store_io_failure (clean-fail, never reads a swapped handle).
    // Plain scalar — mutated strand-only (like every other impl_ field per Decision 3),
    // so no atomic is needed. (data-model §1)
    std::uint64_t generation_{0};

    // Per-direction frame index (rebuilt during open/restart scan).
    // entries are in insertion order (seq ascending, starting from 1).
    std::vector<IndexEntry> inbound_index;
    std::vector<IndexEntry> outbound_index;

    // ── Sentinel write ─────────────────────────────────────────────────────

    bool write_sentinel(std::int64_t offset) const noexcept {
        RecordHeader hdr{};
        SentinelPayload pl{};
        pl.magic = kSentinelMagic;
        pl.version = kFormatVersion;
        pl.session_triple_hash = expected_hash;

        hdr.kind = static_cast<std::uint8_t>(RecordKind::sentinel);
        hdr.dir = 0xFF;
        hdr.seq = 0;
        hdr.len = static_cast<std::uint32_t>(kSentinelPayloadSize);
        hdr.crc32 = compute_record_crc32(hdr, reinterpret_cast<const std::uint8_t*>(&pl),
                                         static_cast<std::uint32_t>(kSentinelPayloadSize));

        if (!file.pwrite_all(&hdr, kHeaderSize, offset)) return false;
        if (!file.pwrite_all(&pl, kSentinelPayloadSize,
                             offset + static_cast<std::int64_t>(kHeaderSize))) {
            return false;
        }
        // Padding bytes (all zero)
        const std::size_t pad = record_padding(kSentinelPayloadSize);
        if (pad > 0) {
            const std::uint8_t zeros[8]{};
            if (!file.pwrite_all(
                    zeros, pad,
                    offset + static_cast<std::int64_t>(kHeaderSize + kSentinelPayloadSize))) {
                return false;
            }
        }
        return true;
    }

    // ── Counter record write ───────────────────────────────────────────────

    bool write_counter(std::int64_t offset, seqnum_t ni, seqnum_t no) const noexcept {
        RecordHeader hdr{};
        CounterPayload pl{};
        pl.next_inbound = ni;
        pl.next_outbound = no;

        hdr.kind = static_cast<std::uint8_t>(RecordKind::counter);
        hdr.dir = 0xFF;
        hdr.seq = 0;
        hdr.len = static_cast<std::uint32_t>(kCounterPayloadSize);
        hdr.crc32 = compute_record_crc32(hdr, reinterpret_cast<const std::uint8_t*>(&pl),
                                         static_cast<std::uint32_t>(kCounterPayloadSize));

        if (!file.pwrite_all(&hdr, kHeaderSize, offset)) return false;
        if (!file.pwrite_all(&pl, kCounterPayloadSize,
                             offset + static_cast<std::int64_t>(kHeaderSize))) {
            return false;
        }
        return true;
    }

    // ── Frame record write ─────────────────────────────────────────────────

    bool write_frame(seqnum_t seq, direction_t dir, std::span<const std::byte> frame) noexcept {
        if (frame.size() > cfg.max_frame_bytes) return false;

        RecordHeader hdr{};
        hdr.kind = static_cast<std::uint8_t>(RecordKind::frame);
        hdr.dir = static_cast<std::uint8_t>(dir);
        hdr.seq = seq;
        hdr.len = static_cast<std::uint32_t>(frame.size());
        hdr.crc32 = compute_record_crc32(hdr, reinterpret_cast<const std::uint8_t*>(frame.data()),
                                         static_cast<std::uint32_t>(frame.size()));

        const std::int64_t record_offset = write_pos;
        if (!file.pwrite_all(&hdr, kHeaderSize, record_offset)) return false;
        if (!frame.empty()) {
            if (!file.pwrite_all(frame.data(), frame.size(),
                                 record_offset + static_cast<std::int64_t>(kHeaderSize))) {
                return false;
            }
        }
        const std::size_t pad = record_padding(frame.size());
        if (pad > 0) {
            const std::uint8_t zeros[8]{};
            if (!file.pwrite_all(
                    zeros, pad,
                    record_offset + static_cast<std::int64_t>(kHeaderSize + frame.size()))) {
                return false;
            }
        }

        // Add index entry
        IndexEntry ie;
        ie.seq = seq;
        ie.dir = dir;
        ie.file_offset = record_offset;
        ie.len = static_cast<std::uint32_t>(frame.size());
        auto& idx = (dir == direction_t::inbound) ? inbound_index : outbound_index;
        idx.push_back(ie);

        write_pos += static_cast<std::int64_t>(record_disk_size(frame.size()));
        return true;
    }

    // ── Frame record read ──────────────────────────────────────────────────

    // Read payload of a frame record at file_offset into dst.
    // N4: dst is now std::pmr::vector<std::byte> (retrieve_scratch_) so the
    // resize() does NOT allocate from the global heap when the buffer already
    // has enough capacity (reserved at open_log() to max_frame_bytes).
    // Returns false on I/O error.
    bool read_frame_payload(const IndexEntry& ie, std::pmr::vector<std::byte>& dst) const noexcept {
        dst.resize(ie.len);
        const std::int64_t payload_offset = ie.file_offset + static_cast<std::int64_t>(kHeaderSize);
        auto n = file.pread_all(dst.data(), ie.len, payload_offset);
        return std::cmp_equal(n, ie.len);
    }

    // ── Restart scan (FR-012 / I-14) ──────────────────────────────────────

    // Returns true on success; false means caller should return store_factory_failed.
    bool restart_scan(const std::string& log_path) noexcept {
        // Step 0: Unlink stale .log.reset.tmp if present (I-14 / T042 prep).
        const std::string tmp_path = log_path + ".reset.tmp";
        if (::access(tmp_path.c_str(), F_OK) == 0) {
            ::unlink(tmp_path.c_str());
        }

        const std::int64_t fsize = file.file_size();
        if (fsize < 0) return false;

        // Empty file: needs initialisation (write sentinel + initial counter).
        if (fsize == 0) {
            return initialise_fresh();
        }

        // Step 1: Read and verify sentinel at offset 0.
        {
            RecordHeader hdr{};
            auto n = file.pread_all(&hdr, kHeaderSize, 0);
            if (std::cmp_not_equal(n, kHeaderSize)) return false;

            if (hdr.kind != static_cast<std::uint8_t>(RecordKind::sentinel)) {
                return false;  // First record must be sentinel
            }
            if (hdr.len != kSentinelPayloadSize) return false;

            SentinelPayload pl{};
            n = file.pread_all(&pl, kSentinelPayloadSize, static_cast<std::int64_t>(kHeaderSize));
            if (std::cmp_not_equal(n, kSentinelPayloadSize)) return false;

            // Verify CRC32 of sentinel
            const std::uint32_t expected_crc =
                compute_record_crc32(hdr, reinterpret_cast<const std::uint8_t*>(&pl),
                                     static_cast<std::uint32_t>(kSentinelPayloadSize));
            if (hdr.crc32 != expected_crc) return false;

            // Verify magic and version
            if (pl.magic != kSentinelMagic) return false;
            if (pl.version != kFormatVersion) return false;

            // Verify session identity
            if (pl.session_triple_hash != expected_hash) return false;
        }

        // Step 2: Scan records from after the sentinel.
        const std::int64_t sentinel_end =
            static_cast<std::int64_t>(record_disk_size(kSentinelPayloadSize));

        std::int64_t scan_pos = sentinel_end;
        std::int64_t last_good_pos = sentinel_end;
        bool seen_counter = false;
        seqnum_t recovered_ni = seqnum_min;
        seqnum_t recovered_no = seqnum_min;

        while (scan_pos < fsize) {
            // Read header
            RecordHeader hdr{};
            const std::int64_t remaining = fsize - scan_pos;
            if (std::cmp_less(remaining, kHeaderSize)) {
                // Partial header at tail → truncate here
                break;
            }
            auto n = file.pread_all(&hdr, kHeaderSize, scan_pos);
            if (std::cmp_not_equal(n, kHeaderSize)) break;

            const std::size_t payload_len = hdr.len;
            const std::int64_t payload_offset = scan_pos + static_cast<std::int64_t>(kHeaderSize);

            // RC#3 + N3 fix: cap-check BEFORE computing disk_size and BEFORE the
            // partial-record size check. An adversarial or corrupt hdr.len
            // (e.g. 0xFFFFFFFF) overflows record_disk_size() arithmetic on 32-bit
            // and produces an incorrect partial-record verdict on 64-bit when it
            // merely appears to not fit.
            //
            // N3: distinguish torn tail from mid-log corruption per [2e §6.3].
            // The restart invariant "only one torn record, and only at the tail"
            // means that if a corrupt/oversized header is followed by more file
            // data, those bytes are a valid suffix that would be silently discarded
            // — that is mid-log corruption and must surface store_factory_failed.
            //
            // If payload_len > max_frame_bytes: corrupt header.
            //   - If there are bytes after the header (scan_pos + kHeaderSize < fsize):
            //     mid-log corruption → return false.
            //   - Otherwise: torn tail → break (truncate to last_good_pos).
            if (payload_len > cfg.max_frame_bytes) {
                if (scan_pos + static_cast<std::int64_t>(kHeaderSize) < fsize) {
                    // Data exists after the corrupt header — mid-log corruption.
                    return false;
                }
                // No data after the header — treat as torn tail, truncate.
                break;
            }

            // payload_len is within bounds; disk_size arithmetic is now safe.
            const std::size_t disk_size = record_disk_size(payload_len);

            // Check that there's enough data for the record payload + padding.
            if (scan_pos + static_cast<std::int64_t>(disk_size) > fsize) {
                // Partial record at tail → truncate here.
                break;
            }

            // Read payload for CRC32 verification
            std::vector<std::uint8_t> payload_buf(payload_len);
            if (payload_len > 0) {
                n = file.pread_all(payload_buf.data(), payload_len, payload_offset);
                if (std::cmp_not_equal(n, payload_len)) break;
            }

            // Verify CRC32
            const std::uint32_t expected_crc = compute_record_crc32(
                hdr, payload_buf.data(), static_cast<std::uint32_t>(payload_len));
            if (hdr.crc32 != expected_crc) {
                // N3 fix: CRC32 mismatch — determine torn-tail vs mid-log.
                // disk_size is valid (cap-check passed). If there is file data
                // AFTER this record's footprint, valid suffix records exist —
                // that is mid-log corruption, not a torn tail.
                if (scan_pos + static_cast<std::int64_t>(disk_size) < fsize) {
                    return false;
                }
                // Torn tail: no valid suffix — truncate to last_good_pos.
                break;
            }

            // Process record
            const auto kind = static_cast<RecordKind>(hdr.kind);
            if (kind == RecordKind::frame) {
                IndexEntry ie;
                ie.seq = hdr.seq;
                ie.dir = static_cast<direction_t>(hdr.dir);
                ie.file_offset = scan_pos;
                ie.len = static_cast<std::uint32_t>(payload_len);
                auto& idx = (ie.dir == direction_t::inbound) ? inbound_index : outbound_index;
                idx.push_back(ie);
            } else if (kind == RecordKind::counter) {
                if (payload_len == kCounterPayloadSize) {
                    CounterPayload cp{};
                    std::memcpy(&cp, payload_buf.data(), kCounterPayloadSize);
                    recovered_ni = cp.next_inbound;
                    recovered_no = cp.next_outbound;
                    seen_counter = true;
                }
            }
            // sentinel inside body is ignored (only first sentinel is authoritative)

            last_good_pos = scan_pos + static_cast<std::int64_t>(disk_size);
            scan_pos = last_good_pos;
        }

        // Truncate tail to last good position if needed
        if (last_good_pos < fsize) {
            if (!file.truncate(last_good_pos)) return false;
            if (!file.datasync()) return false;
        }

        write_pos = last_good_pos;

        // Recover counters
        if (seen_counter) {
            next_inbound = recovered_ni;
            next_outbound = recovered_no;
        } else {
            // Infer from index: vectors are built in seqnum-monotonic order,
            // so back().seq is the maximum (O(1) per direction).
            const seqnum_t max_in = inbound_index.empty() ? seqnum_t{0} : inbound_index.back().seq;
            const seqnum_t max_out =
                outbound_index.empty() ? seqnum_t{0} : outbound_index.back().seq;
            next_inbound = (max_in > 0) ? max_in + 1 : seqnum_min;
            next_outbound = (max_out > 0) ? max_out + 1 : seqnum_min;
        }

        return true;
    }

    // ── Fresh file initialisation ──────────────────────────────────────────

    bool initialise_fresh() noexcept {
        // Write sentinel at offset 0
        if (!write_sentinel(0)) return false;
        const std::int64_t sentinel_disk_size =
            static_cast<std::int64_t>(record_disk_size(kSentinelPayloadSize));

        // Write initial counter record
        if (!write_counter(sentinel_disk_size, seqnum_min, seqnum_min)) return false;
        if (!file.datasync()) return false;

        write_pos =
            sentinel_disk_size + static_cast<std::int64_t>(record_disk_size(kCounterPayloadSize));
        next_inbound = seqnum_min;
        next_outbound = seqnum_min;
        return true;
    }
};

// ── FileStore ctor / dtor ─────────────────────────────────────────────────────

FileStore::FileStore(Config c) noexcept
    : MessageStore(MessageStore::flush_thunk_for<FileStore>()),
      impl_(std::make_unique<FileStoreImpl>()) {
    impl_->cfg = std::move(c);
    impl_->expected_hash =
        session_triple_hash(impl_->cfg.sender_comp_id, impl_->cfg.target_comp_id);
}

// Advisory lock is released when OsFile destructs (close() releases flock).
FileStore::~FileStore() noexcept = default;

// ── FileStore::store() ────────────────────────────────────────────────────────

asio::awaitable<fixpp::core::expected_t<void>> FileStore::store(seqnum_t seq,
                                                                std::span<const std::byte> frame
                                                                [[clang::lifetimebound]],
                                                                direction_t dir) noexcept {
    // Capture the session executor for the leading pump-break post below. (035:
    // the offload uses nested co_spawn(use_awaitable), which resumes on this
    // spawning executor — there is no longer a file_io_executor hop to rebind
    // from; session_ex now serves only the leading post.)
    const auto session_ex = co_await asio::this_coro::executor;

    // T041/US3: leading post to break recursive awaitable_thread::pump() chain
    // (same rationale as MemoryStore — synchronous fast-path of async_lock
    // causes unbounded stack growth in tight coroutine loops).
    // Note: this post re-uses session_ex (which equals this_coro::executor here);
    // the leading-post pattern is correct and is PRESERVED per RC#4 brief.
    co_await asio::post(session_ex, asio::use_awaitable);

    if (!impl_->open_ok) {
        co_return std::unexpected(fixpp::core::error::store_io_failure);
    }

    // T041/US3: acquire per-instance writer mutex (FR-015 / I-01).
    auto guard_result = co_await impl_->mutex_.async_lock();
    if (!guard_result) {
        co_return std::unexpected(fixpp::core::error::store_cancelled);
    }
    auto guard = std::move(*guard_result);

    // ── Critical section ───────────────────────────────────────────────────
    // Seqnum-order check inside CS (FR-018 / I-05).
    const seqnum_t next =
        (dir == direction_t::inbound) ? impl_->next_inbound : impl_->next_outbound;
    if (seq != next) {
        co_return std::unexpected(fixpp::core::error::store_seqnum_out_of_order);
    }

    if (frame.size() > impl_->cfg.max_frame_bytes) {
        co_return std::unexpected(fixpp::core::error::store_io_failure);
    }

    // ── Region 1: STRAND — prepare syscall arguments (mutex held) ────────────
    //
    // Deep-copy frame into PMR scratch buffer, build both RecordHeaders +
    // CounterPayload as locals, compute file offsets and flush decision.
    // impl_ is NOT mutated here — all mutations happen on the success path
    // after the offload returns (Region 3). (035 data-model §3 / brief §1)

    // RC#6: use PMR-backed store_scratch_ (reserved at open_log() to
    // max_frame_bytes; assign() does NOT reallocate when size <= capacity).
    impl_->store_scratch_.assign(frame.begin(), frame.end());
    const std::span<const std::byte> frame_span(impl_->store_scratch_);

    // File offsets for the two records.
    const std::int64_t frame_off = impl_->write_pos;
    const std::int64_t counter_off =
        frame_off + static_cast<std::int64_t>(record_disk_size(frame_span.size()));

    // Build frame record header (mirrors write_frame:557–563).
    RecordHeader frame_hdr{};
    frame_hdr.kind = static_cast<std::uint8_t>(RecordKind::frame);
    frame_hdr.dir = static_cast<std::uint8_t>(dir);
    frame_hdr.seq = seq;
    frame_hdr.len = static_cast<std::uint32_t>(frame_span.size());
    frame_hdr.crc32 =
        compute_record_crc32(frame_hdr, reinterpret_cast<const std::uint8_t*>(frame_span.data()),
                             static_cast<std::uint32_t>(frame_span.size()));

    // Pre-compute post-increment counter values (NOT applied to impl_ until
    // success in Region 3 — avoids leaving counters advanced on I/O failure).
    const seqnum_t ni =
        (dir == direction_t::inbound) ? impl_->next_inbound + 1 : impl_->next_inbound;
    const seqnum_t no =
        (dir == direction_t::outbound) ? impl_->next_outbound + 1 : impl_->next_outbound;

    // Build counter record header + payload (mirrors write_counter:532–542).
    CounterPayload counter_pl{};
    counter_pl.next_inbound = ni;
    counter_pl.next_outbound = no;
    RecordHeader counter_hdr{};
    counter_hdr.kind = static_cast<std::uint8_t>(RecordKind::counter);
    counter_hdr.dir = 0xFF;
    counter_hdr.seq = 0;
    counter_hdr.len = static_cast<std::uint32_t>(kCounterPayloadSize);
    counter_hdr.crc32 =
        compute_record_crc32(counter_hdr, reinterpret_cast<const std::uint8_t*>(&counter_pl),
                             static_cast<std::uint32_t>(kCounterPayloadSize));

    // Flush-policy decision (mirrors :900–914); index sizes are strand-only.
    const auto policy_kind = impl_->cfg.policy.which;
    bool do_flush = false;
    if (policy_kind == FileStorePolicy::kind::commit_per_message) {
        do_flush = true;
    } else if (policy_kind == FileStorePolicy::kind::commit_batched) {
        // The new index entry will be the (N+1)-th entry; N = current total.
        const std::size_t total_after =
            impl_->inbound_index.size() + impl_->outbound_index.size() + 1;
        const std::size_t bs = impl_->cfg.policy.batch_size;
        if (bs > 0 && (total_after % bs) == 0) {
            do_flush = true;
        }
    }

    // Capture raw fd BY VALUE (not the OsFile RAII object — double-close risk).
    // fd is stable: mutex held by outer coroutine prevents concurrent close/move.
    const auto raw_fd = impl_->file.fd_value();
    const std::size_t frame_pad = record_padding(frame_span.size());

    // Snapshot the probe pointer on the strand (where it was installed by the
    // test). The lambda captures it by value so the pool thread can call it.
    // Production value: nullptr (no-op, zero overhead per store() call).
    const auto probe_fn = g_store_offload_probe.load(std::memory_order_relaxed);

    // ── Region 2: POOL — raw syscalls in the offloaded lambda ─────────────────
    //
    // The lambda captures ONLY by value: raw fd, POD headers/payloads, offsets,
    // frame_span (span into store_scratch_ — stable while mutex held), pad,
    // flush decision, and the (nullable) probe function pointer.
    // It touches NO impl_ field and mutates nothing in impl_. (data-model §2/§3)
    //
    // T012 — unconditional operation_aborted→durable catch (FR-004 / C3):
    // Any operation_aborted surfaced at this outer co_await post-dates linearisation
    // (the blocking syscall is non-interruptible on the pool thread; it runs to
    // durable completion). Treat it as durable success — do NOT return store_cancelled
    // for a frame already on disk ([const §XV.15]-adjacent silent-loss class).
    // The co_spawn has no cancellation reaper (terminal-only default) so this catch
    // is defensive per §C3; a non-operation_aborted exception falls through (OOM etc).
    // [[feedback_async_mutex_us3_asio_cancel_and_subagent_seams]]
    bool io_ok = false;
    try {
        io_ok = co_await offload_to(
            impl_->cfg.file_io_executor,
            [raw_fd, frame_hdr, frame_span, frame_off, frame_pad, counter_hdr, counter_pl,
             counter_off, do_flush, probe_fn]() -> bool {
                // Call the test probe (if installed) BEFORE the first pwrite so the
                // test can confirm the syscall ran on a pool thread ≠ strand thread.
                // In production probe_fn is nullptr; the branch is dead-code-eliminated.
                if (probe_fn) {
                    probe_fn(std::this_thread::get_id());
                }

                // pwrite frame header at frame_off.
                if (!raw_pwrite_all(raw_fd, &frame_hdr, kHeaderSize,
                                    static_cast<off_t>(frame_off))) {
                    return false;
                }
                // pwrite frame payload.
                if (!frame_span.empty()) {
                    if (!raw_pwrite_all(raw_fd, frame_span.data(), frame_span.size(),
                                        static_cast<off_t>(
                                            frame_off + static_cast<std::int64_t>(kHeaderSize)))) {
                        return false;
                    }
                }
                // pwrite frame padding.
                if (frame_pad > 0) {
                    const std::uint8_t zeros[8]{};
                    if (!raw_pwrite_all(
                            raw_fd, zeros, frame_pad,
                            static_cast<off_t>(frame_off + static_cast<std::int64_t>(
                                                               kHeaderSize + frame_span.size())))) {
                        return false;
                    }
                }
                // pwrite counter header.
                if (!raw_pwrite_all(raw_fd, &counter_hdr, kHeaderSize,
                                    static_cast<off_t>(counter_off))) {
                    return false;
                }
                // pwrite counter payload.
                if (!raw_pwrite_all(
                        raw_fd, &counter_pl, kCounterPayloadSize,
                        static_cast<off_t>(counter_off + static_cast<std::int64_t>(kHeaderSize)))) {
                    return false;
                }
                // datasync: linearisation point (FR-003 / C2). Blocks until durable.
                if (do_flush && !raw_datasync(raw_fd)) {
                    return false;
                }
                return true;
            });
    } catch (const asio::system_error& e) {
        if (e.code() != asio::error::operation_aborted) {
            throw;  // Only operation_aborted is handled; anything else is unexpected
                    // and propagates (noexcept coroutine → terminate). Consistent
                    // with next_seqnum()/reset().
        }
        // operation_aborted post-dates linearisation: the syscall completed durably.
        // Fall through with io_ok=true so Region 3 applies the correct mutations.
        g_catch_fired.fetch_add(1, std::memory_order_relaxed);
        io_ok = true;
    }

    // ── Region 3: STRAND — apply mutations on success (mutex still held) ──────
    //
    // On I/O failure: no impl_ mutation — counters, index, write_pos unchanged.
    // On success: push index entry, set counters to the pre-computed values,
    // advance write_pos. (035 data-model §3 / brief §3)
    if (!io_ok) {
        co_return std::unexpected(fixpp::core::error::store_io_failure);
    }

    // Push index entry (mirrors write_frame:583–590, stripped of pwrite).
    IndexEntry ie;
    ie.seq = seq;
    ie.dir = dir;
    ie.file_offset = frame_off;
    ie.len = static_cast<std::uint32_t>(frame_span.size());
    auto& idx = (dir == direction_t::inbound) ? impl_->inbound_index : impl_->outbound_index;
    idx.push_back(ie);

    // Set in-memory counters to the pre-computed post-increment values.
    impl_->next_inbound = ni;
    impl_->next_outbound = no;

    // Advance write_pos past both records.
    impl_->write_pos =
        counter_off + static_cast<std::int64_t>(record_disk_size(kCounterPayloadSize));

    // guard releases mutex here — CS complete.
    co_return fixpp::core::expected_t<void>{};
}

// ── FileStore::retrieve() ─────────────────────────────────────────────────────

asio::awaitable<fixpp::core::expected_t<void>> FileStore::retrieve(
    seqnum_t begin, seqnum_t end, direction_t dir,
    retrieve_visitor& visitor [[clang::lifetimebound]]) noexcept {
    // T041/US3: validate inputs before mutex acquisition.
    if (!impl_->open_ok) {
        co_return std::unexpected(fixpp::core::error::store_io_failure);
    }
    if (begin == 0) {
        co_return std::unexpected(fixpp::core::error::store_seqnum_invalid);
    }
    if (end != 0 && end < begin) {
        co_return std::unexpected(fixpp::core::error::store_invalid_range);
    }

    // T041/US3: acquire mutex to snapshot the index, release BEFORE all
    // visitor.on_frame co_awaits (I-03 / FR-017). The per-frame file-read
    // happens OUTSIDE the mutex so visitor can issue concurrent store() calls.
    //
    // N4: snap is std::pmr::vector<IndexEntry> bound to cfg.store_resource so
    // the index snapshot does NOT allocate from the global heap. retrieve()
    // releases the writer mutex before disk reads, so concurrent store() calls
    // use store_scratch_ while retrieve() uses retrieve_scratch_ — no aliasing.
    auto* snap_mr =
        impl_->cfg.store_resource ? impl_->cfg.store_resource : std::pmr::get_default_resource();
    std::pmr::vector<IndexEntry> snap{std::pmr::polymorphic_allocator<IndexEntry>{snap_mr}};
    bool gap_hit = false;
    std::uint64_t g0 = 0;
    {
        auto guard_result = co_await impl_->mutex_.async_lock();
        if (!guard_result) {
            co_return std::unexpected(fixpp::core::error::store_cancelled);
        }
        auto guard = std::move(*guard_result);

        // T015: snapshot generation_ under the mutex alongside the index snapshot.
        // Any reset() that runs AFTER this point (during a visitor.on_frame()
        // suspension) will bump impl_->generation_, making g0 stale.
        g0 = impl_->generation_;

        const auto& idx =
            (dir == direction_t::inbound) ? impl_->inbound_index : impl_->outbound_index;
        const seqnum_t next_seq =
            (dir == direction_t::inbound) ? impl_->next_inbound : impl_->next_outbound;
        const seqnum_t tail_end = (end == 0) ? (next_seq == seqnum_min ? 0 : next_seq - 1) : end;

        if (begin > tail_end || tail_end < seqnum_min) {
            co_return fixpp::core::expected_t<void>{};
        }

        snap.reserve(static_cast<std::size_t>(tail_end - begin + 1));
        for (seqnum_t s = begin; s <= tail_end; ++s) {
            const std::size_t i = static_cast<std::size_t>(s - 1);
            if (i >= idx.size() || idx[i].seq != s) {
                gap_hit = true;
                break;
            }
            snap.push_back(idx[i]);
        }
        // guard releases mutex here — BEFORE any visitor co_await (I-03)
    }

    // Walk snapshotted index entries: read from disk, call visitor.
    // File reads and visitor calls happen WITHOUT holding the mutex (I-03).
    // N4: use retrieve_scratch_ (PMR-backed, reserved to max_frame_bytes at
    // open_log()) instead of a per-call local std::vector<std::byte>.
    //
    // T015: retrieve()'s pread stays on the session strand (Clarifications
    // 2026-06-13). The three inert asio::post() hops (to file_io_executor and
    // back) are removed. Reading impl_->generation_ outside the mutex is safe
    // because generation_ is mutated only on the strand (by reset()) and
    // retrieve()'s pread also runs on the strand — no concurrent access.
    for (const auto& ie : snap) {
        // T015: generation re-check — MUST precede the pread with no co_await
        // between them (data-model §4 / I-03). If reset() ran during the prior
        // visitor.on_frame() suspension it bumped generation_; detect that here
        // and return store_io_failure (never read against the swapped/truncated
        // log). Distinct from store_seqnum_gap (data-model §5).
        if (impl_->generation_ != g0) {
            co_return std::unexpected(fixpp::core::error::store_io_failure);
        }
        // --- NO co_await between the re-check above and the pread below ---
        // Count pread attempts (test-discrimination state for the mid-walk-reset
        // witness). Unconditional: the library TU is compiled WITHOUT FIXPP_TEST_HOOKS,
        // so a gated increment would be invisible to the FIXPP_TEST_HOOKS test targets
        // that link this library (the witness reads it via read_and_reset_retrieve_pread_count).
        // Same always-compiled runtime-hook precedent as g_store_offload_probe and
        // system_clock_source::inflight_count(); a relaxed atomic add on the rare resend
        // read path (NOT the in-memory hot path) is negligible. [gate-b/r1 D reverted —
        // gating broke the witness per triage's stated fallback]
        g_retrieve_pread_count.fetch_add(1, std::memory_order_relaxed);
        if (!impl_->read_frame_payload(ie, impl_->retrieve_scratch_)) {
            co_return std::unexpected(fixpp::core::error::store_io_failure);
        }

        fixpp::core::expected_t<visit_result> vr{visit_result::cont};
        try {
            vr = co_await visitor.on_frame(ie.seq,
                                           std::span<const std::byte>(impl_->retrieve_scratch_));
        } catch (...) {
            co_return std::unexpected(fixpp::core::error::store_visitor_aborted);
        }

        if (!vr) {
            co_return std::unexpected(vr.error());
        }
        switch (*vr) {
            case visit_result::cont:
                continue;
            case visit_result::stop:
                co_return fixpp::core::expected_t<void>{};
            case visit_result::abort:
                co_return std::unexpected(visitor.abort_error());
        }
    }

    if (gap_hit) {
        co_return std::unexpected(fixpp::core::error::store_seqnum_gap);
    }

    co_return fixpp::core::expected_t<void>{};
}

// ── FileStore::next_seqnum() ──────────────────────────────────────────────────

asio::awaitable<fixpp::core::expected_t<seqnum_t>> FileStore::next_seqnum(direction_t dir,
                                                                          bool increment) noexcept {
    // Capture the session executor for the leading pump-break post below (035: no
    // offload rebind hop remains — nested co_spawn resumes on this executor).
    const auto session_ex = co_await asio::this_coro::executor;

    // T041/US3: leading post to break recursive pump() chain.
    co_await asio::post(session_ex, asio::use_awaitable);

    if (!impl_->open_ok) {
        co_return std::unexpected(fixpp::core::error::store_io_failure);
    }

    // T041/US3: acquire writer mutex (FR-015 / I-01).
    auto guard_result = co_await impl_->mutex_.async_lock();
    if (!guard_result) {
        co_return std::unexpected(fixpp::core::error::store_cancelled);
    }
    auto guard = std::move(*guard_result);

    seqnum_t& counter = (dir == direction_t::inbound) ? impl_->next_inbound : impl_->next_outbound;
    const seqnum_t current = counter;
    if (increment) {
        // T041: overflow check BEFORE writing counter record (FR-022 / I-18).
        // Overflow is session-fatal; store does NOT reset autonomously.
        if (current == seqnum_max) {
            co_return std::unexpected(fixpp::core::error::store_seqnum_overflow);
        }

        // ── Region 1: STRAND — advance counter + prepare syscall args (mutex held) ──
        //
        // Counter advances BEFORE the offload hop, matching today's semantics.
        // On I/O failure the counter is NOT rolled back (intentional, per brief §T010).
        ++counter;

        // Build counter record header + payload from the NEW counter values.
        const seqnum_t ni = impl_->next_inbound;
        const seqnum_t no = impl_->next_outbound;

        CounterPayload counter_pl{};
        counter_pl.next_inbound = ni;
        counter_pl.next_outbound = no;
        RecordHeader counter_hdr{};
        counter_hdr.kind = static_cast<std::uint8_t>(RecordKind::counter);
        counter_hdr.dir = 0xFF;
        counter_hdr.seq = 0;
        counter_hdr.len = static_cast<std::uint32_t>(kCounterPayloadSize);
        counter_hdr.crc32 =
            compute_record_crc32(counter_hdr, reinterpret_cast<const std::uint8_t*>(&counter_pl),
                                 static_cast<std::uint32_t>(kCounterPayloadSize));

        const std::int64_t counter_off = impl_->write_pos;
        const auto raw_fd = impl_->file.fd_value();

        // Snapshot the probe pointer on the strand.
        const auto probe_fn = g_store_offload_probe.load(std::memory_order_relaxed);

        // ── Region 2: POOL — raw syscalls in the offloaded lambda ─────────────────
        //
        // T012 — unconditional operation_aborted→durable catch (FR-004 / C3).
        // Same rationale as store(): any operation_aborted at this await post-dates
        // linearisation; treat as durable success.
        // [[feedback_async_mutex_us3_asio_cancel_and_subagent_seams]]
        bool io_ok = false;
        try {
            io_ok = co_await offload_to(
                impl_->cfg.file_io_executor,
                [raw_fd, counter_hdr, counter_pl, counter_off, probe_fn]() -> bool {
                    if (probe_fn) {
                        probe_fn(std::this_thread::get_id());
                    }
                    // pwrite counter header.
                    if (!raw_pwrite_all(raw_fd, &counter_hdr, kHeaderSize,
                                        static_cast<off_t>(counter_off))) {
                        return false;
                    }
                    // pwrite counter payload.
                    if (!raw_pwrite_all(raw_fd, &counter_pl, kCounterPayloadSize,
                                        static_cast<off_t>(counter_off + static_cast<std::int64_t>(
                                                                             kHeaderSize)))) {
                        return false;
                    }
                    // datasync: linearisation point (counter-record pwrite + datasync).
                    return raw_datasync(raw_fd);
                });
        } catch (const asio::system_error& e) {
            if (e.code() != asio::error::operation_aborted) {
                throw;  // Only operation_aborted is handled; anything else propagates.
            }
            g_catch_fired.fetch_add(1, std::memory_order_relaxed);
            io_ok = true;  // Post-dates linearisation: durable success.
        }

        // ── Region 3: STRAND — advance write_pos on success (mutex still held) ──
        if (!io_ok) {
            co_return std::unexpected(fixpp::core::error::store_io_failure);
        }
        impl_->write_pos += static_cast<std::int64_t>(record_disk_size(kCounterPayloadSize));
    }
    // guard releases mutex here.
    co_return fixpp::core::expected_t<seqnum_t>{current};
}

// ── FileStore::reset() ────────────────────────────────────────────────────────

asio::awaitable<fixpp::core::expected_t<void>> FileStore::reset() noexcept {
    // Capture the session executor for the leading pump-break post below (035: no
    // offload rebind hop remains — nested co_spawn resumes on this executor).
    const auto session_ex = co_await asio::this_coro::executor;

    // T041/US3: leading post to break recursive pump() chain.
    co_await asio::post(session_ex, asio::use_awaitable);

    if (!impl_->open_ok) {
        co_return std::unexpected(fixpp::core::error::store_io_failure);
    }

    // T041/US3: acquire writer mutex (FR-015 / I-01).
    auto guard_result = co_await impl_->mutex_.async_lock();
    if (!guard_result) {
        co_return std::unexpected(fixpp::core::error::store_cancelled);
    }
    auto guard = std::move(*guard_result);

    // T042/US3: atomic-rename reset (FR-010 / I-15 / SC-003).
    // Algorithm:
    //   1. Write fresh sentinel + counter to <live>.log.reset.tmp
    //   2. fdatasync the tmp file (Linux; FlushFileBuffers on Windows)
    //   3. rename(tmp, live)  → atomic replace
    //   4. Linux: parent-dir fsync MANDATORY (I-15)
    //   5. Re-open live log (clear index + re-read sentinel)
    //
    // On any failure the live log is the source of truth; the tmp is left
    // or was never written. restart_scan() at next open unlinks stale .reset.tmp.
    //
    // 035 T010: ALL disk I/O runs in the offloaded lambda; ALL impl_ mutation
    // stays on the strand (before/after the co_await). The lambda captures only
    // copies of the needed POD/string values — no impl_ reference. (data-model §3)

    // ── Region 1: STRAND — capture values for the lambda (mutex held) ─────────
    const std::string path = impl_->log_path_;
    const std::uint32_t hash = impl_->expected_hash;
    const auto probe_fn = g_store_offload_probe.load(std::memory_order_relaxed);

    // ── Region 2: POOL — entire atomic-rename sequence in the offloaded lambda ──
    //
    // The lambda publishes its result via two shared slots that survive even if
    // operation_aborted is thrown at the outer co_await (C3 / gate-b/r1 A.1+A.2):
    //
    //   result_file  — shared_ptr<OsFile>. The lambda writes the opened+locked
    //                  OsFile here BEFORE returning. On the normal (non-aborted)
    //                  path, Region 3 moves from this slot. On operation_aborted,
    //                  the catch reads it: if result_file->valid() the lambda
    //                  completed durably and the catch commits (A.2).
    //   rename_done  — shared_ptr<bool>. Set to true AFTER rename+dir-fsync succeed
    //                  but BEFORE the reopen attempt. Region 3 reads this to
    //                  distinguish pre-rename failure (old fd valid, keep it) from
    //                  post-rename failure (old fd now stale → poison) (A.1).
    //
    // The lambda returns bool: true=fully succeeded, false=any failure. This avoids
    // double-move of the OsFile: the result slot holds the sole owned copy throughout.
    //
    // Thread-safety: both shared slots are written ONLY by the pool lambda and read
    // ONLY on the strand after the co_await resumes — no concurrent access.
    //
    // T012 — unconditional operation_aborted→durable catch (FR-004 / C3).
    // [[feedback_async_mutex_us3_asio_cancel_and_subagent_seams]]
    auto result_file = std::make_shared<OsFile>();
    auto rename_done = std::make_shared<bool>(false);

    bool reset_ok = false;
    try {
        reset_ok = co_await offload_to(
            impl_->cfg.file_io_executor,
            [path, hash, probe_fn, result_file, rename_done]() -> bool {
                if (probe_fn) {
                    probe_fn(std::this_thread::get_id());
                }

#ifndef _WIN32
                // ── Linux atomic-rename path ─────────────────────────────────────
                const std::string tmp_path = path + ".reset.tmp";

                // Open tmp file (O_RDWR | O_CREAT | O_TRUNC)
                OsFile tmp_file;
                if (!tmp_file.open_wronly_creat(tmp_path.c_str())) {
                    return false;
                }

                // Write sentinel + initial counter to tmp file using a local tmp_impl.
                {
                    FileStoreImpl tmp_impl;
                    tmp_impl.expected_hash = hash;
                    tmp_impl.file = std::move(tmp_file);
                    tmp_impl.write_pos = 0;
                    tmp_impl.next_inbound = seqnum_min;
                    tmp_impl.next_outbound = seqnum_min;

                    if (!tmp_impl.initialise_fresh()) {
                        // Move file back so it closes properly, then unlink tmp.
                        tmp_file = std::move(tmp_impl.file);
                        ::unlink(tmp_path.c_str());
                        return false;
                    }
                    // tmp_file.datasync() is called inside initialise_fresh()
                    tmp_file = std::move(tmp_impl.file);
                }

                // Close tmp file before rename (required on some POSIX implementations)
                tmp_file = OsFile{};  // destructs: close()

                // Atomic rename: tmp → live log (POSIX rename is atomic per POSIX.1-2008)
                if (::rename(tmp_path.c_str(), path.c_str()) != 0) {
                    ::unlink(tmp_path.c_str());
                    return false;
                }

                // Linux: parent-dir fsync MANDATORY per I-15 / [2e §6.3.5].
                {
                    const std::filesystem::path log_fs_path{path};
                    const auto dir_fs_path = log_fs_path.parent_path();
                    const std::string dir_path =
                        dir_fs_path.empty() ? std::string{"."} : dir_fs_path.string();
                    const int dir_fd = ::open(dir_path.c_str(), O_RDONLY | O_DIRECTORY);
                    if (dir_fd < 0) {
                        return false;
                    }
                    const int fsync_rc = ::fsync(dir_fd);
                    ::close(dir_fd);
                    if (fsync_rc != 0) {
                        return false;
                    }
                }

                // Rename + dir-fsync committed the fresh log. Mark this BEFORE the reopen
                // so Region 3 / catch can distinguish post-rename failure (old fd stale)
                // from pre-rename failure (old fd valid). [gate-b/r1 A.1/A.2]
                *rename_done = true;

                // Re-open the live log (replaced by rename; advisory lock must be re-taken).
                // If this fails: post-rename failure → caller will poison the store (A.1).
                // gate-b/r1 A.1 fault-injection: if the hook is installed and returns false,
                // simulate a reopen failure post-rename to exercise the poison path.
                if (auto* h = g_post_rename_reopen_fail_hook.exchange(nullptr,
                                                                      std::memory_order_relaxed)) {
                    if (!h()) {
                        return false;  // forced post-rename reopen failure → poison
                    }
                }
                OsFile new_file;
                if (!new_file.open(path.c_str())) {
                    return false;
                }
                if (!new_file.try_lock()) {
                    return false;
                }
                // Publish to result slot; the sole owner stays here until Region 3 moves it.
                // (On operation_aborted, catch reads result_file->valid() to confirm durable
                // completion before committing.) [gate-b/r1 A.2]
                *result_file = std::move(new_file);
                return true;

#else
                // ── Windows atomic-rename path ────────────────────────────────────
                const std::string tmp_path = path + ".reset.tmp";
                std::wstring wide_tmp(tmp_path.begin(), tmp_path.end());
                std::wstring wide_live(path.begin(), path.end());

                OsFile tmp_file;
                if (!tmp_file.open_wronly_creat(wide_tmp.c_str())) {
                    return false;
                }
                {
                    FileStoreImpl tmp_impl;
                    tmp_impl.expected_hash = hash;
                    tmp_impl.file = std::move(tmp_file);
                    tmp_impl.write_pos = 0;
                    tmp_impl.next_inbound = seqnum_min;
                    tmp_impl.next_outbound = seqnum_min;
                    if (!tmp_impl.initialise_fresh()) {
                        tmp_file = std::move(tmp_impl.file);
                        DeleteFileW(wide_tmp.c_str());
                        return false;
                    }
                    tmp_file = std::move(tmp_impl.file);
                }
                tmp_file = OsFile{};  // close tmp
                // Windows: MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH MANDATORY (I-15 /
                // RC#1)
                if (!MoveFileExW(wide_tmp.c_str(), wide_live.c_str(),
                                 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                    DeleteFileW(wide_tmp.c_str());
                    return false;
                }
                // Rename succeeded — mark before reopen. [gate-b/r1 A.1/A.2]
                *rename_done = true;
                OsFile new_file;
                if (!new_file.open(wide_live.c_str())) {
                    return false;
                }
                if (!new_file.try_lock()) {
                    return false;
                }
                *result_file = std::move(new_file);
                return true;
#endif
            });
        // gate-b/r1 A.2 fault-injection: if g_force_abort_after_reset_lambda is set,
        // simulate operation_aborted arriving at the outer resume after the lambda
        // completed (co_spawn's terminal-only filter normally prevents this; the seam
        // makes the catch body reachable for witness testing). The flag is consumed
        // once (exchange to false) so subsequent resets are unaffected.
        if (g_force_abort_after_reset_lambda.exchange(false, std::memory_order_relaxed)) {
            throw asio::system_error(asio::error::operation_aborted);
        }
    } catch (const asio::system_error& e) {
        if (e.code() != asio::error::operation_aborted) {
            throw;  // Unexpected (OOM, etc.) — propagate.
        }
        // operation_aborted: post-dates linearisation per C3 (§6.1.4 / contracts C3).
        // The lambda may have completed durably (rename + reopen + lock all succeeded)
        // before the exception replaced the return path. Recover via result_file slot:
        //   • result_file->valid(): lambda completed fully → commit exactly as Region 3
        //     would (A.2). This honours C3: a durable reset MUST return success, never
        //     store_io_failure or store_cancelled.
        //   • !result_file->valid(): lambda returned false (pre- or post-rename failure)
        //     AND was also aborted → fall through to Region 3 (poison/fail-closed).
        // [gate-b/r1 A.2] [[feedback_async_mutex_us3_asio_cancel_and_subagent_seams]]
        g_catch_fired.fetch_add(1, std::memory_order_relaxed);
        if (result_file->valid()) {
            // Lambda completed durably; commit the result exactly as Region 3 would.
            impl_->file = std::move(*result_file);
            ++impl_->generation_;
            impl_->inbound_index.clear();
            impl_->outbound_index.clear();
            impl_->write_pos = static_cast<std::int64_t>(record_disk_size(kSentinelPayloadSize) +
                                                         record_disk_size(kCounterPayloadSize));
            impl_->next_inbound = seqnum_min;
            impl_->next_outbound = seqnum_min;
            co_return fixpp::core::expected_t<void>{};  // durable success (C3)
        }
        // Lambda did not produce a valid file; fall through to Region 3.
        // reset_ok remains false; rename_done indicates which poison branch applies.
    }

    // ── Region 3: STRAND — apply mutations on success (mutex still held) ──────
    //
    // On pre-rename failure (!rename_done): impl_->file unchanged — live log is
    //   still source of truth.
    // On post-rename failure (*rename_done && !reset_ok): the live pathname now names
    //   the fresh reset log but impl_->file is the stale (now-unlinked) previous inode.
    //   Subsequent stores on that inode would vanish on restart → POISON the store so
    //   all further ops (store/next_seqnum/retrieve/reset) fail-closed until restart.
    //   [gate-b/r1 A.1] [L-035-2] (behaviors-and-limitations.md updated)
    // On success: swap in the newly opened file, bump generation_, then reset
    //   index + counters. The generation bump (T015) MUST come immediately after
    //   the file swap so any retrieve() walk suspended during the offload detects
    //   the stale snapshot on its next iteration (data-model §4 / I-03 / FR-006).
    if (!reset_ok) {
        if (*rename_done) {
            // Post-rename reopen/lock failure: old fd names a dead inode (unlinked by
            // rename); poison the store so no write reaches it. A later reset() will
            // poison-check open_ok and return store_io_failure immediately.
            // The only recovery is to restart the process and let the factory re-open
            // the now-fresh live log. [gate-b/r1 A.1] [L-035-2]
            impl_->file = OsFile{};  // close/release the stale (unlinked) fd
            impl_->open_ok = false;  // fail-closed: all further ops return store_io_failure
        }
        co_return std::unexpected(fixpp::core::error::store_io_failure);
    }
    impl_->file = std::move(*result_file);
    // T015: bump epoch — any in-progress retrieve() walk sees g0 != generation_
    // on the next per-frame re-check and returns store_io_failure (clean-fail).
    // Mutated on the strand (here, mutex held); no atomic needed (Decision 3).
    ++impl_->generation_;

    // Reset in-memory state (both directions, both counters).
    // write_pos must reflect the on-disk tail after initialise_fresh(): the new
    // file already contains a sentinel record + an initial counter record written
    // by initialise_fresh() inside tmp_impl. Setting write_pos = 0 would cause
    // the next store() to overwrite the sentinel at byte 0 — RC#2 fix.
    impl_->inbound_index.clear();
    impl_->outbound_index.clear();
    impl_->write_pos = static_cast<std::int64_t>(record_disk_size(kSentinelPayloadSize) +
                                                 record_disk_size(kCounterPayloadSize));
    impl_->next_inbound = seqnum_min;
    impl_->next_outbound = seqnum_min;

    // guard releases mutex here.
    co_return fixpp::core::expected_t<void>{};
}

// ── FileStore::flush_for_session_close() ─────────────────────────────────────
//
// T032 (US2 / FR-028 / I-17 / Appendix D §D.2):
// Drains any pending commit_batched or commit_interval records to disk by
// issuing fdatasync (Linux) / FlushFileBuffers (Windows). This ensures all
// pwritten frame and counter records are durable before the session closes,
// regardless of the configured flush policy.
//
// Returns expected_t<void>{} on success, store_io_failure on I/O error.
// Does NOT surface store_cancelled under graceful close (FR-028 / I-17).
//
// Engine-internal: dispatched via the A1 typed thunk (flush_thunk_for<FileStore>())
// stashed by the Session at open() and called at Session::close(graceful).
// NOT invoked under Session::close(terminal) per Appendix D §D.2.
//
// Hook MUST run to completion outside phase-1's child timeout (no timeout
// applies to this call; the session's phase-2 root cancel fires AFTER this
// returns, not during it — Appendix D §D.2 ordering).
asio::awaitable<fixpp::core::expected_t<void>> FileStore::flush_for_session_close() noexcept {
    // If the store was never successfully opened, return success (nothing to drain).
    if (!impl_->open_ok) {
        co_return fixpp::core::expected_t<void>{};
    }

    // Drain all pending kernel-buffered writes to disk (Linux: fdatasync;
    // Windows: FlushFileBuffers). This covers:
    //   - commit_batched: frames pwritten since last batch boundary fdatasync;
    //   - commit_interval: frames pwritten since last timer-driven fdatasync;
    //   - commit_per_message: no-op (already fsynced per frame), but harmless.
    // store_cancelled is NOT surfaced: this call runs outside any cancellation
    // scope (contracts C1 flush-Pre / C3 carve-out). Only store_io_failure on error.
    //
    // 035 T011: offload the blocking datasync to file_io_executor.
    // No writer mutex (per C3 carve-out), no leading post, not cancellable.
    const auto raw_fd = impl_->file.fd_value();
    const auto probe_fn = g_store_offload_probe.load(std::memory_order_relaxed);

    const bool io_ok =
        co_await offload_to(impl_->cfg.file_io_executor, [raw_fd, probe_fn]() -> bool {
            if (probe_fn) {
                probe_fn(std::this_thread::get_id());
            }
            const bool ok = raw_datasync(raw_fd);
            // gate-b/r2 R#1a: increment the flush-ran witness counter AFTER
            // raw_datasync returns true (fdatasync actually executed and succeeded).
            // Unconditional: the library is compiled without FIXPP_TEST_HOOKS so a
            // gated increment would be invisible to tests that link the library.
            // Cold path (graceful close only) — no §XV.1/§VIII.5 hot-path concern.
            if (ok) {
                g_flush_datasync_count.fetch_add(1, std::memory_order_relaxed);
            }
            return ok;
        });

    if (!io_ok) {
        co_return std::unexpected(fixpp::core::error::store_io_failure);
    }

    co_return fixpp::core::expected_t<void>{};
}

// ── FileStore::open_log() — internal open called from FileStoreFactory::make() ─

bool FileStore::open_log(const std::string& log_path) noexcept {
    if (!impl_->file.open(log_path.c_str())) return false;
    if (!impl_->file.try_lock()) return false;

    // RC#6 + N4: initialise PMR scratch buffers now that cfg is fully populated.
    // Uses cfg.store_resource if non-null, else the PMR default resource.
    // reserve() ensures resize()/assign() never reallocates for frames ≤
    // max_frame_bytes (the frame-size guard in store() enforces this invariant).
    //
    // Two separate buffers (store_scratch_ and retrieve_scratch_) because retrieve()
    // releases the writer mutex before disk reads, enabling concurrent store() calls
    // from within the visitor body. Both paths must have their own scratch.
    {
        auto* mr = impl_->cfg.store_resource ? impl_->cfg.store_resource
                                             : std::pmr::get_default_resource();
        impl_->store_scratch_ =
            std::pmr::vector<std::byte>{std::pmr::polymorphic_allocator<std::byte>{mr}};
        impl_->store_scratch_.reserve(impl_->cfg.max_frame_bytes);

        impl_->retrieve_scratch_ =
            std::pmr::vector<std::byte>{std::pmr::polymorphic_allocator<std::byte>{mr}};
        impl_->retrieve_scratch_.reserve(impl_->cfg.max_frame_bytes);
    }

    const bool ok = impl_->restart_scan(log_path);
    if (ok) {
        impl_->open_ok = true;
        impl_->log_path_ = log_path;  // T042: needed by reset() for atomic-rename
    }
    return ok;
}

}  // namespace fixpp::session
