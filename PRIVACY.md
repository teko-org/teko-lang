# Privacy

**The Teko toolchain collects no data. Period.**

- No telemetry, no analytics, no crash reporting, no update pings.
- `teko build`, `teko run` and `teko test` perform **no network access** of any kind.
- The compiler links nothing beyond libc; there is no embedded phone-home surface to audit around.
- Binaries built by Teko embed only what the manifest declares (name, version, description via the `@(#)` metadata marker) — never user, machine or environment information.

Any future feature that would transmit data (e.g. a package registry client) will be opt-in, documented, and will never run as a side effect of building or testing.

This repository is hosted on GitHub; interactions there (issues, PRs, discussions) are governed by [GitHub's privacy statement](https://docs.github.com/en/site-policy/privacy-policies/github-privacy-statement).
