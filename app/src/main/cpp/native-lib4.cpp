#include<android/log.h>
#include <jni.h>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <iostream>

extern "C" {
#include <libavutil/log.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/samplefmt.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

extern const char *TAG;

static int get_format_from_sample_fmt(const char **fmt, enum AVSampleFormat sample_fmt);

static void fill_samples(double *dst, int nb_samples, int nb_channels, int sample_rate, double *t);

extern "C"
JNIEXPORT jint JNICALL
Java_com_example_helloffmpeg_MainActivity_resampleAudio(JNIEnv *env, jobject thiz,
                                                        jstring dst_file_path) {
    int64_t src_ch_layout = AV_CH_LAYOUT_STEREO, dst_ch_layout = AV_CH_LAYOUT_SURROUND;
    int src_sample_rate = 48000, dst_sample_rate = 44100;
    enum AVSampleFormat src_sample_fmt = AV_SAMPLE_FMT_DBL, dst_sample_fmt = AV_SAMPLE_FMT_S16;

    uint8_t **src_data = nullptr, **dst_data = nullptr;
    int src_linesize, dst_linesize;
    int src_nb_channels = 0, dst_nb_channels = 0;
    //每次要处理的采样的个数（每个声道），可以随意指定
    int src_nb_samples = 1024;
    int dst_nb_samples, max_dst_nb_samples;

    const char *dstFilename = nullptr;
    FILE *dst_file = nullptr;
    int dst_bufsize;
    const char *fmt = nullptr;
    struct SwrContext *swr_ctx = nullptr;
    double t;
    int ret;

    dstFilename = env->GetStringUTFChars(dst_file_path, nullptr);
    __android_log_write(ANDROID_LOG_ERROR, TAG, dstFilename);
    dst_file = fopen(dstFilename, "wbe");
    if (!dst_file) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "Could not open destination file %s\n",
                            dstFilename);
        goto end;
    }

    /* create resampler context */
    swr_ctx = swr_alloc();
    if (!swr_ctx) {
        __android_log_write(ANDROID_LOG_ERROR, TAG, "Could not allocate resampler context\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    //设置输入参数
    av_opt_set_int(swr_ctx, "in_channel_layout", src_ch_layout, 0);
    av_opt_set_int(swr_ctx, "in_sample_rate", src_sample_rate, 0);
    av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", src_sample_fmt, 0);
    //设置输出参数
    av_opt_set_int(swr_ctx, "out_channel_layout", dst_ch_layout, 0);
    av_opt_set_int(swr_ctx, "out_sample_rate", dst_sample_rate, 0);
    av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", dst_sample_fmt, 0);

    if ((ret = swr_init(swr_ctx)) < 0) {
        __android_log_write(ANDROID_LOG_ERROR, TAG,
                            "Failed to initialize the resampling context\n");
        goto end;
    }

    src_nb_channels = av_get_channel_layout_nb_channels(src_ch_layout);
    //初始化src_data和src_linesize
    ret = av_samples_alloc_array_and_samples(&src_data, &src_linesize, src_nb_channels,
                                             src_nb_samples, src_sample_fmt, 0);
    if (ret < 0) {
        __android_log_write(ANDROID_LOG_ERROR, TAG, "Could not allocate source samples\n");
        goto end;
    }

    //计算dst_nb_samples
    max_dst_nb_samples = dst_nb_samples =
            av_rescale_rnd(src_nb_samples, dst_sample_rate, src_sample_rate, AV_ROUND_UP);

    dst_nb_channels = av_get_channel_layout_nb_channels(dst_ch_layout);
    //初始化dst_data和dst_linesize
    //buffer is going to be directly written to a rawaudio file, no alignment
    ret = av_samples_alloc_array_and_samples(&dst_data, &dst_linesize, dst_nb_channels,
                                             dst_nb_samples, dst_sample_fmt, 0);
    if (ret < 0) {
        __android_log_write(ANDROID_LOG_ERROR, TAG, "Could not allocate destination samples\n");
        goto end;
    }

    //时间
    t = 0;
    //循环终止条件为t<10，即会造10s的源数据
    do {
        //造src_nb_samples个采样数据，填充到src_data[0]中
        fill_samples((double *) src_data[0], src_nb_samples, src_nb_channels, src_sample_rate, &t);

        //计算dst_nb_samples
        //swr_get_delay(swr_ctx, src_sample_rate)获得的是上次被缓存起来的，没有来得及被处理的src采样
        dst_nb_samples = av_rescale_rnd(swr_get_delay(swr_ctx, src_sample_rate) + src_nb_samples,
                                        dst_sample_rate, src_sample_rate, AV_ROUND_UP);
        //空间不够用，重新初始化dst_data和dst_linesize
        if (dst_nb_samples > max_dst_nb_samples) {
            av_freep(&dst_data[0]);
            ret = av_samples_alloc(dst_data, &dst_linesize, dst_nb_channels,
                                   dst_nb_samples, dst_sample_fmt, 1);
            if (ret < 0)break;
            max_dst_nb_samples = dst_nb_samples;
        }

        //convert
        //ret：number of samples output per channel，即实际产生的dst采样的个数
        ret = swr_convert(swr_ctx, dst_data, dst_nb_samples, (const uint8_t **) src_data,
                          src_nb_samples);
        if (ret < 0) {
            __android_log_write(ANDROID_LOG_ERROR, TAG, "Error while converting\n");
            goto end;
        }

        dst_bufsize = av_samples_get_buffer_size(&dst_linesize, dst_nb_channels,
                                                 ret, dst_sample_fmt, 1);
        if (dst_bufsize < 0) {
            __android_log_write(ANDROID_LOG_ERROR, TAG, "Could not get sample buffer size\n");
            goto end;
        }
        __android_log_print(ANDROID_LOG_ERROR, TAG, "t:%f in:%d out:%d\n", t, src_nb_samples, ret);
        //将dst采样写入dst文件
        fwrite(dst_data[0], 1, dst_bufsize, dst_file);
    } while (t < 10);

    if ((ret = get_format_from_sample_fmt(&fmt, dst_sample_fmt)) < 0)
        goto end;

    __android_log_print(ANDROID_LOG_ERROR, TAG,
                        "Resampling succeeded. Play the output file with the command:\n"
                        "ffplay -f %s -channel_layout %lli -channels %d -ar %d %s\n",
                        fmt, dst_ch_layout, dst_nb_channels, dst_sample_rate, dstFilename);

    end:
    env->ReleaseStringUTFChars(dst_file_path, dstFilename);
    if (dst_file) fclose(dst_file);
    if (src_data) {
        av_freep(&src_data[0]);
        av_freep(&src_data);
    }
    if (dst_data) {
        av_freep(&dst_data[0]);
        av_freep(&dst_data);
    }
    if (swr_ctx) swr_free(&swr_ctx);

    return ret < 0;
}

static int get_format_from_sample_fmt(const char **fmt, enum AVSampleFormat sample_fmt) {
    int i;
    struct sample_fmt_entry {
        enum AVSampleFormat sample_fmt;
        const char *fmt_be, *fmt_le;
    } sample_fmt_entries[] = {
            {AV_SAMPLE_FMT_U8,  "u8",    "u8"},
            {AV_SAMPLE_FMT_S16, "s16be", "s16le"},
            {AV_SAMPLE_FMT_S32, "s32be", "s32le"},
            {AV_SAMPLE_FMT_FLT, "f32be", "f32le"},
            {AV_SAMPLE_FMT_DBL, "f64be", "f64le"},
    };
    *fmt = nullptr;

    //遍历sample_fmt_entries数组
    for (i = 0; i < FF_ARRAY_ELEMS(sample_fmt_entries); i++) {
        struct sample_fmt_entry *entry = &sample_fmt_entries[i];
        if (sample_fmt == entry->sample_fmt) {
            *fmt = AV_NE(entry->fmt_be, entry->fmt_le);
            return 0;
        }
    }

    __android_log_print(ANDROID_LOG_ERROR, TAG, "Sample format %s not supported as output format\n",
                        av_get_sample_fmt_name(sample_fmt));
    return AVERROR(EINVAL);
}

//t为时间，单位秒
static void fill_samples(double *dst, int nb_samples, int nb_channels, int sample_rate, double *t) {
    int i, j;
    //t increasement:t的步进
    double tincr = 1.0 / sample_rate;
    double *dstp = dst;
    const double c = 2 * M_PI * 440.0;

    /* generate sin tone with 440Hz frequency and duplicated channels */
    //一次循环为一个采样
    //采用packed存储格式（AV_SAMPLE_FMT_DBL）
    //即采样1声道1 采样1声道2 采样2声道1 采样2声道2...
    for (i = 0; i < nb_samples; i++) {
        //第一个声道的数据
        *dstp = sin(c * *t);
        //后面的声道的数据
        for (j = 1; j < nb_channels; j++)
            dstp[j] = dstp[0];

        dstp += nb_channels;
        *t += tincr;
    }
}
