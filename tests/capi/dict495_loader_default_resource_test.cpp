// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/capi/dict495_loader_default_resource_test.cpp
//
// fixpp#495 D-5 (`.specify/495-493-486-dict-reify-copy.md` §7, owner ruling R-D,
// T-20): `fixpp_dict_load_from_xml` backs the returned Dictionary with
// `std::pmr::new_delete_resource()`, so a host's installed default resource holds
// NONE of its storage once the call returns. The installed resource tracks bytes
// currently held (not a total: a transient pmr temporary may allocate and free).
//
// Discriminates against the loader using `std::pmr::get_default_resource()`: held
// bytes would then be non-zero. A non-zero reading with the loader on
// `new_delete_resource()` means a `get_default_resource()` fallback for retained
// storage elsewhere in the loader: a finding in the loader.
//
// Positive control (`InstalledDefaultResourceSeesALoaderThatUsesIt`): the same
// resource, fed by a load that DOES use `get_default_resource()`, must read
// non-zero while that Dictionary lives. If it does not, the zero above is
// vacuous: the instrument cannot see a loader's retained storage.
//
// Standalone (`[const §VII.8]`): it replaces the process-wide default resource.

#include <gtest/gtest.h>

#include <cstddef>
#include <memory_resource>
#include <string>

#include "fix/c_api/dict.h"
#include "fixpp/dict/load_any.hpp"

namespace {

// Forwards to new_delete_resource() and tracks the bytes it currently holds.
class held_bytes_resource final : public std::pmr::memory_resource {
public:
    [[nodiscard]] std::size_t held() const noexcept { return held_; }

private:
    void* do_allocate(std::size_t bytes, std::size_t align) override {
        void* p = std::pmr::new_delete_resource()->allocate(bytes, align);
        held_ += bytes;
        return p;
    }
    void do_deallocate(void* p, std::size_t bytes, std::size_t align) override {
        held_ -= bytes;
        std::pmr::new_delete_resource()->deallocate(p, bytes, align);
    }
    [[nodiscard]] bool do_is_equal(std::pmr::memory_resource const& o) const noexcept override {
        return this == &o;
    }
    std::size_t held_ = 0;
};

// Installs `r` as the default resource for its lifetime, restoring the previous one.
class default_resource_guard {
public:
    explicit default_resource_guard(std::pmr::memory_resource* r) noexcept
        : previous_{std::pmr::set_default_resource(r)} {}
    ~default_resource_guard() { std::pmr::set_default_resource(previous_); }
    default_resource_guard(default_resource_guard const&) = delete;
    default_resource_guard& operator=(default_resource_guard const&) = delete;
    default_resource_guard(default_resource_guard&&) = delete;
    default_resource_guard& operator=(default_resource_guard&&) = delete;

private:
    std::pmr::memory_resource* previous_;
};

TEST(CapiDictionary, LoadDoesNotRetainInstalledDefaultResource) {
    held_bytes_resource host;
    fixpp_dict_t* dict = nullptr;
    {
        default_resource_guard const guard{&host};
        std::string const path = std::string(FIXPP_DICT_DATA_DIR) + "/FIX44.xml";
        ASSERT_EQ(fixpp_dict_load_from_xml(path.c_str(), &dict), FIXPP_ERR_OK);
        ASSERT_NE(dict, nullptr);
        EXPECT_EQ(host.held(), 0U)
            << "the loaded Dictionary must not keep storage in the host's default resource "
               "(fixpp#495 D-5, BREAKING C-ABI 1.8)";
        fixpp_dict_destroy(dict);
    }
    EXPECT_EQ(host.held(), 0U);
}

TEST(CapiDictionary, InstalledDefaultResourceSeesALoaderThatUsesIt) {
    held_bytes_resource host;
    {
        default_resource_guard const guard{&host};
        std::string const path = std::string(FIXPP_DICT_DATA_DIR) + "/FIX44.xml";
        {
            // A loader that takes the host's default resource.
            auto const dict = fixpp::dict::load_any(path, std::pmr::get_default_resource());
            EXPECT_GT(host.held(), 0U)
                << "held_bytes_resource must see a Dictionary loaded through it; otherwise "
                   "LoadDoesNotRetainInstalledDefaultResource's zero proves nothing";
        }
        EXPECT_EQ(host.held(), 0U) << "a destroyed Dictionary must return what it held";
    }
}

}  // namespace
