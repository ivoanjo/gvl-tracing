require "gvl-tracing"

def fib(n)
  return n if n <= 1
  fib(n - 1) + fib(n - 2)
end

ready_queue = Queue.new
can_start_queue = Queue.new

GvlTracing.start("example4.json")

threads = 2.times.map do |id|
  Thread.new do
    Thread.current.name = "fib t#{id}"
    ready_queue << true
    can_start_queue.pop
    fib(37)
  end
end

2.times { ready_queue.pop }
2.times { can_start_queue << true }

threads.map(&:join)

GvlTracing.stop
