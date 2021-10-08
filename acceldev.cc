#define SC_INCLUDE_DYNAMIC_PROCESSES

#include <inttypes.h>

#include "tlm_utils/simple_initiator_socket.h"
#include "tlm_utils/simple_target_socket.h"

using namespace sc_core;
using namespace std;

#include "acceldev.h"
#include <sys/types.h>
#include <time.h>

acceldev::acceldev(sc_module_name name)
	: sc_module(name), socket("socket"), master_socket("master_socket")
{
	socket.register_b_transport(this, &acceldev::b_transport);
	socket.register_transport_dbg(this, &acceldev::transport_dbg);
//	master_socket.register_invalidate_direct_mem_ptr(this, &acceldev::invalidate_direct_mem_ptr);

}

void acceldev::copy_from_dram(){
	//need exepcted addresses for arrays
	
	if(A != NULL){
		delete A;
	}
	if(B != NULL){
		delete B;
	}
	if(C != NULL){
		delete C;
	}

	A = new INTYPE[M*K];
	B = new INTYPE[K*N];
	C = new OUTTYPE[M*N];

	tlm::tlm_generic_payload trans;
	trans.set_command(tlm::TLM_READ_COMMAND);
	trans.set_streaming_width(4);
	trans.set_dmi_allowed(false);
	trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

	sc_time delay = sc_time(1, SC_US);

//	A
//	trans.set_address(NEED TO TALK TO BRENDAN);
	for (int i = 0; i < M*K; i++){	
		trans.set_data_ptr((unsigned char*)A+2*i);
//		trans.set_data_length(M*K*sizeof(INTYPE));
		trans.set_data_length(2);
		trans.set_address(aptr+2*i);
		master_socket->b_transport(trans, delay);
	}
	//wait for trans response?
//	B
	for (int i = 0; i < K*N; i++){	
		trans.set_data_ptr((unsigned char*)B+2*i);
//		trans.set_data_length(M*K*sizeof(INTYPE));
		trans.set_data_length(2);
		trans.set_address(bptr+2*i);
		master_socket->b_transport(trans, delay);
	}
//	trans.set_address(NEED TO TALK TO BRENDAN);
	//wait for trans response?
//	Reset C
}

void acceldev::gemm(){

    for (int i = 0; i < M; ++i) {
        for (int k = 0; k < K; ++k) {
            OUTTYPE A_PART = A[i * lda + k];
            for (int j = 0; j < N; ++j) {
                C[i*ldc + j] += (A_PART*B[k*ldb + j]);// >> SHAMT;
            }
        }
    }
    printf("A %d %d %d %d\n",A[0],A[1],A[2],A[3]);
    printf("B %d %d %d %d\n",B[0],B[1],B[2],B[3]);
    printf("C %d %d %d %d\n",C[0],C[1],C[2],C[3]);
}

void acceldev::copy_to_dram(){

	//copy C to DRAM
 	tlm::tlm_generic_payload trans;
	trans.set_command(tlm::TLM_WRITE_COMMAND);
	trans.set_data_length(4);
	trans.set_streaming_width(4);
	trans.set_dmi_allowed(false);
	trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
	
	sc_time delay = sc_time(1, SC_NS);

	for (int i = 0; i < M*N; i++){	
		trans.set_data_ptr((unsigned char*)C+4*i);
//		trans.set_data_length(M*K*sizeof(INTYPE));
		trans.set_data_length(4);
		trans.set_address(cptr+4*i);
		master_socket->b_transport(trans, delay);
	}

}

