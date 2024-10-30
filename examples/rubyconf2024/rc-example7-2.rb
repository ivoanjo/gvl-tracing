require "gvl-tracing"

def counter_loop
  counter = 0
  counter += 1 while counter < 100_000_000
end

GvlTracing.start("rc-example7-2.json", os_threads_view_enabled: true) do
  ractors = 9.times.map { Ractor.new { counter_loop } }
  counter_loop
  ractors.map(&:take)
end
