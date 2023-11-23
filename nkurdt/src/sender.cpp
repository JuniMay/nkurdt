#include <algorithm>
#include <system_error>
#include <utility>

#include "packet.hpp"
#include "sender.hpp"

namespace nkurdt {

using namespace std::chrono_literals;

void Sender::send_packet(PacketKind kind, std::vector<std::byte>&& payload) {
  // check the window or just wait
  this->logger_.debug("waiting for window to be available");

  while (true) {
    bool window_avalaible = this->window_.wait_window_available(1000ms);
    if (window_avalaible) {
      break;
    } else {
      if (!this->running_) {
        return;
      } else {
        continue;
      }
    }
  }

  this->logger_.debug("window is available");
  this->logger_.debug("getting upper bound");
  auto seq = this->window_.upper_bound();
  // update the upper bound so the previous upper bound is used.
  this->logger_.debug("updating upper bound");
  this->window_.update_upper_bound();

  // make packet
  auto packet = Packet(NKURDT_VERSION, kind, seq, std::move(payload));

  // insert into the pool and pending queue
  this->logger_.debug("inserting packet into pool");

  std::unique_lock<std::mutex> lock(this->packet_pool_mutex_);
  this->packet_pool_.emplace(seq, std::move(packet));

  this->logger_.debug(std::format("pending packet {}", seq.get()));
  this->pending_queue_.push(seq);
}

void Sender::send_handler() {
  while (this->running_) {
    auto seq = this->pending_queue_.wait_pop(1000ms);
    if (seq == std::nullopt) {
      continue;
    }

    std::unique_lock<std::mutex> packet_pool_lock(this->packet_pool_mutex_);
    std::unique_lock<std::mutex> acked_seq_set_lock(this->acked_seq_set_mutex_);

    if (this->selective_repeat_ && this->acked_seq_set_.contains(seq.value())) {
      // for selective repeat, if the packet is acked, just ignore it
      if (this->retry_count_.contains(seq.value())) {
        this->actual_max_retry_count_ = std::max(
          this->actual_max_retry_count_, this->retry_count_.at(seq.value())
        );
        this->retry_count_.erase(seq.value());
      }
      this->packet_pool_.erase(seq.value());
      this->timer_.remove_event(seq.value());
      continue;
    }

    if (seq.value() < this->window_.lower_bound()) {
      // for go back n, if the packet is acked, just ignore it
      // also, this is for cleaning up in selective repeat protocol
      this->logger_.debug(std::format(
        "packet {} is not in the window, cleaning up", seq.value().get()
      ));
      if (this->retry_count_.contains(seq.value())) {
        this->actual_max_retry_count_ = std::max(
          this->actual_max_retry_count_, this->retry_count_.at(seq.value())
        );
        this->retry_count_.erase(seq.value());
      }
      this->packet_pool_.erase(seq.value());
      this->timer_.remove_event(seq.value());
      continue;
    }

    auto lower_bound = this->window_.lower_bound();
    auto upper_bound = this->window_.upper_bound();

    if (lower_bound == upper_bound) {
      this->logger_.debug(std::format(
        "window is empty [{}, {}), continue...", lower_bound.get(),
        upper_bound.get()
      ));
      continue;
    }

    // send the packet and register into the timer
    if (this->retry_count_.contains(seq.value())) {
      if (retry_count_.at(seq.value()) >= this->max_retry_count_) {
        this->logger_.error(std::format(
          "max retry count reached for packet {}", seq.value().get()
        ));
        // if max retry count reached, change state according to the current
        // state
        std::unique_lock<std::mutex> lock(this->state_mutex_);

        if (this->state_ == SenderState::Stopping || this->state_ == SenderState::Pushing) {
          this->state_ = SenderState::Connected;
          this->state_cvar_.notify_all();
          continue;
        } else if (this->state_ == SenderState::Connecting || this->state_ == SenderState::Disconnecting) {
          this->state_ = SenderState::Idle;
          this->state_cvar_.notify_all();
          continue;
        } else {
          this->running_ = false;
          break;
        }
      } else {
        // increase the retry count
        retry_count_.at(seq.value())++;
      }
    } else {
      // otherwise, insert the retry count
      retry_count_.emplace(seq.value(), 0);
    }

    if (this->packet_pool_.contains(seq.value())) {
      this->logger_.info(std::format("sent packet {}", seq.value().get()));
      this->channel_.send(
        this->peer_addr_, this->packet_pool_.at(seq.value()).to_bytes()
      );
      this->timer_.register_event(seq.value(), this->timeout_);
    } else {
      this->logger_.warn(
        std::format("packet {} is not in the pool", seq.value().get())
      );
      this->timer_.remove_event(seq.value());
    }
  }
}

void Sender::recv_handler() {
  while (this->running_) {
    auto [errc, bytes, addr] = this->channel_.recv();
    if (errc) {
      if (this->channel_.is_fatal_error(errc)) {
        this->logger_.error(std::format("fatal error: {}", errc.message()));
        // for fatal error, just panic.
        this->running_ = false;
      } else {
        // async error or timeout, check running again.
        continue;
      }
    }

    // we have a packet to process, process the packet
    auto packet = Packet::from_bytes(bytes);
    if (!packet.has_value()) {
      this->logger_.warn("received invalid packet");
      continue;
    }

    switch (this->state_) {
      case SenderState::Idle: {
        this->logger_.warn("received packet in idle state");
        break;
      }
      case SenderState::Connecting: {
        if (packet->kind() == PacketKind::ConnAck) {
          {
            std::lock_guard<std::mutex> lock(this->state_mutex_);
            this->state_ = SenderState::Connected;
          }
          this->state_cvar_.notify_all();
          this->logger_.info("connected");
        } else {
          this->logger_.warn(std::format(
            "received packet {} in connecting state", to_string(packet->kind())
          ));
        }
        break;
      }
      case SenderState::Connected: {
        this->logger_.warn(std::format(
          "received packet {} in connected state", to_string(packet->kind())
        ));
        break;
      }
      case SenderState::Pushing: {
        if (packet->kind() == PacketKind::DataPushAck) {
          {
            std::lock_guard<std::mutex> lock(this->state_mutex_);
            this->state_ = SenderState::Sending;
          }
          this->state_cvar_.notify_all();
          this->logger_.info("received push ack");
        } else {
          this->logger_.warn(std::format(
            "received packet {} in pushing state", to_string(packet->kind())
          ));
        }
        break;
      }
      case SenderState::Sending: {
        if (packet->kind() == PacketKind::DataAck) {
          this->logger_.info(
            std::format("received data ack {}", packet->seq().get())
          );
        } else {
          this->logger_.warn(std::format(
            "received packet {} in sending state", to_string(packet->kind())
          ));
        }
        break;
      }
      case SenderState::Stopping: {
        if (packet->kind() == PacketKind::DataStopAck) {
          {
            std::lock_guard<std::mutex> lock(this->state_mutex_);
            this->state_ = SenderState::Connected;
          }
          this->state_cvar_.notify_all();
          this->logger_.info("received stop ack");
        } else {
          this->logger_.warn(std::format(
            "received packet {} in stopping state", to_string(packet->kind())
          ));
        }
        break;
      }
      case SenderState::Disconnecting: {
        if (packet->kind() == PacketKind::DisconnAck) {
          {
            std::lock_guard<std::mutex> lock(this->state_mutex_);
            this->state_ = SenderState::Idle;
          }
          this->state_cvar_.notify_all();
          this->logger_.info("disconnected");
        } else {
          this->logger_.warn(std::format(
            "received packet {} in disconnecting state",
            to_string(packet->kind())
          ));
        }
        break;
      }
    }

    if (!packet->is_ack()) {
      this->logger_.warn("received non-ack packet");
    }
    std::unique_lock<std::mutex> lock(this->acked_seq_set_mutex_);
    this->acked_seq_set_.insert(packet->seq());
    if (!this->selective_repeat_) {
      if (this->window_.lower_bound() == packet->seq()) {
        // there is a duplicate ack, out of order or packet loss might occured.
        // for fast retransmission.
        if (this->ack_dup_times == 0) {
          this->logger_.warn(std::format(
            "received first ack {}, not retransmit yet", packet->seq().get()
          ));
        } else if (this->ack_dup_times == 2) {
          // only retransmit once.
          if (this->fast_retransmit_) {
            // this is a duplicated ack, a loss packet might have occured.
            // TCP uses 3 duplicated acks to trigger fast retransmission, but
            // here just use a simpler mechanism.
            auto lower_bound = this->window_.lower_bound();
            auto upper_bound = this->window_.upper_bound();
            this->logger_.warn(std::format(
              "received duplicate ack {}, retransmit", packet->seq().get()
            ));
            // resend the rest of the window without waiting for timer
            this->pending_queue_.lock();
            for (auto seq = lower_bound; seq < upper_bound; seq.update()) {
              this->pending_queue_.unsafe_push(seq);
            }
            this->pending_queue_.unlock_and_notify();
          } else {
            this->logger_.warn(std::format(
              "received duplicate ack {}, but fast retransmission disabled.",
              packet->seq().get()
            ));
          }
        } else {
          this->logger_.warn(std::format(
            "received duplicate ack {} again, not retransmit yet",
            packet->seq().get()
          ));
          if (ack_dup_times >= this->window_.max_window_size()) {
            ack_dup_times = 0;
          }
        }
        this->ack_dup_times += 1;
      } else if (this->window_.lower_bound() < packet->seq()) {
        // reset the dup times.
        this->ack_dup_times = 0;
        // update window.lower_bound = packet.seq
        this->window_.set_lower_bound(packet->seq());
        this->logger_.info(std::format(
          "slide window to [{}, {})", this->window_.lower_bound().get(),
          this->window_.upper_bound().get()
        ));
      }
      this->acked_seq_set_.erase(packet->seq());
    } else {
      // selective repeat
      while (this->acked_seq_set_.contains(this->window_.lower_bound())) {
        this->acked_seq_set_.erase(this->window_.lower_bound());
        this->window_.update_lower_bound();
        this->logger_.info(std::format(
          "slide window to [{}, {})", this->window_.lower_bound().get(),
          this->window_.upper_bound().get()
        ));
      }
    }
  }
}

void Sender::timeout_handler() {
  while (running_) {
    bool has_timed_out = this->timer_.wait_for_timed_out(1000ms);
    if (!has_timed_out) {
      continue;
    }

    auto upper_bound = this->window_.upper_bound();

    // re-pend the packet
    this->pending_queue_.lock();
    this->window_.lock();
    this->timer_.map_timed_out_events(
      [this, upper_bound](SequenceNumber seq) {
        // no matter the seq is within the window or not, just re-pend it.
        // the send handler will clean up the packet if it is not in the window.
        this->pending_queue_.unsafe_push(seq);
        this->logger_.debug(
          std::format("packet {} timed out, re-pending", seq.get())
        );
      },
      true
    );
    this->window_.unlock();
    this->pending_queue_.unlock_and_notify();
  }
}

std::error_condition Sender::init() {
  this->running_ = true;

  this->state_ = SenderState::Idle;
  this->timer_.start();

  this->window_.set_window(0, 0);

  this->send_thread_ = std::thread(&Sender::send_handler, this);
  this->recv_thread_ = std::thread(&Sender::recv_handler, this);
  this->timeout_thread_ = std::thread(&Sender::timeout_handler, this);

  return std::error_condition();
}

std::error_condition Sender::connect(const SocketAddr& addr) {
  std::unique_lock<std::mutex> state_lock(this->state_mutex_);

  this->state_ = SenderState::Connecting;
  this->peer_addr_ = addr;
  this->send_packet(PacketKind::ConnReq, {});

  this->state_cvar_.wait(state_lock, [this] {
    return this->state_ == SenderState::Connected ||
           this->state_ == SenderState::Idle;
  });

  if (this->state_ == SenderState::Idle) {
    return std::make_error_condition(std::errc::timed_out);
  }

  return std::error_condition{};
}

std::error_condition Sender::send(std::istream& is) {
  this->actual_max_retry_count_ = 0;

  std::unique_lock<std::mutex> state_lock(this->state_mutex_);

  this->state_ = SenderState::Pushing;
  this->send_packet(PacketKind::DataPush, {});

  // wait for sending
  this->state_cvar_.wait(state_lock, [this] {
    return this->state_ == SenderState::Sending ||
           this->state_ == SenderState::Connected;
  });

  if (this->state_ == SenderState::Connected) {
    // max retry count exceeded.
    return std::make_error_condition(std::errc::timed_out);
  }

  state_lock.unlock();

  auto st = std::chrono::steady_clock::now();
  size_t total_bytes = 0;

  // send data from is
  while (is) {
    std::vector<std::byte> payload(NKURDT_MAX_PAYLOAD_SIZE);
    is.read(reinterpret_cast<char*>(payload.data()), payload.size());

    auto bytes = is.gcount();
    total_bytes += bytes;
    payload.resize(bytes);
    this->send_packet(PacketKind::Data, std::move(payload));
  }

  // wait for all packets to be acked
  while (true) {
    this->logger_.debug("waiting for window to be zero");
    bool zero = this->window_.wait_window_zero(1000ms);
    if (zero) {
      break;
    } else {
      if (!this->running_) {
        return std::make_error_condition(std::errc::operation_canceled);
      } else {
        continue;
      }
    }
  }

  this->logger_.debug("window is zero");

  auto ed = std::chrono::steady_clock::now();

  auto duration =
    std::chrono::duration_cast<std::chrono::milliseconds>(ed - st);

  /// byte devide by ms, equals to KB/s
  auto throughput = total_bytes / duration.count();

  state_lock.lock();
  this->state_ = SenderState::Stopping;
  this->send_packet(PacketKind::DataStop, {});
  this->state_cvar_.wait(state_lock, [this] {
    return this->state_ == SenderState::Connected;
  });

  state_lock.unlock();

  this->logger_.info("all packets are sent.");
  this->logger_.info(std::format(
    "total bytes: {}, duration: {} ms, throughput: {} KB/s", total_bytes,
    duration.count(), throughput
  ));
  this->logger_.info(
    std::format("actual max retry count: {}", this->actual_max_retry_count_)
  );

  return std::error_condition{};
}

std::error_condition Sender::stop() {
  this->running_ = false;

  this->logger_.debug("stopping timer...");
  this->timer_.stop();

  this->logger_.debug("joining send thread...");
  this->send_thread_.join();

  this->logger_.debug("joining recv thread...");
  this->recv_thread_.join();

  this->logger_.debug("joining timeout thread...");
  this->timeout_thread_.join();

  this->logger_.debug("all threads joined");
  return std::error_condition{};
}

std::error_condition Sender::disconnect() {
  std::unique_lock<std::mutex> state_lock(this->state_mutex_);

  if (this->state_ == SenderState::Idle) {
    return std::error_condition{};
  }

  this->state_ = SenderState::Disconnecting;
  this->send_packet(PacketKind::DisconnReq, {});

  this->state_cvar_.wait(state_lock, [this] {
    return this->state_ == SenderState::Idle;
  });

  return std::error_condition{};
}

}  // namespace nkurdt
