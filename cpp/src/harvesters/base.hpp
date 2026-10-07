#ifndef LOGLITE_HARVESTERS_BASE_HPP_
#define LOGLITE_HARVESTERS_BASE_HPP_

#include "../log.hpp"
#include "../submission.hpp"

#include <nlohmann/json.hpp>
#include <string>

namespace loglite::harvesters {

// ── Harvester base ─────────────────────────────────────────────────────────────
//
// Harvesters own sockets and file mechanics. They submit decoded records through
// a handle bound to one runtime; they do not touch the backlog or the database.

class Harvester {
   public:
    Harvester(std::string name, Submission submission)
        : name_(std::move(name)), submission_(std::move(submission)) {}

    virtual ~Harvester() = default;

    virtual void Start() = 0;
    virtual void Stop() = 0;

    std::string_view Name() const { return name_; }

   protected:
    void Ingest(nlohmann::json entry) { submission_.Push(std::move(entry)); }

    std::string name_;
    Submission submission_;
};

}  // namespace loglite::harvesters

#endif  // LOGLITE_HARVESTERS_BASE_HPP_
