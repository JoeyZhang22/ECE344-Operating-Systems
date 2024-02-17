#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>
#include "thread.h"
#include "interrupt.h"
#include "stdbool.h"

//testing


//GLOBAL VARIABLES AND DATA STRUCTURES ===============================================================================================================================
//states of the threads
typedef enum {
	READY = 0, //Lab 2 -->put into queue
	RUNNING = 1, //Lab 2 --> currently running thread state
	EXITED = -1, //Lab 2 --> state of thread that has exited 
	KILLED = -2, //Lab 2 --> threads in this state should call exit when it runs again
	UNINITIALIZED = -3, //Lab 2 --> when a thread is not being used or set up it is uninitialized
	SLEEP = 2 //used for lab 3
} thread_states;

//Node struct for ready queue
struct thread_node{
	Tid tnumber;
	struct thread_node* next;
};

/* This is the Ready Queue --> elements in this queue are all in state ready to run*/
typedef struct ready_queue{ // follow first in first out principle
	struct thread_node* head; // the head of the queue where dequeing happens
	struct thread_node* tail; //tail of queue --> where enqueing happens
} ready_queue;

/* This is the thread control block */
struct thread {
	thread_states current_state; //stores five states: Ready, running, sleep, Exited, Killed, Unitialized
	ucontext_t current_thread_context;
	Tid thread_id; // integer number form 0 to THREAd_MAX_THREADS -1
	void* stack_ptr; //void type pointer for Stack pointer --> use for memory deallocation and allocation
	//wait queue? 
	struct wait_queue* wq;

	//thread is waiting on specific thread
	int waiting_on;

	//variable for tracking when a thread is woken up already  from queue?
	int exit_code; //if it is -1, none has woken up, if it is 0, then one thread has already woken up from wait queue;
	int exit_code2; //saves previous exit_code even after thread exits
};

/* This is the wait queue structure */
struct wait_queue {
	/* ... Fill this in Lab 3 ... */
	struct thread_node* head;
	struct thread_node* tail;
};


//previously: ready_queue r_q;
//changed to: 
struct wait_queue r_q; //for keeping consistent with new wait_queue implementation and function usage
static volatile Tid current_running_thread; //volatile current running thread id 
static struct thread array_of_threads [THREAD_MAX_THREADS]; //an array of all available threads--> allow of ease of access
static volatile int nums_existing_threads; // number of all threads created

//HELPER FUNCTIONS ================================================================================================================================================
void thread_stub(void (*thread_main)(void *), void *arg);
//Thread state structure with Number code for states

//*Helper Function: Initializes Queue
void set_up_r_q(struct wait_queue* r_q){
	r_q->tail=NULL;
	r_q->head=NULL;
}

//*Helper Function: Enqueues element -> insert element to end of queue
void enqueue (Tid id, struct wait_queue* r_q){
	int enabled = interrupts_set(0);
	if(r_q->head==NULL){ //case where this is the first element to be inserted
		struct thread_node* new_node = (struct thread_node *)malloc(sizeof(struct thread_node));
		new_node->tnumber = id;
		new_node->next = NULL;
		r_q->head=new_node;
		r_q->tail=new_node;
	}
	else { //otherwise
		struct thread_node* new_node = (struct thread_node *)malloc(sizeof(struct thread_node));
		new_node->tnumber = id;
		new_node->next = NULL;
		r_q->tail->next = new_node; 
		//update tial
		r_q->tail = new_node;
	}
	interrupts_set(enabled);
}

