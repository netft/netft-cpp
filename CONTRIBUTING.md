# Contributing to netft-cpp

Thank you for helping improve `netft-cpp`. Bug reports, hardware compatibility reports,
documentation corrections, tests, and focused code changes are welcome.

## Development environment

Install [Pixi](https://pixi.sh), clone the repository, and create the locked environment:

```bash
git clone https://github.com/netft/netft-cpp.git
cd netft-cpp
pixi install
```

Use the repository tasks rather than relying on host tool versions:

```bash
pixi run configure
pixi run build
pixi run test
pixi run install-test
pixi run format-check
pixi run tidy
pixi run coverage
```

`install-test` builds both shared and static installations and validates a separate CMake
consumer. Run the complete relevant command set before opening a pull request. Keep
`pixi.lock` synchronized with `pixi.toml` when dependency resolution actually changes it.

The Pixi tasks are the primary Linux development path. Platform-specific transport changes
must also pass the macOS or Windows CI job. Those jobs deliberately obtain curl and GoogleTest
from Homebrew or vcpkg instead of embedding either dependency in the library.

## Test-driven changes

Use a red-green-refactor workflow for code changes:

1. Add or adjust the smallest behavioral test that demonstrates the missing behavior or bug.
2. Run it and confirm that it fails for the expected reason.
3. Implement the smallest complete change that makes it pass.
4. Run the focused test, then the full test and install-consumer suites.
5. Refactor only while the tests remain green.

Tests should verify public behavior, protocol rules, error paths, or necessary private
invariants. Do not pin README, changelog, release-note, help, diagnostic, log, or error prose.
Protocol bytes, serialized enum values, JSON field names, and other machine-readable interfaces
remain valid test contracts. No test may depend on an unavailable physical sensor.

## Formatting and static analysis

Format C++ sources with:

```bash
pixi run format
```

Before submitting, run `pixi run format-check` and `pixi run tidy`. Avoid unrelated formatting
or refactoring in a focused change. New code must remain C++17-compatible and preserve the
public/private ABI boundary of the installed shared library. Keep operating-system socket
headers and implementation details out of the installed public headers.

## Hardware testing is opt-in

The default test suite uses local fake HTTP and UDP sensors and must not contact real hardware.
Physical-sensor testing requires explicit approval from the person responsible for the device
and test area. Before a hardware run:

- identify the exact sensor host and expected calibration;
- confirm that the host is dedicated to the test and is not a copied example address;
- make the mechanical setup safe, stop hazardous motion, and keep people clear;
- state whether the test will stream, disconnect the network, inject faults, or apply bias; and
- record the sensor model, firmware, selected units, and observed result in the pull request.

Never make hardware access a default Pixi task or CI step. Do not run `netft bias` without
specific authorization for that operation.

## Versioning before 1.0

The project follows Semantic Versioning. While the major version is `0`, minor releases may
contain documented breaking changes to public API or behavior. Patch releases within a minor
series must remain backward compatible and are reserved for fixes and compatible improvements.
Every user-visible change belongs in `CHANGELOG.md`; breaking changes must be called out
explicitly and include migration guidance.

## Manual ros-netft backports

`netft-cpp` is the source of truth for the standalone SDK. Changes are not synchronized
automatically into the separate `ros-netft` repository. After a relevant change is accepted
here, backport it manually in a separate `ros-netft` pull request:

1. Reference the exact `netft-cpp` commit and explain why the ROS integration needs it.
2. Port only the public behavior, compatibility fix, or packaging change needed by the ROS
   wrapper; do not copy private implementation indiscriminately.
3. Preserve the supported ROS 1 and ROS 2 branch conventions and make branch-specific changes
   explicit.
4. Run the applicable ROS build and tests in the supported environment, plus any standalone
   `netft-cpp` regression that the backport retains.
5. Document intentional differences and avoid claiming that the repositories update in lockstep.

## Pull requests

Keep commits reviewable and the pull request limited to one coherent change. Describe the
problem, the chosen behavior, tests run, platform/compiler coverage, and any ABI, hardware, or
backport impact. Update public documentation and `CHANGELOG.md` when users need to know about
the change. All contributions are submitted under the Apache License 2.0.
