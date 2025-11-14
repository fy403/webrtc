#include "debug_utils.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

extern "C" {
#include <libavutil/intreadwrite.h>
}

// 辅助函数，用于安全地转换FFmpeg错误代码为字符串
static std::string av_error_string(int errnum) {
  char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
  av_strerror(errnum, errbuf, sizeof(errbuf));
  return std::string(errbuf);
}

// 用于 NALU 分析的临时变量
static int prev_start_offset = 0;
static int start_code_size = 0;
static int prev_nalu_type = 0;

// 添加起始码类型的枚举
enum class Separator { ShortStartSequence = 3, LongStartSequence = 4 };

// 全局原始音频写入器
class RawAudioWriter {
public:
  RawAudioWriter()
      : format_context(nullptr), initialized(false), audio_stream_index(-1) {}
  ~RawAudioWriter() { finalize(); }

  bool initialize(const std::string &filename) {
    if (initialized) {
      return true;
    }

    // 分配输出格式上下文
    int ret = avformat_alloc_output_context2(&format_context, nullptr, "wav",
                                             filename.c_str());
    if (ret < 0 || !format_context) {
      std::cerr << "Could not create output context for WAV: "
                << av_error_string(ret) << std::endl;
      return false;
    }

    // 创建音频流
    AVStream *stream = avformat_new_stream(format_context, nullptr);
    if (!stream) {
      std::cerr << "Could not create new stream" << std::endl;
      return false;
    }

    // 设置编解码器参数 (PCM)
    AVCodecParameters *codecpar = stream->codecpar;
    codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    codecpar->codec_id = AV_CODEC_ID_PCM_S16LE; // PCM 16-bit little endian
    codecpar->channels = 1;                     // 单通道
    codecpar->channel_layout = AV_CH_LAYOUT_MONO;
    codecpar->sample_rate = 48000; // 默认采样率
    codecpar->bit_rate = 768000;   // 48000 * 1 * 16
    codecpar->block_align = 2;     // 1 channel * 2 bytes per sample
    codecpar->bits_per_coded_sample = 16;
    codecpar->format = AV_SAMPLE_FMT_S16;

    // 设置时间基准以避免时间戳警告
    stream->time_base = (AVRational){1, codecpar->sample_rate};

    stream->time_base = (AVRational){1, 48000};

    audio_stream_index = stream->index;

    // 打开输出文件
    if (!(format_context->oformat->flags & AVFMT_NOFILE)) {
      ret = avio_open(&format_context->pb, filename.c_str(), AVIO_FLAG_WRITE);
      if (ret < 0) {
        std::cerr << "Could not open output file: " << filename << " - "
                  << av_error_string(ret) << std::endl;
        return false;
      }
    }

    // 写入头部信息
    ret = avformat_write_header(format_context, nullptr);
    if (ret < 0) {
      std::cerr << "Error writing WAV header: " << av_error_string(ret)
                << std::endl;
      // 如果写入头部失败，需要关闭已打开的文件
      if (!(format_context->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&format_context->pb);
      }
      return false;
    }

    initialized = true;
    std::cout << "Initialized raw audio writer: " << filename << std::endl;
    return true;
  }

  bool writePacket(const AVPacket *packet) {
    if (!initialized || !format_context) {
      std::cerr << "Raw audio writer not initialized" << std::endl;
      return false;
    }

    if (!packet || !packet->data || packet->size <= 0) {
      std::cerr << "Invalid packet for writing" << std::endl;
      return false;
    }

    AVPacket *pkt = av_packet_clone(packet);
    if (!pkt) {
      std::cerr << "Failed to clone packet" << std::endl;
      return false;
    }

    // 设置流索引
    pkt->stream_index = audio_stream_index;

    // 设置时间戳以避免警告
    if (pkt->pts == AV_NOPTS_VALUE) {
      pkt->pts = 0;
    }
    if (pkt->dts == AV_NOPTS_VALUE) {
      pkt->dts = 0;
    }

    // 写入数据包
    int ret = av_interleaved_write_frame(format_context, pkt);
    if (ret < 0) {
      std::cerr << "Error writing frame to WAV file: " << av_error_string(ret)
                << std::endl;
      av_packet_free(&pkt);
      return false;
    }

    av_packet_free(&pkt);
    return true;
  }

  void finalize() {
    if (!initialized) {
      return;
    }
    if (initialized && format_context) {
      // 写入尾部信息
      av_write_trailer(format_context);

      // 关闭输出文件
      if (!(format_context->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&format_context->pb);
      }

      // 释放资源
      avformat_free_context(format_context);
      format_context = nullptr;
    }
    initialized = false;
    audio_stream_index = -1;
  }

  bool isInitialized() const { return initialized; }

private:
  AVFormatContext *format_context;
  bool initialized;
  int audio_stream_index;
};

static RawAudioWriter g_rawAudioWriter;

static RawAudioWriter g_rawAudioWriter2;

