#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "common.h"
#include "wc.h"
#include "string.h"
#include "ctype.h"


//define struct to be used
typedef struct item item;

//definition of struct items
struct item{
	char* key;
	int count;
	item* next; //for when collision happens
};

struct wc { //this is essentially the hashtable
	/* you can define this struct to have whatever fields you want. */
	//contains array of pointers 
	item** items; //array of pointers : int* pointer means pointer to int, int** means pointer to pointer of integer
	int element_count; //tracking numbers of elements inserted into ht
	int size; //total size of hashtable determined by size*2 or something else
};
//===============================================================================================================================

//define helper functions
unsigned long int hash_function(char* key, int size){
	unsigned long int hash = 5381;//from instructor's notes
	int c;

    while ((c = *key++)){
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
	}
	hash=hash%size; // ensure index returend is not bigger than size
    return hash;
}

item* create_items(char* key, int count){
	item* ptr; //pointer to new hash table element
	ptr=(item*) malloc(sizeof(item));
	//ptr->count= (int) malloc(sizeof(int));
	ptr->key= (char *) malloc(strlen(key)+1); //gives length of key without accounting for NULL terminaotr. Thus addition of 1
	ptr->next= (item*)malloc(sizeof(item)); // allocate memory for next pointer of item
	ptr->next= NULL;
	ptr->count = count; //count should be set to 0 by function call. 
	strcpy(ptr->key,key); //copy string into key

	return ptr;
}

struct wc *
wc_init(char *word_array, long size)
{
	struct wc *wc;

	wc = (struct wc *)malloc(sizeof(struct wc));
	assert(wc);

	//first initialize size and element_count of hashtable
	//TBD();
	wc->size= size*2; //allowed for change. Doesn't have to be 2* size
	wc->items = (item**) calloc(wc->size , sizeof(item*)); //calloc here differs from malloc in that it initializes all elemetns with value of 0 instead of garbage values.
	wc->element_count = 0; //initially no elements are in wc

	//start inserting values into wc
	char* head_of_word; //points to the head of a new word
	char* buffer_str; // buffer to temporarily store string extracted from array
	int counter=0; // initially the length of a given word is 0

	//set up head_of_word as first element of word_array
	head_of_word = word_array; 
	for(int index=0; index<size; index++){
		if(isspace(word_array[index])!=0 && counter>0){ //if a space is encountered and counter is longer than 0 length, we insert string
			
			//malloc space needed for buffer_str first
			buffer_str= (char*) malloc((counter+1)*sizeof(char));
			//strncpy routine
			strncpy(buffer_str, head_of_word, counter);
			strcat(buffer_str,"\0");
			//insert string 
			wc->element_count++; //iterate count of elements
			int insertID= hash_function(buffer_str,wc->size);

			//means inserting item for the first time ie: no collision and no similar values
			if(wc->items[insertID]==NULL){
				//first time inserting word
				wc->items[insertID] = create_items(buffer_str, 1); //inserting word for 1st time. Thus its count is 1
			}
			else{ //routine for where key already exists or need insertion to end of linked list
				//where key already exists !!!!!! important
				item* temp;
				item* ptr;
				int need_chaining = 1;
				temp=wc->items[insertID];

				while(temp!=NULL){

					if(strcmp(buffer_str,temp->key)==0){
						temp->count++;
						need_chaining = 0;
						break;
					}
					ptr=temp;
					temp=temp->next;
				}

				//collision occurs --> use chaining
				if(need_chaining == 1){
					while(ptr->next!=NULL){
						ptr=ptr->next;
					}
					//once NULL has been found
					//set ptr to new item
					ptr->next = create_items(buffer_str,1);
				}

			}
			
			//reset counter
			counter=0;
		}
		//skipping white spaces where no word is being counted
		else if(counter==0 && isspace(word_array[index])!=0){
			NULL;
		}
		//case where new word is beginning
		else if(isspace(word_array[index])==0 && counter==0){
			head_of_word = word_array + index;
			counter = 1;
		}
		//counting how long word is
		else{
			//iterate counter 
			counter++;
		}
	}
	return wc;
}

void
wc_output(struct wc *wc)
{
	for(int i=0; i<wc->size ; i++){
		//printf("here");
		//if index doesnt point to NULL it means an element or more than one elemetn in chains is in index
		if(wc->items[i]!=NULL){
			item* ptr = wc->items[i];
			//while loop to traverse items in chain and print out all information regarding each item
			while(ptr!=NULL){
				printf("%s:%d\n", ptr->key,ptr->count);
				//iterate ptr
				ptr=ptr->next;
			}
		}
	}
}

void
wc_destroy(struct wc *wc)
{
	//traverse array and linked list to free all memeory
	for(int i=0; i<wc->size ; i++){
		if(wc->items[i]!=NULL){
			item* temp_del;
			item* temp_iterate=wc->items[i];
			while(temp_iterate!=NULL){
				temp_del=temp_iterate;
				temp_iterate=temp_iterate->next;
				free(temp_del); //free each item
			}
		}
	}
	free(wc->items);
	free(wc);
}