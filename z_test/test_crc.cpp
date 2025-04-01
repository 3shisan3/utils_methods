#include "crc/crc.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <stdint.h>

void write_crc64_table_to_file(const char* filename, const uint64_t (*table)[256], int rows) {
    FILE* file = fopen(filename, "w");
    if (!file) {
        perror("Failed to open file");
        return;
    }

    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < 256; j++) {
            fprintf(file, "[%d][%d] = 0x%016llX\n", i, j, table[i][j]);
        }
    }

    fclose(file);
    printf("CRC64 table written to %s\n", filename);
}

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

static const uint16_t crc16tab[256]= {
    0x0000,0x1021,0x2042,0x3063,0x4084,0x50a5,0x60c6,0x70e7,
    0x8108,0x9129,0xa14a,0xb16b,0xc18c,0xd1ad,0xe1ce,0xf1ef,
    0x1231,0x0210,0x3273,0x2252,0x52b5,0x4294,0x72f7,0x62d6,
    0x9339,0x8318,0xb37b,0xa35a,0xd3bd,0xc39c,0xf3ff,0xe3de,
    0x2462,0x3443,0x0420,0x1401,0x64e6,0x74c7,0x44a4,0x5485,
    0xa56a,0xb54b,0x8528,0x9509,0xe5ee,0xf5cf,0xc5ac,0xd58d,
    0x3653,0x2672,0x1611,0x0630,0x76d7,0x66f6,0x5695,0x46b4,
    0xb75b,0xa77a,0x9719,0x8738,0xf7df,0xe7fe,0xd79d,0xc7bc,
    0x48c4,0x58e5,0x6886,0x78a7,0x0840,0x1861,0x2802,0x3823,
    0xc9cc,0xd9ed,0xe98e,0xf9af,0x8948,0x9969,0xa90a,0xb92b,
    0x5af5,0x4ad4,0x7ab7,0x6a96,0x1a71,0x0a50,0x3a33,0x2a12,
    0xdbfd,0xcbdc,0xfbbf,0xeb9e,0x9b79,0x8b58,0xbb3b,0xab1a,
    0x6ca6,0x7c87,0x4ce4,0x5cc5,0x2c22,0x3c03,0x0c60,0x1c41,
    0xedae,0xfd8f,0xcdec,0xddcd,0xad2a,0xbd0b,0x8d68,0x9d49,
    0x7e97,0x6eb6,0x5ed5,0x4ef4,0x3e13,0x2e32,0x1e51,0x0e70,
    0xff9f,0xefbe,0xdfdd,0xcffc,0xbf1b,0xaf3a,0x9f59,0x8f78,
    0x9188,0x81a9,0xb1ca,0xa1eb,0xd10c,0xc12d,0xf14e,0xe16f,
    0x1080,0x00a1,0x30c2,0x20e3,0x5004,0x4025,0x7046,0x6067,
    0x83b9,0x9398,0xa3fb,0xb3da,0xc33d,0xd31c,0xe37f,0xf35e,
    0x02b1,0x1290,0x22f3,0x32d2,0x4235,0x5214,0x6277,0x7256,
    0xb5ea,0xa5cb,0x95a8,0x8589,0xf56e,0xe54f,0xd52c,0xc50d,
    0x34e2,0x24c3,0x14a0,0x0481,0x7466,0x6447,0x5424,0x4405,
    0xa7db,0xb7fa,0x8799,0x97b8,0xe75f,0xf77e,0xc71d,0xd73c,
    0x26d3,0x36f2,0x0691,0x16b0,0x6657,0x7676,0x4615,0x5634,
    0xd94c,0xc96d,0xf90e,0xe92f,0x99c8,0x89e9,0xb98a,0xa9ab,
    0x5844,0x4865,0x7806,0x6827,0x18c0,0x08e1,0x3882,0x28a3,
    0xcb7d,0xdb5c,0xeb3f,0xfb1e,0x8bf9,0x9bd8,0xabbb,0xbb9a,
    0x4a75,0x5a54,0x6a37,0x7a16,0x0af1,0x1ad0,0x2ab3,0x3a92,
    0xfd2e,0xed0f,0xdd6c,0xcd4d,0xbdaa,0xad8b,0x9de8,0x8dc9,
    0x7c26,0x6c07,0x5c64,0x4c45,0x3ca2,0x2c83,0x1ce0,0x0cc1,
    0xef1f,0xff3e,0xcf5d,0xdf7c,0xaf9b,0xbfba,0x8fd9,0x9ff8,
    0x6e17,0x7e36,0x4e55,0x5e74,0x2e93,0x3eb2,0x0ed1,0x1ef0
};

