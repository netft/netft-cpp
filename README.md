# netft-cpp

[![CI](https://github.com/netft/netft-cpp/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/netft/netft-cpp/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/netft/netft-cpp?display_name=tag&sort=semver)](https://github.com/netft/netft-cpp/releases)
[![CodeQL](https://github.com/netft/netft-cpp/actions/workflows/codeql.yml/badge.svg?branch=main)](https://github.com/netft/netft-cpp/actions/workflows/codeql.yml)
[![Coverage](https://codecov.io/gh/netft/netft-cpp/graph/badge.svg?branch=main)](https://codecov.io/gh/netft/netft-cpp)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)](https://en.cppreference.com/w/cpp/17)
[![License](https://img.shields.io/github/license/netft/netft-cpp?label=license)](LICENSE)

`netft-cpp` is a standalone C++17 SDK for receiving calibrated force/torque
samples from ATI Net F/T Ethernet sensors. It provides the reusable native core
for applications that need direct control over acquisition, health, and
recovery. For an end-user terminal application, see
[netft-cli](https://github.com/netft/netft-cli).

## Features

- Discovers calibration scales and measurement units before streaming RDT data.
- Exposes raw counts, calibrated samples, sensor status, and stream health.
- Supports reconnect and fail-stop recovery policies.
- Installs shared or static libraries as the `netft::netft` CMake target.

## Installation

| Platform | Architectures | Support |
| --- | --- | --- |
| Linux | x86-64, AArch64 | Tested |
| macOS | x86-64, Apple silicon | Tested |
| Windows | x86-64 | Tested |

Building requires a C++17 compiler, CMake 3.16 or newer, threads, and
libcurl 7.63.0 or newer.

```bash
git clone https://github.com/netft/netft-cpp.git
cd netft-cpp
cmake -S . -B build/release \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=ON \
  -DBUILD_TESTING=OFF \
  -DCMAKE_INSTALL_PREFIX="$PWD/install"
cmake --build build/release
cmake --install build/release
```

Set `BUILD_SHARED_LIBS=OFF` for a static library. See the
[C++ SDK tutorial](https://netft.dev/docs/tutorials/sdks/cpp) for dependency
setup on each supported platform.

## Quick start

Consume an installed package with CMake config mode:

```cmake
find_package(netft 0.3 CONFIG REQUIRED)

add_executable(read_sensor main.cpp)
target_link_libraries(read_sensor PRIVATE netft::netft)
```

Then receive a sample with `netft::Client`:

```cpp
#include <chrono>
#include <iostream>

#include <netft/client.hpp>

int main() {
  netft::Config config;
  config.sensor_host = "192.168.1.1";

  netft::Client client{config};
  client.start([](const netft::Sample &sample) {
    std::cout << sample.force[0] << ' '
              << netft::to_string(sample.force_unit) << '\n';
  });
  const bool received = client.wait_for_first_sample(std::chrono::seconds{2});
  client.stop();
  return received ? 0 : 1;
}
```

`192.168.1.1` is the ATI factory-default sensor address. Replace it with the
address configured for your sensor.

## Documentation

- [C++ SDK tutorial](https://netft.dev/docs/tutorials/sdks/cpp)
- [C++ API reference](https://netft.dev/docs/references/cpp-api/overview)
- [Reliable acquisition and recovery](https://netft.dev/docs/tutorials/fundamentals/reliable-acquisition)
- [Security and safety](https://netft.dev/docs/references/security-and-safety)

Samples preserve the force and torque units reported by the sensor; the SDK
does not silently convert them to a preferred unit system. Automatic discovery
is recommended. Use a manual calibration override only when the active sensor
configuration has been independently verified.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for the development environment, tests,
hardware-testing policy, versioning, and downstream snapshot workflow. Report
security issues through [SECURITY.md](SECURITY.md).

## License

This project is licensed under the [Apache License 2.0](LICENSE).
