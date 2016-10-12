#include<stdio.h>
#include<unistd.h>
#include<signal.h>
#include<fcntl.h>
#include<sys/shm.h>
#include<sys/stat.h>
#include<sys/mman.h>

#define DEBUG 1
#define BLOCKSIZE 4096

int main(int argc, char* argv[]) {

	//Constant name for the shared memory space
	const char* name = "jse13_cop4610";

	//If both an input and an output aren't given, exit
	if(argc != 3) {
		fprintf(stderr, "Error: not enough parameters.\n");
		return 1;
	}
	//Make sure input file exists and is readable
	else if(access(argv[1], F_OK|R_OK) == -1) {
		fprintf(stderr, "Error: input doesn't exist or is not readable.\n");
		return 1;
	}
	//Make sure output file exists and is writable
	else if(access(argv[2], F_OK|W_OK) == -1) {
		fprintf(stderr, "Error: output doesn't exist or is not writable.\n");
		return 1;
	}

	//Make two pipes
	int toChild[2];
	int toParent[2];


	if(pipe(toChild) != 0) {
		fprintf(stderr, "Error: pipe to child process could not be made.\n");
		return 1;
	}
	else if(pipe(toParent) != 0) {
		fprintf(stderr, "Error: pipe to parent process could not be made.\n");
		return 1;
	}
	
	//Fork process
	pid_t child;

	//If in child
	if((child = fork()) == 0) {
		//close the read end of toParent and the write end of toChild
		close(toParent[0]);
		close(toChild[1]);

		//Wait for parent to make file, then open it
		//Note that by default read will block until there is input
		int childSignal;
		while(read(toChild[0], &childSignal, 1) == 1 ) {

			//Open the shared memory
			int childShm = shm_open(name, O_RDONLY, 0666);
			if(childShm == -1) {
				fprintf(stderr, "Error: child unable to open shared memory.\n");
			}

		}
	}
	else if(child == (pid_t) (-1)) {
		fprintf(stderr, "Error: fork failed.\n");
	}
	//Else in parent
	else {
		//Close the read end of toChild and the write end of toParent
		close(toParent[1]);
		close(toChild[0]);

		//Make shared memory space
		int parentShm;
		parentShm = shm_open(name, O_CREAT | O_RDWR, 0666);

		//Check for failure making the shared memory space
		if(parentShm == -1) {
			fprintf(stderr, "Error: failed to create shared memory.\n");
			//Kill child process
			kill(child, SIGKILL);
			return -1;
		}

		//Resize the shared memory space
		ftruncate(parentShm, BLOCKSIZE);

		//Map the shared memory in the process address space
		void* shMem = mmap(0, BLOCKSIZE, PROT_WRITE, MAP_SHARED, parentShm, 0);
		//If map failed, error out
		if(shMem == MAP_FAILED) {
			fprintf(stderr, "Error: failed to map shared memory in parent.\n");
			kill(child, SIGKILL);
			return -1;
		}

		int parentSignal = 1;

		//Send a signal to child to indicate that it can open the shared mem
		write(toChild[1], &parentSignal, 1);


	}

	return 0;
}