//*Helper Function: Dequeue element -> find element and dequeue
Tid dequeue_element(Tid id, struct wait_queue* r_q){
	int enabled = interrupts_set(0);
	//case where queue is empty
	if(r_q->head==NULL){
		//return number
		interrupts_set(enabled);
		return THREAD_INVALID; //queue is empty now so nothing could be ran
	}

	//in case of element found
	struct thread_node* ptr1;
	struct thread_node* ptr2;
	ptr2=r_q->head;
	ptr1=r_q->head;
	
	//case where head is the element
	if(ptr2->tnumber == id){
		r_q->head=ptr2->next;
		free(ptr2);

		//reset head and tail pointer if this is the only element in the queue
		if(r_q->head==NULL){
			r_q->head = NULL;
			r_q->tail = NULL;
		}

		//return id of thread that was dequeued
		interrupts_set(enabled);
		return id;
	}

	ptr2=ptr2->next;
	//otherwise
	while(ptr2!=NULL){
		if(ptr2->tnumber == id){
			//perform cut
			ptr1->next=ptr2->next;

			//check if current ptr2 is a current tail
			if(ptr2 == r_q->tail){
				r_q->tail=ptr1;
			}
			free(ptr2);

			return id;
		}
		ptr1=ptr2;
		ptr2=ptr2->next;
	}
	
	//corner case of queue being empty or element not found
	interrupts_set(enabled);
	return THREAD_INVALID;
}
//*Helper Function: Dequeue head element -> in the case of threadany
Tid dequeue_element_from_head(struct wait_queue* r_q){


	int enabled = interrupts_set(0);
	Tid return_val;
	if(r_q->head ==NULL){
		//printf("line: 145 from thread.c dequeue_element_from_head \n");
		interrupts_set(enabled);
		return THREAD_NONE;
	}
	else if(r_q->head->next != NULL){
		struct thread_node* ptr= r_q->head;
		return_val = ptr->tnumber;
		r_q->head = r_q->head->next;
		free(ptr);
		interrupts_set(enabled);
		return return_val;
	}
	else{ //case where only one last element needs to be dequed
		return_val = r_q->head->tnumber;
		free (r_q->head);
		r_q->head=NULL;
		r_q->tail= NULL;
		interrupts_set(enabled);
		return return_val;
	}
	interrupts_set(enabled);
	return THREAD_NONE;

}
//*Helper Function: Dequeues element -> remove element at begging of queue

void set_context_function(int i, void* temp_sp, void *parg, void (*fn) (void*)){
	//set thread current_state and stack pointer
	array_of_threads[i].current_state=READY;
	array_of_threads[i].stack_ptr=temp_sp;

	array_of_threads[i].wq=wait_queue_create();

	array_of_threads[i].waiting_on=-1;

	//Ensure alignment of stack pointer temp_sp
	temp_sp = temp_sp + THREAD_MIN_STACK;
	int sp_mod = (unsigned long long) temp_sp%16;
	temp_sp=temp_sp-sp_mod; //to align address by adjusting temp_sp
	//voodoo step of subtracting by 8 from piazza
	temp_sp=temp_sp-8; 

	//Manual changes to gregs --> for stub function call
	array_of_threads[i].current_thread_context.uc_mcontext.gregs[REG_RIP] = (greg_t) &thread_stub; //pointer to stub functiona ddress
	array_of_threads[i].current_thread_context.uc_mcontext.gregs[REG_RDI] = (greg_t) fn; //parameter 1 to thread_stub function
	array_of_threads[i].current_thread_context.uc_mcontext.gregs[REG_RSI] = (greg_t) parg;
	array_of_threads[i].current_thread_context.uc_mcontext.gregs[REG_RSP] = (greg_t) temp_sp; //store correct alignment of RSP
	
	//put newly created thread on ready queue
	enqueue(i,&r_q);
	//increment number of threads
	nums_existing_threads++;
}
/*Helper Function: Finds the first thread in array with status EXITED to be used for the creation of the new thread */
//Function returns the Tid of the thread ID that is available for creating a new thread
Tid thread_create_helper(void (*fn) (void*), void *parg){
	//iterates through global array of threads to exit and deallocate first thread with status EXITED
	for(int i=0 ; i<THREAD_MAX_THREADS; i++){ 
		if(array_of_threads[i].current_state == UNINITIALIZED){
			array_of_threads[i].exit_code=-1; //initialize woke up variable
			//no freeing is needed for threads that are uninitialized
			getcontext(&(array_of_threads[i].current_thread_context));
			//allocate sp and ensure alignment of rbp
			void* temp_sp = malloc(THREAD_MIN_STACK);
			//check for allocation of memory error
			if(temp_sp == NULL){
				//return error code for lack of memory
				return THREAD_NOMEMORY;
			}
			else{ //if memory is available
				set_context_function(i, temp_sp, parg,fn);
			}
			return i;
		}
		else if(array_of_threads[i].current_state== EXITED){
			array_of_threads[i].exit_code=-1; //initialize woke up variable
			getcontext(&(array_of_threads[i].current_thread_context));
			//since current thread is extied, we need to free the sp and context it has used 
			array_of_threads[i].current_state = READY;  
			free(array_of_threads[i].stack_ptr); //free all allocations on the stack

			//same process as for uninitialized threads
			//allocate sp and ensure alignment of rbp
			void* temp_sp = malloc(THREAD_MIN_STACK);
			//check for allocation of memory error
			if(temp_sp == NULL){
				//return error code for lack of memory
				return THREAD_NOMEMORY;
			}
			else{ //if memory is available
				set_context_function(i,temp_sp,parg,fn);
			}

			//return after first thread that has been EXITED is freed and
			return i; 
		}
	}	
	return THREAD_NOMORE;
}

