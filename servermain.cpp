#include <arpa/inet.h>
#include <fcntl.h>
#include <iostream>
#include <queue>
#include <thread>
#include <unistd.h>

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

struct CalcTask {
  char *type;
  int client;
  int ires;
  double fres;
};

void send_task(int client, std::mutex *mutex, CalcTask *task) {
  mutex->lock();

  char *op = randomType();
  char command[50] = {};
  task->client = client;
  task->type = op;

  if (op[0] == 'f') {
    double a = randomFloat();
    double b = randomFloat();
    if (strncmp(op, "fadd", 3) == 0) {
      task->fres = a + b;
    } else if (strncmp(op, "fdiv", 3) == 0) {
      task->fres = a / b;
    } else if (strncmp(op, "fmul", 3) == 0) {
      task->fres = a * b;
    } else if (strncmp(op, "fsub", 3) == 0) {
      task->fres = a - b;
    }
    snprintf(command, 50, "%s %8.8g %8.8g\n", op, a, b);
  } else {
    int a = randomInt();
    int b = randomInt();
    if (strncmp(op, "add", 3) == 0) {
      task->ires = a + b;
    } else if (strncmp(op, "div", 3) == 0) {
      task->ires = a / b;
    } else if (strncmp(op, "mul", 3) == 0) {
      task->ires = a * b;
    } else if (strncmp(op, "sub", 3) == 0) {
      task->ires = a - b;
    }
    snprintf(command, 50, "%s %d %d\n", op, a, b);
  }

  send(client, command, strlen(command), 0);

  mutex->unlock();
}

void disconnect_client(std::queue<int> &q, int client, fd_set *read_fds) {
  std::queue<int> temp;

  while (!q.empty()) {
    int front = q.front();
    q.pop();

    if (front != client)
      temp.push(front);
  }

  while (!temp.empty()) {
    q.push(temp.front());
    temp.pop();
  }

  FD_CLR(client, read_fds);
  close(client);
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

  // int client_sockets[max_clients];
  std::queue<int> client_sockets;

  fcntl(server_socket, F_SETFL, O_NONBLOCK);

  fd_set read_fds, temp_fds;
  FD_ZERO(&read_fds);               // Clear the read_fds set
  FD_SET(server_socket, &read_fds); // Add server socket to read_fds set

  int max_fd = server_socket;

  struct sockaddr_in client_addr;
  std::mutex mutex;
  CalcTask pending_task;

  while (true) {
    temp_fds = read_fds;
    int activity = select(max_fd + 1, &temp_fds, NULL, NULL, NULL);

    if ((activity < 0) && (errno != EINTR)) {
      std::cerr << "Error in select" << std::endl;
      return -1;
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
      if (client_sockets.size() >= max_clients) {
        char reject_msg[] = "Rejected by server.";
        send(new_socket, reject_msg, sizeof(reject_msg), 0);
        close(new_socket);
        FD_CLR(new_socket, &read_fds);
        continue;
      } else {
        client_sockets.push(new_socket);
        std::thread t(send_task, new_socket, &mutex, &pending_task);
        t.detach();
      }

      // Add new socket to read_fds set
      FD_SET(new_socket, &read_fds);

      if (new_socket > max_fd) {
        max_fd = new_socket;
      }
    }

    // Find which client trigger an event
    std::queue<int> temp = client_sockets;
    while (!temp.empty()) {
      char buffer[BUFFER_SIZE] = {};
      int client_socket = temp.front();
      temp.pop();
      if (FD_ISSET(client_socket, &temp_fds)) {
        int valread = read(client_socket, buffer, BUFFER_SIZE);
        if (valread == 0) {
          // Client disconnected
          disconnect_client(client_sockets, client_socket, &read_fds);
        } else {
          if (pending_task.client == client_socket) {
            mutex.unlock();
          } else {
            char msg[] = "Waiting...";
            send(client_socket, msg, strlen(msg), 0);
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

  int status = start_server(address.port, 2);

  return 0;
}
