#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/group_read_100.hpp — TEST-ONLY support for 100-wire-group-read-integrity's group
// read cells (tasks T010 to T017; research D9).
//
//   - `fix44()`: the shipped FIX 4.4 dictionary and its `as_table_view()`, loaded once per process
//     from FIXPP_DICT_DATA_DIR (the including target defines it).
//   - `frame44(body)`: a FIX.4.4 frame around `body` (8=, a correct 9=, `body`, 10=000).
//   - `recording_resource`: forwards to an upstream resource and records every allocation request
//     (bytes, alignment, whether it succeeded), so an arena-stepping cell can name which
//     allocation of a read was the first to fail.
//   - `stepped_arena`: a fixed buffer of a given size as a `monotonic_buffer_resource` over
//     `null_memory_resource()` (the construction 073's failloud tests use), behind a
//     `recording_resource`. The cell owns the arena, so it asserts on every lane (research D9
//     "Lanes").

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/xml_loader.hpp>
#include <fixpp/wire/offset_table.hpp>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "support/wire_test_hooks.hpp"

namespace fixpp::test100 {

struct loaded_dictionary {
    fixpp::dict::Dictionary dict;
    fixpp::dict::table_view tv;
};

#ifdef FIXPP_DICT_DATA_DIR
// The shipped FIX44.xml, loaded on first use and kept for the process.
[[nodiscard]] inline loaded_dictionary const& fix44() {
    static loaded_dictionary const d = [] {
        auto dict =
            fixpp::dict::XmlLoader{}.load(std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIX44.xml",
                                          std::pmr::get_default_resource());
        auto tv = dict.as_table_view();
        return loaded_dictionary{std::move(dict), std::move(tv)};
    }();
    return d;
}
#endif

[[nodiscard]] inline std::vector<std::byte> frame44(std::string const& body) {
    std::string full = "8=FIX.4.4\x01";
    full += "9=" + std::to_string(body.size()) + "\x01";
    full += body;
    full += "10=000\x01";
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

// The session header fields the frames below carry after 8 and 9.
[[nodiscard]] inline std::string header44(std::string const& msg_type) {
    return "35=" + msg_type +
           "\x01"
           "49=S\x01"
           "56=T\x01"
           "34=2\x01"
           "52=20240101-00:00:00.000\x01";
}

class recording_resource final : public std::pmr::memory_resource {
public:
    struct event {
        std::size_t bytes;
        std::size_t align;
        bool ok;
    };

    explicit recording_resource(std::pmr::memory_resource* upstream) noexcept
        : upstream_{upstream} {}

    // Starts a new window: `first_failure()` and `events()` report only requests made after it.
    void mark() noexcept { mark_ = log_.size(); }
    [[nodiscard]] std::optional<event> first_failure() const {
        for (std::size_t i = mark_; i < log_.size(); ++i) {
            if (!log_[i].ok) {
                return log_[i];
            }
        }
        return std::nullopt;
    }
    [[nodiscard]] std::size_t requests_since_mark() const noexcept { return log_.size() - mark_; }
    // The requests made after the mark, in order (T039: which allocation a read made first).
    [[nodiscard]] std::span<event const> events_since_mark() const noexcept {
        return {log_.data() + mark_, log_.size() - mark_};
    }

private:
    void* do_allocate(std::size_t bytes, std::size_t align) override {
        try {
            void* p = upstream_->allocate(bytes, align);
            log_.push_back({bytes, align, true});
            return p;
        } catch (...) {
            log_.push_back({bytes, align, false});
            throw;
        }
    }
    void do_deallocate(void* p, std::size_t bytes, std::size_t align) override {
        upstream_->deallocate(p, bytes, align);
    }
    [[nodiscard]] bool do_is_equal(std::pmr::memory_resource const& other) const noexcept override {
        return this == &other;
    }

    std::pmr::memory_resource* upstream_;
    std::vector<event> log_;
    std::size_t mark_ = 0;
};

class stepped_arena {
public:
    explicit stepped_arena(std::size_t room)
        : buf_(room == 0 ? 1 : room),
          mono_{buf_.data(), room, std::pmr::null_memory_resource()},
          rec_{&mono_} {}
    stepped_arena(stepped_arena const&) = delete;
    stepped_arena& operator=(stepped_arena const&) = delete;

    [[nodiscard]] std::pmr::memory_resource* mr() noexcept { return &rec_; }
    [[nodiscard]] recording_resource& rec() noexcept { return rec_; }

private:
    std::vector<std::byte> buf_;
    std::pmr::monotonic_buffer_resource mono_;
    recording_resource rec_;
};

// The kinds of allocation a group read makes, named by the request's size and alignment.
enum class alloc_kind : std::uint8_t {
    none,            // no request failed
    slices,          // a group's slice array (n x group_slice)
    group_row,       // the first row of a table's group_index_
    nested_row,      // the first row of the root's nested_cache_
    subtable,        // a nested sub-OffsetTable shell
    subtable_build,  // a sub-table's own entries_ or overlay_ (alignment of an entry or slot)
    other,
};

[[nodiscard]] inline char const* alloc_kind_name(alloc_kind k) noexcept {
    switch (k) {
        case alloc_kind::none:
            return "none";
        case alloc_kind::slices:
            return "slices";
        case alloc_kind::group_row:
            return "group_index_ row";
        case alloc_kind::nested_row:
            return "nested_cache_ row";
        case alloc_kind::subtable:
            return "sub-table shell";
        case alloc_kind::subtable_build:
            return "sub-table build";
        case alloc_kind::other:
            return "other";
    }
    return "?";
}

// Classifies the first failed request of `rec`'s window. `n_slices` is the instance count of the
// read whose slice array the cell targets; `extra_size` names one more request the caller
// classifies itself (a C cursor shell), reported as `other` with `is_extra` set.
struct classified {
    alloc_kind kind = alloc_kind::none;
    bool is_extra = false;
    std::size_t bytes = 0;
};
[[nodiscard]] inline classified classify_first_failure(recording_resource const& rec,
                                                       std::size_t n_slices,
                                                       std::size_t extra_size = 0) {
    auto const f = rec.first_failure();
    if (!f) {
        return {};
    }
    classified c{alloc_kind::other, false, f->bytes};
    if (extra_size != 0 && f->bytes == extra_size) {
        c.is_extra = true;
    } else if (f->bytes == sizeof(fixpp::wire::OffsetTable) &&
               f->align == alignof(fixpp::wire::OffsetTable)) {
        c.kind = alloc_kind::subtable;
    } else if (f->align == alignof(fixpp::wire::OffsetTable::entry) &&
               f->align < alignof(fixpp::wire::group_slice)) {
        c.kind = alloc_kind::subtable_build;
    } else if (f->bytes == fixpp::wire::offset_table_test_access::nested_cache_row_size) {
        c.kind = alloc_kind::nested_row;
    } else if (f->bytes == fixpp::wire::offset_table_test_access::group_span_size) {
        c.kind = alloc_kind::group_row;
    } else if (n_slices != 0 && f->bytes == n_slices * sizeof(fixpp::wire::group_slice) &&
               f->align == alignof(fixpp::wire::group_slice)) {
        c.kind = alloc_kind::slices;
    }
    return c;
}

}  // namespace fixpp::test100
