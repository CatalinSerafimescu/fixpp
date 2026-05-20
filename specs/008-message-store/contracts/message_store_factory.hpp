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
#pragma once

#include <memory>
#include <memory_resource>
#include <string_view>

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
    // for its allocations.
    //
    // Returns std::unique_ptr<MessageStore> ownership (N1) — no shared_ptr,
    // no sharing across sessions, no mid-session swap ([arch §5.6]).
    //
    // Validation: both default factories (MemoryStoreFactory, FileStoreFactory)
    // enforce the storage-DoS guard against EngineConfig::max_store_memory_per_session
    // (FR-014 / SC-004 / I-11); on overflow returns store_factory_failed.
    // FileStoreFactory also takes an flock / LockFileEx advisory exclusive
    // lock on the live log (FR-013 / I-16); contention → store_factory_failed.
    // FileStoreFactory verifies the sentinel record's session_triple_hash on
    // re-open; mismatch → store_factory_failed.
    [[nodiscard]] virtual fixpp::core::expected_t<std::unique_ptr<MessageStore>>
    make(std::string_view sender,
         std::string_view target,
         std::pmr::memory_resource* mr) noexcept = 0;
};

}  // namespace fixpp::session
