#include "crc/crc.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

// 标准CRC16-CCITT逐位计算（用于对比）
uint16_t crc16_ccitt_standard(const uint8_t *data, uint64_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint64_t i = 0; i < len; i++)
    {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++)
        {
            if (crc & 0x8000)
            {
                crc = (crc << 1) ^ 0x1021;
            }
            else
            {
                crc <<= 1;
            }
        }
    }
    return crc;
}

// CRC16-CCITT 测试用例（多项式0x1021，初始值0xFFFF）
void test_crc16()
{
    const unsigned char empty[] = "";
    const unsigned char test_str[] = "123456789";

    // 空数据测试
    uint16_t crc = crc16(0xFFFF, empty, 0);
    assert(crc == 0xFFFF); // 初始值保持不变

    // 标准测试用例（CRC16-CCITT "123456789" 应得 0x29B1）
    crc = crc16(0xFFFF, test_str, strlen((char *)test_str));

    uint16_t crc_standard = crc16_ccitt_standard(test_str, strlen((char *)test_str));
    printf("CRC16 Result: 0x%04X (Expected: 0x29B1)\n", crc);
    assert(crc == 0x29B1);
}

// CRC32 测试用例（多项式0xEDB88320，初始值0xFFFFFFFF）
void test_crc32()
{
    const unsigned char empty[] = "";
    const unsigned char test_str[] = "123456789";

    // 空数据测试（初始值异或0xFFFFFFFF）
    uint32_t crc = crc32(0xFFFFFFFF, empty, 0);
    assert(crc == 0xFFFFFFFF ^ 0xFFFFFFFF); // 结果为0

    // 标准测试用例（CRC32 "123456789" 应得 0xCBF43926）
    crc = crc32(0xFFFFFFFF, test_str, strlen((char *)test_str));
    printf("CRC32 Result: 0x%08X (Expected: 0xCBF43926)\n", crc);
    assert(crc == 0xCBF43926);
}

// CRC64-ECMA 测试用例（Redis使用的多项式）
void test_crc64()
{
    const unsigned char empty[] = "";
    const unsigned char test_str[] = "123456789";

    // 空数据测试
    uint64_t crc = crc64(0, empty, 0);
    assert(crc == 0); // 初始值为0，结果保持0

    // Redis CRC64测试用例（"123456789" 应得 0xE9C6D914C4B8D9CA）
    crc = crc64(0, test_str, strlen((char *)test_str));
    printf("CRC64 Result: 0x%016llX (Expected: 0xE9C6D914C4B8D9CA)\n", crc);
    assert(crc == 0xE9C6D914C4B8D9CA);
}

int main()
{
    // 初始化查表
    crcTable_init();

    // 执行测试用例
    // test_crc16();
    // test_crc32();
    test_crc64();

    printf("All tests passed!\n");
    return 0;
}