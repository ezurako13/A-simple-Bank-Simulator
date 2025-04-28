# Bank Simulator Project

## Overview
This project implements a banking simulator using client-server architecture with process-based concurrency. The implementation follows the requirements specified in the CSE 344 Midterm Project for 2025.

## Project Structure

The project consists of the following files:

- **BankServer.c/h**: The main server program responsible for opening/closing accounts and managing bank transactions
- **BankClient.c/h**: The client program that sends deposit/withdraw requests to the server
- **bank_shared.h**: Shared definitions and data structures used by both client and server
- **bank_utils.c/h**: Utility functions for error handling, logging, and IPC
- **Makefile**: For compiling and testing the entire project

## Implementation Details

### Communication Mechanisms

1. **Server FIFO**: Used for client-to-server communication (initial connection)
2. **Client FIFOs**: Used for server-to-client communication (responses)
3. **Pipes**: Used for communication between the main server and teller processes
4. **Shared Memory**: Used for the bank database (satisfying the advanced requirement)
5. **Semaphores**: Used for synchronization and mutual exclusion

### Main Components

#### 1. Bank Server

The bank server has these main responsibilities:
- Creating and maintaining a bank database in shared memory
- Accepting client connections and creating teller processes for each client
- Updating the database based on teller requests
- Maintaining a log file of all transactions
- Handling signals properly (SIGINT, SIGTERM, SIGCHLD)

#### 2. Teller Processes

Teller processes are created by the main server (one for each client) and have these responsibilities:
- Receiving and validating client requests
- Communicating with the main server to update the bank database
- Sending responses back to the client
- Implementing the deposit and withdraw operations

#### 3. Bank Client

The client program has these responsibilities:
- Reading operations from a client file
- Connecting to the bank server
- Sending deposit/withdraw requests
- Receiving and displaying responses

### Advanced Implementation

For the full credit implementation, I've included:
- Custom process creation via `pid_t Teller(void* func, void* arg_func)`
- Custom process waiting via `int waitTeller(pid_t pid, int* status)`
- Using shared memory for the bank database
- Using semaphores for synchronization between processes

## How to Run

1. Compile the project:
   ```
   make
   ```

2. Run the server:
   ```
   ./BankServer AdaBank ServerFIFO_Name
   ```

3. Run a client (in a separate terminal):
   ```
   ./BankClient client_file.txt ServerFIFO_Name
   ```

The repository also includes test client files and test targets in the Makefile that can be used for testing the system with different scenarios.

## Project Features

- **Robust Error Handling**: All system calls check for errors and provide appropriate error messages
- **Proper Resource Cleanup**: All resources (FIFOs, shared memory, semaphores) are properly cleaned up
- **Signal Handling**: The system properly handles signals to ensure graceful termination
- **Scalability**: The system can handle multiple clients concurrently
- **Transaction Consistency**: The database is protected from race conditions using semaphores

## Testing

The project includes several test scenarios in the Makefile:
- Single client with deposit operations
- Multiple clients with deposit and withdraw operations
- Multiple clients with invalid operations

These tests help verify that the system works correctly in different scenarios and handles errors appropriately.