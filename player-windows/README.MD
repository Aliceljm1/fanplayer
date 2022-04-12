## 代码阅读笔记

相关文件说明：
ffplayer.c，所有播放器流程控制
adev-win.c, Windows下audio播放处理
vdev-d3d.c，d3d渲染video
vdev-gdi， gid渲染video
vdev-cmn.c,对于video渲染层的统一抽象
## 下列部分函数相关详细描述见 ffplayer.h


初始化ffplayer
void* player_open(char *file, void *win, PLAYER_INIT_PARAMS *params)

播放准备工作
static int player_prepare(PLAYER *player)

初始化渲染HDC
void* veffect_create(void *surface)

解码协调线程，数据存入队列(void pktqueue_video_enqueue(void *ctxt, AVPacket *pkt))
static void* av_demux_thread_proc(void *param)

视频解码线程
static void* video_decode_thread_proc(void *param)

视频渲染函数
void render_video(void *hrender, AVFrame *video)

d3d渲染线程：使用 pthread_mutex_lock(&c->mutex);pthread_cond_wait 等待信号 来控制渲染，每次数据准备好之后调用pthread_cond_signal，pthread_mutex_unlock 渲染线程自动启动，开始渲染
static void* video_render_thread_proc(void *param)

音频解码线程
static void* audio_decode_thread_proc(void *param)

## 细节方面
创建render的两个dev, 通过fanplayer.ini来决定是创建d3d还是gdi,(d3d配置项为vdev_render_type=1 ,gdi=0)
    // init for cmnvars
    render->adev = adev_create(adevtype, 5, render->adev_buf_size, cmnvars);
    render->vdev = vdev_create(vdevtype, surface, 0, w, h, 1000 * frate.den / frate.num, cmnvars);

支持播放的多媒体类型和协议见如下函数（rtsp,rtmp,mp4,h264,h265,jpg,gif）
void CplayerDlg::PlayerOpenFile(TCHAR *file)

日志级别定义：av_log_set_level   (AV_LOG_INFO); 可以自己写代码输入日志到文件中去

拖动进度条调用函数，修改播放器状态，设置seek值。 拖动之后会协调解码线程执行seek,搜索：PS_F_SEEK
void player_seek(void *hplayer, int64_t ms, int type)

播放事件相关消息在播放器的各个逻辑处会发送消息如MSG_OPEN_DONE，MSG_PLAY_COMPLETED

#EXTINF:-1 ,CCTV13 测试m3u8视频播放器
http://39.134.115.163:8080/PLTV/88888910/224/3221225638/index.m3u8

