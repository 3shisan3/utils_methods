#include "crc.h"

#include <stdbool.h>
#include <string.h>

/* 常用CRC配置表
    CRC类型	       width	 poly	               init	                ref_in    ref_out    xor_out
    CRC16-CCITT	    16	    0x1021	              0xFFFF	            false	  false	    0x0000
    CRC16-REDIES    16      0x1021                0x0000                false     false     0x0000
    CRC32	        32	    0x04C11DB7	          0xFFFFFFFF	        true	  true	    0xFFFFFFFF
    CRC64-ECMA	    64	    0x42F0E1EA	          0x00000000	        false	  false	    0x00000000
    CRC64-REDIES    64      0xad93d23594c935a9    0xffffffffffffffff    true      true      0x0000000000000000
 */

// 预定义参数
#define CRC16_POLY UINT16_C(0x1021)                             // redis中crc16使用的反转多项式
#define CRC32_POLY UINT32_C(0x04c11db7)                         // 反转多项式 (IEEE 802.3 标准多项式)
#define CRC64_POLY UINT64_C(0xad93d23594c935a9)                 // redis中crc64使用的反转多项式，与常规标准不同
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
    const uint64_t top_bit = 1ULL << (params->width - 1);
    const uint64_t mask = ((1ULL << (params->width - 1)) - 1) << 1 + 1;     // 排除width 64位时位运算超出范围
    uint64_t poly = params->poly;

    // 如果输出需要反射，多项式需要先反射
    if (params->ref_out)
    {
        poly = crc_reflect(poly, params->width);
    }

    for (int n = 0; n < 256; n++)
    {
        uint8_t byte = (params->ref_in) ? reflect_byte(n) : n;
        uint64_t crc = (uint64_t)byte << (params->width - 8);

        for (int i = 0; i < 8; i++)
        {
            if (crc & top_bit)
            {
                crc = (crc << 1) ^ poly;
            }
            else
            {
                crc <<= 1;
            }
        }

        // 应用输出反射和掩码
        crc = (params->ref_out) ? crc_reflect(crc, params->width) : crc;
        table[n] = (crc ^ params->xor_out) & mask;
    }
}

// 初始化多层表（支持Slice-by-N）
void crc_table_init(uint64_t table[][256], const CRCParams *params)
{
    // 生成基础表（第0层）
    _crc_init_single_table(table[0], params);

    const uint64_t mask = (params->width == 64) ? UINT64_MAX : ((1ULL << params->width) - 1);

    // 生成多层表（第1~N层）
    for (int layer = 1; layer < params->slice_level; layer++)
    {
        for (int n = 0; n < 256; n++)
        {
            uint64_t crc = table[layer - 1][n];

            // 方法1：查表法生成下一层
            for (int byte_pos = 0; byte_pos < layer; byte_pos++)
            {
                uint8_t idx = (crc >> (params->width - 8)) & 0xFF;
                crc = (crc << 8) ^ table[0][idx];
            }
            // 方法2：逐位计算（效率低，兼容性保障）(同基础表生成方案)
            table[layer][n] = crc & mask;
        }
    }

    /*  // 移到计算阶段
    // 若需生成大端表，反转所有条目
    if (!is_little_endian())
    {
        for (int k = 0; k < params->slice_level; k++)
        {
            for (int n = 0; n < 256; n++)
            {
                // 按实际位宽反转
                table[k][n] = crc_reflect(table[k][n], params->width);
            }
        }
    } */
}

/*** ============================== 快速计算函数 ============================== ***/

uint64_t crc_fast(const void *data, uint64_t len, uint64_t table[][256], const CRCParams *params)
{
    const uint8_t *next = (const uint8_t *)data;
    uint64_t crc = params->init;
    const uint64_t mask = (params->width == 64) ? UINT64_MAX : ((1ULL << params->width) - 1);
    const uint8_t slice = params->slice_level;

    // 处理未对齐的头部字节, 先处理到 8 字节对齐 
    while (len > 0 && (uintptr_t)next % slice != 0)
    {
        uint8_t byte = *next++;
        crc = (crc << 8) ^ table[0][((crc >> (params->width - 8)) ^ byte) & 0xFF];
        crc &= mask;
        len--;
    }

    // Slice-by-N快速处理（对齐块）
    while (len >= slice)
    {
        uint64_t chunk = 0;
        memcpy(&chunk, next, slice);

        if (!is_little_endian())
        {
            chunk = rev_slice(chunk, slice);
        }

        // 统一的多层查表计算
        uint64_t combined = chunk ^ (crc << (64 - params->width));
        crc = 0;

        for (int i = 0; i < slice; i++)
        {
            uint8_t idx = (combined >> (8 * (slice - 1 - i))) & 0xFF;
            crc ^= table[i][idx];
        }

        next += slice;
        len -= slice;
        crc &= mask;
    }

    // 处理剩余字节
    while (len--)
    {
        uint8_t byte = *next++;
        crc = (crc << 8) ^ table[0][((crc >> (params->width - 8)) ^ byte) & 0xFF];
        crc &= mask;
    }

    // 最终处理
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

uint16_t crc16(uint16_t crc, const unsigned char *buf, uint64_t len)
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

uint32_t crc32(uint32_t crc, const unsigned char *buf, uint64_t len)
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

uint64_t crc64(uint64_t crc, const unsigned char *buf, uint64_t len)
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