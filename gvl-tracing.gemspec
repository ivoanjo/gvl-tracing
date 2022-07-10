# frozen_string_literal: true

require_relative "lib/gvl_tracing/version"

Gem::Specification.new do |spec|
  spec.name = "gvl-tracing"
  spec.version = GvlTracing::VERSION
  spec.authors = ["Ivo Anjo"]
  spec.email = ["ivo@ivoanjo.me"]

  spec.summary = "TODO: Write a short summary, because RubyGems requires one."
  spec.description = "TODO: Write a longer description or delete this line."
  spec.homepage = "https://github.com/ivoanjo/gvl-tracing"
  # TODO: Missing license
  spec.required_ruby_version = ">= 3.2.0"

  # Specify which files should be added to the gem when it is released.
  # The `git ls-files -z` loads the files in the RubyGem that have been added into git.
  spec.files = Dir.chdir(__dir__) do
    `git ls-files -z`.split("\x0").reject do |f|
      (f == __FILE__) || f.match(%r{\A(?:(?:bin|test|spec|features)/|\.(?:git|travis|circleci)|appveyor)})
    end
  end
  spec.require_paths = ["lib", "ext"]
end
