require "gvl-tracing"

GvlTracing.start("coalesce.json")

mutex = Thread::Mutex.new
cond_var = Thread::ConditionVariable.new

deadline = Time.now + 1

10.times do
  Thread.new do
    mutex.synchronize do
      cond_var.wait(mutex)
    end while Time.now < deadline
  end
end

mutex.synchronize do
  cond_var.signal
end while Time.now < deadline

GvlTracing.stop
