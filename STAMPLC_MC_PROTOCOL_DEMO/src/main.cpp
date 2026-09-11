#include <Arduino.h>
#include <M5StamPLC.h>
#include <mcprotocol_serial_arduino_esp32.hpp>
#include <cstring>

using namespace mcprotocol::serial;

// 読取成功=緑、書込成功=青を150ms点灯。異常=赤は次の成功まで保持。
bool resultLedPulse = false;
uint32_t resultLedAt = 0;
void showResultLed(bool ok, bool write = false) {
  M5StamPLC.setStatusLight(!ok, ok && !write, ok && write);
  resultLedPulse = ok;
  resultLedAt = millis();
}
void updateResultLed() {
  if (resultLedPulse && uint32_t(millis() - resultLedAt) >= 150) {
    M5StamPLC.setStatusLight(0, 0, 0);
    resultLedPulse = false;
  }
}


Esp32UartClient plc(1); // UART1をMC専用で使用します。
const auto protocol = ProtocolConfig::c4_binary(
    PlcProfile::MelsecIqF, SumCheckMode::Enabled,
    RouteConfig{HostStationRoute{}}, TimeoutConfig{3000, 250});

// 非同期の入出力は完了まで保持するため、グローバルに配置します。
// これらのD/M領域は実際に上書きします。使用領域はREADME参照。
const uint16_t first[] = {10, 20, 30}, second[] = {40, 50, 60};
const BitValue flags[16] = {true, false, true}; // 残り13点はOFF
const RandomWriteWordItem randomWords[] = {
    {{DeviceCode::D, 100}, 111}, {{DeviceCode::D, 200}, 222}};
RandomWriteDWordItem randomDwords[] = {
    {{DeviceCode::D, 500}, 123456U}, {{DeviceCode::D, 510}, 0}};
const RandomWriteBitItem randomBits[] = {
    {{DeviceCode::M, 100}, true}, {{DeviceCode::M, 101}, true},
    {{DeviceCode::M, 102}, false}};
const RandomReadWordItem readWords[] = {
    {{DeviceCode::D, 100}}, {{DeviceCode::D, 200}}};
const RandomReadDWordItem readDwords[] = {
    {{DeviceCode::D, 500}}, {{DeviceCode::D, 510}}};

// ビットブロックのpoints=1は16ビット（M100～M115）です。
// WORDブロックのpoints=3は3ワードです。
const MultiBlockWriteBlock writeBlocks[] = {
    {{DeviceCode::D, 100}, 3, Span<const uint16_t>(first)},
    {{DeviceCode::D, 200}, 3, Span<const uint16_t>(second)},
    {{DeviceCode::M, 100}, 1, Span<const BitValue>(flags)}};
const MultiBlockReadBlock readBlocks[] = {
    {{DeviceCode::D, 100}, 3, false}, {{DeviceCode::D, 200}, 3, false},
    {{DeviceCode::M, 100}, 1, true}};
uint16_t unsignedWord = 0, words[6] {};
int16_t signedWord = 0;
bool bit = false;
BitValue bits[16] {};
uint32_t dwords[2] {};
MultiBlockReadBlockResult results[3] {};

const char* const names[] = {
    "write_word",
    "read_word (uint16)",
    "read_word (int16)",
    "write_words",
    "read_words",
    "write_bit",
    "read_bit",
    "write_bits",
    "read_bits",
    "random_write_words",
    "random_read",
    "random_write_bits",
    "read_bits (verify)",
    "multi_block_write",
    "multi_block_read",
    "async_write_words",
    "async_read_words",
    "async_read_word",
    "async_write_bits",
    "async_read_bits",
    "async_random_write_words",
    "async_random_read",
    "async_random_write_bits",
    "async_read_bits (verify)",
    "async_multi_block_write",
    "async_multi_block_read"
};
constexpr unsigned STEP_COUNT = sizeof(names) / sizeof(names[0]);
constexpr unsigned ASYNC_FIRST = 15;
unsigned step = 0;
bool running = false, failed = false;
uint32_t nextStep = 0;

