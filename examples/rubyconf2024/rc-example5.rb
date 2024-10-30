require "gvl-tracing"
require "net/http"
require "benchmark/ips"

def perform_request =
  Net::HTTP.start("www.google.com", open_timeout: 0.5, read_timeout: 0.5, write_timeout: 0.5) { |it| it.get("/") }

GvlTracing.start("rc-example5.json") do
  Benchmark.ips do |x|
    x.config(time: 1, warmup: 0)
    x.report("request") { perform_request }
  end
end
