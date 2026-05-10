# src/session/CMakeLists.txt
# fixpp::session — Session, Application, FSM, seqnum, MessageStore [arch §4.4]

add_library(fixpp_session INTERFACE)
add_library(fixpp::session ALIAS fixpp_session)

target_include_directories(fixpp_session INTERFACE
  "${CMAKE_SOURCE_DIR}/include"
)

# Allowed dependencies: core, dictionary, wire, transport (interface),
#                        log (interface), otel (interface) [arch §2.3]
target_link_libraries(fixpp_session INTERFACE
  fixpp_core
  fixpp_dictionary
  fixpp_wire
  fixpp_transport
  fixpp_log
  fixpp_otel
)
