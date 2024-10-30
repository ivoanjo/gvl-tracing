require "gvl-tracing"

def counter_loop
  counter = 0
  counter += 1 while counter < 100_000_000
end

GvlTracing.start("rc-example3.json") do
  Thread.new {
    counter_loop
    sleep
  }
  3.times { counter_loop }
end
