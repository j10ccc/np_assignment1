#include <arpa/inet.h>
#include <fcntl.h>
#include <iostream>
#include <netdb.h>
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

void handle_connect(int new_socket) {
  char msg[] = "TEXT TCP 1.0\n";
  send(new_socket, msg, strlen(msg), 0);

  ClientInstance new_client = {ProtocolChecking, new_socket};
  client_sockets.push(new_client);
}

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

  ClientInstance client = pending_task.client;
  client.status = Finished;
  pending_task = {};

  disconnect_client(client);
}

int start_server(const char *addr, const char *port) {
  struct addrinfo hints, *res, *p;
  int server_socket;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  if (getaddrinfo(addr, port, &hints, &res) != 0) {
    std::cerr << "getaddrinfo error" << std::endl;
    return -1;
  }

  // Iterate
  for (p = res; p != NULL; p = p->ai_next) {
    if ((server_socket =
             socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      std::cerr << "socket error" << std::endl;
      continue;
    }

    if (bind(server_socket, p->ai_addr, p->ai_addrlen) == -1) {
      std::cerr << "Bind error" << std::endl;
      close(server_socket);
      continue;
    }

    break;
  }

  if (p == NULL) {
    std::cerr << "Failed to bind" << std::endl;
    return 1;
  }

  if (listen(server_socket, 10) == -1) {
    std::cerr << "Listen error" << std::endl;
    return 1;
  }

  printf("Server started at port %s, support %s\n", port,
         res->ai_family == AF_INET6 ? "ipv6" : "ipv4");
  freeaddrinfo(res);

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
        handle_connect(new_socket);
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
          } else if (client_socket.status == ProtocolChecking) {
            if (strcmp(buffer, "OK")) {
              std::thread t(send_task, client_socket);
              t.detach();
            } else {
              disconnect_client(client_socket);
            }
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
  char *addr = argv[1];
  char *port = argv[2];

  int status = start_server(addr, port);

  return status;
}
