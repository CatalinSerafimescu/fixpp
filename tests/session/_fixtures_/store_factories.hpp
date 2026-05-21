// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/_fixtures_/store_factories.hpp
//
// Shared factory helpers for 008-message-store tests.
//
// make_default_memory_store — build a MemoryStore with a common default config.
// make_default_file_config  — build a FileStore::Config with common defaults.
//
// Consolidates the ~10 near-identical per-test factory bodies.  Tests with
// substantive divergence (different max_frame_bytes, custom policy) keep their
// own inline config.
#pragma once

#include <filesystem>
#include <string_view>

#include <asio/any_io_executor.hpp>

#include <fixpp/session/file_store.hpp>
#include <fixpp/session/memory_store.hpp>

namespace fixpp::store_test {

/// Build a MemoryStore with bounded policy and the given capacities.
inline fixpp::session::MemoryStore make_default_memory_store(
    std::size_t inbound_cap = 100,
    std::size_t outbound_cap = 100,
    std::size_t max_frame_bytes = 4096)
{
    fixpp::session::MemoryStore::Config cfg;
    cfg.policy = fixpp::session::capacity_policy::bounded;
    cfg.inbound_capacity = inbound_cap;
    cfg.outbound_capacity = outbound_cap;
    cfg.max_frame_bytes = max_frame_bytes;
    return fixpp::session::MemoryStore{cfg};
}

/// Build a FileStore::Config with common defaults (sender=SENDER, target=TARGET,
/// commit_per_message policy, max_frame_bytes=4096).
inline fixpp::session::FileStore::Config make_default_file_config(
    const std::filesystem::path& dir,
    asio::any_io_executor exec,
    std::string_view sender = "SENDER",
    std::string_view target = "TARGET",
    fixpp::session::FileStorePolicy policy = {})
{
    fixpp::session::FileStore::Config cfg;
    cfg.directory = dir;
    cfg.file_io_executor = std::move(exec);
    cfg.sender_comp_id = std::string(sender);
    cfg.target_comp_id = std::string(target);
    cfg.max_frame_bytes = 4096;
    cfg.policy = policy;
    return cfg;
}

}  // namespace fixpp::store_test
