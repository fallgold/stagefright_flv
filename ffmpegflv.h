#ifndef AVFORMAT_FLV_D_H
#define AVFORMAT_FLV_D_H

#ifdef __cplusplus
//shared lib
//#define LIBFLV_API extern "C" __attribute__((visibility("default")))
#define LIBFLV_API extern "C" 
#else
#define LIBFLV_API 
#endif

#include "libavformat/avformat.h"

LIBFLV_API int ffmpeg_avformat_open_input(AVFormatContext **ps, AVIOContext * pPb, const char *filename, AVInputFormat *fmt, AVDictionary **options);
LIBFLV_API void ffmpeg_avformat_close_input(AVFormatContext *s, AVPacketList **ahead_pkt_list);
LIBFLV_API int ffmpeg_flv_probe(AVProbeData *p);
LIBFLV_API int ffmpeg_avformat_find_stream_info(AVFormatContext *ic, AVDictionary **options);
LIBFLV_API int ffmpeg_av_read_frame(AVFormatContext *s, AVPacket *pkt, int stream_idx, AVPacketList **ahead_pkt_list);
LIBFLV_API AVIOContext *ffmpeg_avio_alloc_context(
                  unsigned char *buffer,
                  int buffer_size,
                  int write_flag,
                  void *opaque,
                  int (*read_packet)(void *opaque, uint8_t *buf, int buf_size),
                  int (*write_packet)(void *opaque, uint8_t *buf, int buf_size),
                  int64_t (*seek)(void *opaque, int64_t offset, int whence));

#endif /* AVFORMAT_FLV_H */
