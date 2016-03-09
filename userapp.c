#include "userapp.h"
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/time.h>
#include <stdlib.h>

pid_t PID;

const char* procfile_name = "/proc/mp2/status";

//Attempt to register process to procfile
void register_process(int period, int comp_time) {
	FILE* procfile = fopen(procfile_name, "w");
	fprintf(procfile, "R, %d, %d, %d\n", PID, period, comp_time);
	fclose(procfile);
}

/**
 * Read from procfile and determine if this process
 * was registered properly
 */
int was_registered(void) {
	FILE* procfile = fopen(procfile_name, "r");
	char line[256];
	
	int line_pid;
	int pid_found = 0;
	while (fgets(line, sizeof(line), procfile)) {
    	sscanf(line, "%d", &line_pid);
	    
		if (line_pid == PID){
			pid_found = 1;		
		}
    }
	fclose(procfile);

	return pid_found;
}


void yield_process(void) {
	FILE* procfile = fopen(procfile_name, "w");
	fprintf(procfile, "Y, %d\n", PID);
	fclose(procfile);
}


void deregister_process(void) {
	FILE* procfile = fopen(procfile_name, "w");
	fprintf(procfile, "D, %d\n", PID);
	fclose(procfile); 
}

//compute factorials recursively
long factorial(int n) {
    if(n <= 1) {
        return 1;
    } else {
    	return ((long) n) * factorial(n-1);
	}
}

int main(int argc, char* argv[]) {
	PID = getpid();

	int period = atoi(argv[1]);
	int job_process_time = atoi(argv[2]);
	register_process(period, job_process_time);

	if (!was_registered()) {
		printf("Process %d could not be registered.\n", PID);
		exit(1);
	}

	//setup everything needed for real-time loop: t0=gettimeofday() YIELD(PID); //Proc filesystem	
	struct timeval te;

	//this is the real-time loop
	int jobs = 5;
	while(jobs--)
	{
		gettimeofday(&te, NULL);
		long long sleep_time = (te.tv_sec*1000LL + te.tv_usec/1000);

		yield_process();

		gettimeofday(&te, NULL);
		long long wakeup_time = (te.tv_sec*1000LL + te.tv_usec/1000);
		printf("Process %d slept for %d ms\n", PID, (int) (wakeup_time - sleep_time));

		//calculate lots of factorials for cpu time
     	int i;
		for(i = 1; i < 20000000; i++) {
	    	factorial(20);
 	    }		
			
		/*
		gettimeofday(&te, NULL);
		long long milliseconds = te.tv_sec*1000LL + te.tv_usec/1000;
		int jobProcessTime = milliseconds - sleep_time;		

		printf("Process %d took %d ms\n", PID, jobProcessTime);   	
		*/
	}
	
	yield_process();
	
	deregister_process(); //Proc filesystem
}
