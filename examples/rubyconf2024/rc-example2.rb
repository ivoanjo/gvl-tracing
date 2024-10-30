require "gvl-tracing"

def counter_loop
  counter = 0
  counter += 1 while counter < 100_000_000
end

GvlTracing.start("rc-example2.json", os_threads_view_enabled: true) do
  threads = 9.times.map { Thread.new { counter_loop } }
  counter_loop
  threads.map(&:join)
end
