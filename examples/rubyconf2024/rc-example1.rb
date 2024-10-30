require "gvl-tracing"

def counter_loop
  counter = 0
  counter += 1 while counter < 1_000_000_000
end

GvlTracing.start("rc-example1.json") do
  t2 = Thread.new { counter_loop }
  counter_loop
  t2.join
end
