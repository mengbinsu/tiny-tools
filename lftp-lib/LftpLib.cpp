/**
 * @file LftpLib.cpp
 * @author fox 
 * @brief 
 * @version 0.1
 * @date 2024-03-19
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <iostream>
#include <queue>
#include <regex>
#include <string>

using std::queue;
using std::string;

#include "LftpLib.h"

#define LFTP_DEBUG

#ifdef LFTP_DEBUG
#define DEBUG(format, ...) printf(format, ##__VA_ARGS__)
#else
#define DEBUG(format, ...)
#endif

#define LFTP_LOG(format, ...)                   \
    do {                                        \
        DEBUG("[%s:%d] Lftp-> " format "\n",    \
            __func__, __LINE__, ##__VA_ARGS__); \
    } while (0)

typedef struct _LftpInfo {
    LftpParam param;

    struct {
        pthread_t tid;
        int running;
        FILE* pipe;
    } sender;

    pthread_mutex_t lock;

    LftpStatus status;
    queue<LftpStatus> status_queue;

} LftpInfo;

static LftpInfo lftpInfo;

static string LftpBytesToString(unsigned long long bytes)
{
    char result[32] = { 0 };

    if (bytes < 1024) {
        sprintf(result, "%lluB", bytes);
    } else if (bytes < 1024 * 1024) {
        sprintf(result, "%.2fKB", (float)bytes / 1024);
    } else if (bytes < 1024 * 1024 * 1024) {
        sprintf(result, "%.2fMB", (float)bytes / (1024 * 1024));
    } else {
        sprintf(result, "%.2fGB", (float)bytes / (1024 * 1024 * 1024));
    }

    return string(result);
}

static string LftpSecondsToString(unsigned long long seconds)
{
    char result[32] = { 0 };

    if (seconds < 60) {
        sprintf(result, "%llds", seconds);
    } else if (seconds < 60 * 60) {
        sprintf(result, "%lldm", seconds / 60);
    } else if (seconds < 60 * 60 * 24) {
        sprintf(result, "%lldh", seconds / (60 * 60));
    } else {
        sprintf(result, "%lldd", seconds / (60 * 60 * 24));
    }

    return string(result);
}

static string LftpStateToString(LFTP_STATE state)
{
    string state_str;
    switch (state) {
    case LFTP_STATE_IDLE:
        state_str = "Start...";
        break;
    case LFTP_STATE_NO_ROUTE_TO_HOST:
        state_str = "No route to host";
        break;
    case LFTP_STATE_LOGIN_INCORRECT:
        state_str = "Login incorrect";
        break;
    case LFTP_STATE_PORT_INCORRECT:
        state_str = "Port incorrect";
        break;
    case LFTP_STATE_MKDIR_OK:
        state_str = "Create directory ok";
        break;
    case LFTP_STATE_REMOTE_DIR_EXIST:
        state_str = "Remote directory exist";
        break;
    case LFTP_STATE_ABORT:
        state_str = "Transfer abort";
        break;
    case LFTP_STATE_TRANSCODING:
        state_str = "Transcoding...";
        break;
    case LFTP_STATE_TRANSFERRING:
        state_str = "Transferring...";
        break;
    case LFTP_STATE_TRANSFERRED:
        state_str = "Transferred";
        break;
    default:
        LFTP_LOG("unkown lftp state");
        break;
    }

    return state_str;
}

static int LftpParseOutput(const string& output, LftpInfo* p)
{
    std::smatch match;

    if (output.find("Sending") != std::string::npos) {
        // Transferring: `.../20240321160232.ts' at 1013275648 (83%) 11.18M/s eta:18s [Sending data]
        std::regex pattern(R"(`(.*?)' at (\d+) \((\d+)%\) (\d+\.\d+\w\/s) eta:(\w+) )");
        // Transferring: `/home/deviser/ts/20240228140912_5191_HDMI.ts' at 81003344 (72%) [Sending data]
        std::regex pattern_ex(R"(`(.*?)' at (\d+) \((\d+)%\) )");

        p->status.transfer_state = LFTP_STATE_TRANSFERRING;

        if (std::regex_search(output, match, pattern)) {
            // Transferring: `.../20240321160232.ts' at 1013275648 (83%) 11.18M/s eta:18s [Sending data]
            string file_name = match[1].str();
            size_t found = file_name.find_last_of("/");
            if (found != std::string::npos) {
                file_name.erase(0, found + 1);
            }

            // p->status.file_name = file_name;
            string bytes = match[2].str();
            p->status.transferred_bytes = LftpBytesToString(atoll(bytes.c_str()));
            p->status.transferred_progress = match[3].str();
            p->status.transfer_rate = match[4].str();
            p->status.remaining_time = match[5].str();
        } else if (std::regex_search(output, match, pattern_ex)) {
            // Transferring: `/home/deviser/ts/20240228140912_5191_HDMI.ts' at 81003344 (72%) [Sending data]
            string file_name = match[1].str();
            size_t found = file_name.find_last_of("/");
            if (found != std::string::npos) {
                file_name.erase(0, found + 1);
            }

            // p->status.file_name = file_name;
            string bytes = match[2].str();
            p->status.transferred_bytes = LftpBytesToString(atoll(bytes.c_str()));
            p->status.transferred_progress = match[3].str();
            p->status.transfer_rate = "0";
            p->status.remaining_time = "0";
        } else {
            LFTP_LOG("parse failed: %s", output.c_str());
        }
    } else if (output.find("transferred") != std::string::npos) {
        // transferred: 1219759412 bytes transferred in 104 seconds (11.20 MiB/s)
        // transferred: 1110982416 bytes transferred in 1 second (82.17M/s)
        // transferred: (small file and transferred fast): 14661659 bytes transferred
        std::regex pattern("(\\d+) bytes transferred in (\\d+) seconds? \\(([^)]+)\\)");
        std::regex pattern_ex("(\\d+) bytes transferred");

        p->status.transfer_state = LFTP_STATE_TRANSFERRED;

        if (std::regex_search(output, match, pattern)) {
            string bytes = match[1].str();
            p->status.transferred_bytes = LftpBytesToString(atoll(bytes.c_str()));
            p->status.transferred_progress = string("100");
            p->status.transferred_time = match[2].str();
            string time = match[2].str();
            p->status.transferred_time = LftpSecondsToString(atoll(time.c_str()));
            p->status.transfer_rate = match[3].str();
        } else if (std::regex_search(output, match, pattern_ex)) {
            string bytes = match[1].str();
            p->status.transferred_bytes = LftpBytesToString(atoll(bytes.c_str()));
            p->status.transferred_progress = string("100");
        } else {
            LFTP_LOG("parse failed: %s", output.c_str());
        }
    } else if (output.find("Login incorrect") != std::string::npos) {
        p->status.transfer_state = LFTP_STATE_LOGIN_INCORRECT;
    } else if (output.find("No route to host") != std::string::npos 
        || output.find("Delaying before reconnect") != std::string::npos
        || output.find("Not connected") != std::string::npos) {
        p->status.transfer_state = LFTP_STATE_NO_ROUTE_TO_HOST;
    } else if (output.find("mkdir ok") != std::string::npos) {
        p->status.transfer_state = LFTP_STATE_MKDIR_OK;
    } else if (output.find("mkdir: Access failed") != std::string::npos) {
        p->status.transfer_state = LFTP_STATE_REMOTE_DIR_EXIST;
    }

    return 0;
}

static int LftpLock(void)
{
    if (lftpInfo.sender.running) {
        return pthread_mutex_lock(&lftpInfo.lock);
    } else {
        return 0;
    }
}

static int LftpUnlock(void)
{
    if (lftpInfo.sender.running) {
        return pthread_mutex_unlock(&lftpInfo.lock);
    } else {
        return 0;
    }
}

static int LftpStatusEnqueue(LftpInfo* p)
{
    pthread_mutex_lock(&p->lock);
    p->status.transfer_state_str = LftpStateToString(p->status.transfer_state);
    p->status_queue.push(p->status);
    pthread_mutex_unlock(&p->lock);

    return 0;
}

static bool LftpStatusDequeue(LftpStatus& status)
{
    bool ret = true;

    LftpLock();

    if (lftpInfo.status_queue.size()) {
        status = lftpInfo.status_queue.front();
        lftpInfo.status_queue.pop();
    } else {
        ret = false;
    }

    LftpUnlock();

    return ret;
}

static int LftpStatusQueueClear(LftpInfo* p)
{
    LftpLock();

    while (!p->status_queue.empty()) {
        p->status_queue.pop();
    }

    LftpUnlock();

    return 0;
}

static int LftpStatusClear(LftpInfo* p)
{
    p->status.file_name = "";
    p->status.transferred_bytes = "0";
    p->status.transferred_progress = "0";
    p->status.transfer_rate = "0";
    p->status.remaining_time = "0";
    p->status.transferred_time = "0";
    p->status.transfer_state_str = "0";
    p->status.transfer_state = LFTP_STATE_IDLE;
    p->status.all_finish = false;

    return 0;
}

static int LftpCreateRemoteDirectory(LftpInfo* p)
{
    char cmd[256] = { 0 };
    int ret = 0;

    snprintf(cmd, sizeof(cmd),
        "unbuffer lftp -e 'open -u %s,%s ftp://%s:%s; mkdir -p %s; exit' 2>&1",
        p->param.username.c_str(),
        p->param.password.c_str(),
        p->param.server.c_str(),
        p->param.port.c_str(),
        p->param.remote_path.c_str());

    LFTP_LOG("cmd: %s\n", cmd);

    FILE* pipe = popen(cmd, "r");
    if (NULL == pipe) {
        LFTP_LOG("mkdir error");
        return -1;
    }

    string output;
    char c;

    while ((c = fgetc(pipe)) != EOF) {
        if (c == '\r' || c == '\n') {
            LFTP_LOG("output: %s", output.c_str());

            LftpParseOutput(output, p);

            if (LFTP_STATE_LOGIN_INCORRECT == p->status.transfer_state
                || LFTP_STATE_NO_ROUTE_TO_HOST == p->status.transfer_state
                || LFTP_STATE_PORT_INCORRECT == p->status.transfer_state) {
                LftpStatusEnqueue(p);
                ret = -1;

                system("killall -9 lftp");

                LFTP_LOG("killall -9 lftp");
                break;
            }
            output.clear();
        } else {
            output += c;
        }
    }

    pclose(pipe);

    return ret;
}

static int LftpPrintStatus(LftpStatus& status)
{
    LFTP_LOG("******************************************************");
    LFTP_LOG("* File name            : %s", status.file_name.c_str());
    LFTP_LOG("* Transferred bytes    : %s", status.transferred_bytes.c_str());
    LFTP_LOG("* Transferred progress : %s", status.transferred_progress.c_str());
    LFTP_LOG("* Transfer rate        : %s", status.transfer_rate.c_str());
    LFTP_LOG("* Remaining time       : %s", status.remaining_time.c_str());
    LFTP_LOG("* Transferred time     : %s", status.transferred_time.c_str());
    LFTP_LOG("* Transfer state       : %d", status.transfer_state);
    LFTP_LOG("* Transfer state str   : %s", status.transfer_state_str.c_str());
    LFTP_LOG("* Transfer all finish  : %d", status.all_finish);
    LFTP_LOG("******************************************************");

    return 0;
}

static int LftpExecCmd(const string& cmd, LftpInfo* p)
{
    int ret = 0;

    p->sender.pipe = popen(cmd.c_str(), "r");
    if (NULL == p->sender.pipe) {
        LFTP_LOG("popen [%s\n] failed\n", cmd.c_str());
        return ret;
    }

    string output;
    char c;

    while ((c = fgetc(p->sender.pipe)) != EOF) {
        if (c == '\r' || c == '\n') {
            LFTP_LOG("output: %s", output.c_str());

            LftpParseOutput(output, p);

            if (!p->sender.running) {
                p->status.transfer_state = LFTP_STATE_ABORT;
                ret = -1;
                LftpStatusEnqueue(p);

                system("killall -9 lftp");

                LFTP_LOG("killall -9 lftp");
                break;
            } else {
                LftpStatusEnqueue(p);

                if (LFTP_STATE_LOGIN_INCORRECT == p->status.transfer_state
                    || LFTP_STATE_NO_ROUTE_TO_HOST == p->status.transfer_state
                    || LFTP_STATE_PORT_INCORRECT == p->status.transfer_state) {
                    ret = -1;

                    system("killall -9 lftp");

                    LFTP_LOG("killall -9 lftp");
                    break;
                }
            }
            output.clear();
        } else {
            output += c;
        }
    }

    pclose(p->sender.pipe);

    return ret;
}

string LftpMakeMp4Filename(const string& filename)
{
    size_t dot_pos = filename.rfind('.');
    if (dot_pos != std::string::npos) {
        return filename.substr(0, dot_pos) + ".mp4";
    }

    return string("");
}

static void* LftpSenderThread(void* arg)
{
    pthread_detach(pthread_self());

    LftpInfo* p = (LftpInfo*)arg;

    p->sender.running = true;

    LFTP_LOG("transfer start");
    LFTP_LOG("ip          : %s", p->param.server.c_str());
    LFTP_LOG("port        : %s", p->param.port.c_str());
    LFTP_LOG("remote dir  : %s", p->param.remote_path.c_str());
    LFTP_LOG("username    : %s", p->param.username.c_str());
    LFTP_LOG("password    : %s", p->param.password.c_str());
    LFTP_LOG("local dir   : %s", p->param.path.c_str());
    for (int i = 0; i < p->param.files.size(); i++) {
        LFTP_LOG("file[%2d]    : %s", i, p->param.files.at(i).c_str());
    }

    // init params
    LftpStatusClear(p);

    if (0 != LftpCreateRemoteDirectory(p)) {
        LFTP_LOG("create dir failed, exit");
        goto lftp_exit;
    }

    if (p->param.files.size()) {
        char base[1024] = { 0 };
        string file_path;
        string cmd;

        snprintf(base, sizeof(base),
            "lftp -e 'open -u %s,%s ftp://%s:%s; cd %s; ",
            p->param.username.c_str(),
            p->param.password.c_str(),
            p->param.server.c_str(),
            p->param.port.c_str(),
            p->param.remote_path.c_str());

        for (int i = 0; i < p->param.files.size(); i++) {
            LftpStatusClear(p);

            p->status.file_name = p->param.files.at(i);
            string ts_file_path;
        
            if (p->param.path.back() == '/') {
                ts_file_path = p->param.path + p->status.file_name;
            } else {
                ts_file_path = p->param.path + "/" + p->status.file_name;
            }

            if (p->param.export_format == LFTP_EXP_FMT_MP4) {
                string mp4_file_path = LftpMakeMp4Filename(ts_file_path);
                if (mp4_file_path.size()) {
                    cmd = string("ffmpeg -i ") + ts_file_path + " -c copy " + mp4_file_path + " -loglevel quiet";

                    LFTP_LOG("system:%s", cmd.c_str());
                    p->status.transfer_state = LFTP_STATE_TRANSCODING;
                    LftpStatusEnqueue(p);

                    system(cmd.c_str());

                    file_path = mp4_file_path;
                    p->status.file_name = LftpMakeMp4Filename(p->status.file_name);
                }
            } else {
                file_path = ts_file_path;
            }

            if (!file_path.size()) {
                LFTP_LOG("file_path invalid");
                continue;
            }

            cmd = string("unbuffer ") + base + " mput -c " + file_path + ";exit' 2>&1";

            LFTP_LOG("upload cmd %d: %s", i, cmd.c_str());
            LFTP_LOG("upload start [%d]: %s", i, p->status.file_name.c_str());

            if (LftpExecCmd(cmd, p) != 0) {
                LFTP_LOG("upload abort %d: %s", i, p->status.file_name.c_str());

                if (p->param.export_format == LFTP_EXP_FMT_MP4) {
                    cmd = string("rm -rf ") + file_path;
                    LFTP_LOG("system:%s", cmd.c_str());
                    system(cmd.c_str());
                }
                
                break;
            }

            if (p->param.export_format == LFTP_EXP_FMT_MP4) {
                cmd = string("rm -rf ") + file_path;
                LFTP_LOG("system:%s", cmd.c_str());
                system(cmd.c_str());
            }

            LFTP_LOG("upload finish [%d]: %s", i, p->status.file_name.c_str());
        }
    }

lftp_exit:
    p->sender.running = false;
    p->sender.tid = 0;
    p->sender.pipe = NULL;

    if (p->status.transfer_state == LFTP_STATE_TRANSFERRING) {
        p->status.transfer_state = LFTP_STATE_TRANSFERRED;
    }
    p->status.all_finish = true;
    LftpStatusEnqueue(p);

    pthread_mutex_destroy(&p->lock);

    LFTP_LOG("transfer finish");

    return NULL;
}

int LftpUploadFilesStart(LftpParam& param)
{
    lftpInfo.param = param;

    if (lftpInfo.sender.tid || lftpInfo.sender.running) {
        LftpUploadFilesStop();
    }

    if (pthread_mutex_init(&lftpInfo.lock, NULL) != 0) {
        printf("pthread_mutex_init failed\n");
    }

    LftpStatusQueueClear(&lftpInfo);

    if (0 == pthread_create(&(lftpInfo.sender.tid), NULL, LftpSenderThread, (void*)(&lftpInfo))) {
        printf("create LftpSenderThread failed\n");
    }

    return 0;
}

bool LftpUploadFilesStatus(LftpStatus& status)
{
    return LftpStatusDequeue(status);
}

int LftpUploadFilesStop(void)
{
    if (lftpInfo.sender.running) {
        LFTP_LOG("transfer abort");
        lftpInfo.sender.running = false;
    }

    return 0;
}

int LftpUploadFilesDestroy(void)
{
    LftpStatusQueueClear(&lftpInfo);
    LftpStatusClear(&lftpInfo);
    lftpInfo.param.files.clear();

    return 0;
}

/* test lftp api */
int main(void)
{
    LftpParam param;

    param.files = { "swz.ts", "wucf-tv.ts" };
    param.path = "~/Videos/ts-files";
    param.server = "127.0.0.1";
    param.port = "21";
    param.username = "ftp";
    param.password = "ftp-test";
    param.remote_path = "lftp-test";
    param.export_format = LFTP_EXP_FMT_TS;

    LftpUploadFilesStart(param);

    while (1) {
        LftpStatus status;
        if (LftpUploadFilesStatus(status)) {
            LftpPrintStatus(status);
            if (status.all_finish) {
                break;
            }
        }

        usleep(500 * 1000);
    }

    LftpUploadFilesStop();

    LftpUploadFilesDestroy();

    return 0;
}