/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "FLVExtractor"
#include <utils/Log.h>

#include "include/FLVExtractor.h"

#include <media/stagefright/FileSource.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/MediaBufferGroup.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/MediaSource.h>
#include <media/stagefright/MediaBuffer.h>

#define CHANNEL_MASK_USE_CHANNEL_ORDER 0

namespace android 
{

// IO wrapper for libavformat
class DataSourceWrapper {
	sp<DataSource> mDataSource;
	int mOffset;

public:
	DataSourceWrapper (const sp<DataSource> &dataSource) 
		: mDataSource(dataSource), 
		mOffset(0) {
	}
	virtual ~DataSourceWrapper() {
	}
    virtual ssize_t readAt(void *buf, size_t buf_size) {
		int ret = mDataSource->readAt(mOffset, buf, buf_size);
		if (ret >= 0)
			mOffset += ret;
		return ret;
	}
    virtual void seek(int offset) {
		mOffset = offset;
	}

public:
	static int IORead(void *opaque, uint8_t *buf, int bufSize) {
		if( bufSize < 0 ) 
			return -1;
		DataSourceWrapper *me = (DataSourceWrapper *) opaque;
		int ret = me->readAt(buf, bufSize);
		return ret ? ret : -1;
	}

	// TODO
	static int64_t IOSeek( void *opaque, int64_t offset, int whence ) {
		return -1;
	}
};

class FLVSource : public MediaSource {

	public:
		FLVSource(
				const sp<DataSource> &dataSource,
				const sp<MetaData> &trackMetadata,
				AVFormatContext *ic,
				AVPacketList **aheadPacketList,
				int streamIdx);

		virtual status_t start(MetaData *params);
		virtual status_t stop();
		virtual sp<MetaData> getFormat();

		virtual status_t read(
				MediaBuffer **buffer,
				const ReadOptions *options = NULL);

	protected:
		virtual ~FLVSource();

	private:
		sp<DataSource> mDataSource;
		sp<MetaData> mTrackMetadata;
		int32_t mSampleRate;
		int32_t mNumChannels;
		int32_t mBitsPerSample;
		int32_t mBlockSize;
		size_t mSize;
		MediaBufferGroup *mGroup;
		bool mInitCheck;
		bool mStarted;

		AVFormatContext *mIc;
		AVPacketList **mAheadPacketList;
		int mStreamIdx;

		status_t init();

