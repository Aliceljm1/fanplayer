// 包含头文件
#include <tchar.h>
#include "vdev.h"
#include "libavformat/avformat.h"

// 内部常量定义
#define DEF_VDEV_BUF_NUM  3

// 内部类型定义
typedef struct {
    // common members
    VDEV_COMMON_MEMBERS
    VDEV_WIN32__MEMBERS
    HDC      hdcsrc;
    HDC      hdcdst;
    HBITMAP *hbitmaps;
    BYTE   **pbmpbufs;
    int      nclear;
} VDEVGDICTXT;


// 内部函数实现
static void* video_render_thread_proc(void *param)
{
    VDEVGDICTXT  *c = (VDEVGDICTXT*)param;
    int pptsSzie;
    while (!(c->status & VDEV_CLOSE)) {
        pthread_mutex_lock(&c->mutex);
        //如果没有准备好的渲染数据，或状态是关闭 则等待线程信号
        while (c->size <= 0 && (c->status & VDEV_CLOSE) == 0) pthread_cond_wait(&c->cond, &c->mutex);
        if (c->size > 0) {//存在缓冲数据则进入，
            c->size--;
            pptsSzie= sizeof(c->ppts) / sizeof(c->ppts[0]);
            if (c->ppts[c->head] != -1) {
                if (c->cmnvars->avdiff > c->cmnvars->init_params->video_droptime){//如果无法及时渲染，丢帧
                    av_log(NULL, AV_LOG_ERROR, "***** drop frame:avdiff=%lld,  vpts=%lld,pptsSzie=%d\n", c->cmnvars->avdiff, c->cmnvars->vpts, pptsSzie);
                }
                else {
                    SelectObject(c->hdcsrc, c->hbitmaps[c->head]);
                    vdev_win32_render_bboxes(c, c->hdcsrc, c->bbox_list);
                    vdev_win32_render_overlay(c, c->hdcsrc, 1);
                    BitBlt(c->hdcdst, c->rrect.left, c->rrect.top, c->rrect.right - c->rrect.left, c->rrect.bottom - c->rrect.top, c->hdcsrc, 0, 0, SRCCOPY);
                    //拷贝到目标c->hdcdst中去。这个hdc就是显示窗口surface对应的hdc，ctxt->hdcdst = GetDC((HWND)surface); 此时UI完成刷新
                }
                c->cmnvars->vpts = c->ppts[c->head];//更新vpts
                av_log(NULL, AV_LOG_INFO, "vpts: %lld\n", c->cmnvars->vpts);
            }
            if (++c->head == c->bufnum) c->head = 0;//++c->head
            pthread_cond_signal(&c->cond);
        }
        pthread_mutex_unlock(&c->mutex);

        // handle av-sync & frame rate & complete
        vdev_avsync_and_complete(c);
    }

    return NULL;
}

/**
* 创建bitmap，图像关联buffer数据，外部会调用sws_scale填充数据 add by ljm 2022-4-6
*/
static void vdev_gdi_lock(void *ctxt, uint8_t *buffer[8], int linesize[8], int64_t pts)
{
    VDEVGDICTXT *c       = (VDEVGDICTXT*)ctxt;
    int          bmpw    =  0;
    int          bmph    =  0;
    BITMAPINFO   bmpinfo = {0};
    BITMAP       bitmap;

    pthread_mutex_lock(&c->mutex);
    //如果缓冲区域已满，或当前状态是关闭状态则等待信号
    while (c->size >= c->bufnum && (c->status & VDEV_CLOSE) == 0) pthread_cond_wait(&c->cond, &c->mutex);
    if (c->size < c->bufnum) {//如果没有到达缓冲区大小，则设置c->tail指向的缓冲区，需要设置三个数据c->ppts，c->hbitmaps，c->pbmpbufs
        c->ppts[c->tail] = pts;
        if (c->hbitmaps[c->tail]) {
            GetObject(c->hbitmaps[c->tail], sizeof(BITMAP), &bitmap);
            bmpw = bitmap.bmWidth ;
            bmph = bitmap.bmHeight;
        }

        //当前软件支持动态修改窗口大小，因此缓冲bitmap需要增加动态设置
        if (bmpw != c->rrect.right - c->rrect.left || bmph != c->rrect.bottom - c->rrect.top) {
            if (c->hbitmaps[c->tail]) DeleteObject(c->hbitmaps[c->tail]);
            bmpinfo.bmiHeader.biSize        =  sizeof(BITMAPINFOHEADER);
            bmpinfo.bmiHeader.biWidth       =  (c->rrect.right  - c->rrect.left);
            bmpinfo.bmiHeader.biHeight      = -(c->rrect.bottom - c->rrect.top );
            bmpinfo.bmiHeader.biPlanes      =  1;
            bmpinfo.bmiHeader.biBitCount    =  32;
            bmpinfo.bmiHeader.biCompression =  BI_RGB;
            c->hbitmaps[c->tail] = CreateDIBSection(c->hdcsrc, &bmpinfo, DIB_RGB_COLORS, (void**)&c->pbmpbufs[c->tail], NULL, 0);
            GetObject(c->hbitmaps[c->tail], sizeof(BITMAP), &bitmap);
        } else if (c->status & VDEV_CLEAR) {
            if (c->nclear++ != c->bufnum) {//清空bitmap数据归零，为黑色
                memset(c->pbmpbufs[c->tail], 0, bitmap.bmWidthBytes * bitmap.bmHeight);
            } else {
                c->nclear  = 0;
                c->status &= ~VDEV_CLEAR;
            }
        }
        //当前播放器支持设置视频在指定窗口位置任意区域播放，因此需要增加偏移c->vrect的像素偏移
        if (buffer  ) buffer  [0] = c->pbmpbufs[c->tail] + c->vrect.top * bitmap.bmWidthBytes + c->vrect.left * sizeof(uint32_t);
        if (linesize) linesize[0] = bitmap.bmWidthBytes;
        if (linesize) linesize[6] = c->vrect.right - c->vrect.left;
        if (linesize) linesize[7] = c->vrect.bottom - c->vrect.top;
        if (!(linesize[6] & 1)) linesize[6] -= 1; // fix swscale right side white line issue.
    }
}

