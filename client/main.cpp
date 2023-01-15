#include "../common/log.h"
#include "tcp_client.hpp"

#define _XOPEN_SOURCE 600  // NOLINT
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void execNewTerm(int fds) {
  winsize winSize{.ws_row = 24, .ws_col = 80};
  ioctl(fds, TIOCSWINSZ, &winSize);

  // The slave side of the PTY becomes the standard input and outputs of the
  // child process
  close(0);  // Close standard input (current terminal)
  close(1);  // Close standard output (current terminal)
  close(2);  // Close standard error (current terminal)

  dup(fds);  // PTY becomes standard input (0)
  dup(fds);  // PTY becomes standard output (1)
  dup(fds);  // PTY becomes standard error (2)

  // Now the original file descriptor is useless
  close(fds);

  // Make the current process a new session leader
  setsid();

  // As the child is a session leader, set the controlling terminal to be the
  // slave side of the PTY (Mandatory for programs like the shell to make them
  // manage correctly their outputs)
  ioctl(0, TIOCSCTTY, 1);

  // Execution of the program
  int rc = execvp("bash", nullptr);
  if (rc != 0) {
    LOGE("execvp error: %d, %s", errno, strerror(errno));
  }
}

int main(int argc, char *argv[]) {
  int fdm = posix_openpt(O_RDWR);
  if (fdm < 0) {
    LOGE("posix_openpt error: %d, %s", errno, strerror(errno));
    return 1;
  }

  if (grantpt(fdm) != 0) {
    LOGE("grantpt error: %d, %s", errno, strerror(errno));
    return 1;
  }

  if (unlockpt(fdm) != 0) {
    LOGE("unlockpt error: %d, %s", errno, strerror(errno));
    return 1;
  }

  // Open the slave side ot the PTY
  int fds = open(ptsname(fdm), O_RDWR);

  asio::io_context io_context;
  asio_net::tcp_client tcp_client(io_context);

  asio::posix::stream_descriptor descriptor(io_context);
  descriptor.assign(fdm);

  std::function<void()> readFromFdm;
  std::string buffer;
  buffer.resize(1024);
  readFromFdm = [&] {
    descriptor.async_read_some(asio::buffer(buffer), [&](const std::error_code &ec, std::size_t length) {
      if (ec) {
        LOGE("descriptor: %s", ec.message().c_str());
        io_context.stop();
        return;
      }
      tcp_client.send(std::string(buffer.data(), length));
      readFromFdm();
    });
  };
  readFromFdm();

  tcp_client.on_open = [fds, &io_context] {
    LOGD("on_open");
    if (!fork()) {
      io_context.stop();
      execNewTerm(fds);
    }
  };
  tcp_client.on_close = [] {
    LOGD("on_close");
  };
  tcp_client.on_open_failed = [](std::error_code ec) {
    LOGD("on_open_failed: %s", ec.message().c_str());
  };
  tcp_client.on_data = [fdm](const std::string &data) {
    write(fdm, data.data(), data.size());
  };
  tcp_client.open("localhost", 6666);
  LOGD("try open...");

  asio::io_context::work work(io_context);
  io_context.run();

  return 0;
}
