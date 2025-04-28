/* BankClient.h
 * Header file for the bank client implementation
 */
#ifndef BANK_CLIENT_H
#define BANK_CLIENT_H

/* Feature test macros must come before any header file inclusions */
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _POSIX_C_SOURCE 199309L 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <semaphore.h>
#include <time.h>

#include "bank_shared.h"
#include "bank_utils.h"


/* Structure to store client information */
typedef struct {
    char operation[10];     /* "deposit" or "withdraw" */
    int amount;             /* Amount to deposit or withdraw */
    char bankId[20];        /* BankID for existing clients, "N" for new clients */
} ClientOperation;

/* Function prototypes */

/* Client initialization and cleanup */
void initializeClient(const char *fifoName);
void cleanupClient(void);

/* Signal handlers */
void handleSignal(int sig);

/* Client file parsing */
int parseClientFile(const char *filename);
void parseClientLine(char *line, ClientOperation *op);

/* Operations */
void sendOperationBatch(void);
void processResponse(ServerResponse *resp, ClientOperation *op, int clientIndex);

/* Helper functions */
int isNewClient(const char *bankId);

/* Global variable declarations (extern) */
extern char serverFifo[SERVER_FIFO_NAME_LEN];
extern int serverFd;
extern ClientOperation *operations;
extern int numOperations;
extern sem_t *clientSem;
extern int currentOpIndex;

#endif /* BANK_CLIENT_H */