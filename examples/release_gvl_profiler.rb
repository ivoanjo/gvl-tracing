require 'lowlevel-toolkit'
require "net/http"

def counter_loop(counter = 0) = (counter += 1 while counter < 150_000_000)

pp(LowlevelToolkit.release_gvl_profiler do
  # counter_loop
  sleep 1
  Net::HTTP.start("www.google.com", open_timeout: 5, read_timeout: 5, write_timeout: 5) { |it| it.get("/") }
  big_data = File.read("/dev/zero", 1_000_000_000)
  Dir.children("/usr/lib")
  (2**1000000).to_s.size
  Zlib::Deflate.deflate(big_data)
  puts "Done!"
end)
