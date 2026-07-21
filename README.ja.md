# famicom-rf-hackrf-decoder

[English README is here](README.md)

ファミコンの VHF RF 出力（NTSC-J）を HackRF One で受信し、リアルタイムに
NTSC カラーデコード + FM 音声復調して PC に表示するソフトウェアデコーダ
です。C++20 + libhackrf + SDL2。GNU Radio 不要。

実機（ファミコン + スーパーマリオブラザーズ）のライブデコード:

![real Famicom decode](docs/screenshot.png)

合成カラーバーのゴールデンテスト出力:

![color bars](docs/colorbars.png)

## 対応チャンネル

| チャンネル | 映像キャリア | 音声キャリア (FM) |
|---|---|---|
| 日本 VHF 1ch | 91.25 MHz | 95.75 MHz |
| 日本 VHF 2ch | 97.25 MHz | 101.75 MHz |

HackRF は DC スパイク回避のため映像キャリア +2.0 MHz にオフセット
チューニングし（1ch → 93.25 MHz）、ソフトウェア側でシフトして復調します。

> **注意: 実機の RF モジュレータは周波数がずれていることがあります。**
> 開発に使った個体は 1ch 設定で映像キャリアが 90.83 MHz（規定 -420 kHz）
> でした。同期しない場合はまず `--spectrum` でキャリアの実位置を確認し、
> `--freq` で合わせてください。包絡線検波なので ±100 kHz 程度の残差は
> 問題になりません。

## ハードウェア: HackRF One

