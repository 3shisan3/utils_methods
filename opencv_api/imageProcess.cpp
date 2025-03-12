#include "imageProcess.h"

#include <iostream>

namespace cvbag
{

//-------------------------------------- 基础工具实现 --------------------------------------
bool isImageEmpty(const cv::Mat &image, const std::string &functionName)
{
    if (image.empty())
    {
        std::cerr << "Error in " << functionName
                  << ": Empty input image!" << std::endl;
        return true;
    }
    return false;
}

int getAllImagePath(const std::string &folder,
                    std::vector<cv::String> &imagePathList,
                    bool recursive)
{
    if (folder.empty())
    {
        std::cerr << "Error: Empty folder path!" << std::endl;
        return -1;
    }

    try
    {
        cv::glob(folder, imagePathList, recursive);
        if (imagePathList.empty())
        {
            std::cerr << "Warning: No images found in " << folder << std::endl;
        }
    }
    catch (const cv::Exception &e)
    {
        std::cerr << "OpenCV Exception: " << e.what() << std::endl;
        return -1;
    }
    return 0;
}

//-------------------------------------- 图像显示实现 --------------------------------------
int showImage(const cv::Mat &image,
              const std::string &winName,
              int waitKeyMs,
              bool destroyWindow)
{
    if (isImageEmpty(image, "showImage"))
        return -1;

    cv::namedWindow(winName, cv::WINDOW_NORMAL);
    cv::imshow(winName, image);

    if (waitKeyMs >= 0)
    {
        cv::waitKey(waitKeyMs);
    }
    else
    {
        std::cerr << "Invalid waitKey value: " << waitKeyMs << std::endl;
    }

    if (destroyWindow)
    {
        cv::destroyWindow(winName);
    }
    return 0;
}

//-------------------------------------- 图像滤波实现 --------------------------------------
int gaussBlur(const cv::Mat &image, cv::Mat &dst,
              int ksize, double sigma)
{
    if (isImageEmpty(image, "gaussBlur"))
        return -1;

    // 确保核大小为奇数
    if (ksize % 2 == 0)
    {
        std::cerr << "Warning: Gaussian kernel size should be odd. Adding 1." << std::endl;
        ksize += 1;
    }

    cv::GaussianBlur(image, dst, cv::Size(ksize, ksize), sigma);
    return 0;
}

//-------------------------------------- 边缘检测实现 --------------------------------------
int sobelX(const cv::Mat &image, cv::Mat &dst, int ksize)
{
    if (isImageEmpty(image, "sobelX"))
        return -1;

    cv::Mat gray;
    if (image.channels() > 1)
    {
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    }
    else
    {
        gray = image.clone();
    }

    cv::Sobel(gray, dst, CV_64F, 1, 0, ksize);
    cv::convertScaleAbs(dst, dst); // 转换为8位图像
    return 0;
}

int sobelY(const cv::Mat &image, cv::Mat &dst, int ksize)
{
    if (isImageEmpty(image, "sobel_y"))
        return -1;

    cv::Mat gray;
    if (image.channels() == 3)
    {
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    }
    else
    {
        gray = image.clone();
    }

    cv::Sobel(gray, dst, CV_64F, 0, 1, ksize); // Y方向梯度
    cv::convertScaleAbs(dst, dst);
    return 0;
}

int sobelXY(const cv::Mat &image, cv::Mat &dst, int ksize)
{
    cv::Mat grad_x, grad_y;
    if (sobelX(image, grad_x, ksize) != 0)
        return -1;
    if (sobelY(image, grad_y, ksize) != 0)
        return -1;

    cv::addWeighted(grad_x, 0.5, grad_y, 0.5, 0, dst);
    return 0;
}

int cannyEdge(const cv::Mat &image, cv::Mat &dst, int low, int high)
{
    if (isImageEmpty(image, "canny"))
        return -1;

    cv::Mat gray;
    if (image.channels() == 3)
    {
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    }
    else
    {
        gray = image.clone();
    }

    cv::Canny(gray, dst, low, high);
    return 0;
}

// ================================= 阈值处理实现 =================================
int otsuThreshold(const cv::Mat &image, cv::Mat &dst)
{
    if (isImageEmpty(image, "otsu"))
        return -1;

    cv::Mat gray;
    if (image.channels() == 3)
    {
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    }
    else
    {
        gray = image.clone();
    }

    cv::threshold(gray, dst, 0, 255, cv::THRESH_OTSU);
    return 0;
}

int fixedThreshold(const cv::Mat &image, cv::Mat &dst,
                          int th, int mode, int maxval)
{
    if (isImageEmpty(image, "threshold"))
        return -1;

    cv::Mat gray;
    if (image.channels() == 3)
    {
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    }
    else
    {
        gray = image.clone();
    }

    cv::threshold(gray, dst, th, maxval, mode);
    return 0;
}

int adaptiveThreshold(const cv::Mat &image, cv::Mat &dst, int blockSize,
                      double C, int adaptiveMethod, int thresholdType)
{
    if (isImageEmpty(image, "adaptiveThreshold"))
        return -1;

    cv::Mat gray;
    if (image.channels() == 3)
    {
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    }
    else
    {
        gray = image.clone();
    }

    if (blockSize % 2 == 0)
        blockSize += 1;
    cv::adaptiveThreshold(gray, dst, 255, adaptiveMethod, thresholdType, blockSize, C);
    return 0;
}

//-------------------------------------- 形态学操作实现 --------------------------------------
int dilate(const cv::Mat &binImg, cv::Mat &dst,
           int ksize, int shape)
{
    if (isImageEmpty(binImg, "dilate"))
        return -1;
    if (binImg.channels() != 1)
    {
        std::cerr << "Error: dilate requires binary image!" << std::endl;
        return -1;
    }

    cv::Mat kernel = cv::getStructuringElement(shape, cv::Size(ksize, ksize));
    cv::dilate(binImg, dst, kernel);
    return 0;
}

int morphOpen(const cv::Mat &binaryImage, cv::Mat &dst, int ksize, int kernelMode)
{
    cv::Mat temp;
    if (erode(binaryImage, temp, ksize, kernelMode) != 0)
        return -1;
    return dilate(temp, dst, ksize, kernelMode);
}

int erode(const cv::Mat &binaryImage, cv::Mat &dst, int ksize, int kernelMode)
{
    // 参数验证
    if (isImageEmpty(binaryImage, "erode"))
        return -1;
    if (binaryImage.channels() != 1)
    {
        std::cerr << "ERROR: erode requires single-channel image!" << std::endl;
        return -1;
    }
    if (ksize % 2 == 0)
    {
        ksize += 1; // 确保核大小为奇数
        std::cerr << "WARNING: Kernel size adjusted to " << ksize << std::endl;
    }

    // 创建结构元素
    cv::Mat kernel = cv::getStructuringElement(
        kernelMode,
        cv::Size(ksize, ksize));

    // 执行腐蚀操作
    cv::erode(binaryImage, dst, kernel);
    return 0;
}

int close(const cv::Mat &binaryImage, cv::Mat &dst, int ksize, int kernelMode)
{
    // 参数验证
    if (isImageEmpty(binaryImage, "close"))
        return -1;
    if (binaryImage.channels() != 1)
    {
        std::cerr << "ERROR: close requires single-channel image!" << std::endl;
        return -1;
    }

    // 创建结构元素
    cv::Mat kernel = cv::getStructuringElement(
        kernelMode,
        cv::Size(ksize, ksize));

    // 执行闭运算（先膨胀后腐蚀）
    cv::morphologyEx(binaryImage, dst, cv::MORPH_CLOSE, kernel);
    return 0;
}

// ================================= 轮廓处理实现 =================================
int findContours(const cv::Mat &binaryImage,
                 std::vector<std::vector<cv::Point>> &contours,
                 ContourConfig config)
{
    if (isImageEmpty(binaryImage, "findContours"))
        return -1;

    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(binaryImage, contours, hierarchy,
                     config.retrievalMode, config.approximationMethod);
    return 0;
}

int drawContours(cv::Mat &image,
                 const std::vector<std::vector<cv::Point>> &contours,
                 ContourConfig config)
{
    if (isImageEmpty(image, "drawContours"))
        return -1;

    cv::drawContours(image, contours, -1,
                     config.color, config.thickness);
    return 0;
}

//-------------------------------------- Gamma变换实现 --------------------------------------
int gammaCorrect(const cv::Mat &image, cv::Mat &dst, double gamma)
{
    if (isImageEmpty(image, "gammaCorrect"))
        return -1;

    // 创建查找表
    cv::Mat lookupTable(1, 256, CV_8U);
    uchar *lut = lookupTable.ptr();
    for (int i = 0; i < 256; ++i)
    {
        lut[i] = cv::saturate_cast<uchar>(pow(i / 255.0, gamma) * 255.0);
    }

    // 应用查找表
    cv::LUT(image, lookupTable, dst);
    return 0;
}

int gamma_picewiseLinear(const cv::Mat &image, cv::Mat &dst,
                         int src1, int dst1, int src2, int dst2)
{
    if (isImageEmpty(image, "gamma_picewiseLinear"))
        return -1;

    cv::Mat lookupTable(1, 256, CV_8U);
    uchar *lut = lookupTable.ptr();

    // 分段线性变换
    for (int i = 0; i < 256; ++i)
    {
        if (i < src1)
        {
            lut[i] = cv::saturate_cast<uchar>(dst1 * i / src1);
        }
        else if (i < src2)
        {
            lut[i] = cv::saturate_cast<uchar>(dst1 + (dst2 - dst1) * (i - src1) / (src2 - src1));
        }
        else
        {
            lut[i] = cv::saturate_cast<uchar>(dst2 + (255 - dst2) * (i - src2) / (255 - src2));
        }
    }

    cv::LUT(image, lookupTable, dst);
    return 0;
}

} // namespace cvbag