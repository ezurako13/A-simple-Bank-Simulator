#!/bin/bash

# Semaphore leak test script for Bank Simulator
# This script specifically tests for leaked semaphores

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}Bank Simulator Semaphore Leak Test${NC}"
echo -e "${BLUE}=================================${NC}"

# Check if we have permission to check semaphores
if [ ! -d "/dev/shm" ] || [ ! -r "/dev/shm" ]; then
    echo -e "${RED}Cannot access /dev/shm - may need root permissions to check semaphores${NC}"
    echo -e "${YELLOW}Continuing with limited semaphore testing...${NC}"
fi

# Clean up previous builds and test leftovers
echo -e "${YELLOW}Cleaning up previous builds and test artifacts...${NC}"
make clean
make clean_fifos

# Clean up any leftover semaphores
echo -e "${YELLOW}Removing any leftover semaphores...${NC}"
find /dev/shm -name "sem.bank_*" -delete 2>/dev/null

# Compile the project
echo -e "${YELLOW}Compiling the project...${NC}"
make all

if [ $? -ne 0 ]; then
    echo -e "${RED}Compilation failed. Exiting tests.${NC}"
    exit 1
fi

# Kill any running bank processes
killall -9 BankServer BankClient 2>/dev/null

# Function to count semaphores
count_sems() {
    SEM_COUNT=$(find /dev/shm -name "sem.bank_*" 2>/dev/null | wc -l)
    echo $SEM_COUNT
}

# Create a directory for logs
mkdir -p sem_logs

# Test 1: Check semaphores during normal operation
echo -e "\n${BLUE}Test 1: Check Semaphores During Normal Operation${NC}"

# Count initial semaphores
INITIAL_SEM_COUNT=$(count_sems)
echo -e "${BLUE}Initial semaphore count: $INITIAL_SEM_COUNT${NC}"

# Start server
echo -e "${YELLOW}Starting server...${NC}"
./BankServer AdaBank ServerFIFO_Name > sem_logs/server.log 2>&1 &
SERVER_PID=$!
sleep 2

# Count semaphores after server start
SERVER_SEM_COUNT=$(count_sems)
echo -e "${BLUE}Semaphore count after server start: $SERVER_SEM_COUNT${NC}"
echo -e "${YELLOW}List of semaphores after server start:${NC}"
find /dev/shm -name "sem.bank_*" 2>/dev/null | sort

# Run client1
echo -e "${YELLOW}Running client1...${NC}"
./BankClient Client1.file ServerFIFO_Name > sem_logs/client1.log 2>&1

# Count semaphores after client1
AFTER_CLIENT1_SEM_COUNT=$(count_sems)
echo -e "${BLUE}Semaphore count after client1: $AFTER_CLIENT1_SEM_COUNT${NC}"
echo -e "${YELLOW}List of semaphores after client1:${NC}"
find /dev/shm -name "sem.bank_*" 2>/dev/null | sort

# Run client2
echo -e "${YELLOW}Running client2...${NC}"
./BankClient Client2.file ServerFIFO_Name > sem_logs/client2.log 2>&1

# Count semaphores after client2
AFTER_CLIENT2_SEM_COUNT=$(count_sems)
echo -e "${BLUE}Semaphore count after client2: $AFTER_CLIENT2_SEM_COUNT${NC}"
echo -e "${YELLOW}List of semaphores after client2:${NC}"
find /dev/shm -name "sem.bank_*" 2>/dev/null | sort

# Run client3
echo -e "${YELLOW}Running client3...${NC}"
./BankClient Client3.file ServerFIFO_Name > sem_logs/client3.log 2>&1

# Count semaphores after client3
AFTER_CLIENT3_SEM_COUNT=$(count_sems)
echo -e "${BLUE}Semaphore count after client3: $AFTER_CLIENT3_SEM_COUNT${NC}"
echo -e "${YELLOW}List of semaphores after client3:${NC}"
find /dev/shm -name "sem.bank_*" 2>/dev/null | sort

# Kill server gracefully
echo -e "${YELLOW}Terminating server...${NC}"
kill -TERM $SERVER_PID
wait $SERVER_PID 2>/dev/null
sleep 1

# Count semaphores after server termination
AFTER_SERVER_TERM_SEM_COUNT=$(count_sems)
echo -e "${BLUE}Semaphore count after server termination: $AFTER_SERVER_TERM_SEM_COUNT${NC}"
if [ "$AFTER_SERVER_TERM_SEM_COUNT" -gt "$INITIAL_SEM_COUNT" ]; then
    echo -e "${RED}Semaphore leak detected! $AFTER_SERVER_TERM_SEM_COUNT semaphores remain:${NC}"
    find /dev/shm -name "sem.bank_*" 2>/dev/null | sort
else
    echo -e "${GREEN}No semaphore leak detected after normal operation${NC}"
fi

# Clean up any leftover semaphores
find /dev/shm -name "sem.bank_*" -delete 2>/dev/null

# Test 2: Check semaphores after server crash
echo -e "\n${BLUE}Test 2: Check Semaphores After Server Crash${NC}"

