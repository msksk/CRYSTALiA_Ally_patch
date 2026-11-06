# 絆きらめく恋いろは - ROG Ally 内蔵ディスプレイ黒画面修正

## 症状

内蔵ディスプレイ (1600x900 @ 120Hz) で `mekuiro.exe` を起動すると、
ウィンドウは作られるが黒画面のまま。HDMI 外部モニターでは正常に起動する。

## 原因

ゲームエンジン `Pal.dll` の初期化 (`PalInit`) がウィンドウを最大化表示し、
その同期 `WM_SIZE(SIZE_MAXIMIZED)` ハンドラがレンダースレッドIDへ
`PostThreadMessageA` を送る。しかしレンダースレッドはまだ作成されておらず、
ID は 0。`PostThreadMessageA(0, ...)` は必ず失敗し、エンジンは
メッセージポンプ付きの無限リトライに入る。

`PalInit` が戻らず `PalRun` に到達しないため、レンダースレッドが永久に
作られず、黒画面のままになる。

## 修正

ゲームフォルダの `d3d9.dll` (この修正版) が `Direct3DCreate9` 呼び出し時に
`Pal.dll` のインポートテーブルから `USER32!PostThreadMessageA` を探して
フックする。スレッドID 0 への投稿は「成功」として返す。
スレッドID 0 は実在しないため、この扱いでゲーム側のリトライが終了する。

ゲーム本体のファイル (`mekuiro.exe`, `Pal.dll`, `*.pac` など) は変更していない。

## 確認環境

- ASUS ROG Ally 内蔵ディスプレイ 1600x900 @ 120Hz
- Windows 11 build 26100 / AMD Radeon Graphics
- ゲーム: 絆きらめく恋いろは (mekuiro_dl)
- `Pal.dll` SHA256:
  `E9A57310313FFD5DE3755139DDA2C74BC0430A4893EBA3D9CB142C82474B91DF`
- 動作確認済み `d3d9.dll` SHA256:
  `800D3CA43C778EDC60997742D166823446A7DDEC8749F726AAD1B1F16E94F660`

動作確認済みの DLL は `d3d9_final.dll` としてこのフォルダに保存している。

## ビルド

32-bit Windows 用の `d3d9.dll` を Docker でビルドする方法は、次の 2 通り。
どちらも Docker と BuildKit が必要で、Linux コンテナ内の Zig で
クロスコンパイルする。ゲーム本体のファイルは変更しない。

- 付属スクリプト (`build.ps1`) を使う: Docker コマンドを意識せず、ビルドからインストールまで実行できる
- Docker コマンドを直接使う: `Dockerfile` を直接実行し、成果物を自分でコピーする

どちらも `_d3d9fix\out\d3d9.dll` を作るところまでは同じ。違いは、
ゲームフォルダへのコピーを自動で行うか、手動で行うか。

通常は `build.ps1` を使うのが簡単。Docker コマンドを自分で実行したい場合は
「Docker」の手順を使う。

Docker を使わない場合は「Zig ネイティブビルド (Docker 不使用)」を参照。

### 共通の前提

- Docker Desktop (Windows) または Docker Engine 20.10 以降
- 初回ビルド時のインターネット接続 (Zig 0.17.1 をダウンロード)
- BuildKit が有効であること (最近の Docker は既定で有効)

### 付属スクリプト (build.ps1)

`build.ps1` は Docker コマンドをラップしたスクリプト。
内部で `docker build` を実行するため、Docker は必要。

`_d3d9fix` フォルダで実行する。

#### ビルドのみ

```powershell
cd _d3d9fix
.\build.ps1
```

成功すると `_d3d9fix\out\d3d9.dll` が作成され、サイズと SHA256 が表示される。
`build.ps1` は成果物が 32-bit PE (x86) の DLL であることも確認する。

#### ビルドしてゲームフォルダへインストール

ゲームを終了してから実行する。`..\d3d9.dll` を上書きする。

```powershell
.\build.ps1 -Install
```

#### 実行ポリシーで止まる場合

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

#### build.ps1 が行うこと

