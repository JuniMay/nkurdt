#include "packet.hpp"

namespace nkurdt {

uint16_t calc_checksum(const std::vector<std::byte>& bytes) {
  // same method as UDP checksum
  uint32_t sum = 0;

  for (size_t i = 0; i < bytes.size() - 1; i += 2) {
    uint16_t word = static_cast<uint16_t>(bytes[i]) |
                    (static_cast<uint16_t>(bytes[i + 1]) << 8);
    sum += word;
  }

  if (bytes.size() % 2 == 1) {
    uint16_t word = static_cast<uint16_t>(bytes[bytes.size() - 1]);
    sum += word;
  }

  while (sum >> 16) {
    sum = (sum & 0xFFFF) + (sum >> 16);
  }

  return ~sum;
}

std::vector<std::byte> Packet::to_bytes() const {
  std::vector<std::byte> bytes;
  bytes.reserve(
    12 + ((this->payload_.size() == 0 || this->payload_.size() > 0xFFFF)
            ? 0
            : 2 + this->payload_.size())
  );

  // magic
  bytes.push_back(std::byte('N'));
  bytes.push_back(std::byte('K'));

  // version, little endian
  bytes.push_back(std::byte(version_ & 0xFF));
  bytes.push_back(std::byte((version_ >> 8) & 0xFF));

  // checksum is left blank for now
  bytes.push_back(std::byte(0));
  bytes.push_back(std::byte(0));

  // kind
  bytes.push_back(std::byte(static_cast<uint8_t>(kind_)));
  // reserved
  bytes.push_back(std::byte(0));

  // seq, little endian
  bytes.push_back(std::byte(seq_.get() & 0xFF));
  bytes.push_back(std::byte((seq_.get() >> 8) & 0xFF));
  bytes.push_back(std::byte((seq_.get() >> 16) & 0xFF));
  bytes.push_back(std::byte((seq_.get() >> 24) & 0xFF));

  if (this->payload_.size() > 0 && this->payload_.size() <= 0xFFFF) {
    // payload size, little endian
    bytes.push_back(std::byte(payload_.size() & 0xFF));
    bytes.push_back(std::byte((payload_.size() >> 8) & 0xFF));

    // payload
    bytes.insert(bytes.end(), payload_.begin(), payload_.end());
  }

  // checksum, little endian
  uint16_t checksum = calc_checksum(bytes);
  bytes[4] = std::byte(checksum & 0xFF);
  bytes[5] = std::byte((checksum >> 8) & 0xFF);

  return bytes;
}

std::string to_string(PacketKind kind) {
  switch (kind) {
    case nkurdt::PacketKind::None:
      return "None";
    case nkurdt::PacketKind::Data:
      return "Data";
    case nkurdt::PacketKind::DataAck:
      return "DataAck";
    case nkurdt::PacketKind::DataPush:
      return "DataPush";
    case nkurdt::PacketKind::DataPushAck:
      return "DataPushAck";
    case nkurdt::PacketKind::DataStop:
      return "DataStop";
    case nkurdt::PacketKind::DataStopAck:
      return "DataStopAck";
    case nkurdt::PacketKind::ConnReq:
      return "ConnReq";
    case nkurdt::PacketKind::ConnAck:
      return "ConnAck";
    case nkurdt::PacketKind::DisconnReq:
      return "DisconnReq";
    case nkurdt::PacketKind::DisconnAck:
      return "DisconnAck";
  }

  return "Unknown";
}

}  // namespace nkurdt