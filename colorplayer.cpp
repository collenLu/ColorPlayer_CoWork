#include "colorplayer.h"
#include "audioplay_sdl2.h"
#include "audiodecodethread.h"
#include "videodecodethread.h"
#include "demuxthread.h"
#include "videooutput.h"
#include <QDebug>
#include <QMessageBox>

static int  _seekDoneVideo = 0;
static int  _seekDoneAudio = 0;
QMutex mutex;
QWaitCondition WaitCondStopDone;

void seekDoneCallBack(mediaItem eMediaItem)
{
    mutex.lock();
    if (eMediaItem == mediaItem_video)
    {
        _seekDoneVideo = 1;
        qDebug()<<"video seek done";
    }
    else if (eMediaItem == mediaItem_audio)
    {
        _seekDoneAudio = 1;
        qDebug()<<"audio seek done";
    }
    else
    {
        qDebug()<<"seekDoneCallBack eMediaItem error!\n"<<eMediaItem;
    }

    if (_seekDoneVideo && _seekDoneAudio)
    {
        WaitCondStopDone.wakeAll();
        qDebug()<<"video audio seekDoneCallBack ok!\n";
    }
    mutex.unlock();
}

ColorPlayer::ColorPlayer()
{
    player = NULL;
    bOpened = 0;
    pMasterClock = new MasterClock();
}

int ColorPlayer::open(const char *url)
{
    qDebug()<<"ColorPlayer::open IN";
    mutex.lock();

    if (!player)
    {
        player = (PlayerInfo *)malloc(sizeof(PlayerInfo));
        if (!player)
        {
            qDebug()<<"alloc PlayerInfo fail";
            mutex.unlock();
            return FAILED;
        }
        memset(player, 0, sizeof(PlayerInfo));
        player->pWaitCondAudioDecodeThread = new QWaitCondition;
        player->pWaitCondAudioOutputThread = new QWaitCondition;
        player->pWaitCondVideoDecodeThread = new QWaitCondition;
        player->pWaitCondVideoOutputThread = new QWaitCondition;
    }

    if (!XFFmpeg::Get()->Open(url))
    {
        qDebug()<<"ffmpeg open fail";
        mutex.unlock();
        return FAILED;
    }

    DemuxThread::Get()->initPlayerInfo(player);
    DemuxThread::Get()->initRawQueue(player);
    VideoDecodeThread::Get()->initPlayerInfo(player);
    VideoDecodeThread::Get()->initDecodeFrameQueue(player);
    VideoOutput::Get()->initPlayerInfo(player);
    VideoOutput::Get()->initDisplayQueue(player);
    VideoOutput::Get()->initMasterClock(pMasterClock);
    VideoOutput::Get()->setCallback(seekDoneCallBack);
    AudioDecodeThread::Get()->initPlayerInfo(player);
    AudioDecodeThread::Get()->initDecodeFrameQueue(player);
    SDL2AudioDisplayThread::Get()->initPlayerInfo(player);
    SDL2AudioDisplayThread::Get()->initDisplayQueue(player);
    SDL2AudioDisplayThread::Get()->init();
    SDL2AudioDisplayThread::Get()->initMasterClock(pMasterClock);
    SDL2AudioDisplayThread::Get()->setCallback(seekDoneCallBack);
    pMasterClock->open(AUDIO_MASTER);
    bOpened = 1;

    mutex.unlock();

    return SUCCESS;
}

int ColorPlayer::close()
{
    qDebug()<<"ColorPlayer start to close";
    stop();

    VideoOutput::Get()->deinitDisplayQueue(player);

    SDL2AudioDisplayThread::Get()->deinitDisplayQueue(player);
    SDL2AudioDisplayThread::Get()->deinit();

    VideoDecodeThread::Get()->deinitDecodeFrameQueue(player);

    AudioDecodeThread::Get()->deinitDecodeFrameQueue(player);

    DemuxThread::Get()->deinitRawQueue(player);

    XFFmpeg::Get()->Close();

    pMasterClock->close();

    qDebug()<<"ColorPlayer end to close";
    return SUCCESS;
}

int ColorPlayer::play()
{
    qDebug()<< "start thread";
    SDL2AudioDisplayThread::Get()->start();
    AudioDecodeThread::Get()->start();
    VideoDecodeThread::Get()->start();
    VideoOutput::Get()->start();
    DemuxThread::Get()->start();
    if (player)
        player->playerState = PLAYER_STATE_START;

    return SUCCESS;
}

int ColorPlayer::pause()
{
    mutex.lock();
    MessageCmd_t MsgCmd;

    qDebug()<< "ColorPlayer send pause cmd!!";
    MsgCmd.cmd = MESSAGE_CMD_PAUSE;
    MsgCmd.cmdType = MESSAGE_CMD_QUEUE;
    SDL2AudioDisplayThread::Get()->queueMessage(MsgCmd);
    VideoOutput::Get()->queueMessage(MsgCmd);
    if (player)
        player->playerState = PLAYER_STATE_PAUSE;

    mutex.unlock();

    return SUCCESS;
}

