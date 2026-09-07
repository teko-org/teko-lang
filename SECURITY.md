# Security Policy

## Supported versions

Teko is pre-release (the next release is `v0.4.0`); only the tip of the active development branch (`main`) receives fixes.

## Reporting a vulnerability

Please **do not** open a public issue for suspected vulnerabilities.

- Use GitHub's private vulnerability reporting on this repository (*Security → Report a vulnerability*), or
- email the maintainer: <schivei@icloud.com>.

Include a minimal reproducer where possible. You should receive an acknowledgment within a week.

## Scope notes

Teko is a set of hook modules taught to [`mc`](https://github.com/minicompiler/mc); a defect in `mc` itself is reported to that project. Areas of particular interest here: the runtime `lib/rt.tk` (syscalls and the ABI boundary), the array and null guards the taught compiler emits, reference counting and the arena, and the package manifest a consumer resolves. CodeQL (the `actions` analyzer) runs on every pull request.
