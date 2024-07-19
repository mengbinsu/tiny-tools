# lftp lib 
Wrap lftp command to upload ts files to remote ftp server. If you need ts files 
transcode to mp4 before upload, you need install ffmpeg and set param export_format 
to LFTP_EXP_FMT_MP4.

For Ubuntu, you need to install lftp and expect first, unbuffer is in expect. 

```
apt-get install lftp
apt-get install expect
```