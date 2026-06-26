//José Miguel Luís Antunes, 2023211288
//André Jorge Balula Leão, 2023210870

#include "handler.h"

int NUM_MINERS;
int TX_POOL_SIZE;
int TRANSACTION_PER_BLOCK;

pid_t miner_pid, validator_pid, stats_pid;

pthread_t *miner_threads;

int fd_pipe_write;
int fd_pipe_read;
fd_set fds_write;
fd_set fds_read;

int msgqid;

volatile sig_atomic_t stop_miner_threads = 0;
volatile sig_atomic_t stop_validator = 0;

void handle_signal(int sig) {
    (void)sig;

    if (sig == SIGINT) {   
        
        write_log("SIGINT - Starting controlled shutdown");

        dump_ledger();

        // Signal all child processes
        if (miner_pid > 0) {
            kill(miner_pid, SIGINT);
            write_log("SIGINT - Sent to Miner process");
        }
        if (validator_pid > 0) {
            kill(validator_pid, SIGINT);
            write_log("SIGINT - Sent to Validator process");
        }
        if (stats_pid > 0) {
            kill(stats_pid, SIGINT);
            write_log("SIGINT - Sent to Statistics process");
        }

        cleanup_transaction_pool();
        write_log("CLEANUP - Transaction Pool");

        cleanup_blockchain_ledger();
        write_log("CLEANUP - Blockchain Ledger");

        // Wait for child processes to terminate
        int status;
        if (miner_pid > 0) {
            waitpid(miner_pid, &status, 0);
            write_log("TERMINATED - Miner process");
        }
        if (validator_pid > 0) {
            waitpid(validator_pid, &status, 0);
            write_log("TERMINATED - Validator process");
        }
        if (stats_pid > 0) {
            waitpid(stats_pid, &status, 0);
            write_log("TERMINATED - Statistics process");
        }
        
        msgctl(msgqid, IPC_RMID, NULL);
        write_log("CLEANUP - Message Queue");

        write_log("CLEANUP - Pipe mutex destroyed");

        close(fd_pipe_read);
        close(fd_pipe_write);
        write_log("CLEANUP - Pipe closed");

        unlink(PIPE_NAME);
        write_log("CLEANUP - Pipe unlinked");

        write_log("MUTEX_DESTROY - Log semaphore destroyed");
        write_log("SHUTDOWN - DEIChain terminated successfully");
        cleanup_values();

        close_log_file();

        exit(EXIT_SUCCESS);

    } else if (sig == SIGUSR1) {
        write_log("SIGUSR1 - Dumping Blockchain Ledger");
        dump_ledger();
    }
}


void miner_sigint_handler(int sig) {
    (void)sig;

    write_log("SIGINT - Miner process received shutdown signal");
    stop_miner_threads = 1;
    
    exit(EXIT_SUCCESS);
}

