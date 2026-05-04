#ifndef LOGLITE_HARVESTERS_MANAGER_HPP_
#define LOGLITE_HARVESTERS_MANAGER_HPP_

#include "base.hpp"

#include <memory>
#include <vector>

namespace loglite::harvesters {

class HarvesterManager {
   public:
    void Register(std::unique_ptr<Harvester> h) {
        harvesters_.push_back(std::move(h));
    }

    void StartAll() {
        for (auto& h : harvesters_) h->Start();
    }

    void StopAll() {
        for (auto& h : harvesters_) h->Stop();
    }

   private:
    std::vector<std::unique_ptr<Harvester>> harvesters_;
};

}  // namespace loglite::harvesters

#endif  // LOGLITE_HARVESTERS_MANAGER_HPP_
