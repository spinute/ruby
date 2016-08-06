---
title: Main
layout: page
parmalink: /main/
---

# GSoC2016 spinute
このページはGoogle Summer of Code(GSoC)2016に採択された提案automatic data structure selection mechanism in Ruby interpreterの成果報告ページです。

## What I did
* Implement Ruby Rope C extension and experimental automatic selection class
  * https://github.com/spinute/CRope (whole repository is for this project)
* Implement Rope into Ruby-Core and its automatic selection for String class
  * https://github.com/spinute/Ruby
  * implement-ropestring branch is for this project
* Implement patch for Ruby core which enables multi-argument "prepend", concatenation and deletion of array and string
  * https://bugs.ruby-lang.org/issues/12333 (3 patches are posted)
* Merge well-tuned open-address hashtable into  Ruby core (in progress)

* C言語で実装されたRope拡張ライブラリの実装
  * https://github.com/spinute/CRope (whole repository is for this project)
* MRIの文字列のRopeを使った内部表現の実装
  * https://github.com/spinute/Ruby
  * implement-ropestringブランチがこの作業ブランチです
* RubyのArray, Stringクラスのprepend, concat, deleteを多引数化するパッチ
  * https://bugs.ruby-lang.org/issues/12333 にて議論を行い、考えられる幾つかの実装パッチを投稿しています
* MRI内部で使われているハッシュテーブルの性能改善パッチのマージ(作業途中)

## Introduction
Note: In this page, I wrote Ruby as MRI(Matz Ruby Interpreter). There are several implementation rather than MRI, e.g. JRuby(in Java), Rubynius(in C++ and Ruby itself) ...etc.

Naive implementation of string object in programming language is a sequential buffer such as array in C language, and Ruby has also adopted such a structure for string.
Rope is another data structure for string object, which expresses given string in tree structure.
Rope surpass array-expression in some basic operation especially in the meaning of time complexity, e.g. concatenation, deletion and substring.
My main challenge in Google Summer of Code 2016 (GSoC2016) is to introduce Rope into Ruby and enable users to enjoy its efficiency without awareness.

In some languages which put their emphasis on efficiency, explicit data structure are required for writing a efficient program.
However, it is not the way Ruby selected.
Ruby user tend to use a few data structure prepared in language and they do not want to dive into the depth of selecting proper data structure for efficiency.
In this way, Ruby provide users with joy of programming and productivity, and I wanted to contribute that virtue by offering automatic data structure selection mechanism which enables users to use efficient data structure transparently.

このページでは、RubyというとRuby処理系のうちのひとつであるMRI(Matz Ruby Interpreter)のことを指します。他の有名な処理系実装の例としては、Javaによる実装であるJRubyや、C++によりVMを実装しその他の大部分をRubyによって実装しているRubiniusなどがあり、MRIと共通する部分も多いかと思うのですが、必ずしもそうではないことにはご注意ください。

言語処理系における文字列のオブジェクトの実装の最も素朴なものとして、例えばC言語の配列などを利用し、連続したメモリ領域に文字列を保持する実装があります。
Rubyでも基本的にはこのような実装を採用しています。
他の文字列の表現として、木構造のものであるRopeというデータ構造があります。このデータ構造では文字列の削除や結合、部分文字列の取得などの操作を効率的に行うことができます。
このような複数のデータ構造には、それぞれ優位な操作がありますが、Rubyの処理系ではこのような多様なデータ構造を使い分けるデザインになっていません。
これは最も素朴のうちのひとつデータ構造であるListが用意されていないことからも理解できます。
Rubyのユーザーは多くの場合、Stack/Queue/Listなどのデータ構造の処理を全て、多用なメソッドを備えたArrayによって達成します。
ユーザーは低レベルなデータ構造の選択から解放され、より高レベルな仕事において生産性を発揮することができるのです。
一方で、用途に応じて真に効率的なデータ構造が異なり、それらを適切に使い分けることでより効率的な処理が行えることも事実です。
このプロジェクトでは、ユーザーには意識させることなく、処理系の内部でデータ構造を動的に切り替えることで、Rubyのユーザーにデータ構造の詳細を選択させない高レベルな設計とデータ操作の効率性とを両立することを目指しました。

