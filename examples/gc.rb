require "gvl-tracing"

def alloc(n)
  n.times { Object.new }
end

GvlTracing.start("gc.json")

3.times.map { Thread.new { alloc(100_000) } }.map(&:join)

sleep(0.05)

GvlTracing.stop
