#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cstdio>

#include "log.h"
#include "tcp_server.hpp"

int main(int argc, char* argv[]) {
  int old = open("/dev/tty", O_RDWR);  // 打开当前控制终端
  ioctl(old, TIOCNOTTY);               // 放弃当前控制终端

  // 根据"man 2 setsid"的说明
  // 调用setsid的进程不能是进程组组长（从bash中运行的命令是组长），故fork出一个子进程，让组长结束，子进程脱离进程组成为新的会话组长
  int pid = fork();
  if (pid != 0) {
    setsid();                // 子进程成为会话组长
    perror("setsid");        // 显示setsid是否成功
    ioctl(0, TIOCSCTTY, 0);  // 这时可以设置新的控制终端了，设置控制终端为stdin

    asio::io_context io_context;

    asio::posix::stream_descriptor descriptor(io_context);
    descriptor.assign(STDIN_FILENO);

    asio_net::tcp_server tcp_server(io_context, 6666);

    std::weak_ptr<asio_net::tcp_session> tcp_session;
    tcp_server.on_session = [&tcp_session](const std::weak_ptr<asio_net::tcp_session>& ws) {
      if (!tcp_session.expired()) {
        LOGD("tcp_session already have, close it");
        ws.lock()->close();
        return;
      }
      LOGD("on_session");

      tcp_session = ws;
      auto session = ws.lock();
      session->on_close = [] {
        LOGD("on_close");
      };
      session->on_data = [ws](const std::string& data) {
        write(STDOUT_FILENO, data.data(), data.size());
      };
    };

    std::function<void()> readFromFdm;
    std::string buffer;
    buffer.resize(1024);
    readFromFdm = [&] {
      descriptor.async_read_some(asio::buffer(buffer), [&](const std::error_code& ec, std::size_t length) {
        auto ts = tcp_session.lock();
        if (!ec && ts) {
          readFromFdm();
          ts->send(std::string(buffer.data(), length));
        }
      });
    };
    readFromFdm();

    tcp_server.start(true);
  }
  return 0;
}
