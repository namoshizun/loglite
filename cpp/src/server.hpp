#pragma once

#include "globals.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <cstdint>

namespace asio = boost::asio;
namespace beast = boost::beast;

namespace loglite {

class Server {
   public:
    Server(ServerContext& ctx, unsigned int thread_count = 0);

    // Start listening and run the thread pool (blocks until shutdown).
    void run();

    // Signal shutdown; may be called from any thread (e.g. signal handler).
    void stop();

   private:
    asio::awaitable<void> accept_loop(asio::ip::tcp::acceptor& acceptor);
    asio::awaitable<void> handle_connection(beast::tcp_stream stream);

    ServerContext& ctx_;
    asio::thread_pool pool_;
    asio::ip::tcp::acceptor acceptor_;
};

}  // namespace loglite
