#ifndef LOGLITE_SERVER_HPP_
#define LOGLITE_SERVER_HPP_

#include "runtime.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <cstdint>
#include <exception>
#include <list>

namespace asio = boost::asio;
namespace beast = boost::beast;

namespace loglite {

class Server {
   public:
    Server(Runtime& runtime);

    // Start listening and run the thread pool (blocks until shutdown).
    void Run();

    // Signal shutdown; may be called from any thread (e.g. signal handler).
    void Stop();

   private:
    asio::awaitable<void> AcceptLoop(asio::ip::tcp::acceptor& acceptor);
    asio::awaitable<void> HandleConnection(beast::tcp_stream& stream);
    void OnTaskCompleted(std::exception_ptr error);

    Runtime& runtime_;
    asio::thread_pool pool_;
    asio::ip::tcp::acceptor acceptor_;
    std::list<beast::tcp_stream> connections_;
    size_t pending_tasks_{0};
    std::exception_ptr failure_;
};

}  // namespace loglite

#endif  // LOGLITE_SERVER_HPP_
