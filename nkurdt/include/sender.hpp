#ifndef NKURDT_SENDER_H_
#define NKURDT_SENDER_H_

#include <format>
#include <istream>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include "concurrency.hpp"
#include "logging.hpp"
#include "packet.hpp"
#include "socket.hpp"

namespace nkurdt {

/// The sender states.
enum class SenderState {
  /// The sender is idle.
  Idle,
  /// The sender is trying to connect.
  Connecting,
  /// The sender is connected.
  Connected,
  /// The sender sent `DataPush` packet and is waiting for `DataPushAck` packet.
  Pushing,
  /// The sender is sending `Data`.
  Sending,
  /// The sender sent `DataStop` packet and is waiting for `DataStopAck` packet.
  Stopping,
  /// The sender is trying to disconnect.
  Disconnecting,
};

class Sender {
 private:
  /// The pending packet queue.
  ConcurrentQueue<SequenceNumber> pending_queue_;

  /// The concurrent timer.
  ConcurrentTimer<SequenceNumber> timer_;

  /// The packet pool
  std::unordered_map<SequenceNumber, Packet> packet_pool_;
  std::mutex packet_pool_mutex_;

  /// The retry count.
  std::unordered_map<SequenceNumber, size_t> retry_count_;

  /// The acked sequence number set.
  std::unordered_set<SequenceNumber> acked_seq_set_;
  std::mutex acked_seq_set_mutex_;

  /// The window, also record the sequence number.
  ///
  /// Window in sender represents the usable sequence number.
  /// [upper, lower + max_window_size) is the usable sequence number.
  /// [lower, upper) is the used sequence number.
  /// Initialized with lower = upper = 0 (no seq is used.)
  /// If lower == upper, the window is empty.
  /// If seq < lower, the seq is already acked.
  ConcurrentWindow window_;

  /// The maximum retry count.
  size_t max_retry_count_;

  /// The actual maximum retry count.
  size_t actual_max_retry_count_ = 0;

  SenderState state_;
  std::mutex state_mutex_;
  std::condition_variable state_cvar_;

  /// Time out in milliseconds.
  std::chrono::milliseconds timeout_;

  /// Send thread.
  std::thread send_thread_;

  /// Recv thread.
  std::thread recv_thread_;

  /// Timeout thread.
  std::thread timeout_thread_;

  /// Running indicator.
  std::atomic<bool> running_;

  bool selective_repeat_ = false;

  bool fast_retransmit_ = false;

  /// Ack duplicate times.
  std::atomic<size_t> ack_dup_times = 0;

  /// The socket channel.
  Socket channel_;

  SocketAddr peer_addr_;

  Logger logger_;

  /// Add the packet into the send buffer.
  void send_packet(PacketKind kind, std::vector<std::byte>&& payload);

  /// Send handler.
  void send_handler();

  /// Receive handler.
  void recv_handler();

  /// Timeout handler.
  void timeout_handler();

 public:
  Sender(Socket&& channel)
    : channel_(std::move(channel)), logger_(std::cout, "Sender") {}

  /// Set if the sender uses selective repeat.
  ///
  /// If this is not set, the sender uses go-back-n.
  void set_selective_repeat(bool selective_repeat) {
    selective_repeat_ = selective_repeat;
  }

  /// Set the ack timeout.
  void set_timeout(std::chrono::milliseconds timeout) { timeout_ = timeout; }

  /// Set maximum window size
  void set_max_window_size(size_t max_window_size) {
    window_.set_max_window_size(max_window_size);
  }

  /// Set maximum retry count
  void set_max_retry_count(size_t max_retry_count) {
    max_retry_count_ = max_retry_count;
  }

  void set_fast_retransmit(bool fast_retransmit) {
    fast_retransmit_ = fast_retransmit;
  }

  /// Initialize the sender.
  std::error_condition init();

  /// Connect to the peer (receiver).
  std::error_condition connect(const SocketAddr& addr);

  /// Send data.
  std::error_condition send(std::istream& is);

  /// Stop the sender.
  std::error_condition stop();

  /// Disconnect from the peer.
  std::error_condition disconnect();

  /// Cleanup the sender.
  std::error_condition cleanup() {
    auto errc = channel_.cleanup();
    return errc;
  }
};

}  // namespace nkurdt

#endif  // NKURDT_SENDER_H_
