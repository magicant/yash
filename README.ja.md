Yash: yet another shell
=======================

https://magicant.github.io/yash/ja/
This README is also available in [English](README.md).


このプログラムは C 言語 (C99: ISO/IEC 9899:1999, JIS X 3010:2003) で
書いた Unix 系 OS 用コマンドライン端末シェルです。Yash は世界で最も
正確な POSIX 規格準拠を目指しながらも、対話シェルおよびシェルスクリプト
実行プログラムとして便利な機能を備えています。

 * グローバルエイリアス
 * 配列
 * ソケットリダイレクト・パイプリダイレクト・プロセスリダイレクト
 * ブレース展開・拡張パス名展開
 * 数式展開での小数の計算
 * プロンプトコマンド
 * コマンドが見付からないときのハンドラー
 * コマンドライン補完 (100 個以上のコマンドに対する補完設定付き)
 * コマンド履歴に基づくコマンドラインの推定

このプログラムは GNU General Public License (Version 2) の元で自由に
再配布・変更などができます。その代わり、プログラムの利用は全て各自の
自己責任の下で行っていただくことになります。作者はプログラムの瑕疵に
対して一切責任を取りません。ライセンスの本文については [COPYING](COPYING)
ファイルを参照してください。

このプログラムの開発は [GitHub](https://github.com/magicant/yash) で
管理されています。
以前は [OSDN](https://osdn.jp/projects/yash/) で管理されていました。


## 開発状況

Yash は既に POSIX.1-2024 (IEEE Std 1003.1) をほぼ全てサポートしています。
規格を満たしていない部分についての詳細は
[マニュアル](https://magicant.github.io/yash/doc/ja/posix.html#nonconformance)
を参照してください。

Yash は安定しています。バグ修正のためのバージョンアップはおおよそ三箇月
に一度リリースされます。変更履歴は [NEWS.ja](NEWS.ja) ファイルにあります。


## 要件

Software Development Utilities オプションと C-Language Development
Utilities オプションを備えた任意の POSIX.1-2001 環境でビルド・実行
できるはずです。

今のところ yash の動作は主に Fedora と macOS で検証されています。


## インストール

Yash をビルドしインストールする方法については [INSTALL.ja](INSTALL.ja)
ファイルを 参照してください。

インストール後、マニュアルは

    $ man yash

で参照できます。また <https://magicant.github.io/yash/doc/ja/> で
オンラインで読むこともできます。


## 基本設定

以下は、yash を使い始めるにあたって有用な設定の手引きです。設定の詳細は
マニュアルを参照してください。

### 初期化スクリプト

Yash がログインシェルとして起動されたとき、`~/.yash_profile` を読み込み
ます。このファイルはシェルスクリプトとして実行されます。この中で
export コマンドを使うことで環境変数を設定できます。

Yash が対話シェルとして起動されたとき、(`~/.yash_profile` も読み込む場合
はその後で) `~/.yashrc` を読み込みます。この中でエイリアス、プロンプト、
コマンド行編集のキーバインド設定、コマンドが見付からないときのハンドラ
ー等を設定します。サンプルとして [share/initialization/sample](
share/initialization/sample) ファイルを参考に自分用の `~/.yashrc`
を設定してください。

### Yash をログインシェルにする

多くの Unix 系 OS では、ログインシェルとして使用可能なシェルは
`/etc/shells` ファイルに名前が挙げられている必要があります。このファイル
を編集し、yash へのパスがファイルに含まれているようにしてください。

その後、端末で `chsh` コマンドを実行してログインシェルを yash に変更し
てください。環境によってはこれ以外の方法でログインシェルを設定するよう
になっているかもしれません。詳しくはお使いの環境のマニュアルを参照くだ
さい。


## 実装上の注意事項

 * C 言語では、ナル文字は文字列の終わりを表します。そのため、入力した
   ファイルの内容や文字列にナル文字が含まれているとそれ以降の部分が正
   しく処理されなくなってしまいます。
 * ソース内の随所で GCC の拡張機能である `__attribute__` キーワードを
   使用しています。GCC および Clang 以外の環境ではプリプロセッサによっ
   て消去するようにしていますが、`__attribute__` 識別子を他の用途で使
   用している処理系ではコンパイルに支障をきたすかもしれません。他にも
   いくつか `_` で始まる識別子を使用しているため、非常に特殊な環境では
   正しくコンパイルできないかもしれません。
 * いくつかのシグナルは特定の番号を持っていると仮定しています:
     SIGHUP=1 SIGINT=2 SIGQUIT=3 SIGABRT=6
     SIGKILL=9 SIGALRM=14 SIGTERM=15
 * ファイルアクセス権フラグは特定の値を持っていると仮定しています:
   ```
   0400=user read    0200=user write   0100=user execute
   0040=group read   0020=group write  0010=group execute
   0004=other read   0002=other write  0001=other execute
   ```
 * POSIX ロケール以外のロケールでの文字クラスの分類は POSIX ロケールで
   の分類と上位互換であると仮定しています。
 * Yash は、シグナルによって強制終了させられた子プロセスの終了状態を
   親プロセスに伝搬するために `setrlimit` 関数を必要とします。この動作は
   `setrlimit` 関数が利用できない場合に無効になります。その場合 yash の
   動作は POSIX を満たしません。


## 既知の問題

 * 行連結を含む複雑な展開があると行番号 (`$LINENO`) が正しく計算されない
   ことがあります。
 * Solaris では一部のロケールで非 ASCII 文字が正しく扱われないことが
   あります。`config.h` ヘッダーファイルで `HAVE_WCSNRTOMBS` マクロの定義
   を削除してビルドすると問題を回避できるかもしれません。


## お知らせ

感想・提案・バグ報告等を歓迎します。ただし、全ての提案やバグ報告に
応じることを約束するものではありません。

 * [課題管理システム](https://github.com/magicant/yash/issues)
 * [掲示板](https://github.com/magicant/yash/discussions)

翻訳に興味がおありの場合は
[TRANSLATING.md](TRANSLATING.md) をご覧ください


----------------------
渡邊裕貴 <magicant@wonderwand.net>
