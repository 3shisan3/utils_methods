#include "crc.h"

#include <stdbool.h>
#include <string.h>

// 预定义参数
#define CRC16_POLY UINT16_C(0x1021)                             // 
#define CRC32_POLY UINT32_C(0xedb88320)                         // 
#define CRC64_POLY UINT64_C(0xad93d23594c935a9)                 // 
// #define CRCINIT_VALUE UINT64_C(0)

static uint64_t crc64_table[8][256] = {{0}};                    // CRC64 表，利用 ​Slice-by-8 加速计算
static uint64_t crc32_table[4][256] = {{0}};                    // CRC32 表，利用 ​Slice-by-4 加速计算
static uint64_t crc16_table[2][256] = {{0}};                    // CRC16 表，利用 ​Slice-by-2 加速计算

// 参数结构体：定义CRC参数（位宽、多项式、初始值、输入/输出反射、最终异或）
typedef struct
{
    uint8_t width;                                              // CRC位宽：16, 32, 64
    uint64_t poly;                                              // 多项式（调整后的形式）
    uint64_t init;                                              // 初始值
    bool ref_in;                                                // 输入字节是否反射
    bool ref_out;                                               // 输出CRC是否反射
    uint64_t xor_out;                                           // 最终异或值
    uint8_t slice_level;                                        // Slice-by-N级别（例如8=Slice-by-8）
} CRCParams;

/*** ============================== 核心工具函数 ============================== ***/

// 反射data的低width位（用于CRC整体反射）
static inline uint_fast64_t crc_reflect(uint_fast64_t data, uint8_t width)
{
    uint_fast64_t ret = data & 0x01;                            // 取最低位

    for (size_t i = 1; i < width; i++) {
        data >>= 1;
        ret = (ret << 1) | (data & 0x01);                       // 左移并拼接下一位
    }

    return ret;
}

// 反射单个字节（8位）
static inline uint8_t reflect_byte(uint8_t byte)
{
    byte = (byte & 0xF0) >> 4 | (byte & 0x0F) << 4;
    byte = (byte & 0xCC) >> 2 | (byte & 0x33) << 2;
    byte = (byte & 0xAA) >> 1 | (byte & 0x55) << 1;
    return byte;
}

// 检测系统端模式（运行时）
static inline bool is_little_endian()
{
    uint64_t n = 1;
    return *(char *)&n;
}

// 字节反转函数：反转 64 位数据的字节顺序
static inline uint64_t rev8(uint64_t a)
{
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap64(a);                                // 使用编译器内置函数进行 64 位字节反转
#else
    uint64_t m;

    m = UINT64_C(0xff00ff00ff00ff);
    a = ((a >> 8) & m) | (a & m) << 8;
    m = UINT64_C(0xffff0000ffff);
    a = ((a >> 16) & m) | (a & m) << 16;
    return a >> 32 | a << 32;
#endif
}

// 使用动态切片长度适配的反转函数
static inline uint64_t rev_slice(uint64_t chunk, uint8_t slice)
{
    switch (slice)
    {
    case 8:
        return rev8(chunk);
    case 4:
    {
        // 手动实现 32 位字节反转
        uint32_t val = (uint32_t)chunk;
        val = ((val & 0xFF000000) >> 24) |
              ((val & 0x00FF0000) >> 8) |
              ((val & 0x0000FF00) << 8) |
              ((val & 0x000000FF) << 24);
        return (uint64_t)val;
    }
    case 2:
    {
        // 手动实现 16 位字节反转
        uint16_t val = (uint16_t)chunk;
        val = ((val & 0xFF00) >> 8) | ((val & 0x00FF) << 8);
        return (uint64_t)val;
    }
    default:
        return chunk;
    }
}

/*** ============================== 查表法核心 ============================== ***/

// 初始化单层表（基础表）
void _crc_init_single_table(uint64_t *table, const CRCParams *params)
{
    const uint64_t max_bit = 1ULL << (params->width - 1);       // crc取最高位 与运算 常量
    const uint64_t mask = (max_bit - 1) << 1 | 1;               // 排除width 64位时位运算超出范围
    const uint64_t poly = params->poly;

    for (int i = 0; i < 256; ++i)
    {
        uint8_t byte = (uint8_t)i;
        if (params->ref_in)
            byte = reflect_byte(byte);

        uint64_t crc = params->ref_in ? (uint64_t)byte : (uint64_t)byte << (params->width - 8);
        for (int j = 0; j < 8; ++j)
        {
            if (params->ref_in)
            {
                uint64_t bit = crc & 1;
                crc = (crc >> 1) ^ (bit ? poly : 0);
            }
            else
            {
                uint64_t bit = crc & (1ULL << (params->width - 1));
                crc = (crc << 1) ^ (bit ? poly : 0);
            }
        }
        table[i] = crc & mask;
    }
}

// 初始化多层表（支持Slice-by-N）
void crc_table_init(uint64_t table[][256], const CRCParams *params)
{
    // 生成基础表（第0层）
    _crc_init_single_table(table[0], params);

    const uint64_t mask = (params->width == 64) ? UINT64_MAX : ((1ULL << params->width) - 1);

    // 生成多层表（第1~N层）
    for (int layer = 1; layer < params->slice_level; ++layer)
    {
        for (int i = 0; i < 256; ++i)
        {
            uint64_t value = table[layer - 1][i];
            uint8_t index = (value >> (params->width - 8)) & 0xFF;
            table[layer][i] = ((value << 8) ^ table[0][index]) & mask;
        }
    }
}

