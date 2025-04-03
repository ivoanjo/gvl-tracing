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

if ["jruby", "truffleruby"].include?(RUBY_ENGINE)
  raise \
    "\n#{"-" * 80}\nSorry! This gem is unsupported on #{RUBY_ENGINE}. Since it relies on a lot of guts of MRI Ruby, " \
    "it's impossible to make a direct port.\n" \
    "Perhaps a #{RUBY_ENGINE} equivalent could be created -- help is welcome! :)\n#{"-" * 80}"
end

require "mkmf"

append_cflags("-Werror-implicit-function-declaration")
append_cflags("-Wunused-parameter")
append_cflags("-Wold-style-definition")
append_cflags("-Wall")
append_cflags("-Wextra")
append_cflags("-Werror") if ENV["ENABLE_WERROR"] == "true"

create_header
create_makefile "lowlevel_toolkit_native_extension"
