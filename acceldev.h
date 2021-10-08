#include <inttypes.h>

#define INTYPE int16_t
#define OUTTYPE int32_t

class acceldev
: public sc_core::sc_module
{
public:	
  //model local regs
  int TA, TB, M, N, K, lda, ldb, ldc;
  float ALPHA, BETA;

  //model bram
//  OUTTYPE bram_c[128][512][2];
//  INTYPE bram_a[64][512][4]; //block #, row #, word position in row (using 64b or 72 possible
	OUTTYPE* C;
	INTYPE* A;
	INTYPE* B;

	int aptr, bptr, cptr;

	tlm_utils::simple_target_socket<acceldev> socket;
	tlm_utils::simple_initiator_socket<acceldev> master_socket;	

	sc_out<bool> irq;
	virtual ~acceldev();
	acceldev(sc_core::sc_module_name name);
	//void start_of_simulation();
	virtual void copy_from_dram();	
	virtual void copy_to_dram();
	virtual void gemm();
	virtual void test_dma();
	
	virtual void b_transport(tlm::tlm_generic_payload& trans,
					sc_time& delay);
	virtual unsigned int transport_dbg(tlm::tlm_generic_payload& trans);
	//virtual void invalidate_direct_mem_ptr(sc_dt::uint64 start, sc_dt::uint64 end); 
};
