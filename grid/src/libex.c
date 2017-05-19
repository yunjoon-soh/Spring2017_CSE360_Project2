#include "libex.h"

int puts(const char *message)
{
	int (*old_puts)(const char *message);
	int result;

	// save original puts
	old_puts = dlsym(RTLD_NEXT, "puts");

	// If the message is Hello world!
	if (strcmp(message, "Hello world!\n") == 0)
	{
		// Print the following, using original puts
		result = old_puts("Goodbye, cruel world!n");
	}
	else
	{
		// if the string is different, use original puts() to echo
		result = old_puts(message);
	}
	return result;
}

FILE *fopen(const char *path, const char *mode) {
	printf("Hooked fopen\n");
	int (*old_fopen)(const char *path, const char *mode);

	// save original fopen
	old_fopen = dlsym(RTLD_NEXT, "fopen");

	char sendMessage[1024];
	memset(sendMessage, 0, sizeof(sendMessage));
	int pathLen = strlen(path);
	// int modeLen = strlen(mode);
	int index = 0;

	// create message to send
	strncpy(&sendMessage[index], "fopen,", 6);
	index += 6;
	strncpy(&sendMessage[index], path, pathLen);
	index += pathLen;
	// printf("path: %s, pathLen:%d, mode: %s, modeLen:%d index:%d\n", path,pathLen, mode, modeLen, index);
	sendMessage[index] = '\0';

	printf("[lib.so]Sending the current message: %s\n", sendMessage);
	fopenConnection(sendMessage, "127.0.0.1");

	return NULL;
}

int fclose(FILE *stream) {
	printf("Hooked fclose\n");
	int (*old_fclose)(FILE * stream);

	// save original fclose
	old_fclose = dlsym(RTLD_NEXT, "fclose");

	if (stream != NULL) {
		old_fclose(stream);
	}

	return 0;
}