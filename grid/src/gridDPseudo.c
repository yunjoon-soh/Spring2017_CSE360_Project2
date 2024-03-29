#include "gridDPseudo.h"

#define SNOOPY_ADDR "127.0.0.1"

/*Grid Donor*/
int main(int argc, char** argv) {
	char* end_ptr; // used for strtol
	int opt_i = 0, opt_k = 0, opt_m = 0, opt_c = 0, opt_p = 0, opt_d = 0, opt, val = -1;
	long base = 10, val_m = -1, val_c = -1; // base for input number for m or c

	char exePath[1024];
	char** exeArgs = NULL;

	// unset the environment variable
	DEBUG(0, "%s\n", "Unloading previous LD_PRELOAD")
	// Note. no ; and this is intended
	// check gridD.h for declaration and detail

	unsetenv("LD_PRELOAD");

	while ( (opt = getopt(argc, argv, "ikm:c:dp:")) != -1 ) {
		switch (opt) {
		case 'i':
			opt_i = 1;
			break;
		case 'k':
			opt_k = 1;
			break;
		case 'm':
			// convert next option argument to long
			val = Strtol(optarg, &end_ptr, base);

			val_m = val;
			opt_m = 1;
			break;
		case 'c':
			// convert next option argument to long
			val = Strtol(optarg, &end_ptr, base);

			val_c = val;
			opt_c = 1;
			break;
		case 'd':
			opt_d++;
			break;
		case 'p':
			Strncpy(exePath + 2, optarg, 1022);
			exePath[0] = '.';
			exePath[1] = '/';
			opt_p = 1;
			break;
		default:
			fprintf(stderr, "%s\n", USAGE);
			exit(EXIT_FAILURE);
		}
	}

	DEBUG(1, "Option i: %d, Option k: %d, Option m: %d %ld, Option c: %d %ld, Option d: %d\n", opt_i, opt_k, opt_m, val_m, opt_c, val_c, opt_d)

	fprintf(stdout, "Starting Grid Donor... Version: %s (Build: %d)\n", SERVER_VERSION, BUILDS);

	fprintf(stdout, "Validating parameters...\n");

	// check if opt_p was used
	if (opt_p != 1) {
		fprintf(stderr, "Option p must be given! Abort execution.\n");
		exit(EXIT_FAILURE);
	} else if (opt_d >= 1) {
		fprintf(stderr, "[DEBUG 1]Option p given: %s\n", exePath);
	}

	// Setup libcall intercept, i.e., export LD_PRELOAD and also unsetenv("LD_PRELOAD") so the child will not know
	// Setup syscall intercept
	// For lib calls, intercept using LD_PRELOAD trick

	/*
	1. Start by spawning the Snoopy's program

	2. Run it inside seccomp
	z
	3. Intercept
	Whenever Snoopy's program makes a libcall, if it is our interest, intercept it and send to helper thread
	Whenever Snoopy's program makes a system call, intercept it and send to helper thread

	Helper thread will then either perform correct actions or communicate with the server on behalf of the Snoopy's program

	4. If necessayr, helperPid waits for the server's response and execute according to the server's response inside the interceptor
	*/

	if (opt_d >= 1) {
		char *ldPath = getenv("LD_PRELOAD");
		fprintf(stderr, "LD_PRELOAD=%s\n", ldPath);
	}

	// setenv("LD_PRELOAD", HOOK_LIB_PATH, 1);

	int childPid = -1, status = -1;

	/*Snoopy Program*/
	if ( (childPid = Fork()) == 0) {
		DEBUG(1, "%s\n", "Child process starting...\n");

		int ret = setenv("LD_PRELOAD", HOOK_LIB_PATH, 1);
		if (ret == -1)
			perror("setenv");

		// install seccomp filter
		 if (install_syscall_filter()){
			DEBUG(1, "%s\n", "install_syscall_filter failed, aborting");
			exit(EXIT_FAILURE);
		 } else{
			DEBUG(1, "%s\n", "install_syscall_filter success");
		 }

		// When input data is passed on to execv()
		// Trace for syscalls
		ptrace(PTRACE_TRACEME, 0, NULL, NULL);

		//Child process execl
		execv(exePath, exeArgs);
	}
	else {

		// Communicate with the server for input data
		// Note. User of gridD decides which program to run, not the Snoopy
		long orig_eax, eax;
		int insyscall = 0;
		long params[3];
		char sendbuf[PAGE_SIZE];
		char rBuf[PAGE_SIZE];
		char recvBuff[PAGE_SIZE];

		int read_ret, write_ret, inner_fd;

		while (1) {
			Waitpid(childPid, &status, 0);

			DEBUG(2, "Child process(pid=%d): status: %d\n", childPid, status);
			if (WIFEXITED(status))
				break;
			orig_eax = ptrace(PTRACE_PEEKUSER, childPid, 4 * ORIG_EAX, NULL);
			DEBUG(2, "Intercepted %ld\n", orig_eax);

			if (orig_eax == SYS_open ) {
				//int open(const char *pathname, int flags);
				if (insyscall == 0) {
					/* Syscall entry */
					insyscall = 1;
					params[0] = ptrace(PTRACE_PEEKUSER, childPid, 4 * EBX, NULL);
					params[1] = ptrace(PTRACE_PEEKUSER, childPid, 4 * ECX, NULL);
					DEBUG(1, "Open called with %ld, %ld\n", params[0], params[1]);

					// concatenate strings for sendbuf
					memset(sendbuf, 0, PAGE_SIZE);
					strcat(sendbuf, "open,");
					strcat(sendbuf, peekData(params[0])); // peekData returns the content of the address location

					// inform server that Snoopy program is opening a file
					int res = fopenConnection(sendbuf, SNOOPY_ADDR, recvBuff);

					// if server was successful opening it sending back the file information
					if(res){
						// syscall will open a SUCCESS_DUMMY_FILE
						// SUCCESS_DUMMY_FILE is just a temporary file that exists
						// this will allow for the syscall to execute successfully
						modifyArg0(SUCCESS_DUMMY_FILE, childPid);

						// create dummy inner_fd
						inner_fd = getAvailableFd(peekData(params[0]));

						// add to key-value structure, so that it can be referenced later
						void* page = addKeyValuePair(inner_fd);

						// save the page to malloc table
						memcpy(page, recvBuff, PAGE_SIZE);

					} else {
						eax = ptrace(PTRACE_PEEKUSER, childPid, 4 * EAX, NULL);
						DEBUG(2, "Open returned with %ld\n", eax);
						insyscall = 0;

						// syscall will open a FAILURE_DUMMY_FILE
						// FAILURE_DUMMY_FILE is just a file that does not exists
						// this will fail the syscall
						modifyArg0(FAILURE_DUMMY_FILE, childPid);
					}
				}
				else { /* Syscall exit */
					eax = ptrace(PTRACE_PEEKUSER, childPid, 4 * EAX, NULL);
					DEBUG(1, "Open returned with %ld\n", eax);
					insyscall = 0;

					modifyReturnValue(inner_fd, childPid);
				}
			}
			else if (orig_eax == SYS_read ) {
				// ssize_t read(int fd, void *buf, size_t count);
				if (insyscall == 0) {
					/* Syscall entry */
					insyscall = 1;
					params[0] = ptrace(PTRACE_PEEKUSER, childPid, 4 * EBX, NULL);
					params[1] = ptrace(PTRACE_PEEKUSER, childPid, 4 * ECX, NULL);
					params[2] = ptrace(PTRACE_PEEKUSER, childPid, 4 * EDX, NULL);
					DEBUG(1, "Read called with %ld, %ld, %ld\n", params[0], params[1], params[2]);

					// based on inner fd, return the memory address of it
					void* inner_addr = getMappedAddr(params[0]);

					// get how many bytes to read
					long nread = params[2];

					// total read bytes
					long read_ret = 0;

					// byte copy of the memory that contains the file content and 
					for(long i = 0; i < nread; i += PAGE_SIZE){

						// retrieve the partial file content
						retrievePageOfFile(inner_addr, (char**) &rBuf); // rBuf is page size

						for(long j = 0; i + j < nread; j++){
							// dereference the params[i], i.e., the untrusted program's buffer to read
							*( (char*) params[i + j] ) =  rBuf[j];

							// set return value of read to number of bytes read
							read_ret++;
						}
					}
				}
				else { /* Syscall exit */
					eax = ptrace(PTRACE_PEEKUSER, childPid, 4 * EAX, NULL);
					DEBUG(2, "Read returned with %ld\n", eax);
					insyscall = 0;

					// modify the return value to be number of bytes read
					modifyReturnValue(read_ret, childPid);
				}
			}
			else if (orig_eax == SYS_write ) {
				// ssize_t write(int fd, const void *buf, size_t count);
				if (insyscall == 0) {
					/* Syscall entry */
					insyscall = 1;
					params[0] = ptrace(PTRACE_PEEKUSER, childPid, 4 * EBX, NULL);
					params[1] = ptrace(PTRACE_PEEKUSER, childPid, 4 * ECX, NULL);
					params[2] = ptrace(PTRACE_PEEKUSER, childPid, 4 * EDX, NULL);
					DEBUG(1, "Write called with %ld, %ld, %ld\n", params[0], params[1], params[2]);

					// concatenate strings for sendbuf
					memset(sendbuf, 0, PAGE_SIZE);
					strcat(sendbuf, "write,");
					strncat(sendbuf, peekData(params[0]), params[2]); // bug when params[2] > PAGE_SIZE - 6

					// inform server what to write
					write_ret = fopenConnection(sendbuf, SNOOPY_ADDR, recvBuff);
				}
				else { /* Syscall exit */
					eax = ptrace(PTRACE_PEEKUSER, childPid, 4 * EAX, NULL);
					DEBUG(2, "Write returned with %ld\n", eax);
					insyscall = 0;

					modifyReturnValue(write_ret, childPid);
				}
			}
			else if (orig_eax == SYS_close ) {
				// int close(int fd);
				if (insyscall == 0) {
					/* Syscall entry */
					insyscall = 1;
					params[0] = ptrace(PTRACE_PEEKUSER, childPid, 4 * EBX, NULL);

					DEBUG(1, "Close called with %ld, %ld, %ld\n", params[0], params[1], params[2]);

					// concatenate strings for sendbuf
					strcat(sendbuf, "close,");
					strcat(sendbuf, peekData(params[0]));

					// request server to close
					fopenConnection(sendbuf, SNOOPY_ADDR, recvBuff);

					// clean up local paired memory
					void* memFile = getMappedAddr(params[0]);
					free(memFile);
				}
				else { /* Syscall exit */
					eax = ptrace(PTRACE_PEEKUSER, childPid, 4 * EAX, NULL);
					DEBUG(2, "Close returned with %ld\n", eax);
					insyscall = 0;
				}
			}
			else {
				DEBUG(1, "Child made syscall #%ld\n", orig_eax);
			}
			ptrace(PTRACE_SYSCALL, childPid, NULL, NULL);

			// 0. Intercept Syscall and libcalls
			// 1. Communicates with the Snoopy's program
			// 2. Communicates with the gridServer
			// So this needs two bidirectional connections
		}
	}

	// wait for all child processes
	Waitpid(childPid, &status, 0);

	DEBUG(1, "Child process(pid=%d) ended with status=%d\n", childPid, status);


	exit(EXIT_SUCCESS);
}

