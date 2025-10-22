#include "debug_utils.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>

// 用于 NALU 分析的临时变量
static int prev_start_offset = 0;
static int start_code_size = 0;
static int prev_nalu_type = 0;

// 添加起始码类型的枚举
enum class Separator
{
    ShortStartSequence = 3,
    LongStartSequence = 4
};

namespace DebugUtils
{

bool save_frame_to_ppm(const AVFrame *frame, const std::string &filename)
{
    if (!frame || !frame->data[0])
    {
        std::cerr << "Invalid frame data for saving: " << filename << std::endl;
        return false;
    }

    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open())
    {
        std::cerr << "Cannot open file for writing: " << filename << std::endl;
        return false;
    }

    // 写入 PPM 头
    file << "P6\n"
         << frame->width << " " << frame->height << "\n255\n";

    // 写入 YUV 数据（PPM 需要 RGB，这里简单处理只保存 Y 分量作为灰度图）
    for (int y = 0; y < frame->height; y++)
    {
        for (int x = 0; x < frame->width; x++)
        {
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

bool save_frame_to_yuv(const AVFrame *frame, const std::string &filename)
{
    if (!frame || !frame->data[0])
    {
        std::cerr << "Invalid frame data for saving: " << filename << std::endl;
        return false;
    }

    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open())
    {
        std::cerr << "Cannot open file for writing: " << filename << std::endl;
        return false;
    }

    // 保存 Y 分量
    for (int y = 0; y < frame->height; y++)
    {
        file.write(reinterpret_cast<const char *>(frame->data[0] + y * frame->linesize[0]), frame->width);
    }

    // 保存 U 分量
    for (int y = 0; y < frame->height / 2; y++)
    {
        file.write(reinterpret_cast<const char *>(frame->data[1] + y * frame->linesize[1]), frame->width / 2);
    }

    // 保存 V 分量
    for (int y = 0; y < frame->height / 2; y++)
    {
        file.write(reinterpret_cast<const char *>(frame->data[2] + y * frame->linesize[2]), frame->width / 2);
    }

    file.close();
    std::cout << "Saved YUV frame to: " << filename << std::endl;
    return true;
}

void print_nalu_info(int nal_type, int nal_size, int index, int separator_type)
{
    const char *type_name = "Unknown";
    switch (nal_type)
    {
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
        type_name = "Coded slice of an auxiliary coded picture without partitioning";
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
    std::string separator_name = (separator_type == static_cast<int>(Separator::ShortStartSequence)) ? "ShortStartSequence (0x00 0x00 0x01)" : "LongStartSequence (0x00 0x00 0x00 0x01)";

    std::cout << "NALU[" << index << "]: Type=" << nal_type << " (" << type_name
              << "), Size=" << nal_size << " bytes, StartCode=" << separator_name << std::endl;
}

void analyze_nal_units(const AVPacket *packet)
{
    const uint8_t *data = packet->data;
    int size = packet->size;
    int offset = 0;
    int nalu_count = 0;
    bool has_sps = false;
    bool has_pps = false;
    bool has_idr = false;

    std::cout << "=== H.264 Packet Analysis ===" << std::endl;
    std::cout << "Total packet size: " << size << " bytes" << std::endl;
    std::cout << "Key frame: " << ((packet->flags & AV_PKT_FLAG_KEY) ? "YES" : "NO") << std::endl;
    std::cout << "PTS: " << packet->pts << std::endl;
    std::cout << "DTS: " << packet->dts << std::endl;

    while (offset < size)
    {
        // 查找起始码
        if (offset + 4 <= size &&
            data[offset] == 0x00 &&
            data[offset + 1] == 0x00 &&
            data[offset + 2] == 0x00 &&
            data[offset + 3] == 0x01)
        {
            // 4字节起始码
            if (nalu_count > 0)
            {
                int prev_nalu_size = offset - prev_start_offset - start_code_size;
                print_nalu_info(prev_nalu_type, prev_nalu_size, nalu_count - 1,
                                (start_code_size == 3) ? static_cast<int>(Separator::ShortStartSequence) : static_cast<int>(Separator::LongStartSequence));

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

            if (offset < size)
            {
                prev_nalu_type = data[offset] & 0x1F;
            }
            nalu_count++;
        }
        else if (offset + 3 <= size &&
                 data[offset] == 0x00 &&
                 data[offset + 1] == 0x00 &&
                 data[offset + 2] == 0x01)
        {
            // 3字节起始码
            if (nalu_count > 0)
            {
                int prev_nalu_size = offset - prev_start_offset - start_code_size;
                print_nalu_info(prev_nalu_type, prev_nalu_size, nalu_count - 1,
                                (start_code_size == 3) ? static_cast<int>(Separator::ShortStartSequence) : static_cast<int>(Separator::LongStartSequence));

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

            if (offset < size)
            {
                prev_nalu_type = data[offset] & 0x1F;
            }
            nalu_count++;
        }
        else
        {
            offset++;
        }
    }

    // 处理最后一个 NALU
    if (nalu_count > 0)
    {
        int last_nalu_size = size - prev_start_offset - start_code_size;
        print_nalu_info(prev_nalu_type, last_nalu_size, nalu_count - 1,
                        (start_code_size == 3) ? static_cast<int>(Separator::ShortStartSequence) : static_cast<int>(Separator::LongStartSequence));

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
    std::cout << "Is decodable: " << ((has_sps && has_pps) ? "YES" : "NO") << std::endl;
    std::cout << "=============================" << std::endl
              << std::endl;
}

} // namespace DebugUtils
