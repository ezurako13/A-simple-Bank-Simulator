/* BankServer.c
 * Implementation of the bank server
 */
#include "BankServer.h"

/* Global variables */
FILE *logFile = NULL;
char serverFifo[SERVER_FIFO_NAME_LEN];
int serverFd = -1, dummyFd = -1;
BankDatabase bankDb;  /* Regular structure, not a pointer */
int activeClients = 0;
int lastClientId = 0;
char bankName[50];
sem_t *serverSem = NULL;
BatchInfo currentBatch = {0, 0, 0}; /* Current batch being processed */
ClientRequest batchRequests[MAX_BATCH_SIZE]; /* Array to store batch requests */

/* Flag to track initialization status - NEW ADDITION */
static int server_initialized = 0;

/* Implementation of main function */
int main(int argc, char *argv[]) {
    /* Check command line arguments */
    if (argc != 3) {
        fprintf(stderr, "Usage: %s BankName ServerFIFO_Name\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    /* Initialize the server */
    initializeServer(argv, argv[1], argv[2]);
    
    /* Wait for client connections */
    waitForClients();
    
    /* Should never reach here unless interrupted by a signal */
    return 0;
}

/* Custom process creation function */
pid_t Teller(void* func, void* arg_func) {
    pid_t pid = fork();
    
    if (pid == -1) {
        errLog(logFile, "Teller: process creation failed");
        return -1;
    } else if (pid == 0) {
        /* Child process - call the teller function */
        void *(*tellerFunc)(void *) = func;
        tellerFunc(arg_func);
        exit(EXIT_SUCCESS);
    }
    free(arg_func); /* Free the argument passed to the teller */
    /* Parent returns child's PID */
    return pid;
}

/* Custom process waiting function */
int waitTeller(pid_t pid, int* status) {
    return waitpid(pid, status, 0);
}

/* Server initialization and cleanup */
void initializeServer(char *argv[], const char *name, const char *fifoName) {
    strncpy(bankName, name, sizeof(bankName) - 1);
    bankName[sizeof(bankName) - 1] = '\0';
    
    /* Match the format in the PDF exactly */
    printf("%s %s #%s\n", argv[0], bankName, fifoName);
    printf("%s is active...\n", bankName);
    
    /* Create log file */
    char logFileName[64];
    snprintf(logFileName, sizeof(logFileName), "%s.bankLog", bankName);
    
    /* Check if log file exists */
    int logExists = access(logFileName, F_OK) == 0;
    
    /* Initialize the database */
    initializeDatabase();
    
    /* Read log file to get the last client ID and restore accounts if it exists */
    if (logExists) {
        /* First read the highest client ID */
        readLogFile(logFileName, &lastClientId);
        
        /* Now restore the accounts */
        restoreDatabaseFromLog(logFileName, &bankDb);
        
        int activeAccounts = 0;
        for (int i = 0; i < bankDb.numAccounts; i++) {
            if (bankDb.accounts[i].active) {
                activeAccounts++;
            }
        }
        
        /* Only print initialization message once - NEW ADDITION */
        if (!server_initialized) {
            printf("Previous logs found. Restored %d active accounts to the bank database.\n", activeAccounts);
            server_initialized = 1;
        }
        
        /* Open log file in APPEND mode */
        logFile = fopen(logFileName, "a+");
        if (logFile == NULL) {
            errExit("Failed to open log file");
        }
        
        /* Add a log separator */
        fprintf(logFile, "# %s Log file updated @%s\n", bankName, __TIME__);
    } else {
        /* Only print initialization message once - NEW ADDITION */
        if (!server_initialized) {
            printf("No previous logs.. Creating the bank database\n");
            server_initialized = 1;
        }
        
        /* Open log file in write mode for first creation only */
        logFile = fopen(logFileName, "w");
        if (logFile == NULL) {
            errExit("Failed to open log file");
        }
        
        /* Write log file header */
        char timeStr[30];
        getCurrentTimeStr(timeStr, sizeof(timeStr));
        fprintf(logFile, "# %s Log file updated @%s\n\n", bankName, timeStr);
    }
    
    /* Set up signal handlers */
    struct sigaction sa;
    sa.sa_handler = handleSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    if (sigaction(SIGINT, &sa, NULL) == -1 || sigaction(SIGTERM, &sa, NULL) == -1) {
        errExitWithLog(logFile, "sigaction");
    }
    
    /* Set up signal handler for child processes */
    struct sigaction sa_chld;
    sa_chld.sa_handler = handleChildSignal;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_NOCLDSTOP;
    
    if (sigaction(SIGCHLD, &sa_chld, NULL) == -1) {
        errExitWithLog(logFile, "sigaction for SIGCHLD");
    }
    
    /* Create the server FIFO - using the template correctly */
    snprintf(serverFifo, SERVER_FIFO_NAME_LEN, SERVER_FIFO_TEMPLATE, fifoName);
    
    umask(0);  /* So we get the permissions we want */
    
    if (mkfifo(serverFifo, FIFO_PERM) == -1 && errno != EEXIST) {
        errExitWithLog(logFile, "mkfifo %s", serverFifo);
    }
    
    /* Create semaphore for synchronizing access to the server FIFO */
    serverSem = sem_open(pidToString(getpid()), O_CREAT, 0666, 1);
    if (serverSem == SEM_FAILED) {
        errExitWithLog(logFile, "sem_open for server FIFO");
    }
}

void cleanupServer(void) {
    /* Close and remove semaphores */
    if (serverSem != NULL && serverSem != SEM_FAILED) {
        sem_close(serverSem);
        sem_unlink(pidToString(getpid()));
    }
    
    /* Close FIFOs */
    if (serverFd != -1) close(serverFd);
    if (dummyFd != -1) close(dummyFd);
    
    /* Remove the server FIFO */
    if (unlink(serverFifo) == -1 && errno != ENOENT) {
        errLog(logFile, "unlink %s", serverFifo);
    }
    
    /* Update log file with final database state */
    char timeStr[30];
    getCurrentTimeStr(timeStr, sizeof(timeStr));
    
    fprintf(logFile, "# %s Log file updated @%s\n\n", bankName, timeStr);
    
    /* Only write active accounts */
    for (int i = 0; i < bankDb.numAccounts; i++) {
        if (bankDb.accounts[i].active) {
            fprintf(logFile, "%s D 0 %d\n", 
                    bankDb.accounts[i].bankId, 
                    bankDb.accounts[i].balance);
        }
    }
    
    /* Add end of log marker */
    fprintf(logFile, "\n## end of log.\n\n");
    
    /* Close log file */
    if (logFile != NULL) {
        fclose(logFile);
    }
    
    printf("%s says \"Bye\"...\n", bankName);
}

/* Signal handlers */
void handleSignal(int sig) {
    static int cleaning_up = 0;
    
    if (cleaning_up) return;
    cleaning_up = 1;
    
    (void)sig; /* Suppress unused parameter warning */
    int savedErrno = errno;
    
    printf("Signal received closing active Tellers\n");
    printf("Removing ServerFIFO... Updating log file...\n");
    
    /* Send termination signal to all child processes */
    kill(0, SIGTERM);
    
    sleep(1); /* Give tellers a chance to exit */
    
    /* Clean up resources */
    cleanupServer();
    
    errno = savedErrno;
    exit(EXIT_SUCCESS);
}

void handleChildSignal(int sig) {
    (void)sig; /* Suppress unused parameter warning */
    int savedErrno = errno;
    pid_t childPid;
    int status;
    
    /* Reap all terminated child processes */
    while ((childPid = waitpid(-1, &status, WNOHANG)) > 0) {
        activeClients--;
        
        if (childPid > 0) {
            if (WIFEXITED(status)) {
                if (WEXITSTATUS(status) != 0) {
                    printLog(logFile, "ERROR: Teller %d exited with non-zero status %d", 
                            childPid, WEXITSTATUS(status));
                }
            } else if (WIFSIGNALED(status)) {
                printLog(logFile, "ERROR: Teller %d killed by signal %d", 
                        childPid, WTERMSIG(status));
            }
        }
    }
    
    errno = savedErrno;
}

/* Set up signal handling for teller processes */
void setupTellerSignals(void) {
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_DFL);
    signal(SIGPIPE, SIG_IGN);
}

/* Extract client number from bankId */
int extractClientNumber(const char *bankId) {
    if (bankId == NULL || strlen(bankId) == 0) {
        return ++lastClientId;  // New client
    }
    
    int num = 0;
    if (sscanf(bankId, "BankID_%d", &num) == 1) {
        return num;
    }
    
    return ++lastClientId;  // Default to new client
}

/* Client connection handling */
void waitForClients(void) {
    /* Main server loop */
    while (1) {
        /* Print the waiting message at the beginning of each cycle */
        printf("Waiting for clients @%s...\n", serverFifo);
        
        /* Open the FIFO for reading if not already open */
        if (serverFd == -1) {
            serverFd = open(serverFifo, O_RDONLY);
            if (serverFd == -1) {
                errExitWithLog(logFile, "open %s for reading", serverFifo);
            }
            
            /* Open an extra write descriptor, so that we never see EOF */
            dummyFd = open(serverFifo, O_WRONLY);
            if (dummyFd == -1) {
                errExitWithLog(logFile, "open %s for writing", serverFifo);
            }
        }
        
        /* Reset batch information */
        resetBatchInfo(&currentBatch);
        
        /* Read client requests */
        ClientRequest req;
        while (1) {
            ssize_t numRead = read_mutually_exclusive(serverSem, serverFd, &req, sizeof(ClientRequest));
            
            if (numRead != sizeof(ClientRequest)) {
                if (numRead == -1 && errno != EINTR) {
                    errLog(logFile, "read");
                }
                break; /* Error or signal interruption */
            }
            
            /* If this is a new batch (different PID or first request), handle it */
            if (currentBatch.pid != req.pid || currentBatch.total == 0) {
                /* If we were already processing a batch, process it now */
                if (currentBatch.pid != 0 && currentBatch.received > 0) {
                    processBatch();
                }
                
                /* Start a new batch */
                currentBatch.pid = req.pid;
                currentBatch.total = req.batchSize;
                currentBatch.received = 0;
            }
            
            /* Store the request in our batch array */
            if (currentBatch.received < MAX_BATCH_SIZE) {
                batchRequests[currentBatch.received] = req;
                currentBatch.received++;
            }
            
            /* If we've received all requests in this batch, process it */
            if (currentBatch.received >= currentBatch.total) {
                processBatch();
                resetBatchInfo(&currentBatch);
                break; /* Exit read loop after processing a complete batch */
            }
        }
        
        /* If the pipe was closed, sleep briefly to avoid busy waiting */
        if (serverFd == -1) {
            sleep(1);
        }
    }
}

/* Improved processBatch function for true concurrency */
void processBatch(void) {
    /* Only process batches with clients */
    if (currentBatch.pid == 0 || currentBatch.received == 0) {
        return;
    }
    
    printf(" - Received %d clients from PID%d..\n", currentBatch.received, currentBatch.pid);
    
    /* Create all teller processes */
    pid_t tellerPids[MAX_BATCH_SIZE] = {0};
    int pipes[MAX_BATCH_SIZE][4] = {{-1}}; /* [i][0]=st_read, [i][1]=st_write, [i][2]=ts_read, [i][3]=ts_write */
    
    /* First set up all pipes */
    for (int i = 0; i < currentBatch.received; i++) {
        /* Create pipes */
        if (pipe(pipes[i]) == -1 || pipe(pipes[i] + 2) == -1) {
            errLog(logFile, "pipe creation failed");
            /* Close any pipes we've created so far */
            for (int j = 0; j <= i; j++) {
                for (int k = 0; k < 4; k++) {
                    if (pipes[j][k] != -1) {
                        close(pipes[j][k]);
                        pipes[j][k] = -1;
                    }
                }
            }
            return; /* Abort batch processing if pipe creation fails */
        }
    }
    
    /* Create a semaphore for database access */
    sem_t *dbSem = sem_open("bank_db_mutex", O_CREAT, 0666, 1);
    if (dbSem == SEM_FAILED) {
        errLog(logFile, "sem_open for database failed");
        return;
    }
    
    /* Spawn all tellers simultaneously */
    for (int i = 0; i < currentBatch.received; i++) {
        /* Allocate memory for teller args - will be freed by teller */
        struct TellerArgs *teller_arg = malloc(sizeof(struct TellerArgs));
        if (!teller_arg) {
            errLog(logFile, "malloc for teller args failed");
            for (int k = 0; k < 4; k++) {
                if (pipes[i][k] != -1) {
                    close(pipes[i][k]);
                    pipes[i][k] = -1;
                }
            }
            continue;
        }
        
        /* Set up teller args */
        memset(teller_arg, 0, sizeof(struct TellerArgs));
        teller_arg->client_req = batchRequests[i];
        teller_arg->pipe_read = pipes[i][0];  /* teller reads from server_to_teller[0] */
        teller_arg->pipe_write = pipes[i][3]; /* teller writes to teller_to_server[1] */
        
        /* Create teller process */
        ClientRequest *req = &batchRequests[i];
        int clientIndex = req->operationIndex;
        void *func = req->op == OP_DEPOSIT ? depositTeller : withdrawTeller;
        
        /* Fork the teller */
        tellerPids[i] = Teller(func, teller_arg);
        
        if (tellerPids[i] <= 0) {
            /* Fork failed, clean up */
            free(teller_arg);
            for (int k = 0; k < 4; k++) {
                if (pipes[i][k] != -1) {
                    close(pipes[i][k]);
                    pipes[i][k] = -1;
                }
            }
            continue;
        }
        
        /* Parent process */
        activeClients++;
        
        /* Close unused pipe ends in parent */
        close(pipes[i][0]); pipes[i][0] = -1;
        close(pipes[i][3]); pipes[i][3] = -1;
        
        /* Print teller activation message */
        printf(" -- Teller %d is active serving Client%02d", tellerPids[i], clientIndex);
        
        if (!req->isNewClient && strlen(req->bankId) > 0) {
            printf("...Welcome back Client%02d\n", clientIndex);
        } else {
            printf("...\n");
        }
    }
    
    /* Set up an fd_set for all pipe descriptors for reading */
    fd_set readfds;
    int maxfd, remaining_tellers;
    
    /* Create arrays to track which tellers need to be responded to and which are completed */
    int teller_completed[MAX_BATCH_SIZE] = {0};
    
    /* Process teller communications using non-blocking select to allow concurrency */
    do {
        FD_ZERO(&readfds);
        maxfd = -1;
        remaining_tellers = 0;
        
        /* Add all active teller read pipes to the set */
        for (int i = 0; i < currentBatch.received; i++) {
            if (!teller_completed[i] && pipes[i][2] != -1) {
                FD_SET(pipes[i][2], &readfds);
                if (pipes[i][2] > maxfd) {
                    maxfd = pipes[i][2];
                }
                remaining_tellers++;
            }
        }
        
        if (remaining_tellers == 0) {
            break; /* No active tellers */
        }
        
        /* Set up a short timeout to make select non-blocking */
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 250000; /* 10ms timeout - fast enough for responsive service */
        
        /* Try to read from any ready teller */
        int select_result = select(maxfd + 1, &readfds, NULL, NULL, &tv);
        
        if (select_result < 0 && errno != EINTR) {
            errLog(logFile, "select failed");
            break;
        } else if (select_result == 0) {
            /* No descriptors ready - check for finished tellers */
            for (int i = 0; i < currentBatch.received; i++) {
                if (!teller_completed[i] && tellerPids[i] > 0) {
                    int status;
                    pid_t result = waitpid(tellerPids[i], &status, WNOHANG);
                    if (result > 0) {
                        /* Teller has completed */
                        teller_completed[i] = 1;
                        
                        /* Close its pipes */
                        if (pipes[i][1] != -1) {
                            close(pipes[i][1]);
                            pipes[i][1] = -1;
                        }
                        if (pipes[i][2] != -1) {
                            close(pipes[i][2]);
                            pipes[i][2] = -1;
                        }
                    }
                }
            }
            continue; /* Try again */
        }
        
        /* Process tellers with ready data */
        for (int i = 0; i < currentBatch.received; i++) {
            if (!teller_completed[i] && pipes[i][2] != -1 && FD_ISSET(pipes[i][2], &readfds)) {
                /* Read teller request */
                TellerRequest teller_req;
                ssize_t numRead = read(pipes[i][2], &teller_req, sizeof(TellerRequest));
                
                if (numRead != sizeof(TellerRequest)) {
                    /* Error or partial read */
                    if (pipes[i][2] != -1) {
                        close(pipes[i][2]);
                        pipes[i][2] = -1;
                    }
                    if (pipes[i][1] != -1) {
                        close(pipes[i][1]);
                        pipes[i][1] = -1;
                    }
                    continue;
                }
                
                /* Process database request - here's where concurrency happens */
                ServerResponse server_resp;
                memset(&server_resp, 0, sizeof(ServerResponse));
                int clientIndex = batchRequests[i].operationIndex;
                
                /* Lock the database for thread safety */
                sem_wait(dbSem);
                
                /* Process the database request */
                processDatabaseRequest(&teller_req, &server_resp, clientIndex);
                
                /* Unlock the database */
                sem_post(dbSem);
                
                /* Send response back to teller immediately */
                if (pipes[i][1] != -1) {
                    if (write(pipes[i][1], &server_resp, sizeof(ServerResponse)) != sizeof(ServerResponse)) {
                        /* Error writing */
                        close(pipes[i][1]);
                        pipes[i][1] = -1;
                    }
                }
            }
        }
    } while (remaining_tellers > 0);
    
    /* Clean up semaphore */
    sem_close(dbSem);
    sem_unlink("bank_db_mutex");
    
    /* Clean up any remaining pipes and wait for tellers */
    for (int i = 0; i < currentBatch.received; i++) {
        /* Close any remaining pipe descriptors */
        for (int k = 0; k < 4; k++) {
            if (pipes[i][k] != -1) {
                close(pipes[i][k]);
                pipes[i][k] = -1;
            }
        }
        
        /* Wait for any teller that's still running */
        if (tellerPids[i] > 0 && !teller_completed[i]) {
            int status;
            if (waitpid(tellerPids[i], &status, WNOHANG) == 0) {
                /* Give it a little time, then try again */
                usleep(50000); /* 50ms */
                if (waitpid(tellerPids[i], &status, WNOHANG) == 0) {
                    /* If still running, terminate it */
                    kill(tellerPids[i], SIGTERM);
                    waitpid(tellerPids[i], &status, 0);
                }
            }
        }
    }
}

/* Reset batch information */
void resetBatchInfo(BatchInfo *batch) {
    batch->pid = 0;
    batch->total = 0;
    batch->received = 0;
}

/* Fixed tellerProcess function with proper fd_set declaration */
void *tellerProcess(void *arg, int isDeposit) {
    /* Set up teller signals */
    setupTellerSignals();
    
    /* Cast argument to the proper type */
    struct TellerArgs *teller_arg = (struct TellerArgs *)arg;
    
    /* Validate argument */
    if (!teller_arg) {
        exit(EXIT_FAILURE);
    }
    
    ClientRequest *req = &teller_arg->client_req;
    int pipe_read = teller_arg->pipe_read;
    int pipe_write = teller_arg->pipe_write;
    
    /* Validate pipe descriptors */
    if (pipe_read < 0 || pipe_write < 0) {
        free(teller_arg);
        exit(1); /* Bad pipe descriptors */
    }
    
    /* Create a unique client FIFO for this specific operation */
    char clientFifo[CLIENT_FIFO_NAME_LEN];
    snprintf(clientFifo, CLIENT_FIFO_NAME_LEN, CLIENT_FIFO_TEMPLATE "_%d", 
             (long)req->pid, req->operationIndex);
    
    /* Open the FIFO for writing without blocking for too long */
    int clientFd = -1;
    
    /* Try to open in non-blocking mode first with retries */
    for (int attempt = 0; attempt < 10 && clientFd == -1; attempt++) {
        clientFd = open(clientFifo, O_WRONLY | O_NONBLOCK);
        
        if (clientFd == -1) {
            if (errno == ENXIO) {
                /* No reader yet, sleep briefly and retry */
                usleep(50000); /* 50ms */
            } else {
                /* Other error */
                break;
            }
        } else {
            /* Success - switch back to blocking mode */
            int flags = fcntl(clientFd, F_GETFL);
            fcntl(clientFd, F_SETFL, flags & ~O_NONBLOCK);
            break;
        }
    }
    
    if (clientFd == -1) {
        /* Still couldn't open client FIFO after retries */
        close(pipe_read);
        close(pipe_write);
        free(teller_arg);
        exit(2);
    }
    
    /* For withdraw operation, validate new client cannot withdraw */
    if (!isDeposit && req->isNewClient) {
        ServerResponse client_resp;
        memset(&client_resp, 0, sizeof(ServerResponse));
        client_resp.status = ERR_INVALID_OPERATION;
        strcpy(client_resp.message, "New clients cannot withdraw. Please deposit first.");
        client_resp.clientIndex = req->operationIndex;
        
        write(clientFd, &client_resp, sizeof(ServerResponse));
        
        close(clientFd);
        close(pipe_read);
        close(pipe_write);
        free(teller_arg);
        exit(EXIT_SUCCESS);
    }
    
    /* Prepare request for the server */
    TellerRequest teller_req;
    memset(&teller_req, 0, sizeof(TellerRequest));
    teller_req.operation = isDeposit ? OP_DEPOSIT : OP_WITHDRAW;
    teller_req.amount = req->amount;
    teller_req.isNewClient = req->isNewClient;
    teller_req.clientPid = req->pid;
    teller_req.clientIndex = req->operationIndex;
    
    if (strlen(req->bankId) > 0) {
        strncpy(teller_req.bankId, req->bankId, sizeof(teller_req.bankId) - 1);
        teller_req.bankId[sizeof(teller_req.bankId) - 1] = '\0';
    }
    
    /* Send request to main server - use non-blocking write with timeout */
    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(pipe_write, &writefds);
    
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    
    int ready = select(pipe_write + 1, NULL, &writefds, NULL, &tv);
    if (ready <= 0) {
        /* Timeout or error */
        ServerResponse client_resp;
        memset(&client_resp, 0, sizeof(ServerResponse));
        client_resp.status = ERR_INVALID_OPERATION;
        strcpy(client_resp.message, "Server communication error");
        client_resp.clientIndex = req->operationIndex;
        
        write(clientFd, &client_resp, sizeof(ServerResponse));
        
        close(clientFd);
        close(pipe_read);
        close(pipe_write);
        free(teller_arg);
        exit(3);
    }
    
    /* Write when ready */
    if (write(pipe_write, &teller_req, sizeof(TellerRequest)) != sizeof(TellerRequest)) {
        /* Write error */
        ServerResponse client_resp;
        memset(&client_resp, 0, sizeof(ServerResponse));
        client_resp.status = ERR_INVALID_OPERATION;
        strcpy(client_resp.message, "Failed to communicate with server");
        client_resp.clientIndex = req->operationIndex;
        
        write(clientFd, &client_resp, sizeof(ServerResponse));
        
        close(clientFd);
        close(pipe_read);
        close(pipe_write);
        free(teller_arg);
        exit(4);
    }
    
    /* Wait for server response with timeout using select */
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(pipe_read, &readfds);
    
    tv.tv_sec = 3; /* 3 second timeout */
    tv.tv_usec = 0;
    
    ready = select(pipe_read + 1, &readfds, NULL, NULL, &tv);
    
    ServerResponse server_resp;
    memset(&server_resp, 0, sizeof(ServerResponse));
    
    if (ready <= 0) {
        /* Timeout or error */
        server_resp.status = ERR_INVALID_OPERATION;
        strcpy(server_resp.message, "Server response timeout");
        server_resp.clientIndex = req->operationIndex;
    } else {
        /* Read the response */
        if (read(pipe_read, &server_resp, sizeof(ServerResponse)) != sizeof(ServerResponse)) {
            /* Read error */
            server_resp.status = ERR_INVALID_OPERATION;
            strcpy(server_resp.message, "Error reading server response");
            server_resp.clientIndex = req->operationIndex;
        }
    }
    
    /* Send response to client */
    if (write(clientFd, &server_resp, sizeof(ServerResponse)) != sizeof(ServerResponse)) {
        /* Error writing to client, but we can't do much about it now */
    }
    
    /* Clean up */
    close(clientFd);
    close(pipe_read);
    close(pipe_write);
    free(teller_arg);
    
    exit(EXIT_SUCCESS);
}

/* Deposit teller */
void *depositTeller(void *arg) {
    return tellerProcess(arg, 1);
}

/* Withdraw teller */
void *withdrawTeller(void *arg) {
    return tellerProcess(arg, 0);
}

/* Process teller request and update database */
void processDatabaseRequest(TellerRequest *req, ServerResponse *resp, int clientNum) {
    resp->status = 0;  /* Success by default */
    resp->clientIndex = req->clientIndex;
    
    if (req->operation == OP_DEPOSIT) {
        if (req->isNewClient) {
            /* Create new account */
            int accountIndex = createAccount(req->amount);
            if (accountIndex >= 0) {
                strncpy(resp->bankId, bankDb.accounts[accountIndex].bankId, sizeof(resp->bankId));
                resp->balance = bankDb.accounts[accountIndex].balance;
                snprintf(resp->message, sizeof(resp->message), 
                        "New account created with %d credits", req->amount);
                
                printf("Client%02d deposited %d credits... updating log\n", 
                       clientNum, req->amount);
            } else {
                resp->status = ERR_INVALID_OPERATION;
                strcpy(resp->message, "Failed to create account");
                printf("Client%02d deposit failed... account creation error\n", 
                       clientNum);
            }
        } else {
            /* Deposit to existing account */
            int accountIndex = findAccount(req->bankId);
            if (accountIndex >= 0) {
                int newBalance = depositToAccount(req->bankId, req->amount);
                if (newBalance >= 0) {
                    strncpy(resp->bankId, req->bankId, sizeof(resp->bankId));
                    resp->balance = newBalance;
                    snprintf(resp->message, sizeof(resp->message), 
                            "Deposited %d credits. New balance: %d", req->amount, newBalance);
                    
                    printf("Client%02d deposited %d credits... updating log\n", 
                           clientNum, req->amount);
                } else {
                    resp->status = ERR_INVALID_OPERATION;
                    strcpy(resp->message, "Deposit operation failed");
                    printf("Client%02d deposit failed... operation error\n", 
                           clientNum);
                }
            } else {
                resp->status = ERR_INVALID_ACCOUNT;
                strcpy(resp->message, "Account not found");
                printf("Client%02d deposit failed... account not found\n", 
                       clientNum);
            }
        }
    } else if (req->operation == OP_WITHDRAW) {
        /* Withdraw from existing account */
        int accountIndex = findAccount(req->bankId);
        if (accountIndex >= 0) {
            int newBalance = withdrawFromAccount(req->bankId, req->amount);
            if (newBalance >= 0) {
                strncpy(resp->bankId, req->bankId, sizeof(resp->bankId));
                resp->balance = newBalance;
                
                if (newBalance == 0) {
                    snprintf(resp->message, sizeof(resp->message), 
                            "Withdrew %d credits. Account closed.", req->amount);
                    removeAccount(req->bankId);
                    printf("Client%02d withdraws %d credits... updating log... Bye Client%02d\n", 
                           clientNum, req->amount, clientNum);
                } else {
                    snprintf(resp->message, sizeof(resp->message), 
                            "Withdrew %d credits. New balance: %d", req->amount, newBalance);
                    printf("Client%02d withdraws %d credits... updating log\n", 
                           clientNum, req->amount);
                }
            } else if (newBalance == ERR_INSUFFICIENT_FUNDS) {
                resp->status = ERR_INSUFFICIENT_FUNDS;
                strcpy(resp->message, "Insufficient funds for withdrawal");
                
                printf("Client%02d withdraws %d credit.. operation not permitted.\n", 
                       clientNum, req->amount);
            } else {
                resp->status = ERR_INVALID_OPERATION;
                strcpy(resp->message, "Withdraw operation failed");
                printf("Client%02d withdraws %d credits... operation failed.\n", 
                       clientNum, req->amount);
            }
        } else {
            resp->status = ERR_INVALID_ACCOUNT;
            strcpy(resp->message, "Account not found");
            printf("Client%02d withdraws %d credits... account not found.\n", 
                   clientNum, req->amount);
        }
    } else {
        resp->status = ERR_INVALID_OPERATION;
        strcpy(resp->message, "Invalid operation");
        printf("Client%02d invalid operation %d\n", clientNum, req->operation);
    }
}

/* Database operations */
void initializeDatabase(void) {
    bankDb.numAccounts = 0;
}

int findAccount(const char *bankId) {
    for (int i = 0; i < bankDb.numAccounts; i++) {
        if (bankDb.accounts[i].active && strcmp(bankDb.accounts[i].bankId, bankId) == 0) {
            return i;
        }
    }
    return -1;  /* Account not found */
}

/* Fixed createAccount function to properly increment lastClientId */
int createAccount(int amount) {
    if (bankDb.numAccounts >= 100) {
        return -1;  /* Maximum number of accounts reached */
    }
    
    /* Increment lastClientId for the new account */
    lastClientId++;
    
    int index = bankDb.numAccounts++;
    generateBankId(bankDb.accounts[index].bankId, lastClientId);
    bankDb.accounts[index].balance = amount;
    bankDb.accounts[index].active = 1;
    
    /* Update log file */
    updateLogFile(logFile, bankDb.accounts[index].bankId, 'D', amount, amount);
    
    return index;
}

int depositToAccount(const char *bankId, int amount) {
    int index = findAccount(bankId);
    if (index == -1) {
        return -1;  /* Account not found */
    }
    
    bankDb.accounts[index].balance += amount;
    
    /* Update log file */
    updateLogFile(logFile, bankId, 'D', amount, bankDb.accounts[index].balance);
    
    return bankDb.accounts[index].balance;
}

int withdrawFromAccount(const char *bankId, int amount) {
    int index = findAccount(bankId);
    if (index == -1) {
        return -1;  /* Account not found */
    }
    
    if (bankDb.accounts[index].balance < amount) {
        return ERR_INSUFFICIENT_FUNDS;  /* Insufficient funds */
    }
    
    bankDb.accounts[index].balance -= amount;
    
    /* Update log file */
    updateLogFile(logFile, bankId, 'W', amount, bankDb.accounts[index].balance);
    
    return bankDb.accounts[index].balance;
}

void removeAccount(const char *bankId) {
    int index = findAccount(bankId);
    if (index == -1) {
        return;  /* Account not found */
    }
    
    bankDb.accounts[index].active = 0;
}

/* Helper functions */
void printServerStatus(void) {
    printf("Server Status:\n");
    printf("Active clients: %d\n", activeClients);
    printf("Number of accounts: %d\n", bankDb.numAccounts);
    
    printf("Accounts:\n");
    for (int i = 0; i < bankDb.numAccounts; i++) {
        if (bankDb.accounts[i].active) {
            printf("%s: %d credits\n", 
                    bankDb.accounts[i].bankId, 
                    bankDb.accounts[i].balance);
        }
    }
}