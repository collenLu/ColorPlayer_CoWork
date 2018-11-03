#include "videodecodethread.h"
#include "videooutput.h"
#include "ffmpeg.h"
#include <QDebug>

VideoDecodeThread::VideoDecodeThread()
{
    pPlayerInfo = NULL;
    bStop = 0;

    pMessage = new message();
    if (!pMessage)
    {
        printf("AudioDecodeThread() error\n");
    }
}

Frame *VideoDecodeThread::GetOneValidFrame(FrameQueue *pQueue)
{
    Frame *pFrame = NULL;
    int i;

    if (pQueue == NULL)
    {
        qDebug()<<"pQueue == NULL";
        return NULL;
    }

    for (i = 0; i < pQueue->size; i++)
    {
        if (pQueue->queue[i].DecState != DecOver && pQueue->queue[i].DispState != DispWait
                && pQueue->queue[i].frame != NULL)
        {
            pFrame = &(pQueue->queue[i]);
            break;
        }
    }

    if (i == pQueue->size)
    {
        //qDebug()<<" can't find valid frame for dec";
        return NULL;
    }

    return pFrame;
}

myPacket *VideoDecodeThread::GetOnePacket(PacketQueue *pPacketQueue)
{
    myPacket *pTemp = NULL;

    if (pPacketQueue == NULL)
    {
        return NULL;
    }

    pPacketQueue->Mutex.lock();
    if (!pPacketQueue->Queue->isEmpty())
    {
        pTemp = pPacketQueue->Queue->takeFirst();
        pPacketQueue->Mutex.unlock();
        return pTemp;
    }
    pPacketQueue->Mutex.unlock();
    return NULL;
}

int VideoDecodeThread::adjustVideoPts(Frame *pFrame)
{
    int deltaPts = 0;
    int needAdjust = 0;
    static int actually_delta_pts = 0;
    static int LastSerial = -1;
    static int SerialFirstPts = -1;
    static int LastFramePts = -1;
    static int cnt = 0;

    if ((XFFmpeg::Get()->fps != 0) && (actually_delta_pts == 0))
    {
        actually_delta_pts = 1000 / XFFmpeg::Get()->fps;
        qDebug()<<"video actually_delta_pts :"<<actually_delta_pts<<"fps:"<<XFFmpeg::Get()->fps;
    }

    if (isFirstFrame == 1)
    {
        //将序列第一帧的pts记录下来
        SerialFirstPts = pFrame->frame->pts;
        LastFramePts = SerialFirstPts;
        isFirstFrame = 0;
        qDebug()<<"video SerialFirstPts :"<<SerialFirstPts;
        return 0;
    }
    else
    {
        deltaPts = pFrame->frame->pts - LastFramePts;
        //当从实际pts算出的delta与根据fps算出来的理论delta要大很多，说明pts是异常的
        if (abs(deltaPts - actually_delta_pts) >= 100)
        {
            //qDebug()<<"abs : "<<abs(deltaPts - actually_delta_pts);
            needAdjust = 1;
        }
        LastFramePts = pFrame->frame->pts;
    }

    if (needAdjust == 1)
    {
        qDebug()<<"video adjust before pts : "<<pFrame->frame->pts;
        int temp = (++cnt) * actually_delta_pts;
        pFrame->frame->pts =  SerialFirstPts + temp;
        qDebug()<<"video adjust after pts : "<<pFrame->frame->pts;
        return 0;
    }

    //qDebug()<<"no needAdjust pts : "<<pFrame->frame->pts;
    return 0;
}

void VideoDecodeThread::stop()
{
    bStop = 1;
    isFirstFrame = 1;
    if (pPlayerInfo)
        pPlayerInfo->pWaitCondVideoDecodeThread->wakeAll();

    wait();
    qDebug()<<"VideoDecodeThread::stop  wait done";
}

