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

## 段階0：エフェクトライブラリと Magisk モジュール

| 場所 | 内容 |
|---|---|
| `effect/src/EffectMain.cpp` | AOSP エフェクトのラッパー（`AELI`）。音楽ストリームのセッションに postprocess として付き、トラックのチャンネル配置（7.1.4）のまま受け取る |
| `effect/src/Protocol.h` | パラメータの番号（状態・メーター・ミュート・スワップ・マスター・設定ファイル再読込）。今後アプリからも使う |
| `effect/tools/effect_host_test.cpp` | `/data/local/tmp` で .so を読み込んで動作確認するテスト（システムには触れない） |
| `module/` | Magisk モジュール。インストール時に端末の `audio_effects_sec.xml` を元にして、上書き用ファイルを作る |
| `module/system/vendor/etc/galaxy_downmixer/options.conf` | 端末上の設定（有効/無効、スワップ、ミュート、マスター、EQ ファイル、ログの間隔） |

```bash
bash scripts/package_module.sh   # .so をビルドして build/GalaxyDownmixer-<version>.zip を作る
bash scripts/install_module.sh   # Magisk でインストール（再起動は手動）
bash scripts/device_check.sh     # 再起動して Apple Music の Atmos を再生しながら実行（読み取りのみ）
```

元に戻す方法：Magisk アプリで削除するか、`adb shell "su -c 'touch /data/adb/modules/galaxy_downmixer/remove'"` を実行して再起動する。起動がおかしいときは Magisk のセーフモード（起動中に音量下ボタンを押し続ける）で起動する。

## 参考資料
- `Dolby ON James ON.txt` / `Dolby ON James OFF.txt`：`dumpsys media.audio_flinger`（Apple Music のトラックは `0x0002D63F` = 7.1.4）
- `device_config/`：端末の `/vendor/etc/audio_effects*.xml`（実際に読み込まれているのは `audio_effects_sec.xml`）
