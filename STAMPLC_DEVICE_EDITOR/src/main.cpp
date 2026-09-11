#include <Arduino.h>
#include <M5StamPLC.h>
#include <mcprotocol_serial_arduino_esp32.hpp>
#include "editor.hpp"

using namespace mcprotocol::serial;

// 読取成功=緑、書込成功=青を150ms点灯。異常=赤は次の成功まで保持。
bool resultLedPulse = false;
bool resultLedWrite = false;
uint32_t resultLedAt = 0;
void showResultLed(bool ok, bool write = false) {
  // 書き込み直後の読み返しで青が消えないよう、150msは保持する。
  if (ok && !write && resultLedPulse && resultLedWrite &&
      uint32_t(millis() - resultLedAt) < 150) return;
  M5StamPLC.setStatusLight(!ok, ok && !write, ok && write);
  resultLedPulse = ok;
  resultLedWrite = write;
  resultLedAt = millis();
}
void updateResultLed() {
  if (resultLedPulse && uint32_t(millis() - resultLedAt) >= 150) {
    M5StamPLC.setStatusLight(0, 0, 0);
    resultLedPulse = false;
  }
}

editor::State state;
Esp32UartClient plc(1);
const auto protocol = ProtocolConfig::c4_binary(PlcProfile::MelsecIqF,
    SumCheckMode::Enabled, RouteConfig{HostStationRoute{}}, TimeoutConfig{3000, 250});
uint32_t nextRead = 0, requestGeneration = 0;
bool writing = false, communicationError = false, uartInitialized = false, dirty = true;
constexpr uint32_t RETRY_INTERVAL_MS = 3000;
const char* notice = "Reading...";
// 非同期の送受信先は完了まで保持。デバイスの型に応じて使い分ける。
bool bitValue = false;
uint16_t wordValue = 0;
int16_t signedWord = 0;
uint32_t dwordValue = 0;
editor::Kind requestKind = editor::Kind::Word;

void display() {
  auto& lcd = M5StamPLC.Display;
  const auto& device = state.device();
  char number[12];
  const char* format = device.radix == 8 ? "%lo" : device.radix == 16 ? "%lX" : "%lu";
  snprintf(number, sizeof(number), format, static_cast<unsigned long>(state.address));
  const char* modes[] = {"DEVICE", "ADDRESS", "VALUE"};
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextSize(2);
  lcd.setTextColor(TFT_CYAN, TFT_BLACK);
  lcd.setCursor(4, 4);
  lcd.printf("MODE: %s", modes[static_cast<unsigned>(state.mode)]);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.setCursor(4, 32);
  lcd.printf("%s%s (%u)", device.name, number, device.radix);
  lcd.setCursor(4, 60);
  if (state.valid) lcd.printf("VALUE: %lld", static_cast<long long>(state.value));
  else lcd.print("VALUE: ---");
  lcd.setTextColor(communicationError ? TFT_RED : TFT_YELLOW, TFT_BLACK);
  lcd.setCursor(4, 88);
  lcd.print(notice);
}

void fail(Status status) {
  showResultLed(false);
  // 書込失敗・結果不明では待ち行列を破棄し、再送しない。
  state.fail();
  communicationError = true;
  notice = "COMM Error";
  Serial.printf("ERROR: %s (status=%u PLC=%04X)\n", status.message,
      static_cast<unsigned>(status.code), static_cast<unsigned>(status.plc_error_code));
  nextRead = millis() + RETRY_INTERVAL_MS;
  dirty = true;
}

void completed(void*, Status status) {
  if (!status.ok()) { fail(status); writing = false; return; }
  if (writing) showResultLed(true, true);
  if (writing) {
    Serial.printf("WRITE OK: %s index=%lu value=%lld\n", state.device().name,
        static_cast<unsigned long>(state.address), static_cast<long long>(state.writes[state.head]));
    state.written();
    writing = false;
    nextRead = millis(); // 最後の書き込み後は読み返す。
  } else {
    const int64_t value = requestKind == editor::Kind::Bit ? (bitValue ? 1 : 0)
        : requestKind == editor::Kind::Word
          ? static_cast<int64_t>(signedWord)
          : (dwordValue < 0x80000000U ? static_cast<int64_t>(dwordValue) : static_cast<int64_t>(dwordValue) - 0x100000000LL);
    state.received(requestGeneration, value);
    if (state.valid) {
      communicationError = false;
      showResultLed(true);
    }
    if (requestGeneration == state.generation && !state.count)
      Serial.printf("READ: %s index=%lu value=%lld\n", state.device().name,
          static_cast<unsigned long>(state.address), static_cast<long long>(value));
    nextRead = requestGeneration == state.generation ? millis() + 500 : millis();
  }
  notice = communicationError ? "COMM Error" : state.count ? "Writing..." : state.valid ? "OK" : "Reading...";
  dirty = true;
}

