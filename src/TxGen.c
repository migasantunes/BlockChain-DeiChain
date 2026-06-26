//José Miguel Luís Antunes, 2023211288
//André Jorge Balula Leão, 2023210870

#include "handler.h"

int main(int argc, char *argv[]){
    srand(time(NULL));

    open_log_file();

    if (argc != 3) {
        write_log("ERROR - Invalid number of arguments - TxGen");
        return 1;
    }

    int reward = atoi(argv[1]);
    int sleep_time = atoi(argv[2]);

    if (reward < 1 || reward > 3 || sleep_time < 200 || sleep_time > 3000) {
        write_log("ERROR - Invalid arguments - TxGen");
        return 1;
    }
    
    char log_message[100];

    Values *values = try_get_values();
    int TX_POOL_SIZE = values->TX_POOL_SIZE;
    shmdt(values);

    TransactionPool *tx_pool = try_get_tx_pool();

    sem_t *sem = sem_open(SEM_NAME, 0);
    if (sem == SEM_FAILED) {
        write_log("ERROR - Failed to open semaphore - TxGen");
        return 1;
    }

    write_log("ATTACHED - Transaction Pool - TxGen");

    // Create transaction
    Transaction tx;
    int i = 0;
    tx.reward = reward;

    while (1) {
        sem_wait(sem);
        
        tx.id = getpid() + i;
        tx.value = rand() % 10000 + 1;
        tx.timestamp = time(NULL);

        // Send transaction to Transaction Pool
        sem_wait(&tx_pool->mutex);

        TransactionSlot *slots = (TransactionSlot *)((char *)tx_pool + tx_pool->slots_offset);

        for(int j = 0; j < TX_POOL_SIZE; j++) {
            if (slots[j].empty) {
                slots[j].empty = 0;
                slots[j].age = 0;
                slots[j].tx = tx;
                sprintf(log_message, "TRANSACTION %d - Sent to pool in slot %d - TxGen", tx.id, j + 1);
                write_log(log_message);
                break;
            }
        }

        tx_pool->slots_used++;

        sem_post(&tx_pool->mutex);
        
        sort_transaction_pool(tx_pool);
        
        i++;
        usleep(sleep_time * 1000);
    }
    
    shmdt(tx_pool);
    
    sem_close(sem);

    close_log_file();

    return 0;
}