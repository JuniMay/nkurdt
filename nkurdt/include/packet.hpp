
#ifndef NKURDT_PACKET_H_
#define NKURDT_PACKET_H_

#include <format>
#include <iostream>
#include <optional>
#include <utility>
#include <vector>

#define NKURDT_VERSION 1
#define NKURDT_MAX_PACKET_SIZE 8192
#define NKURDT_MAX_PAYLOAD_SIZE (NKURDT_MAX_PACKET_SIZE - 14)

namespace nkurdt {

/// Frame kinds
///
/// Note that for any ack in GBN, sequence number of ack is the next expected
/// sequence number, (which is not received yet).
enum class PacketKind : uint8_t {
  /// A empty frame.
  None = 0,
  /// A data frame.
  Data,
  /// A ack frame for data.
  DataAck,
  /// Data push frame.
  DataPush,
  /// Data push ack.
  DataPushAck,
  /// Data stop frame.
  DataStop,
  /// Data stop ack.
  DataStopAck,
  /// A connection request frame.
  ConnReq,
  /// A ack frame for connection request.
  ConnAck,
  /// A disconnection request frame.
  DisconnReq,
  /// A ack frame for disconnection request.
  DisconnAck,
};

static inline std::optional<PacketKind> get_ack_kind(PacketKind kind) {
  if (kind == PacketKind::Data) {
    return PacketKind::DataAck;
  } else if (kind == PacketKind::DataPush) {
    return PacketKind::DataPushAck;
  } else if (kind == PacketKind::DataStop) {
    return PacketKind::DataStopAck;
  } else if (kind == PacketKind::ConnReq) {
    return PacketKind::ConnAck;
  } else if (kind == PacketKind::DisconnReq) {
    return PacketKind::DisconnAck;
  } else {
    return std::nullopt;
  }
}

/// A encapsulation of a sequence number.
class SequenceNumber {
 public:
  SequenceNumber() : inner_(0) {}

  SequenceNumber(uint32_t seq) : inner_(seq) {}

  SequenceNumber(const SequenceNumber& other) : inner_(other.inner_) {}

  SequenceNumber& operator=(const SequenceNumber& other) {
    this->inner_ = other.inner_;
    return *this;
  }

  bool operator==(const SequenceNumber& other) const {
    return this->inner_ == other.inner_;
  }

  bool operator!=(const SequenceNumber& other) const {
    return this->inner_ != other.inner_;
  }

  bool operator<(const SequenceNumber& other) const {
    return this->inner_ < other.inner_;
  }

  bool operator<=(const SequenceNumber& other) const {
    return this->inner_ <= other.inner_;
  }

  bool operator>(const SequenceNumber& other) const {
    return this->inner_ > other.inner_;
  }

  bool operator>=(const SequenceNumber& other) const {
    return this->inner_ >= other.inner_;
  }

  SequenceNumber operator-(const SequenceNumber& other) const {
    return this->inner_ - other.inner_;
  }

  SequenceNumber operator+(const SequenceNumber& other) const {
    return this->inner_ + other.inner_;
  }

  /// Get the sequence number.
  uint32_t get() const { return inner_; }

  /// Update the sequence number.
  void update() {
#if 0
    // alternating bit
    this->inner_ ^= 1;
#endif
    this->inner_++;
  }

  void inv_update() {
#if 0
    // alternating bit
    this->inner_ ^= 1;
#endif
    this->inner_--;
  }

 private:
  uint32_t inner_ = 0;
};

/// Calculate the checksum of the given bytes.
uint16_t calc_checksum(const std::vector<std::byte>& bytes);

/// A packet in the network.
///
/// A packet is a class recording necessary information.
/// It can be converted to a vector of bytes and vice versa.
///
/// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
/// |      'N'      |      'K'      |            Version            |
/// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
/// |            Checksum           |      Kind     |   Reserved    |
/// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
/// |                              Seq                              |
/// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
/// |       Length (optional)       |   Rest is payload (optional)
/// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
class Packet {
 private:
  /// Version of the packet
  uint16_t version_;
  /// Kind of the packet
  PacketKind kind_;
  /// Sequence number of the packet
  SequenceNumber seq_;
  /// Payload of the packet
  std::vector<std::byte> payload_;

