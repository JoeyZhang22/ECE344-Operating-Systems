#include "request.h"
#include "server_thread.h"
#include "common.h"

#define TABLE_SIZE  5381 //from hash function

struct hash_table_entry{
	int key; //index within hashtable
	char * file_name; // name of file
	struct file_data *cache_entry_data; //pointer to file data in memory
	struct hash_table_entry* next; // in case of collision 
	int size;
};

struct lru_entry{
	char* file_name;
	int file_size;
	struct lru_entry* previous;
	struct lru_entry* next;
};

struct lru_queue{
	struct lru_entry* head; // most recently used file is inserted here 
	struct lru_entry* tail; // least recently used file is evicted here
};

//struct for cache table
struct cache_table{
	int occupied_cache; // numbers of bytes in cache already used by files --> each file is 
	int maximum_bytes; // -->equal to max cache size
	struct hash_table_entry ** hash_table; //array of pointers to hash_table_entry
	struct lru_queue* lru_q;
	pthread_mutex_t locking_cache;
};

struct server {
	int nr_threads;
	int max_requests;
	int max_cache_size;
	int exiting;
	/* add any other parameters you need */
	//each server struct keeps a cache_table
	struct cache_table* cache;
	int *conn_buf; 
	pthread_t *threads;
	int request_head;
	int request_tail;
	pthread_mutex_t mutex;
	pthread_cond_t prod_cond; //producer condition
	pthread_cond_t cons_cond; //consumer condition
};

/* static functions */
//HASH TABLE ===================================================================================================================================================================
//hash_function for generating index to hash table
int hash_function(const char* key, int size){
	int hash = 5381;//from instructor's notes
	int c;

    while ((c = *key++)){
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
	}
	hash=hash%size; // ensure index returend is not bigger than size
    return hash;
}

// //hash table init function RETURNS pointer to cache_table
struct cache_table* cache_table_initialization(){
	//Request memory for pointer
	struct cache_table* cache_ptr = (struct cache_table* )malloc(sizeof(struct cache_table));

	//locking_cache mutex initialization
	pthread_mutex_init(&cache_ptr->locking_cache,NULL); //set up locking variable

	//initialize data inside cache_ptr
	cache_ptr->occupied_cache = 0; //cache is unoccupied to begin with
	cache_ptr->maximum_bytes = TABLE_SIZE;
	cache_ptr->hash_table = (struct hash_table_entry**)malloc(sizeof(struct hash_table_entry*) * TABLE_SIZE); //create hash_table array of pointers up to size of cache_table
	
	//LRU queue initialization
	cache_ptr->lru_q = (struct lru_queue *)malloc(sizeof(struct lru_queue)); //initial queue is empty so head and tail both equal NULL
	cache_ptr->lru_q->head=NULL;
	cache_ptr->lru_q->tail=NULL;

	return cache_ptr;
}
//===================================================================================================================================================================

//LRU_QUEUE ===================================================================================================================================================================
//lru_queue pop specified node
struct lru_entry* lru_entry_pop_specified(char* file_name_to_pop, struct lru_queue* queue){
	struct lru_entry* current_ptr = queue->head;
	while(current_ptr!=NULL){
		if(!strcmp(current_ptr->file_name,file_name_to_pop)){
			if(current_ptr->previous == NULL){ //case where current_ptr points to current head in queue
				queue->head = current_ptr->next;
			}
			else{
				current_ptr->previous->next = current_ptr->next; 
			}
			if(current_ptr->next==NULL){ //case where current_ptr is the last entry of lru in the list
				queue->tail = current_ptr->previous;
			}
			else{
				current_ptr->next->previous =  current_ptr->previous;
			}
			return current_ptr;
		}
		current_ptr=current_ptr->next;
	}

	return NULL;
}

//lru_queue enqueue at head
void lru_enqueue_head(char*file_name_to_enqueue, struct lru_queue* queue){
	struct lru_entry* new_entry;
	new_entry = (struct lru_entry*)malloc(sizeof(struct lru_entry));
	new_entry->file_name = strdup(file_name_to_enqueue);

	//check if queue is currently empty
	if(queue->head == NULL || queue->tail == NULL){
		queue->head = new_entry;
		queue->tail = new_entry;
		new_entry->previous = NULL;
		new_entry->next = NULL;
	}
	else{
		new_entry->previous =NULL;
		queue->head->previous = new_entry;
		new_entry->next = queue->head;
		queue->head = new_entry;
	}
	
}

