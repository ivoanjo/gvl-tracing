# frozen_string_literal: true

require "spec_helper"
require "json"
require "net/http"
require "perfetto_trace"

RSpec.describe GvlTracing do
  let(:trace_path) { "tmp/gvl.json" }

  it "has a version number" do
    expect(GvlTracing::VERSION).not_to be nil
  end

  describe "#start" do
    it "fails if already started" do
      expect {
        GvlTracing.start(trace_path) { GvlTracing.start(trace_path) }
      }.to raise_error(/Already started/)
    end

    describe "with a block" do
      before do
        GvlTracing.start(trace_path) {}
      end

      it "stops tracing after the block" do
        expect(File.exist?(trace_path)).to be_truthy
      end

      it "creates valid json" do
        expect { JSON.parse(File.read(trace_path)) }.to_not raise_error
      end
    end
  end

  describe "#stop" do
    it "fails if not started" do
      expect { GvlTracing.stop }.to raise_error(/Tracing not running/)
    end

    it "closes out the json" do
      GvlTracing.start(trace_path)
      expect { JSON.parse(File.read(trace_path)) }.to raise_error(JSON::ParserError)

      GvlTracing.stop
      expect(JSON.parse(File.read(trace_path))).to be_an(Array)
    end
  end

  describe "order of events" do
    it "first and last events are in a consistent order" do
      GvlTracing.start(trace_path) do
        [Thread.new {}, Thread.new {}].each(&:join)
      end

      trace = PerfettoTrace.new(trace_path)
      traces = trace.events_by_thread
      # skip the main thread
      first = traces[traces.keys[1]].reject { |j| !j.name }
      second = traces[traces.keys[2]].reject { |j| !j.name }

      expect(first.first.name).to eq("started")
      expect(first.last.name).to eq("died")

      expect(second.first.name).to eq("started")
      expect(second.last.name).to eq("died")
    end
  end

  describe "thread already started" do
    it "has events that would require the GVL" do
      started = Queue.new
      finish = Queue.new
      thread = Thread.new do
        started << true
        finish.pop
      end
      started.pop
      GvlTracing.start(trace_path) do
        finish << true
        thread.join
      end

      trace = PerfettoTrace.new(trace_path)
      traces = trace.events_by_thread
      # skip the main thread
      first = traces[traces.keys[1]].filter_map(&:name)
      expect(first).to include("wants_gvl")
    end
  end
end
