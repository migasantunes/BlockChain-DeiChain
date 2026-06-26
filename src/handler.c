//José Miguel Luís Antunes, 2023211288
//André Jorge Balula Leão, 2023210870

#include "handler.h"

int NUM_MINERS;
int TX_POOL_SIZE;
int TRANSACTION_PER_BLOCK;
int BLOCKCHAIN_BLOCKS;

FILE *log_file;
pthread_mutexattr_t log_mutex_attr;
pthread_mutexattr_t pipe_mutex_attr;
pthread_condattr_t mq_cond_attr;
pthread_mutexattr_t mq_mutex_attr;

void open_log_file() {
    log_file = fopen("/home/user/Desktop/Projecto/DEIChain_log.txt", "a");
    if (log_file == NULL) {
        perror("ERROR - Failed to open log file");
        exit(EXIT_FAILURE);
    }
}

void close_log_file() {
    if (log_file != NULL) {
        fclose(log_file);
    }
}

void write_log(const char *message) {
    Values *values = try_get_values();

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[26];
    strftime(timestamp, 26, "%Y-%m-%d %H:%M:%S", tm_info);
    
    pthread_mutex_lock(&values->log_mutex);
    
    printf("[%s] %s\n", timestamp, message);
    fprintf(log_file, "[%s] %s\n", timestamp, message);
    fflush(log_file);

    pthread_mutex_unlock(&values->log_mutex);
    shmdt(values);
}

void readsConfig() {
    FILE *file = fopen("/home/user/Desktop/Projecto/config.cfg", "r");

    if (file == NULL) {
        printf("Error: File not found\n");
        return;
    }

    if (fscanf(file, "%d", &NUM_MINERS) != 1 ||
        fscanf(file, "%d", &TX_POOL_SIZE) != 1 ||
        fscanf(file, "%d", &TRANSACTION_PER_BLOCK) != 1 ||
        fscanf(file, "%d", &BLOCKCHAIN_BLOCKS) != 1) {
        printf("Error: Invalid config file format\n");
        fclose(file);
        return;
    }

    fclose(file);
}

// int validate_address(void *addr, size_t size) {
//     void *test = mmap(addr, size, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0);
//     if (test == MAP_FAILED) {
//         return 0;
//     }
//     munmap(test, size);
//     return 1;
// }

void create_tx_pool() {
    write_log("CREATE - Transaction Pool");

    size_t tx_pool_size = sizeof(TransactionPool) + TX_POOL_SIZE * sizeof(TransactionSlot);
    
    // if (!validate_address(TX_POOL_ADDR, tx_pool_size)) {
    //     write_log("ERROR - Invalid address for transaction pool");
    //     exit(EXIT_FAILURE);
    // }

    shm_tx_pool_id = shmget(KEY_SHM_TX_POOL, 0, 0666);
    if (shm_tx_pool_id != -1) {
        shmctl(shm_tx_pool_id, IPC_RMID, NULL);
    }

    shm_tx_pool_id = shmget(KEY_SHM_TX_POOL, tx_pool_size, IPC_CREAT | 0666);

    if (shm_tx_pool_id == -1) {
        write_log("ERROR - Failed to create shared memory for transaction pool - create_tx_pool");
        exit(EXIT_FAILURE);
    }

    void *tx_pool_addr = shmat(shm_tx_pool_id, NULL, 0);

    TransactionPool *tx_pool = (TransactionPool *)tx_pool_addr;
    if (tx_pool == (void *)-1) {
        write_log("ERROR - Failed to attach shared memory for transaction pool - create_tx_pool");
        exit(EXIT_FAILURE);
    }

    tx_pool->slots_offset = sizeof(TransactionPool);
    tx_pool->slots_used = 0;

    sem_init(&tx_pool->mutex, 1, 1);

    sem_unlink(SEM_NAME);
    sem_t *sem = sem_open(SEM_NAME, O_CREAT | O_EXCL, 0666, TX_POOL_SIZE);
    if (sem == SEM_FAILED) {
        write_log("ERROR - Failed to create semaphore for transaction pool - create_tx_pool");
        exit(EXIT_FAILURE);
    }
    sem_close(sem);

    TransactionSlot *slots = (TransactionSlot *)((char *)tx_pool_addr + tx_pool->slots_offset);
    for (int i = 0; i < TX_POOL_SIZE; i++) {
        slots[i].empty = 1;
        slots[i].tx.reward = 0;
    }

    shmdt(tx_pool_addr);
}

