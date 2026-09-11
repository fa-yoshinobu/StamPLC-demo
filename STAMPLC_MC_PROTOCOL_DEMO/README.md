# STAMPLC_MC_PROTOCOL_DEMO：MCプロトコル読み書きデモ

M5公式M5StamPLCライブラリと、PlatformIO公開版 `fa-yoshinobu/mcprotocol-serial-cpp@4.2.1` の
ESP32 UARTアダプターを使います。アダプターが公開する読み書きAPIを、
同期・非同期ともに呼び出す26ステップのサンプルです。
単点WORDの符号あり・なしの両オーバーロードも含みます。
コア専用の通信管理・モニター登録などは対象外です。

## 操作

1. 下表の領域をデモ専用に確保し、PLCプログラムから更新しないようにします。
2. 起動するとLCDに `Ready` と表示します。
3. **KEY1を押すと書き込みを含む全ステップを1回実行**します。
4. LCDに関数名・進捗・OKを表示し、すべて成功すると `ALL PASS` を表示します。
5. USBシリアル（115200bps）に読み取った値と各ステップのPASSを出力します。
6. 完了後はKEY1で再実行できます。KEY2/KEY3には機能を割り当てていません。

通信エラー・読み戻し不一致ではLCDに `COMM Error` だけを表示して停止します。
詳細と失敗したステップはUSBログで確認できます。原因を解消してリセットしてください。
書き込み結果が不明な場合があるため、自動再送・自動再開はしません。
起動するだけではPLCに書き込みません。

## 上書きする領域

| 領域 | 用途 | 全ステップ正常終了後の値 |
| --- | --- | --- |
| D100～D102 | 単点・連続・ランダム・複数ブロック | 10, 20, 30 |
| D200～D202 | ランダム・複数ブロック | 40, 50, 60 |
| D500～D501 | DWORD、下位ワードが先 | DWORDとして123456 |
| D510～D511 | floatの32ビット列 | floatとして12.5 |
| M100～M115 | 単点・連続・ランダム・複数ブロック | M100/M102のみON、ほかOFF |

途中ではD100に-123や111、D200に222、M101にONも書き込みます。
元の値を保存・復元するデモではありません。途中で止まった場合は途中の値が残ります。

## 使用する全API

| 種類 | 同期API | 非同期API |
| --- | --- | --- |
| 単点WORD | read_word(uint16_t&), read_word(int16_t&), write_word | async_read_word(int16_t&) |
| 連続WORD | read_words, write_words | async_read_words, async_write_words |
| 単点ビット | read_bit, write_bit | 連続ビットAPIに1点を指定 |
| 連続ビット | read_bits, write_bits | async_read_bits, async_write_bits |
| ランダムWORD/DWORD | random_read, random_write_words | async_random_read, async_random_write_words |
| ランダムビット書込 | random_write_bits | async_random_write_bits |
| 複数ブロック | multi_block_read, multi_block_write | async_multi_block_read, async_multi_block_write |

単点のasync_write_word/async_read_bit/async_write_bitという名前の関数は現行アダプターにはありません。
ランダムビット読取専用APIもないため、書込後はread_bits/async_read_bitsで照合します。
`src/main.cpp` の `executeStep()` に実際の呼び出しをまとめています。

## ステップ順序

表の読み取り値は、正常時に照合する期待値です。値の並びは番地順です。

