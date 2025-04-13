#include "crc.h"
#include <stdbool.h>
#include <string.h>

/* ==================== 预定义多项式 ==================== */
#define CRC16_POLY  UINT16_C(0x1021)        /* CRC-16/CCITT-FALSE */
#define CRC32_POLY  UINT32_C(0xedb88320)    /* CRC-32 (Ethernet, ZIP) */
#define CRC64_POLY  UINT64_C(0xad93d23594c935a9) /* CRC-64 (Redis) */

/* ==================== 查表法加速表 ==================== */
static uint64_t crc64_table[8][256];        /* Slice-by-8 for CRC64 */
static uint32_t crc32_table[4][256];        /* Slice-by-4 for CRC32 */
static uint16_t crc16_table[2][256];        /* Slice-by-2 for CRC16 */

/* ==================== CRC参数结构体 ==================== */
typedef struct {
    uint8_t  width;                         /* 位宽：16, 32, 64 */
    uint64_t poly;                          /* 多项式 */
    uint64_t init;                          /* 初始值 */
    bool     ref_in;                        /* 输入反射 */
    bool     ref_out;                       /* 输出反射 */
    uint64_t xor_out;                       /* 最终异或值 */
    uint8_t  slice_level;                   /* Slice-by-N级别 */
} CRCParams;

/* ==================== 工具函数 ==================== */

/**
 * @brief 反射单个字节(8位)
 * @param byte 要反射的字节
 * @return 反射后的字节
 */
static inline uint8_t reflect_byte(uint8_t byte) {
    byte = (byte & 0xF0) >> 4 | (byte & 0x0F) << 4;
    byte = (byte & 0xCC) >> 2 | (byte & 0x33) << 2;
    byte = (byte & 0xAA) >> 1 | (byte & 0x55) << 1;
    return byte;
}

/**
 * @brief 反射数据(低width位)
 * @param data  要反射的数据
 * @param width 要反射的位数
 * @return 反射后的数据
 */
static inline uint64_t crc_reflect(uint64_t data, uint8_t width) {
    uint64_t ret = 0;
    for (uint8_t i = 0; i < width; i++) {
        ret = (ret << 1) | (data & 0x01);
        data >>= 1;
    }
    return ret;
}

/**
 * @brief 检测系统是否为小端序
 * @return true-小端序, false-大端序
 */
static inline bool is_little_endian() {
    static const uint16_t test = 0x0102;
    return *(const uint8_t*)&test == 0x02;
}

/**
 * @brief 64位字节反转
 * @param a 要反转的64位数据
 * @return 反转后的64位数据
 */
static inline uint64_t rev8(uint64_t a) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap64(a);
#else
    a = ((a & 0x00000000FFFFFFFF) << 32) | ((a & 0xFFFFFFFF00000000) >> 32);
    a = ((a & 0x0000FFFF0000FFFF) << 16) | ((a & 0xFFFF0000FFFF0000) >> 16);
    a = ((a & 0x00FF00FF00FF00FF) << 8)  | ((a & 0xFF00FF00FF00FF00) >> 8);
    return a;
#endif
}

/**
 * @brief 根据slice级别反转数据
 * @param chunk 要反转的数据块
 * @param slice 切片级别(2,4,8)
 * @return 反转后的数据
 */
static inline uint64_t rev_slice(uint64_t chunk, uint8_t slice) {
    switch (slice) {
        case 8: return rev8(chunk);
        case 4: return (uint64_t)__builtin_bswap32((uint32_t)chunk);
        case 2: return (uint64_t)__builtin_bswap16((uint16_t)chunk);
        default: return chunk;
    }
}

/* ==================== 查表法核心 ==================== */

/**
 * @brief 初始化单层CRC表
 * @param table  表指针
 * @param params CRC参数
 */
static void crc_init_single_table(void *table, const CRCParams *params) {
    const uint64_t mask = (params->width == 64) ? UINT64_MAX : ((1ULL << params->width) - 1);
    
    for (int i = 0; i < 256; i++) {
        uint8_t byte = (uint8_t)i;
        if (params->ref_in) byte = reflect_byte(byte);
        
        uint64_t crc = params->ref_in ? byte : ((uint64_t)byte << (params->width - 8));
        
        for (int j = 0; j < 8; j++) {
            if (params->ref_in) {
                crc = (crc >> 1) ^ ((crc & 1) ? params->poly : 0);
            } else {
                crc = (crc << 1) ^ ((crc >> (params->width - 1)) ? params->poly : 0);
            }
        }
        
        /* 根据宽度存储到正确的表中 */
        switch (params->width) {
            case 16: ((uint16_t*)table)[i] = (uint16_t)(crc & mask); break;
            case 32: ((uint32_t*)table)[i] = (uint32_t)(crc & mask); break;
            case 64: ((uint64_t*)table)[i] = crc & mask; break;
        }
    }
}