TransactionPool *get_tx_pool() {
    int tx_pool_id = shmget(KEY_SHM_TX_POOL, sizeof(TransactionPool) + TX_POOL_SIZE * sizeof(TransactionSlot), 0666);

    if (tx_pool_id == -1) {
        write_log("ERROR - Failed to get shared memory for transaction pool - get_tx_pool");
        return NULL;
    }

    TransactionPool *tx_pool = (TransactionPool *)shmat(tx_pool_id, NULL, 0);
    if (tx_pool == (void *)-1) {
        write_log("ERROR - Failed to attach shared memory for transaction pool - get_tx_pool");
        return NULL;
    }    
    return tx_pool;
}

TransactionPool *try_get_tx_pool() {
    TransactionPool *tx_pool = NULL;

    int attempts = 0;
    while (!tx_pool && attempts < 5) {
        tx_pool = get_tx_pool();
        if (!tx_pool) {
            usleep(50000);
            attempts++;
        }
    }
    
    if (!tx_pool) {
        write_log("ERROR - failed to attach to transaction pool - try_get_tx_pool");
        return NULL;
    }
    
    return tx_pool;
}

void sort_transaction_pool(TransactionPool *tx_pool) {
    sem_wait(&tx_pool->mutex);

    TransactionSlot *slots = (TransactionSlot *)((char *)tx_pool + tx_pool->slots_offset);

    for (int i = 1; i < TX_POOL_SIZE; i++) {
        TransactionSlot key = slots[i];
        int j = i - 1;

        if (key.empty) continue;

        // Mover slots com menor reward ou menor idade para frente
        while (j >= 0 && !slots[j].empty) {
            if (slots[j].tx.reward < key.tx.reward || (slots[j].tx.reward == key.tx.reward && slots[j].age < key.age)) {
                slots[j + 1] = slots[j];
                j--;
            } else {
                break;
            }
        }
        slots[j + 1] = key;
    }

    sem_post(&tx_pool->mutex);
}

void cleanup_transaction_pool() {
    TransactionPool *tx_pool = try_get_tx_pool();

    sem_unlink(SEM_NAME);
    sem_destroy(&tx_pool->mutex);
    
    shmdt(tx_pool);
    shmctl(shm_tx_pool_id, IPC_RMID, NULL);
}

void create_ledger() {
    write_log("CREATE - Blockchain Ledger");

    size_t ledger_size = sizeof(BlockchainLedger) + BLOCKCHAIN_BLOCKS * sizeof(Block) + BLOCKCHAIN_BLOCKS * TRANSACTION_PER_BLOCK * sizeof(Transaction);
    
    // if (!validate_address(LEDGER_ADDR, ledger_size)) {
    //     write_log("ERROR - Invalid address for blockchain ledger");
    //     exit(EXIT_FAILURE);
    // }
    
    shm_blockchain_id = shmget(KEY_SHM_LEDGER, 0, 0666);
    if (shm_blockchain_id != -1) {
        shmctl(shm_blockchain_id, IPC_RMID, NULL);
    }

    shm_blockchain_id = shmget(KEY_SHM_LEDGER, ledger_size, IPC_CREAT | 0666);
    if (shm_blockchain_id == -1) {
        write_log("ERROR - Failed to create shared memory for blockchain ledger - create_ledger");
        exit(EXIT_FAILURE);
    }

    void *ledger_addr = shmat(shm_blockchain_id, NULL, 0);

    BlockchainLedger *ledger = (BlockchainLedger *)ledger_addr;
    if (ledger_addr == (void *)-1) {
        write_log("ERROR - Failed to attach shared memory for blockchain ledger - create_ledger");
        exit(EXIT_FAILURE);
    }

    ledger->blocks = (Block *)((char *)ledger_addr + sizeof(BlockchainLedger));

    // first transation array after blocks array
    Transaction *tx_base = (Transaction *)((char *)ledger->blocks + BLOCKCHAIN_BLOCKS * sizeof(Block));

    for (int i = 0; i < BLOCKCHAIN_BLOCKS; i++) {
        ledger->blocks[i].transactions = tx_base + i * TRANSACTION_PER_BLOCK;
    }

    ledger->current_size = 0;
    sem_init(&ledger->mutex, 1, 1);
    
    shmdt(ledger);
}

