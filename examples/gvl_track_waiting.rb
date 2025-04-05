require 'lowlevel-toolkit'

def counter_loop(counter = 0) = (counter += 1 while counter < 500_000_000)

pp(LowlevelToolkit.gvl_track_waiting do
  t1 = Thread.new { counter_loop }.tap { it.name = 't1' }
  t2 = Thread.new { counter_loop }.tap { it.name = 't2' }

  t1.join; t2.join
end)