void thread_awaken(Tid ID_thread_to_kill){
	int enabled = interrupts_set(0);
	//remove current thread to kill from wait queue of thread that it is waiting on
	int waiting_on = array_of_threads[ID_thread_to_kill].waiting_on;
	int thread_removed = dequeue_element(ID_thread_to_kill, array_of_threads[waiting_on].wq);
	assert(thread_removed == ID_thread_to_kill);

	//set new condition on thread_to_kill
	array_of_threads[ID_thread_to_kill].current_state= KILLED;

	//enqueue thread killed onto ready queue
	enqueue(ID_thread_to_kill, &r_q);
	nums_existing_threads++;

	interrupts_set(enabled);
	return ;
}
//THREADS API IMPLEMENTATION ==========================================================================================================================================
void
thread_init(void)
{
	//dont need interrupt disabling here yet because interrupt hasn't been registered yet by register_interrupt_handler
	/* your optional code here */
	nums_existing_threads++; //initialized as 1 because kernal thread is the first thread
	current_running_thread = 0; //Tid of kernal thread is initialized as zero.

	//initialize thread array
	for(int i=0; i<THREAD_MAX_THREADS; i++){
		//leave the first Tid to kernal thread
		if(i==0){
			array_of_threads[i].current_state = RUNNING;
			array_of_threads[i].thread_id = i; //probably don't need this
			//array_of_threads[i].next =NULL; //initialized as NULL pointer for next

			//set up wq for individual threads
			array_of_threads[i].wq=wait_queue_create();

			array_of_threads[i].waiting_on=-1;

			//code for setting up ucontext for thread 0
			int error = getcontext(&array_of_threads[i].current_thread_context);;
			// should get context fail assert will be triggered
			assert(error==0); //if error returned is zero, it meanse no error has occured --> thus assert does nothing
		}
		else{
			array_of_threads[i].current_state = UNINITIALIZED;
			array_of_threads[i].wq=NULL; //create the wait_queue of other threads when they are created 
		}
	}
	
	//create ready queue
	set_up_r_q(&r_q);
}

Tid
thread_id()
{
	//return current thread tid
	return current_running_thread;	
	//return THREAD_INVALID;
}

/* New thread starts by calling thread_stub. The arguments to thread_stub are
 * the thread_main() function, and one argument to the thread_main() function. 
 */
void
thread_stub(void (*thread_main)(void *), void *arg)
{			

		interrupts_on(); //turn interrupts_on for threads that we just yield into because when we created these threads we are now running, they take the interrupt state of their predecessor which is disabled.
		//here the thread_main function is really just whatever function we want our thread to perform. Upon completion of that function. Our thread will 
		//return back here into the thread_stub function for exiting
        thread_main(arg); // call thread_main() function with arg

		thread_exit(0);
}

