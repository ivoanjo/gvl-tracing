require "gvl-tracing"

def fib(n)
  return n if n <= 1
  fib(n - 1) + fib(n - 2)
end

GvlTracing.start("example1.json")

Thread.new { sleep(0.05) while true }

sleep(0.05)

3.times.map { Thread.new { fib(37) } }.map(&:join)

sleep(0.05)

GvlTracing.stop
