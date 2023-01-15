#define _XOPEN_SOURCE 600  // NOLINT
#define _DARWIN_C_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define __USE_BSD
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/termios.h>

#include "log.h"

int main(int ac, char *av[]) {
  int fdm, fds;
  int rc;
  char input[150];

  // Check arguments
  if (ac <= 1) {
    fprintf(stderr, "Usage: %s program_name [parameters]\n", av[0]);
    exit(1);
  }

  fdm = posix_openpt(O_RDWR);
  if (fdm < 0) {
    fprintf(stderr, "Error %d on posix_openpt()\n", errno);
    return 1;
  }

  rc = grantpt(fdm);
  if (rc != 0) {
    fprintf(stderr, "Error %d on grantpt()\n", errno);
    return 1;
  }

  rc = unlockpt(fdm);
  if (rc != 0) {
    fprintf(stderr, "Error %d on unlockpt()\n", errno);
    return 1;
  }

  // Open the slave side ot the PTY
  fds = open(ptsname(fdm), O_RDWR);

  // Create the child process
  if (fork()) {
    int old = open("/dev/tty", O_RDWR);  // 打开当前控制终端
    ioctl(old, TIOCNOTTY);               // 放弃当前控制终端

    struct termios slave_orig_term_settings;  // Saved terminal settings   // NOLINT
    struct termios new_term_settings;         // Current terminal settings // NOLINT

    if (tcgetattr(STDOUT_FILENO, &slave_orig_term_settings) != 0) {
      LOGE("tcgetattr error: %d, %s", errno, strerror(errno));
    }

    // Set RAW mode on slave side of PTY
    new_term_settings = slave_orig_term_settings;
    cfmakeraw(&new_term_settings);
    tcsetattr(STDOUT_FILENO, TCSANOW, &new_term_settings);

    setsid();                // 子进程成为会话组长
    perror("setsid");        // 显示setsid是否成功
    ioctl(0, TIOCSCTTY, 0);  // 这时可以设置新的控制终端了，设置控制终端为stdin

    fd_set fd_in;

    // FATHER

    // Close the slave side of the PTY
    close(fds);

    while (1) {
      // Wait for data from standard input and master side of PTY
      FD_ZERO(&fd_in);
      FD_SET(0, &fd_in);
      FD_SET(fdm, &fd_in);

      rc = select(fdm + 1, &fd_in, NULL, NULL, NULL);
      switch (rc) {
        case -1:
          fprintf(stderr, "Error %d on select()\n", errno);
          exit(1);

        default: {
          // If data on standard input
          if (FD_ISSET(0, &fd_in)) {
            rc = read(0, input, sizeof(input));
            if (rc > 0) {
              // Send data on the master side of PTY
              write(fdm, input, rc);
            } else {
              if (rc < 0) {
                fprintf(stderr, "Error %d on read standard input\n", errno);
                exit(1);
              }
            }
          }

          // If data on master side of PTY
          if (FD_ISSET(fdm, &fd_in)) {
            rc = read(fdm, input, sizeof(input));
            if (rc > 0) {
              // Send data on standard output
              write(1, input, rc);
            } else {
              if (rc < 0) {
                fprintf(stderr, "Error %d on read master PTY\n", errno);
                exit(1);
              }
            }
          }
        }
      }  // End switch
    }    // End while
  } else {
    // CHILD
    winsize winSize{.ws_row = 24, .ws_col = 80};
    ioctl(fds, TIOCSWINSZ, &winSize);

    // Close the master side of the PTY
    close(fdm);

    // The slave side of the PTY becomes the standard input and outputs of the child process
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

    // As the child is a session leader, set the controlling terminal to be the slave side of the PTY
    // (Mandatory for programs like the shell to make them manage correctly their outputs)
    ioctl(0, TIOCSCTTY, 1);

    // Execution of the program
    {
      char **child_av;
      int i;

      // Build the command line
      child_av = (char **)malloc(ac * sizeof(char *));
      for (i = 1; i < ac; i++) {
        child_av[i - 1] = strdup(av[i]);
      }
      child_av[i - 1] = NULL;
      rc = execvp(child_av[0], child_av);
    }

    // if Error...
    return 1;
  }

  return 0;
}  // main
