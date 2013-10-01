This lib add flv support for Android Stagefright


使用ffmepg的flv demux，在Stagefright中增加对flv的支持

1、ffmpeg for android
    VLC使用ffmpeg，这里偷个懒直接使用的VLC for Android中的ffmpeg
    默认已经打开flv支持选项，直接 {path to vlc-android}/vlc/contrib/android/ffmpeg 拷贝至 {path to android}/external
    调整 config.mak中的prefix为 {path to android}/external/ffmpeg
    make && make install 
    
    可能的编译出错：undefined reference to 'sincos'
    原因：ndk的libm 有问题，改为 croot/out/system/ 下的（复制一份覆盖过去）。
    
2、libstragefright 的Android.mk增加 ffmpeg链接库
    LOCAL_LDFLAGS += -Lexternal/ffmpeg/lib -lavformat -lavcodec -lavutil
    
3、ffmpeg为c开发，需要对使用的接口进行调整，或加一层封装
    参考ffmpegflv.c, ffmpegflv.h
    
4、Extractor
    参考FLVExtractor.cpp, FLVExtractor.h
    

几个比较重要的细节：

1、时间及a/v同步
    可参考《ffmpeg，Stagefright 的时间控制及音视频同步》(http://blog.csdn.net/fallgold/article/details/12218815)

2、aac音频解码
    aac decode需要ESDS数据才能进行解码
    demux时，ffmpeg把aac的ESDS解到了extradata中，把这部分数据写入meta的kKeyESDS段即可

3、h264视频解码
    a、同样的，需要把extradata写入meta的kKeyAVCC段。
    b、h264硬件解码器需要每一个packet需要特殊的包头0x00000001
        原packet包头 4bytes 为packet长度，需要改为 0x00000001

至此，mediaplay已经能很好的支持大部分flv（aac/mp3 + h264）的播放了。

