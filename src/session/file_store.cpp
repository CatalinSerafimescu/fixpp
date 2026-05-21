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
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fixpp/session/file_store.hpp>
#include <memory>
#include <memory_resource>
#include <span>
#include <vector>

// Linux / POSIX headers (Tier-1)
#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

// Windows headers (Tier-2 — compilation only on Linux Tier-1; runtime is T057)
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <crc32c/crc32c.h>

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/sync/async_mutex.hpp>  // T041: per-instance writer mutex
#include <fixpp/session/direction.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>

namespace fixpp::session {

// ── Constants ─────────────────────────────────────────────────────────────────

static constexpr std::uint64_t kSentinelMagic = 0x46495850535354ULL;  // "FIXPPST\0"
static constexpr std::uint8_t kFormatVersion = 1;
static constexpr std::size_t kHeaderSize = 16;  // 16-byte record header
static constexpr std::size_t kSentinelPayloadSize = 32;
static constexpr std::size_t kCounterPayloadSize = 8;
static constexpr std::size_t kAlignment = 8;

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
    std::uint32_t h = 2166136261u;
    for (unsigned char c : s) {
        h ^= c;
        h *= 16777619u;
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
    [[nodiscard]] bool try_lock() noexcept { return ::flock(fd, LOCK_EX | LOCK_NB) == 0; }

    // Release advisory lock
    void unlock() noexcept {
        if (fd >= 0) ::flock(fd, LOCK_UN);
    }

    // pwrite: write at explicit offset (no file-position side-effect)
    [[nodiscard]] bool pwrite_all(const void* buf, std::size_t n, off_t offset) noexcept {
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
    [[nodiscard]] ssize_t pread_all(void* buf, std::size_t n, off_t offset) noexcept {
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
    [[nodiscard]] bool datasync() noexcept { return ::fdatasync(fd) == 0; }

    // Truncate to given size
    [[nodiscard]] bool truncate(off_t new_size) noexcept { return ::ftruncate(fd, new_size) == 0; }

    // Get current file size
    [[nodiscard]] off_t file_size() noexcept {
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

    // Per-direction frame index (rebuilt during open/restart scan).
    // entries are in insertion order (seq ascending, starting from 1).
    std::vector<IndexEntry> inbound_index;
    std::vector<IndexEntry> outbound_index;

    // ── Sentinel write ─────────────────────────────────────────────────────

    bool write_sentinel(std::int64_t offset) noexcept {
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

    bool write_counter(std::int64_t offset, seqnum_t ni, seqnum_t no) noexcept {
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
    // Returns false on I/O error.
    bool read_frame_payload(const IndexEntry& ie, std::vector<std::byte>& dst) noexcept {
        dst.resize(ie.len);
        const std::int64_t payload_offset = ie.file_offset + static_cast<std::int64_t>(kHeaderSize);
        auto n = file.pread_all(dst.data(), ie.len, payload_offset);
        return n == static_cast<decltype(n)>(ie.len);
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
            if (n != static_cast<decltype(n)>(kHeaderSize)) return false;

            if (hdr.kind != static_cast<std::uint8_t>(RecordKind::sentinel)) {
                return false;  // First record must be sentinel
            }
            if (hdr.len != kSentinelPayloadSize) return false;

            SentinelPayload pl{};
            n = file.pread_all(&pl, kSentinelPayloadSize, static_cast<std::int64_t>(kHeaderSize));
            if (n != static_cast<decltype(n)>(kSentinelPayloadSize)) return false;

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
            if (remaining < static_cast<std::int64_t>(kHeaderSize)) {
                // Partial header at tail → truncate here
                break;
            }
            auto n = file.pread_all(&hdr, kHeaderSize, scan_pos);
            if (n != static_cast<decltype(n)>(kHeaderSize)) break;

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
                if (n != static_cast<decltype(n)>(payload_len)) break;
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

FileStore::~FileStore() noexcept {
    // Advisory lock is released when OsFile destructs (close() releases flock)
}

// ── FileStore::store() ────────────────────────────────────────────────────────

asio::awaitable<fixpp::core::expected_t<void>> FileStore::store(seqnum_t seq,
                                                                std::span<const std::byte> frame
                                                                [[clang::lifetimebound]],
                                                                direction_t dir) noexcept {
    // RC#4: capture session executor BEFORE any hop so we can rebind after I/O.
    // this_coro::executor reflects the executor the caller used for co_spawn;
    // after the file_io_executor hop below, this_coro::executor returns the
    // I/O executor — NOT the original session executor. Capturing now preserves
    // the correct return destination per [2d §4.5] D.1 strand contract.
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

    // Deep-copy frame bytes into the store (pwrite to file).
    // T041: post I/O work to file_io_executor. Mutex is held throughout
    // (the post does NOT release the mutex — we remain in CS).
    {
        // RC#6: use PMR-backed store_scratch_ instead of per-call global-heap alloc.
        // store_scratch_ is reserved at ctor with cfg.max_frame_bytes capacity, so
        // assign() does not reallocate as long as frame.size() <= max_frame_bytes
        // (checked above). Zero global operator new/delete calls on the hot path.
        impl_->store_scratch_.assign(frame.begin(), frame.end());
        const auto policy_kind = impl_->cfg.policy.which;

        // Post the actual pwrite + datasync to file_io_executor (I-13).
        co_await asio::post(impl_->cfg.file_io_executor, asio::use_awaitable);

        // Execute I/O on file_io_executor thread while still holding mutex.
        if (!impl_->write_frame(seq, dir, std::span<const std::byte>(impl_->store_scratch_))) {
            // RC#4: rebind back to session executor before co_return so the caller
            // continuation runs on the correct strand.
            co_await asio::post(session_ex, asio::use_awaitable);
            co_return std::unexpected(fixpp::core::error::store_io_failure);
        }

        // Advance counter
        if (dir == direction_t::inbound) {
            ++impl_->next_inbound;
        } else {
            ++impl_->next_outbound;
        }

        // Write counter record before datasync.
        if (!impl_->write_counter(impl_->write_pos, impl_->next_inbound, impl_->next_outbound)) {
            co_await asio::post(session_ex, asio::use_awaitable);
            co_return std::unexpected(fixpp::core::error::store_io_failure);
        }
        impl_->write_pos += static_cast<std::int64_t>(record_disk_size(kCounterPayloadSize));

        // Flush based on policy. Linearisation point for store() is here (I-06).
        if (policy_kind == FileStorePolicy::kind::commit_per_message) {
            if (!impl_->file.datasync()) {
                co_await asio::post(session_ex, asio::use_awaitable);
                co_return std::unexpected(fixpp::core::error::store_io_failure);
            }
        } else if (policy_kind == FileStorePolicy::kind::commit_batched) {
            const std::size_t total = impl_->inbound_index.size() + impl_->outbound_index.size();
            const std::size_t bs = impl_->cfg.policy.batch_size;
            if (bs > 0 && (total % bs) == 0) {
                if (!impl_->file.datasync()) {
                    co_await asio::post(session_ex, asio::use_awaitable);
                    co_return std::unexpected(fixpp::core::error::store_io_failure);
                }
            }
        }
        // commit_interval: periodic flush (timer/co_spawn, deferred US4).

        // RC#4: hop back to the session executor before releasing the mutex and
        // returning. Visitor callbacks and the awaitable completion now run on
        // the caller's strand, satisfying [2d §4.5] D.1.
        co_await asio::post(session_ex, asio::use_awaitable);
    }
    // guard releases mutex here — CS complete.

    co_return fixpp::core::expected_t<void>{};
}

// ── FileStore::retrieve() ─────────────────────────────────────────────────────

asio::awaitable<fixpp::core::expected_t<void>> FileStore::retrieve(
    seqnum_t begin, seqnum_t end, direction_t dir,
    retrieve_visitor& visitor [[clang::lifetimebound]]) noexcept {
    // RC#4: capture session executor before any hop (same rationale as store()).
    const auto session_ex = co_await asio::this_coro::executor;

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
    std::vector<IndexEntry> snap;
    bool gap_hit = false;
    {
        auto guard_result = co_await impl_->mutex_.async_lock();
        if (!guard_result) {
            co_return std::unexpected(fixpp::core::error::store_cancelled);
        }
        auto guard = std::move(*guard_result);

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
    std::vector<std::byte> frame_buf;
    for (const auto& ie : snap) {
        // Post to file_io_executor for the disk read (I-13 / T041).
        co_await asio::post(impl_->cfg.file_io_executor, asio::use_awaitable);
        if (!impl_->read_frame_payload(ie, frame_buf)) {
            // RC#4: rebind before returning so the caller continuation runs on the
            // session strand, not the file-I/O executor.
            co_await asio::post(session_ex, asio::use_awaitable);
            co_return std::unexpected(fixpp::core::error::store_io_failure);
        }
        // RC#4: return to the session executor using the pre-captured session_ex.
        // The original code used `co_await this_coro::executor` here which, after
        // the file_io_executor hop above, returns file_io_executor — a no-op hop
        // that leaves visitor callbacks on the wrong strand ([2d §4.5] D.1 bug).
        co_await asio::post(session_ex, asio::use_awaitable);

        fixpp::core::expected_t<visit_result> vr{visit_result::cont};
        try {
            vr = co_await visitor.on_frame(ie.seq, std::span<const std::byte>(frame_buf));
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
    // RC#4: capture session executor before any hop.
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
        ++counter;

        // Post I/O to file_io_executor while still holding mutex.
        co_await asio::post(impl_->cfg.file_io_executor, asio::use_awaitable);

        // Write counter record to disk. Linearisation point: counter-record pwrite.
        if (!impl_->write_counter(impl_->write_pos, impl_->next_inbound, impl_->next_outbound)) {
            // RC#4: rebind before returning.
            co_await asio::post(session_ex, asio::use_awaitable);
            co_return std::unexpected(fixpp::core::error::store_io_failure);
        }
        impl_->write_pos += static_cast<std::int64_t>(record_disk_size(kCounterPayloadSize));
        if (!impl_->file.datasync()) {
            co_await asio::post(session_ex, asio::use_awaitable);
            co_return std::unexpected(fixpp::core::error::store_io_failure);
        }

        // RC#4: hop back to session executor before releasing mutex and returning.
        co_await asio::post(session_ex, asio::use_awaitable);
    }
    // guard releases mutex here.
    co_return fixpp::core::expected_t<seqnum_t>{current};
}

// ── FileStore::reset() ────────────────────────────────────────────────────────

asio::awaitable<fixpp::core::expected_t<void>> FileStore::reset() noexcept {
    // RC#4: capture session executor before any hop.
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

    // Post I/O to file_io_executor.
    co_await asio::post(impl_->cfg.file_io_executor, asio::use_awaitable);

#ifndef _WIN32
    // ── Linux atomic-rename path ─────────────────────────────────────────────
    const std::string tmp_path = impl_->log_path_ + ".reset.tmp";

    // Open tmp file (O_WRONLY | O_CREAT | O_TRUNC)
    OsFile tmp_file;
    if (!tmp_file.open_wronly_creat(tmp_path.c_str())) {
        co_await asio::post(session_ex, asio::use_awaitable);  // RC#4
        co_return std::unexpected(fixpp::core::error::store_io_failure);
    }

    // Write sentinel + initial counter to tmp file using a temporary impl
    // (borrow the expected_hash from the main impl).
    {
        FileStoreImpl tmp_impl;
        tmp_impl.cfg = impl_->cfg;
        tmp_impl.expected_hash = impl_->expected_hash;
        tmp_impl.file = std::move(tmp_file);
        tmp_impl.write_pos = 0;
        tmp_impl.next_inbound = seqnum_min;
        tmp_impl.next_outbound = seqnum_min;

        if (!tmp_impl.initialise_fresh()) {
            // Move file back so it closes properly
            tmp_file = std::move(tmp_impl.file);
            // Unlink tmp on failure
            ::unlink(tmp_path.c_str());
            co_await asio::post(session_ex, asio::use_awaitable);  // RC#4
            co_return std::unexpected(fixpp::core::error::store_io_failure);
        }
        // tmp_file.datasync() is called inside initialise_fresh()
        tmp_file = std::move(tmp_impl.file);
    }

    // Close tmp file before rename (required on some POSIX implementations)
    tmp_file = OsFile{};  // destructs: close()

    // Atomic rename: tmp → live log (POSIX rename is atomic per POSIX.1-2008)
    if (::rename(tmp_path.c_str(), impl_->log_path_.c_str()) != 0) {
        ::unlink(tmp_path.c_str());
        co_await asio::post(session_ex, asio::use_awaitable);  // RC#4
        co_return std::unexpected(fixpp::core::error::store_io_failure);
    }

    // Linux: parent-dir fsync MANDATORY per I-15 / [2e §6.3.5] to seal the
    // atomic-rename durability contract. A crash after rename() but before the
    // directory inode is flushed can resurrect the pre-reset pathname on most
    // journaled filesystems. Both directory-open failure AND fsync(dir_fd)
    // failure are fatal — return store_io_failure rather than silently continuing.
    {
        const std::filesystem::path log_fs_path{impl_->log_path_};
        const auto dir_fs_path = log_fs_path.parent_path();
        const std::string dir_path = dir_fs_path.empty() ? std::string{"."} : dir_fs_path.string();
        const int dir_fd = ::open(dir_path.c_str(), O_RDONLY | O_DIRECTORY);
        if (dir_fd < 0) {
            // Cannot open parent directory — rename durability cannot be guaranteed.
            co_await asio::post(session_ex, asio::use_awaitable);  // RC#4
            co_return std::unexpected(fixpp::core::error::store_io_failure);
        }
        const int fsync_rc = ::fsync(dir_fd);
        ::close(dir_fd);
        if (fsync_rc != 0) {
            // fsync(dir_fd) failed — rename is NOT durable; report failure.
            co_await asio::post(session_ex, asio::use_awaitable);  // RC#4
            co_return std::unexpected(fixpp::core::error::store_io_failure);
        }
    }

    // Re-open the live log (it was replaced by rename; advisory lock must be re-taken)
    {
        OsFile new_file;
        if (!new_file.open(impl_->log_path_.c_str())) {
            co_await asio::post(session_ex, asio::use_awaitable);  // RC#4
            co_return std::unexpected(fixpp::core::error::store_io_failure);
        }
        if (!new_file.try_lock()) {
            co_await asio::post(session_ex, asio::use_awaitable);  // RC#4
            co_return std::unexpected(fixpp::core::error::store_io_failure);
        }
        impl_->file = std::move(new_file);
    }

#else
    // ── Windows atomic-rename path ────────────────────────────────────────────
    const std::string tmp_path = impl_->log_path_ + ".reset.tmp";
    std::wstring wide_tmp(tmp_path.begin(), tmp_path.end());
    std::wstring wide_live(impl_->log_path_.begin(), impl_->log_path_.end());
    OsFile tmp_file;
    if (!tmp_file.open_wronly_creat(wide_tmp.c_str())) {
        co_await asio::post(session_ex, asio::use_awaitable);  // RC#4
        co_return std::unexpected(fixpp::core::error::store_io_failure);
    }
    {
        FileStoreImpl tmp_impl;
        tmp_impl.cfg = impl_->cfg;
        tmp_impl.expected_hash = impl_->expected_hash;
        tmp_impl.file = std::move(tmp_file);
        tmp_impl.write_pos = 0;
        tmp_impl.next_inbound = seqnum_min;
        tmp_impl.next_outbound = seqnum_min;
        if (!tmp_impl.initialise_fresh()) {
            tmp_file = std::move(tmp_impl.file);
            DeleteFileW(wide_tmp.c_str());
            co_await asio::post(session_ex, asio::use_awaitable);  // RC#4
            co_return std::unexpected(fixpp::core::error::store_io_failure);
        }
        tmp_file = std::move(tmp_impl.file);
    }
    tmp_file = OsFile{};  // close tmp
    // Windows: MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH MANDATORY (I-15 / RC#1)
    if (!MoveFileExW(wide_tmp.c_str(), wide_live.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(wide_tmp.c_str());
        co_await asio::post(session_ex, asio::use_awaitable);  // RC#4
        co_return std::unexpected(fixpp::core::error::store_io_failure);
    }
    {
        OsFile new_file;
        if (!new_file.open(wide_live.c_str())) {
            co_await asio::post(session_ex, asio::use_awaitable);  // RC#4
            co_return std::unexpected(fixpp::core::error::store_io_failure);
        }
        if (!new_file.try_lock()) {
            co_await asio::post(session_ex, asio::use_awaitable);  // RC#4
            co_return std::unexpected(fixpp::core::error::store_io_failure);
        }
        impl_->file = std::move(new_file);
    }
#endif

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

    // RC#4: rebind to the session executor before releasing the mutex and returning.
    // All state mutations above happen on file_io_executor; completion must resume
    // on session_ex per [2d §4.5] D.1.
    co_await asio::post(session_ex, asio::use_awaitable);

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
    // store_cancelled is NOT surfaced: the call is synchronous at the OS level
    // (fdatasync blocks until durable) and runs outside any cancellation scope.
    if (!impl_->file.datasync()) {
        co_return std::unexpected(fixpp::core::error::store_io_failure);
    }

    co_return fixpp::core::expected_t<void>{};
}

// ── FileStore::open_log() — internal open called from FileStoreFactory::make() ─

bool FileStore::open_log(const std::string& log_path) noexcept {
    if (!impl_->file.open(log_path.c_str())) return false;
    if (!impl_->file.try_lock()) return false;

    // RC#6: initialise the PMR scratch buffer now that cfg is fully populated.
    // Uses cfg.store_resource if non-null, else the PMR default resource.
    // reserve() ensures assign() in store() never reallocates for frames ≤
    // max_frame_bytes (the frame-size guard in store() enforces this invariant).
    {
        auto* mr = impl_->cfg.store_resource ? impl_->cfg.store_resource
                                             : std::pmr::get_default_resource();
        impl_->store_scratch_ = std::pmr::vector<std::byte>{
            std::pmr::polymorphic_allocator<std::byte>{mr}};
        impl_->store_scratch_.reserve(impl_->cfg.max_frame_bytes);
    }

    const bool ok = impl_->restart_scan(log_path);
    if (ok) {
        impl_->open_ok = true;
        impl_->log_path_ = log_path;  // T042: needed by reset() for atomic-rename
    }
    return ok;
}

}  // namespace fixpp::session
