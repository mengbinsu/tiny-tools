#include "spect.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <iostream>

// CommandQueue实现
CommandQueue::CommandQueue() {
    pthread_mutex_init(&mutex_, NULL);
    pthread_cond_init(&cond_, NULL);
}

CommandQueue::~CommandQueue() {
    pthread_mutex_destroy(&mutex_);
    pthread_cond_destroy(&cond_);
}

void CommandQueue::Push(const ScpiCommand& cmd) {
    pthread_mutex_lock(&mutex_);
    queue_.push(cmd);
    pthread_cond_signal(&cond_);
    pthread_mutex_unlock(&mutex_);
}

bool CommandQueue::Pop(ScpiCommand& cmd) {
    pthread_mutex_lock(&mutex_);
    while (queue_.empty()) {
        pthread_cond_wait(&cond_, &mutex_);
    }
    cmd = queue_.front();
    queue_.pop();
    pthread_mutex_unlock(&mutex_);
    return true;
}

bool CommandQueue::IsEmpty() const {
    pthread_mutex_lock(&mutex_);
    bool empty = queue_.empty();
    pthread_mutex_unlock(&mutex_);
    return empty;
}

// Spect实现
Spect::Spect(const std::string& ip, int port)
    : ip_(ip)
    , port_(port)
    , socket_(-1)
    , connected_(false)
    , running_(true)
    , timeout_ms_(3000)  // 默认3秒超时
{
    pthread_mutex_init(&mutex_, NULL);
    
    // 启动重连线程
    pthread_create(&reconnect_thread_, NULL, ReconnectThreadFunc, this);
    
    // 启动命令处理线程
    pthread_create(&command_thread_, NULL, CommandThreadFunc, this);
}

Spect::~Spect() {
    running_ = false;
    
    // 等待线程结束
    pthread_join(reconnect_thread_, NULL);
    pthread_join(command_thread_, NULL);
    
    // 断开连接并清理资源
    Disconnect();
    pthread_mutex_destroy(&mutex_);
}

bool Spect::InitSocket() {
    socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_ < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return false;
    }

    // 设置socket选项
    struct timeval timeout;
    timeout.tv_sec = timeout_ms_ / 1000;
    timeout.tv_usec = (timeout_ms_ % 1000) * 1000;
    
    // 设置发送和接收超时
    setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    // 设置TCP keepalive
    int keepalive = 1;
    setsockopt(socket_, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));

    return true;
}

bool Spect::Connect() {
    if (!InitSocket()) {
        return false;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = inet_addr(ip_.c_str());

    if (connect(socket_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to connect: " << strerror(errno) << std::endl;
        close(socket_);
        socket_ = -1;
        return false;
    }

    connected_ = true;
    return true;
}

void Spect::Disconnect() {
    Lock();
    if (socket_ >= 0) {
        close(socket_);
        socket_ = -1;
    }
    connected_ = false;
    Unlock();
}

bool Spect::SendCommand(const std::string& cmd, std::string& response) {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool completed = false;

    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);

    ScpiCommand scpi_cmd;
    scpi_cmd.cmd = cmd;
    scpi_cmd.response = &response;
    scpi_cmd.completed = &completed;
    scpi_cmd.mutex = &mutex;
    scpi_cmd.cond = &cond;

    cmd_queue_.Push(scpi_cmd);

    // 等待命令完成
    pthread_mutex_lock(&mutex);
    while (!completed) {
        pthread_cond_wait(&cond, &mutex);
    }
    pthread_mutex_unlock(&mutex);

    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&cond);

    return true;
}

bool Spect::SendCommands(const std::vector<std::string>& cmds, 
                        std::vector<std::string>& responses) {
    responses.clear();
    for (const auto& cmd : cmds) {
        std::string response;
        if (!SendCommand(cmd, response)) {
            return false;
        }
        responses.push_back(response);
    }
    return true;
}

bool Spect::ReceiveResponse(std::string& response) {
    char buffer[1024];
    ssize_t received = recv(socket_, buffer, sizeof(buffer) - 1, 0);
    if (received < 0) {
        std::cerr << "Receive failed: " << strerror(errno) << std::endl;
        connected_ = false;
        return false;
    }

    buffer[received] = '\0';
    response = buffer;
    return true;
}

void* Spect::ReconnectThreadFunc(void* arg) {
    Spect* spect = static_cast<Spect*>(arg);
    spect->ReconnectLoop();
    return NULL;
}

void Spect::ReconnectLoop() {
    while (running_) {
        if (!connected_) {
            std::cout << "Attempting to reconnect..." << std::endl;
            Connect();
        }
        sleep(5);  // 每5秒检查一次连接状态
    }
}

void* Spect::CommandThreadFunc(void* arg) {
    Spect* spect = static_cast<Spect*>(arg);
    spect->CommandLoop();
    return NULL;
}

void Spect::CommandLoop() {
    while (running_) {
        ScpiCommand cmd;
        if (cmd_queue_.Pop(cmd)) {
            Lock();
            if (connected_) {
                std::string command = cmd.cmd + "\r\n";

                ssize_t sent = send(socket_, command.c_str(), command.length(), 0);
                if (sent > 0) {
                    ReceiveResponse(*cmd.response);
                } else {
                    std::cerr << "Send failed: " << strerror(errno) << std::endl;
                    connected_ = false;
                }
            }
            Unlock();

            pthread_mutex_lock(cmd.mutex);
            *cmd.completed = true;
            pthread_cond_signal(cmd.cond);
            pthread_mutex_unlock(cmd.mutex);
        }
    }
}