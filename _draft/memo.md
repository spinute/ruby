# What I did
* Implement Ruby Rope C extension and experimental automatic selection class
 * https://github.com/spinute/CRope
* Implement Rope into Ruby-Core and its automatic selection for String class
* Implement patch for Ruby core which enables multi-argument "prepend", concatenation and deletion of array and string
* Merge well-tuned open-oddress hashtable Ruby core (in progress)

# Rope/ERope extension
Usual string object in programming language is implemented as sequential buffer such as array in C language, and Ruby have also adopted array structure for string.
Rope is another data structure for string object, which expresses given string in tree structure.
Rope surpass arary-expression in some basic operation in the meaning of time complexity, e.g. concatenation, deletion and substring.
My main challange in Google Summer of Code 2016 (GSoC2016) is to introduce Rope into Ruby and enable users to enjoy its efficiency without awareness.

In some languages which put their emphasis on efficiency, explicit data structure are required for writing a efficient program.
However, it is not the way Ruby selected.
Ruby user tend to use a little data structure prepared in language and they do not want to dive into the depth of selecting proper data structure.
In this way, Ruby provide users with joy of programming and productivity, and I wanted to contribute that virtue by offering automatic data structure selection mechanism.

参照カウントを使って実装していたため、パフォーマンスが思ったほど出ず、concatによる連結と比較して性能が出なかったが、参照カウントの操作を無視するとconcat並みの性能が出ることを確認できた
ただし、参照カウントの操作をしないとメモリリークが発生することになり、この状態の拡張ライブラリは実際には使用できないが、この後Rubyコアへの組み込みを見越してのプロトタイプ実装であったため、拡張ライブラリにおいてGCを実装したりRubyのGCを利用するように修正することは行わなかった。
また、この成果を7月のRuby開発者会議で報告した

# 成果物
* MRI(Rubyのインタプリタ実装)における文字列実装へのRopeを使った内部表現の実装
* C言語で実装されたRope拡張ライブラリ, Gem
* 機能追加パッチひとつ(prepend, concat, deleteの多引数化)
* MRI内部で使われているハッシュテーブルの性能改善パッチのマージ

## 概要
このページはGoogle Summer of Code(GSoC)2016に採択された提案automatic data structure selection mechanism in Ruby interpreterの成果報告ページです。
以下、単にRubyといった時に、特にMRIの実装のことを指すことに注意します。(他のRuby実装としてJRuby, Rubiniusなどもありますが、内部実装には詳しくありません。)
このプロジェクトはプログラミング言語Rubyの処理系実装MRIのデータ構造の実装改善を目指すものです。
最終的な成果の中心としては文字列の実装として既存の配列ベースのものとは別に、Ropeと呼ばれる木構造による表現を実装し、木構造が有利な処理が文字列に適用される際には文字列の内部表現としてRopeを使う、という処理を導入しました。
最も主要な効果としては、既存の実装では文字列の連結に+演算子を使うと非常に遅く、Rubyの内部実装を知るプログラマは<<演算子を利用することでこの問題を回避していたのですが、+演算子を利用した際にも<<演算子を使用した場合と大差ない性能を透過的に得られるようになりました。
また、他にも部分文字列の取得や、中間文字の削除などの演算をRope表現を持つ文字列に対して適用した場合には計算量的に優位に実行することができるようになっています。

計算量の比較
Rope Array
結合 O(1) O(l)
削除 O()
部分文字列の取得

これらの計算量的に優位なデータ構造をユーザーが明示的に利用するための拡張ライブラリは存在していましたが、これはRubyで実装されていました。
このプロジェクトの成果のひとつとして、C言語での実装を行いました。

また、ユーザーからみて透過的に
C言語などのより低レベルな言語ではユーザーはデータ構造を選択し、その実装を行い、その後そのデータ構造を利用する手順が一般的です。
一方で、Rubyのような高レベルな言語のユーザーはこのような低レベルな詳細をできれば意識したくないものかと思います。
例えば、RubyにはListが言語のコアに含まれていません。Rubyのユーザーは基本的にArrayをlist, stack, queueの代わりに多くのオペレーションを備えたarrayを使います。
しかしながら、用途に応じて真に効率的なデータ構造が異なるのも事実です。
このプロジェクトでは、Rubyのこのような特徴を尊重しながら、ユーザーに効率的なデータ処理を提供することを目指しました。

