#!/bin/bash

# Improved Valgrind test script for Bank Simulator
# This script ensures proper synchronization between server and client

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Valgrind options
VALGRIND="valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
          --trace-children=yes --track-fds=yes --child-silent-after-fork=no"

echo -e "${BLUE}Bank Simulator Valgrind Test${NC}"
echo -e "${BLUE}==========================${NC}"

# Clean previous builds and create logs directory
echo -e "${YELLOW}Cleaning up and preparing...${NC}"
make clean
mkdir -p valgrind_logs

# Kill any existing processes
killall -9 BankServer BankClient 2>/dev/null
make clean_fifos

# Compile with debug flags
echo -e "${YELLOW}Compiling with debug flags...${NC}"
make CFLAGS="-Wall -Wextra -pthread -g -O0" all

# Function to check if FIFO exists
check_fifo() {
    for i in {1..20}; do
        if [ -p "/tmp/ServerFIFO_Name" ]; then
            echo -e "${GREEN}FIFO /tmp/ServerFIFO_Name exists!${NC}"
            return 0
        fi
        echo -e "${YELLOW}Waiting for FIFO to be created (attempt $i/20)...${NC}"
        sleep 1
    done
    echo -e "${RED}FIFO not created after 20 seconds. Server might not be running properly.${NC}"
    return 1
}

# Function to run a test with a specific client file
run_test() {
    CLIENT_FILE=$1
    TEST_NAME=$2
    
    echo -e "\n${BLUE}Running Test: $TEST_NAME${NC}"
    
    # Start server
    echo -e "${YELLOW}Starting server with Valgrind...${NC}"
    $VALGRIND --log-file="valgrind_logs/server_${TEST_NAME}.log" ./BankServer AdaBank ServerFIFO_Name &
    SERVER_PID=$!
    
    # Check if FIFO is created
    check_fifo
    if [ $? -ne 0 ]; then
        echo -e "${RED}Aborting test due to FIFO creation failure.${NC}"
        kill -9 $SERVER_PID 2>/dev/null
        return 1
    fi
    
    # Run client
    echo -e "${YELLOW}Running client with Valgrind...${NC}"
    $VALGRIND --log-file="valgrind_logs/client_${TEST_NAME}.log" ./BankClient $CLIENT_FILE ServerFIFO_Name
    
    # Give server time to process
    sleep 2
    
    # Terminate server
    echo -e "${YELLOW}Terminating server...${NC}"
    kill -TERM $SERVER_PID
    wait $SERVER_PID 2>/dev/null
    
    # Cleanup
    make clean_fifos
    
    echo -e "${GREEN}Test $TEST_NAME completed.${NC}"
    echo -e "${YELLOW}Logs available at:${NC}"
    echo -e "  - valgrind_logs/server_${TEST_NAME}.log"
    echo -e "  - valgrind_logs/client_${TEST_NAME}.log"
}

# Function to check for leaks in logs
check_leaks() {
    LOG_FILE=$1
    NAME=$2
    
    echo -e "\n${YELLOW}Checking for leaks in $NAME...${NC}"
    
    # Check for memory leaks
    LEAKED=$(grep -A 1 "LEAK SUMMARY" $LOG_FILE | grep -o "definitely lost: [0-9,]* bytes" | grep -v "definitely lost: 0 bytes")
    if [ -z "$LEAKED" ]; then
        echo -e "${GREEN}No memory leaks detected.${NC}"
    else
        echo -e "${RED}Memory leaks detected: $LEAKED${NC}"
    fi
    
    # Check for file descriptor leaks
    FD_LEAKS=$(grep -A 2 "FILE DESCRIPTORS" $LOG_FILE | grep -v "Open file descriptor" | grep -v "inherited from parent")
    if [ -z "$FD_LEAKS" ]; then
        echo -e "${GREEN}No file descriptor leaks detected.${NC}"
    else
        echo -e "${RED}File descriptor leaks detected:${NC}"
        echo "$FD_LEAKS"
    fi
}

# Run tests with different client files
run_test "Client1.file" "client1"
run_test "Client2.file" "client2"
run_test "Client3.file" "client3"

# Run a concurrent client test
echo -e "\n${BLUE}Running Concurrent Client Test${NC}"

# Start server
echo -e "${YELLOW}Starting server with Valgrind...${NC}"
$VALGRIND --log-file="valgrind_logs/server_concurrent.log" ./BankServer AdaBank ServerFIFO_Name &
SERVER_PID=$!

# Check if FIFO is created
check_fifo
if [ $? -eq 0 ]; then
    # Run clients concurrently (without Valgrind to reduce overhead)
    echo -e "${YELLOW}Running clients concurrently...${NC}"
    ./BankClient Client1.file ServerFIFO_Name > valgrind_logs/client1_concurrent.log 2>&1 &
    CLIENT1_PID=$!
    ./BankClient Client2.file ServerFIFO_Name > valgrind_logs/client2_concurrent.log 2>&1 &
    CLIENT2_PID=$!
    ./BankClient Client3.file ServerFIFO_Name > valgrind_logs/client3_concurrent.log 2>&1 &
    CLIENT3_PID=$!
    
    # Wait for clients to finish
    wait $CLIENT1_PID 2>/dev/null
    wait $CLIENT2_PID 2>/dev/null
    wait $CLIENT3_PID 2>/dev/null
    
    # Give server time to process
    sleep 2
fi

# Terminate server
echo -e "${YELLOW}Terminating server...${NC}"
kill -TERM $SERVER_PID
wait $SERVER_PID 2>/dev/null

# Cleanup
make clean_fifos

# Analyze logs
echo -e "\n${BLUE}Analyzing Valgrind Logs${NC}"
echo -e "${BLUE}====================${NC}"

# Check each log file
for log_file in valgrind_logs/server_*.log; do
    if [ -f "$log_file" ]; then
        check_leaks "$log_file" "$(basename "$log_file")"
    fi
done

for log_file in valgrind_logs/client_*.log; do
    if [ -f "$log_file" ]; then
        check_leaks "$log_file" "$(basename "$log_file")"
    fi
done

echo -e "\n${BLUE}Valgrind Test Complete${NC}"
echo -e "${YELLOW}Check valgrind_logs directory for complete reports.${NC}"