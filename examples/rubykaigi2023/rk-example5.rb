require 'gvl-tracing'
require 'net/http'
require 'benchmark/ips'

GvlTracing.start("rk-example5.json")

def perform_request = \
  Net::HTTP.start('www.google.com', open_timeout: 0.5, read_timeout: 0.5, write_timeout: 0.5) do |http|
    http.get('/')
  end

Benchmark.ips do |x|
  x.config(time: 1, warmup: 0)
  x.report("request") { perform_request }
end

GvlTracing.stop