# Count initial semaphores
INITIAL_SEM_COUNT=$(count_sems)
echo -e "${BLUE}Initial semaphore count: $INITIAL_SEM_COUNT${NC}"

# Start server
echo -e "${YELLOW}Starting server...${NC}"
./BankServer AdaBank ServerFIFO_Name > sem_logs/server_crash.log 2>&1 &
SERVER_PID=$!
sleep 2

# Count semaphores after server start
SERVER_SEM_COUNT=$(count_sems)
echo -e "${BLUE}Semaphore count after server start: $SERVER_SEM_COUNT${NC}"
echo -e "${YELLOW}List of semaphores after server start:${NC}"
find /dev/shm -name "sem.bank_*" 2>/dev/null | sort

# Start a client
echo -e "${YELLOW}Starting client...${NC}"
./BankClient Client1.file ServerFIFO_Name > sem_logs/crash_client.log 2>&1 &
CLIENT_PID=$!
sleep 1

# Force kill server
echo -e "${YELLOW}Force killing server...${NC}"
kill -9 $SERVER_PID
sleep 1

# Count semaphores after server crash
AFTER_CRASH_SEM_COUNT=$(count_sems)
echo -e "${BLUE}Semaphore count after server crash: $AFTER_CRASH_SEM_COUNT${NC}"
if [ "$AFTER_CRASH_SEM_COUNT" -gt "$INITIAL_SEM_COUNT" ]; then
    echo -e "${RED}Semaphore leak detected after crash! $AFTER_CRASH_SEM_COUNT semaphores remain:${NC}"
    find /dev/shm -name "sem.bank_*" 2>/dev/null | sort
else
    echo -e "${GREEN}No semaphore leak detected after server crash${NC}"
fi

# Kill client if still running
kill -9 $CLIENT_PID 2>/dev/null

# Clean up any leftover semaphores
find /dev/shm -name "sem.bank_*" -delete 2>/dev/null

# Test 3: Check semaphores during concurrent operations
echo -e "\n${BLUE}Test 3: Check Semaphores During Concurrent Operations${NC}"

# Count initial semaphores
INITIAL_SEM_COUNT=$(count_sems)
echo -e "${BLUE}Initial semaphore count: $INITIAL_SEM_COUNT${NC}"

# Start server
echo -e "${YELLOW}Starting server...${NC}"
./BankServer AdaBank ServerFIFO_Name > sem_logs/server_concurrent.log 2>&1 &
SERVER_PID=$!
sleep 2

# Start clients concurrently
echo -e "${YELLOW}Starting clients concurrently...${NC}"
./BankClient Client1.file ServerFIFO_Name > sem_logs/concurrent_client1.log 2>&1 &
CLIENT1_PID=$!
./BankClient Client2.file ServerFIFO_Name > sem_logs/concurrent_client2.log 2>&1 &
CLIENT2_PID=$!
./BankClient Client3.file ServerFIFO_Name > sem_logs/concurrent_client3.log 2>&1 &
CLIENT3_PID=$!

# Check semaphores during concurrent operation
sleep 1
DURING_CONCURRENT_SEM_COUNT=$(count_sems)
echo -e "${BLUE}Semaphore count during concurrent operations: $DURING_CONCURRENT_SEM_COUNT${NC}"
echo -e "${YELLOW}List of semaphores during concurrent operations:${NC}"
find /dev/shm -name "sem.bank_*" 2>/dev/null | sort

# Wait for clients to finish
wait $CLIENT1_PID 2>/dev/null
wait $CLIENT2_PID 2>/dev/null
wait $CLIENT3_PID 2>/dev/null
sleep 2

# Check semaphores after clients finish
AFTER_CLIENTS_SEM_COUNT=$(count_sems)
echo -e "${BLUE}Semaphore count after clients finish: $AFTER_CLIENTS_SEM_COUNT${NC}"
echo -e "${YELLOW}List of semaphores after clients finish:${NC}"
find /dev/shm -name "sem.bank_*" 2>/dev/null | sort

# Kill server gracefully
echo -e "${YELLOW}Terminating server...${NC}"
kill -TERM $SERVER_PID
wait $SERVER_PID 2>/dev/null
sleep 1

# Count semaphores after server termination
AFTER_CONCURRENT_SEM_COUNT=$(count_sems)
echo -e "${BLUE}Semaphore count after concurrent test: $AFTER_CONCURRENT_SEM_COUNT${NC}"
if [ "$AFTER_CONCURRENT_SEM_COUNT" -gt "$INITIAL_SEM_COUNT" ]; then
    echo -e "${RED}Semaphore leak detected! $AFTER_CONCURRENT_SEM_COUNT semaphores remain:${NC}"
    find /dev/shm -name "sem.bank_*" 2>/dev/null | sort
else
    echo -e "${GREEN}No semaphore leak detected after concurrent test${NC}"
fi

# Clean up
echo -e "${YELLOW}Final cleanup...${NC}"
find /dev/shm -name "sem.bank_*" -delete 2>/dev/null
make clean_fifos

echo -e "\n${BLUE}Semaphore Leak Test Complete${NC}"
echo -e "${YELLOW}Check sem_logs directory for detailed logs.${NC}"