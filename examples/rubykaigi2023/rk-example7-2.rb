require "gvl-tracing"

GvlTracing.start("rk-example7-2.json")

def counter_loop
  counter = 0
  counter += 1 while counter < 1_000_000_000
end

ractors = 9.times.map { Ractor.new { counter_loop } }
counter_loop
ractors.map(&:take)

GvlTracing.stop