/**
 * @brief 初始化多层CRC表(Slice-by-N)
 * @param table  表指针
 * @param params CRC参数
 */
static void crc_init_multi_table(void *table, const CRCParams *params) {
    /* 初始化基础表(第0层) */
    crc_init_single_table(table, params);
    
    const uint64_t mask = (params->width == 64) ? UINT64_MAX : ((1ULL << params->width) - 1);
    
    /* 生成多层表 */
    for (int layer = 1; layer < params->slice_level; layer++) {
        for (int i = 0; i < 256; i++) {
            uint64_t value;
            
            /* 从正确的表中读取值 */
            switch (params->width) {
                case 16: value = ((uint16_t(*)[256])table)[layer-1][i]; break;
                case 32: value = ((uint32_t(*)[256])table)[layer-1][i]; break;
                case 64: value = ((uint64_t(*)[256])table)[layer-1][i]; break;
            }
            
            uint8_t index;
            if (params->ref_in) {
                index = value & 0xFF;
            } else {
                index = (value >> (params->width - 8)) & 0xFF;
            }
            
            uint64_t new_value;
            switch (params->width) {
                case 16: new_value = ((uint16_t(*)[256])table)[0][index]; break;
                case 32: new_value = ((uint32_t(*)[256])table)[0][index]; break;
                case 64: new_value = ((uint64_t(*)[256])table)[0][index]; break;
            }
            
            if (params->ref_in) {
                new_value ^= (value >> 8);
            } else {
                new_value ^= (value << 8);
            }
            
            /* 存储到正确的表中 */
            switch (params->width) {
                case 16: ((uint16_t(*)[256])table)[layer][i] = (uint16_t)(new_value & mask); break;
                case 32: ((uint32_t(*)[256])table)[layer][i] = (uint32_t)(new_value & mask); break;
                case 64: ((uint64_t(*)[256])table)[layer][i] = new_value & mask; break;
            }
        }
    }
}

/* ==================== 快速计算函数 ==================== */

/**
 * @brief 通用CRC计算函数
 * @param data   输入数据指针
 * @param len    数据长度
 * @param table  CRC表指针
 * @param params CRC参数
 * @return 计算得到的CRC值
 */
static uint64_t crc_compute(const void *data, uint64_t len, const void *table, 
                           const CRCParams *params) {
    const uint8_t *ptr = (const uint8_t *)data;
    uint64_t crc = params->init;
    const uint64_t mask = (params->width == 64) ? UINT64_MAX : ((1ULL << params->width) - 1);
    const uint8_t slice = params->slice_level;
    
    /* 处理未对齐的头部字节 */
    while (len && ((uintptr_t)ptr % slice)) {
        uint8_t byte = *ptr++;
        len--;
        
        if (params->ref_in) {
            byte = reflect_byte(byte);
            uint8_t index = (crc ^ byte) & 0xFF;
            switch (params->width) {
                case 16: crc = (crc >> 8) ^ ((uint16_t(*)[256])table)[0][index]; break;
                case 32: crc = (crc >> 8) ^ ((uint32_t(*)[256])table)[0][index]; break;
                case 64: crc = (crc >> 8) ^ ((uint64_t(*)[256])table)[0][index]; break;
            }
        } else {
            uint8_t index = ((crc >> (params->width - 8)) ^ byte) & 0xFF;
            switch (params->width) {
                case 16: crc = (crc << 8) ^ ((uint16_t(*)[256])table)[0][index]; break;
                case 32: crc = (crc << 8) ^ ((uint32_t(*)[256])table)[0][index]; break;
                case 64: crc = (crc << 8) ^ ((uint64_t(*)[256])table)[0][index]; break;
            }
        }
        crc &= mask;
    }
    
    /* Slice-by-N处理对齐块 */
    while (len >= slice) {
        uint64_t chunk;
        memcpy(&chunk, ptr, slice);
        ptr += slice;
        len -= slice;
        
        if (is_little_endian()) {
            chunk = rev_slice(chunk, slice);
        }
        
        crc ^= chunk;
        
        for (int i = 0; i < slice; i++) {
            uint8_t index;
            if (params->ref_in) {
                index = (crc >> (i * 8)) & 0xFF;
            } else {
                index = (crc >> (params->width - 8 - i * 8)) & 0xFF;
            }
            
            switch (params->width) {
                case 16: 
                    crc = ((uint16_t(*)[256])table)[i][index] ^ (params->ref_in ? (crc >> 8) : (crc << 8));
                    break;
                case 32:
                    crc = ((uint32_t(*)[256])table)[i][index] ^ (params->ref_in ? (crc >> 8) : (crc << 8));
                    break;
                case 64:
                    crc = ((uint64_t(*)[256])table)[i][index] ^ (params->ref_in ? (crc >> 8) : (crc << 8));
                    break;
            }
            crc &= mask;
        }
    }
    
    /* 处理剩余字节 */
    while (len--) {
        uint8_t byte = *ptr++;
        
        if (params->ref_in) {
            byte = reflect_byte(byte);
            uint8_t index = (crc ^ byte) & 0xFF;
            switch (params->width) {
                case 16: crc = (crc >> 8) ^ ((uint16_t(*)[256])table)[0][index]; break;
                case 32: crc = (crc >> 8) ^ ((uint32_t(*)[256])table)[0][index]; break;
                case 64: crc = (crc >> 8) ^ ((uint64_t(*)[256])table)[0][index]; break;
            }
        } else {
            uint8_t index = ((crc >> (params->width - 8)) ^ byte) & 0xFF;
            switch (params->width) {
                case 16: crc = (crc << 8) ^ ((uint16_t(*)[256])table)[0][index]; break;
                case 32: crc = (crc << 8) ^ ((uint32_t(*)[256])table)[0][index]; break;
                case 64: crc = (crc << 8) ^ ((uint64_t(*)[256])table)[0][index]; break;
            }
        }
        crc &= mask;
    }
    
    /* 最终处理 */
    if (params->ref_out) {
        crc = crc_reflect(crc, params->width);
    }
    crc ^= params->xor_out;
    return crc & mask;
}

