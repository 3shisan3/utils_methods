#include "imageProcess.h"

#include <iostream>
#include <string>

const std::string testImgPath = "../res/Yuan Shen 原神 Screenshot 2025.01.29 - 04.19.08.79.png";

using namespace cvbag;

// 测试保存功能
void testSaveFunctions(const cv::Mat &src)
{
    // 保存原始图像
    int ret = saveImage(src, "output/original.jpg", {cv::IMWRITE_JPEG_QUALITY, 95});
    if (ret != 0)
    {
        std::cerr << "保存原始图像失败，错误码: " << ret << std::endl;
    }

    // 保存处理后的图像
    cv::Mat processed;
    if (sobelXY(src, processed, 3) == 0)
    {
        ret = saveImage(processed, "output/edges.png", {cv::IMWRITE_PNG_COMPRESSION, 9});
        if (ret != 0)
        {
            std::cerr << "保存边缘检测结果失败，错误码: " << ret << std::endl;
        }
    }
}

int main()
{

    // 加载测试图像
    cv::Mat src = cv::imread(testImgPath);
    if (src.empty())
    {
        std::cerr << "Error: Cannot load test image at " << testImgPath << std::endl;
        return -1;
    }

    // ------------------------- 测试1: 基础显示 -------------------------
    std::cout << "\n=== 测试1: 图像显示 ===" << std::endl;
    int ret = showImage(src, "Original Image", 2000);
    if (ret != 0)
    {
        std::cerr << "显示测试失败!" << std::endl;
    }

    // ------------------------- 测试2: 高斯模糊 ------------------------
    std::cout << "\n=== 测试2: 高斯模糊 ===" << std::endl;
    cv::Mat blurred;
    if (gaussBlur(src, blurred, 15, 2.0) == 0)
    {
        showImage(blurred, "Gaussian Blur", 1500);
    }
    else
    {
        std::cerr << "高斯模糊失败!" << std::endl;
    }

    // ------------------------- 测试3: Sobel边缘检测 --------------------
    std::cout << "\n=== 测试3: Sobel边缘检测 ===" << std::endl;
    cv::Mat edges;
    if (sobelXY(src, edges, 3) == 0)
    {
        showImage(edges, "Sobel Edges", 1500);
    }
    else
    {
        std::cerr << "边缘检测失败!" << std::endl;
    }

    // ------------------------- 测试4: Otsu阈值分割 ----------------------
    std::cout << "\n=== 测试4: Otsu阈值 ===" << std::endl;
    cv::Mat binary;
    if (otsuThreshold(src, binary) == 0)
    {
        showImage(binary, "Otsu Binary", 1500);
    }
    else
    {
        std::cerr << "阈值分割失败!" << std::endl;
    }

    // ------------------------- 测试5: 形态学开运算 ---------------------
    std::cout << "\n=== 测试5: 形态学开运算 ===" << std::endl;
    cv::Mat opened;
    if (morphOpen(binary, opened, 5, cv::MORPH_ELLIPSE) == 0)
    {
        showImage(opened, "Morph Open", 1500);
    }
    else
    {
        std::cerr << "形态学操作失败!" << std::endl;
    }

    // ------------------------- 测试6: Gamma校正 -------------------------
    std::cout << "\n=== 测试6: Gamma校正 ===" << std::endl;
    cv::Mat gamma;
    if (gammaCorrect(src, gamma, 2.2) == 0)
    {
        showImage(gamma, "Gamma Corrected", 1500);
    }
    else
    {
        std::cerr << "Gamma校正失败!" << std::endl;
    }

    // 测试保存功能
    testSaveFunctions(src);

    return 0;
}