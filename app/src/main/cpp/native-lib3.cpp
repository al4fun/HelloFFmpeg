#include<android/log.h>
#include <jni.h>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

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
}

extern const char *TAG;

#define AUDIO_INBUF_SIZE 20480
#define AUDIO_REFILL_THRESH 4096

static void decode(AVCodecContext *avCodecContext, AVPacket *pkt, AVFrame *avFrame,
                   FILE *outfile);

static int get_format_from_sample_fmt(const char **fmt,
                                      enum AVSampleFormat sample_fmt);

//解码aac文件得到pcm文件----------------------------------------------------------------
extern "C"
JNIEXPORT jint JNICALL
Java_com_example_helloffmpeg_MainActivity_decodeAudio(JNIEnv *env, jobject thiz, jstring file_path,
                                                      jstring dst_file_path) {
    const char *filename = env->GetStringUTFChars(file_path, nullptr);
    const char *outfilename = env->GetStringUTFChars(dst_file_path, nullptr);
    __android_log_write(ANDROID_LOG_ERROR, TAG, filename);
    __android_log_write(ANDROID_LOG_ERROR, TAG, outfilename);

    const AVCodec *avCodec = nullptr;
    AVCodecContext *avCodecContext = nullptr;
    AVCodecParserContext *avCodecParserContext = nullptr;
    int len, ret;
    FILE *infile = nullptr, *outfile = nullptr;
    uint8_t inbuf[AUDIO_INBUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
    uint8_t *data = nullptr;
    size_t data_size;
    AVPacket *pkt = nullptr;
    AVFrame *avFrame = nullptr;
    enum AVSampleFormat avSampleFormat;
    int n_channels = 0;
    const char *fmt = nullptr;

    pkt = av_packet_alloc();

    avCodec = avcodec_find_decoder(AV_CODEC_ID_AAC);
    if (!avCodec) {
        __android_log_write(ANDROID_LOG_ERROR, TAG, "Codec not found\n");
        goto end;
    }

    avCodecParserContext = av_parser_init(avCodec->id);
    if (!avCodecParserContext) {
        __android_log_write(ANDROID_LOG_ERROR, TAG, "avCodecParserContext not found\n");
        goto end;
    }

    avCodecContext = avcodec_alloc_context3(avCodec);
    if (!avCodecContext) {
        __android_log_write(ANDROID_LOG_ERROR, TAG, "Could not allocate avCodecContext\n");
        goto end;
    }

    if (avcodec_open2(avCodecContext, avCodec, nullptr) < 0) {
        __android_log_write(ANDROID_LOG_ERROR, TAG, "Could not open avCodec\n");
        goto end;
    }

    infile = fopen(filename, "rbe");
    if (!infile) {
        __android_log_write(ANDROID_LOG_ERROR, TAG, "Could not open in file\n");
        __android_log_write(ANDROID_LOG_ERROR, TAG, strerror(errno));
        goto end;
    }
    outfile = fopen(outfilename, "wbe");
    if (!outfile) {
        __android_log_write(ANDROID_LOG_ERROR, TAG, "Could not open out file\n");
        __android_log_write(ANDROID_LOG_ERROR, TAG, strerror(errno));
        goto end;
    }

    /* decode until eof */
    data = inbuf;
    data_size = fread(inbuf, 1, AUDIO_INBUF_SIZE, infile);

    while (data_size > 0) {
        //如果avFrame为空，则创建一个avFrame
        if (!avFrame) {
            if (!(avFrame = av_frame_alloc())) {
                __android_log_write(ANDROID_LOG_ERROR, TAG, "Could not allocate video avFrame\n");
                goto end;
            }
        }

        //从data中解析出avPacket数据
        ret = av_parser_parse2(avCodecParserContext, avCodecContext, &pkt->data, &pkt->size,
                               data, data_size,
                               AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
        if (ret < 0) {
            __android_log_write(ANDROID_LOG_ERROR, TAG, "Error while parsing\n");
            goto end;
        }
        //后移数组指针并更新data_size
        data += ret;
        data_size -= ret;

        //解码avPacket,并存入文件
        if (pkt->size)
            decode(avCodecContext, pkt, avFrame, outfile);

        //剩余的数据不多了，将剩余数据移动到inbuf的前部
        //并从文件中再读取一次数据
        if (data_size < AUDIO_REFILL_THRESH) {
            memmove(inbuf, data, data_size);
            data = inbuf;
            len = fread(data + data_size, 1,
                        AUDIO_INBUF_SIZE - data_size, infile);
            if (len > 0)
                data_size += len;
        }
    }

    /* flush the decoder */
    pkt->data = nullptr;
    pkt->size = 0;
    decode(avCodecContext, pkt, avFrame, outfile);

    //打印输出文件的信息
    /* print output pcm infomations, because there have no metadata of pcm */
    avSampleFormat = avCodecContext->sample_fmt;
    //采样格式如果是planar的,则获得packed版的采样格式
    //因为在decode函数中，我们将数据存为文件时，采用的是packed的存法
    if (av_sample_fmt_is_planar(avSampleFormat)) {
        //获得packed版的采样格式
        avSampleFormat = av_get_packed_sample_fmt(avSampleFormat);
    }
    n_channels = avCodecContext->channels;
    if (get_format_from_sample_fmt(&fmt, avSampleFormat) < 0)
        goto end;
    __android_log_print(ANDROID_LOG_ERROR, TAG,
                        "Play the output audio file with the command:\n"
                        "ffplay -f %s -ac %d -ar %d %s\n",
                        fmt, n_channels, avCodecContext->sample_rate,
                        outfilename);

    //释放资源
    end:
    env->ReleaseStringUTFChars(file_path, filename);
    env->ReleaseStringUTFChars(dst_file_path, outfilename);
    if (outfile) fclose(outfile);
    if (infile) fclose(infile);
    if (avCodecContext) avcodec_free_context(&avCodecContext);
    if (avCodecParserContext) av_parser_close(avCodecParserContext);
    if (avFrame) av_frame_free(&avFrame);
    if (pkt) av_packet_free(&pkt);

    return 0;
}

/**
 * 解码avPacket,并存入文件
 */
static void decode(AVCodecContext *avCodecContext, AVPacket *pkt, AVFrame *avFrame,
                   FILE *outfile) {
    int i, ch;
    int ret, data_size;

    /* send the packet with the compressed data to the decoder */
    ret = avcodec_send_packet(avCodecContext, pkt);
    if (ret < 0) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "Error sending a packet for decoding: %s\n",
                            av_err2str(ret));
        return;
    }

    /* read all the output frames (in general there may be any number of them */
    while (ret >= 0) {
        //解码出frame并存入avFrame参数
        ret = avcodec_receive_frame(avCodecContext, avFrame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0) {
            __android_log_write(ANDROID_LOG_ERROR, TAG, "Error during decoding\n");
            return;
        }

        //获取该采样格式每个采样是多少字节
        //一个采样中可能包含多个声道，每个声道的数据大小都是data_size
        data_size = av_get_bytes_per_sample(avCodecContext->sample_fmt);
        if (data_size < 0) {
            /* This should not occur, checking just for paranoia */
            __android_log_write(ANDROID_LOG_ERROR, TAG, "Failed to calculate data size\n");
            return;
        }

        //遍历avFrame中的每一个采样数据
        for (i = 0; i < avFrame->nb_samples; i++)
            //遍历每一个声道
            for (ch = 0; ch < avCodecContext->channels; ch++)
                //文件中数据的排列格式：采样1声道1 采样1声道2 采样2声道1 采样2声道2...
                fwrite(avFrame->data[ch] + data_size * i, 1, data_size, outfile);
    }
}

