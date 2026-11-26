# Changelog

## Unreleased — 0.1.0 development

- Shared Core: isolated session/run logs, correlation fields, bounded rotation/retention and error/terminal flushing.
- Local crash markers, separate-process Windows minidumps, private symbols and a redacted diagnostic preview/export. No upload.
- CLI rejects invalid arguments with exit 2 before bootstrap. `--help`, `--version`, diagnostic export and controlled crash probes run headless.
- Generated build identity, shared About dialog, Windows VERSIONINFO, Python version metadata and exact EXE/PDB hash manifests.

Migration: scripts that relied on silently ignored CLI options must remove them or use `--help`. Screenshot controls now require `--screenshot`. Log filenames move from a shared `SST.log` to session directories; a configured filename continues to select the parent root. Project schemas and scientific defaults are unchanged.

Qualification: see the pro-core1 job report for the actual MSVC/test/GUI results and existing failures. This development version is not a release certification. Runtime minimum and scientific acceptance remain those documented by each app.

SemVer policy: incompatible public API changes increment major (during 0.x, increment minor and document migration); additive compatible API increments minor; compatible fixes increment patch. Project schema and native ABI versions are independent. A release owner supplies the immutable source ID and publishes the qualified scope.