BlockchainLedger *get_ledger() {
    size_t ledger_size = sizeof(BlockchainLedger) + BLOCKCHAIN_BLOCKS * sizeof(Block) + BLOCKCHAIN_BLOCKS * TRANSACTION_PER_BLOCK * sizeof(Transaction);

    int ledger_id = shmget(KEY_SHM_LEDGER, ledger_size, 0666);

    if (ledger_id == -1) {
        write_log("ERROR - Failed to get shared memory for blockchain ledger - get_ledger");
        return NULL;
    }

    BlockchainLedger *ledger = (BlockchainLedger *)shmat(ledger_id, NULL, 0);
    if (ledger == (void *)-1) {
        write_log("ERROR - Failed to attach shared memory for blockchain ledger - get_ledger");
        return NULL;
    }

    ledger->blocks = (Block *)((char *)ledger + sizeof(BlockchainLedger));

    // first transation array after blocks array
    Transaction *tx_base = (Transaction *)((char *)ledger->blocks + BLOCKCHAIN_BLOCKS * sizeof(Block));
    
    for (int i = 0; i < BLOCKCHAIN_BLOCKS; i++) {
        ledger->blocks[i].transactions = tx_base + i * TRANSACTION_PER_BLOCK;
    }

    return ledger;
}

BlockchainLedger *try_get_ledger() {
    BlockchainLedger *ledger = NULL;

    int attempts = 0;
    while (!ledger && attempts < 5) {
        ledger = get_ledger();
        if (!ledger) {
            usleep(50000);
            attempts++;
        }
    }
    
    if (!ledger) {
        write_log("ERROR - failed to attach to blockchain ledger - try_get_ledger");
        return NULL;
    }
    
    return ledger;
}

void cleanup_blockchain_ledger() {
    BlockchainLedger *ledger = try_get_ledger();
    
    sem_destroy(&ledger->mutex);
    
    shmdt(ledger);
    shmctl(shm_blockchain_id, IPC_RMID, NULL);
}

void create_values() {
    size_t values_size = sizeof(Values) + NUM_MINERS * sizeof(MinerStats);

    // if (!validate_address(VALUES_ADDR, values_size)) {
    //     perror("ERROR - Invalid address for values");
    //     exit(EXIT_FAILURE);
    // }

    shm_values_id = shmget(KEY_SHM_VALUES, 0, 0666);
    if (shm_values_id != -1) {
        shmctl(shm_values_id, IPC_RMID, NULL);
    }

    shm_values_id = shmget(KEY_SHM_VALUES, values_size, IPC_CREAT | 0666);
    if (shm_values_id == -1) {
        perror("ERROR - Failed to create shared memory for values - create_values");
        exit(EXIT_FAILURE);
    }

    void *values_addr = shmat(shm_values_id, NULL, 0);

    Values *values = (Values *)values_addr;
    if (values == (void *)-1) {
        perror("ERROR - Failed to attach shared memory for values - create_values");
        exit(EXIT_FAILURE);
    }

    values->miner_stats = (MinerStats *)((char *)values_addr + sizeof(Values));

    values->NUM_MINERS = NUM_MINERS;
    values->TX_POOL_SIZE = TX_POOL_SIZE;
    values->TRANSACTION_PER_BLOCK = TRANSACTION_PER_BLOCK;
    values->BLOCKCHAIN_BLOCKS = BLOCKCHAIN_BLOCKS;

    values->stats_total_duration = 0;
    values->stats_total_blocks = 0;
    values->stats_approved_blocks = 0;

    strcpy(values->PREVIOUS_HASH, "");

    for (int i = 0; i < NUM_MINERS; i++) {
        values->miner_stats[i].valid_blocks = 0;
        values->miner_stats[i].invalid_blocks = 0;
        values->miner_stats[i].total_credits = 0;
    }

    pthread_mutex_init(&values->mutex, NULL);

    pthread_mutexattr_init(&log_mutex_attr);
    pthread_mutexattr_setpshared(&log_mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&values->log_mutex, &log_mutex_attr);

    pthread_mutexattr_init(&pipe_mutex_attr);
    pthread_mutexattr_setpshared(&pipe_mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&values->pipe_mutex, &pipe_mutex_attr);

    pthread_mutexattr_init(&mq_mutex_attr);
    pthread_mutexattr_setpshared(&mq_mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&values->mq_mutex, &mq_mutex_attr);

    pthread_condattr_init(&mq_cond_attr);
    pthread_condattr_setpshared(&mq_cond_attr, PTHREAD_PROCESS_SHARED);
    pthread_cond_init(&values->mq_not_empty, &mq_cond_attr);

    shmdt(values);
}

