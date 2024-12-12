#include "spect.h"
#include <iostream>
#include <vector>
#include <pthread.h>
#include <unistd.h>

// 线程函数，用于发送命令
void* CommandThread(void* arg) {
    Spect* spect = static_cast<Spect*>(arg);
    
    while (true) {
        // 发送单条命令
        std::string response;
        if (spect->SendCommand("*IDN?", response)) {
            std::cout << "IDN Response: " << response << std::endl;
        } else {
            std::cerr << "Failed to send IDN command" << std::endl;
        }

#if 0
        // 发送多条命令
        std::vector<std::string> commands = {
            "*RST",
            ":FREQ:CENT 1GHz",
            ":FREQ:SPAN 1MHz",
            ":FREQ:CENT?"
        };
        std::vector<std::string> responses;
        
        if (spect->SendCommands(commands, responses)) {
            for (size_t i = 0; i < responses.size(); ++i) {
                std::cout << "Command " << i + 1 << " Response: " 
                         << responses[i] << std::endl;
            }
        } else {
            std::cerr << "Failed to send commands" << std::endl;
        }
#endif

        sleep(1);  // 等待1秒
    }
    return NULL;
}

int main() {
    // 创建Spect实例
    Spect spect("192.168.63.41", 5051);
    
    // 设置超时时间（毫秒）
    spect.SetTimeout(2000);

    // 创建多个命令发送线程
    pthread_t threads[3];
    for (int i = 0; i < 1; ++i) {
        pthread_create(&threads[i], NULL, CommandThread, &spect);
    }

    // 等待线程结束（实际上不会结束）
    for (int i = 0; i < 1; ++i) {
        pthread_join(threads[i], NULL);
    }

    return 0;
}