Tid
thread_create(void (*fn) (void *), void *parg)
{
	//accessing shared nums_existing threads variable
	int enable= interrupts_set(0);
	//first check if maximum number of threads has been reached 
	if(nums_existing_threads >= THREAD_MAX_THREADS){
		//disable interrupts as we are not accessing shared variable anymore
		interrupts_set(enable);
		return THREAD_NOMORE;
	}

	//Check if more memory can be allocated for new thread
	//int thread_id_returned = thread_create_helper(fn,parg);
	int ret = thread_create_helper(fn,parg);
	if(ret==THREAD_NOMEMORY){
		//might not need this interrupts_set
		interrupts_set(enable);
		return THREAD_NOMEMORY;
	}

	interrupts_set(enable);	//enable interrupts to happen again
	return ret;
}

Tid
thread_yield(Tid want_tid)
{

	//disable interrupts
	int enable = interrupts_set(0);
	//if current thread has state of killed --> call thread_exit so that it sets it as exited to be zfreeed
	if(array_of_threads[current_running_thread].current_state == KILLED){
		interrupts_set(enable);
		thread_exit(0);
	}
	//display_queue();
	// tells thread system to run any thread in the ready queue
	if (want_tid>= THREAD_MAX_THREADS || want_tid < -2){ //check if want_tid is valid in the first place
		interrupts_set(enable);
		return THREAD_INVALID;
	}
	// tells thread system to continue the execution of caller
	else if(want_tid == THREAD_SELF || want_tid==current_running_thread){
		interrupts_set(enable);
		return current_running_thread;
	}
	//case where ANY Thread could be chosen to run
	else if(want_tid == THREAD_ANY){
		//printf("line: 351 from thread.c thread_yield --> I am in THREAD ANY \n");
		Tid thread_returned;
		thread_returned= dequeue_element_from_head(&r_q);
		//printf("line 375: thread_returned value : %d", thread_returned);

		//check if there are any avaialbe threads to run
		if(thread_returned==THREAD_NONE){
			interrupts_set(enable);
			return THREAD_NONE;
		}
		else{ //dequeue successful thus perform context switching
			//thus we need a boolean to detect whether or not we are back from the thread we yielded to or are yielding for the first time
			volatile bool threadOriginal = false; //note this boolean variable is stored as an local variable on the stack of the thread that first ran this line of code. 

			//get current context for the thread invoking thread_yield
			getcontext(&array_of_threads[current_running_thread].current_thread_context); //this is where Program counter points to when we return to this thread 
			//printf("I am thread: %d: \n", thread_id());
			//printf("404 am here: \n");
			//routine for freeing and destroying EXITED threads
			for(int i=0; i<THREAD_MAX_THREADS; i++){
				if(i!=current_running_thread){ //note the one we need to access can't be the one we are running. or else we wouldve freed the stack of running thread
					if(array_of_threads[i].current_state==EXITED){
						//reset thread waiting on
						//array_of_threads[i].waiting_on=-1;
						//free allocated stack
						free(array_of_threads[i].stack_ptr);
						wait_queue_destroy(array_of_threads[i].wq);
						//set state to T_EMPTY
						//printf("I am thread: %d: \n", thread_id());
						//printf("changing thread %d to UNIT\n",i);
						array_of_threads[i].current_state = UNINITIALIZED;
					}
				}
			}

			// //if current thread has state of killed --> call thread_exit so that it sets it as exited to be freeed
			// if(array_of_threads[current_running_thread].current_state == KILLED){
			// 	thread_exit(0);
			// }

			if(!threadOriginal){
				//change threadOriginal to true meaning that next time around we will be running as the original thread that called yield
				threadOriginal =true;
				//set the current status of the thread to ready again and push back into queue
				if(array_of_threads[current_running_thread].current_state == RUNNING && array_of_threads[current_running_thread].current_state != SLEEP){
					//printf("line: 555 from thread.c thread_yield --> STATUS: %d \n", array_of_threads[current_running_thread].current_state);
					array_of_threads[current_running_thread].current_state= READY;
					//printf("before enqueue: \n");
					//display_queue();
					//printf("line: 555 from thread.c thread_yield --> STATUS: %d \n", array_of_threads[current_running_thread].current_state);
					//printf("line: 392 from thread.c thread_yield --> current running_thread:%d \n", current_running_thread);
					//printf("line: 392 from thread.c thread_yield --> I shouldn't be here\n");
					enqueue(current_running_thread,&r_q);
				}
				//set context of the thread is that available to run in ready queue using the Tid returned
				if(array_of_threads[thread_returned].current_state!= KILLED){
					array_of_threads[thread_returned].current_state=RUNNING;
				} 

				//set context of thread so that we are running on the thread we yielded to
				current_running_thread= thread_returned;
				//printf("line 448 thread.c Yielding to thread: %d\n", current_running_thread);
				setcontext(&array_of_threads[current_running_thread].current_thread_context);
			}
			interrupts_set(enable);
			return thread_returned;
		}
	}
	else{ //an input wantid is registered
		Tid thread_returned = dequeue_element(want_tid,&r_q);
		if(thread_returned==THREAD_INVALID){
			interrupts_set(enable);
			return THREAD_INVALID;
		}
		else{ //in case where thread is ready and available to run
			//thus we need a boolean to detect whether or not we are back from the thread we yielded to or are yielding for the first time
			volatile bool threadOriginal = false;
			//get current context for the thread invoking thread_yield
			getcontext(&array_of_threads[current_running_thread].current_thread_context); //this is where Program counter points to when we return to this thread 
			//printf("line: 386 from thread.c thread_yield \n");

			//routine for freeing and destroying EXITED threads
			for(int i=0; i<THREAD_MAX_THREADS; i++){
				if(i!=current_running_thread){ //note the one we need to access can't be the one we are running. or else we wouldve freed the stack of running thread
					if(array_of_threads[i].current_state==EXITED){
						//array_of_threads[i].waiting_on=-1;
						//free allocated stack
						free(array_of_threads[i].stack_ptr);
						wait_queue_destroy(array_of_threads[i].wq);
						//set state to T_EMPTY
						array_of_threads[i].current_state = UNINITIALIZED;
					}
				}
			}

			// //if current thread has state of killed --> call thread_exit so that it sets it as exited to be freeed
			// if(array_of_threads[current_running_thread].current_state == KILLED){
			// 	thread_exit(0);
			// }

			if(!threadOriginal){
				//change threadOriginal to true meaning that next time around we will be running as the original thread that called yield
				threadOriginal =true;
				//set the current status of the thread to ready again and push back into queue
				if(array_of_threads[current_running_thread].current_state == RUNNING && array_of_threads[current_running_thread].current_state != SLEEP){
					array_of_threads[current_running_thread].current_state= READY;
					enqueue(current_running_thread,&r_q);
				}
				//set context of the thread is that available to run in ready queue using the Tid returned
				if(array_of_threads[want_tid].current_state!= KILLED){
					array_of_threads[want_tid].current_state=RUNNING;
				} 

				//set context of thread so that we are running on the thread we yielded to
				current_running_thread= want_tid;
				setcontext(&(array_of_threads[current_running_thread].current_thread_context));
			}
			interrupts_set(enable);
			return want_tid;
		}
	}

	//if code reaches here, it means thread yield has failed
	interrupts_set(enable);
	return THREAD_FAILED;
}