static int install_syscall_filter(void) {
	struct sock_filter filter[] = {
		/* Validate architecture. */
		VALIDATE_ARCHITECTURE,
		/* Grab the system call number. */
		EXAMINE_SYSCALL,
		/* List allowed syscalls. */
		ALLOW_SYSCALL(rt_sigreturn),
#ifdef __NR_sigreturn
		ALLOW_SYSCALL(sigreturn),
#endif
		ALLOW_SYSCALL(exit_group),
		ALLOW_SYSCALL(exit),
		// ALLOW_SYSCALL(read),
		// ALLOW_SYSCALL(write),
		// ALLOW_SYSCALL(open),
		//KILL_PROCESS, @ Daniel I think this was a major problem.  It was killing any process who used a syscall that wasn't declared allowed.  Now it is done where it allows everything UNLESS Blacklisted.
		// use BLACKLIST(name_of_syscall) to blacklist a syscall
		BLACKLIST(exit),
		RETURN_ALLOW,
	};
	struct sock_fprog prog = {
		.len = (unsigned short)(sizeof(filter) / sizeof(filter[0])),
		.filter = filter,
	};

	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) {
		perror("prctl(NO_NEW_PRIVS)");
		goto failed;
	}
	if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog)) {
		perror("prctl(SECCOMP)");
		goto failed;
	}
	return 0;

