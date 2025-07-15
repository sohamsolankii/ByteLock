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
    this->itemsSemaphore = sem_open("/items_semaphore", O_CREAT, 0666, 0);
    // Open or create a semaphore to count available empty slots in the queue (max 1000)
    this->emptySlotsSemaphore = sem_open("/empty_slots_semaphore", O_CREAT, 0666, 1000);
    // Create or open a shared memory object for the task queue
    this->shmFd = shm_open(this->SHM_NAME, O_CREAT | O_RDWR, 0666);
    ftruncate(this->shmFd, sizeof(SharedMemory));
    // Map the shared memory into the process's address space
    this->sharedMem = static_cast<SharedMemory *>(mmap(nullptr, sizeof(SharedMemory), PROT_READ | PROT_WRITE, MAP_SHARED, this->shmFd, 0));
    this->sharedMem->front = 0;
    this->sharedMem->rear = 0;
    this->sharedMem->size.store(0);
}

// Destructor: Cleans up shared memory
ProcessManagement::~ProcessManagement() {
    munmap(this->sharedMem, sizeof(SharedMemory));
    shm_unlink(this->SHM_NAME);
}

// Submits a task to the queue and forks a new process to handle it
bool ProcessManagement::submitToQueue(std::unique_ptr<Task> task) {
    sem_wait(this->emptySlotsSemaphore); // Wait for an empty slot in the queue
    std::unique_lock<std::mutex> lock(this->queueLock);

    if (this->sharedMem->size.load() >= 1000) {
        return false;
    }
    // Add the task to the shared memory queue
    strcpy(this->sharedMem->tasks[this->sharedMem->rear], task->toString().c_str());
    this->sharedMem->rear = (this->sharedMem->rear + 1) % 1000;
    this->sharedMem->size.fetch_add(1);
    lock.unlock();
    sem_post(this->itemsSemaphore); // Signal that a new item is available

    // Fork a new process to execute the task in parallel
    int pid = fork();
    if (pid < 0) {
        // Fork failed
        return false;
    } else if (pid == 0) {
        // Child process: execute the task and exit
        this->executeTask();
        exit(0);
    }
    // Parent process: return success
    return true;
}

// Executes a single task from the queue (called by child process)
void ProcessManagement::executeTask() {
    sem_wait(this->itemsSemaphore); // Wait for an available item
    std::unique_lock<std::mutex> lock(this->queueLock);
    char taskStr[256];
    // Retrieve the task from the shared memory queue
    strcpy(taskStr, this->sharedMem->tasks[this->sharedMem->front]);
    this->sharedMem->front = (this->sharedMem->front + 1) % 1000;
    this->sharedMem->size.fetch_sub(1);
    lock.unlock();
    sem_post(this->emptySlotsSemaphore); // Signal that a slot is now free

    // Perform the encryption/decryption task
    executeCryption(taskStr);
}

void ProcessManagement::executeTasks() {
    // Wait for all child processes to finish
    int status = 0;
    while (wait(&status) > 0) {}
}