require 'lowlevel-toolkit'

pp(LowlevelToolkit.track_objects_created do
  Object.new
  Time.utc(2025, 4, 17, 1, 0, 0)
  "Hello, world!"
end)
