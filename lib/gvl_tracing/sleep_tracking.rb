# Experimental: This monkey patch when loaded introduces a new state -- "sleeping" -- which is more specific than the
# regular "waiting". This can be useful to distinguish when waiting is happening based on time, vs for some event to
# happen.

module GvlTracing::SleepTracking
  def sleep(...)
    GvlTracing.mark_sleeping
    super
  end
end

include GvlTracing::SleepTracking
