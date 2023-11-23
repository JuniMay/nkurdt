#include "receiver.hpp"
#include "logging.hpp"
#include "packet.hpp"
#include "socket.hpp"

#include "argparser.hpp"

#include <coroutine>
#include <fstream>
#include <sstream>
#include <utility>

int main(int argc, char* argv[]) {
  auto argparser = ArgumanetParser();
  argparser.add_argument("port").required(true).default_value(4321).scan<int>();
  argparser.add_argument("selective-repeat").is_flag(true).scan<bool>();
  argparser.add_argument("max-window-size")
    .required(true)
    .default_value<size_t>(5)
    .scan<size_t>();
  argparser.add_argument("debug").is_flag(true).scan<bool>();

  argparser.parse_args(argc, argv);

  auto port = argparser.get<int>("port").value();
  nkurdt::global_logger.debug(std::format("port: {}", port));

  auto selective_repeat = argparser.get<bool>("selective-repeat").value();
  nkurdt::global_logger.debug(
    (selective_repeat ? "selective repeat" : "go back n")
  );

  auto max_window_size = argparser.get<size_t>("max-window-size").value();
  nkurdt::global_logger.debug(
    std::format("max window size: {}", max_window_size)
  );

  auto debug = argparser.get<bool>("debug").value();
  nkurdt::global_logger.debug(std::format("debug: {}", debug));

  nkurdt::Logger::enable_debug(debug);

  nkurdt::Socket socket(NKURDT_MAX_PACKET_SIZE * 2);
  socket.init(port);
  socket.show_info();

  std::shared_ptr<std::stringstream> temp_os =
    std::make_shared<std::stringstream>();
  nkurdt::Receiver receiver(std::move(socket), temp_os);

  receiver.set_selective_repeat(selective_repeat);
  receiver.set_max_window_size(max_window_size);

  auto recv_generator = receiver.recv();

  while (true) {
    bool has_data = recv_generator.next();

    if (!has_data) {
      break;
    }

    // input file name, write to file
    std::string filename;
    std::cout << "input file name: ";
    std::cin >> filename;

    std::cout << "writing to file " << filename << std::endl;

    std::ofstream ofs(filename, std::ios::binary);

    ofs << temp_os->rdbuf();
  }

  receiver.cleanup();

  return 0;
}
