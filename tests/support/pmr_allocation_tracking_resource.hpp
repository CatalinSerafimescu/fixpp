// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/pmr_allocation_tracking_resource.hpp
//
// Seam #2 (PMR allocation accounting) per
// `specs/002-dictionary-xml-loader/data-model.md` "PMR allocation accounting"
// and `research.md` D-9. Carried as shared infra under `tests/support/` so
// future `dict::` tests can reuse it.
//
// Wraps an upstream `std::pmr::memory_resource` (typically a
// `monotonic_buffer_resource` over a stack buffer) and counts the number of
// distinct `do_allocate` / `do_deallocate` calls plus the total bytes routed
// through it. The harness ALSO counts global `operator new` invocations via a
// process-scoped sentinel so AC-P1 ("every output byte allocated from `mr`")
// and NFR-002-2 ("zero `operator new` calls during the entire `load*` call")
// can be verified with one helper.

#pragma once

#include <atomic>
#include <cstddef>
#include <memory_resource>

namespace fixpp::test_support {

// Process-scoped counter for global `operator new` invocations. Incremented
// from the replacement `operator new` defined in
// `pmr_allocation_tracking_resource.cpp` (if linked) — or, if the test
// translation unit doesn't link a replacement, the counter stays at zero and
// AC-P1's "tracked-resource accounting" alone is exercised. The header-only
// form ships the type-erased counter so consumers can opt in to global-new
// hooking separately.
struct global_new_counter {
    static std::atomic<std::size_t>& count() noexcept {
        static std::atomic<std::size_t> n{0};
        return n;
    }
};

class pmr_allocation_tracking_resource final : public std::pmr::memory_resource {
public:
    explicit pmr_allocation_tracking_resource(
        std::pmr::memory_resource* upstream) noexcept
        : upstream_(upstream) {}

    [[nodiscard]] std::size_t allocate_calls() const noexcept {
        return allocate_calls_;
    }

    [[nodiscard]] std::size_t deallocate_calls() const noexcept {
        return deallocate_calls_;
    }

    [[nodiscard]] std::size_t total_bytes_allocated() const noexcept {
        return total_bytes_allocated_;
    }

    [[nodiscard]] std::pmr::memory_resource* upstream() const noexcept {
        return upstream_;
    }

    void reset_counters() noexcept {
        allocate_calls_ = 0;
        deallocate_calls_ = 0;
        total_bytes_allocated_ = 0;
    }

private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        ++allocate_calls_;
        total_bytes_allocated_ += bytes;
        return upstream_->allocate(bytes, alignment);
    }

    void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override {
        ++deallocate_calls_;
        upstream_->deallocate(p, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(
        std::pmr::memory_resource const& other) const noexcept override {
        return this == &other;
    }

    std::pmr::memory_resource* upstream_;
    std::size_t allocate_calls_{0};
    std::size_t deallocate_calls_{0};
    std::size_t total_bytes_allocated_{0};
};

}  // namespace fixpp::test_support
