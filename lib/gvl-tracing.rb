# gvl-tracing: Ruby gem for getting a timelinew view of GVL usage
# Copyright (c) 2022 Ivo Anjo <ivo@ivoanjo.me>
#
# This file is part of gvl-tracing.
#
# MIT License
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

# frozen_string_literal: true

require_relative "gvl_tracing/version"

require "gvl_tracing_native_extension"
require "json"

module GvlTracing
  class << self
    @@path = "/tmp/gvl-tracing.json"

    def start(file)
      @@path = file
      GvlTracingNativeExtension.start(@@path)
    end

    def stop
      thread_list = Thread.list
      GvlTracingNativeExtension.stop
      set_thread_name(thread_list)
    end

    private

    def set_thread_name(thread_list)
      output_file = File.open(@@path)
      output = output_file.read.split("\n").map do |event|
        parse_event = JSON.parse(event)
        thread_id = parse_event.dig("args", "name")

        next(parse_event) unless thread_id

        thread = thread_list.find { |t| t.native_thread_id.to_s == thread_id.strip }

        next(parse_event) unless thread

        parse_event["args"]["name"] = thread_label(thread)
        parse_event
      end
      File.write(@@path, output.to_json)
    end

    REGEX = /lib(?!.*lib)\/([a-zA-Z-]+)/
    def thread_label(thread)
      lib_name = thread.to_s.match(REGEX)

      return thread.name if lib_name.nil?

      "#{thread.name} from #{lib_name[1]}"
    end
  end
end
