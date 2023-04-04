# conrrent-ruby needs to be installed
require "concurrent"
require "gvl-tracing"

def fib(n)
  return n if n <= 1
  fib(n - 1) + fib(n - 2)
end

GvlTracing.start("examples/thread_name_with_extension.json")
pool = Concurrent::FixedThreadPool.new(5)

40.times do
  pool.post do
    p fib(30)
  end
end
GvlTracing.stop
