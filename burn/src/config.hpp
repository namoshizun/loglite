#ifndef BURN_CONFIG_HPP_
#define BURN_CONFIG_HPP_

#include <cstddef>
#include <string>

namespace burn {

struct Endpoint {
    std::string host;
    std::string port{"7788"};
};

struct Config {
    Endpoint endpoint;
    unsigned concurrency{0};
    double qps{0.0};
    double per_sender_qps{0.0};
    std::size_t message_size_mean{128};
    double info_ratio{0.9};
    unsigned duration_sec{60};
};

}  // namespace burn

#endif
