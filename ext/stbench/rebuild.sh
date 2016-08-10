#!/bin/bash
make
sudo make install
ruby bench.rb
