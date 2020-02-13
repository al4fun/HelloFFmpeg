#include<android/log.h>
#include <jni.h>
#include <string>

extern "C" {
#include <libavutil/log.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavcodec/avcodec.h>
#include <libavutil/timestamp.h>
}

const char *TAG = "TAG";

extern "C"
JNIEXPORT jstring JNICALL
Java_com_example_helloffmpeg_MainActivity_stringFromJNI(JNIEnv *env, jobject thiz) {
    std::string hello = "Hello from C++!";
    return env->NewStringUTF(hello.c_str());
}

//打印FFmpeg版本--------------------------------------------------------------------------------------
extern "C"
JNIEXPORT void JNICALL
Java_com_example_helloffmpeg_MainActivity_helloFFmpeg(JNIEnv *env, jobject thiz) {
    __android_log_print(ANDROID_LOG_ERROR, TAG, "current FFmpeg version: %s",
                        av_version_info());
}

//打印媒体文件信息--------------------------------------------------------------------------------------
extern "C"
JNIEXPORT void JNICALL
Java_com_example_helloffmpeg_MainActivity_printFileInfo(JNIEnv *env, jobject thiz,
                                                        jstring file_path) {
    const char *path = env->GetStringUTFChars(file_path, nullptr);
    __android_log_write(ANDROID_LOG_ERROR, TAG, path);

    AVFormatContext *fmt_ctx = nullptr;
    AVDictionaryEntry *dictionaryEntry = nullptr;
    int ret;

    //注册
    //此方法已被移除，没有替代方法
    //已经不需要调用此方法
    //av_register_all();

    //创建AVFormatContext
    if ((ret = avformat_open_input(&fmt_ctx, path, nullptr, nullptr)) < 0) {
        __android_log_write(ANDROID_LOG_ERROR, TAG, av_err2str(ret));
        goto end;
    }

    //找到流信息
    if ((ret = avformat_find_stream_info(fmt_ctx, nullptr)) < 0) {
        __android_log_write(ANDROID_LOG_ERROR, TAG, av_err2str(ret));
        goto end;
    }

    //解析metadata
    while ((dictionaryEntry = av_dict_get(fmt_ctx->metadata, "", dictionaryEntry,
                                          AV_DICT_IGNORE_SUFFIX))) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "%s=%s\n", dictionaryEntry->key,
                            dictionaryEntry->value);
    }

    //释放资源
    end:
    env->ReleaseStringUTFChars(file_path, path);
    if (fmt_ctx) avformat_close_input(&fmt_ctx);
}

