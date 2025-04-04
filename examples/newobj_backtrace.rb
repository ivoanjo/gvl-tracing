require 'lowlevel-toolkit'

def hello
  Object.new
  nil
end

LowlevelToolkit.track_last_allocation_at do
  hello
  pp LowlevelToolkit.last_allocation_at
end
