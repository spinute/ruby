---
title: Final Report
layout: page
parmalink: /gsoc2016/english/
---

[日本語のページ]({{site.url}}/gsoc2016/japanese)

This page is a report of my project in Google Summer of Code(GSoC)2016.
[Automatic-selection mechanism for data structures in MRI](https://summerofcode.withgoogle.com/projects/#4576418910437376)

## Acknowledgement
I would like to express my best thanks to Mr. Koichi Sasada for mentoring whole my project. His great insight into Ruby helps me much and provides me much more interesting discussions beyond the range I actually have worked on.
I also would like to thank those who manage Google Summer of Code for offering students including me with the precious opportunity in this summer.

## What I did
* Implement Ruby Rope C extension and experimental automatic selection class
  * <https://github.com/spinute/CRope> (whole repository is for this project)
* Implement Rope into Ruby-Core and its automatic selection for String class
  * <https://github.com/spinute/Ruby>
  * "implement-ropestring" branch is for this section of the project
* Implement patches for Ruby core which enables concat and prepend methods in Array and String class to take multiple arguments
  * <https://bugs.ruby-lang.org/issues/12333> (3 patches are posted)
* Merge a well-tuned hashtable into Ruby core (in progress)

## Introduction
Note: There are several implementations of Ruby language e.g. MRI(Matz Ruby Interpreter, written in C), JRuby(in Java), Rubynius(in C++ and Ruby itself) ...etc. In this report, I have written Ruby as MRI.

Classical implementation of string object in programming language is a sequential buffer such as array in C language, and Ruby has also adopted such a structure for string.
Rope is another data structure for string object, which expresses a given string in a tree structure.
Rope surpasses array-expression in some basic operations especially in the meaning of time complexity, e.g. concatenation, deletion and substring.

My main challenge in Google Summer of Code 2016 (GSoC2016) is to introduce Rope into Ruby and enable users to enjoy its efficiency unconsciously.
In some languages which place their emphasis on efficiency, explicit data structures are required to write an efficient program.
However, that is not the approach Ruby has selected.
Ruby users tend to use a few data structures prepared in the language and they usually do not want to dive into the depth of selecting proper data structures for efficiency.
In this way, Ruby provides users with productivity and the joy of programming, and I have contributed to that virtue by offering automatic data structure selection mechanism which enables users to use efficient data structures transparently.

## Background
TODO: write content

## Ruby Rope C extension and experimental automatic selection class

Firstly, I implemented Rope as an extension written in C.
The purpose of this implementation is prototyping; I was novice to Ruby's internal before the project, so I divided complexities in the implementation of Rope string and the modification of the behavior of Ruby.
I implemented Rope by not using Garbage Collector in Ruby core but by using reference count which I prepared by myself for simplicity.
However, reference counting visits all the nodes in a tree when calling concat or delete methods, and hence these methods become slower than that of ideal time complexity of Rope data structure.
The purpose of prototyping is to ensure the possibility of the project, and I succeeded to evaluate the performance of the ideal implementation of Rope by turning off the reference counting, meaning this extension includes memory leaks for accomplishing ideal performance, and it is the reason why I did not provide its extension as RubyGem (Package Management System for Ruby Language).
I decided to leave this problem because I tried implementing Rope string into Ruby interpreter itself and the problem automatically vanished by using Garbage Collector in Ruby.
## Implementation of Rope into Ruby core
At last, I implemented Rope into String class of Ruby interpreter.
When String#concat is called, a state of String object changes into Rope.
On the other hand, when string as an array is needed, Rope string is converted into array string automatically.

As a note, in my proposal, I thought of the problem in wider scope.
The efficiency of the operation is determined by the state of the data, and its expression as a data structure.
For example, to hold sequence of characters, when Array is used then random accessing by index is performed efficiently, on the other hand, when List is used then insertion or deletion of internal characters is efficient, while its not efficient in Array case.
Ruby has offered Array as universal data structure, and not offer other normal data structures such as List, Queue, Stack.
This design is easy to use and provides users with concentration on their high level tasks, however, sometimes it is proved to be inefficient.
The goal of this project is to develop a mechanism which uses multiple data structures internally and switches them dynamically, and, on the other hand, which looks just a normal object for users.

During a life of an object
TODO: write content
TODO: add evaluations

## Post patches for issue#12333
During my Community Bounding Period, I read two books ["Ruby Hacking Guide"](https://ruby-hacking-guide.github.io/) and ["Ruby Under a Microscope"](https://www.nostarch.com/rum), and official online resources such as [Ruby C API reference](http://docs.ruby-lang.org/en/trunk/extension_rdoc.html), [Ruby Wiki](https://bugs.ruby-lang.org/projects/ruby/wiki/).
After that I selected an issue posted in [Bug Tracker System of Ruby](https://bugs.ruby-lang.org/issues) at the perspective of having relations with the topic in this project without going too complicated.
I selected this issue [\"String#concat, Array#concat, String#prepend to take multiple arguments\"](https://bugs.ruby-lang.org/issues/12333) as my first activity diving into Ruby internal , and implemented a feature that enables methods such as concat and prepend in Array and String to have multiple arguments.
I discussed some details and possible implementations in the page, and posted 3 patches.
I also reported this implementation in a developer's meeting in July, and received some feedbacks about implementation from Ruby committers and a positive response for this feature from Matz.
