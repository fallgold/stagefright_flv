#include "ffmpegflv.h"
#include "libavformat/url.h"

extern AVInputFormat ff_flv_demuxer;

int ffmpeg_flv_probe(AVProbeData *p) {
	return ff_flv_demuxer.read_probe(p);
}

int ffmpeg_avformat_open_input(AVFormatContext **ps, AVIOContext * pPb, const char *filename, AVInputFormat *fmt, AVDictionary **options) {
	static int ffmpeg_init = 0;
	if (!ffmpeg_init) {
		/*REGISTER_PROTOCOL (FILE, file);*/
		extern URLProtocol ff_file_protocol;
		if(CONFIG_FILE_PROTOCOL) {
			ffurl_register_protocol(&ff_file_protocol, sizeof(ff_file_protocol));
		}
		/*REGISTER_DECODER (H264, h264);*/
		extern AVCodec ff_h264_decoder;;
		if(CONFIG_H264_DECODER) {
			avcodec_register(&ff_h264_decoder); 
		}
		ffmpeg_init = 1;
	}
    *ps = avformat_alloc_context();
	(*ps)->pb = pPb;
    return avformat_open_input(ps, filename, (fmt ? fmt : &ff_flv_demuxer), options);
}

AVIOContext *ffmpeg_avio_alloc_context(
                  unsigned char *buffer,
                  int buffer_size,
                  int write_flag,
                  void *opaque,
                  int (*read_packet)(void *opaque, uint8_t *buf, int buf_size),
                  int (*write_packet)(void *opaque, uint8_t *buf, int buf_size),
                  int64_t (*seek)(void *opaque, int64_t offset, int whence)) {
    return avio_alloc_context(buffer, buffer_size, write_flag, opaque, read_packet, write_packet, seek);
}

void ffmpeg_avformat_close_input(AVFormatContext *s, AVPacketList **ahead_pkt_list) {
	if (s->pb) {
		av_free(s->pb);
		s->pb = 0;
	}
	avformat_close_input(&s);
	for (int i = 0; i < 4; i++) {
		AVPacketList *p = ahead_pkt_list[i];
		while(p) {
			av_free(p);
			p = p->next;
		}
		ahead_pkt_list[i] = 0;
	}
}

int ffmpeg_avformat_find_stream_info(AVFormatContext *ic, AVDictionary **options) {
	return avformat_find_stream_info(ic, options);
}

int ffmpeg_av_read_frame(AVFormatContext *ic, AVPacket *pkt, int stream_idx, AVPacketList **ahead_pkt_list) {
	if (stream_idx >= 4)
		return -3;
	if (ahead_pkt_list[stream_idx]) {
		*pkt = ahead_pkt_list[stream_idx]->pkt;
		ahead_pkt_list[stream_idx] = ahead_pkt_list[stream_idx]->next;
		return 0;
	}
	int ret = av_read_frame(ic, pkt);
	if (ret < 0) {
		return ret;
	}
	int64_t start_time = AV_NOPTS_VALUE;
	int64_t duration = AV_NOPTS_VALUE;
	/* check if packet is in play range specified by user, then queue, otherwise discard */
	int pkt_in_play_range = duration == AV_NOPTS_VALUE ||
			(pkt->pts - ic->streams[pkt->stream_index]->start_time) *
			av_q2d(ic->streams[pkt->stream_index]->time_base) -
			(double)(start_time != AV_NOPTS_VALUE ? start_time : 0)/1000000
			<= ((double)duration/1000000);
	if (!pkt_in_play_range) {
		return -1;
	}
	if (pkt->stream_index == stream_idx) {
		return 0;
	} else {
		AVPacketList *pkl = av_malloc(sizeof(AVPacketList));
		pkl->pkt = *pkt;
		pkl->next = NULL;
		int idx = pkt->stream_index;
		if (!ahead_pkt_list[idx])
			ahead_pkt_list[idx] = pkl;
		else {
			AVPacketList *tmp = ahead_pkt_list[idx];
			while(tmp->next)
				tmp = tmp->next;
			tmp->next = pkl;
		}
		return -2;
	}
}
