# Galaxy Downmixer

Apple Music の Dolby Atmos（7.1.4ch / 12ch PCM）音源に対し、HRTF（頭部伝達関数）や残響処理を適用せず、マルチチャンネル・マトリクス演算によって 2ch ステレオへダウンミックスする Magisk モジュールおよび Android コントロールアプリです。

Samsung Galaxy S21 (One UI 7 / Android 15) にて実機動作確認を行っています。

---

## 開発の背景と特徴

### 1. HRTF / 残響処理を介さないダウンミックス
Apple Music で配信されている Dolby Atmos 音源は、通常の 2ch ステレオ版と比較してリミッターやコンプレッションの適用が抑えられており、広いダイナミックレンジとマルチチャンネルの空間情報が含まれています。

しかし、標準のスマートフォン環境（One UI / Android）で空間オーディオを有効にすると、バーチャルサラウンド化のための HRTF 畳み込みや残響処理が付加され、周波数特性や位相の変化が生じます。

Galaxy Downmixer では以下の処理を行います：
- **端末の Spatializer ライブラリをバイパス**: HRTF 畳み込みや残響エフェクトを無効化。
- **直接マトリクス演算**: 12ch PCM（7.1.4）の音源データを、直接 2ch ステレオへ合成。
- **LFE チャンネル専用 LPF**: 4 次 Butterworth LPF (125Hz) を適用し、不要な帯域の混入や位相干渉を抑制。

### 2. システムワイドで動作するイコライザー & 補正機能
ダウンミックス後の音声に対して、リアルタイムで各種補正処理を行います。
- **グローバル PEQ（共通パラメトリック EQ）**:
  全体の周波数特性を調整する RBJ Biquad フィルタベースのイコライザー。
- **左右独立 PEQ**:
  聴力の左右差や、イヤホン・ヘッドホンの左右個体差のバランス補正に対応。
- **L/R チャンネルスワップ**:
  左右のチャンネル出力を反転させる機能。スワップ時も「左右独立 PEQ」は物理的な出力先（左耳・右耳）に対して適用されるよう処理順序を設計しています。
- **Equalizer APO 形式のプリセット対応**:
  PC 用ソフト「Equalizer APO」形式の設定ファイルの読み込みに対応。

### 3. Material 3 準拠のコントロールアプリ
- 各入力チャンネル（7.1.4ch）およびステレオ出力の **リアルタイム Peak / RMS メーター** 表示。
- チャンネルごとの個別ミュート機能（特定チャンネルのみの再生確認が可能）。
- 接続されたオーディオ機器（有線イヤホン、Bluetooth 等）に応じた EQ プリセットの自動切り替え。

---

## 信号処理の流れ (Signal Pipeline)

```
[7.1.4ch 入力 (Apple Music)]
      │
      ├── 入力メーター（各ch Peak / RMS）
      ├── チャンネルミュート
      ├── [LFE 以外] ──> マトリクス・ダウンミックス ──> ドライ遅延補正 (172 samples @48kHz)
      └── [LFE]     ──> 4次 Butterworth LPF (125Hz) ──> マトリクス
      │
      ├── ミックスメーター
      ├── マスターゲイン調整
      ├── 共通 Parametric EQ
      ├── L / R チャンネルスワップ
      ├── 左右独立 Parametric EQ
      ├── 出力メーター（FL / FR Peak / RMS）
      │
[2ch ステレオ出力]
```

---

## 動作環境 (Requirements)

- **実機確認済み環境**: Samsung Galaxy S21 5G (One UI 7 / Android 15)
- **Root 環境**: Magisk / KernelSU / APatch
- **音楽アプリ**: Apple Music（「ドルビーアトモス」を有効に設定）

※ Galaxy S21 以外の Samsung 端末でも動作する可能性がありますが、`audio_effects` の仕様や Spatializer ライブラリのバージョンが異なる場合があります。

---

## インストール手順 (Installation)

### 1. Magisk モジュールのインストール
1. [Releases](../../releases) ページから最新の `GalaxyDownmixer-vX.X.X.zip` をダウンロードします。
2. Magisk アプリの「モジュール」タブから、ダウンロードした ZIP をインストールします。
3. 端末を再起動します。

### 2. コントロールアプリのインストール
1. [Releases](../../releases) から `GalaxyDownmixer.apk` をダウンロードして端末にインストールします。
2. アプリを起動し、Root 権限を許可します（エフェクトとのパラメータ同期に必要です）。

### 3. 動作確認
1. Apple Music で Dolby Atmos 対応トラックを再生します。
2. Galaxy Downmixer アプリを開き、7.1.4ch の入力メーターが振れていれば正しくダウンミックスが機能しています。

---

## アンインストール方法

- **通常時**: Magisk アプリのモジュール一覧から「Galaxy Downmixer」を削除し、再起動します。
- **不具合発生時（セーフモード）**: 万が一起動しなくなった場合は、起動中に端末の音量下ボタンを長押しして Magisk のセーフモードで起動し、モジュールを削除してください。

---

## 開発・ビルド方法 (For Developers)

本リポジトリのソースコードには、権利関係の都合上 Samsung 製のバイナリを含んでいません。
ソースコードからモジュール ZIP を手動ビルドする場合は、接続した実機からライブラリを取り出してからビルドを実行してください。

```bash
# 端末から必要なバイナリを取得
adb pull /system/lib64/libswspatializer_legacy.so ./
adb pull /vendor/lib/soundfx/libswspatializer.so ./libswspatializer_vendor32.so

# パッチ適用とモジュール ZIP 作成
python scripts/patch_spatializer.py
bash scripts/package_module.sh
```

---

## 免責事項 (Disclaimer)

本モジュールは Root 権限を使用し、システムオーディオ構成（`audio_effects`）を変更します。
本ツールの使用によって生じたいかなる損害・端末の不具合についても、作者は一切の責任を負いません。すべて自己責任でご使用ください。
