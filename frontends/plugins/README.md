# Host application bridges

JoltFX ships SDK-independent bridge libraries for After Effects, Premiere Pro,
and DaVinci Resolve. They expose stable identifiers and the import, export, and
effect feature contract while keeping proprietary host SDK code out of the
portable build.

The `requires_host_sdk` flag is intentionally set until a host-specific build
is configured. The bridges are not installable Adobe or Fusion binaries by
themselves; a release build must link the relevant SDK adapter and be tested
against the host application's published SDK version.
