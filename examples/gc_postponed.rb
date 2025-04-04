require 'lowlevel-toolkit'

at_finish = -> do
  kind = GC.latest_gc_info[:major_by]
  puts "GC finished (#{kind ? "major (#{kind})" : "minor"})"
end

LowlevelToolkit.on_gc_finish(at_finish) do
  GC.start(full_mark: false)
  GC.start(full_mark: true)
end
