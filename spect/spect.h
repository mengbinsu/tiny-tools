#ifndef SPECT_H_
#define SPECT_H_

#include <string>
#include <queue>
#include <pthread.h>

// SCPI命令结构体
struct ScpiCommand {
    std::string cmd;           // 命令字符串
    std::string* response;     // 响应存储指针
    bool* completed;           // 完成标志
    pthread_mutex_t* mutex;    // 同步互斥锁
    pthread_cond_t* cond;      // 同步条件变量
};

// 线程安全的命令队列
class CommandQueue {
public:
    CommandQueue();
    ~CommandQueue();
    
    void Push(const ScpiCommand& cmd);
    bool Pop(ScpiCommand& cmd);
    bool IsEmpty() const;

private:
    std::queue<ScpiCommand> queue_;
    mutable pthread_mutex_t mutex_;
    pthread_cond_t cond_;
};

// 频谱仪控制类
class Spect {
public:
    Spect(const std::string& ip, int port);
    ~Spect();

    // 禁止拷贝和赋值
    Spect(const Spect&) = delete;
    Spect& operator=(const Spect&) = delete;

    // 连接管理
    bool Connect();
    void Disconnect();
    bool IsConnected() const { return connected_; }

    // 命令发送
    bool SendCommand(const std::string& cmd, std::string& response);
    bool SendCommands(const std::vector<std::string>& cmds, 
                     std::vector<std::string>& responses);

    // 设置/获取超时时间（毫秒）
    void SetTimeout(int timeout_ms) { timeout_ms_ = timeout_ms; }
    int GetTimeout() const { return timeout_ms_; }

private:
    bool InitSocket();
    bool ReceiveResponse(std::string& response);
    static void* ReconnectThreadFunc(void* arg);
    static void* CommandThreadFunc(void* arg);
    void ReconnectLoop();
    void CommandLoop();
    void Lock() { pthread_mutex_lock(&mutex_); }
    void Unlock() { pthread_mutex_unlock(&mutex_); }

    std::string ip_;              // 设备IP地址
    int port_;                    // 设备端口
    int socket_;                  // Socket句柄
    bool connected_;              // 连接状态
    bool running_;                // 运行状态
    int timeout_ms_;             // 超时时间（毫秒）
    
    pthread_t reconnect_thread_;  // 重连线程
    pthread_t command_thread_;    // 命令处理线程
    pthread_mutex_t mutex_;       // 主互斥锁
    CommandQueue cmd_queue_;      // 命令队列
};

#endif  // SPECT_H_