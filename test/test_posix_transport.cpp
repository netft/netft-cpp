#include "detail/udp_transport.hpp"

#include <gtest/gtest.h>

#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <future>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

enum class PollBehavior {
  RepeatedEintrThenTimeout,
  EintrPastDeadline,
  Error,
  InvalidDescriptor,
  Readable,
  Timeout
};
enum class SendBehavior { Complete, Error, ShortWrite };
enum class ReceiveBehavior { Payload, Interrupted, Error };

constexpr int kInterruptCount = 3;
std::vector<int> poll_timeouts;
int poll_calls{};
std::promise<void> *resolver_entered{};
std::shared_future<void> *resolver_release{};
PollBehavior poll_behavior{PollBehavior::RepeatedEintrThenTimeout};
SendBehavior send_behavior{SendBehavior::Complete};
ReceiveBehavior receive_behavior{ReceiveBehavior::Payload};

void reset_behaviors() {
  poll_timeouts.clear();
  poll_calls = 0;
  poll_behavior = PollBehavior::RepeatedEintrThenTimeout;
  send_behavior = SendBehavior::Complete;
  receive_behavior = ReceiveBehavior::Payload;
}

} // namespace

extern "C" int __real_getaddrinfo(const char *, const char *, const addrinfo *, addrinfo **);
extern "C" int __wrap_getaddrinfo(const char *host, const char *service, const addrinfo *hints,
                                  addrinfo **result) {
  if (resolver_entered && resolver_release) {
    resolver_entered->set_value();
    resolver_release->wait();
  }
  return __real_getaddrinfo(host, service, hints, result);
}

extern "C" int __wrap_poll(pollfd *descriptors, const nfds_t count, const int timeout) {
  poll_timeouts.push_back(timeout);
  switch (poll_behavior) {
  case PollBehavior::Error:
    errno = EBADF;
    return -1;
  case PollBehavior::InvalidDescriptor:
    if (count > 0) {
      descriptors[0].revents = POLLNVAL;
    }
    return 1;
  case PollBehavior::Readable:
    if (count > 0) {
      descriptors[0].revents = POLLIN;
    }
    return 1;
  case PollBehavior::Timeout:
    return 0;
  case PollBehavior::EintrPastDeadline:
    if (poll_calls++ == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds{timeout} + 10ms);
      errno = EINTR;
      return -1;
    }
    return 0;
  case PollBehavior::RepeatedEintrThenTimeout:
    if (poll_calls++ < kInterruptCount) {
      std::this_thread::sleep_for(10ms);
      errno = EINTR;
      return -1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{timeout});
    return 0;
  }
  return 0;
}

extern "C" ssize_t __wrap_send(int, const void *, const std::size_t length, int) {
  if (send_behavior == SendBehavior::Error) {
    errno = EIO;
    return -1;
  }
  if (send_behavior == SendBehavior::ShortWrite) {
    return static_cast<ssize_t>(length - 1);
  }
  return static_cast<ssize_t>(length);
}

extern "C" ssize_t __wrap_recv(int, void *data, const std::size_t length, int) {
  if (receive_behavior == ReceiveBehavior::Interrupted) {
    errno = EINTR;
    return -1;
  }
  if (receive_behavior == ReceiveBehavior::Error) {
    errno = EIO;
    return -1;
  }
  constexpr std::size_t payload_size = 4;
  const auto copied = std::min(length, payload_size);
  std::fill_n(static_cast<std::uint8_t *>(data), copied, 0U);
  return static_cast<ssize_t>(copied);
}

TEST(PosixTransportTest, RepeatedEintrPreservesOriginalTimeout) {
  reset_behaviors();
  poll_behavior = PollBehavior::RepeatedEintrThenTimeout;

  netft::detail::UdpTransport transport;
  transport.connect("127.0.0.1", 49152);
  std::array<std::uint8_t, 36> buffer{};

  EXPECT_EQ(transport.receive(buffer.data(), buffer.size(), 100ms), 0U);

  ASSERT_EQ(poll_timeouts.size(), static_cast<std::size_t>(kInterruptCount + 1));
  for (std::size_t index = 1; index < poll_timeouts.size(); ++index) {
    EXPECT_LT(poll_timeouts[index], poll_timeouts[index - 1]);
  }
}

TEST(PosixTransportTest, NonEintrPollErrorStillThrows) {
  reset_behaviors();
  poll_behavior = PollBehavior::Error;

  netft::detail::UdpTransport transport;
  transport.connect("127.0.0.1", 49152);
  std::array<std::uint8_t, 36> buffer{};

  EXPECT_THROW(transport.receive(buffer.data(), buffer.size(), 100ms), std::runtime_error);
  EXPECT_EQ(poll_timeouts.size(), 1U);
}

