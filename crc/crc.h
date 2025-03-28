/***************************************************************
Copyright (c) 2022-2030, shisan233@sszc.live.
All rights reserved.
File:        crc.h
Version:     1.0
Author:      cjx
start date: 2025-05-23
Description: CRC（循环冗余校验）​ 是一种基于多项式除法的错误检测算法，用于验证数据传输或存储的完整性。
    其核心思想是通过对数据块进行多项式除法运算，生成固定长度的校验码（如 CRC16、CRC32 等），
    接收端通过重新计算校验码并与原校验码对比来判断数据是否被篡改
Version history

[序号]   |   [修改日期]   |   [修改者]   |   [修改内容]
1           2025-05-23        cjx           create

*****************************************************************/

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 使用前必须提前调用初始化一次（不在获取接口中做检测） */
void crcTable_init(void);

/**
 * @brief CRC 计算函数：计算输入数据的 CRC 值
 *
 * @param[in] crc               初始的 CRC 值（一般为 0）
 * @param[in] buf               指向输入数据的缓冲区
 * @param[in] len               输入数据的长度（字节数）
 *
 * @return 返回计算后的 CRC 值
 */
uint16_t crc16(uint16_t crc, const unsigned char *buf, uint64_t len);

uint32_t crc32(uint32_t crc, const unsigned char *buf, uint64_t len);

uint64_t crc64(uint64_t crc, const unsigned char *buf, uint64_t len);


#ifdef __cplusplus
}
#endif