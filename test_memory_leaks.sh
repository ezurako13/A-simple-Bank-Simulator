#!/bin/bash

# Valgrind test script for Bank Simulator
# This script tests for memory leaks, resource leaks, and orphan processes

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}Bank Simulator Memory Leak Test${NC}"
echo -e "${BLUE}=============================${NC}"

# Clean up previous builds and test leftovers
echo -e "${YELLOW}Cleaning up previous builds and test artifacts...${NC}"
make clean
make clean_fifos

# Check if valgrind is installed
if ! command -v valgrind &> /dev/null; then
    echo -e "${RED}Valgrind is not installed. Please install it first.${NC}"
    exit 1
fi

# Compile the project
echo -e "${YELLOW}Compiling the project...${NC}"
make all

if [ $? -ne 0 ]; then
    echo -e "${RED}Compilation failed. Exiting tests.${NC}"
    exit 1
fi

# Create a directory for valgrind logs
mkdir -p valgrind_logs

# Function to check for leftover resources
check_leftovers() {
    echo -e "${YELLOW}Checking for leftover resources...${NC}"
    
    # Check for any leftover FIFOs
    FIFO_COUNT=$(find /tmp -name "bank_*" | wc -l)
    if [ $FIFO_COUNT -gt 0 ]; then
        echo -e "${RED}Found $FIFO_COUNT leftover FIFO(s):${NC}"
        find /tmp -name "bank_*"
    else
        echo -e "${GREEN}No leftover FIFOs found.${NC}"
    fi
    
    # Check for leftover semaphores
    SEM_COUNT=$(find /dev/shm -name "sem.bank_*" 2>/dev/null | wc -l)
    if [ $SEM_COUNT -gt 0 ]; then
        echo -e "${RED}Found $SEM_COUNT leftover semaphore(s):${NC}"
        find /dev/shm -name "sem.bank_*" 2>/dev/null
    else
        echo -e "${GREEN}No leftover semaphores found.${NC}"
    fi
    
    # Check for orphan processes
    ORPHAN_COUNT=$(ps aux | grep -E "BankServer|BankClient" | grep -v grep | grep -v $0 | wc -l)
    if [ $ORPHAN_COUNT -gt 0 ]; then
        echo -e "${RED}Found $ORPHAN_COUNT orphan process(es):${NC}"
        ps aux | grep -E "BankServer|BankClient" | grep -v grep | grep -v $0
    else
        echo -e "${GREEN}No orphan processes found.${NC}"
    fi
}

# Kill any running bank processes
killall -9 BankServer BankClient 2>/dev/null

# Clean up any leftovers from previous runs
make clean_fifos

# Test 1: Server memory leak test with client1
echo -e "\n${BLUE}Test 1: Server with Client1${NC}"
echo -e "${YELLOW}Starting server with valgrind...${NC}"
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose --log-file=valgrind_logs/server_test1.log ./BankServer AdaBank ServerFIFO_Name &
SERVER_PID=$!

# Wait for server to initialize
sleep 2

echo -e "${YELLOW}Running client1...${NC}"
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose --log-file=valgrind_logs/client1.log ./BankClient Client1.file ServerFIFO_Name

# Allow time for server to process
sleep 2

# Kill the server gracefully
echo -e "${YELLOW}Terminating server...${NC}"
kill -TERM $SERVER_PID
wait $SERVER_PID 2>/dev/null

# Check for leftover resources
check_leftovers

# Test 2: Server memory leak test with client2
echo -e "\n${BLUE}Test 2: Server with Client2${NC}"
echo -e "${YELLOW}Starting server with valgrind...${NC}"
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose --log-file=valgrind_logs/server_test2.log ./BankServer AdaBank ServerFIFO_Name &
SERVER_PID=$!

# Wait for server to initialize
sleep 2

echo -e "${YELLOW}Running client2...${NC}"
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose --log-file=valgrind_logs/client2.log ./BankClient Client2.file ServerFIFO_Name

# Allow time for server to process
sleep 2

# Kill the server gracefully
echo -e "${YELLOW}Terminating server...${NC}"
kill -TERM $SERVER_PID
wait $SERVER_PID 2>/dev/null

# Check for leftover resources
check_leftovers

# Test 3: Server memory leak test with client3
echo -e "\n${BLUE}Test 3: Server with Client3${NC}"
echo -e "${YELLOW}Starting server with valgrind...${NC}"
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose --log-file=valgrind_logs/server_test3.log ./BankServer AdaBank ServerFIFO_Name &
SERVER_PID=$!

# Wait for server to initialize
sleep 2

echo -e "${YELLOW}Running client3...${NC}"
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose --log-file=valgrind_logs/client3.log ./BankClient Client3.file ServerFIFO_Name

