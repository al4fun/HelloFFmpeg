#include<android/log.h>
#include <jni.h>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <iostream>

extern "C" {
#include <libavutil/imgutils.h>
#include <libavutil/parseutils.h>
#include <libswscale/swscale.h>
}

extern const char *TAG;

static void fill_yuv_image(uint8_t *data[4], int linesize[4],
                           int width, int height, int frame_index);
/**
 * 使用SwsContext来对avframe数据进行像素格式转换(如yuv转rgb)和缩放
 * 参考：《SwsContext和sws_scale.md》
 */
extern "C"
JNIEXPORT jint JNICALL
Java_com_example_helloffmpeg_MainActivity_scaleVideo(JNIEnv *env, jobject thiz,
                                                     jstring dst_file_path) {
    uint8_t *src_data[4], *dst_data[4];
    int src_linesize[4], dst_linesize[4];
    int src_w = 320, src_h = 240, dst_w, dst_h;
    enum AVPixelFormat src_pix_fmt = AV_PIX_FMT_YUV420P, dst_pix_fmt = AV_PIX_FMT_RGB24;
    const char *dst_size = nullptr;
    const char *dstFilename = nullptr;
    FILE *dst_file = nullptr;
    int dst_bufsize;
    struct SwsContext *sws_ctx = nullptr;
    int i, ret;

    dst_size = "640x480";
    if (av_parse_video_size(&dst_w, &dst_h, dst_size) < 0) {
        __android_log_print(ANDROID_LOG_ERROR, TAG,
                            "Invalid size '%s', must be in the form WxH or a valid size abbreviation\n",
                            dst_size);
        goto end;
    }

    dstFilename = env->GetStringUTFChars(dst_file_path, nullptr);
    __android_log_write(ANDROID_LOG_ERROR, TAG, dstFilename);
    dst_file = fopen(dstFilename, "wbe");
    if (!dst_file) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "Could not open destination file %s\n",
                            dstFilename);
        goto end;
    }

    //创建SwsContext
    sws_ctx = sws_getContext(src_w, src_h, src_pix_fmt,
                             dst_w, dst_h, dst_pix_fmt,
                             SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_ctx) {
        __android_log_print(ANDROID_LOG_ERROR, TAG,
                            "Impossible to create scale context for the conversion "
                            "fmt:%s s:%dx%d -> fmt:%s s:%dx%d\n",
                            av_get_pix_fmt_name(src_pix_fmt), src_w, src_h,
                            av_get_pix_fmt_name(dst_pix_fmt), dst_w, dst_h);
        ret = AVERROR(EINVAL);
        goto end;
    }

    //为image分配空间并填充src_data和src_linesize
    if ((ret = av_image_alloc(src_data, src_linesize,
                              src_w, src_h, src_pix_fmt, 16)) < 0) {
        __android_log_write(ANDROID_LOG_ERROR, TAG, "Could not allocate source image\n");
        goto end;
    }

    //为image分配空间并填充dst_data和dst_linesize
    /* buffer is going to be written to rawvideo file, no alignment */
    if ((ret = av_image_alloc(dst_data, dst_linesize,
                              dst_w, dst_h, dst_pix_fmt, 1)) < 0) {
        __android_log_write(ANDROID_LOG_ERROR, TAG, "Could not allocate destination image\n");
        goto end;
    }
    dst_bufsize = ret;

    //生成一百张yuv image并转换
    for (i = 0; i < 100; i++) {
        /* generate synthetic video */
        fill_yuv_image(src_data, src_linesize, src_w, src_h, i);

        //转换像素格式并进行缩放
        sws_scale(sws_ctx, (const uint8_t *const *) src_data,
                  src_linesize, 0, src_h, dst_data, dst_linesize);

        //写入文件
        //将100张图片写入同一个文件，形成一个原始视频文件
        fwrite(dst_data[0], 1, dst_bufsize, dst_file);
    }

    __android_log_print(ANDROID_LOG_ERROR, TAG,
                        "Scaling succeeded. Play the output file with the command:\n"
                        "ffplay -f rawvideo -pix_fmt %s -video_size %dx%d %s\n",
                        av_get_pix_fmt_name(dst_pix_fmt), dst_w, dst_h, dstFilename);

    end:
    env->ReleaseStringUTFChars(dst_file_path, dstFilename);
    if (dst_file) fclose(dst_file);
    if (src_data) av_freep(&src_data[0]);
    if (dst_data) av_freep(&dst_data[0]);
    if (sws_ctx) sws_freeContext(sws_ctx);
    return ret < 0;
}

/**
 * 生成一张yuv image，存入data和linesize
 */
static void fill_yuv_image(uint8_t *data[4], int linesize[4],
                           int width, int height, int frame_index) {
    int x, y;

    /* Y */
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++)
            data[0][y * linesize[0] + x] = x + y + frame_index * 3;

    /* Cb and Cr */
    for (y = 0; y < height / 2; y++) {
        for (x = 0; x < width / 2; x++) {
            data[1][y * linesize[1] + x] = 128 + y + frame_index * 2;
            data[2][y * linesize[2] + x] = 64 + x + frame_index * 5;
        }
    }
}