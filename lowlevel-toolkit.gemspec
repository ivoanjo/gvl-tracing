# lowlevel-toolkit: Ruby gem for calling observability APIs
# Copyright (c) 2025 Ivo Anjo <ivo@ivoanjo.me>
#
# This file is part of lowlevel-toolkit.
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

require_relative "lib/lowlevel_toolkit/version"

Gem::Specification.new do |spec|
  spec.name = "lowlevel-toolkit"
  spec.version = LowlevelToolkit::VERSION
  spec.authors = ["Ivo Anjo"]
  spec.email = ["ivo@ivoanjo.me"]

  spec.summary = "Ruby gem for calling observability APIs"
  spec.homepage = "https://github.com/ivoanjo/lowlevel-toolkit"
  spec.license = "MIT"
  spec.required_ruby_version = ">= 3.3.0"

  # Specify which files should be added to the gem when it is released.
  # The `git ls-files -z` loads the files in the RubyGem that have been added into git.
  spec.files = Dir.chdir(__dir__) do
    `git ls-files -z`.split("\x0").reject do |f|
      (f == __FILE__) || f.match(%r{\A(?:(?:bin|test|spec|features|examples)/|\.(?:git|travis|circleci)|appveyor)}) ||
        [".editorconfig", ".ruby-version", ".standard.yml", "gems.rb", "Rakefile"].include?(f)
    end
  end
  spec.require_paths = ["lib", "ext"]
  spec.extensions = ["ext/lowlevel_toolkit_native_extension/extconf.rb"]
end
