# Galaxy Downmixer

Root化した Galaxy S21 で、Apple Music の Dolby Atmos（7.1.4 / 12ch PCM）を、自分で決めた係数で 2ch にダウンミックスする計画のプロジェクト。

## 現状（段階1：音声処理の中核）

`dsp/` は端末に依存しない C++17 のライブラリ。PC でテストし、NDK でもビルドできる。

### 処理の順番
```
入力メーター → ミュート → [LFE以外: マトリクス → ドライ遅延 172サンプル(@48kHz)]
                          [LFE: Butterworth 4次 LPF 125Hz → マトリクス]
→ ミックスメーター → マスターゲイン → 共通PEQ → L/Rスワップ → 左右独立PEQ
→ 出力メーター → FL/FR に書き込み（ほかのチャンネルは 0）
```
左右独立PEQ はスワップの後に置いている。こうすることで、スワップしても左用の補正は常に物理的な左のイヤホンにかかる。

| ファイル | 内容 |
|---|---|
| `dsp/include/gdm/Engine.h` | 処理本体（リアルタイム処理中にロックもメモリ確保もしない。設定はロックフリーで受け渡す） |
| `dsp/include/gdm/Settings.h` | ユーザー設定（マトリクス、ミュート、遅延、LFE、PEQ、スワップ） |
| `dsp/include/gdm/ChannelMap.h` | AOSP のチャンネルマスクと既定のダウンミックス係数 |
| `dsp/include/gdm/Biquad.h` | RBJ の式による biquad（PK/LSC/HSC/LP/HP/BP/NO/AP） |
| `dsp/include/gdm/ApoParser.h` | Equalizer APO 形式の EQ ファイルを読み込む |
| `dsp/include/gdm/Meter.h` | チャンネルごとと L/R の peak / RMS（dBFS） |
| `dsp/tools/gdm_cli.cpp` | WAV を入れて WAV を出す PC 用ツール |

## ビルドとテスト（Git Bash）
```bash
bash scripts/build_pc.sh          # MSVC でビルドして単体テストを実行
python scripts/reference_check.py # SciPy で書いた別実装と出力を比べる
bash scripts/build_android.sh     # NDK で armeabi-v7a / arm64-v8a 向けにビルドできるか確認
```

PC で音を確かめる場合：
```bash
build/pc/Release/gdm_cli.exe in_7.1.4.wav out.wav --eq "upper 6kHz no touch ig955 eq.txt" --mute TBL,TBR --swap
```

## 参考資料
- `Dolby ON James ON.txt` / `Dolby ON James OFF.txt`：`dumpsys media.audio_flinger`（Apple Music のトラックは `0x0002D63F` = 7.1.4）
- `device_config/`：端末の `/vendor/etc/audio_effects*.xml`（実際に読み込まれているのは `audio_effects_sec.xml`）
