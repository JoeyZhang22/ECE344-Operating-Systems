#include "thread.h"
#include "test_thread.h"

int
main(int argc, char **argv)
{
	//************************************** orginal command needed 
	thread_init();
	test_basic();
	return 0;
	//***************************************

	//modified-> should delete after testing
	//thread_init();
	
}