int ColorPlayer::resume()
{
    mutex.lock();
    MessageCmd_t MsgCmd;

    qDebug()<< "ColorPlayer send resume cmd!!";
    MsgCmd.cmd = MESSAGE_CMD_RESUME;
    MsgCmd.cmdType = MESSAGE_CMD_QUEUE;
    SDL2AudioDisplayThread::Get()->queueMessage(MsgCmd);
    VideoOutput::Get()->queueMessage(MsgCmd);
    if (player)
        player->playerState = PLAYER_STATE_RESUME;

    mutex.unlock();

    return SUCCESS;
}

int ColorPlayer::stop()
{
    if (player)
    {
        player->playerState = PLAYER_STATE_STOP;
    }

    SDL2AudioDisplayThread::Get()->stop();
    VideoOutput::Get()->stop();

    VideoDecodeThread::Get()->stop();
    AudioDecodeThread::Get()->stop();

    DemuxThread::Get()->stop();

    return SUCCESS;
}

int ColorPlayer::set_pos()
{
    return SUCCESS;
}

int64_t ColorPlayer::get_pos()
{
    return pMasterClock->get_audio_clock();
}

int ColorPlayer::get_play_time_ms()
{
    return XFFmpeg::Get()->totalMs;
}

int ColorPlayer::get_video_width()
{
    return XFFmpeg::Get()->width;
}

int ColorPlayer::get_video_height()
{
    return XFFmpeg::Get()->height;
}

int ColorPlayer::cancel_seek()
{
    return SUCCESS;
}

int ColorPlayer::need_avsync()
{
    MessageCmd_t MsgCmd;

    qDebug()<< "ColorPlayer send need_avsync cmd!!";
    MsgCmd.cmd = MESSAGE_CMD_NEED_AVSYNC;
    MsgCmd.cmdType = MESSAGE_CMD_QUEUE;
    if (VideoOutput::Get())
        VideoOutput::Get()->queueMessage(MsgCmd);
    return SUCCESS;
}

int ColorPlayer::cancel_avsync()
{
    MessageCmd_t MsgCmd;

    qDebug()<< "ColorPlayer send cancel_avsync cmd!!";
    MsgCmd.cmd = MESSAGE_CMD_CANCEL_AVSYNC;
    MsgCmd.cmdType = MESSAGE_CMD_QUEUE;
    if (VideoOutput::Get())
        VideoOutput::Get()->queueMessage(MsgCmd);
    return SUCCESS;
}

void ColorPlayer::flush()
{
    VideoOutput::Get()->flush();

    SDL2AudioDisplayThread::Get()->flush();

    VideoDecodeThread::Get()->flushDecodeFrameQueue(player);

    AudioDecodeThread::Get()->flushDecodeFrameQueue(player);

    DemuxThread::Get()->deinitRawQueue(player);

    XFFmpeg::Get()->Flush();

    pMasterClock->flush();
}

int ColorPlayer::seek(float position)
{
    qDebug()<<"ColorPlayer::seek IN";
    mutex.lock();
    _seekDoneVideo = 0;
    _seekDoneAudio = 0;
    stop();
    if (XFFmpeg::Get()->Seek(position) == true)
    {
        flush();
    }
    play();

    while((_seekDoneVideo != 1) || (_seekDoneVideo != 1))
    {
        //2s timeout
        if (!WaitCondStopDone.wait(&mutex, 2000))
        {
            qDebug()<<"ColorPlayer::seek  wait timeout";
            break;
        }
    }

    qDebug()<<"color player seek ok!\n";
    mutex.unlock();
    return SUCCESS;
}

int ColorPlayer::set_speed()
{
    return SUCCESS;
}

int ColorPlayer::get_speed()
{
    return SUCCESS;
}

PlayerInfo* ColorPlayer::get_player_Instanse()
{
    return player;
}

void ColorPlayer::init_context()
{

}

void ColorPlayer::deinit_context()
{

}

ColorPlayer::~ColorPlayer()
{
    qDebug()<<"ColorPlayer::~ColorPlayer()";

    if (!pMasterClock)
    {
        delete pMasterClock;
    }

    if (player->pWaitCondAudioDecodeThread)
    {
        delete player->pWaitCondAudioDecodeThread;
    }

    if (player->pWaitCondAudioOutputThread)
    {
        delete player->pWaitCondAudioOutputThread;
    }

    if (player->pWaitCondVideoDecodeThread)
    {
        delete player->pWaitCondVideoDecodeThread;
    }

    if (player->pWaitCondVideoOutputThread)
    {
        delete player->pWaitCondVideoOutputThread;
    }

    if (!player)
    {
        free((void *)player);
    }
}
