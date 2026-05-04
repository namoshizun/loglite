#ifndef LOGLITE_API_HPP_
#define LOGLITE_API_HPP_

#include "harvesters/manager.hpp"

#include <filesystem>
#include <nlohmann/json.hpp>

namespace loglite {

// ── Server ────────────────────────────────────────────────────────────────────
//
// Runs the HTTP server, blocking until a shutdown signal is received or
// Stop() is called from another thread.  extra_harvesters are started before
// the server and stopped after it returns.

void RunServer(const std::filesystem::path& config_path,
               harvesters::HarvesterManager& extra_harvesters,
               unsigned int thread_count = 0);

// Signal a running server to shut down.  Safe to call from any thread,
// including a Python thread holding the GIL.
void StopServer();

// ── Migrations ────────────────────────────────────────────────────────────────

void Rollout(const std::filesystem::path& config_path, int start_version = -1);
void Rollback(const std::filesystem::path& config_path, int version, bool force = false);

// ── Backlog ───────────────────────────────────────────────────────────────────
//
// Thread-safe push into the active server's backlog.  Must only be called
// after RunServer() has started (i.e. from a harvester thread).

void PushToBacklog(nlohmann::json entry);

}  // namespace loglite

#endif  // LOGLITE_API_HPP_
