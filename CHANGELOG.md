# Changelog

All notable changes to this project are documented in this file.

## 0.2.2 - 2026-07-25

### Fixed

- Require libcurl 7.63.0 or newer, matching the first release that provides the `CURLOPT_CURLU` API used by sensor discovery.

## 0.2.1 - 2026-07-25

### Fixed

- Restore GCC 10 and manylinux2014 source compatibility by parsing sensor calibration counts with a C++17 locale-independent implementation.

## 0.2.0 - 2026-07-24

### Added

- Expose the exact signed RDT axis counts on every `Sample` through `raw_wrench`, ordered as force X/Y/Z followed by torque X/Y/Z.

### Changed

- Advance the shared-library ABI for the expanded public `Sample` layout.

## 0.1.3 - 2026-07-23

### Fixed

- Keep UDP receive timeouts bounded when `poll()` is interrupted by signals, preventing repeated `EINTR` retries from extending the configured timeout.

## 0.1.2 - 2026-07-22

### Changed

- Lower the supported CMake baseline from 3.20 to 3.16 and verify shared and static installed consumers with the pinned minimum version in CI.

## 0.1.1 - 2026-07-22

### Fixed

- Replace the project-specific sensor address in defaults and examples with ATI's factory-default `192.168.1.1` address.
- Keep tests focused on executable and machine-readable contracts instead of human-facing help, error, log, diagnostic, and release wording.
- Normalize generated release notes so source formatting does not introduce awkward wrapped continuation lines.

## 0.1.0 - 2026-07-22

### Added

- Standalone C++17 SDK for validated configuration, calibrated RDT force/torque samples, status decoding, and lifecycle-safe client operation.
- Bounded HTTP sensor discovery for product, calibration scales, and sensor-selected force and torque units, with complete manual calibration overrides when discovery is unavailable.
- `netft` CLI with `info`, `monitor`, and software `bias` commands, human and stable JSON summaries, atomic file output, defined exit codes, and SIGINT handling.
- Sequence, status, rate, calibration-revision, and error health reporting with exponential reconnect and first-fault fail-stop recovery policies.
- Deterministic fake HTTP/UDP sensor tests covering protocol, discovery, streaming, lifecycle, faults, recovery, CLI behavior, and sanitizer configurations without physical hardware.
- Shared and static installation, versioned shared-library ABI, public CMake package export as `netft::netft`, installed CLI, and out-of-tree consumer validation.