void VideoDecodeThread::run()
{
    int ret = -1;
    Frame *pFrame = NULL;
    myPacket *pMyPkt = NULL;
    int bEof = 0;
    qDebug()<<"VideoDecodeThread::run()";

    MessageCmd_t Msgcmd;
    Msgcmd.cmd = MESSAGE_CMD_NONE;
    Msgcmd.cmdType = MESSAGE_QUEUE_TYPES;

    isFirstFrame = 1;
    bStop = 0;
    while(!bStop)
    {
        if (!pPlayerInfo)
        {
            qDebug()<<" VideoDecodeThread pPlayerInfo empty !! :";
            msleep(10);
            continue;
        }

        if ((pMessage->message_dequeue(&Msgcmd) == SUCCESS) && (Msgcmd.cmd == MESSAGE_CMD_FORCE_EOF))
        {
            qDebug()<<"video decode thread get EOF cmd~";
            bEof = 1;
        }

        if (pPlayerInfo->videoPacketQueue.Queue->isEmpty())
        {
            if (bEof == 1)
            {
                bStop = 1;//stop video decode thread

                MessageCmd_t MsgCmd;
                MsgCmd.cmd = MESSAGE_CMD_FORCE_EOF;
                MsgCmd.cmdType = MESSAGE_CMD_QUEUE;
                VideoOutput::Get()->queueMessage(MsgCmd);

                continue;
            }
            else
            {
                qDebug()<<" video Raw Queue empty !!";
                msleep(1);
                continue;
            }
        }

        if (!(pFrame = GetOneValidFrame(&pPlayerInfo->videoFrameQueue)))
        {
            if (!bStop)
            {
                //qDebug()<<" video GetOneValidFrame fail ==> sleep wait";
                pPlayerInfo->VDispQueue.mutex.lock();
                pPlayerInfo->pWaitCondVideoDecodeThread->wait(&(pPlayerInfo->VDispQueue.mutex));
                //qDebug()<<"video GetOneValidFrame fail ==> ==> wakeup";
                pPlayerInfo->VDispQueue.mutex.unlock();
            }
            continue;
        }

        if ((pMyPkt = GetOnePacket(&(pPlayerInfo->videoPacketQueue))) == NULL)
        {
            qDebug()<<" video GetOnePacket fail";
            continue;
        }

        ret = XFFmpeg::Get()->Decode(&pMyPkt->AVPkt, pFrame->frame);
        if (ret == 0)
        {
            av_free_packet(&pMyPkt->AVPkt);
            if (pMyPkt != NULL)
            {
                free((void *)pMyPkt);
                pMyPkt = NULL;
            }
            qDebug()<<"Video Dec error! pts:"<<pFrame->frame->pts;
            pFrame->DecState = DecWait;
            pFrame->DispState = DispOver;
            continue;
        }

        //qDebug()<<"video ==> pts = "<<pFrame->frame->pts;
        //free malloc pkt
        av_free_packet(&pMyPkt->AVPkt);
        if (pMyPkt != NULL)
        {
            free((void *)pMyPkt);
            pMyPkt = NULL;
        }

        pPlayerInfo->VDispQueue.mutex.lock();
        pFrame->DecState = DecOver;
        pFrame->DispState = DispWait;
        pPlayerInfo->VDispQueue.Queue->append(pFrame);
        pPlayerInfo->pWaitCondVideoOutputThread->wakeAll();
        pPlayerInfo->VDispQueue.mutex.unlock();
    }
    qDebug()<<"VideoDecodeThread::run() stop!";
}

void VideoDecodeThread::initPlayerInfo(PlayerInfo *pPI)
{
    if (pPI != NULL)
    {
        pPlayerInfo = pPI;
    }
}

void VideoDecodeThread::queueMessage(MessageCmd_t MsgCmd)
{
    pMessage->message_queue(MsgCmd);
}

void VideoDecodeThread::initDecodeFrameQueue(PlayerInfo *pPI)
{
    for (int i = 0; i < FRAME_QUEUE_SIZE; i++)
    {
        if (!(pPI->videoFrameQueue.queue[i].frame = av_frame_alloc()))
        {
            qDebug()<<"alloc frame fail 1";
            return;
        }
        pPI->videoFrameQueue.queue[i].DecState = DecButt;
        pPI->videoFrameQueue.queue[i].DispState = DispButt;
    }
    pPI->videoFrameQueue.size = FRAME_QUEUE_SIZE;
}

void VideoDecodeThread::deinitDecodeFrameQueue(PlayerInfo *pPI)
{
    if (pPI)
    {
        for (int i = 0; i < pPI->videoFrameQueue.size; i++)
        {
            if(pPI->videoFrameQueue.queue[i].frame)
            {
                av_frame_unref(pPI->videoFrameQueue.queue[i].frame);
                av_frame_free(&(pPI->videoFrameQueue.queue[i].frame));
                pPI->videoFrameQueue.queue[i].frame = NULL;
                pPI->videoFrameQueue.queue[i].DecState = DecButt;
                pPI->videoFrameQueue.queue[i].DispState = DispButt;
            }
        }
    }
}

void VideoDecodeThread::flushDecodeFrameQueue(PlayerInfo *pPI)
{
    for (int i = 0; i < pPI->videoFrameQueue.size; i++)
    {
        if(pPI->videoFrameQueue.queue[i].frame)
        {
            av_frame_unref(pPI->videoFrameQueue.queue[i].frame);
            pPI->videoFrameQueue.queue[i].DecState = DecButt;
            pPI->videoFrameQueue.queue[i].DispState = DispButt;
        }
    }
}


VideoDecodeThread::~VideoDecodeThread()
{
    qDebug()<<"~VideoDecodeThread() IN";

    stop();//stop run thread

    for (int i = 0; i < pPlayerInfo->videoFrameQueue.size; i++)
    {
        if(pPlayerInfo->videoFrameQueue.queue[i].frame)
        {
            av_frame_unref(pPlayerInfo->videoFrameQueue.queue[i].frame);
            av_frame_free(&(pPlayerInfo->videoFrameQueue.queue[i].frame));
            pPlayerInfo->videoFrameQueue.queue[i].frame = NULL;
            pPlayerInfo->videoFrameQueue.queue[i].DecState = DecButt;
            pPlayerInfo->videoFrameQueue.queue[i].DispState = DispButt;
        }
    }

    if (pMessage)
    {
        delete pMessage;
    }
}
