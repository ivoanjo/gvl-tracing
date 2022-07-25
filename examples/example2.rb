require "gvl-tracing"

def fib(n)
  return n if n <= 1
  fib(n - 1) + fib(n - 2)
end

GvlTracing.start("example2.json")

other_thread = Thread.new { fib(37) } # runs in other thread
fib(37) # runs in main thread

other_thread.join
GvlTracing.stop
