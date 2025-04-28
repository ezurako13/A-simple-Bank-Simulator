#!/bin/bash

# File descriptor leak test script for Bank Simulator
# This script specifically targets file descriptor leaks

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}Bank Simulator File Descriptor Leak Test${NC}"
echo -e "${BLUE}======================================${NC}"

# Clean up previous builds and test leftovers
echo -e "${YELLOW}Cleaning up previous builds and test artifacts...${NC}"
make clean
make clean_fifos

# Compile the project
echo -e "${YELLOW}Compiling the project...${NC}"
make all

if [ $? -ne 0 ]; then
    echo -e "${RED}Compilation failed. Exiting tests.${NC}"
    exit 1
fi

# Kill any running bank processes
killall -9 BankServer BankClient 2>/dev/null

# Function to count open file descriptors for a process
count_fds() {
    PID=$1
    if [ -d "/proc/$PID/fd" ]; then
        FD_COUNT=$(ls -la /proc/$PID/fd | wc -l)
        # Subtract 3 for ., .., and the command itself
        FD_COUNT=$((FD_COUNT - 3))
        echo $FD_COUNT
    else
        echo "0"
    fi
}

# Function to check for pipe leaks
check_pipe_leaks() {
    PID=$1
    NAME=$2
    
    if [ -d "/proc/$PID/fd" ]; then
        echo -e "${YELLOW}Checking pipe descriptors for $NAME (PID: $PID)...${NC}"
        PIPES=$(ls -la /proc/$PID/fd | grep pipe | wc -l)
        echo -e "  ${BLUE}$NAME has $PIPES open pipe(s)${NC}"
        
        # List all file descriptors
        echo -e "${YELLOW}  Open file descriptors for $NAME:${NC}"
        ls -la /proc/$PID/fd | tail -n +4
    else
        echo -e "${RED}  $NAME (PID: $PID) is not running${NC}"
    fi
}

# Create a directory for logs
mkdir -p fd_logs

# Test 1: Monitor FDs during normal operation
echo -e "\n${BLUE}Test 1: Monitor File Descriptors During Normal Operation${NC}"

# Start server
echo -e "${YELLOW}Starting server...${NC}"
./BankServer AdaBank ServerFIFO_Name > fd_logs/server.log 2>&1 &
SERVER_PID=$!
sleep 2

# Check initial FD count
INITIAL_FD_COUNT=$(count_fds $SERVER_PID)
echo -e "${BLUE}Initial server FD count: $INITIAL_FD_COUNT${NC}"

# Run the first client
echo -e "${YELLOW}Running client1...${NC}"
./BankClient Client1.file ServerFIFO_Name > fd_logs/client1.log 2>&1

# Check FD count after client1
AFTER_CLIENT1_FD_COUNT=$(count_fds $SERVER_PID)
echo -e "${BLUE}Server FD count after client1: $AFTER_CLIENT1_FD_COUNT${NC}"
if [ "$AFTER_CLIENT1_FD_COUNT" -gt "$INITIAL_FD_COUNT" ]; then
    echo -e "${RED}Potential FD leak! FD count increased by $((AFTER_CLIENT1_FD_COUNT - INITIAL_FD_COUNT))${NC}"
    check_pipe_leaks $SERVER_PID "Server"
else
    echo -e "${GREEN}No FD leak detected after client1${NC}"
fi

# Run the second client
echo -e "${YELLOW}Running client2...${NC}"
./BankClient Client2.file ServerFIFO_Name > fd_logs/client2.log 2>&1

# Check FD count after client2
AFTER_CLIENT2_FD_COUNT=$(count_fds $SERVER_PID)
echo -e "${BLUE}Server FD count after client2: $AFTER_CLIENT2_FD_COUNT${NC}"
if [ "$AFTER_CLIENT2_FD_COUNT" -gt "$AFTER_CLIENT1_FD_COUNT" ]; then
    echo -e "${RED}Potential FD leak! FD count increased by $((AFTER_CLIENT2_FD_COUNT - AFTER_CLIENT1_FD_COUNT))${NC}"
    check_pipe_leaks $SERVER_PID "Server"
else
    echo -e "${GREEN}No FD leak detected after client2${NC}"
fi

# Run the third client
echo -e "${YELLOW}Running client3...${NC}"
./BankClient Client3.file ServerFIFO_Name > fd_logs/client3.log 2>&1

# Check FD count after client3
AFTER_CLIENT3_FD_COUNT=$(count_fds $SERVER_PID)
echo -e "${BLUE}Server FD count after client3: $AFTER_CLIENT3_FD_COUNT${NC}"
if [ "$AFTER_CLIENT3_FD_COUNT" -gt "$AFTER_CLIENT2_FD_COUNT" ]; then
    echo -e "${RED}Potential FD leak! FD count increased by $((AFTER_CLIENT3_FD_COUNT - AFTER_CLIENT2_FD_COUNT))${NC}"
    check_pipe_leaks $SERVER_PID "Server"