/**
 * @brief 计算输入数据的 CRC16 值
 *
 * @param[in] buf       指向输入数据的缓冲区
 * @param[in] len       输入数据的长度（字节数）
 * 
 * @return 返回计算得到的 16 位 CRC 值
 */
uint16_t crc16_redis(const unsigned char *buf, int len) {
    int counter;
    uint16_t crc = 0;
    for (counter = 0; counter < len; counter++)
            crc = (crc<<8) ^ crc16tab[((crc>>8) ^ *buf++)&0x00FF];
    return crc;
}

// CRC16-CCITT 测试用例（多项式0x1021，初始值0xFFFF）
void test_crc16()
{
    const unsigned char empty[] = "";
    const unsigned char test_str[] = "123456789";

    // 空数据测试
    uint16_t crc = crc16(0, empty, 0);
    assert(crc == 0); // 初始值保持不变

    // 标准测试用例（CRC16-CCITT "123456789" 应得 0x29B1）
    crc = crc16(0, test_str, strlen((char *)test_str));

    uint16_t crc_standard = crc16_ccitt_standard(test_str, strlen((char *)test_str));

    uint16_t crc_redis = crc16_redis(test_str, strlen((char *)test_str));
    printf("CRC16 Result: 0x%04X (Expected: 0x29B1)\n", crc);
    assert(crc == 0x31C3);
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

/* ***************************************************************************************************************************** */
// CRC64-ECMA 测试用例（Redis使用的多项式）

#define POLY UINT64_C(0xad93d23594c935a9) // CRC64 多项式
static uint64_t crc64_table[8][256] = {{0}};                    // CRC64 表，利用 ​Slice-by-8 加速计算

static inline uint_fast64_t crc_reflect(uint_fast64_t data, uint8_t width)
{
    uint_fast64_t ret = data & 0x01;                            // 取最低位

    for (size_t i = 1; i < width; i++) {
        data >>= 1;
        ret = (ret << 1) | (data & 0x01);                       // 左移并拼接下一位
    }

    return ret;
}


uint64_t _crc64(uint_fast64_t crc, const void *in_data, const uint64_t len)
{
    const uint8_t *data = (const uint8_t *)in_data; // 输入数据指针
    unsigned long long bit;        // 临时变量，用于存储当前位

    for (uint64_t offset = 0; offset < len; offset++)
    {                             // 遍历每个字节
        uint8_t c = data[offset]; // 当前字节
        for (uint_fast8_t i = 0x01; i & 0xff; i <<= 1)
        {                                   // 遍历每个位
            bit = crc & 0x8000000000000000; // 取 CRC 的最高位
            if (c & i)
            {               // 如果当前位为 1
                bit = !bit; // 反转最高位
            }

            crc <<= 1; // 左移 CRC
            if (bit)
            {                // 如果最高位为 1
                crc ^= POLY; // 异或多项式
            }
        }

        crc &= 0xffffffffffffffff; // 确保 CRC 为 64 位
    }

    crc = crc & 0xffffffffffffffff;                   // 再次确保 CRC 为 64 位
    return crc_reflect(crc, 64) ^ 0x0000000000000000; // 返回最终 CRC 值
}

typedef uint64_t (*crcfn64)(uint64_t, const void *, const uint64_t);

void crcspeed64little_init(crcfn64 crcfn, uint64_t table[8][256])
{
    uint64_t crc;

    /* generate CRCs for all single byte sequences */
    /* 生成所有单字节序列的 CRC 值 */
    /* 遍历所有可能的单字节值（0-255） */
    for (int n = 0; n < 256; n++)
    {
        unsigned char v = n;           // 将当前值转换为单字节
        table[0][n] = crcfn(0, &v, 1); // 计算单字节的 CRC 值并存入表的第一层
    }

    /* generate nested CRC table for future slice-by-8 lookup */
    /* 生成嵌套的 CRC 表，用于后续的 Slice-by-8 查找 */
    /* 遍历所有单字节值，生成嵌套的 CRC 表 */
    for (int n = 0; n < 256; n++)
    {
        crc = table[0][n]; // 获取表的第一层中的 CRC 值
        for (int k = 1; k < 8; k++)
        {
            /* 通过异或和移位操作，生成下一层的 CRC 值 */
            crc = table[0][crc & 0xff] ^ (crc >> 8);
            table[k][n] = crc; // 将生成的 CRC 值存入表的第 k 层
        }
    }
}

uint64_t crcspeed64little(uint64_t little_table[8][256], uint64_t crc,
                          void *buf, size_t len)
{
    unsigned char *next = (unsigned char *)buf;

    /* process individual bytes until we reach an 8-byte aligned pointer */
    /* 处理单个字节，直到指针 8 字节对齐 */
    while (len && ((uintptr_t)next & 7) != 0)
    {
        crc = little_table[0][(crc ^ *next++) & 0xff] ^ (crc >> 8);
        len--;
    }

    /* fast middle processing, 8 bytes (aligned!) per loop */
    /* 快速中间处理，每次处理 8 字节（对齐！） */
    while (len >= 8)
    {
        crc ^= *(uint64_t *)next;
        crc = little_table[7][crc & 0xff] ^
              little_table[6][(crc >> 8) & 0xff] ^
              little_table[5][(crc >> 16) & 0xff] ^
              little_table[4][(crc >> 24) & 0xff] ^
              little_table[3][(crc >> 32) & 0xff] ^
              little_table[2][(crc >> 40) & 0xff] ^
              little_table[1][(crc >> 48) & 0xff] ^
              little_table[0][crc >> 56];
        next += 8;
        len -= 8;
    }

    /* process remaining bytes (can't be larger than 8) */
    /* 处理剩余字节（不超过 8 字节） */
    while (len)
    {
        crc = little_table[0][(crc ^ *next++) & 0xff] ^ (crc >> 8);
        len--;
    }

    return crc;
}

void crc64_init_redis(void)
{
    crcspeed64little_init(_crc64, crc64_table); // 调用 crcspeed64native_init 函数，填充 crc64_table
}

uint64_t crc64_redis(uint64_t crc, const unsigned char *s, uint64_t l)
{
    return crcspeed64little(crc64_table, crc, (void *)s, l); // 调用 crcspeed64native 函数，使用预计算的 CRC 表加速计算
}

void test_crc64()
{
    const unsigned char empty[] = "";
    const unsigned char test_str[] = "123456789";

    // 空数据测试
    uint64_t crc = crc64(0, empty, 0);
    assert(crc == 0); // 初始值为0，结果保持0

    // Redis CRC64测试用例（"123456789" 应得 0xE9C6D914C4B8D9CA）
    crc = crc64(0,  (unsigned char*)"123456789", 9);
    // printf("CRC64 Result: 0x%016llX (Expected: 0xE9C6D914C4B8D9CA)\n", crc);
    assert(crc == 0xE9C6D914C4B8D9CA);

    char li[] = "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed "
                "do eiusmod tempor incididunt ut labore et dolore magna "
                "aliqua. Ut enim ad minim veniam, quis nostrud exercitation "
                "ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis "
                "aute irure dolor in reprehenderit in voluptate velit esse "
                "cillum dolore eu fugiat nulla pariatur. Excepteur sint "
                "occaecat cupidatat non proident, sunt in culpa qui officia "
                "deserunt mollit anim id est laborum.";

    crc64_init_redis();

    // write_crc64_table_to_file("./redis_table.txt", crc64_table, 8);
    uint64_t crc_redis = crc64_redis(0, (unsigned char*)li, sizeof(li));
    
    crc = crc64(0, (unsigned char*)li, sizeof(li));
    assert(crc == 0xc7794709e69683b3);
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