		FLVSource(const FLVSource&);
		FLVSource &operator=(const FLVSource &);
};

FLVSource::FLVSource(
		const sp<DataSource> &dataSource,
		const sp<MetaData> &trackMetadata,
		AVFormatContext *ic,
		AVPacketList **aheadPacketList,
		int streamIdx)
	: mDataSource(dataSource),
	  mTrackMetadata(trackMetadata),
	  mInitCheck(false),
	  mStarted(false),
	  mIc(ic),
	  mAheadPacketList(aheadPacketList),
	  mStreamIdx(streamIdx)
{
	ALOGI("FLVSource::FLVSource");
	mInitCheck = init();
}

FLVSource::~FLVSource()
{
	ALOGI("FLVSource::~FLVSource");
	if(mStarted) {
		stop();
	}
}

status_t FLVSource::start(MetaData *params)
{
	ALOGI("FLVSource::start");

	mGroup = new MediaBufferGroup;
	mGroup->add_buffer(new MediaBuffer(1 << 16));

	mStarted = true;
	return OK;
}

status_t FLVSource::stop()
{
	ALOGI("FLVSource::stop");

	delete mGroup;
	mGroup = NULL;

	mStarted = false;
	return OK;
}

sp<MetaData> FLVSource::getFormat()
{
	return mTrackMetadata;
}

status_t FLVSource::read(MediaBuffer **outBuffer, const ReadOptions *options)
{
	*outBuffer = NULL;

	int64_t seekTimeUs;
	ReadOptions::SeekMode mode;
	if(options != NULL && options->getSeekTo(&seekTimeUs, &mode)) {
		// TODO
		// mIOWrapper->seek();
	}

    MediaBuffer *buffer;
    status_t err = mGroup->acquire_buffer(&buffer);
    if (err != OK) {
        return err;
    }

    AVPacket pkt;
	int ret;
	char *dst = (char *)buffer->data();
	for (int i = 0; i < 512; /*fixme*/ i++) { 
		ret = ffmpeg_av_read_frame(mIc, &pkt, mStreamIdx, mAheadPacketList);
		if (ret != -2) {
			break;
		}
	}

	if (ret < 0) {
		ALOGV("ffmpeg_av_read_frame failed, ret: %d", ret);
		buffer->release();
		return ERROR_END_OF_STREAM;
	}

	if (mIc->streams[mStreamIdx]->codec->codec_id == AV_CODEC_ID_H264) {
		dst[0] = 0x0;
		dst[1] = 0x0;
		dst[2] = 0x0;
		dst[3] = 0x1;
		memcpy(dst + 4, pkt.data + 4, pkt.size - 4);
	} else {
		memcpy(dst, pkt.data, pkt.size);
	}

    buffer->set_range(0, pkt.size);

	//ALOGV("ic:: duration: %lld| ac:: start_time: %lld| pkt:: size: %d, pos: %lld, pts: %lld, idx: %d, dts: %lld, dur: %d", 
			//mIc->duration, mIc->streams[mStreamIdx]->start_time, pkt.size, pkt.pos, pkt.pts, pkt.stream_index, pkt.dts, pkt.duration);

	AVStream *st = mIc->streams[mStreamIdx];
	buffer->meta_data()->setInt64(kKeyTime, (st->time_base.num / (double) st->time_base.den) * pkt.pts * 1000000LL);
	buffer->meta_data()->setInt32(kKeyIsSyncFrame, 1); 

	*outBuffer = buffer;
	
	return OK;
}

static void reassembleESDS(char *s, int size, char *esds) {
    int csd0size = size;
    esds[0] = 3; // kTag_ESDescriptor;
    int esdescriptorsize = 26 + csd0size;
    CHECK(esdescriptorsize < 268435456); // 7 bits per byte, so max is 2^28-1
    esds[1] = 0x80 | (esdescriptorsize >> 21);
    esds[2] = 0x80 | ((esdescriptorsize >> 14) & 0x7f);
    esds[3] = 0x80 | ((esdescriptorsize >> 7) & 0x7f);
    esds[4] = (esdescriptorsize & 0x7f);
    esds[5] = esds[6] = 0; // es id
    esds[7] = 0; // flags
    esds[8] = 4; // kTag_DecoderConfigDescriptor
    int configdescriptorsize = 18 + csd0size;
    esds[9] = 0x80 | (configdescriptorsize >> 21);
    esds[10] = 0x80 | ((configdescriptorsize >> 14) & 0x7f);
    esds[11] = 0x80 | ((configdescriptorsize >> 7) & 0x7f);
    esds[12] = (configdescriptorsize & 0x7f);
    esds[13] = 0x40; // objectTypeIndication
    esds[14] = 0x15; // not sure what 14-25 mean, they are ignored by ESDS.cpp,
    esds[15] = 0x00; // but the actual values here were taken from a real file.
    esds[16] = 0x18;
    esds[17] = 0x00;
    esds[18] = 0x00;
    esds[19] = 0x00;
    esds[20] = 0xfa;
    esds[21] = 0x00;
    esds[22] = 0x00;
    esds[23] = 0x00;
    esds[24] = 0xfa;
    esds[25] = 0x00;
    esds[26] = 5; // kTag_DecoderSpecificInfo;
    esds[27] = 0x80 | (csd0size >> 21);
    esds[28] = 0x80 | ((csd0size >> 14) & 0x7f);
    esds[29] = 0x80 | ((csd0size >> 7) & 0x7f);
    esds[30] = (csd0size & 0x7f);
    memcpy((void*)&esds[31], s, csd0size);
    // data following this is ignored, so don't bother appending it
}

status_t FLVSource::init()
{
	ALOGI("FLVSource::init");

	AVStream *st = mIc->streams[mStreamIdx];
	AVCodecContext *ac = st->codec;

	mTrackMetadata->setInt32(kKeyChannelCount, ac->channels);
	mTrackMetadata->setInt32(kKeyChannelMask, ac->channels);
	mTrackMetadata->setInt32(kKeySampleRate, ac->sample_rate);
	mTrackMetadata->setInt32(kKeyBitRate, ac->bit_rate);

	switch (ac->codec_id) {
		case AV_CODEC_ID_AAC: 
		{
			mTrackMetadata->setCString(kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_AAC);

			// TODO
			//sp<AMessage> msg = new AMessage;
			//msg.setBuffer("csd-0", new ABuffer((char *)ac->extradata, ac->extradata_size));
			//convertMessageToMetaData(msg, mTrackMetadata);

			char esds[ac->extradata_size + 31];
			reassembleESDS((char *)ac->extradata, ac->extradata_size, esds);
			mTrackMetadata->setData(kKeyESDS, kTypeESDS, esds, sizeof(esds));
			break;
		}
		case AV_CODEC_ID_MP3:
		{
			mTrackMetadata->setCString(kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_MPEG);
			break;
		}
		case AV_CODEC_ID_H264:
		{
			mTrackMetadata->setCString(kKeyMIMEType, MEDIA_MIMETYPE_VIDEO_AVC);
            mTrackMetadata->setData(kKeyAVCC, kTypeAVCC, (char *)ac->extradata, ac->extradata_size);
			break;
		}
		case AV_CODEC_ID_FLV1: // FLV_CODECID_H263 ??
		default:
		{
			mTrackMetadata->setCString(kKeyMIMEType, "video/unknown");
            ALOGV("Unsupported video codec (%x)\n", ac->codec_id);
		}
	}

	if (ac->codec_type == AVMEDIA_TYPE_VIDEO) {
		if (ac->width && ac->height) {
			mTrackMetadata->setInt32(kKeyWidth, ac->width);
			mTrackMetadata->setInt32(kKeyHeight, ac->height);
		} else {
			ALOGV("error meta: idx: %d, codec_t:%d, width: %d, height: %d", mStreamIdx, ac->codec_type, ac->width, ac->height);
		}
	}

	return OK;
}


// FLVExtractor

FLVExtractor::FLVExtractor(const sp<DataSource> &source)
	: mDataSource(source),
	  mInitCheck(false),
	  mIc(0),
	  mAheadPacketList({0,0,0,0}),
	  mIOBuffer(0),
	  mIOWrapper(0)
{
    ALOGV("FLVExtractor::FLVExtractor");
	mInitCheck = init();
}

FLVExtractor::~FLVExtractor()
{
	if (mIc) {
		ffmpeg_avformat_close_input(mIc, mAheadPacketList);
		mIc = 0;
	}
	if (mIOBuffer) {
		delete []mIOBuffer;
		mIOBuffer = 0;
	}
	if (mIOWrapper) {
		delete mIOWrapper;
	}
}

sp<MetaData> FLVExtractor::getTrackMetaData(size_t index, uint32_t flags)
{
	if(mInitCheck != OK) {
		return NULL;
	} 
	if (index > 3)
		index = 0;
	return mSource[index]->getFormat();
}

sp<MetaData> FLVExtractor::getMetaData()
{
	ALOGI("FLVExtractor::getMetaData");
	
	if(mInitCheck != OK) {
		return NULL;
	}
	return mFileMeta;
}

size_t FLVExtractor::countTracks()
{
	size_t n = mInitCheck == OK ? mIc->nb_streams : 0;
	ALOGI("FLVExtractor::countTracks mInitCheck is %d, countTracks is %d", mInitCheck, n);
	return n;
}

sp<MediaSource> FLVExtractor::getTrack(size_t index)
{
	ALOGI("FLVExtractor::getTrack");
	if(mInitCheck != OK) {
		return NULL;
	}

	if (index > 3)
		index = 0;

	return mSource[index];
}

status_t FLVExtractor::init()
{
	ALOGV("FLVExtractor::init");

	// buffer size cannot large than 32767, or else ffmpeg will free the buffer and remalloc a small one.
	int bufferSize = 1 << 15; 
	mIOBuffer = new unsigned char[bufferSize];
	mIOWrapper = new DataSourceWrapper(mDataSource);
	AVIOContext *pb = ffmpeg_avio_alloc_context(mIOBuffer, bufferSize, 0, mIOWrapper, DataSourceWrapper::IORead, NULL, DataSourceWrapper::IOSeek);
	int err = ffmpeg_avformat_open_input(&mIc, pb, "", NULL, NULL);
    if (err < 0) {  
		ALOGE("FLV init error, ffmpeg_avformat_open_input failed, err: %d", err);
		return NO_INIT;
	}
    err = ffmpeg_avformat_find_stream_info(mIc, NULL);
    if (err < 0) {
		ALOGE("FLV init error, ffmpeg_find_stream_info failed, err: %d", err);
		return NO_INIT;
	} 

	mFileMeta = new MetaData;

	for (unsigned int i = 0; i < mIc->nb_streams && i < 4; i++) {
		MetaData *trackMeta = new MetaData;
		trackMeta->setInt64(kKeyDuration, mIc->duration);
		mSource[i] = new FLVSource(mDataSource, trackMeta, mIc, mAheadPacketList, i);
	}

	mFileMeta->setCString(kKeyMIMEType, "video/flv");

	return OK;
}

// Sniffer

bool SniffFLV(
        const sp<DataSource> &source, String8 *mimeType, float *confidence,
        sp<AMessage> *)
{
    uint8_t header[8];
    if (source->readAt(0, header, sizeof(header)) < 8)
        return false;

	AVProbeData pd;
	pd.buf = header;
	if (!ffmpeg_flv_probe(&pd))
		return false;

    *mimeType = MEDIA_MIMETYPE_CONTAINER_FLV;
    *confidence = 0.4;

    return true;
}

}//namespace android