//lru_queue dequeue at tail -->means least used is being evicted
void lru_dequeue_tail(struct lru_queue* queue){
	struct lru_entry* current_tail = queue->tail;
	if(current_tail==NULL){
		printf("You are trying to dequeue a tail that is NULL");
		return;
	}
	else{
		if(current_tail->previous ==NULL){ // popping only entry 
			queue->head= NULL;
			queue->tail= NULL;
		}
		else{
			current_tail->previous->next=NULL;
			queue->tail = current_tail->previous;
		}
		//free up currently popped tail
		free(current_tail->file_name);
		free(current_tail);
	}
}

//lru_queue make head --> makes a file come to head of queue --> most recently used
void lru_bring_up(char* file_name_to_head, struct lru_queue* queue){
	//printf("all good til 1\n");
	struct lru_entry* temp_ptr = lru_entry_pop_specified(file_name_to_head, queue);
	

	temp_ptr->next = queue->head;
	temp_ptr->previous = NULL;
	
	if(queue->head ==NULL || queue->tail==NULL){ //no more entry in queue
		queue->head = temp_ptr;
		queue->tail = temp_ptr;
		temp_ptr->previous=NULL;
		temp_ptr->next=NULL;
	}
	else{
		temp_ptr->previous= NULL;
		queue->head->previous = temp_ptr;
		temp_ptr->next = queue->head;
		queue->head = temp_ptr;
	}
}

//lru_queue clean up --> frees all memories associated with lru queue
void lru_clean_up(struct lru_queue* queue){
	struct lru_entry* next_ptr;
	struct lru_entry* current_ptr = queue->head;

	//queue traversal routine
	while(current_ptr!= NULL){
		next_ptr = current_ptr->next;
		free(current_ptr->file_name);
		free(current_ptr);
		current_ptr=next_ptr;
	}

	//after finishing elemental free perform free for queue itself
	free(queue);
}
//===================================================================================================================================================================

//CACHE_EVIT, LOOKUP, INSERT FUNCTIONS ===================================================================================================================================================================
static struct file_data *file_data_init(void);
static void file_data_free(struct file_data *data);
//evict helper function--> determines size of cache to evict +'ve or -'ve integer
int cache_evict_helper(int max_cache_storage, int file_size, int occupied_cache){
	return file_size + occupied_cache - max_cache_storage; // if number is -'ve the no files need to be evicted. 
	//if number is +'ve file of size abs(number) need to be evicted 
}
//cache_lookup function
struct hash_table_entry* cache_lookup(struct cache_table* cache,const char* file_to_look){
	int hash_key = hash_function(file_to_look, TABLE_SIZE);

	struct hash_table_entry* current_ptr = cache->hash_table[hash_key];

	//move onto next entry in the chain if collision occured
	while(current_ptr!= NULL){
		if(!strcmp(current_ptr->cache_entry_data->file_name,file_to_look)){ //if strcmp==0 then file name is equal. THUS return current_ptr
			return current_ptr;
		}
		current_ptr=current_ptr->next;
	}
	return NULL;
}

//cache_insert function
struct hash_table_entry* cache_insert(struct server * sv, struct file_data *data_to_store){
	//1. Check if file size can even fit into cache
	if(data_to_store->file_size > sv->max_cache_size){
		//printf("file size exceeds cache's max size\n ");
		return NULL;
	}
	
	//2. Check if there is already file in cache 
	if(cache_lookup(sv->cache,data_to_store->file_name)!=NULL){
		return NULL; //no need to insert as file is already in cache
	}

