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

  /*
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
  */
  //////// Write M, N, K
  unsigned long result;
  ioctl(fd, WRITE_CMD + 0x10, &M);

  printf("We have written M!!\n");
  fflush(stdout);

  ioctl(fd, WRITE_CMD + 0x18, &N);

  printf("We have written N!!\n");
  fflush(stdout);

  ioctl(fd, WRITE_CMD + 0x20, &K);

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
    //printf("We are not finished with GEMM!!\n");
    //fflush(stdout);
  } while ((result & 2) == 0);
  
  printf("We have finihsed!\n");
  printf("result %x\n",result);
  fflush(stdout);

  if(read(fd, C, M*N*sizeof(OUTTYPE)) != M*N*sizeof(OUTTYPE)){
	printf("did not copy C correctly!\n");
	close(fd);
	exit(-1);
  }  

  printf("We have read from C!!\n");
  ioctl(fd, READ_CMD + 0, &result);
  printf("result %x\n",result);
  fflush(stdout);
}

int main(int argc, char * argv[]) 
{
  unsigned long val, result;
  unsigned long volatile gie, iie;
  struct sigaction action;
  time_t t;

  srand((unsigned) time(&t));

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

/*  // enable FPGA interrupts (global and IP)
  ioctl(fd, READ_CMD + 0x4, &gie);
  gie = gie | 0x00000001;
  ioctl(fd, WRITE_CMD + 0x4, &gie);

  iie = 0x1;
  ioctl(fd, WRITE_CMD + 0x8, &iie);
*/
  int m = 512;
  int n = 512;
  int k = 128;

  INTYPE *A = (INTYPE*)calloc(m*k,sizeof(INTYPE));
  INTYPE *B = (INTYPE*)calloc(k*n,sizeof(INTYPE));
  OUTTYPE *C = (OUTTYPE*)calloc(m*n,sizeof(OUTTYPE));
  OUTTYPE *C_golden = (OUTTYPE*)calloc(m*n,sizeof(OUTTYPE));
  memset(C_golden,0,m*n*sizeof(OUTTYPE));
//  INTYPE A[1024*1024] = {0};//= {1,2,3,4};
//  INTYPE B[1024*1024] = {0};//= {5,6,7,8};
//  OUTTYPE C[1024*1024] = {0};//= {0,0,0,0};

  //write random data into A, B
  //do mult in software
  //match?

  for (int i = 0; i < m*k; i++){
	//A[i] = i*sizeof(INTYPE);//
	A[i] = rand() % 512;
	//if (i < 10) printf ("A %d",A[i]);	
  }
  for (int i = 0; i < k*n; i++){
	//B[i] = i*sizeof(INTYPE);//
	B[i] = rand() % 512;
	//if (i < 10) printf ("B %d",B[i]);	
  }
  for (int i = 0; i < m; i++){
	for (int j = 0; j < k; ++j){
		OUTTYPE A_PART = A[i*k + j];
	    for (int k = 0; k < n; k++){
		C_golden[i*n+k] += (A_PART*B[j*n + k]);
	    }
	}
  }

  //Ensure proper usage
  if(argc > 2)
  {
    printf("Usage: %s [val]\n",argv[0]);
    return -1;
  }

  gemm(m,n,k,A,B,C);

  int err_found = 0;

  for (int i = 0; i < m; i++){
	for (int j = 0; j < n; j++){
	   if (C_golden[i*n+j] != C[i*n+j]){
		err_found = 1;
		printf("row %d col %d Golden = %x test = %x\n",i,j,C_golden[i*n+j],C[i*n+j]);
	   }
	}
  }
 
  if (!err_found) printf("We matched golden!! \n");

//  printf("C = %d %d %d %d\n",C[0], C[1], C[2], C[3]);

//  A[0] = 3;

//  gemm(m,n,k,A,B,C); 
//  printf("C = %d %d %d %d\n",C[0], C[1], C[2], C[3]); 
  
  //In the end, close the device driver
  close(fd);

  //printf("C = %d %d %d %d\n",C[0], C[1], C[2], C[3]);
  fflush(stdout);

  free(A);
  free(B);
  free(C);

  return 0;
}

