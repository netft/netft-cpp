# netft-cpp

[![CI](https://github.com/netft/netft-cpp/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/netft/netft-cpp/actions/workflows/ci.yml)
[![CodeQL](https://github.com/netft/netft-cpp/actions/workflows/codeql.yml/badge.svg?branch=main)](https://github.com/netft/netft-cpp/actions/workflows/codeql.yml)
[![Coverage](https://codecov.io/gh/netft/netft-cpp/graph/badge.svg?branch=main)](https://codecov.io/gh/netft/netft-cpp)
[![Release](https://img.shields.io/github/v/release/netft/netft-cpp?display_name=tag&sort=semver)](https://github.com/netft/netft-cpp/releases)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)](https://en.cppreference.com/w/cpp/17)
[![License](https://img.shields.io/github/license/netft/netft-cpp?label=license)](LICENSE)

`netft-cpp` is a standalone C++17 SDK and command-line tool for receiving calibrated
force/torque samples from ATI Net F/T Ethernet sensors over the RDT protocol, with HTTP
configuration discovery, stream health reporting, and explicit recovery policies.

## Highlights

- Discovers calibration scales and measurement units from the sensor before streaming.
- Tracks sequence, status, delivery, and recovery health with reconnect and fail-stop policies.
- Installs as the `netft::netft` CMake target and includes a human- and JSON-readable CLI.

## Supported platforms

| Platform | Architectures | SDK | CLI |
| --- | --- | --- | --- |
| Linux | x86-64, AArch64 | Tested | Tested |
| macOS | x86-64, Apple silicon | Tested | Tested |
| Windows | x86-64 | Tested | Not currently supported |

Building requires a C++17 compiler, CMake 3.16 or newer, threads, and libcurl 7.63.0 or newer.
GoogleTest is required only when `BUILD_TESTING=ON`. The checked-in Pixi environment provides
the reproducible Linux development toolchain; macOS and Windows builds use the same CMake
project with externally supplied dependencies.

## Installation

Pixi provides the reproducible development environment:

```bash
git clone https://github.com/netft/netft-cpp.git
cd netft-cpp
pixi install
pixi run cmake -S . -B build/release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=ON \
  -DBUILD_TESTING=OFF \
  -DCMAKE_INSTALL_PREFIX="$PWD/install"
pixi run cmake --build build/release
pixi run cmake --install build/release
```

Set `BUILD_SHARED_LIBS=OFF` for a static library. The same CMake commands work with system
packages instead of Pixi when the required compiler, CMake, Threads, and libcurl dependencies
are available. Homebrew can provide the macOS dependencies, while vcpkg can provide the Windows
dependencies. Pass their installation prefixes or toolchain file to CMake as appropriate. For
a non-system shared-library prefix, configure the platform dynamic loader for `install/lib`
before running `install/bin/netft`.

On Windows, configure with `-DNETFT_BUILD_CLI=OFF`. The SDK uses WinSock 2 and links `ws2_32`
automatically; the current CLI still relies on POSIX file and process facilities.

## CMake usage

After installation, consume the package with CMake config mode:

```cmake
find_package(netft 0.1 CONFIG REQUIRED)

add_executable(read_sensor main.cpp)
target_link_libraries(read_sensor PRIVATE netft::netft)
```

Pass the installation prefix through `CMAKE_PREFIX_PATH` when it is not in a standard search
location. A minimal streaming program is:

```cpp
#include <chrono>
#include <iostream>

#include <netft/client.hpp>

int main() {
  netft::Config config;
  config.sensor_host = "192.168.1.1";

  netft::Client client{config};
  client.start([](const netft::Sample &sample) {
    std::cout << sample.raw_wrench[0] << ' ' << sample.force[0] << ' '
              << netft::to_string(sample.force_unit) << '\n';
  });
  const bool received = client.wait_for_first_sample(std::chrono::seconds{2});
  client.stop();
  return received ? 0 : 1;
}
```

`192.168.1.1` is the ATI [factory-default sensor address](https://www.ati-ia.com/app_content/Documents/9620-05-Net%20FT.pdf). Replace it with the address configured for your sensor when it has been moved to another network.

## CLI usage

The installed `netft` executable provides three commands:

```bash
netft --help
netft info --host 192.168.1.1
netft monitor --host 192.168.1.1 --duration 10
netft monitor --host 192.168.1.1 --duration 10 --json --output sample.json
netft bias --host 192.168.1.1
```

`info` reads and prints sensor configuration without starting an RDT stream. `monitor` keeps
the latest sample and reports a summary after five seconds by default. `bias` waits for a
sample, applies software bias, and succeeds only after receiving a later sample. `--json`
selects JSON output, while `--output PATH` replaces the output file atomically.

CLI exit codes are `0` for success without reported device warnings or errors, `1` when a
completed operation reports device warnings or errors, `2` for invalid usage or an operational
failure, and `130` for interruption by `SIGINT`.

## Automatic discovery and units

Without a manual override, the client requests `http://HOST:HTTP_PORT/netftapi2.xml` before
each streaming session and reads the product name, counts per force unit, counts per torque
unit, force unit, and torque unit. Raw RDT counts are divided by those sensor-selected
calibration scales. Every `Sample` carries the exact signed RDT counts in `raw_wrench`, the calibrated `force`
and `torque` values, the resulting `force_unit` and `torque_unit`, and the configuration
revision. The active configuration is also available through `Client::health()`.

Sample values therefore use the units selected by the sensor; the library does not silently
convert them to a preferred unit system. In particular, do not assume that an ATI sensor's
native torque unit is `N-mm`: if the device configuration reports `N-m`, the sample torque is
scaled and labeled as `N-m`. Check `netft info` or the sample unit fields before consuming the
values. Reconnection performs discovery again when no override is active, and a changed
calibration advances the configuration revision.

## Manual override

Use a manual override only when HTTP discovery is unavailable and the exact active sensor
calibration is known. All four values are required together: counts per force unit, counts per
torque unit, force unit, and torque unit. Partial overrides are rejected.

```bash
netft monitor --host 192.168.1.1 --duration 5 \
  --counts-per-force-unit 1000000 \
  --counts-per-torque-unit 1000 \
  --force-unit N \
  --torque-unit N-mm
```

The numbers and units above are illustrative and must be replaced with values from the sensor's
active configuration. `info` always performs discovery and does not accept a manual override.
In C++, assign a complete `netft::Calibration` to `Config::calibration_override`:

```cpp
config.calibration_override = netft::Calibration{
    1000000.0,
    1000.0,
    netft::ForceUnit::Newton,
    netft::TorqueUnit::NewtonMillimeter,
};
```

Canonical override unit strings are `lbf`, `N`, `klbf`, `kN`, and `kgf` for force, and
`lbf-in`, `lbf-ft`, `N-m`, `N-mm`, `kgf-cm`, and `kN-m` for torque.

## Error and recovery model

`RecoveryPolicy::Reconnect` is the default. Configuration, timeout, socket, serious-status,
and malformed-packet-storm failures close the current session, enter an interruptible
exponential backoff, and start a new session. The initial delay is 0.25 seconds by default,
doubles up to five seconds, and resets after a session receives a valid record. Duplicate and
out-of-order RDT records, plus stalled and unconfirmed backward FT-sequence records, are not
delivered; gaps and other quality events are recorded in `HealthSnapshot`.

`RecoveryPolicy::FailStop` instead latches the first fault and moves the client to `Faulted`.
Applications can inspect `Client::fault_code()` and `Client::health()` for state, counters, the
last error, current calibration, rates, and sequence progress before deciding whether to start
a new session. Callback exceptions are counted; they remain non-fatal under reconnect policy
and become a callback fault under fail-stop policy.

## Network security

ATI Net F/T configuration discovery uses HTTP and RDT streaming uses UDP. These device protocols
do not provide transport encryption, peer authentication, or message integrity through this
library. Run the sensor and client on a trusted, isolated network segment; do not expose the
sensor HTTP or RDT ports directly to the Internet or an untrusted shared network. Use firewall
allowlists and an authenticated network boundary when traffic must cross an untrusted network.

Automatic discovery values are not authenticated solely because they came from the configured
sensor address. Verify the discovered calibration and units before enabling a controller. A
complete manual calibration override avoids HTTP discovery when independently verified values
are required, but it does not authenticate the UDP measurement stream. See
[SECURITY.md](SECURITY.md) for the supported network model, deployment controls, and private
vulnerability reporting.

## Hardware safety

Force/torque data can feed motion, force-control, and protective-limit decisions. Treat the
sensor, network, application, controller, and independent safety system as one installation:

- Verify the discovered units and calibration against the sensor configuration before enabling
  any controller. Do not copy the example address or calibration into another installation.
- Applying software bias changes the measurement zero and can change downstream control and
  limit behavior. Unload or fixture the sensor as required, stop hazardous motion, and keep
  people clear before calling `Client::bias()` or running `netft bias`.
- Test disconnects, stale data, device status errors, and the selected recovery policy under
  controlled conditions. This software is not a substitute for an emergency stop or a
  safety-rated control path.

Repository tests use simulated HTTP and UDP sensors. Connecting tests or examples to physical
hardware is always an explicit, operator-approved action.

### Opt-in hardware verification

The hardware harness builds and installs a fresh Release CLI into a temporary prefix, checks
the sensor-selected calibration returned by automatic discovery, and monitors a two-second
stream without printing individual samples. It requires an explicit sensor host and does not
assume a particular unit system or calibration scale:

```bash
NETFT_SENSOR_HOST=192.168.1.1 pixi run hardware-test
```

The default `hardware-test` task explicitly forces `NETFT_ALLOW_BIAS=0`, so an inherited shell
setting cannot enable bias. Review the reported product, units, scales, rates, and counters
against the intended installation before using the results.

The bias variant must be treated as a separate physical operation. Before every individual
run, obtain fresh operator authorization, verify that the named sensor is the intended device,
unload or fixture it as required, stop hazardous motion, and keep people clear. Only after
those checks may an operator deliberately run:

```bash
NETFT_SENSOR_HOST=192.168.1.1 pixi run hardware-bias-test
```

Authorization from an earlier run must not be reused. The bias task opts in by setting
`NETFT_ALLOW_BIAS=1`; the harness then requires the CLI result to report
`"bias_applied": true`.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for the development workflow, tests, formatting rules,
hardware-test policy, versioning, and `ros-netft` backports.

## License

This project is licensed under the Apache License 2.0. See [LICENSE](LICENSE).