void *miner_function(void *arg) {
    int miner_id = *(int *)arg;
    int c = 0;

    TransactionPool *tx_pool = try_get_tx_pool();

    TransactionSlot *slots = (TransactionSlot *)((char *)tx_pool + tx_pool->slots_offset);
    
    fd_pipe_write = open(PIPE_NAME, O_WRONLY);
    if (fd_pipe_write == -1) {
        write_log("ERROR - Failed to open pipe for writing - Miner thread");
        return NULL;
    }
    
    while (!stop_miner_threads) {
        FD_ZERO(&fds_write);
        FD_SET(fd_pipe_write, &fds_write);
        
        if (select(fd_pipe_write + 1, NULL, &fds_write, NULL, NULL) > 0) {
            if (FD_ISSET(fd_pipe_write, &fds_write)) {
                Values *values = try_get_values();

                // Copy transactions from pool and create a block
                Block block;
                
                snprintf(block.block_id, sizeof(block.block_id), "%lu-%d", pthread_self() + c, miner_id);
                block.nonce = 0;
                block.timestamp = time(NULL);
                strncpy(block.previous_hash, values->PREVIOUS_HASH, sizeof(block.previous_hash));
                block.transactions = malloc(TRANSACTION_PER_BLOCK * sizeof(Transaction));

                shmdt(values);
                
                sem_wait(&tx_pool->mutex);
                int tx_count = 0;
                for (int i = 0; i < TX_POOL_SIZE && tx_count < TRANSACTION_PER_BLOCK; i++) {
                    if (slots[i].empty == 0) {
                        block.transactions[tx_count].id = slots[i].tx.id;
                        block.transactions[tx_count].value = slots[i].tx.value;
                        block.transactions[tx_count].reward = slots[i].tx.reward;
                        block.transactions[tx_count].timestamp = slots[i].tx.timestamp;
                        tx_count++;
                    }
                }

                if (tx_count < TRANSACTION_PER_BLOCK) {
                    write_log("ERROR - Not enough transactions to create a block");
                    free(block.transactions);
                    sem_post(&tx_pool->mutex);
                    sleep(5);
                    continue;
                }
                
                sem_post(&tx_pool->mutex);
                
                int max_reward = 0;
                for (int i = 0; i < TRANSACTION_PER_BLOCK; i++) {
                    if (block.transactions[i].reward > max_reward) {
                        max_reward = block.transactions[i].reward;
                    }
                }
                int difficulty = max_reward;

                // Compute the PoW
                PoWResult pow_result = compute_pow(&block, difficulty);
                if (pow_result.error) {
                    write_log("ERROR - PoW computation exceeded max operations");
                } else {
                    write_log("PoW SUCCESS - Sending block through pipe");

                    size_t total_size = sizeof(Block) + sizeof(Transaction) * TRANSACTION_PER_BLOCK;

                    char *buffer = malloc(total_size);
                    memcpy(buffer, &block, sizeof(Block));
                    memcpy(buffer + sizeof(Block), block.transactions, sizeof(Transaction) * TRANSACTION_PER_BLOCK);

                    values = try_get_values();
                    pthread_mutex_lock(&values->pipe_mutex);
                    shmdt(values);

                    ssize_t total_written = 0;
                    while ((size_t)total_written < total_size) {
                        ssize_t bytes_written = write(fd_pipe_write, buffer + total_written, total_size - total_written);
                        if (bytes_written > 0) {
                            total_written += bytes_written;
                        } else {
                            write_log("ERROR - Failed to write full block to pipe");
                            break;
                        }
                    }

                    if ((size_t)total_written == total_size) {
                        write_log("BLOCK SENT - Block sent through pipe");
                    } else {
                        write_log("ERROR - Incomplete block sent through pipe");
                    }
                    	
                    values = try_get_values();
                    pthread_mutex_unlock(&values->pipe_mutex);
                    shmdt(values);
                    free(buffer);
                }

                free(block.transactions);
                c++;
            }
        }
    }

    shmdt(tx_pool);

    return NULL;
}

void miner(){
    signal(SIGINT, miner_sigint_handler);

    miner_threads = malloc(NUM_MINERS * sizeof(pthread_t));
    if (miner_threads == NULL) {
        write_log("ERROR - Failed to allocate memory for miner threads - Miner process");
        return;
    }

    // Create miner threads
    for(int i = 0; i < NUM_MINERS; i++) {
        if (pthread_create(&miner_threads[i], NULL, miner_function, &i) != 0) {
            write_log("ERROR - Failed to create miner thread - Miner process");
            break;
        }
        char log_message[256];
        snprintf(log_message, sizeof(log_message), "CREATE - Miner %d", i+1);
        write_log(log_message);
    }
    
    // Wait for threads to finish
    for (int i = 0; i < NUM_MINERS; i++) {
        if (miner_threads[i]) {
            pthread_join(miner_threads[i], NULL);
            char log_message1[256];
            snprintf(log_message1, sizeof(log_message1), "TERMINATED - Miner %d", i+1);
            write_log(log_message1);
        }
    }

    free(miner_threads);
}


void validator_sigint_handler(int sig) {
    (void)sig;
    
    write_log("SIGINT - Validator process received shutdown signal");
    stop_validator = 1;
    
    exit(EXIT_SUCCESS);
    
}

void update_age_and_rewards() {
    TransactionPool *tx_pool = try_get_tx_pool();

    sem_wait(&tx_pool->mutex);

    TransactionSlot *slots = (TransactionSlot *)((char *)tx_pool + tx_pool->slots_offset);

    for (int i = 0; i < TX_POOL_SIZE; i++) {
        if (slots[i].empty) continue;
        
        slots[i].age++;
        if (slots[i].age % 50 == 0) {
            slots[i].tx.reward += 1;
        }
    }

    sem_post(&tx_pool->mutex);

    shmdt(tx_pool);
}

