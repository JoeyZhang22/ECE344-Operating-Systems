#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h> 



void error_checking(int returned_value, char* error_message){
	if(returned_value == -1){//error occured 

		//exit using the errno number recorded
		int err_code = errno;
		//print error message using printerror
		perror(error_message);
		exit(err_code);
		return;
	}
	else{ //no error
		return;
	}
}

int recur_pipe_call(char *input_command_strings[], int index_of_command, int max_command_num, int in_pipe_fd){ //input_command_strings starts from index 1
	//printf("came in here \n");
	int fd[2]; //file descriptor init
	//fd[0]-->READ
	//fd[1] -->WRTIE
	pipe(fd); //create pipe

	
	//fork to create parent and child process
	pid_t pid = fork();
	if(pid==0){ //child process --> need child process to write
		//printf("came into children \n");
		close(fd[0]); // close read 
		dup2(in_pipe_fd,STDIN_FILENO); //read from in_pipe_fd

		//printf("came before execlp \n");
		//printf("command being executed is: %s \n", input_command_strings[index_of_command]);
		if(input_command_strings[index_of_command+1] != NULL){
			dup2(fd[1], STDOUT_FILENO); //set stdout(ie have printf to send output to fd[1])
		}
		if(input_command_strings[index_of_command+1] == NULL){
			//close(in_pipe_fd);
			//printf("in children attempting to execute second cat command: %s \n", input_command_strings[index_of_command]);
		}

		//close(fd[0]);
		//close(fd[1]);
		error_checking(execlp(input_command_strings[index_of_command], input_command_strings[index_of_command], NULL), "excelp error when it is not the last command");
	}
	else{ 
		close(in_pipe_fd);
		close(fd[1]); //don't need to write as a parent

		if(input_command_strings[index_of_command+1] == NULL){ //reached end of commands
			int wait_status;
			waitpid(pid,&wait_status,0);
			WIFEXITED(wait_status);
			int child_exit_code = WEXITSTATUS(wait_status);

			if(child_exit_code!=0){
				return(child_exit_code);
			}
			return child_exit_code;
		}

		int exit_code = recur_pipe_call(input_command_strings,index_of_command+1,max_command_num,fd[0]);

		int wait_status;

		waitpid(pid,&wait_status,0);

		WIFEXITED(wait_status);

		int child_exit_code = WEXITSTATUS(wait_status);

		if(exit_code==0 && child_exit_code!=0){
			return child_exit_code;
		}
		else if(exit_code!=0 && child_exit_code==0){
			return exit_code;
		}
		else if(exit_code==0 && child_exit_code==0){
			return 0;
		}
		else if(exit_code!=0 && child_exit_code!=0){
			return child_exit_code;
		}
	}
	return 0;
	
}

int main(int argc, char *argv[]) {
	if(argc<2){
		printf("Not enough argumentss"); //one is for the program ./pipe , the other is the command to run
		return EINVAL;
	}
	else{
		int exit_code=recur_pipe_call(argv,1,argc, STDIN_FILENO);
		exit(exit_code);
	}
	return 0;
}