## 概要
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
また、オブジェクトの回収はRubyレベルではGCを使って行われているのですが、拡張ライブラリを作成する段階ではGCの動作をまだよく理解していなかったため、オブジェクトに参照カウントを埋め込んで、メモリの管理は独自に行いました。
参照カウントGCを自力で行う選択をしたことにより、文字列の結合や削除を行う際に木の内部の全てのノードの参照カウントを増減する必要が出てしまい、計算量的には結合O(n)、削除O(n)となってしまっている点は改善点です。
しかしながら、参照カウントの操作を行わないようにすると(実際にはメモリリークしているものの)理論通りの性能が出ることが確認でき、Ruby処理系に組み込む際にはRubyのGCを使いこの問題は解決されるため、ここではこの問題は放置することにしました。

At first, I implemented Rope as an extension written in C.
The purpose of this implementation is prototyping; I was novice to Ruby internal before the project, so I divided complexities in the implementation of Rope string and modifying the behavior of Ruby.
I implemented Rope by not using Garbage Collector in Ruby core but using reference count prepared by myself, for simplicity.
However, reference counting traverses all the nodes in a tree when calling concat or delete methods, and hence these methods become slower than that of ideal time complexity of Rope data structure.
The purpose of prototyping is ensuring the possibility of the project, and I was possible to evaluate performance of the ideal implementation of Rope by turning off the reference counting, which means this extension include memory leak for accomplishing ideal performance, and it is the reason why I did not provide its extension as RubyGem (Package Management System for Ruby Language).
I decided to leave this problem because I tried to implement Rope string into Ruby interpreter itself and then the problem is automatically vanished by using Garbage Collector in Ruby.

