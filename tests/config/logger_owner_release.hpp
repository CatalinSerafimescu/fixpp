// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/config/logger_owner_release.hpp
//
// release_log_owners — drop every Logger a loaded config bundle owns, so the
// FileSinks underneath it close.
//
// ⚠️ shutdown() DRAINS BUT DOES NOT CLOSE. Logger::shutdown() signals and joins
// the drain thread; sink->close() happens only in Logger::Impl::~Impl(). So a
// test that has merely shut a logger down still holds an open handle inside its
// log directory, and removing that directory is the Windows failure #404 is
// about. Releasing the owners is therefore a PRECONDITION of
// fixpp::test_support::remove_temp_dir(), not tidiness.
//
// Defined once, for the four sources of the config_045_tests binary, so the set
// of owning fields has a single home: a new logger-holding field on the bundle
// would otherwise have to be remembered at every call site independently.
#pragma once

namespace fixpp::config_test {

template <typename Bundle>
void release_log_owners(Bundle& bundle) {
    bundle.engine.logger.reset();
    for (auto& sess : bundle.sessions) sess.config.logger_override.reset();
}

}  // namespace fixpp::config_test
