/* BankClient.c
 * Implementation of the bank client
 */
#include "BankClient.h"
#include <unistd.h>

/* Global variables */
char serverFifo[SERVER_FIFO_NAME_LEN];
int serverFd = -1;
ClientOperation *operations = NULL;
int numOperations = 0;
sem_t *clientSem = NULL;

/* Current operation index for better display */
int currentOpIndex = 0;

/* Main function */
int main(int argc, char *argv[]) {
    /* Check command line arguments */
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <client_file> #ServerFIFO_Name\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    /* Initialize the client */
    initializeClient(argv[2]);
    
    /* Parse the client file */
    int numClients = parseClientFile(argv[1]);
    if (numClients <= 0) {
        fprintf(stderr, "Error: No valid operations found in client file\n");
        cleanupClient();
        exit(EXIT_FAILURE);
    }
    
    printf("Reading %s..\n", argv[1]);
    printf("%d clients to connect.. creating clients..\n", numClients);
    
    /* Connect to the bank server */
    serverFd = open(serverFifo, O_WRONLY);
    if (serverFd == -1) {
        fprintf(stderr, "Cannot connect %s...\nexiting..\n", serverFifo);
        cleanupClient();
        exit(EXIT_FAILURE);
    }
    
    printf("Connected to Adabank..\n");
    
    /* Send all operations in batch mode */
    sendOperationBatch();
    
    printf("exiting..\n");
    
    /* Clean up resources */
    cleanupClient();
    
    return 0;
}

/* Client initialization and cleanup */
void initializeClient(const char *fifoName) {
    /* Set up signal handler */
    struct sigaction sa;
    sa.sa_handler = handleSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    if (sigaction(SIGINT, &sa, NULL) == -1 || 
        sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
    
    /* Set up the server FIFO name - properly use the template */
    snprintf(serverFifo, SERVER_FIFO_NAME_LEN, SERVER_FIFO_TEMPLATE, fifoName);
}

void cleanupClient(void) {
    /* Close server FIFO */
    if (serverFd != -1) close(serverFd);
    
    /* Remove any client FIFOs we created */
    for (int i = 0; i < numOperations; i++) {
        char clientFifo[CLIENT_FIFO_NAME_LEN];
        snprintf(clientFifo, CLIENT_FIFO_NAME_LEN, CLIENT_FIFO_TEMPLATE "_%d", 
                 (long)getpid(), i + 1);
        unlink(clientFifo);
    }
    
    /* Clean up semaphore */
    if (clientSem != NULL && clientSem != SEM_FAILED) {
        sem_close(clientSem);
        sem_unlink(pidToString(getpid()));
    }
    
    /* Free the operations array */
    free(operations);
}

/* Signal handlers */
void handleSignal(int sig) {
    (void)sig; /* Suppress unused parameter warning */
    int savedErrno = errno;
    
    /* Clean up resources */
    cleanupClient();
    
    errno = savedErrno;
    exit(EXIT_SUCCESS);
}

/* Client file parsing */
int parseClientFile(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }
    
    /* Count number of lines in the file */
    char line[256];
    int lineCount = 0;
    
    while (fgets(line, sizeof(line), file) != NULL) {
        /* Skip comment lines that start with # */
        if (line[0] == '#' || strlen(line) <= 1) {
            continue;
        }
        lineCount++;
    }
    
    /* Allocate memory for operations */
    operations = (ClientOperation *)malloc(lineCount * sizeof(ClientOperation));
    if (operations == NULL) {
        perror("malloc");
        fclose(file);
        exit(EXIT_FAILURE);
    }
    
    /* Reset file position to beginning */
    rewind(file);
    
    /* Parse each line */
    while (fgets(line, sizeof(line), file) != NULL) {
        /* Skip comment lines that start with # */
        if (line[0] == '#' || strlen(line) <= 1) {
            continue;
        }
        
        parseClientLine(line, &operations[numOperations]);
        numOperations++;
    }
    
    fclose(file);
    return numOperations; /* Number of operations is the client count */
}