void acceldev::test_dma(){

	//copy C to DRAM

	int buf[2] = {0xdead,0xbeef};

	int buf_read[1] = {0x0};
	
 	unsigned char en_ptr[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

	tlm::tlm_generic_payload trans;
	trans.set_command(tlm::TLM_WRITE_COMMAND);
	trans.set_data_ptr((unsigned char*)buf);
	trans.set_data_length(8);
	trans.set_streaming_width(4);
	trans.set_byte_enable_ptr(en_ptr);
	trans.set_dmi_allowed(false);
	trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

	sc_time delay = sc_time(1, SC_US);

	trans.set_address(0x0);
   	master_socket->b_transport(trans,delay);

	if(trans.is_response_ok()){
		printf("Successfully wrote %x %x to 0x00\n", *buf, *(buf+1));
		fflush(stdout);
	}else{
		printf("test_dma err1\n");
		fflush(stdout);
	}

	trans.set_command(tlm::TLM_READ_COMMAND);	
	trans.set_data_length(4);

	trans.set_data_ptr((unsigned char*)buf_read);
	trans.set_address(0x4);
   	master_socket->b_transport(trans,delay);
	
	if(trans.is_response_ok()){
		printf("Successfully read %x from dma\n", *buf_read);
		fflush(stdout);
	}else{
		printf("test_dma err2\n");
		fflush(stdout);
	}
	wait(sc_time(1,SC_US));
	return;
}

void acceldev::b_transport(tlm::tlm_generic_payload& trans, sc_time& delay)
{
	tlm::tlm_command cmd = trans.get_command();
	sc_dt::uint64 addr = trans.get_address();
	unsigned char *data = trans.get_data_ptr();
	unsigned int len = trans.get_data_length();
	unsigned char *byt = trans.get_byte_enable_ptr();
	unsigned int wid = trans.get_streaming_width();


	if (byt != 0) {
		trans.set_response_status(tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE);
		return;
	}

	if (len > 4 || wid < len) {
		trans.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
		return;
	}
	trans.set_response_status(tlm::TLM_OK_RESPONSE);

	// Pretend this is slow!
	delay += sc_time(1, SC_US);

	if (trans.get_command() == tlm::TLM_READ_COMMAND) {
		sc_time now = sc_time_stamp() + delay;
		uint32_t v = 1;

		switch (addr) {
			case 0:
				v = gemm_done;
				break;
			default:
				break;
		}
		memcpy(data, &v, len);
		printf("Acceldev READ addr %x, data %x, len %d\n", addr, *(uint32_t*)data,len);
		fflush(stdout);
	} else if (cmd == tlm::TLM_WRITE_COMMAND) {
		printf("Acceldev WRITE addr %x, data %x, len %d\n", addr, *(uint32_t*)data,len);
		fflush(stdout);
		static sc_time old_ts = SC_ZERO_TIME, now, diff;	

		now = sc_time_stamp() + delay;
		diff = now - old_ts;
		switch (addr) {
			case 0:
				/*
				if (*(uint32_t *)data == 1){
					copy_from_dram();
				}else if (*(uint32_t *)data == 2){
					gemm();
					copy_to_dram();
				}else if (*(uint32_t *)data == 3){
					test_dma();
				}else{
					printf("That is not a valid HAL function!\n");
				}*/
				gemm_done = 0;
				copy_from_dram();
				gemm();
				copy_to_dram();	
				gemm_done = 1;
				break;
			case 0x4:
				M = *(uint32_t *)data;
				printf("M = %d\n",M);
				break;
			case 0x8:
				N = *(uint32_t *)data;
				ldb = N;
				ldc = N;
				printf("N = %d\n",N);
				break;
			case 0xc:
				K = *(uint32_t *)data;
				lda = K;
				printf("K = %d\n",K);
				break;
			case 0x10:
				aptr = *(uint32_t *)data;
				printf("aptr = %d\n",aptr);
				break;
			case 0x18:
				bptr = *(uint32_t *)data;
				printf("bptr = %d\n",bptr);
				break;
			case 0x20:
				cptr = *(uint32_t *)data;
				printf("cptr = %d\n",cptr);
				break;
			default:
				break;
		}
		old_ts = now;
	}

}

//Don't touch?
unsigned int acceldev::transport_dbg(tlm::tlm_generic_payload& trans)
{
	unsigned int len = trans.get_data_length();
	return len;
}


acceldev::~acceldev(){
	printf("DESTROY ACCELDEV?\n");
}
