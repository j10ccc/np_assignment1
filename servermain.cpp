#include <arpa/inet.h>
#include <fcntl.h>
#include <iostream>
#include <queue>
#include <thread>
#include <unistd.h>

#include "calcLib.h"

#define DEBUG
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 2
#define TIMEOUT 5

struct HostAddress {
  std::string host;
  int port;
};

enum ClientStatusEnum { ProtocolChecking, Waiting, Calculating, Finished };

struct ClientInstance {
  ClientStatusEnum status;
  int id;
};

struct CalcTask {
  char *type;
  ClientInstance client;
  int ires;
  double fres;
};

std::queue<ClientInstance> client_sockets;
fd_set read_fds;
CalcTask pending_task;
std::mutex task_mutex;

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

void disconnect_client(ClientInstance client) {
  std::queue<ClientInstance> temp;

  while (!client_sockets.empty()) {
    ClientInstance front = client_sockets.front();
    client_sockets.pop();

    if (front.id != client.id)
      temp.push(front);
  }

  while (!temp.empty()) {
    client_sockets.push(temp.front());
    temp.pop();
  }

  FD_CLR(client.id, &read_fds);
  close(client.id);
}

void send_task(ClientInstance client) {
  client.status = Waiting;
  task_mutex.lock();

  char *op = randomType();
  char command[50] = {};
  pending_task.client = client;
  pending_task.type = op;

  if (op[0] == 'f') {
    double a = randomFloat();
    double b = randomFloat();
    if (strncmp(op, "fadd", 3) == 0) {
      pending_task.fres = a + b;
    } else if (strncmp(op, "fdiv", 3) == 0) {
      pending_task.fres = a / b;
    } else if (strncmp(op, "fmul", 3) == 0) {
      pending_task.fres = a * b;
    } else if (strncmp(op, "fsub", 3) == 0) {
      pending_task.fres = a - b;
    }
    snprintf(command, 50, "%s %8.8g %8.8g\n", op, a, b);
  } else {
    int a = randomInt();
    int b = randomInt();
    if (strncmp(op, "add", 3) == 0) {
      pending_task.ires = a + b;
    } else if (strncmp(op, "div", 3) == 0) {
      pending_task.ires = a / b;
    } else if (strncmp(op, "mul", 3) == 0) {
      pending_task.ires = a * b;
    } else if (strncmp(op, "sub", 3) == 0) {
      pending_task.ires = a - b;
    }
    snprintf(command, 50, "%s %d %d\n", op, a, b);
  }

  send(client.id, command, strlen(command), 0);
  client.status = Calculating;

  sleep(TIMEOUT);

  if (pending_task.client.id == client.id) {
    char msg[] = "ERROR TO\n";
    send(client.id, msg, strlen(msg), 0);
    client.status = Finished;
    disconnect_client(client);
    task_mutex.unlock();
  }
}

void handle_connection(int client) {}

void check_response(char *raw) {
  try {
    if (pending_task.type[0] == 'f') {
      double res = atof(raw);
      if (abs(res - pending_task.fres) >= 0.0001)
        throw std::runtime_error("Calculate Error.\n");
    } else {
      int res = atoi(raw);
      if (res != pending_task.ires)
        throw std::runtime_error("Calculate Error.\n");
    }
    char msg[] = "OK\n";
    send(pending_task.client.id, msg, strlen(msg), 0);
  } catch (const std::runtime_error &e) {
    send(pending_task.client.id, e.what(), strlen(e.what()), 0);
  }

  pending_task.client.status = Finished;

  disconnect_client(pending_task.client);
}

int start_server(int port) {
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
  listen(server_socket, MAX_CLIENTS);

  fcntl(server_socket, F_SETFL, O_NONBLOCK);

  fd_set temp_fds;
  FD_ZERO(&read_fds);               // Clear the read_fds set
  FD_SET(server_socket, &read_fds); // Add server socket to read_fds set

  int max_fd = server_socket;

  struct sockaddr_in client_addr;

  while (true) {
    temp_fds = read_fds;
    int activity = select(max_fd + 1, &temp_fds, NULL, NULL, NULL);

    if ((activity < 0) && (errno != EINTR)) {
      continue;
    }

    if (FD_ISSET(server_socket, &temp_fds)) {
      int new_socket;
      socklen_t addr_len = sizeof(client_addr);
      new_socket =
          accept(server_socket, (struct sockaddr *)&client_addr, &addr_len);
      if (new_socket < 0) {
        std::cerr << "Error accepting incoming connection" << std::endl;
        return -1;
      }

      // Add new socket to client_sockets queue
      if (client_sockets.size() >= MAX_CLIENTS) {
        char reject_msg[] = "Rejected by server.";
        send(new_socket, reject_msg, sizeof(reject_msg), 0);
        close(new_socket);
        FD_CLR(new_socket, &read_fds);
        continue;
      } else {
        ClientInstance new_client = {ProtocolChecking, new_socket};
        client_sockets.push(new_client);
        std::thread t(send_task, new_client);
        t.detach();
      }

      // Add new socket to read_fds set
      FD_SET(new_socket, &read_fds);

      if (new_socket > max_fd) {
        max_fd = new_socket;
      }
    }

    // Find which client trigger an event
    std::queue<ClientInstance> temp = client_sockets;
    while (!temp.empty()) {
      char buffer[BUFFER_SIZE] = {};
      ClientInstance client_socket = temp.front();
      temp.pop();
      if (FD_ISSET(client_socket.id, &temp_fds)) {
        int valread = read(client_socket.id, buffer, BUFFER_SIZE);
        if (valread == 0) {
          // Client disconnected
          disconnect_client(client_socket);
        } else {
          if (pending_task.client.id == client_socket.id) {
            check_response(buffer);
            task_mutex.unlock();
          } else {
            char msg[] = "Waiting...";
            send(client_socket.id, msg, strlen(msg), 0);
          }
        }
      }
    }
  }

  return 0;
}

int main(int argc, char *argv[]) {
  HostAddress address = parse_address(argv[1]);

#ifdef DEBUG
  printf("Host %s, and port %d.\n", address.host.c_str(), address.port);
#endif

  int status = start_server(address.port);

  return 0;
}
