#include<stdio.h>
#include<unistd.h>
#include<signal.h>
#include<fcntl.h>
#include<sys/shm.h>
#include<sys/stat.h>
#include<sys/mman.h>

#define DEBUG 0
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
    //Make sure output file is writable; if it doesn't exist then we'll make it
    else if(access(argv[2], W_OK) == -1) {
        if(access(argv[2], F_OK) != -1) 
            fprintf(stderr, "Error: output is not writable.\n");
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
    /*************************************************************************\
     * Child Process
     **************************************************************************/
    if(DEBUG == 1) {
        fprintf(stderr, "Forking...\n");
    }
    if((child = fork()) == 0) {
        int childSignal;     //Signal that child sends to parent, holds block count
        int childBlockCount; //Block count received from parent

        int   childFd;        //File descriptor for shared memory
        char* childShm;       //Shared memory segment pointer
        int   outFile;        //Fd for output file

        int writing = 1;      //Setinel for main loop

        if(DEBUG == 1) {
            fprintf(stderr, "Inside of child process.\n");
        }
        //close the read end of toParent and the write end of toChild
        close(toParent[0]);
        close(toChild[1]);

        //Block until parent says it's okay to map shared memory
        read(toChild[0], &childSignal, sizeof(int));

        //Open the shared memory
        childFd = shm_open(name, O_RDONLY, 0666);
        if(childFd == -1) {
            fprintf(stderr, "Error: child unable to open shared memory.\n");

            //Tell parent an error was encountered
            childSignal = -2;
            write(toParent[1], &childSignal, sizeof(int));

            return 1;
        }
        else if(DEBUG == 1) {
            fprintf(stderr, "Child: successfully opened shared memory.\n");
        }

        //Map the shared memory in the process address space
        childShm = mmap(0, BLOCKSIZE, PROT_READ, MAP_SHARED, childFd, 0);
        //If map failed, error out
        if(childShm == MAP_FAILED) {
            fprintf(stderr, "Error: failed to map shared memory in child.\n");

            //Tell parent an error was encountered
            childSignal = -2;
            write(toParent[1], &childSignal, sizeof(int));

            return -1;
        }
        else if(DEBUG == 1) {
            fprintf(stderr, "Child: successfully mapped shared memory.\n");
        }


        //Open file for writing
        outFile = open(argv[2], O_WRONLY|O_CREAT, S_IRUSR|S_IWUSR);
        if(outFile == -1) {
            fprintf(stderr, "Failed to open output file.\n");

            childSignal = -2;
            write(toParent[1], &childSignal, sizeof(int));

            return -1;
        }


        //Signal parent that child is ready to recieve input
        childSignal = -1;
        write(toParent[1], &childSignal, sizeof(int));


        //Main loop: wait for parent confirmation, copy data
        while(writing == 1) {

            //Read in block number
            read(toChild[0], &childSignal, sizeof(int));

            //Read in block count
            read(toChild[0], &childBlockCount, sizeof(int));

            if(DEBUG == 1) {
                fprintf(stderr, "Child: received block number of %d and \
block size of %d.\n", childSignal, childBlockCount);
            }

            //If both are 0, respond with 0 and exit
            if(childSignal == 0 && childBlockCount == 0) {

                if(DEBUG == 1) {
                    fprintf(stderr, "Child: received 0 and 0, exiting...\n");
                }

                writing = 0;
                childSignal = 0;
                write(toParent[1], &childSignal, sizeof(int));

            }
            //Otherwise, copy from shm to output file
            else {
                //If writing fails, notify parent
                if (write(outFile, childShm, childBlockCount) == -1) {

                    writing = 0;
                    childSignal = -2;
                    write(toParent[1], &childSignal, sizeof(int));
                }
                else {
                    //Respond to parent with block count
                    write(toParent[1], &childSignal, sizeof(int));
                }

            }
        }

        //Exit stuff: unmap memory
        if(munmap(childShm, BLOCKSIZE) == -1) {
            return -1;
        }

        //Exit stuff: close shared memory
        if(close(childFd) == -1) {
            return -1;
        }

        //Exit stuff: close output file
        if(close(outFile) == -1) {
            return -1;
        }

        return 0;

    }
    else if(child == (pid_t) (-1)) {
        fprintf(stderr, "Error: fork failed.\n");
    }

    /*************************************************************************\
     * Parent Process
     **************************************************************************/
    else {

        int   parentFd;        //File descriptor for shared memory
        char* parentShm;       //Shared memory segment pointer

        int   parentSignal;    //used to signal child and to hold block count
        int   fromChild;       //Value pulled from child pipe
        int   parentBlockSize; //holds block size to send to child
        int   inFile;         //Fd for input file
        
        int   reading = 1;     //Setinel for main read loop

        //Close the read end of toChild and the write end of toParent
        close(toParent[1]);
        close(toChild[0]);

        if(DEBUG == 1) {
            fprintf(stderr, "In parent process.\n");
        }

        //Create shared memory

        parentFd = shm_open(name, O_CREAT | O_RDWR, 0666);

        //Check for failure making the shared memory space
        if(parentFd == -1) {
            fprintf(stderr, "Error: failed to create shared memory.\n");
            //Kill child process
            kill(child, SIGKILL);
            return -1;
        }
        else if(DEBUG == 1) {
            fprintf(stderr, "Parent: successfully created shared memory.\n");
        }

        //Resize the shared memory space
        ftruncate(parentFd, BLOCKSIZE);

        //Map the shared memory in the process address space
        parentShm  = mmap(0, BLOCKSIZE, PROT_WRITE, MAP_SHARED, parentFd, 0);
        //If map failed, error out
        if(parentShm == MAP_FAILED) {
            fprintf(stderr, "Error: failed to map shared memory in parent.\n");
            kill(child, SIGKILL);
            return -1;
        }
        else if(DEBUG == 1) {
            fprintf(stderr, "Parent: successfully mapped shared memory.\n");
        }

        
        //Let the child know it can map the shared memory segment
        parentSignal = 1;
        if(DEBUG == 1) {
            fprintf(stderr, "Parent: writing to child.\n");
        }
        write(toChild[1], &parentSignal, sizeof(int));

        //Block until child indicates it's ready to receive input
        read(toParent[0], &fromChild, sizeof(int));
        if(fromChild == -2) {
            if(DEBUG == 0) {
                fprintf(stderr, "Parent: child encountered an error, exiting...\n");
            }

            return -1;
        }


        //Main loop: get info from input file, write to shm, signal child
        //Open input file
        inFile = open(argv[1], O_RDONLY);
        if(inFile == -1) {
            fprintf(stderr, "Failed to open input file.\n");

            //Never enter main loop
            reading = 0;
        }

        parentSignal = 1; //Block count starts at 1
        while(reading == 1) {

            if(DEBUG == 1) {
                fprintf(stderr, "Parent: on block %d.\n", parentSignal);
            }

            //Read in from file to shared memory
            parentBlockSize = read(inFile, parentShm, BLOCKSIZE);

            //If it doesn't read in BLOCKSIZE items, exit the loop
            if(parentBlockSize != BLOCKSIZE) {
                reading = 0;
            }

            //signal child
            if(DEBUG == 1) {
                fprintf(stderr, "Parent: sending block count of %d and block \
size of %d.\n", parentSignal, parentBlockSize);
            }
            write(toChild[1], &parentSignal, sizeof(int)); //Write buffer count
            write(toChild[1], &parentBlockSize, sizeof(int)); //Write buffer count


            //Block for child response
            read(toParent[0], &fromChild, sizeof(int));
            if(fromChild != parentSignal) {
                //If child doesn't respond with the proper block num
                reading = 0;
            }
            if(DEBUG == 1) {
                fprintf(stderr, "Parent: child responded with %d.\n", fromChild);
            }

            //Increment block count
            parentSignal++;

        } 

        if(DEBUG == 1) {
            fprintf(stderr, "Parent: leaving main loop.\n");
        }


        parentSignal = 0;
        parentBlockSize = 0;

        //After loop, tell child to exit
        write(toChild[1], &parentSignal, sizeof(int)); //Write buffer count
        write(toChild[1], &parentBlockSize, sizeof(int)); //Write buffer count

        //Block until child acknowledges
        read(toParent[0], &fromChild, sizeof(int));
        if(fromChild != 0) {
            //TODO: if child doesn't return 0, something is wrong
        }    

        //Exit stuff: unmap memory
        if(munmap(parentShm, BLOCKSIZE) == -1) {
            return -1;
        }

        //Exit stuff: close shared memory
        if(close(parentFd) == -1) {
            return -1;
        }

        //Exit stuff: close input file
        if(close(inFile) == -1) {
            return -1;
        }

        //Exit stuff: delete shared memory
        if(shm_unlink(name) == -1) {
            fprintf(stderr, "Parent: could not delete shared memory space.\n");
            return -1;
        }
       
    }

    return 0;
}
