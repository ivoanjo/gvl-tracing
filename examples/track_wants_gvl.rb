require 'lowlevel-toolkit'

def counter_loop(counter = 0) = (counter += 1 while counter < 150_000_000)

before = Time.now

pp(LowlevelToolkit.track_wants_gvl do
  t1 = Thread.new { counter_loop }.tap { |it| it.name = 't1' }
  t2 = Thread.new { counter_loop }.tap { |it| it.name = 't2' }

  t1.join; t2.join
end.map { |thread, wants_gvl_ns| [thread, wants_gvl_ns / 1_000_000_000.0] })

puts "Total time: #{Time.now - before}"
