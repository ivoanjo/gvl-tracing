require "gvl-tracing"

def fib(n)
  return n if n <= 1
  fib(n - 1) + fib(n - 2)
end

GvlTracing.start("example3.json")

other_ractor = Ractor.new { fib(37) } # runs in other ractor
fib(37) # runs in main thread

other_ractor.take
GvlTracing.stop