/*** ============================== 快速计算函数 ============================== ***/

uint64_t crc_fast(const void *data, uint64_t len, uint64_t table[][256], const CRCParams *params)
{
    const uint8_t *next = (const uint8_t *)data;
    uint64_t crc = params->init;
    const uint64_t mask = (params->width == 64) ? UINT64_MAX : ((1ULL << params->width) - 1);
    const uint8_t slice = params->slice_level;

    // 处理未对齐的头部字节, 先处理到 8 字节对齐
    while (len && ((uintptr_t)next % slice) != 0)
    {
        uint8_t byte = *next++;
        len--;

        if (params->ref_in)
        {
            byte = reflect_byte(byte);
            uint8_t index = (crc ^ byte) & 0xFF;
            crc = (crc >> 8) ^ table[0][index];
        }
        else
        {
            uint8_t index = ((crc >> (params->width - 8)) ^ byte) & 0xFF;
            crc = (crc << 8) ^ table[0][index];
        }
        crc &= mask;
    }

    // Slice-by-N快速处理（对齐块）
    while (len >= slice)
    {
        uint64_t chunk;
        memcpy(&chunk, next, slice);
        next += slice;
        len -= slice;

        if (is_little_endian())
            chunk = rev_slice(chunk, slice);

        crc ^= chunk;

        for (int i = 0; i < slice; ++i)
        {
            uint8_t index;
            if (params->ref_in)
                index = (crc >> (i * 8)) & 0xFF;
            else
                index = (crc >> (params->width - 8 - i * 8)) & 0xFF;
            crc = table[i][index] ^ (params->ref_in ? (crc >> 8) : (crc << 8));
            crc &= mask;
        }
    }

    // 处理剩余字节
    while (len--)
    {
        uint8_t byte = *next++;

        if (params->ref_in)
        {
            byte = reflect_byte(byte);
            uint8_t index = (crc ^ byte) & 0xFF;
            crc = (crc >> 8) ^ table[0][index];
        }
        else
        {
            uint8_t index = ((crc >> (params->width - 8)) ^ byte) & 0xFF;
            crc = (crc << 8) ^ table[0][index];
        }
        crc &= mask;
    }

    // 最终处理
    if (params->ref_out)
        crc = crc_reflect(crc, params->width);
    crc ^= params->xor_out;
    return crc & mask;
}

/*** ============================== 外部使用接口封装 ============================== ***/

void crcTable_init(void)
{
    CRCParams crc16_params = {
        .width = 16,
        .poly = CRC16_POLY,
        .init = 0x0000,
        .ref_in = false,
        .ref_out = false,
        .xor_out = 0x0000,
        .slice_level = 2
    };
    crc_table_init(crc16_table, &crc16_params);

    CRCParams crc32_params = {
        .width = 32,
        .poly = CRC32_POLY,
        .init = 0xFFFFFFFF,
        .ref_in = true,
        .ref_out = true,
        .xor_out = 0xFFFFFFFF,
        .slice_level = 4
    };
    crc_table_init(crc32_table, &crc32_params);

    // CRC64-Redis
    CRCParams crc64_params = {
        .width = 64,
        .poly = CRC64_POLY,
        .init = 0xffffffffffffffff,
        .ref_in = true,
        .ref_out = true,
        .xor_out = 0x0000000000000000,
        .slice_level = 8
    };
    crc_table_init(crc64_table, &crc64_params);
}

uint16_t crc16(const unsigned char *buf, uint64_t len, uint16_t crc)
{
    static const CRCParams base_params = {
        .width = 16,
        .poly = CRC16_POLY,
        .init = 0x0000,
        .ref_in = false,
        .ref_out = false,
        .xor_out = 0x0000,
        .slice_level = 2
    };
    // 避免多线程问题
    CRCParams crc16_params = base_params;
    crc16_params.init = crc;

    return crc_fast(buf, len, crc16_table, &crc16_params);
}

uint32_t crc32(const unsigned char *buf, uint64_t len, uint32_t crc)
{
    static const CRCParams base_params = {
        .width = 32,
        .poly = CRC32_POLY,
        .init = 0xFFFFFFFF,
        .ref_in = true,
        .ref_out = true,
        .xor_out = 0xFFFFFFFF,
        .slice_level = 4
    };

    CRCParams crc32_params = base_params;
    crc32_params.init = crc;

    return crc_fast(buf, len, crc32_table, &crc32_params);
}

uint64_t crc64(const unsigned char *buf, uint64_t len, uint64_t crc)
{
    static const CRCParams base_params = {
        .width = 64,
        .poly = CRC64_POLY,
        .init = 0xffffffffffffffff,
        .ref_in = true,
        .ref_out = true,
        .xor_out = 0x0000000000000000,
        .slice_level = 8
    };

    CRCParams crc64_params = base_params;
    crc64_params.init = crc;

    return crc_fast(buf, len, crc64_table, &crc64_params);
}