void startRead() {
  const DeviceAddress address{state.device().code, state.address};
  requestGeneration = state.generation;
  requestKind = state.device().kind;
  Status status;
  if (requestKind == editor::Kind::Bit) {
    // LCS/LCCも直接ビットアクセスで読み取る。
    status = plc.async_read_bits(address, {&bitValue, 1}, completed);
  } else if (requestKind == editor::Kind::Word) {
    status = plc.async_read_word(address, signedWord, completed);
  } else {
    const RandomReadDWordItem item[] = {{address}};
    status = plc.async_random_read(RandomReadRequest({}, item), {}, {&dwordValue, 1}, completed);
  }
  if (!status.ok()) fail(status);
}

void startWrite() {
  const DeviceAddress address{state.device().code, state.address};
  const auto value = state.writes[state.head];
  writing = true;
  Status status;
  if (state.device().kind == editor::Kind::Bit) {
    bitValue = value != 0;
    status = plc.async_write_bits(address, {&bitValue, 1}, completed);
  } else if (state.device().kind == editor::Kind::Word) {
    wordValue = static_cast<uint16_t>(value);
    status = plc.async_write_words(address, {&wordValue, 1}, completed);
  } else {
    const RandomWriteDWordItem item[] = {{address, static_cast<uint32_t>(value)}};
    status = plc.async_random_write_words({}, item, completed);
  }
  if (!status.ok()) { fail(status); writing = false; }
}

void change(int delta) {
  bool changed = false;
  if (state.mode == editor::Mode::Device) changed = state.select(delta);
  else if (state.mode == editor::Mode::Address) changed = state.moveAddress(delta);
  else if (!communicationError) changed = state.edit(delta);
  if (changed) {
    if (!communicationError) nextRead = millis(); // 操作で復旧待ちを短縮しない。
    notice = communicationError ? "COMM Error" : state.count ? "Writing..." : "Reading...";
  } else if (!communicationError) {
    notice = state.count ? "BUSY / queue full" : state.valid ? "Limit" : "Read first";
  }
  dirty = true;
}

Status openUart() {
  Esp32UartConfig uart;
  uart.baud = 19200;
  uart.format = SERIAL_8E1;
  uart.rx_pin = STAMPLC_PIN_485_RX;
  uart.tx_pin = STAMPLC_PIN_485_TX;
  uart.direction = Esp32Direction::Rs485Rts;
  uart.rts_pin = STAMPLC_PIN_485_DIR;
  const Status status = plc.begin(uart, protocol);
  uartInitialized = status.ok();
  return status;
}

void setup() {
  auto config = M5StamPLC.config();
  config.enableModbusSlave = false;
  M5StamPLC.config(config);
  M5StamPLC.begin();
  M5StamPLC.setStatusLight(0, 0, 0);
  M5StamPLC.Display.setRotation(1);
  Serial.begin(115200);
  const Status status = openUart();
  if (!status.ok()) fail(status);
}

void loop() {
  M5StamPLC.update();
  updateResultLed();
  plc.update();
  if (M5StamPLC.BtnA.wasPressed()) { state.cycleMode(); dirty = true; }
  const bool up = M5StamPLC.BtnB.wasPressed(), down = M5StamPLC.BtnC.wasPressed();
  if (up != down) change(up ? 1 : -1); // 同時押しは値を変えない。
  if (!plc.busy() && communicationError && static_cast<int32_t>(millis() - nextRead) >= 0) {
    // ponytail: 3秒待ちは遅延応答の排除を保証しない。遅延があり得る設備では
    // 最大応答時間を確認して待機時間を延ばすか、通信路を確実に復旧してから再開する。
    const Status status = uartInitialized ? plc.recover(protocol) : openUart();
    if (status.ok()) startRead(); // 失敗した書き込みは再送せず、現在値を読み直す。
    else fail(status);
  } else if (!communicationError && !plc.busy()) {
    if (state.count) startWrite(); // 値変更は定期読取より優先。
    else if (static_cast<int32_t>(millis() - nextRead) >= 0) startRead();
  }
  if (dirty) { display(); dirty = false; }
  delay(1);
}
