# STAMPLCとFX5UのMCプロトコル通信

各サンプルに共通する配線・PLC設定・開発環境をまとめています。

## サンプルを選ぶ

| プロジェクト | 内容 | PLCへの書き込み |
| --- | --- | --- |
| [STAMPLC_D100_MONITOR](STAMPLC_D100_MONITOR/README.md) | D100を定期的に読み、LCDに表示 | なし |
| [STAMPLC_D100_SIMPLE](STAMPLC_D100_SIMPLE/README.md) | 同期読み取りとdelayでD100を表示するシンプル版 | なし |
| [STAMPLC_D100_TREND](STAMPLC_D100_TREND/README.md) | D100の直近120回の値を折れ線グラフで表示 | なし |
| [STAMPLC_DEVICE_EDITOR](STAMPLC_DEVICE_EDITOR/README.md) | キーでデバイス・番地・値を選択 | VALUEモードで上下キーを押すと書き込み |
| [STAMPLC_MC_PROTOCOL_DEMO](STAMPLC_MC_PROTOCOL_DEMO/README.md) | 各読み書きAPIを順に実行し、読み戻して確認 | KEY1で開始すると書き込み |

書き込みに使う領域と操作は各プロジェクトのREADMEで確認してください。

## 用意するもの

- M5Stack STAMPLCと電源・USBケーブル
- FX5U（内蔵RS-485ポートを使用）
- RS-485用ツイストペア線と信号GND線
- GX Works3、VS Code / PlatformIO

## 1. 配線する

電源を切って、STAMPLCの**PWR-485**とFX5Uの**内蔵RS-485端子**を接続します。2線式の半二重通信なので、FX5U側では送信と受信の同極性端子を結びます。

| STAMPLC PWR-485 | FX5U内蔵RS-485 |
| --- | --- |
| A | SDA（TXD+）とRDA（RXD+）を短絡して接続 |
| B | SDB（TXD−）とRDB（RXD−）を短絡して接続 |
| GND | SG |
| 電源＋ | 通信配線では接続しない |

```text
STAMPLC                         FX5U
 A ------------------------+--- SDA
                           +--- RDA
 B ------------------------+--- SDB
                           +--- RDB
 GND -------------------------- SG
```

A/Bの線をツイストペアにします。端子の並び順を推測せず、実機の刻印で確認してください。PWR-485の電源＋はSTAMPLCの入力電源につながっているため、SGや信号端子へ接続しません。[M5Stack公式仕様・ピン配置](https://docs.m5stack.com/en/core/StamPLC)

1対1接続では両機器が線路の両端になります。FX5Uの終端抵抗は2線式用の110Ωに設定します。STAMPLCは公式V1.0回路図のR1（120Ω）とS1による終端回路を確認し、終端を有効にしてください。有効な内蔵終端に外付け抵抗を重ねて追加しないでください。FX5シリアル通信マニュアル・4.5配線、STAMPLC回路図（[M5Stack製品ページ](https://docs.m5stack.com/en/core/StamPLC)のSchematicsから参照）

## 2. GX Works3でFX5Uを設定する

ナビゲーションの「パラメータ → FX5UCPU → ユニットパラメータ → 485シリアルポート」を開き、以下の設定を行います。画面の表記はGX Works3の版によって多少異なります。

| 項目 | このサンプルの設定 |
| --- | --- |
| 通信プロトコル | MCプロトコル |
| 通信速度 | 19200 bps |
| データ長 | 8 bit |
| パリティ | 偶数（Even） |
| ストップビット | 1 bit |
| サムチェックコード | 付加する／あり |
| 局番 | 0 |
| メッセージ形式 | 形式5（Pattern 5） |

形式5は4Cフレームのバイナリ通信です。「8E1」は8bit・Even・1stopの略で、コードの`SERIAL_8E1`と対応します。形式5ではデータ長を8bitにします。三菱電機MCプロトコルマニュアル・3.1

設定したパラメータをFX5Uに書き込み、設備を停止できる状態でCPUリセット等により反映させます。設定画面を変更しただけではPLCの通信条件は変わりません。上の設定項目と内蔵ポートの操作場所はFX5シリアル通信マニュアル・4.6を参照してください。

## STAMPLC側の共通設定

| 項目 | 設定 |
| --- | --- |
| ポート | UART1 / PWR-485 |
| RX / TX / DIR | GPIO39 / GPIO0 / GPIO46 |
| 通信形式 | SERIAL_8E1、19200bps |
| プロファイル | PlcProfile::MelsecIqF |
| MC伝文 | ProtocolConfig::c4_binary()：4Cバイナリ・形式5 |
| サムチェック / 局番 | あり / 0 |
| 通信バッファ | 要求768、応答768、要求データ384バイト |

UART1はMC通信用に専有します。各サンプルでは公式M5StamPLCライブラリの
Modbusスレーブ機能を無効にしています。同じUARTをSerial1等から別に初期化しないでください。

## 使用ライブラリ

| ライブラリ | バージョン |
| --- | --- |
| [fa-yoshinobu/mcprotocol-serial-cpp](https://registry.platformio.org/libraries/fa-yoshinobu/mcprotocol-serial-cpp) | 4.2.1 |
| M5StamPLC | 1.2.0 |
| M5Unified | 0.2.13 |
| M5GFX | 0.2.28 |

各プロジェクトのplatformio.iniに設定済みです。ライブラリはPlatformIOが取得するため、
別途ローカルリポジトリを用意する必要はありません。
使用するプロジェクトのフォルダーをPlatformIOで開いてください。

## RGB LED表示

| 色 | 意味 |
| --- | --- |
| 赤（R） | 通信異常。次の通信成功まで保持 |
| 緑（G） | 読み取り成功。約150ms点灯 |
| 青（B） | 書き込み成功。約150ms点灯 |

D100モニターは読み取り専用なので赤・緑を使用します。
送信中・受信中ではなく、通信の完了結果を表示します。
LED処理は各プロジェクトのmain.cpp内にあります。

## 通信できない場合

- FX5Uへパラメータを書き込み、CPUへ反映したか確認します。
- 形式5・局番・サムチェック・19200bps・8E1が一致しているか確認します。
- PWR-485のA/B、FX5U側の短絡、SG接続、終端設定を確認します。
- USBシリアルログ（115200bps）でエラーを確認します。

通信エラー後の動作はサンプルごとに異なります。D100モニターは再試行しますが、
書き込みを行う例では結果不明の書き込みを自動再送しません。
詳しい復旧操作は各プロジェクトのREADMEを参照してください。