## issue12333を実装したパッチの投稿
GSoCには実際にプロジェクトが始まる前の準備期間が1ヶ月ほどあり、この期間に僕はまずRubyの開発者向けドキュメントを読み、Rubyソースコード完全解説(http://i.loveruby.net/ja/rhg/book/), [Rubyのしくみ Ruby Under a Microscope](http://tatsu-zine.com/books/ruby-under-a-microscope-ja)というRubyの内部実装を解説する二冊の書籍に目を通したり、Ruby under the micro scopeの著者Patのブログを読んだりしていました。
また、オンラインにある開発者向けドキュメントとして、[Ruby C API reference](http://docs.ruby-lang.org/en/trunk/extension_rdoc.html), [Ruby Issue Tracking System](https://bugs.ruby-lang.org/projects/ruby/wiki/)などにも目を通していました。

その後、Rubyの実装に実際に修正を加える体験をしてみようということで、RubyのIssueトラッカーに投稿されたissueの中から今回の対象範囲(String/Array/Hash)に関連のありそうなもので、かつ修正の方法の目処がつくものを選定し、仕様を議論しながら実装を何種類か投稿しました。
これがオープンソースプログラムへの初めてのコミットとなります。(まだパッチはマージしていませんが、7月のRuby開発者会議の際に開発者のみなさまにフィードバックを頂きまして、まつもとさんに機能としてはまああっていいんじゃないかという肯定的なお返事を頂きまして、現在修正パッチを投稿しています。)

During my Community Bounding Period, I read two books ["Ruby Hacking Guide"](https://ruby-hacking-guide.github.io/), ["Ruby Under a Microscope"](https://www.nostarch.com/rum), and official online resources such as [Ruby C API reference](http://docs.ruby-lang.org/en/trunk/extension_rdoc.html), [Ruby Wiki](https://bugs.ruby-lang.org/projects/ruby/wiki/)at first.
After that I selected an issue posted in (Bug Tracker System of Ruby)[https://bugs.ruby-lang.org/issues] at the perspective of having relation with the topic in this project and not going too complicated.
I selected this issue [`String#concat`, `Array#concat`, `String#prepend` to take multiple arguments](https://bugs.ruby-lang.org/issues/12333) as my first activity diving into Ruby internal , and implemented a feature that enable methods such as concat, prepend and delete in Array and String to have have multiple arguments.
I discussed some details and possible implementation on the page, and posted 3 patches.
I also reported this implementation in a developer's meeting on July, and got some feedbacks about implementation from Ruby committers and positive response for this feature from Matz.

## Ruby文字列実装のRope対応
Rubyの文字列実装は基本的にstring.cという10000行程度のひとつのファイルに含まれています。
内部的には
しかしながら、GCのふるまいや、Copy on Write、エンコーディングの対応など、理解の浅かった部分があり、かなり手間取りました。
この過程では現在のruby開発の中心であり、Rubyの内部実装に非常に詳しいメンター笹田さんから多くのアドバイスを頂きました。
当初の目標では、Ruby処理系におけるデータ構造の自動動的選択というより広いスコープで問題を定義していました。
データをどのような形式で保持するかによってそのデータの集合に対しての操作がどの程度の時間で行うことができるかが変わってきます。
例としては、単純なデータの列を保持する際に、配列としてそれを保持するのか、リストによって保持するのかによって、前者ではランダムアクセスが定数時間で行うことができるのに対して、後者では列の中間付近への新たなデータの挿入や削除に優位性があります。
Rubyでのこれまでの選択は、配列を使い、リストは提供しない、というものでした。これはユーザーから見ての簡単さという意味では優れていますが、パフォーマンスクリティカルなコードを書く際には問題となることもあるだろう、という見込みでした。
ここで、このプロジェクトの目標は、ユーザーから見るとひとつのデータ構造だが、内部では動的にデータ構造を入れ替え、複数のデータ構造の利点を併せ持つ動的なクラスを作成することでした。

オブジェクトに対してメソッドが繰り返し行われます。メソッドの引数とオブジェクトの状態に依って、メソッドのコストが決定します。
ただしここでメソッドのコストを完全に決定することは全てのメソッドで効率的に行うことができるわけではありません。
一部のメソッドではオブジェクトの状態、引数によって変化するメソッドのコストが、実際にメソッドを実行するのと同程度の複雑さでした評価できないものもあります。
一方で、一部のメソッドでは、実際に実行せずとも操作のコストを効率的に見積もることが出来ます。
例えば文字列の例で考えてみますと、文字列の長さが多くの場合の文字列の計算量的なサイズとなります。
文字列のconcatメソッドでは結合文字列の長さの和が出力文字列の長さとなり、効率よく結果を見積もることが出来ます。
一方、substrメソッドでは出力文字列の長さはそれほど簡単には見積もることができないでしょう。
さらに、ランダム性の関わるchoiceメソッドなどでは事前には正確な値を得ることはできず、せいぜい見積もりしかできなくなってしまいます。

ここで、concatメソッドのような、入力から出力のサイズが決定的/効率的にわかるようなメソッドについてはRope, List, 配列によるメソッドの実装の実行コストを実行前に比較することができます。
メソッドの実行列が与えられたとき、どの表現におけるメソッドの処理が最も効率的であるかは、動的計画法によって効率的に計算できる、というのが当初の目論見でした。

しかしながら、このアイデアはRubyにおける命令の列を先読みするような方法が現状ないということでもう少し簡明な方法を取ることにしました。
先の手法では命令を先読みし、命令列に対しての適切な文字列表現の遷移を計算するというものでしたが、簡明版では先読みはせず、逐次の命令列に対してある表現が優位なことがわかっているメソッドが実行される際には表現を変換してから操作を実行します。

具体的には、文字列の結合が挙げられます。
Rubyの+演算子によって文字列の結合を行う際、

他にも、

コミットへのリンク
やりのこし