void process_blocks(Block *block) {
    char log_message[256];
    snprintf(log_message, sizeof(log_message), "PROCESSING BLOCK - Block ID: %s", block->block_id);
    write_log(log_message);

    msgqid = msgget(MSG_QUEUE_KEY, 0666);
    if (msgqid == -1) {
        write_log("ERROR - Failed to access message queue - Statistics process");
        exit(EXIT_FAILURE);
    }

    sem_t *sem = sem_open(SEM_NAME, 0);

    Values *values;
    
    Message msg;
    msg.block_valid = 1;
    
    char *block_number = strchr(block->block_id, '-');
    if (block_number != NULL) {
        msg.miner_id = atoi(block_number + 1);
    } else {
        write_log("ERROR - Failed to extract miner ID from block ID");
    }

    int max_difficulty = 0;
    int credits = 0;
    
    for (int i = 0; i < TRANSACTION_PER_BLOCK; i++) {
        if (block->transactions[i].reward > max_difficulty) {
            max_difficulty = block->transactions[i].reward;
        }
        credits += block->transactions[i].reward;
    }

    msg.credits = credits;
    
    PoWResult result = compute_pow(block, max_difficulty);
    
    if (result.error) {
        write_log("ERROR - PoW computation exceeded max opfrations");
        msg.block_valid = 2;
        
    } else if (!result.error) {

        values = try_get_values();

        if(strcmp(block->previous_hash, values->PREVIOUS_HASH) == 0) {
            shmdt(values);

            snprintf(log_message, sizeof(log_message), "PoW SUCCESS - Nonce: %u, Hash: %s", result.nonce, result.hash);
            write_log(log_message);
            
            int transactions_index[TRANSACTION_PER_BLOCK];
            int found = 0;

            TransactionPool *tx_pool = try_get_tx_pool();
            
            sem_wait(&tx_pool->mutex);

            TransactionSlot *slots = (TransactionSlot *)((char *)tx_pool + tx_pool->slots_offset);
            
            for (int i = 0; i < TRANSACTION_PER_BLOCK; i++) {
                for (int j = 0; j < TX_POOL_SIZE; j++) {
                    if (slots[j].tx.id == block->transactions[i].id) {
                        transactions_index[found] = j;
                        found ++;
                    }
                }
            }
            if (found == TRANSACTION_PER_BLOCK) {
                for (int i = 0; i < TRANSACTION_PER_BLOCK; i++) {
                    tx_pool->slots_used--;
                    slots[transactions_index[i]].empty = 1;
                    slots[transactions_index[i]].age = 0;
                    sem_post(sem);
                }
            } else {
                write_log("ERROR - Not all transactions found in pool");
                msg.block_valid = 2;
            }
            
            sem_post(&tx_pool->mutex);

            shmdt(tx_pool);
            
            update_age_and_rewards();
        } else {
            shmdt(values);
            write_log("ERROR - Block hash does not match previous hash");
            msg.block_valid = 2;
        }
    } else {
        msg.block_valid = 2;
    }
    
    if (msg.block_valid == 1) {
        Values *values = try_get_values();
        strncpy(values->PREVIOUS_HASH, result.hash, sizeof(values->PREVIOUS_HASH));
        shmdt(values);

        snprintf(log_message, sizeof(log_message), "BLOCK VALID - Block ID: %s, Credits: %d", block->block_id, msg.credits);
        write_log(log_message);
        add_block_to_ledger(block);
        
    }else {
        snprintf(log_message, sizeof(log_message), "BLOCK INVALID - Block ID: %s", block->block_id);
        write_log(log_message);
    }
    
    sem_close(sem);

    values = try_get_values();
    pthread_mutex_lock(&values->mq_mutex);
    shmdt(values);

    msgsnd(msgqid, &msg, sizeof(msg) - sizeof(long), 0);
    write_log("MESSAGE SENT - Validator process");

    values = try_get_values();
    pthread_cond_signal(&values->mq_not_empty);
    pthread_mutex_unlock(&values->mq_mutex);
    shmdt(values);
}

