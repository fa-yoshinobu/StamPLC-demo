#include <Arduino.h>
#include <M5StamPLC.h>
#include <mcprotocol_serial_arduino_esp32.hpp>

using namespace mcprotocol::serial;

Esp32UartClient plc(1); // PWR-485はUART1を使用。
Esp32UartConfig uart;
bool uartInitialized = false;
const auto protocol = ProtocolConfig::c4_binary(
    PlcProfile::MelsecIqF, SumCheckMode::Enabled,
    RouteConfig{HostStationRoute{}}, TimeoutConfig{3000, 250});

void setup() {
  // 同じUARTを使う公式Modbus機能を無効にする。
  auto config = M5StamPLC.config();
  config.enableModbusSlave = false;
  M5StamPLC.config(config);
  M5StamPLC.begin();
  M5StamPLC.setStatusLight(0, 0, 0);
  M5StamPLC.Display.setRotation(1);
  M5StamPLC.Display.setTextSize(3);
  Serial.begin(115200);

  uart.baud = 19200;
  uart.format = SERIAL_8E1;
  uart.rx_pin = STAMPLC_PIN_485_RX;
  uart.tx_pin = STAMPLC_PIN_485_TX;
  uart.direction = Esp32Direction::Rs485Rts;
  uart.rts_pin = STAMPLC_PIN_485_DIR;
}

void loop() {
  M5StamPLC.update();
  Status status = ok_status();
  if (!uartInitialized) {
    status = plc.begin(uart, protocol);
    uartInitialized = status.ok();
  } else if (plc.requires_transport_reset()) {
    // 同じD100を読む表示用途。3秒待ちでも遅延応答の排除は保証しない。
    status = plc.recover(protocol);
  }

  // 応答またはタイムアウトまで、この関数の中で待つ。
  // 符号変換もライブラリに任せる。コールバックやplc.update()は不要。
  int16_t d100 = 0;
  if (status.ok()) status = plc.read_word({DeviceCode::D, 100}, d100);

  auto& lcd = M5StamPLC.Display;
  lcd.fillScreen(TFT_BLACK);
  lcd.setCursor(8, 8);
  lcd.setTextColor(status.ok() ? TFT_WHITE : TFT_RED, TFT_BLACK);
  M5StamPLC.setStatusLight(!status.ok(), status.ok(), 0);
  if (status.ok()) {
    lcd.printf("D100:%d", static_cast<int>(d100));
    Serial.printf("D100:%d\n", static_cast<int>(d100));
    delay(150); // 読み取り成功の緑LEDを点灯。
    M5StamPLC.setStatusLight(0, 0, 0);
    delay(850); // 合計1秒待って次の読み取りへ。
  } else {
    lcd.print("COMM Error"); // 古い値は表示しない。
    Serial.printf("ERROR: %s (status=%u, PLC=0x%04X)\n", status.message,
        static_cast<unsigned>(status.code), static_cast<unsigned>(status.plc_error_code));
    delay(3000); // 赤LEDを保持して3秒後に再試行。
  }
}
