require 'lowlevel-toolkit'

LowlevelToolkit.print_gc_timing do
  puts "Minor GC:"
  GC.start(full_mark: false)
  puts "Major GC:"
  GC.start(full_mark: true)
end
