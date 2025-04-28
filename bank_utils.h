/* bank_utils.h
 * Utility functions for bank simulator
 */
#ifndef BANK_UTILS_H
#define BANK_UTILS_H

#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <sys/types.h>
#include <semaphore.h>

/* Define maximum number of operations in a batch */
#define MAX_BATCH_SIZE 500

/* Error handling functions */
void errExit(const char *format, ...);
void errExitWithLog(FILE *log, const char *format, ...);
void errLog(FILE *log, const char *format, ...);
void printLog(FILE *log, const char *format, ...);

/* IPC utility functions */
int read_with_timeout(int fd, void *buf, size_t count, int timeout_sec);
int write_with_retry(int fd, const void *buf, size_t count, int max_retries);

/* Bank-specific utility functions */
void generateBankId(char *bankId, int clientNum);
void getCurrentTimeStr(char *timeStr, size_t size);
int readLogFile(const char *filename, int *lastClientNum);
void updateLogFile(FILE *logFile, const char *bankId, char opType, int amount, int balance);
int restoreDatabaseFromLog(const char *filename, void *db);

/* PID to string conversion for semaphore naming */
char *pidToString(pid_t pid);

/* Mutual exclusion for read/write operations */
int read_mutually_exclusive(sem_t *sem, int fd, void *buf, size_t size);
int write_mutually_exclusive(sem_t *sem, int fd, void *buf, size_t size);

#endif /* BANK_UTILS_H */