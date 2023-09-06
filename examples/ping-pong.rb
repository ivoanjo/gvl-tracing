require "gvl-tracing"
require "gvl_tracing/sleep_tracking"

# Shamelessly borrowed from https://github.com/ruby/ruby/pull/8331 with permission from @KJTsanaktsidis
#
# It demonstrates a big bias for the same thread to acquire a mutex, starving other threads out.

GvlTracing.start("ping-pong#{ENV["RUBY_VARIANT"]}.json")

at_exit { GvlTracing.stop }

resource_pool_klass = Class.new do
  const_set :PoolTimeoutError, Class.new(StandardError)

  def initialize(resources)
    @resources = resources
    @mutex = Thread::Mutex.new
    @condvar = Thread::ConditionVariable.new
  end

  def acquire
    @mutex.synchronize do
      t_start = Process.clock_gettime(Process::CLOCK_MONOTONIC)
      t_deadline = t_start + 1.0
      loop do
        return @resources.pop if @resources.any?
        wait_for = t_deadline - Process.clock_gettime(Process::CLOCK_MONOTONIC)
        raise self.class::PoolTimeoutError if wait_for <= 0
        @condvar.wait(@mutex, wait_for)
      end
    end
  end

  def release(resource)
    @mutex.synchronize do
      @resources << resource
      @condvar.signal
    end
  end
end

worker_klass = Class.new do
  def initialize(pool, iterations)
    @pool = pool
    @iterations = iterations
  end

  def start
    @thread = Thread.new { run }
  end

  def stop
    @thread&.kill
    @thread&.join
    @thread = nil
  end

  def join
    @thread&.join
    @thread = nil
  end

  def run
    @iterations.times do
      r = @pool.acquire
      sleep 0.001
      @pool.release r
    end
  end
end

pool = resource_pool_klass.new([Object.new])
workers = 3.times.map do
  worker_klass.new(pool, 1000).tap { _1.start }
end

# No thread should raise a pool timeout.
begin
  workers.each { _1.join }
rescue resource_pool_klass::PoolTimeoutError
  raise "[Bug #19717] a PoolTimeoutError was raised from a thread being unfairly starved"
end
