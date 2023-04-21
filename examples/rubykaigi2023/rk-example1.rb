require 'gvl-tracing'

GvlTracing.start("rk-example1.json")

def counter_loop
  counter = 0
  counter += 1 while counter < 1_000_000_000
end

t2 = Thread.new { counter_loop }
counter_loop
t2.join

GvlTracing.stop