Values *get_values() {
    int values_id = shmget(KEY_SHM_VALUES, sizeof(Values) + NUM_MINERS * sizeof(MinerStats), 0666);
    if (values_id == -1) {
        perror("ERROR - Failed to get shared memory for values - get_values");
        return NULL;
    }

    Values *values = (Values *)shmat(values_id, NULL, 0 );

    if (values == (void *)-1) {
        perror("ERROR - Failed to attach shared memory for values - get_values");
        return NULL;
    }

    values->miner_stats = (MinerStats *)((char *)values + sizeof(Values));

    return values;
}

Values *try_get_values() {
    Values *values = NULL;

    int attempts = 0;
    while (!values && attempts < 5) {
        values = get_values();
        if (!values) {
            usleep(50000);
            attempts++;
        }
    }
    
    if (!values) {
        perror("ERROR - failed to attach to values - try_get_values");
        return NULL;
    }
    
    return values;
}

void cleanup_values() {
    Values *values = try_get_values();

    pthread_mutex_destroy(&values->mutex);
    pthread_mutexattr_destroy(&log_mutex_attr);
    pthread_mutex_destroy(&values->pipe_mutex);
    pthread_mutex_destroy(&values->log_mutex);
    pthread_mutexattr_destroy(&mq_mutex_attr);
    
    shmdt(values);
    shmctl(shm_values_id, IPC_RMID, NULL);
}

void hash_to_hex(const unsigned char *hash, char *hex) {
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(hex + (i * 2), "%02x", hash[i]);
    }
    hex[SHA256_DIGEST_LENGTH * 2] = '\0';
}

int hash_meets_difficulty(const char *hash, int difficulty) {
    for (int i = 0; i < difficulty; i++) {
        if (hash[i] != '0') {
            return 0; // Does not meet difficulty
        }
    }
    return 1; // Meets difficulty
}

// Compute the PoW for a block
PoWResult compute_pow(Block *block, int difficulty) {
    PoWResult result;
    result.nonce = 0;
    result.error = 0;

    unsigned char hash[SHA256_DIGEST_LENGTH];
    char hash_hex[SHA256_DIGEST_LENGTH * 2 + 1];

    for (int i = 0; i < POW_MAX_OPS; i++) {
        block->nonce = i;

        char transactions_data[4096] = "";
        for (int j = 0; j < TRANSACTION_PER_BLOCK; j++) {
            char transaction_str[256];
            snprintf(transaction_str, sizeof(transaction_str), "%d%d%d%ld",
                    block->transactions[j].id,
                    block->transactions[j].reward,
                    block->transactions[j].value,
                    block->transactions[j].timestamp);
            strcat(transactions_data, transaction_str);
        }

        char block_data[8192];
        snprintf(block_data, sizeof(block_data), "%s%s%ld%d%s", block->block_id, block->previous_hash, block->timestamp, block->nonce, transactions_data);

        SHA256((unsigned char *)block_data, strlen(block_data), hash);
        hash_to_hex(hash, hash_hex);

        if (hash_meets_difficulty(hash_hex, difficulty)) {
            result.nonce = i;
            strcpy(result.hash, hash_hex);
            return result;
        }
    }

    result.error = 1;
    return result;
}

