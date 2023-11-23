#ifndef NKURDT_SOCKET_HPP_
#define NKURDT_SOCKET_HPP_

#include <system_error>
#include <vector>
#include <tuple>
#include <chrono>

#if defined(_WIN32)
// for std::max
#define NOMINMAX
#include "WinSock2.h"
#include "Ws2tcpip.h"
#pragma comment(lib, "Ws2_32.lib")
// disable deprecated warning
#pragma warning(disable : 4996)
#elif defined(__linux__) || defined(__APPLE__)
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace nkurdt {

#if defined(_WIN32)
using SocketAddr = SOCKADDR_IN;
#elif defined(__linux__) || defined(__APPLE__)
using SocketAddr = struct sockaddr_in;
#endif

/// A cross-platform socket class.
class Socket {
 private:
#if defined(_WIN32)
  SOCKET sock_ = INVALID_SOCKET;
#elif defined(__linux__) || defined(__APPLE__)
  int sock_ = -1;
#endif

  size_t max_buf_size_ = 0;

 public:
  Socket(size_t max_buf_size) : max_buf_size_(max_buf_size) {}
  ~Socket() = default;

  /// Initialize the socket.
  std::error_condition init(uint16_t port);

  /// Send data to the specified address.
  std::error_condition
  send(const SocketAddr& addr, const std::vector<std::byte>& data);

  /// Receive data from the socket.
  std::tuple<std::error_condition, std::vector<std::byte>, SocketAddr> recv();

  /// Set receive timeout.
  std::error_condition set_recv_timeout(std::chrono::milliseconds timeout);

  /// If the error is a fatal error or a async/timed out error.
  bool is_fatal_error(std::error_condition errc);

  /// Close the socket and cleanup
  std::error_condition cleanup();

  std::error_condition show_info();
};

}  // namespace nkurdt

#endif  // NKURDT_SOCKET_HPP_
