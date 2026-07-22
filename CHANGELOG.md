# Changelog

All notable changes to this project are documented in this file.

## 0.1.0 - 2026-07-22

### Added

- Standalone C++17 SDK for validated configuration, calibrated RDT force/torque samples,
  status decoding, and lifecycle-safe client operation.
- Bounded HTTP sensor discovery for product, calibration scales, and sensor-selected force and
  torque units, with complete manual calibration overrides when discovery is unavailable.
- `netft` CLI with `info`, `monitor`, and software `bias` commands, human and stable JSON
  summaries, atomic file output, defined exit codes, and SIGINT handling.
- Sequence, status, rate, calibration-revision, and error health reporting with exponential
  reconnect and first-fault fail-stop recovery policies.
- Deterministic fake HTTP/UDP sensor tests covering protocol, discovery, streaming, lifecycle,
  faults, recovery, CLI behavior, and sanitizer configurations without physical hardware.
- Shared and static installation, versioned shared-library ABI, public CMake package export as
  `netft::netft`, installed CLI, and out-of-tree consumer validation.
