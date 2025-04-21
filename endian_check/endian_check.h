/***************************************************************
Copyright (c) 2022-2030, shisan233@sszc.live.
All rights reserved.
File:        endian_check.h
Version:     1.0
Author:      cjx
start date: 2025-4-21
Description: 校验当前环境大小端序
Version history

[序号]    |   [修改日期]  |   [修改者]   |   [修改内容]

*****************************************************************/

#pragma once

#ifdef __cplusplus  //  C++
#include <cstdint>

constexpr bool isLittleEndian() {
    return static_cast<const uint8_t&>(0x12345678) == 0x78;
}
#else               // C
#include <stdint.h>
#include <stdbool.h>

bool isLittleEndian() {
    return *(uint8_t*)&(uint32_t){0x12345678} == 0x78;
}
#endif

