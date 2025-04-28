/* bank_utils.c
 * Implementation of utility functions for bank simulator
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/select.h>
#include <semaphore.h>
#include "bank_utils.h"

/* Error handling functions */
void errExit(const char *format, ...) {
    va_list argList;
    va_start(argList, format);
    vfprintf(stderr, format, argList);
    fprintf(stderr, " (errno=%d: %s)\n", errno, strerror(errno));
    va_end(argList);
    exit(EXIT_FAILURE);
}

void errExitWithLog(FILE *log, const char *format, ...) {
    va_list argList;
    
    va_start(argList, format);
    vfprintf(stderr, format, argList);
    fprintf(stderr, " (errno=%d: %s)\n", errno, strerror(errno));
    
    vfprintf(log, format, argList);
    fprintf(log, " (errno=%d: %s)\n", errno, strerror(errno));
    va_end(argList);
    
    fflush(log);
    exit(EXIT_FAILURE);
}

void errLog(FILE *log, const char *format, ...) {
    va_list argList;
    
    va_start(argList, format);
    vfprintf(stderr, format, argList);
    fprintf(stderr, " (errno=%d: %s)\n", errno, strerror(errno));
    
    vfprintf(log, format, argList);
    fprintf(log, " (errno=%d: %s)\n", errno, strerror(errno));
    va_end(argList);
    
    fflush(log);
}

void printLog(FILE *log, const char *format, ...) {
    va_list argList;
    char timeStr[30];
    
    getCurrentTimeStr(timeStr, sizeof(timeStr));
    
    va_start(argList, format);
    fprintf(stderr, "[%s] ", timeStr);
    vfprintf(stderr, format, argList);
    fprintf(stderr, "\n");
    
    fprintf(log, "[%s] ", timeStr);
    vfprintf(log, format, argList);
    fprintf(log, "\n");
    va_end(argList);
    
    fflush(log);
}

/* Optimized read_with_timeout function */
int read_with_timeout(int fd, void *buf, size_t count, int timeout_sec) {
    fd_set readfds;
    struct timeval tv;
    
    /* Set up select with timeout */
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;
    
    /* Wait for data to be available */
    int retval = select(fd + 1, &readfds, NULL, NULL, &tv);
    
    if (retval == -1) {
        if (errno == EINTR) {
            /* If interrupted, try once more with reduced timeout */
            usleep(100000); /* 100ms */
            FD_ZERO(&readfds);
            FD_SET(fd, &readfds);
            tv.tv_sec = 1; /* Reduced timeout */
            tv.tv_usec = 0;
            retval = select(fd + 1, &readfds, NULL, NULL, &tv);
            if (retval <= 0) return -1;
        } else {
            return -1;
        }
    } else if (retval == 0) {
        /* Timeout occurred */
        errno = ETIMEDOUT;
        return -1;
    }
    
    /* Data is available, read it */
    return read(fd, buf, count);
}

/* Optimized write_with_retry function */
int write_with_retry(int fd, const void *buf, size_t count, int max_retries) {
    int retries = 0;
    ssize_t bytes_written;
    const char *buffer = buf;
    size_t total_written = 0;
    
    while (total_written < count && retries < max_retries) {
        bytes_written = write(fd, buffer + total_written, count - total_written);
        
        if (bytes_written == -1) {
            if (errno == EINTR || errno == EAGAIN) {
                /* Interrupted or would block, retry after short delay */
                retries++;
                usleep(100000); /* 100ms */
                continue;
            } else {
                /* Unrecoverable error */
                return -1;
            }
        }
        
        total_written += bytes_written;
    }
    
    return (int)total_written;
}

/* Bank-specific utility functions */
void generateBankId(char *bankId, int clientNum) {
    snprintf(bankId, 20, "BankID_%02d", clientNum);
}

void getCurrentTimeStr(char *timeStr, size_t size) {
    time_t now;
    struct tm *tm_info;
    
    time(&now);
    tm_info = localtime(&now);
    
    strftime(timeStr, size, "%H:%M %B %d %Y", tm_info);
}

