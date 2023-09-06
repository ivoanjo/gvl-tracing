require "gvl-tracing"

GvlTracing.start("rk-example2.json")

def counter_loop
  counter = 0
  counter += 1 while counter < 1_000_000_000
end

threads = 9.times.map { Thread.new { counter_loop } }
counter_loop
threads.map(&:join)

GvlTracing.stop
