#ifndef LOGLITE_SUBMISSION_HPP_
#define LOGLITE_SUBMISSION_HPP_

#include <functional>
#include <memory>
#include <mutex>
#include <utility>

#include <nlohmann/json.hpp>

namespace loglite {

// A handle bound to one runtime. After that runtime seals ingestion, Push
// fails on this handle even if a newer runtime is current.
class Submission {
   public:
    struct Gate {
        std::mutex mu;
        std::function<bool(nlohmann::json)> submit;

        bool Push(nlohmann::json entry) {
            std::function<bool(nlohmann::json)> submit_copy;
            {
                std::lock_guard lock{mu};
                submit_copy = submit;
            }
            return submit_copy ? submit_copy(std::move(entry)) : false;
        }

        void Seal() {
            std::lock_guard lock{mu};
            submit = nullptr;
        }
    };

    Submission() = default;
    explicit Submission(std::shared_ptr<Gate> gate) : gate_(std::move(gate)) {}

    [[nodiscard]] bool bound() const noexcept { return static_cast<bool>(gate_); }

    bool Push(nlohmann::json entry) const { return gate_ ? gate_->Push(std::move(entry)) : false; }

   private:
    std::shared_ptr<Gate> gate_;
};

}  // namespace loglite

#endif  // LOGLITE_SUBMISSION_HPP_