void
thread_exit(int exit_code)
{
	int enabled = interrupts_set(0);

	//printf("seg here 1\n");
	thread_wakeup(array_of_threads[thread_id()].wq, 1);
	//printf("seg here 2\n");
	//check if this is the last thread to run
	if(nums_existing_threads>=1){ //if other threads are still waiting to be run
		//set current thread to be exited
		//printf("seg here 3\n");
		//printf("nums_of_existing_threads: %d\n", nums_existing_threads);
		array_of_threads[current_running_thread].current_state= EXITED;
		//printf("current_state: %d\n", array_of_threads[current_running_thread].current_state);
		//printf("seg here 4\n");
		//decrement number of running threads
		nums_existing_threads--;
		
		//printf("seg here 5\n");
		//update exit_code status
		//printf("In thread_exit\n");
		//printf("Thread ID in thread_exit: %d\n", thread_id());
		//printf("exit_code: %d\n",exit_code);
		array_of_threads[thread_id()].exit_code=exit_code;
		//printf("seg here 6\n");
		//wake up all threads that depend on this one thread
		//printf("inside wq we have tid: %d\n", array_of_threads[thread_id()].wq->head->tnumber);
		thread_wakeup(array_of_threads[thread_id()].wq, 1); //wake up all thread

		//printf("seg here 7\n");
		//printf("woke up %d nums threads\n", numwoke);
		interrupts_set(enabled);

		//run which ever other thread is available
		thread_yield(THREAD_ANY);
		if(thread_yield(THREAD_ANY)==THREAD_NONE){
			exit(0);
		}
	}
	else{
		//if this is the last thread running --> exit
		interrupts_set(enabled);
		exit(0);
	}
}

