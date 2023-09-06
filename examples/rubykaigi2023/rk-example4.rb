require "gvl-tracing"
require "net/http"

GvlTracing.start("rk-example4.json")

20.times do
  Net::HTTP.start("www.google.com", open_timeout: 0.5, read_timeout: 0.5, write_timeout: 0.5) { |it| it.get("/") }
end

GvlTracing.stop
