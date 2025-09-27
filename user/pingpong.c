#include "kernel/types.h"
#include "kernel/stat.h"
#include "user.h"

int main(int argc, char *argv[]) {
  if (argc > 1) {
    fprintf(2, "Usage: pingpong");
    exit(1);
  }

  int c2f[2];
  int f2c[2];
  if (pipe(c2f) < 0 || pipe(f2c) < 0) {
    fprintf(2, "pingpong: pipe failed\n");
    exit(1);
  }

  if (fork() == 0) {
    close(f2c[1]);
    close(c2f[0]);
    int received_pid;
    read(f2c[0], &received_pid, sizeof(received_pid));
    printf("%d: received ping from pid %d\n", getpid(), received_pid);
    int my_pid = getpid();
    write(c2f[1], &my_pid, sizeof(my_pid));
  } else {
    close(f2c[0]);
    close(c2f[1]);
    int my_pid = getpid();
    write(f2c[1], &my_pid, sizeof(my_pid));
    int received_pid;
    read(c2f[0], &received_pid, sizeof(received_pid));
    printf("%d: received pong from pid %d\n", getpid(), received_pid);
  }

  exit(0);
}