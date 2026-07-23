#include <gtest/gtest.h>

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <locale>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "cli.hpp"
#include "detail/protocol.hpp"
#include "support/fake_sensor.hpp"

extern char **environ;

namespace {

using namespace std::chrono_literals;

netft::cli::Options parse(std::initializer_list<const char *> arguments) {
  std::vector<std::string> values;
  for (const auto *argument : arguments) {
    values.emplace_back(argument);
  }
  return netft::cli::parse_options(values);
}

netft::cli::Options options_for(netft::cli::Command command,
                                const netft::test::FakeSensor &sensor) {
  netft::cli::Options options;
  options.command = command;
  options.config.sensor_host = sensor.host();
  options.config.rdt_port = sensor.rdt_port();
  options.config.http_port = sensor.http_port();
  options.config.receive_timeout = 50ms;
  options.config.configuration_connect_timeout = 100ms;
  options.config.configuration_timeout = 250ms;
  options.duration = 50ms;
  options.json = true;
  return options;
}

bool sample_count_matches_delivered_count(const std::string &json) {
  std::smatch sample_match;
  std::smatch delivered_match;
  const std::regex sample_pattern{"\\\"sample_count\\\":([0-9]+)"};
  const std::regex delivered_pattern{"\\\"delivered_count\\\":([0-9]+)"};
  return std::regex_search(json, sample_match, sample_pattern) &&
         std::regex_search(json, delivered_match, delivered_pattern) &&
         sample_match[1] == delivered_match[1];
}

std::uint64_t json_unsigned(const std::string &json, const std::string &key) {
  std::smatch match;
  const std::regex pattern{"\\\"" + key + "\\\":([0-9]+)"};
  if (!std::regex_search(json, match, pattern)) {
    throw std::runtime_error("missing JSON unsigned field: " + key);
  }
  return std::stoull(match[1]);
}

std::filesystem::path temporary_path(const std::string &suffix) {
  return std::filesystem::temp_directory_path() /
         ("netft-cli-" + std::to_string(::getpid()) + "-" + suffix);
}

pid_t spawn_cli(const std::vector<std::string> &arguments, int stdout_fd, int stderr_fd) {
  std::vector<std::string> storage;
  storage.reserve(arguments.size() + 1);
  storage.emplace_back(NETFT_CLI_PATH);
  storage.insert(storage.end(), arguments.begin(), arguments.end());
  std::vector<char *> argv;
  argv.reserve(storage.size() + 1);
  for (auto &argument : storage) {
    argv.push_back(argument.data());
  }
  argv.push_back(nullptr);

  posix_spawn_file_actions_t actions;
  if (::posix_spawn_file_actions_init(&actions) != 0) {
    throw std::runtime_error("cannot initialize spawn file actions");
  }
  const int stdout_result = ::posix_spawn_file_actions_adddup2(&actions, stdout_fd, STDOUT_FILENO);
  const int stderr_result = ::posix_spawn_file_actions_adddup2(&actions, stderr_fd, STDERR_FILENO);
  pid_t pid{};
  const int spawn_result =
      stdout_result == 0 && stderr_result == 0
          ? ::posix_spawn(&pid, NETFT_CLI_PATH, &actions, nullptr, argv.data(), environ)
          : EINVAL;
  ::posix_spawn_file_actions_destroy(&actions);
  if (spawn_result != 0) {
    throw std::runtime_error("cannot spawn netft CLI");
  }
  return pid;
}

class CommaDecimalPoint final : public std::numpunct<char> {
protected:
  char do_decimal_point() const override { return ','; }
  char do_thousands_sep() const override { return '_'; }
  std::string do_grouping() const override { return "\3"; }
};

class GlobalLocaleGuard {
public:
  explicit GlobalLocaleGuard(const std::locale &replacement)
      : previous_(std::locale::global(replacement)) {}
  ~GlobalLocaleGuard() { std::locale::global(previous_); }

private:
  std::locale previous_;
};

TEST(CliParser, RecognizesHelpAndDefaultsMonitorToFiveSeconds) {
  EXPECT_TRUE(parse({"--help"}).help);
  const auto options = parse({"monitor", "--host", "sensor.local"});
  EXPECT_EQ(options.command, netft::cli::Command::Monitor);
  EXPECT_EQ(options.config.sensor_host, "sensor.local");
  EXPECT_DOUBLE_EQ(options.duration.count(), 5.0);
}

TEST(CliParser, RejectsMissingUnknownAndInvalidValues) {
  EXPECT_THROW(parse({}), netft::cli::UsageError);
  EXPECT_THROW(parse({"unknown"}), netft::cli::UsageError);
  EXPECT_THROW(parse({"monitor", "--rdt-port", "0"}), netft::cli::UsageError);
  EXPECT_THROW(parse({"monitor", "--http-port", "65536"}), netft::cli::UsageError);
  EXPECT_THROW(parse({"monitor", "--duration", "nan"}), netft::cli::UsageError);
  EXPECT_THROW(parse({"monitor", "--duration", "0"}), netft::cli::UsageError);
}

TEST(CliParser, RejectsEveryUnsupportedBoundaryWithoutInspectingCopy) {
  const std::vector<std::vector<std::string>> cases{
      {"monitor", "--host"},
      {"monitor", "--duration", "not-a-number"},
      {"monitor", "--rdt-port", "not-a-port"},
      {"monitor", "--output", ""},
      {"monitor", "--force-unit", "unsupported"},
      {"monitor", "--torque-unit", "unsupported"},
      {"monitor", "--unknown"},
      {"info", "--duration", "1"},
      {"bias", "--duration", "1"},
      {"monitor", "--host", " "},
      {"info", "--counts-per-force-unit", "1", "--counts-per-torque-unit", "1", "--force-unit", "N",
       "--torque-unit", "N-mm"},
  };

  for (const auto &arguments : cases) {
    EXPECT_THROW(netft::cli::parse_options(arguments), netft::cli::UsageError);
  }
}

TEST(CliParser, RequiresCompleteManualCalibration) {
  EXPECT_THROW(parse({"monitor", "--counts-per-force-unit", "1000000"}), netft::cli::UsageError);

  const auto options =
      parse({"monitor", "--counts-per-force-unit", "1000000", "--counts-per-torque-unit", "2000000",
             "--force-unit", "N", "--torque-unit", "N-mm"});
  ASSERT_TRUE(options.config.calibration_override);
  EXPECT_EQ(options.config.calibration_override->force_unit, netft::ForceUnit::Newton);
  EXPECT_EQ(options.config.calibration_override->torque_unit, netft::TorqueUnit::NewtonMillimeter);
}

TEST(CliInfo, DiscoversConfigurationWithoutStartingRdt) {
  netft::test::FakeSensor sensor;
  auto options = options_for(netft::cli::Command::Info, sensor);
  std::ostringstream output;
  std::ostringstream errors;

  EXPECT_EQ(netft::cli::run(options, output, errors), 0);
  EXPECT_TRUE(errors.str().empty());
  EXPECT_TRUE(sensor.wait_for_http_request());
  EXPECT_FALSE(sensor.wait_for_command(netft::detail::Command::StartRealtime, 1, 20ms));
  const auto text = output.str();
  EXPECT_NE(text.find("\"product\":\"Fake Net F/T\""), std::string::npos);
  EXPECT_NE(text.find("\"endpoint\":\"127.0.0.1:"), std::string::npos);
  EXPECT_NE(text.find("\"configuration_source\":\"sensor\""), std::string::npos);
  EXPECT_NE(text.find("\"force_unit\":\"N\""), std::string::npos);
  EXPECT_NE(text.find("\"torque_unit\":\"N-m\""), std::string::npos);
  EXPECT_EQ(text.find("\"sample_count\""), std::string::npos);
}

TEST(CliInfo, RejectsInvalidUtf8WithoutReplacingAtomicOutput) {
  const std::vector<std::string> invalid_products{
      std::string{"invalid"} + static_cast<char>(0x80),
      std::string{"invalid"} + static_cast<char>(0xc0) + static_cast<char>(0xaf),
      std::string{"invalid"} + static_cast<char>(0xe0) + static_cast<char>(0x80) +
          static_cast<char>(0xaf),
      std::string{"invalid"} + static_cast<char>(0xf0) + static_cast<char>(0x80) +
          static_cast<char>(0x80) + static_cast<char>(0xaf),
      std::string{"invalid"} + static_cast<char>(0xe2) + '(' + static_cast<char>(0xa1),
      std::string{"invalid"} + static_cast<char>(0xe2) + static_cast<char>(0x82),
      std::string{"invalid"} + static_cast<char>(0xed) + static_cast<char>(0xa0) +
          static_cast<char>(0x80),
      std::string{"invalid"} + static_cast<char>(0xf4) + static_cast<char>(0x90) +
          static_cast<char>(0x80) + static_cast<char>(0x80),
      std::string{"invalid"} + static_cast<char>(0xf5) + static_cast<char>(0x80) +
          static_cast<char>(0x80) + static_cast<char>(0x80),
  };

  for (std::size_t index = 0; index < invalid_products.size(); ++index) {
    SCOPED_TRACE(index);
    netft::test::FakeSensor sensor;
    sensor.set_xml_configuration("<netft><prodname>" + invalid_products[index] +
                                 "</prodname><cfgcpf>1000000</cfgcpf><cfgcpt>1000000</cfgcpt>"
                                 "<scfgfu>N</scfgfu><scfgtu>Nm</scfgtu></netft>");
    auto options = options_for(netft::cli::Command::Info, sensor);
    const auto path = temporary_path("invalid-utf8-" + std::to_string(index) + ".json");
    const std::string original_contents = std::to_string(::getpid()) + std::to_string(index);
    {
      std::ofstream existing{path};
      existing << original_contents;
    }
    options.output_path = path.string();
    std::ostringstream output;
    std::ostringstream errors;

    EXPECT_EQ(netft::cli::run(options, output, errors), 2);
    EXPECT_TRUE(output.str().empty());
    EXPECT_FALSE(errors.str().empty());
    std::ifstream existing{path};
    const std::string contents{std::istreambuf_iterator<char>{existing}, {}};
    EXPECT_EQ(contents, original_contents);
    std::filesystem::remove(path);
  }
}

TEST(CliInfo, PreservesValidUtf8WhileEscapingJsonControls) {
  netft::test::FakeSensor sensor;
  const std::string product = std::string{"\xc3\xa9\xe5\x8a\x9b"} + "\"" +
                              "\xe4\xbc\xa0\n\xe6\x84\x9f\xe5\x99\xa8\xf0\x9f\xa7\xad";
  sensor.set_xml_configuration("<netft><prodname>" + product +
                               "</prodname><cfgcpf>1000000</cfgcpf><cfgcpt>1000000</cfgcpt>"
                               "<scfgfu>N</scfgfu><scfgtu>Nm</scfgtu></netft>");
  auto options = options_for(netft::cli::Command::Info, sensor);
  std::ostringstream output;
  std::ostringstream errors;

  ASSERT_EQ(netft::cli::run(options, output, errors), 0);
  EXPECT_TRUE(errors.str().empty());
  EXPECT_NE(output.str().find(std::string{"\"product\":\"\xc3\xa9\xe5\x8a\x9b"} +
                              "\\\"\xe4\xbc\xa0\\n\xe6\x84\x9f\xe5\x99\xa8"
                              "\xf0\x9f\xa7\xad\""),
            std::string::npos);
}

TEST(CliInfo, SerializesEveryJsonControlBranch) {
  netft::test::FakeSensor sensor;
  std::string product{"controls"};
  product.push_back('"');
  product.push_back('\\');
  product.push_back('\b');
  product.push_back('\f');
  product.push_back('\n');
  product.push_back('\r');
  product.push_back('\t');
  product.push_back(static_cast<char>(0x01));
  sensor.set_xml_configuration("<netft><prodname>" + product +
                               "</prodname><cfgcpf>1000000</cfgcpf><cfgcpt>1000000</cfgcpt>"
                               "<scfgfu>N</scfgfu><scfgtu>Nm</scfgtu></netft>");
  auto options = options_for(netft::cli::Command::Info, sensor);
  std::ostringstream output;
  std::ostringstream errors;

  ASSERT_EQ(netft::cli::run(options, output, errors), 0);
  EXPECT_NE(output.str().find(R"json("product":"controls\"\\\b\f\n\r\t\u0001")json"),
            std::string::npos);
  EXPECT_TRUE(errors.str().empty());
}

TEST(CliInfo, HumanOutputIsNonempty) {
  netft::test::FakeSensor sensor;
  auto options = options_for(netft::cli::Command::Info, sensor);
  options.json = false;
  std::ostringstream output;
  std::ostringstream errors;

  EXPECT_EQ(netft::cli::run(options, output, errors), 0);
  EXPECT_FALSE(output.str().empty());
  EXPECT_TRUE(errors.str().empty());
}

TEST(CliMonitor, EmitsStableJsonAndUsesDeliveredCountAsSampleCount) {
  netft::test::FakeSensor sensor;
  auto options = options_for(netft::cli::Command::Monitor, sensor);
  std::ostringstream output;
  std::ostringstream errors;

  EXPECT_EQ(netft::cli::run(options, output, errors), 0);
  const auto text = output.str();
  for (const auto *key :
       {"elapsed_s",          "sample_count",        "received_count",     "delivered_count",
        "rate_limited_count", "receive_rate_hz",     "delivery_rate_hz",   "lost_count",
        "duplicate_count",    "out_of_order_count",  "malformed_count",    "reconnect_count",
        "timeout_count",      "warning_count",       "device_error_count", "device_status",
        "fault_code",         "last_rdt_sequence",   "last_ft_sequence",   "last_force",
        "last_torque",        "requested_duration_s"}) {
    EXPECT_NE(text.find(std::string{"\""} + key + "\""), std::string::npos) << key;
  }
  const auto health = sensor.commands();
  EXPECT_FALSE(health.empty());
  EXPECT_TRUE(errors.str().empty());
  EXPECT_TRUE(sample_count_matches_delivered_count(text));
}

TEST(CliMonitor, JsonUsesClassicLocaleForEveryFloatingPointValue) {
  GlobalLocaleGuard locale_guard{std::locale{std::locale::classic(), new CommaDecimalPoint}};
  netft::test::FakeSensor sensor;
  auto options = options_for(netft::cli::Command::Monitor, sensor);
  std::ostringstream output;
  std::ostringstream errors;

  ASSERT_EQ(netft::cli::run(options, output, errors), 0);
  EXPECT_TRUE(errors.str().empty());
  EXPECT_NE(output.str().find("\"requested_duration_s\":0.0"), std::string::npos);
  EXPECT_NE(
      output.str().find("\"endpoint\":\"127.0.0.1:" + std::to_string(sensor.rdt_port()) + "\""),
      std::string::npos);
}

TEST(CliMonitor, RejectsNonfiniteJsonWithoutEmittingInvalidDocument) {
  netft::test::FakeSensor sensor;
  sensor.pause();
  constexpr std::array<std::int32_t, 6> maximum_axes{
      std::numeric_limits<std::int32_t>::max(), std::numeric_limits<std::int32_t>::max(),
      std::numeric_limits<std::int32_t>::max(), std::numeric_limits<std::int32_t>::max(),
      std::numeric_limits<std::int32_t>::max(), std::numeric_limits<std::int32_t>::max()};
  for (std::uint32_t sequence = 1; sequence <= 64; ++sequence) {
    sensor.queue_record(sequence, 0, 1000 + sequence * 4, maximum_axes);
  }
  auto options = options_for(netft::cli::Command::Monitor, sensor);
  options.config.calibration_override =
      netft::Calibration{1e-300, 1e-300, netft::ForceUnit::Newton, netft::TorqueUnit::NewtonMeter};
  const auto output_path = temporary_path("nonfinite.json");
  const std::string original_contents = std::to_string(::getpid());
  {
    std::ofstream existing{output_path};
    existing << original_contents;
  }
  options.output_path = output_path.string();
  sensor.resume();
  std::ostringstream output;
  std::ostringstream errors;

  EXPECT_EQ(netft::cli::run(options, output, errors), 2);
  EXPECT_TRUE(output.str().empty());
  EXPECT_FALSE(errors.str().empty());
  std::ifstream existing{output_path};
  const std::string contents{std::istreambuf_iterator<char>{existing}, {}};
  EXPECT_EQ(contents, original_contents);
  std::filesystem::remove(output_path);
}

TEST(CliMonitor, HumanOutputIsNonempty) {
  netft::test::FakeSensor sensor;
  auto options = options_for(netft::cli::Command::Monitor, sensor);
  options.json = false;
  std::ostringstream output;
  std::ostringstream errors;

  ASSERT_EQ(netft::cli::run(options, output, errors), 0);
  EXPECT_TRUE(errors.str().empty());
  EXPECT_FALSE(output.str().empty());
}

TEST(CliMonitor, ReturnsOneForDeviceWarnings) {
  netft::test::FakeSensor sensor;
  sensor.pause();
  sensor.queue_record(1, 0x80010000U, 100);
  auto options = options_for(netft::cli::Command::Monitor, sensor);
  options.duration = 80ms;
  sensor.resume();
  std::ostringstream output;
  std::ostringstream errors;

  EXPECT_EQ(netft::cli::run(options, output, errors), 1);
  EXPECT_NE(output.str().find("\"warning_count\":"), std::string::npos);
}

TEST(CliMonitor, SeriousOnlyStreamReturnsOneWithStatusEvidence) {
  netft::test::FakeSensor sensor;
  sensor.pause();
  constexpr std::uint32_t serious_status = 0x00000001U;
  for (std::uint32_t sequence = 1; sequence <= 16; ++sequence) {
    sensor.queue_record(sequence, serious_status, 1000 + sequence * 4);
  }
  auto options = options_for(netft::cli::Command::Monitor, sensor);
  options.duration = 80ms;
  sensor.resume();
  std::ostringstream output;
  std::ostringstream errors;

  EXPECT_EQ(netft::cli::run(options, output, errors), 1);
  EXPECT_TRUE(errors.str().empty());
  EXPECT_NE(output.str().find("\"device_error_count\":1"), std::string::npos);
  EXPECT_NE(output.str().find("\"device_status\":1"), std::string::npos);
}

TEST(CliMonitor, ReturnsTwoWhenNoSampleArrives) {
  netft::test::FakeSensor sensor;
  sensor.pause();
  auto options = options_for(netft::cli::Command::Monitor, sensor);
  options.duration = 30ms;
  std::ostringstream output;
  std::ostringstream errors;

  EXPECT_EQ(netft::cli::run(options, output, errors), 2);
  EXPECT_TRUE(output.str().empty());
  EXPECT_FALSE(errors.str().empty());
}

TEST(CliMonitor, ReturnsTwoAfterAStoredClientTimeout) {
  netft::test::FakeSensor sensor;
  sensor.pause();
  auto options = options_for(netft::cli::Command::Monitor, sensor);
  options.config.receive_timeout = 20ms;
  options.config.recovery_policy = netft::RecoveryPolicy::FailStop;
  options.duration = 80ms;
  std::ostringstream output;
  std::ostringstream errors;

  EXPECT_EQ(netft::cli::run(options, output, errors), 2);
  EXPECT_TRUE(output.str().empty());
  EXPECT_FALSE(errors.str().empty());
}

TEST(CliBias, BiasesAfterFirstSampleAndReportsLaterSample) {
  netft::test::FakeSensor sensor{10.0};
  auto options = options_for(netft::cli::Command::Bias, sensor);
  options.config.receive_timeout = 250ms;
  std::ostringstream output;
  std::ostringstream errors;

  EXPECT_EQ(netft::cli::run(options, output, errors), 0);
  EXPECT_TRUE(sensor.wait_for_command(netft::detail::Command::SetSoftwareBias));
  const auto commands = sensor.commands();
  ASSERT_GE(commands.size(), 3U);
  EXPECT_EQ(commands[0], netft::detail::Command::StartRealtime);
  EXPECT_EQ(commands[1], netft::detail::Command::SetSoftwareBias);
  EXPECT_EQ(commands[2], netft::detail::Command::StartRealtime);
  EXPECT_NE(output.str().find("\"bias_applied\":true"), std::string::npos);
  EXPECT_TRUE(sample_count_matches_delivered_count(output.str()));
  EXPECT_EQ(json_unsigned(output.str(), "last_rdt_sequence"), 2U);
  EXPECT_GE(json_unsigned(output.str(), "delivered_count"), 3U);
}

TEST(CliBias, StopsWhenAlreadyInterruptedBeforeAFirstSample) {
  netft::test::FakeSensor sensor;
  sensor.pause();
  auto options = options_for(netft::cli::Command::Bias, sensor);
  volatile std::sig_atomic_t interrupted = 1;
  std::ostringstream output;
  std::ostringstream errors;

  EXPECT_EQ(netft::cli::run(options, output, errors, &interrupted), 130);
  EXPECT_TRUE(output.str().empty());
}

TEST(CliBias, ReturnsTwoWhenTheClientFaultsBeforeAFirstSample) {
  netft::test::FakeSensor sensor;
  sensor.pause();
  auto options = options_for(netft::cli::Command::Bias, sensor);
  options.config.receive_timeout = 20ms;
  options.config.recovery_policy = netft::RecoveryPolicy::FailStop;
  std::ostringstream output;
  std::ostringstream errors;

  EXPECT_EQ(netft::cli::run(options, output, errors), 2);
  EXPECT_TRUE(output.str().empty());
  EXPECT_FALSE(errors.str().empty());
}

TEST(CliOutput, WritesResultToRequestedFile) {
  netft::test::FakeSensor sensor;
  auto options = options_for(netft::cli::Command::Info, sensor);
  const auto path = std::filesystem::temp_directory_path() /
                    ("netft-cli-" + std::to_string(::getpid()) + ".json");
  std::filesystem::remove(path);
  options.output_path = path.string();
  std::ostringstream output;
  std::ostringstream errors;

  ASSERT_EQ(netft::cli::run(options, output, errors), 0);
  EXPECT_TRUE(output.str().empty());
  std::ifstream file{path};
  const std::string text{std::istreambuf_iterator<char>{file}, {}};
  EXPECT_FALSE(text.empty());
  std::filesystem::remove(path);
}

TEST(CliOutput, PreservesAnExistingDirectoryWhenReplacementFails) {
  netft::test::FakeSensor sensor;
  auto options = options_for(netft::cli::Command::Info, sensor);
  const auto path = temporary_path("output-directory");
  std::filesystem::remove_all(path);
  ASSERT_TRUE(std::filesystem::create_directory(path));
  options.output_path = path.string();
  std::ostringstream output;
  std::ostringstream errors;

  EXPECT_EQ(netft::cli::run(options, output, errors), 2);
  EXPECT_TRUE(output.str().empty());
  EXPECT_TRUE(std::filesystem::is_directory(path));
  std::filesystem::remove(path);
}

TEST(CliProcess, SigintStopsClientAndReturns130) {
  netft::test::FakeSensor sensor;
  const int null_fd = ::open("/dev/null", O_WRONLY);
  ASSERT_GE(null_fd, 0);
  const auto pid = spawn_cli({"monitor", "--host", sensor.host(), "--rdt-port",
                              std::to_string(sensor.rdt_port()), "--http-port",
                              std::to_string(sensor.http_port()), "--duration", "30", "--json"},
                             null_fd, null_fd);
  ::close(null_fd);

  ASSERT_TRUE(sensor.wait_for_command(netft::detail::Command::StartRealtime));
  ASSERT_EQ(::kill(pid, SIGINT), 0);
  int status{};
  ASSERT_EQ(::waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 130);
  EXPECT_TRUE(sensor.wait_for_command(netft::detail::Command::StopStreaming));
}

TEST(CliProcess, SigintDuringInfoSuppressesResultAndReturns130AfterDiscovery) {
  netft::test::FakeSensor sensor;
  sensor.set_http_response_delay(200ms);
  const auto stdout_path = temporary_path("info-sigint.out");
  std::filesystem::remove(stdout_path);
  const int stdout_fd = ::open(stdout_path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0600);
  const int null_fd = ::open("/dev/null", O_WRONLY);
  ASSERT_GE(stdout_fd, 0);
  ASSERT_GE(null_fd, 0);
  const auto pid = spawn_cli({"info", "--host", sensor.host(), "--http-port",
                              std::to_string(sensor.http_port()), "--json"},
                             stdout_fd, null_fd);
  ::close(stdout_fd);
  ::close(null_fd);

  ASSERT_TRUE(sensor.wait_for_http_request());
  ASSERT_EQ(::kill(pid, SIGINT), 0);
  int status{};
  EXPECT_EQ(::waitpid(pid, &status, WNOHANG), 0) << "info exited before delayed discovery unwound";
  ASSERT_EQ(::waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 130);
  std::ifstream file{stdout_path};
  const std::string text{std::istreambuf_iterator<char>{file}, {}};
  EXPECT_TRUE(text.empty());
  std::filesystem::remove(stdout_path);
}

} // namespace
