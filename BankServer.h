/* BankServer.h
 * Header file for the bank server implementation
 */
#ifndef BANK_SERVER_H
#define BANK_SERVER_H

#define _GNU_SOURCE /* Required for various GNU extensions */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <time.h>

#include "bank_shared.h"
#include "bank_utils.h"


/* Structure to track batch operations */
typedef struct {
    pid_t pid;        /* Client process PID */
    int total;        /* Total operations in batch */
    int received;     /* Operations received so far */
} BatchInfo;

/* Structure for teller arguments */
struct TellerArgs {
    ClientRequest client_req;
    int pipe_read;
    int pipe_write;
};

/* Bank account structure */
typedef struct {
    char bankId[20];
    int balance;
    int active;
} Account;

/* Bank database structure */
typedef struct {
    Account accounts[MAX_BATCH_SIZE];  /* Array of accounts (max 100 clients) */
    int numAccounts;        /* Current number of accounts */
} BankDatabase;

/* Teller to Server message for database operations */
typedef struct {
    int operation;          /* OP_DEPOSIT or OP_WITHDRAW */
    char bankId[20];        /* Account ID */
    int amount;             /* Amount to deposit/withdraw */
    int isNewClient;        /* Flag indicating if this is a new client */
    pid_t clientPid;        /* Client PID (for response) */
    int clientIndex;        /* Client index for display */
} TellerRequest;

/* Function prototypes */

/* Custom process creation/waiting functions */
pid_t Teller(void* func, void* arg_func);
int waitTeller(pid_t pid, int* status);

/* Server initialization and cleanup */
void initializeServer(char *argv[], const char *bankName, const char *fifoName);
void cleanupServer(void);

/* Signal handlers */
void handleSignal(int sig);
void handleChildSignal(int sig);
void setupTellerSignals(void);

/* Client number handling */
int extractClientNumber(const char *bankId);

/* Client connection handling */
void waitForClients(void);
void resetBatchInfo(BatchInfo *batch);
void processBatch(void);
void processDatabaseRequest(TellerRequest *req, ServerResponse *resp, int clientNum);

/* Teller functions */
void *tellerProcess(void *arg, int isDeposit);
void *depositTeller(void *arg);
void *withdrawTeller(void *arg);

/* Database operations - only accessed by main server */
void initializeDatabase(void);
int findAccount(const char *bankId);
int createAccount(int amount);
int depositToAccount(const char *bankId, int amount);
int withdrawFromAccount(const char *bankId, int amount);
void removeAccount(const char *bankId);

/* Helper functions */
void printServerStatus(void);

/* Global variable declarations (extern) */
extern FILE *logFile;
extern char serverFifo[SERVER_FIFO_NAME_LEN];
extern int serverFd, dummyFd;
extern BankDatabase bankDb;  /* Now a regular structure, not a pointer */
extern int activeClients;
extern int lastClientId;
extern char bankName[50];
extern sem_t *serverSem;
extern BatchInfo currentBatch;
extern ClientRequest batchRequests[MAX_BATCH_SIZE];

#endif /* BANK_SERVER_H */