| ステップ | API | 対象番地と書き込み値／読み取り期待値 |
| --- | --- | --- |
| 1 | `write_word` | D100へ−123の16bit表現を書き込み |
| 2 | `read_word (uint16)` | D100＝65413（符号なし） |
| 3 | `read_word (int16)` | D100＝−123（符号付き） |
| 4 | `write_words` | D100～D102へ10, 20, 30 |
| 5 | `read_words` | D100～D102＝10, 20, 30 |
| 6 | `write_bit` | M100へON |
| 7 | `read_bit` | M100＝ON |
| 8 | `write_bits` | M100～M102へON, OFF, ON |
| 9 | `read_bits` | M100～M102＝ON, OFF, ON |
| 10 | `random_write_words` | D100へ111、D200へ222、D500～D501へDWORD 123456、D510～D511へfloat 12.5 |
| 11 | `random_read` | ステップ10の4つの値を照合 |
| 12 | `random_write_bits` | M100～M102へON, ON, OFF |
| 13 | `read_bits (verify)` | M100～M102＝ON, ON, OFF |
| 14 | `multi_block_write` | D100～D102へ10, 20, 30、D200～D202へ40, 50, 60、M100～M115はM100/M102のみON、ほかOFF |
| 15 | `multi_block_read` | ステップ14の3ブロックを照合 |
| 16 | `async_write_words` | D100～D102へ10, 20, 30 |
| 17 | `async_read_words` | D100～D102＝10, 20, 30 |
| 18 | `async_read_word` | D100＝10（符号付き） |
| 19 | `async_write_bits` | M100～M102へON, OFF, ON |
| 20 | `async_read_bits` | M100～M102＝ON, OFF, ON |
| 21 | `async_random_write_words` | ステップ10と同じ番地・値を書き込み |
| 22 | `async_random_read` | ステップ21の4つの値を照合 |
| 23 | `async_random_write_bits` | M100～M102へON, ON, OFF |
| 24 | `async_read_bits (verify)` | M100～M102＝ON, ON, OFF |
| 25 | `async_multi_block_write` | ステップ14と同じ3ブロックを書き込み |
| 26 | `async_multi_block_read` | ステップ25の3ブロックを照合 |

最初の15ステップは同期版、残りは非同期版です。
各ステップの間に500msの表示時間を設けています。

## 読み書きの考え方

- **連続アクセス**：D100から3点など、先頭番地と配列で指定します。
- **ランダムアクセス**：D100・D200・D500など、離れた番地を指定します。
  WORDとDWORDの入出力配列は別々です。
- **複数ブロック**：D100から3ワード、D200から3ワード、M100から16ビットをまとめます。
  ビットブロックのpoints=1は「1ビット」ではなく「16ビット」です。
  読取結果のdata_offset/data_countを使って、それぞれの出力配列を参照します。
- **符号付きWORD**：`read_word` / `async_read_word` にint16_tを渡すと、
  ライブラリが符号を解釈します。-123の生の16ビット値は65413です。
- **DWORD**：D500/D501の2ワードを32ビット値として読み書きします。
- **float**：float専用通信命令ではなくDWORDを使います。
  数値キャストはせず、`memcpy` でfloatとuint32_tのビット列をコピーします。

書き込みステップのLCDの`OK`とUSBログの`PASS`は、PLCから正常応答（ACK）を受け取ったことを示します。この時点では読み戻し照合はまだ行っていません。

後続の読み取りステップで`verifyRead()`が期待値と比較し、一致すればそのステップも`OK`／`PASS`になります。全26ステップを通過すると`ALL PASS`を表示します。PLC側が同じ領域を更新していると、不一致として停止します。

## RGB LED

| 色 | 意味 |
| --- | --- |
| 緑 | 読み取りと期待値の照合に成功。約150ms点灯 |
| 青 | 書き込みの正常応答を受信。約150ms点灯 |
| 赤 | 通信エラーまたは読み戻し不一致。点灯してデモを停止 |

LEDは通信結果を示します。各ステップの間には500msの表示時間があるため、青の点灯が終わってから次の読み取りへ進みます。異常時は原因を解消して本体をリセットしてください。自動再開はしません。

## 同期と非同期

同期関数は応答またはタイムアウトまで戻りません。
この間、同じloopでのM5更新は止まります。同期APIの使用例を示すために含めています。

非同期関数は要求の受付結果を返します。受付成功はPLCの処理成功ではありません。
`loop()` の `plc.update()` で送受信を進め、
`completed()` のStatusで最終結果を確認します。
受信配列は完了まで保持し、同時に発行する要求は1件です。
コールバックから次の要求を発行せず、次のloopで開始します。

追加のRTOSタスクは作らず、millis()の差分で待機します。
通信バッファは要求768・応答768・要求データ384バイト、loopスタックは標準設定です。

## 接続設定

配線・FX5Uの設定・使用ライブラリは[共通README](../README.md)を参照してください。
