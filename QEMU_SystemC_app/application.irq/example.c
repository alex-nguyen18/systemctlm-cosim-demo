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
#define FPGA_ABSIZE (2 * 1024 * 1024 * sizeof(INTYPE))
#define FPGA_CSIZE  (4 * 1024 * 1024 * sizeof(INTYPE))

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
    //ioctl(fd, SMUGGLE_ADDR + i, &result); // not a problem if it gets cut down since DRAM is in the low part of physical addr space
    //ioctl(fd, WRITE_CMD + 16 + 4 * i, &result);
    //printf("%d \n",result);
  }
}

void gemm(unsigned long M, unsigned long N, unsigned long K, INTYPE* A, INTYPE* B, OUTTYPE* C) {
  //////// Open FPGA as file
  int fd=open("/dev/fpga", O_RDWR); 
  
  //////// Write M, N, K
  unsigned long result;
  ioctl(fd, WRITE_CMD + 4 + 0, &M);
  ioctl(fd, WRITE_CMD + 4 + 4, &N);
  ioctl(fd, WRITE_CMD + 4 + 8, &K);

  //////// Init the Accel to expect START of data block transfer
  int start = 1;
  ioctl(fd, WRITE_CMD + 40, &start);
  // Wait for init to finish
  result = 0;
  do {
    ioctl(fd, READ_CMD + 40, &result); // check if finished
  } while (result == 0);
  printf("\nAccelerator initilized!\n");

  //////// Write A, B
  int total_bytes_array=M>N?M*K*2:K*N*2;         // Larger of A,B will determine number of writes
  int total_blocks=(total_bytes_array+(FPGA_ABSIZE-1))/FPGA_ABSIZE;  // Number of block writes
  int bytes_copied_a=0;                            // Tracking total bytes written
  int bytes_copied_b=0;
  int read_val=3;                                  // Tell accel what to read
  int a_bytes, b_bytes;
  for (int k=0; k<total_blocks; k++) {
    // Write A, B
    //a_bytes = min(FPGA_ABSIZE, M*K*2-bytes_copied_a);
    //b_bytes = min(FPGA_ABSIZE, K*N*2-bytes_copied_b);
    a_bytes = FPGA_ABSIZE<(M*K*2-bytes_copied_a)?FPGA_ABSIZE:(M*K*2-bytes_copied_a);
	 b_bytes = FPGA_ABSIZE<(M*N*2-bytes_copied_b)?FPGA_ABSIZE:(M*N*2-bytes_copied_b);
	 write(fd, A+(k*FPGA_ABSIZE), a_bytes);
    write(fd, B+(k*FPGA_ABSIZE), b_bytes);
    // Notify Accel to Read A or B
    // Write 1 -- read A
    // Write 2 -- read B
    // Write 3 -- read A,B
    read_val = 3;
    if (a_bytes == 0 && b_bytes != 0) {
      read_val = 2;
    }
    if (b_bytes == 0 && a_bytes != 0) {
      read_val = 1;
    }
    ioctl(fd, WRITE_CMD + 44, &read_val); // start 
    // Wait for accel finished notification
    do {
      ioctl(fd, READ_CMD + 44, &result); // check if finished
    } while (result == 0);
    bytes_copied_a += a_bytes;
    bytes_copied_b += b_bytes;
  }
  printf("\tIn %d blocks:\n\t\tWrote %d bytes in A\n\t\tWrote %d bytes in B\n", total_blocks, bytes_copied_a, bytes_copied_b);

  //////// Tell GEMM to Run and Wait for Finish
  result = 1;
  ioctl(fd, WRITE_CMD + 0, &result); // start
  do {
    ioctl(fd, READ_CMD + 0, &result); // check if finished
  } while (result == 0);
  
  //////// Read Result from DRAM block by block
  total_bytes_array=M*N*4;
  //total_blocks=total_bytes_array/FPGA_CSIZE;
  total_blocks=(total_bytes_array+(FPGA_CSIZE-1))/FPGA_CSIZE;  // Number of block writes  
  int bytes_copied_c=0;
  int c_bytes;
  result = 1;
  for (int k=0; k<total_blocks; k++) {
    c_bytes = FPGA_CSIZE<(M*N*4-bytes_copied_c)?FPGA_CSIZE:(M*N*4-bytes_copied_c);
	 // Request a Block
    ioctl(fd, WRITE_CMD + 48, &result);
	 // Wait for response
	 result = 0;
    do {
      ioctl(fd, READ_CMD + 48, &result); // check if finished
    } while (result == 0);
	 // Read
	 read(fd,C+(k*FPGA_CSIZE),c_bytes);
  }
  close(fd);  

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


  initialise(fd);

  gemm(m,n,k,A,B,C);
  //In the end, close the device driver
  close(fd);

  printf("C = %d %d %d %d\n",C[0], C[1], C[2], C[3]);
  fflush(stdout);

  return 0;
}

