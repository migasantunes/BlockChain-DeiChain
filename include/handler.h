//José Miguel Luís Antunes, 2023211288
//André Jorge Balula Leão, 2023210870
 
#ifndef HANDLER_H
#define HANDLER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <pthread.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <openssl/sha.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#define POW_MAX_OPS 1000000
#define MAX_VALIDATORS 2

#define PIPE_NAME "VALIDATOR_INPUT"

#define KEY_SHM_TX_POOL 01234
#define KEY_SHM_LEDGER  56789
#define KEY_SHM_VALUES  98765

#define MSG_QUEUE_KEY 1234

// #define TX_POOL_ADDR  ((void *)0x700000000)
// #define LEDGER_ADDR   ((void *)0x800000000)
// #define VALUES_ADDR   ((void *)0x900000000)

#define SEM_NAME "/tx_pool_sem"

int shm_tx_pool_id;
int shm_blockchain_id;
int shm_values_id;

// Estrutura de uma transação
typedef struct {
    int id;
    int reward;
    int value;
    time_t timestamp;
} Transaction;

// Slot na Transaction Pool
typedef struct {
    int empty; // 0 = ocupado, 1 = vazio
    int age;
    Transaction tx;
} TransactionSlot;

// Estrutura da Transaction Pool
typedef struct {
    int slots_used;
    sem_t mutex;
    int slots_offset;
} TransactionPool;

// Estrutura de um bloco
typedef struct {
    char block_id[128];
    char previous_hash[SHA256_DIGEST_LENGTH * 2 + 1];
    int nonce;
    time_t timestamp;
    Transaction *transactions; // Array dinâmico de transações
} Block;

// Estrutura da Blockchain Ledger
typedef struct {
    int current_size;
    sem_t mutex;
    Block *blocks; // Array dinâmico de blocos
} BlockchainLedger;

typedef struct {
    int nonce;
    char hash[SHA256_DIGEST_LENGTH * 2 + 1];
    int error; //max operations exceeded
} PoWResult;

typedef struct{
    long block_valid; // 1 for valid, 2 for invalid
    int miner_id;
    int credits; 
    time_t validation_time;
} Message;

typedef struct {
    int valid_blocks;
    int invalid_blocks;
    int total_credits;
} MinerStats;

typedef struct {
    int NUM_MINERS;
    int TX_POOL_SIZE;
    int TRANSACTION_PER_BLOCK;
    int BLOCKCHAIN_BLOCKS;
    long stats_total_duration;
    int stats_total_blocks;
    int stats_approved_blocks;
    char PREVIOUS_HASH[SHA256_DIGEST_LENGTH * 2 + 1];
    pthread_mutex_t log_mutex;
    pthread_mutex_t mutex;
    pthread_mutex_t pipe_mutex;
    pthread_mutex_t mq_mutex;
    pthread_cond_t mq_not_empty;
    MinerStats *miner_stats;
} Values;

void open_log_file();
void close_log_file();
void write_log(const char *message);
void readsConfig();
int validate_address(void *addr, size_t size);

// Funções de values
void create_values();
Values *get_values();
Values *try_get_values();
void cleanup_values();

// Funções de Transações
void create_tx_pool();
TransactionPool *get_tx_pool();
TransactionPool *try_get_tx_pool();
void cleanup_transaction_pool();
void sort_transaction_pool(TransactionPool *tx_pool);

// Funções de Blocos
void create_ledger();
BlockchainLedger *get_ledger();
BlockchainLedger *try_get_ledger();
void cleanup_blockchain_ledger();
void add_block_to_ledger(Block *block);
void dump_ledger();

void hash_to_hex(const unsigned char *hash, char *hex);
int hash_meets_difficulty(const char *hash, int difficulty);
PoWResult compute_pow(Block *block, int difficulty);

void dump_statistics();

#endif