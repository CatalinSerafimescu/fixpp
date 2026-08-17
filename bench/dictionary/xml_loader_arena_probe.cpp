// SPDX-License-Identifier: AGPL-3.0-or-later
// bench/dictionary/xml_loader_arena_probe.cpp
//
// #263 diagnostic probe (NOT a gate, NOT a benchmark).
//
// `xml_loader_bench` hands `XmlLoader::load` a `std::pmr::monotonic_buffer_resource`
// over a fixed 4 MiB stack array — the NFR-002-1 budget. A monotonic resource
// whose initial buffer is exhausted falls back to its UPSTREAM silently, so a
// load that has outgrown 4 MiB shows up only as a step in wall-clock time.
//
// This probe answers, per dictionary:
//   (a) how many bytes the load actually requests, and how many survive, and
//   (b) whether the 4 MiB arena in the shipped bench overflows.
//
// Compile definition required: FIXPP_DICT_DATA_DIR.

#include <fixpp/dict/xml_loader.hpp>

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <memory_resource>
#include <string_view>

namespace {

// Forwards to an upstream and tallies what passes through.
class counting_resource final : public std::pmr::memory_resource {
public:
    explicit counting_resource(std::pmr::memory_resource* up) noexcept : up_(up) {}

    std::size_t total_bytes{0};   // cumulative requested
    std::size_t allocations{0};   // do_allocate calls
    std::size_t peak_live{0};     // high-water mark of outstanding bytes
    std::size_t live{0};          // outstanding bytes right now

private:
    void* do_allocate(std::size_t bytes, std::size_t align) override {
        total_bytes += bytes;
        ++allocations;
        live += bytes;
        if (live > peak_live) {
            peak_live = live;
        }
        return up_->allocate(bytes, align);
    }
    void do_deallocate(void* p, std::size_t bytes, std::size_t align) override {
        live -= bytes;
        up_->deallocate(p, bytes, align);
    }
    [[nodiscard]] bool do_is_equal(std::pmr::memory_resource const& o) const noexcept override {
        return this == &o;
    }

    std::pmr::memory_resource* up_;
};

constexpr std::size_t kArena = 4u * 1024u * 1024u;  // the shipped bench's budget

void probe(std::string_view filename) {
    std::filesystem::path const path = std::filesystem::path{FIXPP_DICT_DATA_DIR} / filename;

    // (a) exact request/survivor accounting, straight to new_delete.
    counting_resource direct{std::pmr::new_delete_resource()};
    std::size_t surviving = 0;
    {
        auto d = fixpp::dict::XmlLoader{}.load(path, &direct);
        surviving = direct.live;
        (void)d;
    }

    // (b) does the shipped 4 MiB arena overflow? monotonic over a 4 MiB heap
    // block, upstream = counting -> upstream.total_bytes > 0 means overflow.
    counting_resource upstream{std::pmr::new_delete_resource()};
    auto buffer = std::make_unique<std::byte[]>(kArena);
    {
        std::pmr::monotonic_buffer_resource mr{buffer.get(), kArena, &upstream};
        auto d = fixpp::dict::XmlLoader{}.load(path, &mr);
        (void)d;
    }

    std::printf(
        "%-12s requested=%9zu B  allocs=%7zu  peak_live=%9zu B  surviving=%9zu B  |  "
        "4MiB-arena overflow: %s (upstream=%zu B in %zu allocs)\n",
        std::string{filename}.c_str(), direct.total_bytes, direct.allocations, direct.peak_live,
        surviving, upstream.total_bytes > 0 ? "YES" : "no", upstream.total_bytes,
        upstream.allocations);
}

}  // namespace

int main() {
    std::printf("#263 XmlLoader arena probe — budget = %zu B (4 MiB)\n\n", kArena);
    probe("FIX42.xml");
    probe("FIX44.xml");
    probe("FIX50SP2.xml");
    return 0;
}
