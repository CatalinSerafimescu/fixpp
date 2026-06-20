# src/config/CMakeLists.txt
# fixpp_config_toml — TOML session-config loader [arch §2.1 row 14 / arch §2.3]
# 044-toml-session-config (FR-001..FR-004 / SC-006)
#
# OPT-IN target: no module in the core graph links this. Consumers that want
# config-file loading add: target_link_libraries(... fixpp::config_toml)
#
# FR-004/SC-006: tomlplusplus is PRIVATE — it MUST NOT appear in any public
# header under include/fixpp/config/. Parser isolation enforced here.

find_package(tomlplusplus CONFIG REQUIRED)

add_library(fixpp_config_toml STATIC
  toml_config_loader.cpp
  loader_internal.cpp
  scalar_mappers.cpp
  selector_resolver.cpp
)
add_library(fixpp::config_toml ALIAS fixpp_config_toml)

target_include_directories(fixpp_config_toml PUBLIC
  "${CMAKE_SOURCE_DIR}/include"
)

# Allowed dependencies [arch §2.3]: core, session, dictionary, tls, transport.
# tomlplusplus is PRIVATE: the parser is an implementation detail only.
target_link_libraries(fixpp_config_toml
  PUBLIC  fixpp_core
          fixpp_session
          fixpp_dictionary
          fixpp_tls
          fixpp_transport
  PRIVATE tomlplusplus::tomlplusplus
)
