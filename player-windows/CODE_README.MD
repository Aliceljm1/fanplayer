## 代码阅读笔记

相关文件说明：
adev-win.c, Windows下audio播放处理
vdev-d3d.c，d3d渲染video
vdev-gdi， gid渲染video
vdev-cmn.c,对于video渲染层的统一抽象


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


创建render的两个dev, 通过fanplayer.ini来决定是创建d3d还是gdi,(d3d配置项为vdev_render_type=1 )
    // init for cmnvars
    render->adev = adev_create(adevtype, 5, render->adev_buf_size, cmnvars);
    render->vdev = vdev_create(vdevtype, surface, 0, w, h, 1000 * frate.den / frate.num, cmnvars);


