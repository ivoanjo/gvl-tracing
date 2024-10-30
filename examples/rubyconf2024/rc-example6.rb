require "gvl-tracing"
require "net/http"
require "benchmark/ips"

def perform_request =
  Net::HTTP.start("www.google.com", open_timeout: 0.5, read_timeout: 0.5, write_timeout: 0.5) { |it| it.get("/") }

def counter_loop
  counter = 0
  counter += 1 while counter < 1_000_000_000
end

GvlTracing.start("rc-example6.json") do
  Thread.new { counter_loop }

  Benchmark.ips do |x|
    x.config(time: 1, warmup: 0)
    x.report("request") { perform_request }
  end
end