else
    echo -e "${GREEN}No FD leak detected after client3${NC}"
fi

# Kill the server gracefully
echo -e "${YELLOW}Terminating server...${NC}"
kill -TERM $SERVER_PID
wait $SERVER_PID 2>/dev/null

# Test 2: Monitor FDs during stress test (concurrent clients)
echo -e "\n${BLUE}Test 2: Monitor File Descriptors During Stress Test${NC}"

# Start server
echo -e "${YELLOW}Starting server...${NC}"
./BankServer AdaBank ServerFIFO_Name > fd_logs/server_stress.log 2>&1 &
SERVER_PID=$!
sleep 2

# Check initial FD count
INITIAL_FD_COUNT=$(count_fds $SERVER_PID)
echo -e "${BLUE}Initial server FD count: $INITIAL_FD_COUNT${NC}"

# Run clients concurrently
echo -e "${YELLOW}Running clients concurrently...${NC}"
./BankClient Client1.file ServerFIFO_Name > fd_logs/stress_client1.log 2>&1 &
CLIENT1_PID=$!
./BankClient Client2.file ServerFIFO_Name > fd_logs/stress_client2.log 2>&1 &
CLIENT2_PID=$!
./BankClient Client3.file ServerFIFO_Name > fd_logs/stress_client3.log 2>&1 &
CLIENT3_PID=$!

# Check FD count during concurrent operation
sleep 1
DURING_STRESS_FD_COUNT=$(count_fds $SERVER_PID)
echo -e "${BLUE}Server FD count during stress: $DURING_STRESS_FD_COUNT${NC}"
if [ "$DURING_STRESS_FD_COUNT" -gt "$INITIAL_FD_COUNT" ]; then
    echo -e "${YELLOW}FD count increased during concurrent processing (expected)${NC}"
    check_pipe_leaks $SERVER_PID "Server"
else
    echo -e "${BLUE}No FD increase during concurrent processing (unusual)${NC}"
fi

# Wait for clients to finish
wait $CLIENT1_PID 2>/dev/null
wait $CLIENT2_PID 2>/dev/null
wait $CLIENT3_PID 2>/dev/null
sleep 2

# Check FD count after stress test
AFTER_STRESS_FD_COUNT=$(count_fds $SERVER_PID)
echo -e "${BLUE}Server FD count after stress test: $AFTER_STRESS_FD_COUNT${NC}"
if [ "$AFTER_STRESS_FD_COUNT" -gt "$INITIAL_FD_COUNT" ]; then
    echo -e "${RED}Potential FD leak! FD count increased by $((AFTER_STRESS_FD_COUNT - INITIAL_FD_COUNT))${NC}"
    check_pipe_leaks $SERVER_PID "Server"
else
    echo -e "${GREEN}No persistent FD leak detected after stress test${NC}"
fi

# Kill the server gracefully
echo -e "${YELLOW}Terminating server...${NC}"
kill -TERM $SERVER_PID
wait $SERVER_PID 2>/dev/null

# Test 3: Check for leftover FIFOs after server crash
echo -e "\n${BLUE}Test 3: Check for Leftover FIFOs After Server Crash${NC}"

# Start server
echo -e "${YELLOW}Starting server...${NC}"
./BankServer AdaBank ServerFIFO_Name > fd_logs/server_crash.log 2>&1 &
SERVER_PID=$!
sleep 2

# Count initial FIFOs
INITIAL_FIFO_COUNT=$(find /tmp -name "bank_*" | wc -l)
echo -e "${BLUE}Initial FIFO count: $INITIAL_FIFO_COUNT${NC}"

# Start a client
echo -e "${YELLOW}Starting client...${NC}"
./BankClient Client1.file ServerFIFO_Name > fd_logs/crash_client.log 2>&1 &
CLIENT_PID=$!
sleep 1

# Force kill the server
echo -e "${YELLOW}Force killing server...${NC}"
kill -9 $SERVER_PID
sleep 1

# Count FIFOs after crash
AFTER_CRASH_FIFO_COUNT=$(find /tmp -name "bank_*" | wc -l)
echo -e "${BLUE}FIFO count after crash: $AFTER_CRASH_FIFO_COUNT${NC}"
if [ "$AFTER_CRASH_FIFO_COUNT" -gt 0 ]; then
    echo -e "${RED}Leftover FIFOs detected after crash:${NC}"
    find /tmp -name "bank_*" -ls
else
    echo -e "${GREEN}No leftover FIFOs after crash${NC}"
fi

# Clean up
echo -e "${YELLOW}Cleaning up...${NC}"
kill -9 $CLIENT_PID 2>/dev/null
make clean_fifos

echo -e "\n${BLUE}File Descriptor Leak Test Complete${NC}"
echo -e "${YELLOW}Check fd_logs directory for detailed logs.${NC}"