# typed: false
# frozen_string_literal: true

# Homebrew formula for the Teko compiler (`teko`).
#
# Install:
#   brew tap schivei/teko && brew install teko
#
# This formula builds `teko` FROM SOURCE using the system C compiler, out of the
# portable bootstrap snapshot published with each release (teko-bootstrap-src.tar.gz:
# the generated teko.c plus the minimal runtime/assert sources). Building from source
# means the bottle is architecture-native on both Apple Silicon (arm64) and Intel
# (x86_64), and the resulting binary is never quarantined by Gatekeeper — so there is
# no binary-formula caveat and no code-signing / notarization needed for this pre-alpha
# CLI tooling.
#
# The tap repository is teko-org/homebrew-teko; this file is its Formula/teko.rb.
class Teko < Formula
  desc "Self-hosted compiler for the Teko programming language (transpiles to C)"
  homepage "https://github.com/teko-org/teko-lang"
  # TODO(release): the tag + URL below are PLACEHOLDERS until 0.0.1.3-bootstrap ships.
  # The release pipeline (or a manual bump at first release) rewrites both `url` and
  # `sha256` to point at the published teko-bootstrap-src.tar.gz.
  url "https://github.com/teko-org/teko-lang/releases/download/0.0.1.3-bootstrap/teko-bootstrap-src.tar.gz"
  # TODO(release): filled by the release pipeline / manual bump at first release.
  sha256 "0000000000000000000000000000000000000000000000000000000000000000"
  license any_of: ["Apache-2.0", "MIT"]

  def install
    # Build line kept byte-identical to the bundle's build.sh and to
    # scripts/package_release.sh:
    #   cc -std=c23 -w -Iruntime -Iassert teko.c runtime/teko_rt.c assert/assert.c -lm -o teko
    system ENV.cc, "-std=c23", "-w",
           "-Iruntime", "-Iassert",
           "teko.c", "runtime/teko_rt.c", "assert/assert.c",
           "-lm", "-o", "teko"
    bin.install "teko"
  end

  test do
    # `teko` is project-oriented: run with no arguments it prints a usage banner and
    # exits 2. That the banner reaches us proves the compiler binary loads and runs.
    # (There is no --version/--help flag yet in this pre-alpha, so we assert on the
    # banner text and its documented exit status.)
    assert_predicate bin/"teko", :executable?
    output = shell_output("#{bin}/teko 2>&1", 2)
    assert_match(/teko/i, output)
  end
end