/* Fixed readLogFile function to correctly initialize lastClientId */
int readLogFile(const char *filename, int *lastClientNum) {
    FILE *file = fopen(filename, "r");
    char line[256];
    int clientNum;
    int maxClientNum = 0;
    
    if (file == NULL) {
        /* Log file doesn't exist yet, start with client number 0 */
        *lastClientNum = 0;
        return 0;
    }
    
    /* Skip header lines that start with # */
    while (fgets(line, sizeof(line), file)) {
        if (line[0] != '#' && strlen(line) > 10) {
            if (sscanf(line, "BankID_%d", &clientNum) == 1) {
                if (clientNum > maxClientNum) {
                    maxClientNum = clientNum;
                }
            }
        }
    }
    
    fclose(file);
    *lastClientNum = maxClientNum;
    return 1;
}

/* Optimized updateLogFile function to properly format log entries */
void updateLogFile(FILE *logFile, const char *bankId, char opType, int amount, int balance) {
    /* Don't log zero amount operations */
    if (amount <= 0) return;
    
    /* Write transaction log in correct format */
    fprintf(logFile, "%s %c %d %d\n", bankId, opType, amount, balance);
    fflush(logFile);
}

/* Convert PID to string for semaphore naming */
static char pidString[16];
char *pidToString(pid_t pid) {
    snprintf(pidString, sizeof(pidString), "bank_%ld", (long)pid);
    return pidString;
}

/* Mutual exclusion for read/write operations */
int read_mutually_exclusive(sem_t *sem, int fd, void *buf, size_t size) {
    sem_wait(sem);
    int result = read(fd, buf, size);
    sem_post(sem);
    return result;
}

int write_mutually_exclusive(sem_t *sem, int fd, void *buf, size_t size) {
    sem_wait(sem);
    int result = write(fd, buf, size);
    sem_post(sem);
    return result;
}

/* Function to restore database from log file */
int restoreDatabaseFromLog(const char *filename, void *db_ptr) {
    /* Cast the void pointer to BankDatabase pointer */
    struct {
        struct {
            char bankId[20];
            int balance;
            int active;
        } accounts[MAX_BATCH_SIZE];
        int numAccounts;
    } *db = (void*)db_ptr;
    
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        return 0; /* File doesn't exist, nothing to restore */
    }
    
    char line[256];
    int accountsRestored = 0;
    
    /* Reset the database */
    db->numAccounts = 0;
    
    /* Process each line */
    while (fgets(line, sizeof(line), file)) {
        /* Skip header lines and end marker */
        if (line[0] == '#' || strlen(line) <= 1) {
            continue;
        }
        
        /* Parse BankID_XX D/W amount balance */
        char bankId[20];
        char opType;
        int amount, balance;
        
        if (sscanf(line, "%s %c %d %d", bankId, &opType, &amount, &balance) == 4) {
            /* Check if account already exists in our database */
            int index = -1;
            for (int i = 0; i < db->numAccounts; i++) {
                if (strcmp(db->accounts[i].bankId, bankId) == 0) {
                    index = i;
                    break;
                }
            }
            
            /* Create new account if needed */
            if (index == -1) {
                if (db->numAccounts >= 100) {
                    fprintf(stderr, "Maximum number of accounts reached\n");
                    continue;
                }
                
                index = db->numAccounts++;
                strncpy(db->accounts[index].bankId, bankId, sizeof(db->accounts[index].bankId) - 1);
                db->accounts[index].bankId[sizeof(db->accounts[index].bankId) - 1] = '\0';
                db->accounts[index].balance = 0;
                db->accounts[index].active = 1;
            }
            
            /* Update balance to the final value from log */
            db->accounts[index].balance = balance;
            
            /* If balance is 0, mark account as inactive */
            if (balance == 0) {
                db->accounts[index].active = 0;
            } else {
                /* Only count actual active accounts with non-zero balance */
                if (db->accounts[index].active) {
                    accountsRestored++;
                }
            }
        }
    }
    
    fclose(file);
    return accountsRestored;
}