Tid
thread_kill(Tid tid) 
{	
	int enabled = interrupts_set(0);
	//checking if Tid is negative or not a valid thread
	if(tid<0 || tid>= THREAD_MAX_THREADS){
		interrupts_set(enabled);
		return THREAD_INVALID;
	}
	else if(tid ==current_running_thread){// a thread can't kill itself
		interrupts_set(enabled);
		return THREAD_INVALID;
	}
	else if(array_of_threads[tid].current_state==UNINITIALIZED){
		interrupts_set(enabled);
		return THREAD_INVALID;
	}
	else{ //successful kill
		//go through all thread wait queue's 
		//printf("here \n");
		if(array_of_threads[tid].current_state==SLEEP){
			array_of_threads[tid].current_state = KILLED;
			thread_awaken(tid);
		}
		array_of_threads[tid].current_state = KILLED;
		array_of_threads[tid].exit_code= 9; //means this thread is killed
		
		interrupts_set(enabled);
		return tid;
		//free(array_of_threads[tid].stack_ptr); --> will be double freed
	}
	interrupts_set(enabled);
	return THREAD_FAILED; //if none of the previous conditions were triggered
}

/*******************************************************************
 * Important: The rest of the code should be implemented in Lab 3. *
 *******************************************************************/

/* make sure to fill the wait_queue structure defined above */
struct wait_queue *
wait_queue_create()
{
	struct wait_queue *wq;

	wq = malloc(sizeof(struct wait_queue));
	assert(wq);

	wq->head = NULL;
	wq->tail = NULL;

	return wq;
}

void
wait_queue_destroy(struct wait_queue *wq)
{
	//implement linked list free 
	struct thread_node* temp_ptr1;
	struct thread_node* temp_ptr2;
	temp_ptr1 = wq->head;
	while(temp_ptr1!=NULL){
		temp_ptr2=temp_ptr1->next;
		free(temp_ptr1);
		temp_ptr1=temp_ptr2;
	}
	free(wq);
}

Tid
thread_sleep(struct wait_queue *queue)
{
	int enabled = interrupts_set(0);
	//first check if queue is empty
	if(queue==NULL){
		interrupts_set(enabled);
		return THREAD_INVALID; 
	}
	
	else if(nums_existing_threads==1){ //meaning there is only one thread running-->ie curent thread
		interrupts_set(enabled);
		return THREAD_NONE;
	}
	else{
		//set current state of thread running to sleep
		array_of_threads[current_running_thread].current_state = SLEEP;
		//enqueue current thread into given wait queue
		enqueue(current_running_thread,queue);
		//since thread sleeping means thread is not aviable to run unless awaken decrement nums_thread
		nums_existing_threads--;
		//turn interrupt back on: note that since thread_yield already has Tid stored even if we get interrupted before return, it is fine
		interrupts_set(enabled);

		//NOTNEEDED: store the Tid of the thread we are yielding to in local variable
		return thread_yield(THREAD_ANY);
	}
	interrupts_set(enabled);
	return THREAD_FAILED;
}

/* when the 'all' parameter is 1, wakeup all threads waiting in the queue.
 * returns whether a thread was woken up on not. */