/* ==================== 外部接口 ==================== */

/**
 * @brief 初始化所有CRC表(只需调用一次)
 */
void crcTable_init(void) {
    /* CRC16 (CCITT-FALSE) */
    const CRCParams crc16_params = {
        .width      = 16,
        .poly       = CRC16_POLY,
        .init       = 0xFFFF,
        .ref_in     = false,
        .ref_out    = false,
        .xor_out    = 0x0000,
        .slice_level = 2
    };
    crc_init_multi_table(crc16_table, &crc16_params);
    
    /* CRC32 (Ethernet, ZIP) */
    const CRCParams crc32_params = {
        .width      = 32,
        .poly       = CRC32_POLY,
        .init       = 0xFFFFFFFF,
        .ref_in     = true,
        .ref_out    = true,
        .xor_out    = 0xFFFFFFFF,
        .slice_level = 4
    };
    crc_init_multi_table(crc32_table, &crc32_params);
    
    /* CRC64 (Redis) */
    const CRCParams crc64_params = {
        .width      = 64,
        .poly       = CRC64_POLY,
        .init       = 0xFFFFFFFFFFFFFFFF,
        .ref_in     = true,
        .ref_out    = true,
        .xor_out    = 0x0000000000000000,
        .slice_level = 8
    };
    crc_init_multi_table(crc64_table, &crc64_params);
}

/**
 * @brief 计算CRC16值
 * @param buf  输入数据指针
 * @param len  数据长度
 * @param crc  初始CRC值
 * @return 计算得到的CRC16值
 */
uint16_t crc16(const unsigned char *buf, uint64_t len, uint16_t crc) {
    const CRCParams params = {
        .width      = 16,
        .poly       = CRC16_POLY,
        .init       = crc,
        .ref_in     = false,
        .ref_out    = false,
        .xor_out    = 0x0000,
        .slice_level = 2
    };
    return (uint16_t)crc_compute(buf, len, crc16_table, &params);
}

/**
 * @brief 计算CRC32值
 * @param buf  输入数据指针
 * @param len  数据长度
 * @param crc  初始CRC值
 * @return 计算得到的CRC32值
 */
uint32_t crc32(const unsigned char *buf, uint64_t len, uint32_t crc) {
    const CRCParams params = {
        .width      = 32,
        .poly       = CRC32_POLY,
        .init       = crc,
        .ref_in     = true,
        .ref_out    = true,
        .xor_out    = 0xFFFFFFFF,
        .slice_level = 4
    };
    return (uint32_t)crc_compute(buf, len, crc32_table, &params);
}

/**
 * @brief 计算CRC64值
 * @param buf  输入数据指针
 * @param len  数据长度
 * @param crc  初始CRC值
 * @return 计算得到的CRC64值
 */
uint64_t crc64(const unsigned char *buf, uint64_t len, uint64_t crc) {
    const CRCParams params = {
        .width      = 64,
        .poly       = CRC64_POLY,
        .init       = crc,
        .ref_in     = true,
        .ref_out    = true,
        .xor_out    = 0x0000000000000000,
        .slice_level = 8
    };
    return crc_compute(buf, len, crc64_table, &params);
}