void validator_worker() {
    FD_ZERO(&fds_read);
    FD_SET(fd_pipe_read, &fds_read);

    if (select(fd_pipe_read + 1, &fds_read, NULL, NULL, NULL) > 0) {
        if (FD_ISSET(fd_pipe_read, &fds_read)){
            Values *values = try_get_values();
            pthread_mutex_lock(&values->pipe_mutex);
            shmdt(values);

            size_t total_size = sizeof(Block) + sizeof(Transaction) * TRANSACTION_PER_BLOCK;

            char *buffer = malloc(total_size);
            ssize_t total_read = 0;

            while ((size_t)total_read < total_size) {
                ssize_t bytes_read = read(fd_pipe_read, buffer + total_read, total_size - total_read);
                if (bytes_read > 0) {
                    total_read += bytes_read;
                } else if (bytes_read == 0) {
                    write_log("PIPE CLOSED - No more data to read");
                    stop_validator = 1;
                    break;
                } else {
                    write_log("ERROR - Failed to read from pipe");
                    break;
                }
            }

            values = try_get_values();
            pthread_mutex_unlock(&values->pipe_mutex);
            shmdt(values);

            if ((size_t)total_read == total_size) {
                Block *block = (Block *)buffer;
                block->transactions = (Transaction *)(buffer + sizeof(Block));
                process_blocks(block);
            } else {
                char log_message[256];
                snprintf(log_message, sizeof(log_message), "ERROR - Incomplete block read (%zd/%zu bytes)", total_read, total_size);
                write_log(log_message);
            }

            free(buffer);
        }
    }
}

void manage_additional_validators(pid_t* validators, int* active_count) {
    TransactionPool *tx_pool = try_get_tx_pool();

    sem_wait(&tx_pool->mutex);
    float occupancy = (float)tx_pool->slots_used / TX_POOL_SIZE;
    sem_post(&tx_pool->mutex);

    shmdt(tx_pool);

    // criação/destruição
    if (occupancy > 0.6 && *active_count < 1) {
        validators[*active_count] = fork();
        if (validators[*active_count] == 0) {
            validator_worker();
            exit(EXIT_SUCCESS);
        }
        (*active_count)++;
        write_log("CREATED -  Validator 2 (60% threshold)");

    } else if (occupancy > 0.8 && *active_count < 2) {
        validators[*active_count] = fork();
        if (validators[*active_count] == 0) {
            validator_worker();
            exit(EXIT_SUCCESS);
        }
        (*active_count)++;
        write_log("CREATED - Validator 3 (80% threshold)");

    } else if (occupancy < 0.4) {
        if (*active_count > 0) {
            for(int i = 0; i < *active_count; i++) {
                if (validators[i] > 0) {
                    kill(validators[i], SIGTERM);
                    waitpid(validators[i], NULL, 0);
                    validators[i] = 0;
                    
                    char log_msg9[256];
                    snprintf(log_msg9, sizeof(log_msg9), "TERMINATED - Validator %d (PID: %d)", i + 2, validators[i]);
                    write_log(log_msg9);
                }
            }
            
            (*active_count) = 0;
            write_log("TERMINATION SUCCESSFUL - Additional Validators (40% threshold)");
        }
    }
}
    
void validator() {
    
    fd_pipe_read = open(PIPE_NAME, O_RDONLY);
    if (fd_pipe_read == -1) {
        write_log("ERROR - Failed to open FIFO pipe for reading - Validator process");
        return;
    }
    
    pid_t additional_validators[MAX_VALIDATORS];
    int active_additional = 0;
    
    signal(SIGINT, validator_sigint_handler);
    
    while (!stop_validator) {
        validator_worker();   
        
        manage_additional_validators(additional_validators, &active_additional);
    }
    
    // Fase de shutdown - encerra validators adicionais
    write_log("MAIN VALIDATOR: Terminating additional validators...");
    for (int i = 0; i < active_additional; i++) {
        kill(additional_validators[i], SIGTERM);
        if (waitpid(additional_validators[i], NULL, 0) > 0) {
            char log_msg10[256];
            snprintf(log_msg10, sizeof(log_msg10), "TERMINATED - Additional Validator %d (PID: %d)", i, additional_validators[i]);
            write_log(log_msg10);
        }
    }

    write_log("TERMINATED - Main Validator Process");
}

void statistics_signal_handler(int sig) {
    (void)sig;
    
    if (sig == SIGINT) {
        write_log("SIGINT - Statistics process received shutdown signal");
        dump_statistics();
        
        exit(EXIT_SUCCESS);
    } else if (sig == SIGUSR1) {
        dump_statistics();
    }
}