/**
*解锁之后会唤起渲染线程继续执行，pthread_cond_wait会被唤起
* 默认缓冲bitmap队列长度为3，总是利用尾部解码
*c->size表示当前缓冲的数据的个数，c->tail 指向可以随时给解码过程利用的缓冲区尾部序号
*c->head指向已经准备好的缓冲区序号
* unlock执行完毕说明c->tail指向的缓冲bitmap数据准备妥当，准备解码下一帧数据
* 在渲染线程中会执行 ++c->heard， 这样c->heard就一直更着c->tail递增， 只有在一开始和播放结束的时候两个值才都是0
*/
static void vdev_gdi_unlock(void *ctxt)
{
    VDEVGDICTXT *c = (VDEVGDICTXT*)ctxt;
    if (++c->tail == c->bufnum) c->tail = 0;
    c->size++; pthread_cond_signal(&c->cond);
    pthread_mutex_unlock(&c->mutex);
}

static void vdev_gdi_destroy(void *ctxt)
{
    VDEVGDICTXT *c = (VDEVGDICTXT*)ctxt;
    int          i;

    DeleteDC (c->hdcsrc);
    ReleaseDC((HWND)c->surface, c->hdcdst);
    for (i=0; i<c->bufnum; i++) {
        if (c->hbitmaps[i]) {//回收bitmap对象
            DeleteObject(c->hbitmaps[i]);
        }
    }

    pthread_mutex_destroy(&c->mutex);
    pthread_cond_destroy (&c->cond );

    free(c->ppts    );
    free(c->hbitmaps);
    free(c->pbmpbufs);
    free(c);
}

// 接口函数实现
void* vdev_gdi_create(void *surface, int bufnum)
{
    VDEVGDICTXT *ctxt = (VDEVGDICTXT*)calloc(1, sizeof(VDEVGDICTXT));
    if (!ctxt) return NULL;

    // init mutex & cond
    pthread_mutex_init(&ctxt->mutex, NULL);
    pthread_cond_init (&ctxt->cond , NULL);

    // init vdev context， 目前缓冲区定义为3比较合适
    bufnum         = bufnum ? bufnum : DEF_VDEV_BUF_NUM;
    ctxt->surface  = surface;
    ctxt->bufnum   = bufnum;
    ctxt->pixfmt   = AV_PIX_FMT_RGB32;
    ctxt->lock     = vdev_gdi_lock;
    ctxt->unlock   = vdev_gdi_unlock;
    ctxt->destroy  = vdev_gdi_destroy;

    // alloc buffer & semaphore
    ctxt->ppts     = (int64_t*)calloc(bufnum, sizeof(int64_t));
    ctxt->hbitmaps = (HBITMAP*)calloc(bufnum, sizeof(HBITMAP));
    ctxt->pbmpbufs = (BYTE**  )calloc(bufnum, sizeof(BYTE*  ));

    ctxt->hdcdst = GetDC((HWND)surface);
    ctxt->hdcsrc = CreateCompatibleDC(ctxt->hdcdst);
    if (!ctxt->ppts || !ctxt->hbitmaps || !ctxt->pbmpbufs || !ctxt->mutex || !ctxt->cond || !ctxt->hdcdst || !ctxt->hdcsrc) {
        av_log(NULL, AV_LOG_ERROR, "failed to allocate resources for vdev-gdi !\n");
        exit(0);
    }

    // create video rendering thread
    pthread_create(&ctxt->thread, NULL, video_render_thread_proc, ctxt);
    return ctxt;
}