//从mp4文件中提取音频流并保存为aac文件--------------------------------------------------------------------
extern "C"
JNIEXPORT void JNICALL
Java_com_example_helloffmpeg_MainActivity_extractAudio(JNIEnv *env, jobject thiz,
                                                       jstring src_path, jstring dst_path) {
    int ret;
    AVFormatContext *in_fmt_ctx = nullptr;
    int audio_index;
    AVStream *in_stream = nullptr;
    AVCodecParameters *in_codecpar = nullptr;
    AVFormatContext *out_fmt_ctx = nullptr;
    AVOutputFormat *out_fmt = nullptr;
    AVStream *out_stream = nullptr;
    AVPacket pkt;

    const char *srcPath = env->GetStringUTFChars(src_path, nullptr);
    const char *dstPath = env->GetStringUTFChars(dst_path, nullptr);
    __android_log_write(ANDROID_LOG_ERROR, TAG, srcPath);
    __android_log_write(ANDROID_LOG_ERROR, TAG, dstPath);

    //in_fmt_ctx
    ret = avformat_open_input(&in_fmt_ctx, srcPath, nullptr, nullptr);
    if (ret < 0) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "avformat_open_input失败：%s",
                            av_err2str(ret));
        goto end;
    }

    //audio_index
    audio_index = av_find_best_stream(in_fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audio_index < 0) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "查找音频流失败：%s",
                            av_err2str(audio_index));
        goto end;
    }

    //in_stream、in_codecpar
    in_stream = in_fmt_ctx->streams[audio_index];
    in_codecpar = in_stream->codecpar;
    if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "The Codec type is invalid!");
        goto end;
    }

    //out_fmt_ctx
    out_fmt_ctx = avformat_alloc_context();
    out_fmt = av_guess_format(NULL, dstPath, NULL);
    out_fmt_ctx->oformat = out_fmt;
    if (!out_fmt) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "Cloud not guess file format");
        goto end;
    }

    //out_stream
    out_stream = avformat_new_stream(out_fmt_ctx, NULL);
    if (!out_stream) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "Failed to create out stream");
        goto end;
    }

    //拷贝编解码器参数
    ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
    if (ret < 0) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "avcodec_parameters_copy：%s",
                            av_err2str(ret));
        goto end;
    }
    out_stream->codecpar->codec_tag = 0;


    //创建并初始化目标文件的AVIOContext
    if ((ret = avio_open(&out_fmt_ctx->pb, dstPath, AVIO_FLAG_WRITE)) < 0) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "avio_open：%s",
                            av_err2str(ret));
        goto end;
    }

    //initialize packet
    av_init_packet(&pkt);
    pkt.data = nullptr;
    pkt.size = 0;

    //写文件头
    if ((ret = avformat_write_header(out_fmt_ctx, nullptr)) < 0) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "avformat_write_header：%s",
                            av_err2str(ret));
        goto end;
    }

    while (av_read_frame(in_fmt_ctx, &pkt) == 0) {
        if (pkt.stream_index == audio_index) {
            //输入流和输出流的时间基可能不同，因此要根据时间基的不同对时间戳pts进行转换
            pkt.pts = av_rescale_q(pkt.pts, in_stream->time_base, out_stream->time_base);
            pkt.dts = pkt.pts;
            //根据时间基转换duration
            pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
            pkt.pos = -1;
            pkt.stream_index = 0;

            //写入
            av_interleaved_write_frame(out_fmt_ctx, &pkt);

            //释放packet
            av_packet_unref(&pkt);
        }
    }

    //写文件尾
    av_write_trailer(out_fmt_ctx);

    //释放资源
    end:
    env->ReleaseStringUTFChars(src_path, srcPath);
    env->ReleaseStringUTFChars(dst_path, dstPath);
    if (in_fmt_ctx) avformat_close_input(&in_fmt_ctx);
    if (out_fmt_ctx) {
        if (out_fmt_ctx->pb) avio_close(out_fmt_ctx->pb);
        avformat_free_context(out_fmt_ctx);
    }
}