failed:
	if (errno == EINVAL)
		fprintf(stderr, "SECCOMP_FILTER is not available. :(\n");
	return 1;
}

// from https://github.com/alfonsosanchezbeato/ptrace-redirect/blob/master/redir_filter.c
static void redirect_file(pid_t child, const char *file)
{
	char *stack_addr, *file_addr;

	stack_addr = (char *) ptrace(PTRACE_PEEKUSER, child, sizeof(long) * RSP, 0);
	
	/* Move further of red zone and make sure we have space for the file name */
	stack_addr -= 128 + PATH_MAX;
	file_addr = stack_addr;

	/* Write new file in lower part of the stack */
	do {
		unsigned int i;
		char val[sizeof (long)];

		for (i = 0; i < sizeof (long); ++i, ++file) {
			val[i] = *file;
			if (*file == '\0') break;
		}

		ptrace(PTRACE_POKETEXT, child, stack_addr, *(long *) val);
		stack_addr += sizeof (long);
	} while (*file);

	/* Change argument to open */
	ptrace(PTRACE_POKEUSER, child, sizeof(long) * RDI, file_addr);
}

static int modifyArg0(void* FAILURE_DUMMY_FILE, int childPid){
	//TODO
	return -1;
}

static void modifyReturnValue(long value, int childPid){
	//TODO
}

static char* peekData(long value){
	//TODO

	return NULL;
}