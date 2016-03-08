#include "userapp.h"
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/time.h>
#include <stdlib.h>

pid_t PID;

const char* procfile_name = "/proc/mp2/status";

void register_process(int period, int comp_time) {
	FILE* procfile = fopen("/proc/mp2/status", "w");
	fprintf(procfile, "R, %d, %d, %d\n", PID, period, comp_time);
	fclose(procfile);
}

int read_status(void) {
	FILE* procfile = fopen("/proc/mp2/status", "r");
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

	if (pid_found) {
		return 1;
	} else {
		return 0;
	}
}


void yield_process(void) {
	FILE* procfile = fopen("/proc/mp2/status", "w");
	fprintf(procfile, "Y, %d\n", PID);
	fclose(procfile);
}


void deregister_process(void) {
	FILE* procfile = fopen("/proc/mp2/status", "w");
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
	register_process(period, job_process_time); //Proc filesystem

	//Proc filesystem: Verify the process was admitted if (!process in the list) exit 1;	
	if (!read_status()) {
		exit(1);
	}

	//setup everything needed for real-time loop: t0=gettimeofday() YIELD(PID); //Proc filesystem	
	struct timeval te;

	//this is the real-time loop
	int jobs = 5;
	while(--jobs)
	{
		//wakeup_time=gettimeofday()-t0 and factorial computation
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
			
		//Proc filesystem. JobProcessTime=gettimeofday()-wakeup_time }
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