//从mp4文件中提取视频流并保存为h264文件----------------------------------------------------------------
extern "C"
JNIEXPORT void JNICALL
Java_com_example_helloffmpeg_MainActivity_extractVideo(JNIEnv *env, jobject thiz,
                                                       jstring src_path, jstring dst_path) {
    int ret;
    AVFormatContext *in_fmt_ctx = nullptr;
    int video_index;
    AVStream *in_stream = nullptr;
    AVCodecParameters *in_codecpar = nullptr;
    AVFormatContext *out_fmt_ctx = nullptr;
    AVOutputFormat *out_fmt = nullptr;
    AVStream *out_stream = nullptr;
    AVPacket pkt;

    const char *srcPath = env->GetStringUTFChars(src_path, nullptr);
    const char *dstPath = env->GetStringUTFChars(dst_path, nullptr);
    __android_log_write(ANDROID_LOG_ERROR, TAG, srcPath);
    __android_log_write(ANDROID_LOG_ERROR, TAG, dstPath);

    //in_fmt_ctx
    ret = avformat_open_input(&in_fmt_ctx, srcPath, nullptr, nullptr);
    if (ret < 0) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "avformat_open_input失败：%s",
                            av_err2str(ret));
        goto end;
    }

    //video_index
    video_index = av_find_best_stream(in_fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_index < 0) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "查找视频流失败：%s",
                            av_err2str(video_index));
        goto end;
    }

    //in_stream、in_codecpar
    in_stream = in_fmt_ctx->streams[video_index];
    in_codecpar = in_stream->codecpar;
    if (in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "The Codec type is invalid!");
        goto end;
    }

    //out_fmt_ctx
    out_fmt_ctx = avformat_alloc_context();
    out_fmt = av_guess_format(NULL, dstPath, NULL);
    out_fmt_ctx->oformat = out_fmt;
    if (!out_fmt) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "Cloud not guess file format");
        goto end;
    }

    //out_stream
    out_stream = avformat_new_stream(out_fmt_ctx, NULL);
    if (!out_stream) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "Failed to create out stream");
        goto end;
    }

    //拷贝编解码器参数
    ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
    if (ret < 0) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "avcodec_parameters_copy：%s",
                            av_err2str(ret));
        goto end;
    }
    out_stream->codecpar->codec_tag = 0;


    //创建并初始化目标文件的AVIOContext
    if ((ret = avio_open(&out_fmt_ctx->pb, dstPath, AVIO_FLAG_WRITE)) < 0) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "avio_open：%s",
                            av_err2str(ret));
        goto end;
    }

    //initialize packet
    av_init_packet(&pkt);
    pkt.data = nullptr;
    pkt.size = 0;

    //写文件头
    if ((ret = avformat_write_header(out_fmt_ctx, nullptr)) < 0) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "avformat_write_header：%s",
                            av_err2str(ret));
        goto end;
    }

    while (av_read_frame(in_fmt_ctx, &pkt) == 0) {
        if (pkt.stream_index == video_index) {
            //输入流和输出流的时间基可能不同，因此要根据时间基的不同对时间戳pts进行转换
            pkt.pts = av_rescale_q(pkt.pts, in_stream->time_base, out_stream->time_base);
            pkt.dts = pkt.pts;
            //根据时间基转换duration
            pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
            pkt.pos = -1;
            pkt.stream_index = 0;

            //写入
            av_interleaved_write_frame(out_fmt_ctx, &pkt);

            //释放packet
            av_packet_unref(&pkt);
        }
    }

    //写文件尾
    av_write_trailer(out_fmt_ctx);

    //释放资源
    end:
    env->ReleaseStringUTFChars(src_path, srcPath);
    env->ReleaseStringUTFChars(dst_path, dstPath);
    if (in_fmt_ctx) avformat_close_input(&in_fmt_ctx);
    if (out_fmt_ctx) {
        if (out_fmt_ctx->pb) avio_close(out_fmt_ctx->pb);
        avformat_free_context(out_fmt_ctx);
    }
}

//改变封装格式(mp4 -> flv)----------------------------------------------------------------------------------
//流程与extractAudio类似，可参考对比
static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt, const char *tag);