void parseClientLine(char *line, ClientOperation *op) {
    /* Remove trailing newline */
    size_t len = strlen(line);
    if (len > 0 && line[len-1] == '\n') {
        line[len-1] = '\0';
    }
    
    /* Parse line format: "BankID_XX operation amount" or "N operation amount" */
    char *token = strtok(line, " ");
    if (token == NULL) {
        fprintf(stderr, "Error: Invalid line format\n");
        exit(EXIT_FAILURE);
    }
    
    strncpy(op->bankId, token, sizeof(op->bankId) - 1);
    op->bankId[sizeof(op->bankId) - 1] = '\0';
    
    token = strtok(NULL, " ");
    if (token == NULL) {
        fprintf(stderr, "Error: Invalid line format\n");
        exit(EXIT_FAILURE);
    }
    
    strncpy(op->operation, token, sizeof(op->operation) - 1);
    op->operation[sizeof(op->operation) - 1] = '\0';
    
    token = strtok(NULL, " ");
    if (token == NULL) {
        fprintf(stderr, "Error: Invalid line format\n");
        exit(EXIT_FAILURE);
    }
    
    op->amount = atoi(token);
}

/* Improved sendOperationBatch function for better batch processing */
void sendOperationBatch() {
    /* Create all the client FIFOs first */
    for (int i = 0; i < numOperations; i++) {
        char clientFifo[CLIENT_FIFO_NAME_LEN];
        snprintf(clientFifo, CLIENT_FIFO_NAME_LEN, CLIENT_FIFO_TEMPLATE "_%d", 
                 (long)getpid(), i + 1);
        
        /* Create the FIFO */
        umask(0);  /* So we get the permissions we want */
        if (mkfifo(clientFifo, FIFO_PERM) == -1 && errno != EEXIST) {
            perror("mkfifo");
            continue;
        }
    }
    
    /* Send all operations in rapid succession */
    for (int i = 0; i < numOperations; i++) {
        currentOpIndex = i;
        ClientOperation *op = &operations[i];
        
        /* Display client connection message */
        int clientIndex = i + 1;
        printf("Client%02d connected..", clientIndex);
        
        if (strcmp(op->operation, "deposit") == 0) {
            printf("depositing %d credits\n", op->amount);
        } else {
            printf("withdrawing %d credits\n", op->amount);
        }
        
        /* Set up the request */
        ClientRequest req;
        memset(&req, 0, sizeof(ClientRequest));
        req.pid = getpid();
        req.msgType = MSG_OPERATION;
        req.isNewClient = isNewClient(op->bankId);
        req.batchSize = numOperations;
        req.operationIndex = clientIndex;
        
        if (strcmp(op->operation, "deposit") == 0) {
            req.op = OP_DEPOSIT;
        } else if (strcmp(op->operation, "withdraw") == 0) {
            req.op = OP_WITHDRAW;
        } else {
            fprintf(stderr, "Error: Invalid operation: %s\n", op->operation);
            continue;
        }
        
        req.amount = op->amount;
        
        if (!req.isNewClient) {
            strncpy(req.bankId, op->bankId, sizeof(req.bankId) - 1);
            req.bankId[sizeof(req.bankId) - 1] = '\0';
        }
        
        /* Send the request to the server with retries */
        int retries = 3;
        while (retries--) {
            if (write(serverFd, &req, sizeof(ClientRequest)) == sizeof(ClientRequest)) {
                break; /* Success */
            } else if (errno == EAGAIN || errno == EINTR) {
                /* Temporary error, retry after a small delay */
                usleep(50000); /* 50ms */
            } else {
                /* Permanent error */
                perror("write to server");
                break;
            }
        }
    }
    
    /* Now open and read all client FIFOs for responses */
    int fd_array[MAX_BATCH_SIZE];
    fd_set readfds;
    int received_responses = 0;
    
    /* First, open all FIFOs in non-blocking mode */
    for (int i = 0; i < numOperations; i++) {
        char clientFifo[CLIENT_FIFO_NAME_LEN];
        snprintf(clientFifo, CLIENT_FIFO_NAME_LEN, CLIENT_FIFO_TEMPLATE "_%d", 
                 (long)getpid(), i + 1);
        
        fd_array[i] = open(clientFifo, O_RDONLY | O_NONBLOCK);
    }
    
    /* Process responses until we get them all or timeout */
    time_t start_time = time(NULL);
    
    while (received_responses < numOperations && time(NULL) - start_time < 30) {
        /* Set up descriptor set */
        FD_ZERO(&readfds);
        int maxfd = -1;
        
        for (int i = 0; i < numOperations; i++) {
            if (fd_array[i] > 0) {
                FD_SET(fd_array[i], &readfds);
                if (fd_array[i] > maxfd) {
                    maxfd = fd_array[i];
                }
            }
        }
        
        if (maxfd == -1) {
            /* No open FDs, try to open some */
            for (int i = 0; i < numOperations; i++) {
                if (fd_array[i] <= 0) {
                    char clientFifo[CLIENT_FIFO_NAME_LEN];
                    snprintf(clientFifo, CLIENT_FIFO_NAME_LEN, CLIENT_FIFO_TEMPLATE "_%d", 
                             (long)getpid(), i + 1);
                    
                    fd_array[i] = open(clientFifo, O_RDONLY | O_NONBLOCK);
                }
            }
            
            /* Check again after opening */
            for (int i = 0; i < numOperations; i++) {
                if (fd_array[i] > 0) {
                    FD_SET(fd_array[i], &readfds);
                    if (fd_array[i] > maxfd) {
                        maxfd = fd_array[i];
                    }
                }
            }
            
            if (maxfd == -1) {
                /* Still no open FDs, wait a bit */
                usleep(100000); /* 100ms */
                continue;
            }
        }
        
        /* Wait for data with timeout */
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 250000; /* 250ms timeout - more responsive */
        
        int ready = select(maxfd + 1, &readfds, NULL, NULL, &tv);
        if (ready < 0) {
            if (errno == EINTR) continue; /* Interrupted, try again */
            perror("select");
            break;
        } else if (ready == 0) {
            continue; /* Timeout, try again */
        }
        
        /* Check which FDs have data */
        for (int i = 0; i < numOperations; i++) {
            if (fd_array[i] > 0 && FD_ISSET(fd_array[i], &readfds)) {
                /* Read response */
                ServerResponse resp;
                ssize_t bytes_read = read(fd_array[i], &resp, sizeof(ServerResponse));
                
                if (bytes_read == sizeof(ServerResponse)) {
                    /* Process the response */
                    processResponse(&resp, &operations[i], i + 1);
                    received_responses++;
                    
                    /* Close this FD since we're done with it */
                    close(fd_array[i]);
                    fd_array[i] = -1;
                } else if (bytes_read == -1 && errno != EAGAIN) {
                    /* Error other than "would block" */
                    perror("read from server");
                    close(fd_array[i]);
                    fd_array[i] = -1;
                }
            }
        }
    }
    
    /* Close any remaining FDs */
    for (int i = 0; i < numOperations; i++) {
        if (fd_array[i] > 0) {
            close(fd_array[i]);
        }
    }
    
    /* Remove the client FIFOs */
    for (int i = 0; i < numOperations; i++) {
        char clientFifo[CLIENT_FIFO_NAME_LEN];
        snprintf(clientFifo, CLIENT_FIFO_NAME_LEN, CLIENT_FIFO_TEMPLATE "_%d", 
                 (long)getpid(), i + 1);
        unlink(clientFifo);
    }
}

/* Process server response */
void processResponse(ServerResponse *resp, ClientOperation *op, int clientIndex) {
    /* Process the server's response */
    if (resp->status == 0) {
        /* Success */
        if (resp->balance == 0 && strcmp(op->operation, "withdraw") == 0) {
            printf("Client%02d served.. account closed\n", clientIndex);
        } else {
            printf("Client%02d served.. %s\n", clientIndex, resp->bankId);
            
            /* CRITICAL CHANGE: Only update 'N' operations with the new BankID */
            if (strcmp(op->bankId, "N") == 0) {
                /* Update the current operation's bankId */
                strncpy(op->bankId, resp->bankId, sizeof(op->bankId) - 1);
                op->bankId[sizeof(op->bankId) - 1] = '\0';
            }
        }
    } else {
        /* Error */
        printf("Client%02d something went WRONG: %s\n", clientIndex, resp->message);
    }
}

/* Helper functions */
int isNewClient(const char *bankId) {
    return (strcmp(bankId, "N") == 0);
}