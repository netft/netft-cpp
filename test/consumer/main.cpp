#include "netft/client.hpp"

int main() {
  netft::Config config;
  netft::validate(config);
  netft::Client client{config};
  try {
    client.bias();
  } catch (const netft::NotConnectedError &) {
    return 0;
  } catch (...) {
    return 1;
  }
  return 2;
}