	//3. if file size is not larger than cache and is also not in cache, we can insert file into cache and update lru order
	int evict_file_size = cache_evict_helper(sv->max_cache_size,data_to_store->file_size,sv->cache->occupied_cache); 
	if(evict_file_size<=0){
		//if eviction was successful:
		int hash_map_key = hash_function(data_to_store->file_name, TABLE_SIZE);//get bin key
		struct hash_table_entry* new_entry_ptr = (struct hash_table_entry*)malloc(sizeof(struct hash_table_entry));
		
		//initialize entry fields 
		new_entry_ptr->cache_entry_data = file_data_init();
		new_entry_ptr->cache_entry_data->file_size=data_to_store->file_size;//store: size
		new_entry_ptr->cache_entry_data->file_buf= strdup(data_to_store->file_buf);//dup: data
		new_entry_ptr->cache_entry_data->file_name=strdup(data_to_store->file_name);//dup: name

		//inserting entry into hash_table
		if(sv->cache->hash_table[hash_map_key]!=NULL){//collision happened
			new_entry_ptr->next= sv->cache->hash_table[hash_map_key];
			sv->cache->hash_table[hash_map_key]=new_entry_ptr;
		}
		else{ //no collision
			sv->cache->hash_table[hash_map_key]=new_entry_ptr;
		}

		//increment occupied cache 
		sv->cache->occupied_cache=sv->cache->occupied_cache + data_to_store->file_size;

		// //insert new LRU node into queue
		// lru_enqueue_head(data_to_store->file_name,sv->cache->lru_q);

		return new_entry_ptr;
	}
	else{
		return NULL; //eviction failed 
	}
}
//===================================================================================================================================================================

//cache_evict function
/* initialize file data */
static struct file_data *
file_data_init(void)
{
	struct file_data *data;

	data = Malloc(sizeof(struct file_data));
	data->file_name = NULL;
	data->file_buf = NULL;
	data->file_size = 0;
	return data;
}

/* free all file data */
static void
file_data_free(struct file_data *data)
{
	free(data->file_name);
	free(data->file_buf);
	free(data);
}

static void
do_server_request(struct server *sv, int connfd)
{
	int ret;
	struct request *rq;
	struct file_data *data;

	data = file_data_init();

	/* fill data->file_name with name of the file being requested */
	rq = request_init(connfd, data);
	if (!rq) {
		file_data_free(data);
		return;
	}

	//NO CACHE: check if max_cache_size is defined as 0
	if(sv->max_cache_size<data->file_size){
		ret = request_readfile(rq);
		if (ret == 0) { /* couldn't read file */
			goto out;
		}
		/* send file to client */
		request_sendfile(rq);
	}

	else{ //if there is cache provided
		//lock section when section is accessing Cache
		pthread_mutex_lock(&sv->cache->locking_cache);
		//look up file in cache
		struct hash_table_entry* found_file_entry = cache_lookup(sv->cache,data->file_name);
		if(found_file_entry!=NULL){//file exists in cache
			//populate data with file content and file size
			data->file_buf= strdup(found_file_entry->cache_entry_data->file_buf);
			data->file_size=found_file_entry->cache_entry_data->file_size;
			request_set_data(rq,data);

			pthread_mutex_unlock(&sv->cache->locking_cache);
			
			//send file 
			request_sendfile(rq);
		}
		else{//if file doesn't exist, insert file into cache and then send file
			pthread_mutex_unlock(&sv->cache->locking_cache);

			//request read from disk
			ret=request_readfile(rq);

			if(!ret){
				goto out;//request failed
			}

			//lock cache
			pthread_mutex_lock(&sv->cache->locking_cache);
			//insert data in cache and lru
			found_file_entry = cache_insert(sv,data);
			pthread_mutex_unlock(&sv->cache->locking_cache);
			//request_sendfile for both cases 
			request_sendfile(rq);
		}
	}
out:
	request_destroy(rq);
	file_data_free(data);
}

