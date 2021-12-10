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

#define READ_CMD  (0x0U << 31)
#define WRITE_CMD (0x1U << 31)

#define COMMAND_MASK 0x80000000
#define SMUGGLE_ADDR 0x01000000

#define INTYPE uint16_t
#define OUTTYPE uint32_t
#define FPGA_ABSIZE (2 * 1024 * 1024 * sizeof(INTYPE))
#define FPGA_CSIZE  (4 * 1024 * 1024 * sizeof(INTYPE))

int det_int = 0;
int fd; 

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
    //ioctl(fd, SMUGGLE_ADDR + i, &result); // not a problem if it gets cut down since DRAM is in the low part of physical addr space
    //ioctl(fd, WRITE_CMD + 16 + 4 * i, &result);
    //printf("%d \n",result);
  }
}

void gemm(unsigned long M, unsigned long N, unsigned long K, INTYPE* A, INTYPE* B, OUTTYPE* C) {


  //////// Write M, N, K
  unsigned long result;
  ioctl(fd, WRITE_CMD + 52 + 0, &M);

  printf("We have written M!!\n");
  fflush(stdout);

  ioctl(fd, WRITE_CMD + 52 + 8, &N);

  printf("We have written N!!\n");
  fflush(stdout);

  ioctl(fd, WRITE_CMD + 52 + 16, &K);

  printf("We have written K!!\n");
  fflush(stdout);
  int written = write(fd, A, M*K*sizeof(INTYPE));
  if(written != M*K*sizeof(INTYPE)){
	printf("did not copy A correctly! expected %d only %d \n", written, M*K*sizeof(INTYPE));
	close(fd);
	exit(-1);
  }

  printf("We have written to A!!\n");
  fflush(stdout);
  written = write(fd, B, K*N*sizeof(INTYPE));
  if(written != K*N*sizeof(INTYPE)){
	printf("did not copy B correctly! expected %d only %d \n", written, K*N*sizeof(INTYPE));
	close(fd);
	exit(-1);
  }

  printf("We have written to B!!\n");
  fflush(stdout);

  //////// Tell GEMM to Run and Wait for Finish
  result = 1;
  ioctl(fd, WRITE_CMD + 0, &result); // start

  printf("We have started!!\n");
  fflush(stdout);
  do {
    ioctl(fd, READ_CMD + 0, &result); // check if finished
    printf("We are not finished with GEMM!!\n");
    fflush(stdout);
  } while (result == 0);
  
  printf("We have finihsed!\n");
  fflush(stdout);

  if(read(fd, C, M*N*sizeof(OUTTYPE)) != M*N*sizeof(OUTTYPE)){
	printf("did not copy C correctly!\n");
	close(fd);
	exit(-1);
  }  

  printf("We have read from C!!\n");
  fflush(stdout);
}

int main(int argc, char * argv[]) 
{
  unsigned long val, result;
  unsigned long volatile gie, iie;
  struct sigaction action;

  printf("We gonna open fpga!!\n");
  fflush(stdout);

   //////// Open FPGA as file
  fd=open("/dev/fpga", O_RDWR); 
  if(fd < 0)
  {

      printf("Unable to open /dev/fpga.  Ensure it exists!\n");
      return -1;
  }
  fcntl(fd, F_SETOWN, getpid());
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL)|O_ASYNC);

  if (fd == -1){
	printf("could not open /dev/fpga!!\n");
	exit(-1);
  } 

  // install signal handler
  sigemptyset(&action.sa_mask);
  sigaddset(&action.sa_mask, SIGIO);

  action.sa_handler = sighandler;
  action.sa_flags=0;

  sigaction(SIGIO, &action, NULL);

  printf("We have opened /dev/fpga!!\n");
  fflush(stdout);

  initialise(fd);

  // enable FPGA interrupts (global and IP)
  ioctl(fd, READ_CMD + 0x4, &gie);
  gie = gie | 0x00000001;
  ioctl(fd, WRITE_CMD + 0x4, &gie);

  iie = 0x1;
  ioctl(fd, WRITE_CMD + 0x8, &iie);

  int m = 32;
  int n = 32;
  int k = 32;
  INTYPE A[32*32] = {0};//= {1,2,3,4};
  INTYPE B[32*32] = {0};//= {5,6,7,8};
  OUTTYPE C[32*32] = {0};//= {0,0,0,0};

  A[0] = 1;
  B[0] = 1;
  B[1] = 2;

  //Ensure proper usage
  if(argc > 2)
  {
    printf("Usage: %s [val]\n",argv[0]);
    return -1;
  }

  gemm(m,n,k,A,B,C);
  //In the end, close the device driver
  close(fd);

  printf("C = %d %d %d %d\n",C[0], C[1], C[2], C[3]);
  fflush(stdout);

  return 0;
}

