void
run_server()
{
  running.store(true);

  std::thread server_thread(server_thread_body);

  for (uint32_t iteration = 0; running.load(); iteration++) {
    if (is_start_pressed()) {
      running.store(false);
      printf("User pressed start button, exiting driver\n");
      break;
    }
  }

  // TODO: gracefully shutdown server thread
  //   printf("Awaiting server thread to join\n");
  //   server_thread.join();
}


class Client {
public:
  Client(sockaddr_in client, int client_socket)
    : _client(client),
      _client_socket(client_socket)
  {
  }

  Client(const Client &)            = delete;
  Client &operator=(const Client &) = delete;
  Client(Client &&other)
  {
    _client              = other._client;
    _client_socket       = other._client_socket;
    other._client_socket = -1;
  }

  ~Client()
  {
    if (_client_socket >= 0) {
      close(_client_socket);
    }
  }

  int send(std::span<uint8_t> data)
  {
    int bytes_written = ::send(_client_socket, data.data(), data.size(), 0);
    if (bytes_written <= 0) {
      printf("Failed to write to socket %d\n", _client_socket);
    }
    return bytes_written;
  }

  int recv(std::span<uint8_t> data)
  {
    int bytes_read = ::recv(_client_socket, data.data(), data.size(), 0);
    if (bytes_read <= 0) {
      printf("Socket %d closed\n", _client_socket);
    }
    return bytes_read;
  }

  struct address_t {
    uint8_t a, b, c, d;
  };

  address_t address() const
  {
    address_t result;
    memcpy(&result, &_client.sin_addr.s_addr, sizeof(result));
    return result;
  }

  int socket() const
  {
    return _client_socket;
  }

private:
  sockaddr_in _client;
  int _client_socket;
};

void
process_command(std::span<uint8_t> data)
{
  printf("Received command %s\n", data.data());
  if (strcmp((char *)data.data(), "exit") == 0) {
    running.store(false);
    return;
  }

  //   printf("Received command: ");
  //   for (uint8_t byte : data) {
  //     printf("%02x ", byte);
  //   }
  //   printf("\n");
}

void
socket_thread(Client client)
{
  printf("Entered socket thread for socket %d ip ", client.socket());

  const auto address = client.address();
  printf("%d.%d.%d.%d\n", address.a, address.b, address.c, address.d);

  int bytes_read;

  std::array<uint8_t, 1024> read_buffer;
  while (true) {

    if ((bytes_read = client.recv(read_buffer)) <= 0) {
      printf("Failed to read from socket %d, aborting\n", client.socket());
      break;
    }

    process_command(std::span(read_buffer.data(), bytes_read));

    // // Echo stuff back
    // read_buffer[bytes_read] = 0;
    // if (int bytes_written = client.send(std::span<uint8_t>(&read_buffer[0],
    // bytes_read));
    //     bytes_written <= 0) {
    //   printf("Failed to write to socket %d, aborting\n", client.socket());
    //   break;
    // }
  }

  printf("Exiting socket thread for socket %d\n", client.socket());
}

void
server_thread_body()
{
  int listenfd;
  struct sockaddr_in saddr;
  fd_set readset;
  fd_set writeset;
  int i, maxfdp1;

  listenfd = socket(AF_INET, SOCK_STREAM, 0);

  if (listenfd < 0) {
    printf("httpd: socket create failed\n");
    return;
  }

  memset(&saddr, 0, sizeof(saddr));
  saddr.sin_family      = AF_INET;
  saddr.sin_addr.s_addr = htonl(INADDR_ANY);
  saddr.sin_port        = htons(2343);

  if (bind(listenfd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
    printf("httpd: bind failed\n");
    close(listenfd);
    return;
  }

  if (listen(listenfd, 10) < 0) {
    printf("httpd: listen failed\n");
    close(listenfd);
    return;
  }

  printf("httpd: listening for connections on socket %d\n", listenfd);

  for (; running.load();) {
    maxfdp1 = listenfd + 1;

    FD_ZERO(&readset);
    FD_ZERO(&writeset);
    FD_SET(listenfd, &readset);

    i = select(maxfdp1, &readset, &writeset, 0, 0);

    if (i == 0)
      continue;

    // Check for new incoming connections
    if (FD_ISSET(listenfd, &readset)) {
      int client_socket;
      struct sockaddr_in client;
      socklen_t client_size;

      client_size   = sizeof(client);
      client_socket = accept(listenfd, (struct sockaddr *)&client, &client_size);

      printf("connect from %08lx, port %d, socket %d\n",
             client.sin_addr.s_addr,
             client.sin_port,
             client_socket);

      if (client_socket < 0) {
        // Nope, you dead
      } else {
        socket_thread(Client(client, client_socket));
        // TODO : launch this as another thread etc.
      }
    }
  }

  printf("Exiting server thread\n");
}