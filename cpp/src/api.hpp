#ifndef LOGLITE_API_HPP_
#define LOGLITE_API_HPP_

#include "submission.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <nlohmann/json.hpp>

namespace loglite {

// Runs one server. `on_ready` fires after the schema is usable, before
// admission. `on_stop` fires when shutdown begins and must only signal
// producers; the caller then waits until NotifyProducersFinished(epoch).
void RunServer(const std::filesystem::path& config_path, std::function<void()> on_ready = {},
               std::function<void()> on_stop = {});

// Signal a running server to shut down.  Safe to call from any thread,
// including a Python thread holding the GIL.
void StopServer();

// ── Migrations ────────────────────────────────────────────────────────────────

void Rollout(const std::filesystem::path& config_path, int start_version = -1);
void Rollback(const std::filesystem::path& config_path, int version, bool force = false);

// ── Backlog ───────────────────────────────────────────────────────────────────
//
// Compatibility push into the active run. A Submission captured earlier stays
// bound to the run that created it.
void PushToBacklog(nlohmann::json entry);
[[nodiscard]] Submission CurrentSubmission();
[[nodiscard]] uint64_t CurrentEpoch();
void NotifyProducersFinished(uint64_t epoch);

}  // namespace loglite

#endif  // LOGLITE_API_HPP_
