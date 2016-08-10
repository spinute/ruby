require "STBench"
require "benchmark"

def bench_insert(keytype, pattern, ht_init_size=0, scale=10, keylen=5)
    puts "Insert bench: %-15s %-15s %-12s %-15s %s" % ["keytype=#{keytype},", "pattern=#{pattern},", "scale=#{scale},", "ht_init_size=#{ht_init_size},", "keylen=#{keylen}"]
    pid = Process.fork do
	bench = STBench.new(keytype, ht_init_size, scale, pattern, keylen)
	bench.setup
	puts Benchmark.measure { bench.insert }
	bench.cleanup
    end
    Process.waitpid pid
end

def bench_init(keytype, ht_init_size=0, scale=1)
    puts "Init bench: %-15s %-12s %s" % ["keytype=#{keytype},", "scale=#{scale}", "ht_init_size=#{ht_init_size}"]
    pid = Process.fork do
	bench = STBench.new(keytype, ht_init_size, scale, "rand", 432) # pattern is arbitrary
	bench.setup
	puts Benchmark.measure { bench.ht_init }
	bench.cleanup
    end
    Process.waitpid pid
end

bench_insert 'num', 'same'
bench_insert 'num', 'rand'
bench_insert 'num', 'diff'
bench_insert 'str', 'same'
bench_insert 'str', 'rand'
bench_insert 'str', 'same', 0, 1, 1000
bench_insert 'str', 'rand', 0, 1, 1000

bench_init 'num'
bench_init 'str'
4.times do |i|
    bench_init 'num', 100**i
    bench_init 'str', 100**i
end