void add_block_to_ledger(Block *block) {
    BlockchainLedger *ledger = try_get_ledger();

    sem_wait(&ledger->mutex);

    int index = ledger->current_size;

    strncpy(ledger->blocks[index].block_id, block->block_id, sizeof(block->block_id));
    strncpy(ledger->blocks[index].previous_hash, block->previous_hash, sizeof(block->previous_hash));
    ledger->blocks[index].nonce = block->nonce;
    ledger->blocks[index].timestamp = block->timestamp;

    for (int i = 0; i < TRANSACTION_PER_BLOCK; i++) {
        ledger->blocks[index].transactions[i] = block->transactions[i];
    }

    ledger->current_size++;

    char log_message[256];
    snprintf(log_message, sizeof(log_message), "BLOCK ADDED - Block ID: %s", block->block_id);
    write_log(log_message);

    if (ledger->current_size == BLOCKCHAIN_BLOCKS) {
        write_log("FULL LEDGER - starting shutdown process");
        kill(getppid(), SIGINT);
    }

    sem_post(&ledger->mutex);
    shmdt(ledger);
}

void dump_statistics() {
    Values *values = try_get_values();

    pthread_mutex_lock(&values->mutex);

    int total_blocks = values->stats_total_blocks;
    int approved_blocks = values->stats_approved_blocks;
    long total_duration = values->stats_total_duration;
    MinerStats *miner_stats_temp = malloc(NUM_MINERS * sizeof(MinerStats));
    memcpy(miner_stats_temp, values->miner_stats, NUM_MINERS * sizeof(MinerStats));

    pthread_mutex_unlock(&values->mutex);

    shmdt(values);

    char log_message[256];
    write_log("DUMP STATISTICS - Received signal to dump statistics");
    
    write_log("==================== STATISTICS ====================");
    snprintf(log_message, sizeof(log_message), "TOTAL OF VALIDATED BLOCKS: %d", total_blocks);
    write_log(log_message);
    snprintf(log_message, sizeof(log_message), "BLOCKS IN THE BLOCKCHAIN: %d", approved_blocks);
    write_log(log_message);
    snprintf(log_message, sizeof(log_message), "AVERAGE VALIDATION TIME: %ld ms", total_duration / total_blocks);
    write_log(log_message);
    for (int i = 0; i < NUM_MINERS; i++) {
        snprintf(log_message, sizeof(log_message), "MINER %d - Valid Blocks: %d, Invalid Blocks: %d, Total Credits: %d",
                 i, miner_stats_temp[i].valid_blocks, miner_stats_temp[i].invalid_blocks, miner_stats_temp[i].total_credits);
        write_log(log_message);
    }
    write_log("====================================================");

    free(miner_stats_temp);

}

void dump_ledger() {
    BlockchainLedger *ledger = try_get_ledger();

    char log_message[1024];
    write_log("DUMP LEDGER - Received signal to dump blockchain ledger");

    sem_wait(&ledger->mutex);
    
    write_log("=================== Start Ledger ===================");
    for (int i = 0; i < ledger->current_size; i++) {
        snprintf(log_message, sizeof(log_message), "||---- Block %03d --", i);
        write_log(log_message);

        snprintf(log_message, sizeof(log_message), "Block ID: %s", ledger->blocks[i].block_id);
        write_log(log_message);

        snprintf(log_message, sizeof(log_message), "Previous Hash:%s", ledger->blocks[i].previous_hash);
        write_log(log_message);

        snprintf(log_message, sizeof(log_message), "Block Timestamp: %ld", ledger->blocks[i].timestamp);
        write_log(log_message);

        snprintf(log_message, sizeof(log_message), "Nonce: %u", ledger->blocks[i].nonce);
        write_log(log_message);

        write_log("Transactions:");
        for (int j = 0; j < TRANSACTION_PER_BLOCK; j++) {
            snprintf(log_message, sizeof(log_message),
                     " [%d] ID: %d | Reward: %d | Value: %d | Timestamp: %ld",
                     j,
                     ledger->blocks[i].transactions[j].id,
                     ledger->blocks[i].transactions[j].reward,
                     ledger->blocks[i].transactions[j].value,
                     ledger->blocks[i].transactions[j].timestamp);
            write_log(log_message);
        }
        write_log("||------------------------------");
    }
    write_log("=================== End Ledger ===================");

    sem_post(&ledger->mutex);

    shmdt(ledger);
}