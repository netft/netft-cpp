# Security Policy

## Supported versions

Security fixes are applied to the latest released version and the `main` branch. Older releases
may not receive backports.

## Reporting a vulnerability

Please do not open a public issue for a suspected vulnerability. Use
[GitHub private vulnerability reporting](https://github.com/netft/netft-cpp/security/advisories/new)
when available. If private reporting is unavailable, contact the maintainer at
hanxudong159@126.com.

Include the affected version, deployment assumptions, a minimal reproduction or data-flow
description, and the security impact. Remove sensor addresses, credentials, and private network
details before submitting a report.

## Network security model

ATI Net F/T devices expose configuration through HTTP and stream RDT records through UDP.
`netft-cpp` implements those device protocols and does not add transport encryption, peer
authentication, or message integrity. Automatic discovery data and RDT records must therefore
not be treated as authenticated solely because they came from the configured sensor address.

The supported deployment model assumes that the sensor and client run on a trusted, isolated
network segment. Do not expose the sensor, its HTTP interface, or its RDT port directly to the
Internet or to an untrusted shared network. Where traffic must cross an untrusted network, place
the complete sensor connection inside an authenticated network boundary such as a managed VPN
or equivalent industrial-network gateway.

Recommended deployment controls include:

- isolate the sensor network with a dedicated interface or VLAN;
- restrict traffic with host and network firewalls to the expected client and sensor addresses;
- prevent untrusted devices from joining, routing to, or bridging the sensor segment;
- monitor unexpected calibration, unit, sensor-address, and connection changes.

The library disables HTTP redirects and proxy use and strictly validates the configuration
response, but those controls do not authenticate its origin.

## Configuration integrity and hardware safety

Automatic discovery reads calibration scales and units over unauthenticated HTTP. Verify the
discovered values against the intended sensor configuration before enabling a controller. When
an application must use fixed, independently verified calibration values, configure a complete
manual calibration override. A manual override avoids HTTP discovery but does not authenticate
the UDP measurement stream.

Force/torque data can affect motion and protective-limit decisions. Use fail-stop behavior where
appropriate, detect stale or invalid data, and retain an independent safety-rated control path.
This library is not a substitute for an emergency stop or other safety system.

## Known protocol limitations

A report that depends only on the absence of HTTPS or authenticated UDP in the ATI device
protocol may describe a known protocol limitation rather than a defect that this library can
correct independently. Reports remain valuable when they demonstrate an unexpected exposure,
a bypass of the stated trusted-network model, unsafe handling of unauthenticated data, or a
practical mitigation that remains compatible with the device.
