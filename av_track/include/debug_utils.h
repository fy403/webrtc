#ifndef DEBUG_UTILS_H
#define DEBUG_UTILS_H

#include <string>

extern "C"
{
#include <libavformat/avformat.h>
}

namespace DebugUtils
{
    // 保存帧为PPM图片格式
    bool save_frame_to_ppm(const AVFrame *frame, const std::string &filename);
    
    // 保存帧为YUV格式
    bool save_frame_to_yuv(const AVFrame *frame, const std::string &filename);
    
    // 分析H.264 NALU单元
    void analyze_nal_units(const AVPacket *packet);
    
    // 打印NALU信息
    void print_nalu_info(int nal_type, int nal_size, int index, int separator_type);
}

#endif // DEBUG_UTILS_H