1. `docker build` を実行する
2. `out\d3d9.dll` を 32-bit PE か確認する
3. サイズと SHA256 を表示する
4. `-Install` 指定時だけ `..\d3d9.dll` へコピーする

### Docker

`build.ps1` を使わず、Docker コマンドを直接実行する場合の手順。
ビルド後に DLL を手動でコピーする点が `build.ps1` との違い。

#### ビルド

```powershell
cd _d3d9fix
docker build --output "type=local,dest=./out" .
```

成功すると `_d3d9fix\out\d3d9.dll` が作成される。
`out` フォルダに以前の成果物がある場合は、先に削除するか別の出力先を指定する。

必要なら SHA256 を確認する。

```powershell
Get-FileHash .\out\d3d9.dll -Algorithm SHA256
```

#### ゲームフォルダへコピー

ゲームを終了してから、`out\d3d9.dll` をゲームフォルダの `d3d9.dll` へ
上書きコピーする。

```powershell
Copy-Item .\out\d3d9.dll ..\d3d9.dll -Force
```

#### ビルド内容

`build.ps1` も同じ `Dockerfile` を使う。

- ベースイメージ: `debian:bookworm-slim`
- Zig 0.17.1 (Linux x86_64) を公式サイトからダウンロード
- ダウンロードした tar.xz の SHA256 を照合
- `zig cc --target=x86-windows-gnu -shared -O2` でコンパイル
- `scratch` ステージから `d3d9.dll` だけを出力

Zig のバージョンを変更する場合は、`Dockerfile` の `ARG ZIG_VERSION` と
`ARG ZIG_SHA256` を書き換える。SHA256 は
`https://ziglang.org/download/index.json` の `x86_64-linux.shasum` で確認できる。
ファイル名の形式が変わった場合は、`Dockerfile` 内のダウンロード URL も修正する。

### Zig ネイティブビルド (Docker 不使用)

Windows 版 Zig 0.17.1 を用意し、`_d3d9fix` フォルダで実行する。

```powershell
zig cc --target=x86-windows-gnu -shared -O2 -o d3d9.dll d3d9proxy_final.c d3d9.def
```

Visual Studio は不要。生成した `d3d9.dll` をゲームフォルダへコピーする。

### ハッシュについて

Zig のビルドにはソースパスなどのメタデータが含まれるため、再ビルドした
DLL の SHA256 は確認済みのものと一致しない場合がある。
エクスポート関数は同一です。動作確認済みの DLL は `d3d9_final.dll` に
保存しているので、問題があればそちらを使う。

## 元に戻す

ゲームフォルダの `d3d9.dll` を削除する。
(このゲームには元々 `d3d9.dll` が含まれていない)

または `_d3d9fix\restore.ps1` を実行する。

```powershell
cd _d3d9fix
.\restore.ps1
```

## 注意

- `Pal.dll` が更新された場合、インポート名検索で自動対応するが、
  見つからない場合は `%TEMP%\mekuiro_d3d9fix.log` にエラーが残る。
- この `d3d9.dll` はゲームフォルダ内でのみ有効。システムには影響しない。
- セーブデータ (`save\*.dat`) はゲームが自動生成したもの。消さないこと。
- ゲームを再インストールした場合は、この `d3d9.dll` を再度コピーする。
- 起動時に `d3d9.dll` が別ファイルに戻される場合は、
  `_d3d9fix\d3d9_final.dll` を `..\d3d9.dll` にコピーし直す。
- Docker ビルドは初回に Zig をダウンロードするため、オフライン環境では
  ネイティブビルドを使う。

## ファイル

- `..\d3d9.dll` : 修正版 (ゲームが読み込む)
- `d3d9_final.dll` : 動作確認済み DLL のバックアップ
- `d3d9proxy_final.c` : プロキシのソース
- `d3d9.def` : DLL エクスポート定義
- `Dockerfile` : Docker ビルド定義
- `.dockerignore` : Docker ビルドコンテキストの制限
- `build.ps1` : Docker ビルド / インストールスクリプト
- `restore.ps1` : 元に戻すスクリプト
- `README.md` : このファイル
- `out\d3d9.dll` : Docker ビルドの成果物 (ビルド時に生成)
