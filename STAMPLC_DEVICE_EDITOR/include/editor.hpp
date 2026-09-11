#pragma once
#include <cstdint>
#include <mcprotocol/serial/types.hpp>

namespace editor {
using mcprotocol::serial::DeviceCode;
enum class Kind { Bit, Word, DWord };
struct Device { const char* name; DeviceCode code; Kind kind; unsigned radix; };
constexpr Device devices[] = {
    {"X",DeviceCode::X,Kind::Bit,8}, {"Y",DeviceCode::Y,Kind::Bit,8},
    {"M",DeviceCode::M,Kind::Bit,10}, {"L",DeviceCode::L,Kind::Bit,10},
    {"F",DeviceCode::F,Kind::Bit,10}, {"B",DeviceCode::B,Kind::Bit,16},
    {"D",DeviceCode::D,Kind::Word,10}, {"W",DeviceCode::W,Kind::Word,16},
    {"R",DeviceCode::R,Kind::Word,10}, {"SM",DeviceCode::SM,Kind::Bit,10},
    {"SD",DeviceCode::SD,Kind::Word,10}, {"SB",DeviceCode::SB,Kind::Bit,16},
    {"SW",DeviceCode::SW,Kind::Word,16}, {"Z",DeviceCode::Z,Kind::Word,10},
    {"LZ",DeviceCode::LZ,Kind::DWord,10}, {"TS",DeviceCode::TS,Kind::Bit,10},
    {"TC",DeviceCode::TC,Kind::Bit,10}, {"TN",DeviceCode::TN,Kind::Word,10},
    {"STS",DeviceCode::STS,Kind::Bit,10}, {"STC",DeviceCode::STC,Kind::Bit,10},
    {"STN",DeviceCode::STN,Kind::Word,10}, {"CS",DeviceCode::CS,Kind::Bit,10},
    {"CC",DeviceCode::CC,Kind::Bit,10}, {"CN",DeviceCode::CN,Kind::Word,10},
    {"LCS",DeviceCode::LCS,Kind::Bit,10}, {"LCC",DeviceCode::LCC,Kind::Bit,10},
    {"LCN",DeviceCode::LCN,Kind::DWord,10}};
constexpr unsigned deviceCount = sizeof(devices) / sizeof(devices[0]);
enum class Mode { Device, Address, Value };

// loopだけから操作する状態。受け付けた値変更は順番に1回ずつ送信する。
struct State {
  unsigned selected = 6;
  std::uint32_t address = 100, generation = 0;
  std::uint32_t savedAddresses[deviceCount] {}; // 起動中だけデバイスごとの番地を保持。
  Mode mode = Mode::Device;
  std::int64_t value = 0;
  bool valid = false;
  static constexpr unsigned capacity = 8;
  std::int64_t writes[capacity] {};
  unsigned head = 0, count = 0;
  const Device& device() const { return devices[selected]; }
  void cycleMode() { mode = static_cast<Mode>((static_cast<unsigned>(mode) + 1) % 3); }
  void invalidate() { valid = false; ++generation; }
  void fail() { count = 0; head = 0; invalidate(); }
  bool select(int delta) {
    if (count) return false; // 未完了の書き込み先を変えない。
    savedAddresses[selected] = address;
    selected = (selected + deviceCount + delta) % deviceCount;
    address = savedAddresses[selected];
    invalidate();
    return true;
  }
  bool moveAddress(int delta) {
    if (count || (delta < 0 && address == 0) || (delta > 0 && address == 0xFFFFFFU)) return false;
    address += delta;
    invalidate();
    return true;
  }
  bool edit(int delta) {
    if (!valid || count == capacity) return false;
    const auto kind = device().kind;
    const std::int64_t minimum = kind == Kind::Bit ? 0 : kind == Kind::Word ? -32768 : -2147483648LL;
    const std::int64_t maximum = kind == Kind::Bit ? 1 : kind == Kind::Word ? 32767 : 2147483647LL;
    const auto next = value + delta;
    if (next < minimum || next > maximum) return false;
    value = next;
    writes[(head + count) % capacity] = value;
    ++count;
    return true;
  }
  void written() { head = (head + 1) % capacity; --count; }
  void received(std::uint32_t requestGeneration, std::int64_t data) {
    if (requestGeneration == generation && !count) { value = data; valid = true; }
  }
};
} // namespace editor
