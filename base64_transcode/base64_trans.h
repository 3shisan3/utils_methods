#pragma once

#include <string>
#include <vector>

const std::string base64Chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* std::string base64_decode(const std::string& encodedString) {
    std::vector<unsigned char> decodedBytes;
    decodedBytes.reserve(encodedString.size());

    size_t padding = 0;
    for (char c : encodedString) {
        if (c == '=')
            padding++;
        else {
            size_t index = base64Chars.find(c);
            if (index != std::string::npos)
                decodedBytes.push_back(static_cast<unsigned char>(index));
        }
    }

    const size_t outputSize = (decodedBytes.size() / 4) * 3 - padding;
    std::string decodedString;
    decodedString.reserve(outputSize);

    for (size_t i = 0; i < decodedBytes.size(); i += 4) {
        unsigned char a = decodedBytes[i];
        unsigned char b = decodedBytes[i + 1];
        unsigned char c = decodedBytes[i + 2];
        unsigned char d = decodedBytes[i + 3];

        unsigned char byte1 = (a << 2) | (b >> 4);
        unsigned char byte2 = (b << 4) | (c >> 2);
        unsigned char byte3 = (c << 6) | d;

        decodedString.push_back(byte1);

        if (i + 2 < decodedBytes.size())
            decodedString.push_back(byte2);

        if (i + 3 < decodedBytes.size())
            decodedString.push_back(byte3);
    }

    return decodedString;
} */

static std::string base64_encode(const std::string &str)
{
    std::string ret;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    int len = str.size();
    int index = 0;

    while (len--)
    {
        char_array_3[i++] = str[index++];
        if (i == 3)
        {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; (i < 4); i++)
                ret += base64Chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i)
    {
        for (j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (j = 0; (j < i + 1); j++)
            ret += base64Chars[char_array_4[j]];

        while ((i++ < 3))
            ret += '=';
    }

    return ret;
}

static inline bool is_base64(unsigned char c)
{
    return (isalnum(c) || (c == '+') || (c == '/'));
}

static std::string base64_decode(const std::string &encoded_string)
{
    int in_len = encoded_string.size();
    int i = 0;
    int j = 0;
    int in_ = 0;
    unsigned char char_array_4[4], char_array_3[3];
    std::string ret;

    while (in_len-- && (encoded_string[in_] != '=') && is_base64(encoded_string[in_]))
    {
        char_array_4[i++] = encoded_string[in_];
        in_++;
        if (i == 4)
        {
            for (i = 0; i < 4; i++)
                char_array_4[i] = base64Chars.find(char_array_4[i]);

            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

            for (i = 0; (i < 3); i++)
                ret += char_array_3[i];
            i = 0;
        }
    }

    if (i)
    {
        for (j = i; j < 4; j++)
            char_array_4[j] = 0;

        for (j = 0; j < 4; j++)
            char_array_4[j] = base64Chars.find(char_array_4[j]);

        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

        for (j = 0; (j < i - 1); j++)
            ret += char_array_3[j];
    }

    return ret;
}