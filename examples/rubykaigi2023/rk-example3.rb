require "gvl-tracing"

GvlTracing.start("rk-example3.json")

def counter_loop
  counter = 0
  counter += 1 while counter < 1_000_000_000
end

Thread.new { counter_loop; sleep }
3.times { counter_loop }

GvlTracing.stop