void display(const char* message) {
  auto& lcd = M5StamPLC.Display;
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextColor(failed ? TFT_RED : TFT_WHITE, TFT_BLACK);
  lcd.setTextSize(2);
  lcd.setCursor(4, 4);
  if (failed) { lcd.print("COMM Error"); return; }
  lcd.println("MC Read/Write");
  if (running) {
    lcd.printf("%u/%u\n", step + 1, STEP_COUNT);
    lcd.println(names[step]);
  }
  lcd.println(message);
}

void fail(Status status) {
  showResultLed(false);
  Serial.printf("ERROR step=%u: %s (status=%u PLC=%04X)\n", step + 1,
      status.message, static_cast<unsigned>(status.code),
      static_cast<unsigned>(status.plc_error_code));
  running = false;
  failed = true;
  display("");
  // 書込結果が不明な場合もあるため、自動再送しません。
}

// 読み戻しの実機チェック。PLC側で同じ領域を更新すると不一致になります。
bool verifyRead() {
  switch (step) {
    case 1: Serial.printf("D100=%u\n", unsignedWord); return unsignedWord == 65413;
    case 2: Serial.printf("D100=%d\n", signedWord); return signedWord == -123;
    case 4: case 16:
      Serial.printf("D100..102=%u,%u,%u\n", words[0], words[1], words[2]);
      return words[0] == 10 && words[1] == 20 && words[2] == 30;
    case 6: Serial.printf("M100=%d\n", bit); return bit;
    case 8: case 19:
      Serial.printf("M100..102=%d,%d,%d\n", bits[0], bits[1], bits[2]);
      return bits[0] && !bits[1] && bits[2];
    case 10: case 21: {
      // floatは数値キャストではなく、32ビットのビット列をコピー。
      float value = 0;
      std::memcpy(&value, &dwords[1], sizeof(value));
      Serial.printf("D100=%u D200=%u DWORD=%lu FLOAT=%.2f\n",
          words[0], words[1], static_cast<unsigned long>(dwords[0]), value);
      return words[0] == 111 && words[1] == 222 &&
             dwords[0] == 123456U && value == 12.5f;
    }
    case 12: case 23:
      Serial.printf("M100..102=%d,%d,%d\n", bits[0], bits[1], bits[2]);
      return bits[0] && bits[1] && !bits[2];
    case 14: case 25:
      // offsetはブロック種別に対応するwords/bits配列内の位置です。
      for (const auto& r : results) {
        Serial.printf("%s%lu:", r.bit_block ? "M" : "D",
            static_cast<unsigned long>(r.head_device.number));
        for (unsigned i = 0; i < r.data_count; ++i)
          Serial.printf(" %u", r.bit_block ? unsigned(bits[r.data_offset + i])
                                        : unsigned(words[r.data_offset + i]));
        Serial.println();
      }
      for (unsigned i = 0; i < 3; ++i)
        if (words[i] != first[i] || words[i + 3] != second[i]) return false;
      for (unsigned i = 0; i < 16; ++i)
        if (bits[i] != flags[i]) return false;
      return true;
    case 17: Serial.printf("D100=%d\n", signedWord); return signedWord == 10;
    default: return true; // 書込ACK。後続の読取で値を検証します。
  }
}

// 同期版の戻り値と非同期版の完了結果を共通処理。
// コールバック中では次の通信を開始せず、次のloopで開始します。
void completed(void*, Status status) {
  if (!status.ok()) { fail(status); return; }
  if (!verifyRead()) {
    fail(make_status(StatusCode::InvalidArgument, "Readback mismatch"));
    return;
  }
  showResultLed(true, std::strstr(names[step], "write") != nullptr);
  Serial.printf("PASS %u/%u %s\n", step + 1, STEP_COUNT, names[step]);
  display("OK");
  ++step;
  if (step == STEP_COUNT) {
    running = false;
    display("ALL PASS");
    Serial.println("ALL PASS: all UART read/write APIs completed.");
  }
  nextStep = millis() + 500; // 各結果をLCDで確認するための間隔
}

