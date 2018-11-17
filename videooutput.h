﻿#ifndef VIDEOOUTPUT_H
#define VIDEOOUTPUT_H
#include "colorplayer.h"
#include "messagequeue.h"
#include "QThread"
#include <QMutex>
#include <QWaitCondition>

class VideoOutput:public QThread
{
public:
    static VideoOutput *Get()
    {
        static VideoOutput vt;
        return &vt;
    }

    void run();
    void stop();
    void flush();
    void initPlayerInfo(PlayerInfo *pPI);
    void initDisplayQueue(PlayerInfo *pPI);
    void deinitDisplayQueue(PlayerInfo *pPI);
    void initMasterClock(MasterClock * pMC);
    void queueMessage(MessageCmd_t MsgCmd);
    Frame *GetFrameFromDisplayQueue(PlayerInfo *pPI);
    void receiveFrametoDisplayQueue(Frame *pFrame);
    void setCallback(pFuncCallback callback);
    virtual ~VideoOutput();
private:
    VideoOutput();
    int NeedAVSync(MessageCmd_t MsgCmd, int bPaused);
    int DecideKeepFrame(int need_av_sync, int64_t pts);
    int CheckOutWaitOrDisplay(int64_t pts);
    int64_t CalcSyncLate(int64_t pts);
    int bVideoFreeRun;
    PlayerInfo *pPlayerInfo;
    message *pMessage;
    MasterClock *pMasterClock;
    Frame *pLastFrame;
    int bStop;
    int bFirstFrame;
    QMutex mutex;
    QWaitCondition WaitCondStopDone;
    pFuncCallback _funcCallback;
};

#endif // VIDEOOUTPUT_H
