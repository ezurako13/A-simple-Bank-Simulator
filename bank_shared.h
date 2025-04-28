/* bank_shared.h
 * Shared definitions between client and server
 */
#ifndef BANK_SHARED_H
#define BANK_SHARED_H

#include <sys/types.h>
#include <semaphore.h>

/* FIFO paths - using /tmp directory for WSL compatibility */
#define SERVER_FIFO_TEMPLATE "/tmp/%s"
#define SERVER_FIFO_NAME_LEN 64
#define CLIENT_FIFO_TEMPLATE "/tmp/bank_cl_%ld"
#define CLIENT_FIFO_NAME_LEN 64

/* Permissions for the FIFOs */
#define FIFO_PERM (S_IRUSR | S_IWUSR | S_IWGRP)

/* Operation codes */
#define OP_DEPOSIT  1
#define OP_WITHDRAW 2

/* Special message types */
#define MSG_OPERATION 0
#define MSG_BATCH_INFO 1

/* Message structures */
typedef struct {
    pid_t pid;                  /* Client's PID */
    int msgType;                /* Message type (operation or batch info) */
    int op;                     /* Operation code (deposit/withdraw) */
    int amount;                 /* Amount to deposit/withdraw */
    char bankId[20];            /* Bank ID for existing clients */
    int isNewClient;            /* Flag indicating if this is a new client */
    int batchSize;              /* Number of operations in this batch */
    int operationIndex;         /* Index of this operation in the batch (1-based) */
} ClientRequest;

typedef struct {
    int status;                 /* Status code (0 for success, negative for error) */
    int balance;                /* Current account balance after operation */
    char bankId[20];            /* Bank ID assigned to the client */
    char message[100];          /* Status or error message */
    int clientIndex;            /* Client index number for display */
} ServerResponse;

/* Error codes */
#define ERR_INSUFFICIENT_FUNDS -1
#define ERR_INVALID_OPERATION -2
#define ERR_INVALID_ACCOUNT -3

#endif /* BANK_SHARED_H */