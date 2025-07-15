#include <iostream>
#include "ProcessManagement.hpp"
#include <unistd.h>
#include <cstring>
#include <sys/fcntl.h>
#include <sys/wait.h>
#include "../encryptDecrypt/Cryption.hpp"
#include <sys/mman.h>
#include <atomic>
#include <semaphore.h>

// Constructor: Initializes shared memory and semaphores for inter-process communication and task queue management
ProcessManagement::ProcessManagement() {
    // Open or create a semaphore to count the number of items in the queue
    sem_t* itemsSemaphore = sem_open("/items_semaphore", O_CREAT, 0666, 0);
    // Open or create a semaphore to count available empty slots in the queue (max 1000)
    sem_t* emptySlotsSemaphore = sem_open("/empty_slots_semaphore", O_CREAT, 0666, 1000);
    // Create or open a shared memory object for the task queue
    shmFd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    ftruncate(shmFd, sizeof(SharedMemory));
    // Map the shared memory into the process's address space
    sharedMem = static_cast<SharedMemory *>(mmap(nullptr, sizeof(SharedMemory), PROT_READ | PROT_WRITE, MAP_SHARED, shmFd, 0));
    sharedMem->front = 0;
    sharedMem->rear = 0;
    sharedMem->size.store(0);
}

// Destructor: Cleans up shared memory
ProcessManagement::~ProcessManagement() {
    munmap(sharedMem, sizeof(SharedMemory));
    shm_unlink(SHM_NAME);
}

// Submits a task to the queue and forks a new process to handle it
bool ProcessManagement::submitToQueue(std::unique_ptr<Task> task) {
    sem_wait(emptySlotsSemaphore); // Wait for an empty slot in the queue
    std::unique_lock<std::mutex> lock(queueLock);

    if (sharedMem->size.load() >= 1000) {
        return false;
    }
    // Add the task to the shared memory queue
    strcpy(sharedMem->tasks[sharedMem->rear], task->toString().c_str());
    sharedMem->rear = (sharedMem->rear + 1) % 1000;
    sharedMem->size.fetch_add(1);
    lock.unlock();
    sem_post(itemsSemaphore); // Signal that a new item is available

    // Fork a new process to execute the task in parallel
    int pid = fork();
    if (pid < 0) {
        // Fork failed
        return false;
    } else if (pid == 0) {
        // Child process: execute the task and exit
        executeTask();
        exit(0);
    }
    // Parent process: return success
    return true;
}

// Executes a single task from the queue (called by child process)
void ProcessManagement::executeTask() {
    sem_wait(itemsSemaphore); // Wait for an available item
    std::unique_lock<std::mutex> lock(queueLock);
    char taskStr[256];
    // Retrieve the task from the shared memory queue
    strcpy(taskStr, sharedMem->tasks[sharedMem->front]);
    sharedMem->front = (sharedMem->front + 1) % 1000;
    sharedMem->size.fetch_sub(1);
    lock.unlock();
    sem_post(emptySlotsSemaphore); // Signal that a slot is now free

    // Perform the encryption/decryption task
    executeCryption(taskStr);
}