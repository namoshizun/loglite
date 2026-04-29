#ifndef LOGLITE_HARVESTERS_BASE_HPP_
#define LOGLITE_HARVESTERS_BASE_HPP_

#include "../backlog.hpp"
#include "../log.hpp"

#include <nlohmann/json.hpp>
#include <string>

namespace loglite::harvesters {

// ── Harvester base ─────────────────────────────────────────────────────────────
//
// All harvesters own a reference to the shared Backlog and push JSON log entries
// via ingest().  Concrete harvesters implement start() / stop().

class Harvester {
   public:
    Harvester(std::string name, Backlog& backlog) : name_(std::move(name)), backlog_(backlog) {}

    virtual ~Harvester() = default;

    virtual void Start() = 0;
    virtual void Stop() = 0;

    std::string_view Name() const { return name_; }

   protected:
    void Ingest(nlohmann::json entry) { backlog_.Add(std::move(entry)); }

    std::string name_;
    Backlog& backlog_;
};

}  // namespace loglite::harvesters

#endif  // LOGLITE_HARVESTERS_BASE_HPP_
