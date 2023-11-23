#include "sender.hpp"
#include "logging.hpp"
#include "packet.hpp"
#include "socket.hpp"

#include "argparser.hpp"

#include <chrono>
#include <fstream>
#include <utility>

int main(int argc, char* argv[]) {
  using namespace std::chrono_literals;

  auto argparser = ArgumanetParser();
  argparser.add_argument("remote-ip")
    .required(true)
    .default_value<std::string>("127.0.0.1")
    .scan<std::string>();
  argparser.add_argument("remote-port")
    .required(true)
    .default_value(3333)
    .scan<int>();
  argparser.add_argument("port").required(true).default_value(1234).scan<int>();
  argparser.add_argument("selective-repeat").is_flag(true).scan<bool>();
  argparser.add_argument("fast-retransmit").is_flag(true).scan<bool>();
  argparser.add_argument("timeout")
    .required(true)
    .default_value<size_t>(1000)
    .scan<size_t>();
  argparser.add_argument("max-window-size")
    .required(true)
    .default_value<size_t>(5)
    .scan<size_t>();
  argparser.add_argument("max-retry-count")
    .required(true)
    .default_value<size_t>(-1)
    .scan<size_t>();
  argparser.add_argument("debug").is_flag(true).scan<bool>();

  argparser.parse_args(argc, argv);

  auto remote_ip = argparser.get<std::string>("remote-ip").value();
  nkurdt::global_logger.debug(std::format("remote ip: {}", remote_ip));

  auto remote_port = argparser.get<int>("remote-port").value();
  nkurdt::global_logger.debug(std::format("remote port: {}", remote_port));

  auto port = argparser.get<int>("port").value();
  nkurdt::global_logger.debug(std::format("port: {}", port));

  auto selective_repeat = argparser.get<bool>("selective-repeat").value();
  nkurdt::global_logger.debug(
    (selective_repeat ? "selective repeat" : "go back n")
  );

  auto fast_retransmit = argparser.get<bool>("fast-retransmit").value();
  nkurdt::global_logger.debug(
    std::format("fast retransmit: {}", fast_retransmit)
  );

  auto timeout = argparser.get<size_t>("timeout").value();
  nkurdt::global_logger.debug(std::format("timeout: {}", timeout));

  auto max_window_size = argparser.get<size_t>("max-window-size").value();
  nkurdt::global_logger.debug(
    std::format("max window size: {}", max_window_size)
  );

  auto max_retry_count = argparser.get<size_t>("max-retry-count").value();
  nkurdt::global_logger.debug(
    std::format("max retry count: {}", max_retry_count)
  );

  auto debug = argparser.get<bool>("debug").value();
  nkurdt::global_logger.debug(std::format("debug: {}", debug));

  nkurdt::Logger::enable_debug(debug);

  nkurdt::Socket socket(NKURDT_MAX_PACKET_SIZE * 2);
  socket.init(port);
  socket.show_info();

  nkurdt::Sender sender(std::move(socket));

  sender.set_selective_repeat(selective_repeat);
  sender.set_fast_retransmit(fast_retransmit);
  sender.set_timeout(std::chrono::milliseconds(timeout));
  sender.set_max_window_size(max_window_size);
  sender.set_max_retry_count(max_retry_count);

  nkurdt::SocketAddr addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(remote_port);
  addr.sin_addr.s_addr = inet_addr(remote_ip.c_str());

  sender.init();

  while (true) {
    std::string prompt;
    std::cin >> prompt;

    if (prompt == "exit") {
      sender.stop();
      sender.cleanup();
      break;
    } else if (prompt == "send") {
      std::cout << "input file name: ";
      std::string filename;

      std::cin >> filename;

      std::ifstream ifs(filename, std::ios::binary);

      sender.send(ifs);
    } else if (prompt == "connect") {
      nkurdt::global_logger.info("connecting...");
      sender.connect(addr);
    } else if (prompt == "disconnect") {
      nkurdt::global_logger.info("disconnecting...");
      sender.disconnect();
    }
  }

  return 0;
}
