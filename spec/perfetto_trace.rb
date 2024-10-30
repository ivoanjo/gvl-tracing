# frozen_string_literal: true

class PerfettoTrace
  def initialize(file_path)
    @trace = JSON.parse(File.read(file_path))
  end

  def threads
    @trace
      .select { |j| j["ph"] == "M" && j["name"] == "thread_name" }
      .uniq { |j| j["args"]["tid"] }
      .map { |j| Row.new(j) }
  end

  def non_main_threads
    threads.select { |t| t.thread_name != "Main Thread" }
  end

  def events_by_thread
    @trace
      .select { |j| j["tid"] }
      .group_by { |j| j["tid"] }
      .map { |tid, events| [tid, events.map { |j| Row.new(j) }] }
      .to_h
  end

  Row = Data.define(:row) do
    def meta? = ph == "M"

    def phase_begin? = ph == "B"

    def phase_end? = ph == "E"

    def phase = row["ph"]

    def pid = row["pid"]

    def tid = row["tid"]

    def ts = row["ts"]

    def name = row["name"]

    def args = row["args"]

    def thread_name = row.dig("args", "name")
  end
end
