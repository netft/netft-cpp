#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <utility>

#include "netft/status.hpp"

namespace {

class StatusBit : public ::testing::TestWithParam<std::pair<std::uint32_t, const char *>> {};

TEST(Status, ClassifiesHealthConditionAndError) {
  EXPECT_EQ(netft::classify_status(0), netft::StatusSeverity::Ok);
  EXPECT_EQ(netft::classify_status(0x80010000U), netft::StatusSeverity::Warn);
  EXPECT_EQ(netft::classify_status(0x80020000U), netft::StatusSeverity::Error);
  EXPECT_EQ(netft::classify_status(0x00010000U), netft::StatusSeverity::Error);
}

TEST_P(StatusBit, DecodesEveryDefinedActiveBit) {
  const auto [mask, name] = GetParam();
  EXPECT_NE(netft::decode_status(mask).find(name), std::string::npos);
}

INSTANTIATE_TEST_SUITE_P(
    Status, StatusBit,
    ::testing::Values(std::make_pair(0x80000000U, "error summary"),
                      std::make_pair(0x40000000U, "CPU or RAM error"),
                      std::make_pair(0x20000000U, "digital board error"),
                      std::make_pair(0x10000000U, "analog board error"),
                      std::make_pair(0x08000000U, "serial link communication error"),
                      std::make_pair(0x04000000U, "program memory verification error"),
                      std::make_pair(0x02000000U, "halted due to configuration errors"),
                      std::make_pair(0x01000000U, "settings validation error"),
                      std::make_pair(0x00800000U, "configuration incompatible with calibration"),
                      std::make_pair(0x00400000U, "network communication failure"),
                      std::make_pair(0x00200000U, "CAN communication error"),
                      std::make_pair(0x00100000U, "RDT communication error"),
                      std::make_pair(0x00080000U, "EtherNet/IP protocol failure"),
                      std::make_pair(0x00040000U, "DeviceNet protocol failure"),
                      std::make_pair(0x00020000U, "transducer saturation or A/D error"),
                      std::make_pair(0x00010000U, "monitor condition latched"),
                      std::make_pair(0x00004000U, "watchdog timeout error"),
                      std::make_pair(0x00002000U, "stack check error"),
                      std::make_pair(0x00001000U, "serial EEPROM I2C failure"),
                      std::make_pair(0x00000800U, "serial flash SPI failure"),
                      std::make_pair(0x00000400U, "analog board watchdog timeout"),
                      std::make_pair(0x00000200U, "excessive strain gage excitation current"),
                      std::make_pair(0x00000100U, "insufficient strain gage excitation current"),
                      std::make_pair(0x00000080U, "artificial analog ground out of range"),
                      std::make_pair(0x00000040U, "analog board power supply too high"),
                      std::make_pair(0x00000020U, "analog board power supply too low"),
                      std::make_pair(0x00000010U, "serial link data unavailable"),
                      std::make_pair(0x00000008U, "reference voltage or power monitoring error"),
                      std::make_pair(0x00000004U, "internal temperature error"),
                      std::make_pair(0x00000002U, "HTTP protocol failure")));

TEST(Status, ReportsHealthyWhenNoBitsSet) { EXPECT_EQ(netft::decode_status(0), "healthy"); }

} // namespace