/**
 * 根据采样格式获取be le信息
 */
static int get_format_from_sample_fmt(const char **fmt,
                                      enum AVSampleFormat sample_fmt) {
    int i;
    *fmt = nullptr;
    //采样格式与格式字符串的对应关系
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

    //遍历sample_fmt_entries数组
    for (i = 0; i < FF_ARRAY_ELEMS(sample_fmt_entries); i++) {
        struct sample_fmt_entry *entry = &sample_fmt_entries[i];
        if (sample_fmt == entry->sample_fmt) {
            *fmt = AV_NE(entry->fmt_be, entry->fmt_le);
            return 0;
        }
    }

    __android_log_print(ANDROID_LOG_ERROR, TAG,
                        "sample format %s is not supported as output format\n",
                        av_get_sample_fmt_name(sample_fmt));
    return -1;
}

//造采样数据，并编码成mp2文件-----------------------------------------------------------------------
static int check_sample_fmt(const AVCodec *codec, enum AVSampleFormat sample_fmt);

static int select_sample_rate(const AVCodec *codec);

static int select_channel_layout(const AVCodec *codec);

static void encode(AVCodecContext *ctx, AVFrame *frame, AVPacket *pkt, FILE *output);

extern "C"
JNIEXPORT jint JNICALL
Java_com_example_helloffmpeg_MainActivity_encodeAudio(JNIEnv *env, jobject thiz,
                                                      jstring dst_file_path) {
    const char *outfilename = env->GetStringUTFChars(dst_file_path, nullptr);
    __android_log_write(ANDROID_LOG_ERROR, TAG, outfilename);

    const AVCodec *codec = nullptr;
    AVCodecContext *avCodecContext = nullptr;
    AVFrame *frame = nullptr;
    AVPacket *pkt = nullptr;
    int i, j, k, ret;
    FILE *file = nullptr;
    uint16_t *samples = nullptr;
    float t, tincr;

    /* find the MP2 encoder */
    codec = avcodec_find_encoder(AV_CODEC_ID_MP2);
    if (!codec) {
        __android_log_write(ANDROID_LOG_ERROR, TAG, "Codec not found\n");
        goto end;
    }

    avCodecContext = avcodec_alloc_context3(codec);
    if (!avCodecContext) {
        __android_log_write(ANDROID_LOG_ERROR, TAG, "Could not allocate audio codec context\n");
        goto end;
    }

    //编码的比特率
    /* put sample parameters */
    avCodecContext->bit_rate = 64000;

    //编码的采样格式
    //检查encoder是否支持s16 pcm格式
    /* check that the encoder supports s16 pcm input */
    avCodecContext->sample_fmt = AV_SAMPLE_FMT_S16;
    if (!check_sample_fmt(codec, avCodecContext->sample_fmt)) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "Encoder does not support sample format %s",
                            av_get_sample_fmt_name(avCodecContext->sample_fmt));
        goto end;
    }

    /* select other audio parameters supported by the encoder */
    //编码的采样率
    avCodecContext->sample_rate = select_sample_rate(codec);
    //编码的channel_layout
    avCodecContext->channel_layout = select_channel_layout(codec);
    //编码的声道数
    avCodecContext->channels = av_get_channel_layout_nb_channels(avCodecContext->channel_layout);

    /* open it */
    if (avcodec_open2(avCodecContext, codec, nullptr) < 0) {
        __android_log_write(ANDROID_LOG_ERROR, TAG, "Could not open codec\n");
        goto end;
    }

    file = fopen(outfilename, "wbe");
    if (!file) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "Could not open %s\n", outfilename);
        goto end;
    }

    /* packet for holding encoded output */
    pkt = av_packet_alloc();
    if (!pkt) {
        __android_log_write(ANDROID_LOG_ERROR, TAG, "could not allocate the packet\n");
        goto end;
    }

    /* frame containing input raw audio */
    frame = av_frame_alloc();
    if (!frame) {
        __android_log_write(ANDROID_LOG_ERROR, TAG, "Could not allocate audio frame\n");
        goto end;
    }
    frame->nb_samples = avCodecContext->frame_size;
    frame->format = avCodecContext->sample_fmt;
    frame->channel_layout = avCodecContext->channel_layout;
    /* allocate the data buffers */
    ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
        __android_log_write(ANDROID_LOG_ERROR, TAG, "Could not allocate audio data buffers\n");
        goto end;
    }

    //手动生成一些假的采样数据
    /* encode a single tone sound */
    t = 0;
    //t increasement:t的步进
    tincr = 2 * M_PI * 440.0 / avCodecContext->sample_rate;
    //一次循环为一个frame
    for (i = 0; i < 200; i++) {
        /* make sure the frame is writable -- makes a copy if the encoder
         * kept a reference internally */
        ret = av_frame_make_writable(frame);
        if (ret < 0) goto end;
        samples = (uint16_t *) frame->data[0];

        //一次循环为一个采样
        //采用packed存储格式（AV_SAMPLE_FMT_S16）
        //即采样1声道1 采样1声道2 采样2声道1 采样2声道2...
        for (j = 0; j < avCodecContext->frame_size; j++) {
            //第一个声道的数据
            //这里似乎有点问题，只适用于两个声道
            //应该写成：samples[avCodecContext->channels * j] = ...
            samples[2 * j] = (u_int16_t) (sin(t) * 10000);

            //后面的声道的数据
            for (k = 1; k < avCodecContext->channels; k++)
                //这里似乎有点问题，只适用于两个声道
                //应该写成：samples[avCodecContext->channels * j + k] = samples[avCodecContext->channels * j]
                samples[2 * j + k] = samples[2 * j];

            t += tincr;
        }
        encode(avCodecContext, frame, pkt, file);
    }

    /* flush the encoder */
    encode(avCodecContext, nullptr, pkt, file);

    end:
    env->ReleaseStringUTFChars(dst_file_path, outfilename);
    if (file) fclose(file);
    if (frame) av_frame_free(&frame);
    if (pkt) av_packet_free(&pkt);
    if (avCodecContext) avcodec_free_context(&avCodecContext);

    return 0;
}