int
thread_wakeup(struct wait_queue *queue, int all)
{
	int enabled = interrupts_set(0);
	if(queue==NULL){
		interrupts_set(enabled);
		return 0;
	}
	else if(all==1){
		int nums_thread_awoken=0;
		//while loop iteration to dequeue all element from waiting queue and put into ready queue
		while(1){
			Tid ret= dequeue_element_from_head(queue);
			if(ret==THREAD_NONE){
				break;
			}		
			else{
				if(array_of_threads[ret].current_state!=KILLED){
					array_of_threads[ret].current_state= READY;
				}
				
				nums_thread_awoken++;
				//enqueue into ready queue
				enqueue(ret,&r_q);
				nums_existing_threads++; //available threads inc
			}
		}
		interrupts_set(enabled);
		return nums_thread_awoken;
	}

	else{//wake up one thread
		Tid woken_up_element_ID= dequeue_element_from_head(queue); //deque from wait
		if(woken_up_element_ID == THREAD_NONE){
			//no threads to wake up from slumber
			interrupts_set(enabled);
			return 0;
		}

		//only change woken_up_eleemnt_ID thread state to READY if it hasn't been killed yet
		if(array_of_threads[woken_up_element_ID].current_state!=KILLED){
			array_of_threads[woken_up_element_ID].current_state= READY;
		}
		
		//enqueue into ready queue
		enqueue(woken_up_element_ID,&r_q);
		nums_existing_threads++; //available threads inc

		interrupts_set(enabled);
		return 1; //1 thread up so return 1
	}

	interrupts_set(enabled);
	return 0;
}

/* suspend current thread until Thread tid exits */
Tid
thread_wait(Tid tid, int *exit_code)
{
	//printf("THREAD ID: %d \n",thread_id());
	//printf("waiting on Tid: %d\n", tid);
	//printf("Proof I came in thread_wait thread.c 676\n");
	//printf("given tid state is: %d \n",array_of_threads[tid].current_state);
	if(tid<0 || tid>=THREAD_MAX_THREADS || tid==thread_id() ){ //basic invalid tid's //||array_of_threads[tid].current_state==SLEEP
		//printf("THREAD ID: %d \n",thread_id());
	//	printf("RETURN from wait 0\n");
		return THREAD_INVALID;
	}
	else if(array_of_threads[tid].current_state==EXITED ||(array_of_threads[tid].current_state==UNINITIALIZED && array_of_threads[tid].exit_code!=-1 && exit_code!=NULL)){
		//printf("RETURN from wait 1\n");
		*exit_code= array_of_threads[tid].exit_code;
        array_of_threads[tid].exit_code = -1;
		return tid; //the thread we want to wait on already has at least another thread that is waiting
	}
	else if(array_of_threads[tid].current_state==UNINITIALIZED){

		//*exit_code= array_of_threads[tid].exit_code2;
	//	printf("THREAD ID: %d \n",thread_id());
	//	printf("RETURN from wait 2\n");
		return THREAD_INVALID;
	}
	else{
		//update current thread's waiting on id
		array_of_threads[thread_id()].waiting_on=tid;
	//	printf("I am here before sleep in thread.c 676\n");
		thread_sleep(array_of_threads[tid].wq);
		// only one thread should successfully return
		int enabled= interrupts_set(0); //lock critical section so only one thread can modify exit code
	//	printf("after sleep my thread id is: %d\n",thread_id());
	//	printf("I am here after sleep in thread.c 676\n");
		//printf("my threadID is: %d\n",thread_id());
		//printf("was waiting on %d \n", tid);
		//printf("thread exitcode here: %d\n", array_of_threads[tid].exit_code);
		//printf("my thread exitcode is: %d\n", array_of_threads[thread_id()].exit_code);
		//other's should fail
		if(array_of_threads[tid].exit_code != -1){// case for where only one thread wakes up successfully
	//		printf("I am here in thread.c 676\n");
			if(exit_code!=NULL){
				*exit_code = array_of_threads[tid].exit_code;
			}
			array_of_threads[tid].exit_code2= array_of_threads[tid].exit_code;
			array_of_threads[tid].exit_code = -1;

		//	printf("RETURN from wait 3\n");
			interrupts_set(enabled);
			return tid;
		}
		else if(array_of_threads[thread_id()].current_state==KILLED){
			interrupts_set(enabled);
			thread_exit(array_of_threads[thread_id()].exit_code);
		}

		//other threads should clear their waiting on id as well
		array_of_threads[tid].waiting_on=-1;

		//printf("RETURN from wait 4\n");
		interrupts_set(enabled);
		return THREAD_INVALID;
	}

	//printf("RETURN from wait 5\n");
	return 0;
}

