# CRYSTALiA作品向け ASUS ROG Ally用パッチ

## 概要

- ASUS Rog AllyでCRYSTALiAのPCゲームを動作するようにする修正パッチです。
- 手元では以下の作品で動作のを確認しています。
    - 絆きらめく恋いろは
    - 紅月ゆれる恋あかり
- 同じエンジンを使用している他の作品でも有効と思われますが未確認です。

## ビルド

ビルドに成功するとoutフォルダー内に `d3d9.dll` が作成されるので、dllをコピーしてexeなどがあるフォルダーに設置してください。

### 付属スクリプト (build.ps1)

```powershell
.\build.ps1

# 実行ポリシーで止まる場合
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

### Docker

```powershell
docker build --output "type=local,dest=./out" .
```

### Zig ネイティブビルド

※Windows 版 Zig 0.17.1 が必要です

```powershell
zig cc --target=x86-windows-gnu -shared -O2 -o d3d9.dll d3d9proxy_final.c d3d9.def
```

## 元に戻す

設置した `d3d9.dll` を削除してください。

## 詳しい人向け詳細

### 原因

- ゲームエンジン `Pal.dll` の初期化関数 (`PalInit`) がウィンドウを最大化表示する
- 同期ハンドラ `WM_SIZE(SIZE_MAXIMIZED)` がレンダースレッドIDへ`PostThreadMessageA` を送る
- しかし、その時点ではレンダースレッドがまだ作成されていないので ID が 0 となる
- それにより`PostThreadMessageA(0, ...)` が必ず失敗し、ゲームエンジンがメッセージポンプ付きの無限リトライ状態になる
- さらに`PalInit` が戻らず `PalRun` に到達しないため、レンダースレッドが永久に作られず、黒画面のままになる。

### パッチ内容

- `d3d9.dll` が `Direct3DCreate9` 呼び出し時に`Pal.dll` のインポートテーブルから `USER32!PostThreadMessageA` を探してフックする
- スレッドID 0 へのPostは「成功」として返す
- スレッドID 0 が実在しないので強制的にゲーム側のリトライが打ち切られる
