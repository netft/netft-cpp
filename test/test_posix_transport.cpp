#include "detail/posix_transport.hpp"

#include <gtest/gtest.h>

#include <poll.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

enum class PollBehavior { RepeatedEintrThenTimeout, Error };

constexpr int kInterruptCount = 3;
std::vector<int> poll_timeouts;
int poll_calls{};
PollBehavior poll_behavior{PollBehavior::RepeatedEintrThenTimeout};

} // namespace

extern "C" int __wrap_poll(pollfd *, nfds_t, const int timeout) {
  poll_timeouts.push_back(timeout);
  if (poll_behavior == PollBehavior::Error) {
    errno = EBADF;
    return -1;
  }
  if (poll_calls++ < kInterruptCount) {
    std::this_thread::sleep_for(10ms);
    errno = EINTR;
    return -1;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds{timeout});
  return 0;
}

TEST(PosixTransportTest, RepeatedEintrPreservesOriginalTimeout) {
  poll_timeouts.clear();
  poll_calls = 0;
  poll_behavior = PollBehavior::RepeatedEintrThenTimeout;

  netft::detail::PosixTransport transport;
  transport.connect("127.0.0.1", 49152);
  std::array<std::uint8_t, 36> buffer{};

  EXPECT_EQ(transport.receive(buffer.data(), buffer.size(), 100ms), 0U);

  ASSERT_EQ(poll_timeouts.size(), static_cast<std::size_t>(kInterruptCount + 1));
  for (std::size_t index = 1; index < poll_timeouts.size(); ++index) {
    EXPECT_LT(poll_timeouts[index], poll_timeouts[index - 1]);
  }
}

TEST(PosixTransportTest, NonEintrPollErrorStillThrows) {
  poll_timeouts.clear();
  poll_calls = 0;
  poll_behavior = PollBehavior::Error;

  netft::detail::PosixTransport transport;
  transport.connect("127.0.0.1", 49152);
  std::array<std::uint8_t, 36> buffer{};

  EXPECT_THROW(transport.receive(buffer.data(), buffer.size(), 100ms), std::runtime_error);
  EXPECT_EQ(poll_timeouts.size(), 1U);
}
