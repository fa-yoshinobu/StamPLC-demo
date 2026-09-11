#include <Arduino.h>
#include <M5StamPLC.h>
#include <mcprotocol_serial_arduino_esp32.hpp>

using namespace mcprotocol::serial;

// 読取成功=緑を150ms点灯。異常=赤は次の成功まで保持。
bool resultLedPulse = false;
uint32_t resultLedAt = 0;
void showResultLed(bool ok) {
  M5StamPLC.setStatusLight(!ok, ok, 0);
  resultLedPulse = ok;
  resultLedAt = millis();
}
void updateResultLed() {
  if (resultLedPulse && uint32_t(millis() - resultLedAt) >= 150) {
    M5StamPLC.setStatusLight(0, 0, 0);
    resultLedPulse = false;
  }
}


// STAMPLCのPWR-485に接続されたUARTピン。
constexpr int RX_PIN = STAMPLC_PIN_485_RX;    // GPIO39
constexpr int TX_PIN = STAMPLC_PIN_485_TX;    // GPIO0
constexpr int DIR_PIN = STAMPLC_PIN_485_DIR;  // GPIO46
constexpr uint32_t PLC_BAUD = 19200;
constexpr uint32_t RETRY_INTERVAL_MS = 3000; // 再試行間隔
const auto protocol = ProtocolConfig::c4_binary(
    PlcProfile::MelsecIqF, SumCheckMode::Enabled,
    RouteConfig{HostStationRoute{}}, TimeoutConfig{3000, 250});

// Esp32UartClientがUART1の初期化・送受信・タイムアウトを処理をします。
// Serial1.begin()などで同じUARTを別に初期化しないでください。
Esp32UartClient plc(1);
int16_t d100 = 0; // 非同期の受信先は、読み取り完了まで保持します。
bool communicationOk = true;
bool uartInitialized = false;
uint32_t nextRead = 0;
constexpr unsigned HISTORY_SIZE = 120;
int16_t history[HISTORY_SIZE] {};
unsigned historyCount = 0, historyHead = 0;

// LCDはsetup/loopタスクだけが操作します。
void showDisplay(bool ok) {
  auto& lcd = M5StamPLC.Display;
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextColor(ok ? TFT_WHITE : TFT_RED, TFT_BLACK);
  lcd.setTextSize(2);
  lcd.setCursor(4, 4);
  if (ok) lcd.printf("D100:%d", static_cast<int>(d100));
  else lcd.print("COMM Error");

  // リングバッファを古い順に表示する。値の移動コピーは不要。
  const unsigned oldest = (historyHead + HISTORY_SIZE - historyCount) % HISTORY_SIZE;
  int32_t minimum = 32767, maximum = -32768;
  for (unsigned i = 0; i < historyCount; ++i) {
    const int32_t value = history[(oldest + i) % HISTORY_SIZE];
    if (value < minimum) minimum = value;
    if (value > maximum) maximum = value;
  }
  lcd.drawRect(0, 28, 240, 78, TFT_DARKGREY);
  lcd.drawFastHLine(1, 67, 238, TFT_DARKGREY);
  int previousX = 0, previousY = 0;
  for (unsigned i = 0; i < historyCount; ++i) {
    const int32_t value = history[(oldest + i) % HISTORY_SIZE];
    const int x = 1 + (HISTORY_SIZE - historyCount + i) * 237 / (HISTORY_SIZE - 1);
    // 差分は32bitで計算し、-32768～32767の全範囲を扱う。
    const int y = maximum == minimum ? 67 : 104 - (value - minimum) * 75 / (maximum - minimum);
    if (i) lcd.drawLine(previousX, previousY, x, y, TFT_CYAN);
    else lcd.drawPixel(x, y, TFT_CYAN);
    previousX = x;
    previousY = y;
  }
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.setCursor(4, 110);
  if (historyCount) lcd.printf("MIN:%ld MAX:%ld", static_cast<long>(minimum), static_cast<long>(maximum));
  else lcd.print("No samples");
  lcd.setCursor(4, 124);
  lcd.printf("%u/120 samples  KEY1:CLEAR", historyCount);
}

// 異常時は通信だけを止め、M5の更新処理は続けます。
// 待機後に同じD100読み取りだけを再試行します。
void check(Status status) {
  if (status.ok()) return;
  communicationOk = false;
  showResultLed(false);
  Serial.printf("ERROR: %s (status=%u, PLC=0x%04X)\n",
                status.message, static_cast<unsigned>(status.code),
                static_cast<unsigned>(status.plc_error_code));
  Serial.println("Retry in 3 seconds.");
  showDisplay(false); // 古い値を消す。
  nextRead = millis() + RETRY_INTERVAL_MS;
}

// loop内のplc.update()から呼ばれます。成功時だけ値を表示します。
void onRead(void*, Status status) {
  check(status);
  if (!communicationOk) return;
  history[historyHead] = d100;
  historyHead = (historyHead + 1) % HISTORY_SIZE;
  if (historyCount < HISTORY_SIZE) ++historyCount;
  showResultLed(true);
  Serial.printf("D100:%d\n", static_cast<int>(d100));
  showDisplay(true);
  nextRead = millis() + 200;
}

Status openUart() {
  Esp32UartConfig uart;
  uart.baud = PLC_BAUD;
  uart.format = SERIAL_8E1;
  uart.rx_pin = RX_PIN;
  uart.tx_pin = TX_PIN;
  uart.direction = Esp32Direction::Rs485Rts;
  uart.rts_pin = DIR_PIN;
  const Status status = plc.begin(uart, protocol);
  uartInitialized = status.ok();
  return status;
}

void setup() {
  // 公式ライブラリでLCDと本体を初期化。UART1をMC通信で使うため
  // 同じポートを使用する公式Modbusスレーブ機能は無効にします。
  auto m5Config = M5StamPLC.config();
  m5Config.enableModbusSlave = false;
  M5StamPLC.config(m5Config);
  M5StamPLC.begin();
  M5StamPLC.setStatusLight(0, 0, 0);
  auto& lcd = M5StamPLC.Display;
  lcd.setRotation(1);  // 横向き240×135
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.setTextSize(3);
  lcd.setCursor(8, 8);
  lcd.println("FX5U Waiting...");
  Serial.begin(115200);  // PCへのログはUSB。PLC用UARTとは独立しています。
  Serial.println("STAMPLC -> FX5U Connect / 19200 8E1 / 4C Format5");

  check(openUart());
}

void loop() {
  M5StamPLC.update();
  if (M5StamPLC.BtnA.wasPressed()) {
    historyCount = historyHead = 0;
    showDisplay(communicationOk);
  }
  updateResultLed();
  plc.update(); // 送受信を少しずつ進め、応答待ちでもloopを回します。
  // 時刻差で判定するため、millis()の周回をまたいでも待機できます。
  if (!plc.busy() && static_cast<int32_t>(millis() - nextRead) >= 0) {
    if (!communicationOk) {
      // 同じD100だけを読む表示用途。線上の遅延応答の排除は保証しません。
      Serial.println("Reconnecting UART...");
      const Status status = uartInitialized ? plc.recover(protocol) : openUart();
      communicationOk = status.ok();
      check(status);
    }
    if (communicationOk)
      check(plc.async_read_word({DeviceCode::D, 100}, d100, onRead));
  }
  delay(1); // RTOSへ実行機会を渡します。
}
