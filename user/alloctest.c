#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "kernel/fcntl.h"
#include "kernel/memlayout.h"
#include "user/user.h"

enum { NCHILD = 50, NFD = 10};

void
test0() {
  int i, j;
  int fd;

  printf("filetest: start\n");

  if(NCHILD*NFD < NFILE) {
    printf("test setup is wrong\n");
    exit(1);
  }

  for (i = 0; i < NCHILD; i++) {
    int pid = fork();
    if(pid < 0){
      printf("fork failed\n");
      exit(-1);
    }
    if(pid == 0){
      for(j = 0; j < NFD; j++) {
        if ((fd = open("README", O_RDONLY)) < 0) {
          // the open() failed; exit with -1
          printf("open failed\n");
          exit(-1);
        }
      }
      sleep(10);
      exit(0);  // no errors; exit with 0.
    }
  }

  for(int i = 0; i < NCHILD; i++){
    int xstatus;
    wait(&xstatus);
    if(xstatus == -1) {
       printf("filetest: FAILED\n");
       exit(-1);
    }
  }

  printf("filetest: OK\n");
}

void test1()
{
  int pid, xstatus, n0, n;

  printf("memtest: start\n");

  n0 = nfree();

  pid = fork();
  if(pid < 0){
    printf("fork failed");
    exit(1);
  }

  if(pid == 0){
    int i, fd;

    n0 = nfree();
    for(i = 0; i < NFD; i++) {
      if ((fd = open("README", O_RDONLY)) < 0) {
        // the open() failed; exit with -1
        printf("open failed\n");
        exit(-1);
      }
    }
    n = n0 - nfree();
    // n should be 0 but we're okay with 1
    if(n != 0 && n != 1){
      printf("expected to allocate at most one page, got %d\n", n);
      exit(-1);
    }
    exit(0);
  }

  wait(&xstatus);
  if(xstatus == -1)
    goto failed;

  n = n0 - nfree();
  if(n){
    printf("expected to free all the pages, got %d\n", n);
    goto failed;
  }
  printf("memtest: OK\n");
  return;

failed:
  printf("memtest: FAILED\n");
}

int
main(int argc, char *argv[])
{
  test0();
  test1();
  exit(0);
}
