#include <string.h>
#include "common.h"
#include <sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include <unistd.h>
#include <errno.h>
#include<dirent.h>

/* make sure to use syserror() when a system call fails. see common.h */
void
usage()
{
	fprintf(stderr, "Usage: cpr srcdir dstdir\n"); //this is how the command is typed into terminal
	exit(1);
}
//get permission bits for directory
int getChmod(const char *path){
	struct stat ret;
	stat(path,&ret);

	return (ret.st_mode & S_IRUSR)|(ret.st_mode & S_IWUSR)|(ret.st_mode & S_IXUSR)|/*owner*/
        (ret.st_mode & S_IRGRP)|(ret.st_mode & S_IWGRP)|(ret.st_mode & S_IXGRP)|/*group*/
        (ret.st_mode & S_IROTH)|(ret.st_mode & S_IWOTH)|(ret.st_mode & S_IXOTH);/*other*/
}

//file_copy function
void file_copy_routine(char* source_path_name, char* dest_path_name){
	int fd_source, fd_dest; // file descriptor
	char buffer[4096]; //buffer used to read characters from one file to the next

	//For single file only
	fd_source = open(source_path_name,O_RDONLY); 

	//read denied 
	if(fd_source < 0){
		//errors
		printf("Error: source file is not readable or does not exist");
		syserror(open,source_path_name);
		close(fd_source);
	}

	fd_dest = open(dest_path_name, O_WRONLY | O_CREAT, S_IRWXU); //if file path does not exist, we create the file using the path given //here bitwise OR operator is used
	
	//write into file
	int ret=read(fd_source, &buffer, 4096);
	while(1){ //read will be 1 as long as file has not reached the end.
		if(ret==0){
			break;
		}
		write(fd_dest, &buffer, ret); //write characters from file source to destination. 
		ret=read(fd_source, &buffer, 4096);
	}

	//close both files
	close(fd_dest);
	close(fd_source);
}

//directory recursive funciton
void dir_recursive(const char* path_name_to_dir_source, const char* path_name_to_dir_dest){

	//first step is to open directory	
	DIR* entryDIR = opendir(path_name_to_dir_source);
	//check if opendir is successful
	if(entryDIR==NULL){
		syserror(opendir, path_name_to_dir_source);
		printf("Error: entryDIR generated is NULL \n");
		return;
	}

	else {
		struct dirent *dir_element;
		dir_element = readdir(entryDIR);

		while(dir_element!=NULL){
			if(!strcmp(dir_element->d_name, ".") || !(strcmp(dir_element->d_name,".."))){
				//do nothing
				NULL;
			}
			else{
				//create current path name of source and dest
				char* dir_element_source_name = (char*) malloc(strlen(path_name_to_dir_source)+strlen(dir_element->d_name)+1+1); //add 1 for null pointer and 1 more for /
				strcpy(dir_element_source_name, path_name_to_dir_source);
				strcat(dir_element_source_name, "/");
				strcat(dir_element_source_name, dir_element->d_name);
				strcat(dir_element_source_name,"\0"); //add terminating null pointer

				char* dir_element_dest_name = (char*) malloc(strlen(path_name_to_dir_dest)+strlen(dir_element->d_name)+1+1);
				strcpy(dir_element_dest_name, path_name_to_dir_dest);
				strcat(dir_element_dest_name, "/");			
				strcat(dir_element_dest_name, dir_element->d_name);
				strcat(dir_element_dest_name,"\0"); //add terminating null pointer

				//determine if this is a subdirectory or a file
				struct stat SB;
				stat(dir_element_source_name,&SB); //populates SB with a struct type statstr
				//if is just a file
				if(S_ISREG(SB.st_mode)){
					//copy file routine
					file_copy_routine(dir_element_source_name,dir_element_dest_name);
					//Remember to set chmod for file access permission
					mode_t mode_code = SB.st_mode;
					chmod(dir_element_dest_name,mode_code);
				}
				//else is directory
				else{ 
					//create new directory
					mkdir(dir_element_dest_name,0700);
					//recur function
					dir_recursive(dir_element_source_name,dir_element_dest_name);
					//Remember to set chmod for directory access permission after directory is created
					chmod(dir_element_dest_name, SB.st_mode);
				}
			}
			//iterate to read rest of entries in direcotry
			dir_element = readdir(entryDIR);
		}

		//last step is to close directory stream
		closedir(entryDIR);

		//important return --> what do we want here for recursion.
		return;
	}
}
int
main(int argc, char *argv[]) //argc means argument count. argv means argument vector
{
	if (argc != 3) { // checks if input in the terminal has three strings
		usage();
	}
	
	//code starts: Follow the following order of function calls -> open,creat,read,write,and close
	// char* command = argv[0];
	char* source_path_name = argv[1]; //refers to directory
	char* dest_path_name = argv[2]; //refers to directory

	//testing
	// printf("Command: %s\n", command);
	// printf("Source: %s\n", source_path_name);
	// printf("Dest: %s\n", dest_path_name);

	//test copying file function --> pass in full path name to copy file
	//file_copy_routine(source_path_name,dest_path_name);
	//first create target directory
	struct stat SB;
	stat(source_path_name,&SB);
	if(mkdir(dest_path_name,0700)<0){
		syserror(mkdir,dest_path_name);
	}
	dir_recursive(source_path_name, dest_path_name);

	//chmod here
	chmod(dest_path_name,SB.st_mode);



	return 0;
}
