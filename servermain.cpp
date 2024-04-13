#include <cstdio>
#include <stdio.h>
#include <stdlib.h>
#include <string>

// Included to get the support library
#include "calcLib.h"

// Enable if you want debugging to be printed, see examble below.
// Alternative, pass CFLAGS=-DDEBUG to make, make CFLAGS=-DDEBUG
#define DEBUG

struct HostAddress {
  std::string host;
  int port;
};

HostAddress parse_ip_addr(const std::string addr) {
  HostAddress address;

  size_t colonPos = addr.find_last_of(':');

  if (addr[0] == '[') {
    // For ipv6 address, [2001:0db8:85a3:0000:0000:8a2e:0370:7334]:8080
    address.host = addr.substr(1, colonPos - 2);
    address.port = stoi(addr.substr(colonPos + 1));
  } else {
    address.host = addr.substr(0, colonPos);
    address.port = stoi(addr.substr(colonPos + 1));
  }

  return address;
}

int main(int argc, char *argv[]) {
  HostAddress address = parse_ip_addr(argv[1]);

#ifdef DEBUG
  printf("Host %s, and port %d.\n", address.host.c_str(), address.port);
#endif
}
