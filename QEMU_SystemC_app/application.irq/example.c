#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <stdint.h>

#define READ_CMD  (0x0 << 31)
#define WRITE_CMD (0x1 << 31)

#define COMMAND_MASK 0x80000000
#define SMUGGLE_ADDR 0x01000000

#define INTYPE uint16_t
#define OUTTYPE uint32_t

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

void gemm(int fd, int m, int n, int k, INTYPE* A, INTYPE* B, OUTTYPE* C) {
  unsigned long result;
  ioctl(fd, WRITE_CMD + 4 + 0, &m);
  ioctl(fd, WRITE_CMD + 4 + 4, &n);
  ioctl(fd, WRITE_CMD + 4 + 8, &k);

  write(fd,A,m*k*sizeof(INTYPE));
  write(fd,B,k*n*sizeof(INTYPE));

  result = 1;
  ioctl(fd, WRITE_CMD + 0, &result); // start
  
  do {
    ioctl(fd, READ_CMD + 0, &result); // check if finished
  } while (result == 0);

  read(fd,C,m*n*sizeof(OUTTYPE));
}


int main(int argc, char * argv[]) 
{
  unsigned long val, result;
  struct sigaction action;
  int fd;

  
  int m = 2;
  int n = 2;
  int k = 2;
  INTYPE A[4] = {1,2,3,4};
  INTYPE B[4] = {5,6,7,8};
  OUTTYPE C[4] = {0,0,0,0};

  //Ensure proper usage
  if(argc > 2)
  {
    printf("Usage: %s [val]\n",argv[0]);
    return -1;
  }

  // open hardware device (driver)
  fd=open("/dev/fpga", O_RDWR);

  initialise(fd);

  gemm(fd,m,n,k,A,B,C);
  //In the end, close the device driver
  close(fd);

  printf("C = %d %d %d %d\n",*C, *(C+1), *(C+2), *(C+3));
  fflush(stdout);

  return 0;
}

