#include<android/log.h>
#include <jni.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include <libavutil/log.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
}

extern const char *TAG;


//解析h264文件，将其中的图像帧保存为yuv文件----------------------------------------------------------------
//ffmpeg可以有多种途径实现同一目标，例如，这里是通过fread直接读文件，并使用av_parser_parse2来得到packet
//而另一种选择是通过AVFormatContext来加载文件，并通过av_read_frame来得到packet

#define INBUF_SIZE 4096

static void yuv_save(AVFrame *avFrame, char *filename);

static void decode(AVCodecContext *avCodecContext, AVFrame *avFrame, AVPacket *pkt,
                   const char *filename);

extern "C"
JNIEXPORT jint JNICALL
Java_com_example_helloffmpeg_MainActivity_decodeVideo(JNIEnv *env, jobject thiz,
                                                      jstring file_path, jstring dst_file_path) {
    const char *filename = env->GetStringUTFChars(file_path, nullptr);
    const char *outfilename = env->GetStringUTFChars(dst_file_path, nullptr);
    __android_log_write(ANDROID_LOG_ERROR, TAG, filename);
    __android_log_write(ANDROID_LOG_ERROR, TAG, outfilename);

    const AVCodec *avCodec = nullptr;
    AVCodecParserContext *avCodecParserContext = nullptr;
    AVCodecContext *avCodecContext = nullptr;
    FILE *file = nullptr;
    AVFrame *avFrame = nullptr;
    uint8_t inbuf[INBUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
    uint8_t *data = nullptr;
    size_t data_size;
    int ret;
    AVPacket *avPacket = nullptr;

    avPacket = av_packet_alloc();
    if (!avPacket)
        goto end;

    //原型：memset(void *buffer, int c, int count)
    //将inbuf[INBUF_SIZE]及其后面的元素都设为了0
    /* set end of buffer to 0 (this ensures that no overreading happens for damaged MPEG streams) */
    memset(inbuf + INBUF_SIZE, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    //找到h264的解码器
    avCodec = avcodec_find_decoder(AV_CODEC_ID_H264);
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

    /* For some codecs, such as msmpeg4 and mpeg4, width and height
       MUST be initialized there because this information is not
       available in the bitstream. */

    /* open it */
    if (avcodec_open2(avCodecContext, avCodec, nullptr) < 0) {
        __android_log_write(ANDROID_LOG_ERROR, TAG, "Could not open avCodec\n");
        goto end;
    }

    //打开输入文件
    file = fopen(filename, "rbe");
    if (!file) {
        __android_log_write(ANDROID_LOG_ERROR, TAG, "Could not open in file\n");
        __android_log_write(ANDROID_LOG_ERROR, TAG, strerror(errno));
        goto end;
    }

    avFrame = av_frame_alloc();
    if (!avFrame) {
        __android_log_write(ANDROID_LOG_ERROR, TAG, "Could not allocate video avFrame\n");
        goto end;
    }

    while (!feof(file)) {
        /* read raw data from the input file */
        data_size = fread(inbuf, 1, INBUF_SIZE, file);
        if (!data_size)
            break;

        /* use the avCodecParserContext to split the data into frames */
        data = inbuf;
        while (data_size > 0) {
            //从data中解析出avPacket数据
            ret = av_parser_parse2(avCodecParserContext, avCodecContext, &avPacket->data,
                                   &avPacket->size,
                                   data, data_size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
            if (ret < 0) {
                __android_log_write(ANDROID_LOG_ERROR, TAG, "Error while parsing\n");
                goto end;
            }
            //后移数组指针并更新data_size
            data += ret;
            data_size -= ret;

            //解码avPacket
            if (avPacket->size)
                decode(avCodecContext, avFrame, avPacket, outfilename);
        }
    }

    /* flush the decoder */
    decode(avCodecContext, avFrame, nullptr, outfilename);

    end:
    env->ReleaseStringUTFChars(file_path, filename);
    env->ReleaseStringUTFChars(dst_file_path, outfilename);
    if (file) fclose(file);
    if (avCodecParserContext) av_parser_close(avCodecParserContext);
    if (avCodecContext) avcodec_free_context(&avCodecContext);
    if (avFrame) av_frame_free(&avFrame);
    if (avPacket) av_packet_free(&avPacket);

    return 0;
}

//从packet中解码出frame
static void decode(AVCodecContext *avCodecContext, AVFrame *avFrame, AVPacket *pkt,
                   const char *filename) {
    char buf[1024];
    int ret;

    //将packet发送给codec
    ret = avcodec_send_packet(avCodecContext, pkt);
    if (ret < 0) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "Error sending a packet for decoding: %s\n",
                            av_err2str(ret));
        return;
    }

    while (ret >= 0) {
        //解码出frame并存入avFrame参数
        ret = avcodec_receive_frame(avCodecContext, avFrame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return;
        } else if (ret < 0) {
            __android_log_write(ANDROID_LOG_ERROR, TAG, "Error during decoding\n");
            return;
        }

        //为防止文件太多观察不便，每20个avFrame中抽取一个并保存为文件
        if (avCodecContext->frame_number % 20 == 0) {
            __android_log_print(ANDROID_LOG_ERROR, TAG, "saving avFrame %3d\n",
                                avCodecContext->frame_number);

            /* the picture is allocated by the decoder. no need to
               free it */
            //拼接文件名
            //C库函数:int snprintf(char *str, size_t size, const char *format, ...),将可变参数(...)按照format格式化成字符串，
            //并将字符串复制到str中，size为要写入的字符的最大数目，超过size会被截断。
            snprintf(buf, sizeof(buf), "%s-%d.yuv", filename, avCodecContext->frame_number);
            yuv_save(avFrame, buf);
        }
    }
}

//将avFrame保存为yuv文件
static void yuv_save(AVFrame *avFrame, char *filename) {
    FILE *file;

    file = fopen(filename, "we");
    if (!file) {
        __android_log_write(ANDROID_LOG_ERROR, TAG, "Could not open out file\n");
        __android_log_write(ANDROID_LOG_ERROR, TAG, strerror(errno));
        return;
    }

    //这段代码的原理参考《YUV与FFmpeg.md》
    int width = avFrame->width;
    int height = avFrame->height;
    for (int i = 0; i < height; i++)
        fwrite(avFrame->data[0] + i * avFrame->linesize[0], 1, width, file);
    for (int j = 0; j < height / 2; j++)
        fwrite(avFrame->data[1] + j * avFrame->linesize[1], 1, width / 2, file);
    for (int k = 0; k < height / 2; k++)
        fwrite(avFrame->data[2] + k * avFrame->linesize[2], 1, width / 2, file);

    fclose(file);
}

//造yuv数据，并编码为h264文件--------------------------------------------------------------------------------------------
static void encode(AVCodecContext *avCodecContext, AVFrame *frame, AVPacket *pkt, FILE *outfile);

extern "C"
JNIEXPORT jint JNICALL
Java_com_example_helloffmpeg_MainActivity_encodeVideo(JNIEnv *env, jobject thiz,
                                                      jstring dst_file_path) {
    const char *outfilename = env->GetStringUTFChars(dst_file_path, nullptr);
    __android_log_write(ANDROID_LOG_ERROR, TAG, outfilename);

    const AVCodec *codec = nullptr;
    AVCodecContext *avCodecContext = nullptr;
    int i, ret, x, y;
    FILE *file = nullptr;
    AVFrame *frame = nullptr;
    AVPacket *pkt = nullptr;
    uint8_t endcode[] = {0, 0, 1, 0xb7};

    //codec = avcodec_find_encoder_by_name(codec_name);
    //codec = avcodec_find_encoder(AV_CODEC_ID_MPEG1VIDEO);
    //codec = avcodec_find_encoder(AV_CODEC_ID_MPEG2VIDEO);
    //库里面似乎没有包含h264的编码器，会报找不到错误。
    //ffmpeg中默认不含h264的编码器，如果要使用h264编码器，需要
    //在编译ffmpeg时将h264编码器添加进来。参考《《音视频开发进阶指南》笔记.md》
    codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        __android_log_write(ANDROID_LOG_ERROR, TAG, "Codec not found\n");
        goto end;
    }

    avCodecContext = avcodec_alloc_context3(codec);
    if (!avCodecContext) {
        __android_log_write(ANDROID_LOG_ERROR, TAG, "Could not allocate video codec context\n");
        goto end;
    }

    pkt = av_packet_alloc();
    if (!pkt) {
        __android_log_write(ANDROID_LOG_ERROR, TAG, "av_packet_alloc() failed\n");
        goto end;
    }

    /* put sample parameters */
    avCodecContext->bit_rate = 400000;
    /* resolution must be a multiple of two */
    avCodecContext->width = 352;
    avCodecContext->height = 288;
    //AVRational表示一个分数
    //时间基，(1/25)秒
    avCodecContext->time_base = (AVRational) {1, 25};
    //帧率，25帧/秒
    avCodecContext->framerate = (AVRational) {25, 1};

    //每10帧产生一个I帧。
    //在将帧发送给编码器之前检查帧的pict_type，如果是AV_PICTURE_TYPE_I，
    //那么gop_size会被忽略，输出的帧必然是I帧
    /* emit one intra frame every ten frames
     * check frame pict_type before passing frame
     * to encoder, if frame->pict_type is AV_PICTURE_TYPE_I
     * then gop_size is ignored and the output of encoder
     * will always be I frame irrespective to gop_size
     */
    avCodecContext->gop_size = 10;
    avCodecContext->max_b_frames = 1;
    avCodecContext->pix_fmt = AV_PIX_FMT_YUV420P;

    if (codec->id == AV_CODEC_ID_H264) {
        av_opt_set(avCodecContext->priv_data, "preset", "slow", 0);
    }

    /* open it */
    ret = avcodec_open2(avCodecContext, codec, nullptr);
    if (ret < 0) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "Could not open codec: %s\n", av_err2str(ret));
        goto end;
    }

    file = fopen(outfilename, "wbe");
    if (!file) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "Could not open %s\n", outfilename);
        goto end;
    }

    frame = av_frame_alloc();
    if (!frame) {
        __android_log_write(ANDROID_LOG_ERROR, TAG, "Could not allocate video frame\n");
        goto end;
    }
    frame->format = avCodecContext->pix_fmt;
    frame->width = avCodecContext->width;
    frame->height = avCodecContext->height;

    ret = av_frame_get_buffer(frame, 32);
    if (ret < 0) {
        __android_log_write(ANDROID_LOG_ERROR, TAG, "Could not allocate the video frame data\n");
        goto end;
    }

    /* encode 1 second of video */
    //每次循环是一帧。共25帧，对于帧率25，就是1秒的画面。
    for (i = 0; i < 25; i++) {
        //输出并清空文件缓冲区
        fflush(stdout);

        /* make sure the frame data is writable */
        ret = av_frame_make_writable(frame);
        if (ret < 0) goto end;

        //造数据
        /* prepare a dummy image */
        /* Y */
        for (y = 0; y < avCodecContext->height; y++) {
            for (x = 0; x < avCodecContext->width; x++) {
                frame->data[0][y * frame->linesize[0] + x] = x + y + i * 3;
            }
        }
        /* Cb and Cr */
        for (y = 0; y < avCodecContext->height / 2; y++) {
            for (x = 0; x < avCodecContext->width / 2; x++) {
                frame->data[1][y * frame->linesize[1] + x] = 128 + y + i * 2;
                frame->data[2][y * frame->linesize[2] + x] = 64 + x + i * 5;
            }
        }

        //该帧的展示时间戳，以时间基为单位
        frame->pts = i;

        /* encode the image */
        encode(avCodecContext, frame, pkt, file);
    }

    /* flush the encoder */
    encode(avCodecContext, nullptr, pkt, file);

    //如果编码格式是mpeg1或mpeg2,那么要加上文件尾
    //h264不用加
    /* add sequence end code to have a real MPEG file */
    if (codec->id == AV_CODEC_ID_MPEG1VIDEO || codec->id == AV_CODEC_ID_MPEG2VIDEO)
        fwrite(endcode, 1, sizeof(endcode), file);

    end:
    env->ReleaseStringUTFChars(dst_file_path, outfilename);
    if (file) fclose(file);
    if (avCodecContext) avcodec_free_context(&avCodecContext);
    if (frame) av_frame_free(&frame);
    if (pkt) av_packet_free(&pkt);

    return 0;
}

static void encode(AVCodecContext *avCodecContext, AVFrame *frame, AVPacket *pkt, FILE *outfile) {
    int ret;

    if (frame) __android_log_print(ANDROID_LOG_ERROR, TAG, "Send frame %lli\n", frame->pts);

    /* send the frame to the encoder */
    ret = avcodec_send_frame(avCodecContext, frame);
    if (ret < 0) {
        __android_log_write(ANDROID_LOG_ERROR, TAG, "Error sending a frame for encoding\n");
        return;
    }

    while (ret >= 0) {
        ret = avcodec_receive_packet(avCodecContext, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0) {
            __android_log_write(ANDROID_LOG_ERROR, TAG, "Error during encoding\n");
            return;
        }

        __android_log_print(ANDROID_LOG_ERROR, TAG, "Write packet %lli (size=%5d)\n",
                            pkt->pts, pkt->size);
        fwrite(pkt->data, 1, pkt->size, outfile);
        av_packet_unref(pkt);
    }
}
