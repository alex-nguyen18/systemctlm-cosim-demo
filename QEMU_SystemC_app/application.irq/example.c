#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#define READ_CMD  (0x0 << 31)
#define WRITE_CMD (0x1 << 31)

#define COMMAND_MASK 0x80000000
#define SMUGGLE_ADDR 0x01000000

int det_int = 0;

// signal handler for receiving events from hardware driver
void sighandler(int signo)
{
  if(signo==SIGIO)
    {
      det_int++;
      printf("\nInterrupt detected\n");
    }
  return;
}

void initialise(int fd) {
  unsigned long result;
  for (int i = 0; i < 3; ++i) {
    ioctl(fd, SMUGGLE_ADDR + i, &result); // not a problem if it gets cut down since DRAM is in the low part of physical addr space
    ioctl(fd, WRITE_CMD + 16 + 4 * i, &result);
  }
}

void gemm(int fd, int m, int n, int k) {
  unsigned long result;
  ioctl(fd, WRITE_CMD + 4 + 0, &m);
  ioctl(fd, WRITE_CMD + 4 + 4, &n);
  ioctl(fd, WRITE_CMD + 4 + 8, &k);

  result = 1;
  ioctl(fd, WRITE_CMD + 0, &result); // start
  
  do {
    ioctl(fd, READ_CMD + 0, &result); // start
  } while (result == 0);
}


int main(int argc, char * argv[]) 
{
  unsigned long val, result;
  struct sigaction action;
  int fd;

  //Ensure proper usage
  if(argc > 2)
  {
    printf("Usage: %s [val]\n",argv[0]);
    return -1;
  }

  // install signal handler
  sigemptyset(&action.sa_mask);
  sigaddset(&action.sa_mask, SIGIO);

  action.sa_handler = sighandler;
  action.sa_flags=0;

  sigaction(SIGIO, &action, NULL);

  // open hardware device (driver)
  fd=open("/dev/fpga", O_RDWR);
  if(fd < 0)
  {

      printf("Unable to open /dev/fpga.  Ensure it exists!\n");
      return -1;
  }
  fcntl(fd, F_SETOWN, getpid());
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL)|O_ASYNC);

  if(argc > 1) {
    //Assign val
    val = atol(argv[1]);

    //Write to addr0
    ioctl(fd, WRITE_CMD + 0, &val);

  } else {

    //Read hardware 
    ioctl(fd, READ_CMD + 0, &result);

    printf("The SystemC time is %lu ns\n", result);

    ioctl(fd, READ_CMD + 4, &result);

    printf("The SystemC clock is %lu\n", result);
  }

  // Read interrupt
  ioctl(fd, READ_CMD + 3, &result);
  printf("Interrupt is %lu\n", result);

  // Trigger interrupt
  val = 1;
  ioctl(fd, WRITE_CMD + 3, &val);

  //Wait for interrupt
  while(!det_int) continue;

  printf("Interrupt received\n");

  //In the end, close the device driver
  close(fd);

  return 0;
}