struct lock {
	/* ... Fill this in ... */
	bool lock; //is true if lock has been acquired by thread, FALSE otherwise
	Tid thread_holding_lock;
	struct wait_queue * wq;
};

struct lock *
lock_create()
{
	int enabled= interrupts_set(0);
	struct lock *lock;

	lock = malloc(sizeof(struct lock));
	assert(lock);

	//create wait queue
	lock->wq=wait_queue_create();	
	//assign lock variable to be False initially
	lock->lock=false; //false==no lock acquired
	lock->thread_holding_lock=-1; //-1 means no lock is holding lock

	interrupts_set(enabled);
	return lock;
}

void
lock_destroy(struct lock *lock)
{

	int enabled = interrupts_set(0);
	assert(lock != NULL);

	if(lock==false){
		if(lock->wq !=NULL){
			//free wait queue
			struct thread_node* temp_ptr1;
			struct thread_node* temp_ptr2;
			temp_ptr1 = lock->wq->head;
			while(temp_ptr1!=NULL){
				temp_ptr2=temp_ptr1->next;
				free(temp_ptr1);
				temp_ptr1=temp_ptr2;
			}

			free(lock);
			interrupts_set(enabled);
			return;
		}
	}

	interrupts_set(enabled);
	return;
}

void
lock_acquire(struct lock *lock)
{	
	int enabled = interrupts_set(0);
	assert(lock != NULL);

	while(lock->lock == true){
		thread_sleep(lock->wq);
	}

	lock->lock=true;
	lock->thread_holding_lock=thread_id(); 

	interrupts_set(enabled);
	
}

void
lock_release(struct lock *lock)
{
	int enabled = interrupts_set(0);
	assert(lock != NULL);
	lock->lock=false;
	lock->thread_holding_lock=thread_id();
	thread_wakeup(lock->wq,1);

	interrupts_set(enabled);
}

struct cv {
	/* ... Fill this in ... */
	struct wait_queue * cv_wait_queue;
};

struct cv *
cv_create()
{
	int enabled = interrupts_set(0);
	struct cv *cv;

	cv = malloc(sizeof(struct cv));
	assert(cv);

	//cv=0; //cv=0 means condition has not met cv= anything else means met
	cv->cv_wait_queue=wait_queue_create();


	interrupts_set(enabled);
	return cv;
}

void
cv_destroy(struct cv *cv)
{
	int enabled = interrupts_set(0);
	assert(cv != NULL);

		if(cv->cv_wait_queue !=NULL){
			//free wait queue
			struct thread_node* temp_ptr1;
			struct thread_node* temp_ptr2;
			temp_ptr1 = cv->cv_wait_queue->head;
			while(temp_ptr1!=NULL){
				temp_ptr2=temp_ptr1->next;
				free(temp_ptr1);
				temp_ptr1=temp_ptr2;
			}

			free(cv);
			interrupts_set(enabled);
			return;
		}

	interrupts_set(enabled);
	return;
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);

	if(lock->lock == true && lock->thread_holding_lock==thread_id()){ //if our thread is the one holding the lock, then we need to 
		//int enabled = interrupts_set(0); //keep this operation locked
		lock_release(lock);
		thread_sleep(cv->cv_wait_queue);
		//interrupts_set(enabled);
		lock_acquire(lock); //expecting that the following section of code is also CS so reacquire lock
	}
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);

	if(lock->lock == true && lock->thread_holding_lock==thread_id()){ //if our thread is the one holding the lock, then we need to 
		thread_wakeup(cv->cv_wait_queue,0); //wake up one thread 
	}
	
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);

	if(lock->lock == true && lock->thread_holding_lock==thread_id()){ //if our thread is the one holding the lock, then we need to 
		thread_wakeup(cv->cv_wait_queue,1); //wake up one thread 
	}
	
}