 public:
  Packet(uint16_t version) : version_(version) {}

  Packet(
    uint16_t version,
    PacketKind kind,
    SequenceNumber seq,
    std::vector<std::byte>&& payload
  )
    : version_(version), kind_(kind), seq_(seq), payload_(std::move(payload)) {}

  static std::optional<Packet> from_bytes(const std::vector<std::byte>& bytes) {
    if (calc_checksum(bytes) != 0) {
      std::cout << "invalid checksum!" << std::endl;
      // The checksum is not correct.
      return std::nullopt;
    }

    uint16_t version;

    if (bytes.size() < 12) {
      // The size of the header is 12.
      return std::nullopt;
    }

    // verify the magic
    if (bytes[0] != std::byte('N') || bytes[1] != std::byte('K')) {
      return std::nullopt;
    }

    // get the version, little endian
    version =
      static_cast<uint16_t>(bytes[2]) | (static_cast<uint16_t>(bytes[3]) << 8);

    // checksum is not necessary

    // get the frame kind
    PacketKind kind = static_cast<PacketKind>(bytes[6]);

    // reserved zero
    if (bytes[7] != std::byte(0)) {
      return std::nullopt;
    }

    SequenceNumber seq = SequenceNumber(
      static_cast<uint32_t>(bytes[8]) | (static_cast<uint32_t>(bytes[9]) << 8) |
      (static_cast<uint32_t>(bytes[10]) << 16) |
      (static_cast<uint32_t>(bytes[11]) << 24)
    );

    std::vector<std::byte> payload;

    if (bytes.size() > 12) {
      // get the payload size, little endian
      size_t payload_size =
        static_cast<size_t>(bytes[12]) | (static_cast<size_t>(bytes[13]) << 8);

      if (bytes.size() < 14 + payload_size) {
        // The size of the packet is too small
        return std::nullopt;
      }

      payload = std::vector<std::byte>(
        bytes.begin() + 14, bytes.begin() + 14 + payload_size
      );
    }

    return Packet(version, kind, seq, std::move(payload));
  }

  std::vector<std::byte> to_bytes() const;

  /// Get the version of the packet.
  uint16_t version() const { return version_; }

  const SequenceNumber& seq() const { return seq_; }

  PacketKind kind() const { return kind_; }

  const std::vector<std::byte>& payload() const { return payload_; }

  bool is_ack() const {
    return kind_ == PacketKind::DataAck || kind_ == PacketKind::DataPushAck ||
           kind_ == PacketKind::DataStopAck || kind_ == PacketKind::ConnAck ||
           kind_ == PacketKind::DisconnAck;
  }

  bool operator==(const Packet& other) const {
    return this->seq_ == other.seq_;
  }

  bool operator!=(const Packet& other) const {
    return this->seq_ != other.seq_;
  }

  bool operator<(const Packet& other) const { return this->seq_ < other.seq_; }

  bool operator<=(const Packet& other) const {
    return this->seq_ <= other.seq_;
  }

  bool operator>(const Packet& other) const { return this->seq_ > other.seq_; }

  bool operator>=(const Packet& other) const {
    return this->seq_ >= other.seq_;
  }
};

std::string to_string(nkurdt::PacketKind kind);

}  // namespace nkurdt

namespace std {

/// Hash function for SequenceNumber
template <>
struct hash<nkurdt::SequenceNumber> {
  size_t operator()(const nkurdt::SequenceNumber& seq) const {
    return hash<uint32_t>()(seq.get());
  }
};

/// Hash function for Packet
template <>
struct hash<nkurdt::Packet> {
  size_t operator()(const nkurdt::Packet& packet) const {
    return hash<uint32_t>()(packet.seq().get());
  }
};

}  // namespace std

#endif  // NKURDT_PACKET_H_
