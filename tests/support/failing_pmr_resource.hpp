// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/failing_pmr_resource.hpp
//
// Seam #9 (allocator-failure injection) per
// `specs/002-dictionary-xml-loader/research.md` D-9. Carried as shared infra
// under `tests/support/` so future `dict::` tests can reuse it.
//
// Wraps an upstream `std::pmr::memory_resource` and throws `std::bad_alloc`
// on the configured Nth `do_allocate` call (1-indexed). Used to verify
// `XmlLoader::load*` translates PMR allocation failures into
// `dict::xml_oom_error` via `core::detail::trap_throw_or_throw<xml_oom_error>`
// per AC-L9 / AC-P2 / `research.md` D-3.

#pragma once

#include <cstddef>
#include <memory_resource>
#include <new>

namespace fixpp::test_support {

class failing_pmr_resource final : public std::pmr::memory_resource {
public:
    // Throw `std::bad_alloc` on the `fail_on_call_n`-th `do_allocate` call
    // (1-indexed). `fail_on_call_n == 0` means "never fail".
    failing_pmr_resource(std::pmr::memory_resource* upstream,
                         std::size_t fail_on_call_n) noexcept
        : upstream_(upstream), fail_on_call_n_(fail_on_call_n) {}

    [[nodiscard]] std::size_t allocate_calls() const noexcept {
        return allocate_calls_;
    }

    [[nodiscard]] std::size_t deallocate_calls() const noexcept {
        return deallocate_calls_;
    }

    [[nodiscard]] std::pmr::memory_resource* upstream() const noexcept {
        return upstream_;
    }

private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        ++allocate_calls_;
        if (fail_on_call_n_ != 0 && allocate_calls_ == fail_on_call_n_) {
            throw std::bad_alloc{};
        }
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
    std::size_t fail_on_call_n_;
    std::size_t allocate_calls_{0};
    std::size_t deallocate_calls_{0};
};

}  // namespace fixpp::test_support
