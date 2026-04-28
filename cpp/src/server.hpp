#ifndef LOGLITE_SERVER_HPP_
#define LOGLITE_SERVER_HPP_

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
    void Run();

    // Signal shutdown; may be called from any thread (e.g. signal handler).
    void Stop();

   private:
    asio::awaitable<void> AcceptLoop(asio::ip::tcp::acceptor& acceptor);
    asio::awaitable<void> HandleConnection(beast::tcp_stream stream);

    ServerContext& ctx_;
    asio::thread_pool pool_;
    asio::ip::tcp::acceptor acceptor_;
};

}  // namespace loglite

#endif  // LOGLITE_SERVER_HPP_
