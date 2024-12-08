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

require "bundler/gem_tasks"
require "standard/rake"
require "rake/extensiontask"
require "rspec/core/rake_task"

Rake::ExtensionTask.new("gvl_tracing_native_extension")
RSpec::Core::RakeTask.new(:spec)

task default: [:compile, :"standard:fix", :spec]

Rake::Task["build"].enhance { Rake::Task["spec_validate_permissions"].execute }

task :spec_validate_permissions do
  require "rspec"
  RSpec.world.reset # If any other tests ran before, flushes them
  ret = RSpec::Core::Runner.run(["spec/gem_packaging.rb"])
  raise "Release tests failed! See error output above." if ret != 0
end
