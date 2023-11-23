#ifndef NKURDT_RECEIVER_H_
#define NKURDT_RECEIVER_H_

#include <coroutine>
#include <format>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <string_view>
#include "concurrency.hpp"
#include "logging.hpp"
#include "packet.hpp"
#include "socket.hpp"

namespace nkurdt {

enum class ReceiverState {
  Idle,
  Connected,
  Receiving,
};

/// A generator.
///
/// C++23 supports generator, but this project is using C++20 :(
template <typename T>
struct Generator {
  struct promise_type {
    T value;
    std::exception_ptr exception;

    std::suspend_always initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    std::suspend_always yield_value(T v) {
      value = v;
      return {};
    }
    Generator get_return_object() {
      return Generator{std::coroutine_handle<promise_type>::from_promise(*this)
      };
    }
    void unhandled_exception() { exception = std::current_exception(); }
    void return_void() {}
  };

  std::coroutine_handle<promise_type> coro;

  Generator(std::coroutine_handle<promise_type> h) : coro(h) {}
  ~Generator() {
    if (coro)
      coro.destroy();
  }
  Generator(const Generator&) = delete;
  Generator& operator=(const Generator&) = delete;
  Generator(Generator&& other) : coro(other.coro) { other.coro = nullptr; }
  Generator& operator=(Generator&& other) {
    if (this != &other) {
      coro = other.coro;
      other.coro = nullptr;
    }
    return *this;
  }

  T next() {
    if (coro) {
      coro.resume();
      if (coro.done()) {
        std::exception_ptr e = coro.promise().exception;
        if (e)
          std::rethrow_exception(e);
      }
      return coro.promise().value;
    }
    throw std::runtime_error("Cannot iterate past the end.");
  }
};

/// A receiver as a coroutine.
///
/// The receiver class is used in a upper layer client. The client start this
/// coroutine and then the receiver will receive data from the sender. After a
/// `DataStop` packet is received, the receiver go back and resume the client.
class Receiver {
 private:
  /// The window.
  ///
  /// [lower, upper) indicates the ready-to-receive seq range.
  /// i.e. seq lower than `lower` is already received, and seq greater than or
  /// equal to `upper` is not ready to be received yet (because the network is
  /// too busy).
  std::pair<SequenceNumber, SequenceNumber> window_;
  /// The recv buffer.
  std::set<Packet> recv_buffer_;
  /// The channel.
  Socket channel_;
  /// The peer address.
  std::optional<SocketAddr> peer_addr_;
  /// The logger.
  Logger logger_;
  /// The maximum window size.
  size_t max_window_size_;
  /// The state.
  ReceiverState state_;
  /// Running indicator.
  bool running_;

  /// Selective repeat flag
  bool selective_repeat_;

  std::shared_ptr<std::ostream> os_;

 public:
  Receiver(Socket&& channel, std::shared_ptr<std::ostream> os)
    : channel_(std::move(channel)), logger_(std::cout, "Receiver"), os_(os) {}

  ~Receiver() = default;

  /// The receive coroutine.
  ///
  /// This coroutine will receive data from the sender. After a `DataStop`
  /// packet is received, the coroutine will go back and resume the client.
  /// If disconnect or error happens, the coroutine will return false.
  Generator<bool> recv();

  void set_ostream(std::shared_ptr<std::ostream> os) { os_ = os; }

  void set_selective_repeat(bool selective_repeat) {
    selective_repeat_ = selective_repeat;
  }

  void set_max_window_size(size_t max_window_size) {
    max_window_size_ = max_window_size;
  }

  void cleanup() { channel_.cleanup(); }
};

}  // namespace nkurdt

#endif  // NKURDT_RECEIVER_H_
