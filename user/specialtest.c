#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "kernel/fcntl.h"
#include "kernel/spinlock.h"
#include "kernel/sleeplock.h"
#include "kernel/fs.h"
#include "kernel/file.h"
#include "user/user.h"

void testnull(void);
void testzero(void);
void testuptime(void);
void teststatus(void);
void testrandom(void);

#define fail(msg) do {printf("FAILURE:" msg "\n"); failed = 1; goto done;} while (0);

static int failed = 0;

int
main(int argc, char *argv[])
{
  testnull();
  testzero();
  testuptime();
  testrandom();
  exit(failed);
}

void
testnull(void)
{
  int fd, r;
  static char buf[32] = { 'a', 'b', 'c', 0 };

  printf("\nSTART: test /dev/null\n");

  fd = open("/dev/null", O_RDWR);
  if (fd < 0)
    fail("could not open /dev/null\n");

  printf("reading from /dev/null..\n");
  r = read(fd, buf, sizeof(buf));
  if (r != 0)
    fail("read /dev/null did not return EOF\n");

  printf("writing to /dev/null..\n");
  r = write(fd, buf, sizeof(buf));
  if (r != sizeof(buf))
    fail("could not write to /dev/null\n");

  printf("reading from /dev/null again..\n");
  r = read(fd, buf, sizeof(buf));
  if (r != 0)
    fail("read /dev/null did not return EOF after write");

  if (buf[0] != 'a')
    fail("/dev/null read non-zero amount of bytes\n");

  printf("SUCCESS: test /dev/null\n");
done:
  close(fd);
}

void
testzero(void)
{
  int fd, r;
  char buf[8] = {'a','b','c','d','e','f','g','h'};

  printf("\nSTART: test /dev/zero\n");

  fd = open("/dev/zero", O_RDWR);
  if (fd < 0)
    fail("could not open /dev/zero");

  printf("writing to /dev/zero..\n");
  r = write(fd, buf, sizeof(buf));
  if (r != sizeof(buf))
    fail("could not write to /dev/zero");

  printf("reading from /dev/zero..\n");
  r = read(fd, buf, sizeof(buf));
  if (r != 8)
    fail("could not read from /dev/zero");

  for(int i = 0; i < sizeof(buf); i++) {
    if (buf[i])
      fail("reading from /dev/zero produced non-zero bytes");
  }

  printf("SUCCESS: test /dev/zero\n");
done:
  close(fd);
}

void
testuptime(void)
{
  int fd, r, first, second;
  char buf[16] = { 0 };

  printf("\nSTART: test /dev/uptime\n");

  fd = open("/dev/uptime", O_RDONLY);
  if (fd < 0)
    fail("could not open /dev/uptime");

  printf("Reading from /dev/uptime..\n");
  r = read(fd, buf, sizeof(buf));
  if (r <= 0)
    fail("could not read /dev/uptime");
  first = atoi(buf);
  memset(buf, 0, sizeof(buf));

  sleep(2);

  close(fd);
  fd = open("/dev/uptime", O_RDONLY);
  printf("Reading from /dev/uptime again..\n");
  r = read(fd, buf, sizeof(buf));
  if (r <= 0)
    fail("could not read /dev/uptime");
  second = atoi(buf);

  if(first <= 0 || second <= 0 || second <= first || second - first > 50) {
    printf("expected two positive, monotonically increasing integers near each other\n");
    printf("         got: %d %d\n", first, second);
    failed = 1;
    goto done;
  }

  printf("SUCCESS: test /dev/uptime\n");
done:
  close(fd);
}

void
testrandom(void)
{
  int r = 0, fd1 = -1, fd2 = -1;
  char buf1[8], buf2[8], buf3[8], buf4[8];

  printf("\nSTART: test /dev/random\n");

  printf("Opening /dev/random..\n");
  fd1 = open("/dev/random", O_RDONLY);
  if(fd1 < 0)
    fail("Failed to open /dev/random");
  fd2 = open("/dev/random", O_RDONLY);
  if (fd2 < 0)
    fail("Failed to open /dev/random");


  printf("reading from /dev/random four times..\n");
  r = read(fd1, buf1, sizeof(buf1));
  if (r != sizeof(buf1)) fail("Failed to read /dev/random");
  r = read(fd1, buf2, sizeof(buf2));
  if (r != sizeof(buf2)) fail("Failed to read /dev/random");
  r = read(fd2, buf3, sizeof(buf3));
  if (r != sizeof(buf3)) fail("Failed to read /dev/random");
  r = read(fd2, buf4, sizeof(buf4));
  if (r != sizeof(buf4)) fail("Failed to read /dev/random");

  r = memcmp(buf1, buf2, 8) &&
      memcmp(buf1, buf3, 8) &&
      memcmp(buf1, buf4, 8) &&
      memcmp(buf2, buf3, 8) &&
      memcmp(buf2, buf4, 8) &&
      memcmp(buf3, buf4, 8);

  if(!r)
    fail("Reads of /dev/random should return random bytes..");

  printf("SUCCESS: test /dev/random\n");
done:
  close(fd1);
  close(fd2);
}