//检查encoder是否支持某种格式
/* check that a given sample format is supported by the encoder */
static int check_sample_fmt(const AVCodec *codec, enum AVSampleFormat sample_fmt) {
    const enum AVSampleFormat *p = codec->sample_fmts;

    while (*p != AV_SAMPLE_FMT_NONE) {
        if (*p == sample_fmt)
            return 1;
        p++;
    }
    return 0;
}

//找到codec支持的最高的采样率
/* just pick the highest supported samplerate */
static int select_sample_rate(const AVCodec *codec) {
    const int *p;
    int best_samplerate = 0;

    if (!codec->supported_samplerates)
        return 44100;

    p = codec->supported_samplerates;
    while (*p) {
        if (!best_samplerate || abs(44100 - *p) < abs(44100 - best_samplerate))
            best_samplerate = *p;
        p++;
    }
    return best_samplerate;
}

//选择声道数最多的layout
/* select layout with the highest channel count */
static int select_channel_layout(const AVCodec *codec) {
    const uint64_t *p;
    uint64_t best_ch_layout = 0;
    int best_nb_channels = 0;

    if (!codec->channel_layouts)
        return AV_CH_LAYOUT_STEREO;

    p = codec->channel_layouts;
    while (*p) {
        int nb_channels = av_get_channel_layout_nb_channels(*p);

        if (nb_channels > best_nb_channels) {
            best_ch_layout = *p;
            best_nb_channels = nb_channels;
        }
        p++;
    }
    return best_ch_layout;
}

/**
 * 将frame编码为packet，并写入文件
 */
static void encode(AVCodecContext *ctx, AVFrame *frame, AVPacket *pkt,
                   FILE *output) {
    int ret;

    /* send the frame for encoding */
    ret = avcodec_send_frame(ctx, frame);
    if (ret < 0) {
        __android_log_write(ANDROID_LOG_ERROR, TAG, "Error sending the frame to the encoder\n");
        return;
    }

    /* read all the available output packets (in general there may be any
     * number of them */
    while (ret >= 0) {
        ret = avcodec_receive_packet(ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0) {
            __android_log_write(ANDROID_LOG_ERROR, TAG, "Error encoding audio frame\n");
            return;
        }

        fwrite(pkt->data, 1, pkt->size, output);
        //clear packet
        av_packet_unref(pkt);
    }
}