static void *
do_server_thread(void *arg)
{
	struct server *sv = (struct server *)arg;
	int connfd;

	while (1) {
		//printf("line 455: looping before \n");
		pthread_mutex_lock(&sv->mutex);
		//printf("line 455: looping \n");
		while (sv->request_head == sv->request_tail) {
		//	printf("line 472: looping inside \n");
			/* buffer is empty */
			if (sv->exiting) {
				pthread_mutex_unlock(&sv->mutex);
				goto out;
			}
			pthread_cond_wait(&sv->cons_cond, &sv->mutex);
		}
	//	printf("line 480: skipped looping \n");
		/* get request from tail */
		connfd = sv->conn_buf[sv->request_tail];
		/* consume request */
		sv->conn_buf[sv->request_tail] = -1;
		sv->request_tail = (sv->request_tail + 1) % sv->max_requests;
		
		pthread_cond_signal(&sv->prod_cond);
		pthread_mutex_unlock(&sv->mutex);
		/* now serve request */
		do_server_request(sv, connfd);
	//	printf("line 473: after do_server_request: \n");
	}
out:
//	printf("line before NULL \n");
	return NULL;
}

/* entry point functions */

struct server *
server_init(int nr_threads, int max_requests, int max_cache_size)
{
//	printf("line 479: in server_init\n");
	struct server *sv;
	int i;

	sv = Malloc(sizeof(struct server));
	sv->nr_threads = nr_threads;
	/* we add 1 because we queue at most max_request - 1 requests */
	sv->max_requests = max_requests + 1;
	sv->max_cache_size = max_cache_size;
	sv->exiting = 0;

	/* Lab 4: create queue of max_request size when max_requests > 0 */
	sv->conn_buf = Malloc(sizeof(*sv->conn_buf) * sv->max_requests);
	for (i = 0; i < sv->max_requests; i++) {
		sv->conn_buf[i] = -1;
	}
	sv->request_head = 0;
	sv->request_tail = 0;

	/* Lab 5: init server cache and limit its size to max_cache_size */
	sv->cache= cache_table_initialization(); //initialize cache_table //i added here

	/* Lab 4: create worker threads when nr_threads > 0 */
	//added code here:

	//BETWEEN HERE AND ADDED CODE is my own added code
	pthread_mutex_init(&sv->mutex, NULL);
	pthread_cond_init(&sv->prod_cond, NULL);
	pthread_cond_init(&sv->cons_cond, NULL);	
	sv->threads = Malloc(sizeof(pthread_t) * nr_threads);

	//Routine for creating numbers of worker threads
	for (i = 0; i < nr_threads; i++) {
		SYS(pthread_create(&(sv->threads[i]), NULL, do_server_thread,
				   (void *)sv));
				   //pthread_create stores creates thread and stores thread ID into sv->threads array
				   //lets thread do entry function: do_server_thread
	}
	return sv;
	//returns to server.c main
}

void
server_request(struct server *sv, int connfd)
{
	if (sv->nr_threads == 0) { /* no worker threads */
		do_server_request(sv, connfd);
	} else {
		/*  Save the relevant info in a buffer and have one of the
		 *  worker threads do the work. */

		pthread_mutex_lock(&sv->mutex);
		while (((sv->request_head - sv->request_tail + sv->max_requests)
			% sv->max_requests) == (sv->max_requests - 1)) {
			/* buffer is full */
			pthread_cond_wait(&sv->prod_cond, &sv->mutex);
		}
		/* fill conn_buf with this request */
		assert(sv->conn_buf[sv->request_head] == -1);
		sv->conn_buf[sv->request_head] = connfd;
		sv->request_head = (sv->request_head + 1) % sv->max_requests;
		pthread_cond_signal(&sv->cons_cond);
		pthread_mutex_unlock(&sv->mutex);
	}
}

void
server_exit(struct server *sv)
{
	int i;
	/* when using one or more worker threads, use sv->exiting to indicate to
	 * these threads that the server is exiting. make sure to call
	 * pthread_join in this function so that the main server thread waits
	 * for all the worker threads to exit before exiting. */
	pthread_mutex_lock(&sv->mutex);
	sv->exiting = 1;
	pthread_cond_broadcast(&sv->cons_cond);
	pthread_mutex_unlock(&sv->mutex);
	for (i = 0; i < sv->nr_threads; i++) {
		pthread_join(sv->threads[i], NULL);
	}

	/* make sure to free any allocated resources */
	lru_clean_up(sv->cache->lru_q);
	free(sv->conn_buf);
	free(sv->threads);
	free(sv);
}
