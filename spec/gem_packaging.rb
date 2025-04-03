# Note: This file does not end with _spec on purpose, it should only be run after packaging, e.g. with `rake spec_validate_permissions`

require "rubygems"
require "rubygems/package"
require "rubygems/package/tar_reader"
require "zlib"
require "lowlevel-toolkit"

RSpec.describe "gem release process (after packaging)" do
  let(:gem_version) { LowlevelToolkit::VERSION }
  let(:packaged_gem_file) { "pkg/lowlevel-toolkit-#{gem_version}.gem" }

  it "sets the right permissions on the gem files" do
    gem_files = Dir.glob("pkg/*.gem")
    expect(gem_files).to include(packaged_gem_file)

    gem_files.each do |gem_file|
      Gem::Package::TarReader.new(File.open(gem_file)) do |tar|
        data = tar.find { |entry| entry.header.name == "data.tar.gz" }

        Gem::Package::TarReader.new(Zlib::GzipReader.new(StringIO.new(data.read))) do |data_tar|
          data_tar.each do |entry|
            filename = entry.header.name.split("/").last
            octal_permissions = entry.header.mode.to_s(8)[-3..]

            expected_permissions = "644"

            expect(octal_permissions).to eq(expected_permissions),
              "Unexpected permissions for #{filename} inside #{gem_file} (got #{octal_permissions}, " \
              "expected #{expected_permissions})"
          end
        end
      end
    end
  end
end
