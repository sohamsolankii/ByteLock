#include "Cryption.hpp"
#include "../processes/Task.hpp"
#include "../fileHandling/ReadEnv.cpp"
#include <ctime>
#include <iomanip>

// This function is called by a child process to perform encryption or decryption on a file as specified by the task data.
// It is designed to be used in a multiprocessing context, where each process handles a single task.
int executeCryption(const std::string& taskData) {
    // Deserialize the task from the string representation
    Task task = Task::fromString(taskData);
    ReadEnv env;
    std::string envKey = env.getenv();
    int key = std::stoi(envKey);
    if (task.action == Action::ENCRYPT) {
        char ch;
        // Encrypt each character in the file using the key
        while (task.f_stream.get(ch)) {
            ch = (ch + key) % 256;
            task.f_stream.seekp(-1, std::ios::cur);
            task.f_stream.put(ch);
        }
        task.f_stream.close();
    } else {
        char ch;
        // Decrypt each character in the file using the key
        while (task.f_stream.get(ch)) {
            ch = (ch - key + 256) % 256;
            task.f_stream.seekp(-1, std::ios::cur);
            task.f_stream.put(ch);
        }
        task.f_stream.close();
    }
    // Log the completion time for the operation
    std::time_t t = std::time(nullptr);
    std::tm* now = std::localtime(&t);
    std::cout << "Exiting the encryption/decryption at: " << std::put_time(now, "%Y-%m-%d %H:%M:%S") << std::endl;
    
    return 0;
}