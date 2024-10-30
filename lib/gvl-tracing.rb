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

module GvlTracing
  class << self
    private :_start
    private :_stop

    def start(file, os_threads_view_enabled: false)
      _start(file, os_threads_view_enabled)
      _init_local_storage(Thread.list)
      @path = file

      return unless block_given?

      begin
        yield
      ensure
        stop
      end
    end

    def stop
      thread_list = _stop

      append_thread_names(thread_list)

      trim_all_seen_threads
    end

    private

    def append_thread_names(list)
      thread_names = aggreate_thread_list(list).join(",\n")
      File.open(@path, "a") do |f|
        f.puts(thread_names)
        f.puts("]")
      end
    end

    def aggreate_thread_list(list)
      list.each_with_object([]) do |t, acc|
        next unless t.name || t == Thread.main

        acc << "  {\"ph\": \"M\", \"pid\": #{Process.pid}, \"tid\": #{thread_id_for(t)}, \"name\": \"thread_name\", \"args\": {\"name\": \"#{thread_label(t)}\"}}"
      end
    end

    REGEX = /lib(?!.*lib)\/([a-zA-Z-]+)/
    def thread_label(thread)
      if thread == Thread.main
        return thread.name || "Main Thread"
      end

      lib_name = thread.to_s.match(REGEX)

      return thread.name if lib_name.nil?

      "#{thread.name} from #{lib_name[1]}"
    end

    def thread_id_for(t)
      RUBY_VERSION.start_with?("3.2.") ? t.native_thread_id : _thread_id_for(t)
    end
  end
end

# Eagerly initialize context for main thread
GvlTracing.send(:thread_id_for, Thread.main)
