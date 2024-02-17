#include "common.h"
#include "math.h"
#include "stdio.h"
#include "stdlib.h"


//recursion routine
int recursive(int number){
	if(number==1 || number==0){
		return 1;
	}
	else{
		return number* recursive(number-1);
	}

}

int
main(int argc, char** argv)
{	
	//take in argument from command line
	double number;
	char *endptr;
	number=strtod(argv[1], &endptr);
	if(*endptr != '\0'){
		printf("Huh?\n");
	}
	else if((number-floor(number))!= 0 || number<=0){
		printf("Huh?\n");
	}
	else if(number>12){
		printf("Overflow\n");
	}
	else{
		printf("%d\n", recursive((int) number));
	}

	return 0;
}