extern "C"
JNIEXPORT int JNICALL
Java_com_example_helloffmpeg_MainActivity_remux(JNIEnv *env, jobject thiz, jstring file_path,
                                                jstring dst_file_path) {
    const char *in_filename = env->GetStringUTFChars(file_path, nullptr);
    const char *out_filename = env->GetStringUTFChars(dst_file_path, nullptr);
    __android_log_write(ANDROID_LOG_ERROR, TAG, in_filename);
    __android_log_write(ANDROID_LOG_ERROR, TAG, out_filename);

    AVOutputFormat *ofmt = nullptr;
    AVFormatContext *ifmt_ctx = nullptr, *ofmt_ctx = nullptr;
    AVPacket pkt;
    int ret, i;
    int stream_index = 0;
    int *stream_mapping = nullptr;
    int stream_mapping_size = 0;

    //ifmt_ctx
    if ((ret = avformat_open_input(&ifmt_ctx, in_filename, 0, 0)) < 0) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "Could not open input file '%s'", in_filename);
        goto end;
    }

    if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0) {
        __android_log_write(ANDROID_LOG_ERROR, TAG, "Failed to retrieve input stream information");
        goto end;
    }
    av_dump_format(ifmt_ctx, 0, in_filename, 0);

    //ofmt_ctx、ofmt
    //获得ofmt_ctx和ofmt的另一种方法，参考extractAudio函数
    avformat_alloc_output_context2(&ofmt_ctx, nullptr, nullptr, out_filename);
    if (!ofmt_ctx) {
        __android_log_write(ANDROID_LOG_ERROR, TAG, "Could not create output context\n");
        ret = AVERROR_UNKNOWN;
        goto end;
    }
    ofmt = ofmt_ctx->oformat;

    stream_mapping_size = ifmt_ctx->nb_streams;
    stream_mapping = (int *) av_mallocz_array(stream_mapping_size, sizeof(*stream_mapping));
    if (!stream_mapping) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    //遍历每一路流
    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        AVStream *out_stream;
        AVStream *in_stream = ifmt_ctx->streams[i];
        AVCodecParameters *in_codecpar = in_stream->codecpar;

        //输出文件中只保留了音频流、视频流和字幕流
        if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
            stream_mapping[i] = -1;
            continue;
        }

        //角标是此路流在输入文件中的索引
        //值是此路流在输出文件中的索引
        stream_mapping[i] = stream_index++;

        out_stream = avformat_new_stream(ofmt_ctx, nullptr);
        if (!out_stream) {
            __android_log_write(ANDROID_LOG_ERROR, TAG, "Failed allocating output stream\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        //拷贝流的codec参数
        ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
        if (ret < 0) {
            __android_log_write(ANDROID_LOG_ERROR, TAG, "Failed to copy codec parameters\n");
            goto end;
        }
        out_stream->codecpar->codec_tag = 0;
    }
    av_dump_format(ofmt_ctx, 0, out_filename, 1);

    if (!(ofmt->flags & AVFMT_NOFILE)) {
        //创建并初始化AVIOContext
        ret = avio_open(&ofmt_ctx->pb, out_filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            __android_log_print(ANDROID_LOG_ERROR, TAG, "Could not open output file '%s'",
                                out_filename);
            goto end;
        }
    }

    //写媒体文件头信息
    ret = avformat_write_header(ofmt_ctx, nullptr);
    if (ret < 0) {
        __android_log_write(ANDROID_LOG_ERROR, TAG, "Error occurred when opening output file\n");
        goto end;
    }

    while (1) {
        AVStream *in_stream, *out_stream;

        //读取packet
        ret = av_read_frame(ifmt_ctx, &pkt);
        if (ret < 0) break;

        in_stream = ifmt_ctx->streams[pkt.stream_index];
        //无效的流 或 不是我们想要的流
        if (pkt.stream_index >= stream_mapping_size ||
            stream_mapping[pkt.stream_index] < 0) {
            av_packet_unref(&pkt);
            continue;
        }

        pkt.stream_index = stream_mapping[pkt.stream_index];
        out_stream = ofmt_ctx->streams[pkt.stream_index];
        log_packet(ifmt_ctx, &pkt, "in");

        //输入流和输出流的时间基可能不同，因此要根据时间基的不同对时间戳pts进行转换
        pkt.pts = av_rescale_q(pkt.pts, in_stream->time_base, out_stream->time_base);
        pkt.dts = av_rescale_q(pkt.dts, in_stream->time_base, out_stream->time_base);
        //根据时间基转换duration
        pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
        pkt.pos = -1;
        log_packet(ofmt_ctx, &pkt, "out");

        //写入packet数据
        ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
        if (ret < 0) {
            __android_log_write(ANDROID_LOG_ERROR, TAG, "Error muxing packet\n");

            break;
        }
        av_packet_unref(&pkt);
    }

    //写媒体文件尾信息
    av_write_trailer(ofmt_ctx);

    end:
    env->ReleaseStringUTFChars(file_path, in_filename);
    env->ReleaseStringUTFChars(dst_file_path, out_filename);
    if (ifmt_ctx) avformat_close_input(&ifmt_ctx);
    if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE)) avio_closep(&ofmt_ctx->pb);
    if (ofmt_ctx) avformat_free_context(ofmt_ctx);
    if (stream_mapping) av_freep(&stream_mapping);
    if (ret < 0 && ret != AVERROR_EOF) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "Error occurred: %s\n", av_err2str(ret));
        return 1;
    }

    return 0;
}

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt, const char *tag) {
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

    printf("%s: pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
           tag,
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
           av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
           pkt->stream_index);
}