void statistics() {
    signal(SIGINT, statistics_signal_handler);
    signal(SIGUSR1, statistics_signal_handler);

    msgqid = msgget(MSG_QUEUE_KEY, 0666);
    if (msgqid == -1) {
        write_log("ERROR - Failed to access message queue - Statistics process");
        exit(EXIT_FAILURE);
    }
    
    char log_message[256];
    snprintf(log_message, sizeof(log_message), "START - Statistics process - PID: %d", getpid());
    write_log(log_message);
    
    while (1) {

        Values *values = try_get_values();
        pthread_mutex_lock(&values->mq_mutex);
        pthread_cond_wait(&values->mq_not_empty, &values->mq_mutex);
        shmdt(values);
        
        Message msg;
        while (msgrcv(msgqid, &msg, sizeof(msg) - sizeof(long), 0, IPC_NOWAIT) != -1) {
            write_log("STATISTICS - Message received");

            Values *values = try_get_values();

            pthread_mutex_lock(&values->mutex);
            
            values->stats_total_duration += msg.validation_time;
            values->stats_total_blocks++;
            
            if (msg.miner_id >= 0 && msg.miner_id < NUM_MINERS) {
                if (msg.block_valid == 1) {
                    values->miner_stats[msg.miner_id].valid_blocks++;
                    values->miner_stats[msg.miner_id].total_credits += msg.credits;
                    values->stats_approved_blocks++;
                } else if (msg.block_valid == 2) {
                    values->miner_stats[msg.miner_id].invalid_blocks++;
                }
            }

            pthread_mutex_unlock(&values->mutex);

            shmdt(values);
        }

        values = try_get_values();
        pthread_mutex_unlock(&values->mq_mutex);
        shmdt(values);
    }
}

int main() {
    // Initialize shared memory for values
    FILE* log_file = fopen("/home/user/Desktop/Projecto/DEIChain_log.txt", "w");
    if (log_file == NULL) {
        perror("ERROR - Failed to open log file");
        exit(EXIT_FAILURE);
    }
    fclose(log_file);

    open_log_file();
    
    readsConfig();

    // Create shm Values
    create_values();
    Values *values = try_get_values();
    if (!values) {
        perror("ERROR - Failed to create shared memory for values - Controller process");
        exit(EXIT_FAILURE);
    }
    shmdt(values);

    write_log("------------------DEIChain Log------------------");

    char log_message12[256];
    snprintf(log_message12, sizeof(log_message12), "START - DEIChain - PID: %d", getpid());
    write_log(log_message12);
    
    signal(SIGINT, handle_signal);
    signal(SIGUSR1, handle_signal);
    
    values = try_get_values();

    NUM_MINERS = values->NUM_MINERS;
    TX_POOL_SIZE = values->TX_POOL_SIZE;
    TRANSACTION_PER_BLOCK = values->TRANSACTION_PER_BLOCK;

    shmdt(values);

    unlink(PIPE_NAME);

    // Create Pipe
    if (mkfifo(PIPE_NAME, 0666) == -1) {
        write_log("ERROR - Failed to create FIFO pipe - Controller process");
        return 1;
    }

    // Create shm Transaction Pool
    create_tx_pool();
    TransactionPool *tx_pool = try_get_tx_pool();
    if (!tx_pool) {
        write_log("ERROR - Failed to create transaction pool - Controller process");
        exit(EXIT_FAILURE);
    }
    shmdt(tx_pool);

    // Create shm Blockchain Ledger
    create_ledger();
    BlockchainLedger *ledger = try_get_ledger();
    if (!ledger) {
        write_log("ERROR - Failed to create blockchain ledger - Controller process");
        exit(EXIT_FAILURE);
    }
    shmdt(ledger);

    msgqid = msgget(MSG_QUEUE_KEY, IPC_CREAT | 0666);
    if (msgqid == -1) {
        write_log("ERROR - Failed to create message queue - Controller process");
        return 1;
    }

    // Create process Miner
    miner_pid = fork();
    if (miner_pid == 0) {
        write_log("START - Miner Process");
        miner();
        exit(EXIT_SUCCESS);
    } else if (miner_pid < 0) {
        write_log("ERROR - Failed to execute miner process - Controller process");
        exit(EXIT_FAILURE);
    }

    // Create process Validator
    validator_pid = fork();
    if (validator_pid == 0) {
        write_log("START - Main Validator Process");
        validator();
        exit(EXIT_SUCCESS);
    } else if (validator_pid < 0) {
        write_log("ERROR - Failed to create Validator process");
        exit(EXIT_FAILURE);
    }

    // Create process Statistics
    stats_pid = fork();
    if (stats_pid == 0) {
        statistics();
        exit(EXIT_SUCCESS);
    } else if (stats_pid < 0) {
        write_log("ERROR - Failed to execute statistics process - Controller process");
        exit(EXIT_FAILURE);
    }

    while(1) pause();
}