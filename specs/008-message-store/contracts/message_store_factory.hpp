// SPDX-License-Identifier: AGPL-3.0-or-later
//
// specs/008-message-store/contracts/message_store_factory.hpp
//
// SHAPE ORACLE — declaration-only contract for the EXTENDED-IN-PLACE
// fixpp::session::MessageStoreFactory. Anchor: .specify/2e-msgstore.md v0.4
// §4.4 (N1 — unique_ptr ownership).
//
// CRITICAL: this contract EXTENDS the minimal polymorphic-bind-target stub
// shipped by 007 at include/fixpp/session/message_store_factory.hpp. The
// class identity (fixpp::session::MessageStoreFactory), the deleted move /
// copy, and the virtual destructor remain UNCHANGED. This feature ADDS the
// pure-virtual make(...) method — it does NOT replace or duplicate the
// class.
//
// SessionConfig::store_factory is std::unique_ptr<MessageStoreFactory>
// (already at session_config.hpp:106 per the 007-shipped [2d §4.5] Appendix
// D §D.1 amendment); the unique_ptr ownership of the returned MessageStore
// per make() is N1 (independent of the factory's ownership shape).
//
// Store-object allocation contract (v0.5 per [2e §D.6] — Gap 3 close):
// the make() return type std::unique_ptr<MessageStore> commits the v1.0
// contract to std::default_delete<MessageStore> destruction (the
// default unique_ptr deleter): the concrete store object MUST be
// destructible via `delete static_cast<MessageStore*>(p)`. Factory
// implementations that wish to use a PMR allocator for the store
// object itself MUST wrap deallocation into a std::default_delete-
// compatible path (typical pattern: a static operator delete overload
// on the concrete store class that routes back to the PMR resource,
// paired with std::pmr::polymorphic_allocator::new_object for the
// matching allocation). A std::unique_ptr<MessageStore, CustomDeleter>
// return type is NOT supported in v1.0; reserved for a possible
// post-v1.0 evolution per [const §X.4]. The `mr` PMR parameter
// threaded into make() and the [2e §6.1.1] / [2e §8] / FR-026 / FR-027
// PMR contracts govern only the store's INTERNAL storage (slab, ring,
// framing scratch, index, persisted-frame copy) — they do NOT govern
// the deleter shape of the store object itself.
#pragma once

#include <memory>
#include <memory_resource>
#include <string_view>

#include <asio/any_io_executor.hpp>

#include <fixpp/core/error.hpp>                  // expected_t
#include <fixpp/session/message_store.hpp>       // MessageStore

namespace fixpp::session {

class MessageStoreFactory {
public:
    MessageStoreFactory()                                       = default;
    MessageStoreFactory(const MessageStoreFactory&)             = delete;
    MessageStoreFactory& operator=(const MessageStoreFactory&)  = delete;
    MessageStoreFactory(MessageStoreFactory&&)                  = delete;
    MessageStoreFactory& operator=(MessageStoreFactory&&)       = delete;
    virtual ~MessageStoreFactory()                              = default;

    // make: mint a MessageStore for the given <sender, target> identity.
    // sender / target are FIX-CompID strings consumed for the on-disk log
    // filename (FileStore) and for the session_triple_hash in the sentinel
    // record (FileStore). mr is the per-session PMR resource the impl uses
    // for its allocations. max_store_memory_bytes is the engine-resolved
    // EngineConfig::max_store_memory_per_session value the engine threads
    // in at call time so the factory can enforce the storage-DoS guard
    // (FR-014 / SC-004 / I-11) without taking an EngineConfig& back-channel
    // through the constructor — keeps the factory CTOR Config-only per the
    // design-doc §4.4 frozen surface. file_io_executor is the
    // engine-resolved EngineConfig::file_io_executor value the engine
    // threads in at call time for FileStore impls that need it
    // (FR-024 / I-13 / research D-7); the FileStoreFactory populates the
    // minted FileStore::Config::file_io_executor with this value (preserving
    // [2e §4.3.2]:665 required-at-construction on FileStore itself, since
    // FileStore is constructed inside make()) UNLESS the factory's Config
    // already carries a caller-supplied executor, in which case the
    // Config-supplied executor wins (caller override; Path-B user shape).
    // MemoryStore impls ignore the file_io_executor parameter (the empty /
    // default-constructed value is permitted on the MemoryStoreFactory path).
    //
    // Returns std::unique_ptr<MessageStore> ownership (N1) — no shared_ptr,
    // no sharing across sessions, no mid-session swap ([arch §5.6]).
    //
    // Validation: both default factories (MemoryStoreFactory, FileStoreFactory)
    // enforce the storage-DoS guard against max_store_memory_bytes
    // (FR-014 / SC-004 / I-11) using checked overflow-safe arithmetic; on
    // any overflow / cap-exceeded condition returns store_factory_failed.
    // FileStoreFactory also takes an flock / LockFileEx advisory exclusive
    // lock on the live log (FR-013 / I-16); contention → store_factory_failed.
    // FileStoreFactory verifies the sentinel record's session_triple_hash on
    // re-open; mismatch → store_factory_failed. FileStoreFactory rejects with
    // store_factory_failed if the resolved file_io_executor (Config-supplied
    // OR threaded-in) is empty.
    [[nodiscard]] virtual fixpp::core::expected_t<std::unique_ptr<MessageStore>>
    make(std::string_view sender,
         std::string_view target,
         std::pmr::memory_resource* mr,
         std::size_t max_store_memory_bytes,
         asio::any_io_executor file_io_executor) noexcept = 0;
};

}  // namespace fixpp::session