namespace DebugUtils {

bool save_frame_to_ppm(const AVFrame *frame, const std::string &filename) {
  if (!frame || !frame->data[0]) {
    std::cerr << "Invalid frame data for saving: " << filename << std::endl;
    return false;
  }

  std::ofstream file(filename, std::ios::binary);
  if (!file.is_open()) {
    std::cerr << "Cannot open file for writing: " << filename << std::endl;
    return false;
  }

  // 写入 PPM 头
  file << "P6\n" << frame->width << " " << frame->height << "\n255\n";

  // 写入 YUV 数据（PPM 需要 RGB，这里简单处理只保存 Y 分量作为灰度图）
  for (int y = 0; y < frame->height; y++) {
    for (int x = 0; x < frame->width; x++) {
      uint8_t y_value = frame->data[0][y * frame->linesize[0] + x];
      // 将 Y 分量复制到 RGB 三个通道，创建灰度图
      file.put(y_value); // R
      file.put(y_value); // G
      file.put(y_value); // B
    }
  }

  file.close();
  std::cout << "Saved frame to: " << filename << std::endl;
  return true;
}

bool save_frame_to_yuv(const AVFrame *frame, const std::string &filename) {
  if (!frame || !frame->data[0]) {
    std::cerr << "Invalid frame data for saving: " << filename << std::endl;
    return false;
  }

  std::ofstream file(filename, std::ios::binary);
  if (!file.is_open()) {
    std::cerr << "Cannot open file for writing: " << filename << std::endl;
    return false;
  }

  // 保存 Y 分量
  for (int y = 0; y < frame->height; y++) {
    file.write(
        reinterpret_cast<const char *>(frame->data[0] + y * frame->linesize[0]),
        frame->width);
  }

  // 保存 U 分量
  for (int y = 0; y < frame->height / 2; y++) {
    file.write(
        reinterpret_cast<const char *>(frame->data[1] + y * frame->linesize[1]),
        frame->width / 2);
  }

  // 保存 V 分量
  for (int y = 0; y < frame->height / 2; y++) {
    file.write(
        reinterpret_cast<const char *>(frame->data[2] + y * frame->linesize[2]),
        frame->width / 2);
  }

  file.close();
  std::cout << "Saved YUV frame to: " << filename << std::endl;
  return true;
}

void print_nalu_info(int nal_type, int nal_size, int index,
                     int separator_type) {
  const char *type_name = "Unknown";
  switch (nal_type) {
  case 1:
    type_name = "Coded slice of a non-IDR picture";
    break;
  case 2:
    type_name = "Coded slice data partition A";
    break;
  case 3:
    type_name = "Coded slice data partition B";
    break;
  case 4:
    type_name = "Coded slice data partition C";
    break;
  case 5:
    type_name = "Coded slice of an IDR picture";
    break;
  case 6:
    type_name = "Supplemental enhancement information (SEI)";
    break;
  case 7:
    type_name = "Sequence parameter set (SPS)";
    break;
  case 8:
    type_name = "Picture parameter set (PPS)";
    break;
  case 9:
    type_name = "Access unit delimiter";
    break;
  case 10:
    type_name = "End of sequence";
    break;
  case 11:
    type_name = "End of stream";
    break;
  case 12:
    type_name = "Filler data";
    break;
  case 13:
    type_name = "Sequence parameter set extension";
    break;
  case 14:
    type_name = "Prefix NAL unit";
    break;
  case 15:
    type_name = "Subset sequence parameter set";
    break;
  case 16:
    type_name = "Reserved";
    break;
  case 17:
    type_name = "Reserved";
    break;
  case 18:
    type_name = "Reserved";
    break;
  case 19:
    type_name =
        "Coded slice of an auxiliary coded picture without partitioning";
    break;
  case 20:
    type_name = "Coded slice extension";
    break;
  case 21:
    type_name = "Coded slice extension for depth view components";
    break;
  default:
    break;
  }

  // 打印起始码类型
  std::string separator_name =
      (separator_type == static_cast<int>(Separator::ShortStartSequence))
          ? "ShortStartSequence (0x00 0x00 0x01)"
          : "LongStartSequence (0x00 0x00 0x00 0x01)";

  std::cout << "NALU[" << index << "]: Type=" << nal_type << " (" << type_name
            << "), Size=" << nal_size << " bytes, StartCode=" << separator_name
            << std::endl;
}

void analyze_nal_units(const AVPacket *packet) {
  const uint8_t *data = packet->data;
  int size = packet->size;
  int offset = 0;
  int nalu_count = 0;
  bool has_sps = false;
  bool has_pps = false;
  bool has_idr = false;

  std::cout << "=== H.264 Packet Analysis ===" << std::endl;
  std::cout << "Total packet size: " << size << " bytes" << std::endl;
  std::cout << "Key frame: "
            << ((packet->flags & AV_PKT_FLAG_KEY) ? "YES" : "NO") << std::endl;
  std::cout << "PTS: " << packet->pts << std::endl;
  std::cout << "DTS: " << packet->dts << std::endl;

  while (offset < size) {
    // 查找起始码
    if (offset + 4 <= size && data[offset] == 0x00 &&
        data[offset + 1] == 0x00 && data[offset + 2] == 0x00 &&
        data[offset + 3] == 0x01) {
      // 4字节起始码
      if (nalu_count > 0) {
        int prev_nalu_size = offset - prev_start_offset - start_code_size;
        print_nalu_info(prev_nalu_type, prev_nalu_size, nalu_count - 1,
                        (start_code_size == 3)
                            ? static_cast<int>(Separator::ShortStartSequence)
                            : static_cast<int>(Separator::LongStartSequence));

        // 检查关键参数集
        if (prev_nalu_type == 7)
          has_sps = true;
        if (prev_nalu_type == 8)
          has_pps = true;
        if (prev_nalu_type == 5)
          has_idr = true;
      }

      prev_start_offset = offset;
      start_code_size = 4;
      offset += 4;

      if (offset < size) {
        prev_nalu_type = data[offset] & 0x1F;
      }
      nalu_count++;
    } else if (offset + 3 <= size && data[offset] == 0x00 &&
               data[offset + 1] == 0x00 && data[offset + 2] == 0x01) {
      // 3字节起始码
      if (nalu_count > 0) {
        int prev_nalu_size = offset - prev_start_offset - start_code_size;
        print_nalu_info(prev_nalu_type, prev_nalu_size, nalu_count - 1,
                        (start_code_size == 3)
                            ? static_cast<int>(Separator::ShortStartSequence)
                            : static_cast<int>(Separator::LongStartSequence));

        if (prev_nalu_type == 7)
          has_sps = true;
        if (prev_nalu_type == 8)
          has_pps = true;
        if (prev_nalu_type == 5)
          has_idr = true;
      }

      prev_start_offset = offset;
      start_code_size = 3;
      offset += 3;

      if (offset < size) {
        prev_nalu_type = data[offset] & 0x1F;
      }
      nalu_count++;
    } else {
      offset++;
    }
  }

  // 处理最后一个 NALU
  if (nalu_count > 0) {
    int last_nalu_size = size - prev_start_offset - start_code_size;
    print_nalu_info(prev_nalu_type, last_nalu_size, nalu_count - 1,
                    (start_code_size == 3)
                        ? static_cast<int>(Separator::ShortStartSequence)
                        : static_cast<int>(Separator::LongStartSequence));

    if (prev_nalu_type == 7)
      has_sps = true;
    if (prev_nalu_type == 8)
      has_pps = true;
    if (prev_nalu_type == 5)
      has_idr = true;
  }

  std::cout << "=== Summary ===" << std::endl;
  std::cout << "Total NAL units: " << nalu_count << std::endl;
  std::cout << "Has SPS: " << (has_sps ? "YES" : "NO") << std::endl;
  std::cout << "Has PPS: " << (has_pps ? "YES" : "NO") << std::endl;
  std::cout << "Has IDR: " << (has_idr ? "YES" : "NO") << std::endl;
  std::cout << "Is decodable: " << ((has_sps && has_pps) ? "YES" : "NO")
            << std::endl;
  std::cout << "=============================" << std::endl << std::endl;
}

// 原始音频包保存函数
bool initialize_raw_audio_writer(const std::string &filename) {
  return g_rawAudioWriter.initialize(filename);
}

bool save_raw_audio_packet(const AVPacket *packet, std::string filename) {
  // 如果写入器未初始化，先初始化
  if (!g_rawAudioWriter.isInitialized()) {
    // 默认使用文件名 raw_audio.wav
    if (!initialize_raw_audio_writer(filename)) {
      return false;
    }
  }

  return g_rawAudioWriter.writePacket(packet);
}

void finalize_raw_audio_file() { g_rawAudioWriter.finalize(); }

bool save_raw_audio_frame2(const AVFrame *frame, const std::string &filename) {
  // 如果写入器未初始化，先初始化
  if (!g_rawAudioWriter2.isInitialized()) {
    if (!initialize_raw_audio_writer2(filename)) {
      return false;
    }
  }
  // 创建一个AVPacket来存储帧数据
  AVPacket *pkt = av_packet_alloc();
  if (!pkt) {
    std::cerr << "Failed to allocate packet for audio frame" << std::endl;
    return false;
  }

  // 将帧数据复制到包中
  int ret = av_new_packet(pkt, frame->nb_samples * frame->channels *
                                   2); // 假设16位音频
  if (ret < 0) {
    std::cerr << "Failed to allocate packet data for audio frame" << std::endl;
    av_packet_free(&pkt);
    return false;
  }

  // 复制音频数据
  memcpy(pkt->data, frame->data[0], pkt->size);

  // 设置包属性
  pkt->pts = frame->pts;
  pkt->dts = frame->pts;
  pkt->duration = frame->nb_samples;
  pkt->stream_index = 0;

  bool result = g_rawAudioWriter2.writePacket(pkt);
  av_packet_free(&pkt);
  return result;
}

bool initialize_raw_audio_writer2(const std::string &filename) {
  return g_rawAudioWriter2.initialize(filename);
}

void finalize_raw_audio_frame_file2() { g_rawAudioWriter2.finalize(); }

} // namespace DebugUtils