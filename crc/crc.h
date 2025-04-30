/***************************************************************
Copyright (c) 2022-2030, shisan233@sszc.live.
SPDX-License-Identifier: MIT 
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

/* 常用CRC配置表
   CRC类型            width   poly                init                 ref_in  ref_out xor_out
   CRC-16/CCITT       16      0x1021              0xFFFF               false   false   0x0000
   CRC-16/Redis       16      0x1021              0x0000               false   false   0x0000
   CRC-32             32      0xEDB88320          0xFFFFFFFF           true    true    0xFFFFFFFF
   CRC-64/ECMA        64      0x42F0E1EBA9FA5F    0xFFFFFFFFFFFFFFFF   false   false   0xFFFFFFFFFFFFFFFF
   CRC-64/XZ(Redis)   64      0xAD93D23594C935A9  0xFFFFFFFFFFFFFFFF   true    true    0xFFFFFFFFFFFFFFFF
*/

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 使用前必须提前调用初始化一次（不在获取接口中做检测）
 未进行线程安全功能添加，保证程序使用时初始化一次就行。
*/
void crcTable_init(void);

/**
 * @brief CRC 计算函数：计算输入数据的 CRC 值
 *
 * @param[in] crc               初始的 CRC 值（可参考配置表）
 * @param[in] buf               指向输入数据的缓冲区
 * @param[in] len               输入数据的长度（字节数）
 *
 * @return 返回计算后的 CRC 值
 */
uint16_t crc16(const unsigned char *buf, uint64_t len, uint16_t crc);

uint32_t crc32(const unsigned char *buf, uint64_t len, uint32_t crc);

uint64_t crc64(const unsigned char *buf, uint64_t len, uint64_t crc);


#ifdef __cplusplus
}
#endif