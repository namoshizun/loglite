#include "runtime.hpp"

#include "schedule.hpp"

#include <stdexcept>

namespace loglite {

Runtime::Runtime(Config config)
    : cfg_(std::move(config)),
      store_(cfg_),
      live_(static_cast<size_t>(cfg_.sse_limit)),
      read_pool_(cfg_.resolve_pool_size()),
      write_strand_(asio::make_strand(write_pool_.get_executor())),
      gate_(std::make_shared<Submission::Gate>()) {
    store_.Open();
    store_.Initialize();
    if (!store_.schema().usable())
        throw std::runtime_error("readiness requires a usable log schema");

    readers_ = std::make_unique<LogReaderPool>(store_, cfg_.resolve_pool_size());
    ingestion_ =
        std::make_unique<Ingestion>(store_, live_, static_cast<size_t>(cfg_.task_backlog_max_size),
                                    std::chrono::seconds{cfg_.task_backlog_flush_interval});
    queries_ = std::make_unique<QueryService>(*readers_, store_, read_pool_.get_executor());

    gate_->submit = [this](nlohmann::json entry) {
        return ingestion_->Submit(std::move(entry)).admitted;
    };
}

Runtime::~Runtime() {
    if (gate_) gate_->Seal();
    if (readers_) readers_->Close();
    store_.Close();
    write_pool_.join();
    read_pool_.join();
}

HttpAccess Runtime::http() {
    return HttpAccess{cfg_, *ingestion_, *queries_, live_, store_.schema(), stopping_, started_at_};
}

void Runtime::RequestStop() {
    stopping_.store(true, std::memory_order_release);
    for (const auto& weak : shutdown_timers_) {
        if (auto timer = weak.lock()) timer->cancel();
    }
    live_.Wake();
}

void Runtime::RegisterShutdownTimer(const std::shared_ptr<asio::steady_timer>& timer) {
    std::erase_if(shutdown_timers_, [](const auto& item) { return item.expired(); });
    shutdown_timers_.push_back(timer);
}

int Runtime::Settle() { return ingestion_->Settle(); }

void Runtime::Maintain() { store_.Maintain(); }

}  // namespace loglite
