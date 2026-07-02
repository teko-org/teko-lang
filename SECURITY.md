# Security Policy

## Supported versions

Teko is pre-release (`0.0.1.0-bootstrap`); only the tip of the active development branch (`chore/reboot`) receives fixes.

## Reporting a vulnerability

Please **do not** open a public issue for suspected vulnerabilities.

- Use GitHub's private vulnerability reporting on this repository (*Security → Report a vulnerability*), or
- email the maintainer: <schivei@icloud.com>.

Include a minimal reproducer where possible. You should receive an acknowledgment within a week.

## Scope notes

The compiler runs a SAST gate in CI (CodeQL + a clang-tidy security profile) and has an audited capability surface (`src/checker/capability_audit.md`). Areas of particular interest for reports: the `extern`/FFI boundary, generated-C memory safety, the arena/region allocator, and `.tkb`/`.tkl` artifact parsing.