## Rubyにおける文字列の実装と、Ropeの長所/短所
Rubyにおけるオブジェクトは
また、Rubyの文字列はmutableです。
str = str + str1
を実行すると、strの文字列の後にstr1の文字列をくっつけた文字列オブジェクトを新たに生成し、これをstrは保持します

一方で、
str << str2
では、strの文字列バッファの末尾に直接str2の文字列をコピーします
ただし、このときにバッファが小さい場合にはバッファの拡張が発生します。

このため、前者では新たなオブジェクトの生成str, str1両方のコピーが発生するのに対し、後者ではバッファの拡張が不要な場合にはコピーが一回発生するだけで済むため、後者が高速でした。
これは上の演算を繰り返すときに、strが何度もコピーされることが大きな原因であると考えられます。

今回のRopeを使った実装ではこの問題点をユーザーから見ると透過的に解決しています。
現在の実装では+演算までの実装となっていますが、このような最適化が可能なオペレーションは他にも数多く存在します。
例えば、
これは既にRopeは実装されているので各関数の処理を実装するだけでよく、実現可能な見通しはあるので今後の取り組もうかと考えています。

また、副次的な利点として、Rubyの文字列表現として、immutableなものを導入することができているという点があります。
immutableなオブジェクトは最適化をかけやすく、例えば並列操作が比較的容易といった点で今後のRubyの並列化の際に活用できる可能性もあるかと思います。

## C言語で実装されたRope拡張ライブラリ
これはRopeにおける文字列の処理を高速化することを確認するためのプロトタイプとして行いました。
具体的には、Ropeでは文字列の結合、削除、
一方で、添字を指定しての文字の取得は木をトラバースする必要が有るため、O(logn)程度の時間がかかります。
また、Rubyオブジェクトとしては木構造の全てのノードがそれぞれRubyのオブジェクトとなるため、Rubyのオブジェクトをノード数だけ生成するオーバーヘッドがあります。

既存のRopeの実装はRubyレベルでのものはあったものの、今回の実装はCレベルで行い、拡張ライブラリとしてRuby処理系に組み込みました。
また、オブジェクトの回収はRubyレベルではGCを使って行われているのですが、拡張ライブラリを作成する段階ではGCの動作をまだよく理解していなかったため、オブジェクトに参照カウントを埋め込んで、メモリの管理は独自で行いました。
参照カウントGCを自力で行う選択をしたことにより、文字列の結合や削除を行う際に木の内部の全てのノードの参照カウントを増減する必要が出てしまい、計算量的には結合O(n)、削除O(n)となってしまっている点は改善点です。
しかしながら、Ruby処理系に組み込む際にはRubyのGCを使いこの問題は解決されるため、ここでは放置しました。

## issue12333を実装したパッチの投稿
GSoCには実際にプロジェクトが始まる前の準備期間が1ヶ月ほどあり、この期間に僕はまずRubyの開発者向けドキュメントを読み、Ruby hacking guide, Ruby under the micrescopeというRubyの内部実装を解説する二冊の書籍を読み、Ruby under the micro scopeの著者Patのブログを読んだりしていました。
その後、Rubyの実装に実際に修正を加える体験をしてみようということで、RubyのIssueトラッカーに投稿されたissueの中から今回の対象範囲(String/Array/Hash)に関連のありそうなもので、かつ修正の方法の目処がつくものを選定しました。
これがオープンソースプログラムへの初めてのコミットとなります。(まだパッチはマージしていませんが、7月のRuby開発者会議の際に開発者のみなさまにフィードバックを頂きまして、まつもとさんに機能としてはあっていいんじゃないかという肯定的なお返事を頂きまして、現在修正パッチを投稿しています。)

## Ruby文字列実装のRope対応
Rubyの文字列実装は基本的にstring.cという10000行程度のひとつのファイルに含まれています。
内部的には
しかしながら、GCのふるまいや、Copy on Write、エンコーディングの対応など、理解の浅かった部分があり、かなり手間取りました。
この過程では現在のruby開発の中心であり、Rubyの内部実装に非常に詳しいメンター笹田さんから多くのアドバイスを頂きました。

コミットへのリンク
やりのこし