TEST(PosixTransportTest, ExpiredDeadlineDoesNotRepoll) {
  reset_behaviors();
  poll_behavior = PollBehavior::EintrPastDeadline;

  netft::detail::UdpTransport transport;
  transport.connect("127.0.0.1", 49152);
  std::array<std::uint8_t, 36> buffer{};

  EXPECT_EQ(transport.receive(buffer.data(), buffer.size(), 20ms), 0U);
  EXPECT_EQ(poll_timeouts.size(), 1U);
}

TEST(PosixTransportTest, SendBeforeConnectThrows) {
  reset_behaviors();
  netft::detail::UdpTransport transport;
  std::array<std::uint8_t, 8> request{};
  EXPECT_THROW(transport.send(request), std::runtime_error);
}

TEST(PosixTransportTest, ReceiveBeforeConnectThrows) {
  reset_behaviors();
  netft::detail::UdpTransport transport;
  std::array<std::uint8_t, 36> buffer{};
  EXPECT_THROW(transport.receive(buffer.data(), buffer.size(), 20ms), std::runtime_error);
}

TEST(PosixTransportTest, SendErrorThrows) {
  reset_behaviors();
  send_behavior = SendBehavior::Error;
  netft::detail::UdpTransport transport;
  transport.connect("127.0.0.1", 49152);
  std::array<std::uint8_t, 8> request{};
  EXPECT_THROW(transport.send(request), std::runtime_error);
}

TEST(PosixTransportTest, ShortSendThrows) {
  reset_behaviors();
  send_behavior = SendBehavior::ShortWrite;
  netft::detail::UdpTransport transport;
  transport.connect("127.0.0.1", 49152);
  std::array<std::uint8_t, 8> request{};
  EXPECT_THROW(transport.send(request), std::runtime_error);
}

TEST(PosixTransportTest, InvalidPollDescriptorThrows) {
  reset_behaviors();
  poll_behavior = PollBehavior::InvalidDescriptor;
  netft::detail::UdpTransport transport;
  transport.connect("127.0.0.1", 49152);
  std::array<std::uint8_t, 36> buffer{};
  EXPECT_THROW(transport.receive(buffer.data(), buffer.size(), 20ms), std::runtime_error);
}

TEST(PosixTransportTest, InterruptedReceiveReturnsZero) {
  reset_behaviors();
  poll_behavior = PollBehavior::Readable;
  receive_behavior = ReceiveBehavior::Interrupted;
  netft::detail::UdpTransport transport;
  transport.connect("127.0.0.1", 49152);
  std::array<std::uint8_t, 36> buffer{};
  EXPECT_EQ(transport.receive(buffer.data(), buffer.size(), 20ms), 0U);
}

TEST(PosixTransportTest, ReceiveErrorThrows) {
  reset_behaviors();
  poll_behavior = PollBehavior::Readable;
  receive_behavior = ReceiveBehavior::Error;
  netft::detail::UdpTransport transport;
  transport.connect("127.0.0.1", 49152);
  std::array<std::uint8_t, 36> buffer{};
  EXPECT_THROW(transport.receive(buffer.data(), buffer.size(), 20ms), std::runtime_error);
}

TEST(PosixTransportTest, ReadableDatagramReturnsPayloadSize) {
  reset_behaviors();
  poll_behavior = PollBehavior::Readable;
  receive_behavior = ReceiveBehavior::Payload;
  netft::detail::UdpTransport transport;
  transport.connect("127.0.0.1", 49152);
  std::array<std::uint8_t, 36> buffer{};
  EXPECT_EQ(transport.receive(buffer.data(), buffer.size(), 20ms), 4U);
}

TEST(PosixTransportTest, LargeTimeoutSaturatesPollMilliseconds) {
  reset_behaviors();
  poll_behavior = PollBehavior::Timeout;
  netft::detail::UdpTransport transport;
  transport.connect("127.0.0.1", 49152);
  std::array<std::uint8_t, 36> buffer{};
  const auto timeout = std::chrono::duration<double>{
      static_cast<double>(std::numeric_limits<int>::max()) / 1000.0 + 1.0};
  EXPECT_EQ(transport.receive(buffer.data(), buffer.size(), timeout), 0U);
  ASSERT_EQ(poll_timeouts.size(), 1U);
  EXPECT_EQ(poll_timeouts.front(), std::numeric_limits<int>::max());
}

TEST(PosixTransportTest, ShutdownCannotCancelSystemResolution) {
  netft::detail::UdpTransport transport;
  std::promise<void> entered, release;
  auto entered_future = entered.get_future();
  auto release_future = release.get_future().share();
  resolver_entered = &entered;
  resolver_release = &release_future;
  auto connection = std::async(std::launch::async, [&] { transport.connect("127.0.0.1", 49152); });
  const auto entered_status = entered_future.wait_for(1s);
  EXPECT_EQ(entered_status, std::future_status::ready);
  if (entered_status == std::future_status::ready) {
    transport.shutdown();
    EXPECT_EQ(connection.wait_for(10ms), std::future_status::timeout);
  }
  release.set_value();
  EXPECT_NO_THROW(connection.get());
  resolver_entered = nullptr;
  resolver_release = nullptr;
}
