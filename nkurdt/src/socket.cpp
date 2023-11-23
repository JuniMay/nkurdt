#include "socket.hpp"
#include <format>
#include <iostream>
#include <system_error>
#include <utility>
#include <vector>
#include "logging.hpp"

namespace nkurdt {

using namespace std::chrono_literals;

std::error_condition Socket::init(uint16_t port) {
#if defined(_WIN32)
  WSADATA wsa_data;
  int wsa_result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
  if (wsa_result != 0) {
    return std::error_condition(WSAGetLastError(), std::system_category());
  }
#endif

  // create the socket
  sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

  if (
#if defined(_WIN32)
    sock_ == INVALID_SOCKET
#elif defined(__linux__) || defined(__APPLE__)
    sock_ == -1
#endif
  ) {
    return std::error_condition(
#if defined(_WIN32)
      WSAGetLastError()
#elif defined(__linux__) || defined(__APPLE__)
      errno
#endif
        ,
      std::system_category()
    );
  }

  SocketAddr addr;

  memset(&addr, 0, sizeof(addr));

  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);

  int result =
    bind(sock_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));

  if (result < 0) {
    return std::error_condition(
#if defined(_WIN32)
      WSAGetLastError()
#elif defined(__linux__) || defined(__APPLE__)
      errno
#endif
        ,
      std::system_category()
    );
  }

  // set timeout
  this->set_recv_timeout(1000ms);

  return std::error_condition();
}

std::error_condition
Socket::send(const SocketAddr& addr, const std::vector<std::byte>& data) {
  if (data.size() > max_buf_size_) {
    return std::error_condition(EMSGSIZE, std::system_category());
  }

  global_logger.debug(std::format(
    "send: {} bytes to {}:{}", data.size(),
    inet_ntoa(reinterpret_cast<const in_addr&>(addr.sin_addr)),
    ntohs(addr.sin_port)
  ));

  int result = sendto(
    sock_, reinterpret_cast<const char*>(data.data()),
    static_cast<int>(data.size()), 0, reinterpret_cast<const sockaddr*>(&addr),
    sizeof(addr)
  );
  if (result < 0) {
    return std::error_condition(
#if defined(_WIN32)
      WSAGetLastError()
#elif defined(__linux__) || defined(__APPLE__)
      errno
#endif
        ,
      std::system_category()
    );
  }
  return std::error_condition();
}

std::tuple<std::error_condition, std::vector<std::byte>, SocketAddr>
Socket::recv() {
  std::vector<std::byte> buf(max_buf_size_);
  SocketAddr addr;
  socklen_t addr_len = sizeof(addr);
  int result = recvfrom(
    sock_, reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()), 0,
    reinterpret_cast<sockaddr*>(&addr), &addr_len
  );
  if (result < 0) {
    return std::make_tuple(
      std::error_condition(
#if defined(_WIN32)
        WSAGetLastError()
#elif defined(__linux__) || defined(__APPLE__)
        errno
#endif
          ,
        std::system_category()
      ),
      std::vector<std::byte>(), SocketAddr()
    );
  }

  buf.resize(static_cast<size_t>(result));
  return std::make_tuple(std::error_condition(), std::move(buf), addr);
}

std::error_condition Socket::cleanup() {
#if defined(_WIN32)
  int result = closesocket(sock_);
  if (result != 0) {
    return std::error_condition(WSAGetLastError(), std::system_category());
  }
  WSACleanup();
#elif defined(__linux__) || defined(__APPLE__)
  int result = close(sock_);
  if (result != 0) {
    return std::error_condition(errno, std::system_category());
  }
#endif
  return std::error_condition();
}

std::error_condition Socket::set_recv_timeout(std::chrono::milliseconds timeout
) {
  struct timeval tv;
  tv.tv_sec = (long)timeout.count() / 1000;
  tv.tv_usec = (timeout.count() % 1000) * 1000;

  int result = setsockopt(
    sock_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv),
    sizeof(tv)
  );

  if (result < 0) {
    return std::error_condition(
#if defined(_WIN32)
      WSAGetLastError()
#elif defined(__linux__) || defined(__APPLE__)
      errno
#endif
        ,
      std::system_category()
    );
  }

  return std::error_condition();
}

bool Socket::is_fatal_error(std::error_condition errc) {
  if (!errc) {
    return false;
  }
#if defined(_WIN32)
  return errc.value() != WSAETIMEDOUT;
#elif defined(__linux__) || defined(__APPLE__)
  return errc.value() != EAGAIN && errc.value() != EWOULDBLOCK;
#endif
}

std::error_condition Socket::show_info() {
  char hostname[256];
  gethostname(hostname, sizeof(hostname));

  struct addrinfo hints, *info;

  memset(&hints, 0, sizeof(hints));

  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;

  int result = getaddrinfo(hostname, nullptr, &hints, &info);

  if (result != 0) {
    return std::error_condition(
#if defined(_WIN32)
      WSAGetLastError()
#elif defined(__linux__) || defined(__APPLE__)
      errno
#endif
        ,
      std::system_category()
    );
  }

  char ip[INET_ADDRSTRLEN];

  memset(ip, 0, sizeof(ip));

  inet_ntop(
    AF_INET, &reinterpret_cast<SocketAddr*>(info->ai_addr)->sin_addr, ip,
    INET_ADDRSTRLEN
  );

  freeaddrinfo(info);

  // port
  SocketAddr addr;
  socklen_t addr_len = sizeof(addr);
  result = getsockname(sock_, reinterpret_cast<sockaddr*>(&addr), &addr_len);

  if (result < 0) {
    return std::error_condition(
#if defined(_WIN32)
      WSAGetLastError()
#elif defined(__linux__) || defined(__APPLE__)
      errno
#endif
        ,
      std::system_category()
    );
  }

  extern Logger global_logger;

  global_logger.log(
    LogLevel::Info,
    std::format("socket info: {}, {}:{}", hostname, ip, ntohs(addr.sin_port))
  );

  return std::error_condition();
}

}  // namespace nkurdt
