# AGENTS.md（PlatformIO 開発用）

このリポジトリは PlatformIO を前提に開発する。エージェントは **`platformio.ini` を唯一のビルド定義**として扱い、環境（env）を跨ぐ変更は慎重に行う。
推論は英語で行い、報告は日本語で行うこと。

---

## まず最初に読むもの

- `platformio.ini`：環境定義・ビルドフラグ・依存関係の一次情報
- `src/`：実装
- `include/`：公開ヘッダ（必要な場合）
- `lib/`：ローカルライブラリ
- `test/`：PlatformIO Unit Test
- `scripts/`：ビルド補助（extra_scripts 等）
- `data/`：SPIFFS/LittleFS 等のデータ（使用する場合）
- `docs/`: ドキュメント

---

## 必須コマンド（コピペで動く形）

### 環境一覧
- `pio project config`

### ビルド
- 既定 env：`pio run`
- env 指定：`pio run -e <env>`

### フラッシュ（書き込み）
- `pio run -e <env> -t upload`

### シリアルモニタ
- `pio device monitor -b <baud> -p <port>`
- 例：`pio device monitor -b 115200 -p /dev/ttyUSB0`

### テスト
- 全テスト：`pio test -e <env>`
- フィルタ：`pio test -e <env> -f <pattern>`

### 静的解析（設定がある場合）
- `pio check -e <env>`

### クリーン
- `pio run -t clean -e <env>`

---

## env の扱い（最重要）

- 変更・追加する前に `platformio.ini` の `env:` ブロックを確認する
- **env ごとにフレームワークやボード設定が異なる**ことがある（例：Arduino / ESP-IDF、Mbed、STM32 HAL 等）
- `build_flags` / `lib_deps` / `monitor_speed` / `upload_port` / `debug_tool` は env ごとに分ける
- 既存 env を壊さないため、変更後は少なくとも以下を実行する  
  - `pio run -e <主要env>`  
  - `pio test -e <主要env>`（テストがある場合）

---

## 依存関係（libraries）

- 追加は原則 `platformio.ini` の `lib_deps` に記載する
- バージョンは可能なら固定する（再現性のため）
- `lib/` 直下に置くローカルライブラリは、外部依存と混ぜない（役割を分離する）

---

## 設定値・秘密情報

- Wi-Fi SSID / API keys / 証明書などの秘匿情報はコミットしない
- 例：`include/secrets.h` / `.env` / `secrets.example.h` のようにテンプレを用意して運用する
- `platformio.ini` に直書きが必要な場合は `build_flags` で参照し、値自体は別管理にする

---

## ハードウェア依存の注意

- ピン配置、I2C/SPI 設定、クロック設定等はボード差分が出やすい
- 新しいボード/派生機を追加する場合は
  - env を追加し、差分を `platformio.ini` に閉じ込める
  - 既存 env の挙動を変えない（互換維持）

---

## ログ・デバッグ

- `Serial`/UART ログは、ビルドフラグで ON/OFF 可能にする（サイズ増加を抑える）
- 可能なら以下の形に統一する  
  - `LOG_LEVEL`（例：ERROR/WARN/INFO/DEBUG）  
  - `LOG_TAG`（モジュール名）
- デバッグ（対応している場合）
  - `pio debug -e <env>`
  - `debug_tool` / `debug_init_break` は env に閉じ込める

---

## コード規約（最低限）

- 副作用のある初期化は `setup()`/初期化関数に集約し、グローバル初期化は避ける
- ISR（割り込み）内では動的確保・重い処理を避ける
- ヒープ/スタック使用量に注意し、巨大配列は `static`/`PROGMEM` 等を検討する（環境依存）
- 例外（C++ exceptions）は env の設定に従う（無効な環境が多い）

---

## ファイルの追加・変更ルール

- `src/`：アプリ本体（エントリポイントは env/フレームワークに従う）
- `include/`：外部公開ヘッダ（不要なら増やさない）
- `lib/`：再利用コンポーネント（README を添える）
- `test/`：ユニットテスト（ホスト実行/組み込み実行の違いを明記）
- 生成物はコミットしない  
  - 例：`.pio/`, `*.elf`, `*.bin`, `*.map` など

---

## CI（ある場合の前提）

- CI で `pio run -e <env>` が動くようにする
- `upload` は CI で実行しない（物理デバイスが必要なため）
- テストは実行可能な形（ホスト向け `native` env など）があると望ましい

---

## 境界条件（Always / Ask first / Never）

### ✅ Always
- 変更前に `platformio.ini` を読む
- env 指定でビルド・テストを実行する（最低 1 つは通す）
- 依存追加は `lib_deps` に記載する
- 生成物をコミットしない

### ⚠️ Ask first（事前相談）
- env の削除/統合、既存 env の `framework`/`board` 変更
- フラッシュ手順や fuse/bootloader を伴う変更
- 暗号鍵・証明書・シリアル番号などの取り扱い方針変更
- `extra_scripts` やビルドパイプラインを大きく変える変更

### 🚫 Never
- 秘密情報をコミットする
- `.pio/` をコミットする
- env を跨いで動作が変わる変更を、検証なしに入れる

---

## 変更時に残す情報（PR/コミットに含める）

- 対象 env（例：`env:esp32dev`）
- 実行したコマンド（例：`pio run -e esp32dev && pio test -e native`）
- 動作確認内容（実機が必要なら、確認した手順と結果）
