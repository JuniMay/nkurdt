#include "receiver.hpp"
#include <utility>
#include "packet.hpp"

namespace nkurdt {

Generator<bool> Receiver::recv() {
  this->state_ = ReceiverState::Idle;
  this->running_ = true;
  this->window_.first = 0;
  this->window_.second = (uint32_t)this->max_window_size_;

  while (this->running_) {
    auto [errc, data, addr] = this->channel_.recv();
    if (errc) {
      if (this->channel_.is_fatal_error(errc)) {
        this->running_ = false;
        this->logger_.error(std::format("recv error: {}", errc.message()));
        break;
      } else {
        // this is async or timeout error, continue
        continue;
      }
    }

    auto packet = Packet::from_bytes(data);
    if (!packet) {
      this->logger_.error("invalid packet");
      continue;
    }

    if (packet->is_ack()) {
      this->logger_.warn("received ack packet");
      continue;
    }

    auto ack_kind = get_ack_kind(packet->kind()).value();

    if (packet.value().seq() < this->window_.first && this->state_ != ReceiverState::Idle) {
      // this is a duplicate packet, reply with ack
      auto seq = this->selective_repeat_ ? packet->seq() : this->window_.first;
      auto ack_packet = Packet(NKURDT_VERSION, ack_kind, seq, {});

      this->logger_.info(std::format(
        "received duplicate packet {}, reply with ack {}",
        packet.value().seq().get(), seq.get()
      ));

      if (this->peer_addr_.has_value()) {
        this->channel_.send(this->peer_addr_.value(), ack_packet.to_bytes());
      } else {
        this->logger_.error("peer address is not set");
      }

      continue;
    }

    if (packet.value().seq() >= this->window_.second && this->state_ != ReceiverState::Idle) {
      // this is an out of order packet, some packet is lost.
      this->logger_.warn(std::format(
        "received packet with larger seq number {}", packet.value().seq().get()
      ));

      // if this is gbn, reply with previous ack so the sender can skip the
      // timer.
      if (!this->selective_repeat_) {
        this->logger_.info(std::format(
          "reply with ack of {} (duplicated ack)", this->window_.first.get()
        ));
        auto ack_packet =
          Packet(NKURDT_VERSION, ack_kind, this->window_.first, {});

        if (this->peer_addr_.has_value()) {
          this->channel_.send(this->peer_addr_.value(), ack_packet.to_bytes());
        } else {
          this->logger_.error("peer address is not set");
        }
      }

      continue;
    }

    bool reply_ack = false;
    bool is_data_stop = false;
    SequenceNumber seq = packet->seq();

    this->logger_.info(
      std::format("received packet {}", packet.value().seq().get())
    );

    switch (this->state_) {
      case ReceiverState::Idle: {
        if (packet->kind() == PacketKind::ConnReq) {
          reply_ack = true;
          this->peer_addr_ = addr;
          this->state_ = ReceiverState::Connected;
          // also sync the window lower
          this->window_.first = packet->seq();
          this->window_.second =
            packet->seq() + (uint32_t)this->max_window_size_;
        } else {
          this->logger_.warn(std::format(
            "received invalid packet {} at Idle state.",
            to_string(packet->kind())
          ));
        }
        break;
      }
      case ReceiverState::Receiving: {
        switch (packet->kind()) {
          case PacketKind::Data: {
            // this is a data packet, no state transition is needed
            reply_ack = true;
            break;
          }
          case PacketKind::DataStop: {
            reply_ack = true;
            is_data_stop = true;
            this->state_ = ReceiverState::Connected;
            break;
          }
          default: {
            this->logger_.warn(std::format(
              "received invalid packet {} at Receiving state.",
              to_string(packet->kind())
            ));
            break;
          }
        }
        break;
      }
      case ReceiverState::Connected: {
        if (packet->kind() == PacketKind::DataPush) {
          reply_ack = true;
          this->state_ = ReceiverState::Receiving;
        } else if (packet->kind() == PacketKind::DisconnReq) {
          reply_ack = true;
          this->state_ = ReceiverState::Idle;
        } else {
          this->logger_.warn(std::format(
            "received invalid packet {} at Connected state.",
            to_string(packet->kind())
          ));
        }
        break;
      }
    }

    // For selective repeat, recv buffer is useful if the packet is out of order
    // or lost. But for go back n, if the packet is out of order, the ack will
    // be replied immediately. And the packet will be insert and erased from the
    // recv buffer. The buffer here is just for compatibility with selective
    // repeat.
    this->recv_buffer_.insert(std::move(packet.value()));

    // try to slide window, because set is ordered, we can just check the first
    // element in the set.
    while (!this->recv_buffer_.empty()) {
      auto first = this->recv_buffer_.begin();
      if (first->seq() == this->window_.first) {
        // slide window
        this->window_.first.update();
        this->window_.second.update();
        this->logger_.info(std::format(
          "slide window upper to [{}, {})", this->window_.first.get(),
          this->window_.second.get()
        ));
        // write data to ostream
        if (this->os_) {
          this->os_->write(
            (const char*)first->payload().data(), first->payload().size()
          );
          this->os_->flush();
        } else {
          this->logger_.error("ostream is not set");
        }
        this->recv_buffer_.erase(first);
      } else {
        break;
      }
    }

    if (this->selective_repeat_) {
      // selective repeat: just reply ack of this packet
      auto ack_packet =
        Packet(NKURDT_VERSION, ack_kind, packet.value().seq(), {});

      if (this->peer_addr_.has_value()) {
        this->channel_.send(this->peer_addr_.value(), ack_packet.to_bytes());
      } else {
        this->logger_.error("peer address is not set");
      }
    } else {
      // go back n: reply the ack of the first packet in the window (which is an
      // open interval)
      auto ack_packet =
        Packet(NKURDT_VERSION, ack_kind, this->window_.first, {});

      if (this->peer_addr_.has_value()) {
        this->channel_.send(this->peer_addr_.value(), ack_packet.to_bytes());
      } else {
        this->logger_.error("peer address is not set");
      }
    }

    if (is_data_stop && seq < this->window_.first) {
      co_yield true;
    }

    if (this->state_ == ReceiverState::Idle && seq < this->window_.first) {
      break;
    }
  }

  co_yield false;
}

}  // namespace nkurdt
