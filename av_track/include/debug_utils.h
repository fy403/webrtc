#ifndef DEBUG_UTILS_H
#define DEBUG_UTILS_H

#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
}
namespace DebugUtils {

// 图像保存函数
bool save_frame_to_ppm(const AVFrame *frame, const std::string &filename);
bool save_frame_to_yuv(const AVFrame *frame, const std::string &filename);

// H.264 NALU 分析函数
void print_nalu_info(int nal_type, int nal_size, int index, int separator_type);
void analyze_nal_units(const AVPacket *packet);

// 原始音频包保存函数
bool initialize_raw_audio_writer(const std::string &filename);
bool save_raw_audio_packet(const AVPacket *packet, std::string debug_filename);
void finalize_raw_audio_file();

} // namespace DebugUtils

#endif // DEBUG_UTILS_H