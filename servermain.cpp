#include <arpa/inet.h>
#include <iostream>

#include "calcLib.h"

#define DEBUG
#define BUFFER_SIZE 1024

struct HostAddress {
  std::string host;
  int port;
};

HostAddress parse_address(const std::string addr) {
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

int start_server(int port, int max_clients) {
  // Create server socket
  int server_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (server_socket == -1) {
    std::cerr << "Error creating server socket" << std::endl;
    return -1;
  }

  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(port);

  // Bind server socket to port
  if (bind(server_socket, (struct sockaddr *)&server_addr,
           sizeof(server_addr)) < 0) {
    std::cerr << "Bind failed" << std::endl;
    return -1;
  }

  // Listen for incoming connections
  listen(server_socket, max_clients);

  return 0;
}

int main(int argc, char *argv[]) {
  HostAddress address = parse_address(argv[1]);

#ifdef DEBUG
  printf("Host %s, and port %d.\n", address.host.c_str(), address.port);
#endif

  int status = start_server(address.port, 10);

  return 0;
}
