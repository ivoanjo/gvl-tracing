require "gvl-tracing"
require "net/http"

def perform_request =
  Net::HTTP.start("www.google.com", open_timeout: 0.5, read_timeout: 0.5, write_timeout: 0.5) { |it| it.get("/") }

GvlTracing.start("rc-example4.json") do
  20.times { perform_request }
end
