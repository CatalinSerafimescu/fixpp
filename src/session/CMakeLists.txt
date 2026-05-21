# src/session/CMakeLists.txt
# fixpp::session — Session, Application, FSM, seqnum, MessageStore [arch §4.4]
#
# Promoted from INTERFACE to a compiled STATIC library in Phase 4
# (007-threading-clock T012/T014): the minimal real Session skeleton (D-4 /
# E10) ships an out-of-line src/session/session.cpp (ctor + never-null
# session_arena() resolution chain + per-story open()/close() bodies). Mirrors
# the fixpp_core INTERFACE→STATIC and fixpp_sync precedents. asio::asio is now
# a direct dependency (Session uses asio::awaitable / cancellation_state);
# core stays the asio-free leaf [arch §2.2] — the asio-dependent threading
# headers (clock/session_executor/…) are header-only and pull asio through
# whoever includes them.

add_library(fixpp_session STATIC
  session.cpp
  session_executor.cpp   # fixpp::core::make_session_executor (T019; the
                         # core-namespace bridge defined session-side so
                         # core/ stays the asio-free leaf — [arch §2.3])
  ${CMAKE_SOURCE_DIR}/src/core/system_clock_source.cpp  # production fixpp::core
                         # default Clock impl (T028); asio-dependent so it is
                         # compiled here, NOT into the asio-free fixpp_core
                         # leaf [arch §2.2] (same rationale as the bridge).
)
add_library(fixpp::session ALIAS fixpp_session)

target_include_directories(fixpp_session PUBLIC
  "${CMAKE_SOURCE_DIR}/include"
)

# Allowed dependencies: core, dictionary, wire, transport (interface),
#                        log (interface), otel (interface) [arch §2.3]
#                        + asio (awaitable / cancellation slots — 007).
target_link_libraries(fixpp_session PUBLIC
  fixpp_core
  fixpp_dictionary
  fixpp_wire
  fixpp_transport
  fixpp_log
  fixpp_otel
  asio::asio
)

# 008-message-store: Crc32c::crc32c is added PRIVATE in T024 when file_store.cpp
# ships (per-record + sentinel CRC32 over Castagnoli polynomial 0x1EDC6F41;
# research D-3; tasks.md T003 "FileStore-touching targets only"). The find_package
# at the top-level CMakeLists.txt is already in place.

# T021 (008-message-store / US1): MemoryStore is header-only.
# NO src/session/memory_store.cpp ships. Decision rationale: hot-path
# primitives benefit from HALO-elision ([const §XI.6]) — the same
# rationale that kept 006/007 async_mutex/Clock header-inline. The
# 007 precedent for header-inline session primitives is canonical.
# See include/fixpp/session/memory_store.hpp for the full implementation.

# T024 (008-message-store / US1): FileStore compiled translation unit.
# T026 (008-message-store / US1): FileStoreFactory compiled translation unit.
# Both are added to the fixpp_session STATIC target below once T024 ships.
target_sources(fixpp_session PRIVATE
  file_store.cpp
  file_store_factory.cpp
  quickfix_compat/cfg_loader.cpp  # T047 (008-message-store / US4): QuickFIX .cfg → FileStoreFactory config translation
)

target_link_libraries(fixpp_session PRIVATE Crc32c::crc32c)