// 各APIの呼出例。同期版は応答を待ち、async版は要求受付後すぐ戻ります。
Status executeStep() {
  switch (step) {
    case 0: return plc.write_word({DeviceCode::D, 100}, static_cast<uint16_t>(-123));
    case 1: return plc.read_word({DeviceCode::D, 100}, unsignedWord);
    case 2: return plc.read_word({DeviceCode::D, 100}, signedWord);
    case 3: return plc.write_words({DeviceCode::D, 100}, first);
    case 4: return plc.read_words({DeviceCode::D, 100}, {words, 3});
    case 5: return plc.write_bit({DeviceCode::M, 100}, true);
    case 6: return plc.read_bit({DeviceCode::M, 100}, bit);
    case 7: return plc.write_bits({DeviceCode::M, 100}, {flags, 3});
    case 8: return plc.read_bits({DeviceCode::M, 100}, {bits, 3});
    case 9: return plc.random_write_words(randomWords, randomDwords);
    case 10: return plc.random_read(RandomReadRequest(readWords, readDwords), {words, 2}, dwords);
    case 11: return plc.random_write_bits(randomBits);
    case 12: return plc.read_bits({DeviceCode::M, 100}, {bits, 3});
    case 13: return plc.multi_block_write(MultiBlockWriteRequest(writeBlocks));
    case 14: return plc.multi_block_read(MultiBlockReadRequest(readBlocks), words, bits, results);
    case 15: return plc.async_write_words({DeviceCode::D, 100}, first, completed);
    case 16: return plc.async_read_words({DeviceCode::D, 100}, {words, 3}, completed);
    case 17: return plc.async_read_word({DeviceCode::D, 100}, signedWord, completed);
    case 18: return plc.async_write_bits({DeviceCode::M, 100}, {flags, 3}, completed);
    case 19: return plc.async_read_bits({DeviceCode::M, 100}, {bits, 3}, completed);
    case 20: return plc.async_random_write_words(randomWords, randomDwords, completed);
    case 21: return plc.async_random_read(RandomReadRequest(readWords, readDwords), {words, 2}, dwords, completed);
    case 22: return plc.async_random_write_bits(randomBits, completed);
    case 23: return plc.async_read_bits({DeviceCode::M, 100}, {bits, 3}, completed);
    case 24: return plc.async_multi_block_write(MultiBlockWriteRequest(writeBlocks), completed);
    case 25: return plc.async_multi_block_read(MultiBlockReadRequest(readBlocks), words, bits, results, completed);
    default: return make_status(StatusCode::InvalidArgument, "Invalid demo step");
  }
}

void setup() {
  auto config = M5StamPLC.config();
  config.enableModbusSlave = false; // UART1の競合を避けます。
  M5StamPLC.config(config);
  M5StamPLC.begin();
  M5StamPLC.setStatusLight(0, 0, 0);
  M5StamPLC.Display.setRotation(1);
  Serial.begin(115200);
  // IEEE754単精度floatをDWORDへ。D510が下位、D511が上位。
  const float value = 12.5f;
  static_assert(sizeof(value) == sizeof(uint32_t), "32-bit float required");
  std::memcpy(&randomDwords[1].value, &value, sizeof(value));
  Esp32UartConfig uart;
  uart.baud = 19200;
  uart.format = SERIAL_8E1;
  uart.rx_pin = STAMPLC_PIN_485_RX;
  uart.tx_pin = STAMPLC_PIN_485_TX;
  uart.direction = Esp32Direction::Rs485Rts;
  uart.rts_pin = STAMPLC_PIN_485_DIR;
  const Status status = plc.begin(uart, protocol);
  if (!status.ok()) { fail(status); return; }
  display("Ready");
  // 起動時は書き込まず、KEY1で全26ステップを一度実行します。
}

void loop() {
  M5StamPLC.update();
  updateResultLed();
  plc.update();
  if (!running && !failed && M5StamPLC.BtnA.wasPressed()) {
    step = 0;
    running = true;
    nextStep = millis();
    Serial.println("START: overwriting D100..102,D200..202,D500..501,D510..511,M100..115");
  }
  if (running && !plc.busy() && static_cast<int32_t>(millis() - nextStep) >= 0) {
    display("Running");
    const bool asynchronous = step >= ASYNC_FIRST;
    const Status status = executeStep();
    if (!asynchronous) completed(nullptr, status);
    else if (!status.ok()) fail(status); // 受付失敗には完了通知が来ません。
  }
  delay(1);
}
