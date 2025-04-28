# Makefile for Bank Simulator Project

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -pthread
LDFLAGS = -lrt -pthread

# Valgrind specific flags
VALGRIND_FLAGS = -g -O0
VALGRIND = valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
           --trace-children=yes --track-fds=yes --child-silent-after-fork=no

# FIFO name for server
SERVER_FIFO = ServerFIFO_Name

# Source files
COMMON_SRCS = bank_utils.c
SERVER_SRCS = BankServer.c $(COMMON_SRCS)
CLIENT_SRCS = BankClient.c $(COMMON_SRCS)

# Object files
COMMON_OBJS = $(COMMON_SRCS:.c=.o)
SERVER_OBJS = $(SERVER_SRCS:.c=.o)
CLIENT_OBJS = $(CLIENT_SRCS:.c=.o)

# Executables
SERVER = BankServer
CLIENT = BankClient

# Default target
all: $(SERVER) $(CLIENT) create_client_files

# Valgrind build target - compiles with debug flags
val: CFLAGS += $(VALGRIND_FLAGS)
val: clean all
	@echo "Built with Valgrind-friendly flags"
	@mkdir -p valgrind_logs

# Server executable
$(SERVER): $(SERVER_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

# Client executable
$(CLIENT): $(CLIENT_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

# Generic rule for object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Create the client test files
create_client_files:
	@echo "Creating sample client files..."
	@echo "N deposit 300" > Client1.file
	@echo "BankID_None withdraw 30" >> Client1.file
	@echo "N deposit 2000" >> Client1.file
	@echo "BankID_01 withdraw 300" > Client2.file
	@echo "N deposit 20" >> Client2.file
	@echo "BankID_02 withdraw 30" > Client3.file
	@echo "N deposit 2000" >> Client3.file
	@echo "BankID_02 deposit 200" >> Client3.file
	@echo "BankID_02 withdraw 300" >> Client3.file
	@echo "N withdraw 20" >> Client3.file
	@echo "Sample client files created."

# Run the server
run_server: $(SERVER)
	-rm -f /tmp/$(SERVER_FIFO)
	./$(SERVER) AdaBank $(SERVER_FIFO)

# Run a client with a specified file
run_client1: $(CLIENT)
	./$(CLIENT) Client1.file $(SERVER_FIFO)

run_client2: $(CLIENT)
	./$(CLIENT) Client2.file $(SERVER_FIFO)

run_client3: $(CLIENT)
	./$(CLIENT) Client3.file $(SERVER_FIFO)

# Valgrind server
val_server: val
	-rm -f /tmp/$(SERVER_FIFO)
	$(VALGRIND) --log-file=valgrind_logs/server.log ./$(SERVER) AdaBank $(SERVER_FIFO)

# Valgrind clients
val_client1: val
	$(VALGRIND) --log-file=valgrind_logs/client1.log ./$(CLIENT) Client1.file $(SERVER_FIFO)

val_client2: val
	$(VALGRIND) --log-file=valgrind_logs/client2.log ./$(CLIENT) Client2.file $(SERVER_FIFO)

val_client3: val
	$(VALGRIND) --log-file=valgrind_logs/client3.log ./$(CLIENT) Client3.file $(SERVER_FIFO)

# Run a comprehensive valgrind test
val_test: val
	@echo "Running comprehensive Valgrind test"
	@echo "Starting server with Valgrind..."
	@-rm -f /tmp/$(SERVER_FIFO)
	@$(VALGRIND) --log-file=valgrind_logs/server_test.log ./$(SERVER) AdaBank $(SERVER_FIFO) & \
	SERVER_PID=$$!; \
	sleep 3; \
	echo "Running client1 with Valgrind..."; \
	$(VALGRIND) --log-file=valgrind_logs/client1_test.log ./$(CLIENT) Client1.file $(SERVER_FIFO); \
	echo "Running client2 with Valgrind..."; \
	$(VALGRIND) --log-file=valgrind_logs/client2_test.log ./$(CLIENT) Client2.file $(SERVER_FIFO); \
	echo "Running client3 with Valgrind..."; \
	$(VALGRIND) --log-file=valgrind_logs/client3_test.log ./$(CLIENT) Client3.file $(SERVER_FIFO); \
	echo "Tests complete, stopping server..."; \
	kill -TERM $$SERVER_PID; \
	wait $$SERVER_PID 2>/dev/null || true

# Enhanced Valgrind test that checks for resource leaks
val_leak_test: val
	@echo "Running resource leak test with Valgrind"
	@mkdir -p valgrind_logs
	@chmod +x ./test_memory_leaks.sh
	./test_memory_leaks.sh

# Clean up all FIFOs in /tmp
clean_fifos:
	-rm -f /tmp/bank_*
	-rm -f /tmp/$(SERVER_FIFO)

# Clean up
clean: clean_fifos
	rm -f $(SERVER) $(CLIENT) *.o *.log

# Clean including valgrind logs
distclean: clean
	rm -rf valgrind_logs

# Dependencies
BankServer.o: BankServer.c BankServer.h bank_shared.h bank_utils.h
BankClient.o: BankClient.c BankClient.h bank_shared.h bank_utils.h
bank_utils.o: bank_utils.c bank_utils.h

.PHONY: all clean clean_fifos run_server run_client1 run_client2 run_client3 create_client_files val val_server val_client1 val_client2 val_client3 val_test val_leak_test distclean