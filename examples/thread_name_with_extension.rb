# concurrent-ruby needs to be installed
require "concurrent"
require "gvl-tracing"

def fib(n)
  return n if n <= 1
  fib(n - 1) + fib(n - 2)
end

GvlTracing.start("thread_name_with_extension.json")
pool = Concurrent::FixedThreadPool.new(5)

finished = Queue.new

40.times do
  pool.post do
    p fib(30)
    finished << true
  end
end

40.times { finished.pop }

GvlTracing.stop
