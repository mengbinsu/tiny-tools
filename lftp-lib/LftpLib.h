/**
 * @file LftpLlib.h
 * @author fox 
 * @brief 
 * @version 0.1
 * @date 2024-03-19
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#ifndef LFTPLIB_H
#define LFTPLIB_H

#include <string>
using std::string;

#include <vector>
using std::vector;

typedef enum _LFTP_STATE {
    LFTP_STATE_IDLE = 0,
    LFTP_STATE_NO_ROUTE_TO_HOST,
    LFTP_STATE_LOGIN_INCORRECT,
    LFTP_STATE_PORT_INCORRECT,
    LFTP_STATE_MKDIR_OK,
    LFTP_STATE_REMOTE_DIR_EXIST,
    LFTP_STATE_ABORT,
    LFTP_STATE_TRANSCODING,
    LFTP_STATE_TRANSFERRING,
    LFTP_STATE_TRANSFERRED,
    LFTP_STATE_MAX
} LFTP_STATE;

typedef enum _LFTP_EXP_FMT {
    LFTP_EXP_FMT_TS = 0,
    LFTP_EXP_FMT_MP4,
    LFTP_EXP_FMT_MAX
} LFTP_EXP_FMT;

typedef struct _LftpParam {
    vector<string> files;
    string path;
    LFTP_EXP_FMT export_format;

    string server;
    string port;
    string username;
    string password;
    string remote_path;
} LftpParam;

typedef struct _LftpStatus {
    string file_name;
    string transferred_bytes;
    string transferred_progress;
    string transfer_rate;
    string remaining_time;
    string transferred_time;
    string transfer_state_str;
    LFTP_STATE transfer_state;
    bool all_finish;
} LftpStatus;

/**
 * @brief lftp start transfer 
 * 
 * @param param 
 * @return int 
 */
int LftpUploadFilesStart(LftpParam& param);

/**
 * @brief get lftp transfer status 
 * 
 * @param status 
 * @return true success
 * @return false fail 
 */
bool LftpUploadFilesStatus(LftpStatus& status);

/**
 * @brief lftp stop transfer 
 * 
 * @return int 
 */
int LftpUploadFilesStop(void);

/**
 * @brief release resources 
 * 
 * @return int 
 */
int LftpUploadFilesDestroy(void);


#endif