# Allow time for server to process
sleep 2

# Kill the server gracefully
echo -e "${YELLOW}Terminating server...${NC}"
kill -TERM $SERVER_PID
wait $SERVER_PID 2>/dev/null

# Check for leftover resources
check_leftovers

# Test 4: Concurrent client test
echo -e "\n${BLUE}Test 4: Server with Concurrent Clients${NC}"
echo -e "${YELLOW}Starting server with valgrind...${NC}"
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose --log-file=valgrind_logs/server_test4.log ./BankServer AdaBank ServerFIFO_Name &
SERVER_PID=$!

# Wait for server to initialize
sleep 2

echo -e "${YELLOW}Running client1, client2, and client3 concurrently...${NC}"
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose --log-file=valgrind_logs/client1_concurrent.log ./BankClient Client1.file ServerFIFO_Name &
CLIENT1_PID=$!
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose --log-file=valgrind_logs/client2_concurrent.log ./BankClient Client2.file ServerFIFO_Name &
CLIENT2_PID=$!
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose --log-file=valgrind_logs/client3_concurrent.log ./BankClient Client3.file ServerFIFO_Name &
CLIENT3_PID=$!

# Allow time for clients to finish
wait $CLIENT1_PID 2>/dev/null
wait $CLIENT2_PID 2>/dev/null
wait $CLIENT3_PID 2>/dev/null

# Allow time for server to process
sleep 2

# Kill the server gracefully
echo -e "${YELLOW}Terminating server...${NC}"
kill -TERM $SERVER_PID
wait $SERVER_PID 2>/dev/null

# Check for leftover resources
check_leftovers

# Test 5: Server crash test
echo -e "\n${BLUE}Test 5: Server Crash Test${NC}"
echo -e "${YELLOW}Starting server with valgrind...${NC}"
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose --log-file=valgrind_logs/server_crash.log ./BankServer AdaBank ServerFIFO_Name &
SERVER_PID=$!

# Wait for server to initialize
sleep 2

echo -e "${YELLOW}Starting client1...${NC}"
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose --log-file=valgrind_logs/client_crash.log ./BankClient Client1.file ServerFIFO_Name &
CLIENT_PID=$!

# Wait a bit for the client to start
sleep 1

# Kill the server abruptly
echo -e "${YELLOW}Simulating server crash...${NC}"
kill -9 $SERVER_PID

# Wait for the client to finish or time out
wait $CLIENT_PID 2>/dev/null || echo -e "${YELLOW}Client didn't exit cleanly${NC}"

# Check for leftover resources
check_leftovers

# Clean up final leftovers
echo -e "\n${YELLOW}Final cleanup...${NC}"
make clean_fifos
killall -9 BankServer BankClient 2>/dev/null

# Analyze Valgrind logs
echo -e "\n${BLUE}Analyzing Valgrind Logs${NC}"
echo -e "${BLUE}====================${NC}"

analyze_log() {
    LOG_FILE=$1
    NAME=$2
    
    if [ ! -f "$LOG_FILE" ]; then
        echo -e "${RED}Log file $LOG_FILE not found!${NC}"
        return
    }
    
    # Extract memory leak summary
    LEAKS=$(grep -A 1 "LEAK SUMMARY" $LOG_FILE | tail -1 | grep -oE "[0-9,]+ bytes" | head -1)
    
    # Extract error count
    ERRORS=$(grep "ERROR SUMMARY" $LOG_FILE | grep -oE "[0-9,]+ errors" | head -1)
    
    if [[ -z "$LEAKS" || "$LEAKS" == "0 bytes" ]] && [[ -z "$ERRORS" || "$ERRORS" == "0 errors" ]]; then
        echo -e "${GREEN}$NAME: No leaks or errors detected!${NC}"
    else
        echo -e "${RED}$NAME: Found $LEAKS leaked and $ERRORS${NC}"
        echo -e "${YELLOW}Details for $NAME:${NC}"
        grep -A 20 "LEAK SUMMARY" $LOG_FILE | head -21
        grep -A 3 "ERROR SUMMARY" $LOG_FILE | head -4
    fi
}

# Analyze each log file
for log_file in valgrind_logs/*.log; do
    if [ -f "$log_file" ]; then
        name=$(basename "$log_file" .log)
        analyze_log "$log_file" "$name"
    fi
done

echo -e "\n${BLUE}Memory Leak Test Complete${NC}"
echo -e "${YELLOW}Check valgrind_logs directory for detailed reports.${NC}"
echo -e "${YELLOW}If you want to debug specific leaks, run: less valgrind_logs/[log_file]${NC}"