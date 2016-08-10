require "STBench"
require "benchmark"

def run_bench(keytype, pattern, ht_init_size=0, scale=10, keylen=5)
    print "%15s %15s %12s %15s %12s: " % ["keytype=#{keytype},", "pattern=#{pattern},", "scale=#{scale},", "ht_init_size=#{ht_init_size},", "keylen=#{keylen}"]
    puts Benchmark.measure {
	bench = STBench.new(keytype, ht_init_size, scale, pattern, keylen)
	bench.insert
    }
end

run_bench 'num', 'same'
run_bench 'num', 'rand'
run_bench 'num', 'diff'
run_bench 'str', 'same'
run_bench 'str', 'rand'
run_bench 'str', 'same', 0, 1, 1000
run_bench 'str', 'rand', 0, 1, 1000