[HackRF One](https://greatscottgadgets.com/hackrf/one/)（Great Scott
Gadgets）は 1 MHz〜6 GHz をカバーするオープンソース SDR で、USB 2.0
経由で最大 20 MSPS・8bit の IQ サンプリングができます。本プロジェクト
では受信専用・10 MSPS で使用します。

![hardware setup](docs/hardware-setup.jpg)

接続: ファミコンの RF 出力（本来テレビのアンテナ端子につなぐ 75Ω RCA
ケーブル）を HackRF の **ANTENNA** (SMA) 端子へ。RCA→SMA 変換アダプタが
あればベストですが、写真のように同軸の芯線を SMA のピンに直接絡げるだけ
でも十分動きます（RF モジュレータの出力は強いのでケーブルロスは問題に
なりません）。電波を空中に出さないよう有線接続とし、AMP はオフ・LNA は
控えめから始めて、クリップ警告を見ながら `l`/`g` キーで調整してください。

基板の LED: 3V3/1V8/RF = 電源系、USB = ホスト接続、RX = famidec の
受信ストリーミング中に点灯します。

## ビルド

```sh
brew install hackrf sdl2 cmake pkg-config   # macOS（Linux も同じライブラリでOK）
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

開発・動作確認は macOS (Apple Silicon)。libhackrf と SDL2 以外に
プラットフォーム依存はありません。

## 使い方

```sh
# 1. まずスペクトラムで信号確認（映像キャリアの実位置を見る）
./build/famidec --channel 1 --spectrum

# 2. ライブ表示（チャンネルプリセット or 実測周波数）
./build/famidec --channel 1
./build/famidec --freq 90.83e6

# 受信しながら IQ を録画
./build/famidec --freq 90.83e6 --record cap.cs8

# 録画ファイルからデコード（hackrf_transfer の .cs8 も可）
./build/famidec --input file --file cap.cs8 --loop

# ヘッドレスでフレームを PPM 出力（デバッグ・検証用）
./build/famidec --input file --file cap.cs8 --dump-frames out_ --frames 30
```

### オプション一覧

| オプション | 説明 |
|---|---|
| `--channel 1\|2` | 日本 VHF チャンネルプリセット（既定 1） |
| `--freq HZ` | 映像キャリア周波数を直接指定 |
| `--input hackrf\|file` | 入力ソース（既定 hackrf） |
| `--file PATH` / `--loop` | .cs8 再生 / ループ再生 |
| `--rate HZ` / `--offset HZ` | サンプルレート（既定 10e6）/ チューニングオフセット（既定 2e6） |
| `--lna N` / `--vga N` / `--amp` | LNA 0-40 (既定24) / VGA 0-62 (既定20) / RFアンプ |
| `--mode color\|gray` | カラー / 白黒（既定 color） |
| `--detector envelope\|sync` | 包絡線検波 / キャリアPLL同期検波 |
| `--sat F` / `--hue DEG` | 彩度 / 色相トリム |
| `--overscan F` | 左右クロップ量（片側、0〜0.15、既定 0.047 ≒ NES の256px画面） |
| `--fm-freq HZ` | `f` キーで聴く FM 局の周波数（既定 80.0e6） |
| `--no-audio` / `--volume F` | FM音声を無効化 / 音量 0..1（既定 0.7） |
| `--record PATH` | デコードしながら生 IQ を .cs8 保存 |
| `--dump-frames PREFIX` / `--frames N` | ヘッドレスで PPM フレーム出力 |
| `--dump-composite PATH` | AGC 後コンポジット信号を f32 で保存（デバッグ） |
| `--spectrum` | PSD を表示して終了（映像なし） |

### キー操作 / 画面表示

- `q` / ESC: 終了、`l` / `L`: LNA ±8dB、`g` / `G`: VGA ±2dB、
  `c`: カラー⇔白黒、`s`: スクリーンショット (BMP)、`h`: ヘルプ表示
- `v`: IQ 録画の開始/停止（`famidec_rec_NNN.cs8` に連番保存、録画中は
  赤い `REC` カウンタ表示）。再生は
  `famidec --input file --file famidec_rec_001.cs8 --loop`。
  生 IQ は 10 MSPS で約 20 MB/s（約 72 GB/時）なので短めの録画推奨
- `←` / `→`: 周波数 ±50 kHz、`↑` / `↓`: ±1 MHz（受信中にリアルタイム
  チューニング）
- `r`: CRT 再現モード（ブラウン管風の湾曲 + 走査線 + 四隅減光）
- `o`: 右上 HUD の表示/非表示
- `f`: FM ラジオモード — `--fm-freq`（既定 80.0 MHz TOKYO FM）へリチューン
  して広帯域 FM（±75 kHz、デエンファシス 50 µs）を復調。カーソルキーで
  100 kHz / 1 MHz ステップ選局、もう一度 `f` でテレビに復帰。
  受信感度はアンテナ次第（ファミコンの RF ケーブルは FM には不向き）
- **左上（大きな緑文字）**: 昔のテレビ風チャンネル表示（`CH1`）
- **右上（黄色）**: 同期ロック状態とデコードFPS
  （`V-SYNC:OK H-SYNC:OK 60.0FPS`）、映像・音声キャリア
  （`VHF:90.83MHz AUD:95.33MHz`）、実測遅延（`DELAY V:35ms A:88ms`）
- 同期が取れていないときはテレビと同じく**砂嵐**が表示されます。
  ノイズで vsync を見逃してもライン PLL のフライホイールでフレームは
  流れ続けます

## 仕組み

```
HackRF One (10 MSPS, 映像キャリア +2 MHz オフセット)
  → 複素 DC ブロッカ（DC スパイク除去）
  → NCO ミキサで映像キャリアを 0 Hz へ
  → 4.3 MHz 複素 LPF（音声 FM・隣接チャンネル除去）
  → AM 検波（包絡線 or キャリア PLL 同期検波）
  → AGC（sync tip / ペデスタル追跡 → IRE 正規化）
  → 同期分離 + フライホイール式ライン PLL / vsync 検出
  → Y/C 帯域分離（3.58 MHz BPF、Y = コンポジット − クロマ）
  → ライン毎カラーバースト位相測定 → クロマ QAM 復調（U/V）
  → YUV→RGB、640×480（240p ラインダブル）
  → トリプルバッファ → SDL2 表示
音声分岐（LPF 前）→ −4.5 MHz ミキサ → 1/25 間引き FIR（400 kHz）
  → FM 弁別 → 75 µs デエンファシス → 1/8 間引き → 50 kHz → SDL オーディオ
```

ファミコンは放送規格非準拠（ノンインターレース 240p、クロマ位相が
ライン毎に 120° 回転、フレーム毎の短ライン）のため:

- Y/C 分離はラインコムではなく**帯域分離**
- vsync は等化パルスを要求しない**長パルス領域検出**
- カラーバーストはバースト PLL に頼らず**ライン毎に独立測定**

スレッド構成: USB コールバック（SPSC リングへ push のみ）→ DSP スレッド
（復調〜フレーム生成）→ メインスレッド（SDL 描画）。Apple Silicon で
10 MSPS 実測 約13倍のリアルタイム余裕があります。

## テスト

```sh
./build/synth_ntsc            # 合成カラーバー IQ → デコード → RGB 検証 (PASS/FAIL)
./build/synth_ntsc bars.cs8   # 合成 IQ を .cs8 に書き出し（E2E テスト用）
ctest --test-dir build
```

## トラブルシューティング

| 症状 | 対処 |
|---|---|
| 砂嵐のまま同期しない | `--spectrum` でキャリア実位置を確認 → `--freq` で指定 |
| 画面が暗い/白飛び | `l`/`L` `g`/`G` でゲイン調整（クリップに注意） |
| 色が出ない（白黒） | 信号品質不足。ゲインと周波数を調整 |
| 色相がずれる | `--hue DEG` で微調整（例 `--hue 10`） |
| 縦にずれる | `src/dsp/ntsc_decoder.hpp` の `kActiveStartLine` を調整 |
| 音が出ない/歪む | `--volume` 調整。音声キャリアのずれも `--freq` の精度に依存 |

## ライセンス / 免責

MIT License — [LICENSE](LICENSE) を参照。

受信専用ツールです。HackRF One での送信は行いません。
