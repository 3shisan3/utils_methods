/***************************************************************
Copyright (c) 2022-2030, shisan233@sszc.live.
All rights reserved.
File:
Version:     1.0
Author:      cjx
start date:
Description: 围绕OpenCV的图像处理函数进行封装
Version history

[序号]    |   [修改日期]  |   [修改者]   |   [修改内容]
1             2025-3-12      cjx        create

*****************************************************************/

#pragma once

#include <opencv2/highgui.hpp>
#include <opencv2/opencv.hpp>
#include <vector>
#include <string>

/**
 * @namespace cvbag
 * @brief 图像处理工具函数集合 (适配OpenCV 4)
 */
namespace cvbag
{

//-------------------------------------- 基础工具 --------------------------------------

/**
 * @brief 检查图像是否为空
 * @param image 输入图像
 * @param functionName 调用函数名称(用于错误提示)
 * @return true: 空图像, false: 有效图像
 */
bool isImageEmpty(const cv::Mat &image, const std::string &functionName);

/**
 * @brief 获取目录下所有图像路径
 * @param folder 目录路径
 * @param[out] imagePathList 输出路径列表
 * @param recursive 是否递归搜索子目录
 * @return 0: 成功, -1: 路径错误
 */
int getAllImagePath(const std::string &folder,
                    std::vector<cv::String> &imagePathList,
                    bool recursive = false);

/**
 * @brief 保存图像到本地文件
 * @param image 输入图像 (支持所有OpenCV可写格式)
 * @param filePath 保存路径 (需包含扩展名如 .jpg, .png)
 * @param params 编码参数 (可选，如JPEG质量参数等)
 * @return 0-成功，-1-输入为空，-2-路径无效，-3-编码失败
 */
int saveImage(const cv::Mat &image, 
              const std::string &filePath,
              const std::vector<int>& params = {});

//-------------------------------------- 图像显示 --------------------------------------

/**
 * @brief 显示图像窗口
 * @param image 输入图像
 * @param winName 窗口名称(唯一标识)
 * @param waitKeyMs 等待时间(毫秒), 0表示阻塞
 * @param destroyWindow 是否自动销毁窗口
 * @return 0: 成功, -1: 空图像
 */
int showImage(const cv::Mat &image,
              const std::string &winName = "Image",
              int waitKeyMs = 0,
              bool destroyWindow = true);

//-------------------------------------- 图像滤波 --------------------------------------

/**
 * @brief 高斯模糊
 * @param image 输入图像
 * @param[out] dst 输出图像
 * @param ksize 高斯核尺寸(正奇数)
 * @param sigma 标准差(0表示自动计算)
 * @return 0: 成功, -1: 空图像
 */
int gaussBlur(const cv::Mat &image, cv::Mat &dst,
              int ksize = 3, double sigma = 0);

//-------------------------------------- 边缘检测 --------------------------------------

/**
 * @brief Sobel水平梯度计算
 * @param image 输入图像(推荐灰度图)
 * @param[out] dst 输出梯度图(CV_64F类型)
 * @param ksize 核大小(1/3/5/7)
 * @return 0: 成功, -1: 空图像
 */
int sobelX(const cv::Mat &image, cv::Mat &dst, int ksize = 3);

/**
 * @brief Sobel垂直梯度计算
 * @param image 输入图像(自动转换为灰度)
 * @param[out] dst 输出梯度图(CV_8UC1)
 * @param ksize Sobel核大小(有效值:1/3/5/7，默认3)
 * @note 输出图像经过绝对值转换和缩放
 */
int sobelY(const cv::Mat &image, cv::Mat &dst, int ksize = 3);

/**
 * @brief 综合梯度计算
 * @param image 输入图像
 * @param[out] dst 输出梯度图
 * @param ksize Sobel核大小
 * @return 0: 成功, -1: 空图像
 */
int sobelXY(const cv::Mat &image, cv::Mat &dst, int ksize = 3);

/**
 * @brief Canny边缘检测
 * @param image 输入图像
 * @param[out] dst 输出二值边缘图
 * @param low 低阈值
 * @param high 高阈值
 * @return 0: 成功, -1: 空图像
 */
int cannyEdge(const cv::Mat &image, cv::Mat &dst,
              int low = 50, int high = 150);

//-------------------------------------- 阈值处理 --------------------------------------

/**
 * @brief Otsu自动阈值分割
 * @param image 输入图像(自动转灰度)
 * @param[out] dst 输出二值图
 * @return 0: 成功, -1: 空图像
 */
int otsuThreshold(const cv::Mat &image, cv::Mat &dst);

/**
 * @brief 固定阈值分割
 * @param image 输入图像
 * @param[out] dst 输出图像
 * @param thresh 阈值
 * @param maxval 最大值
 * @param type 阈值类型(THRESH_BINARY等)
 * @return 0: 成功, -1: 空图像
 */
int fixedThreshold(const cv::Mat &image, cv::Mat &dst,
                   int thresh = 127, int maxval = 255,
                   int type = cv::THRESH_BINARY);

/**
 * @brief 自适应阈值分割
 * @param blockSize 邻域大小(奇数)
 * @param C 从均值/高斯加权值中减去的常数
 * @param adaptiveMethod 自适应方法(cv::ADAPTIVE_THRESH_MEAN_C/cv::ADAPTIVE_THRESH_GAUSSIAN_C)
 * @param thresholdType 阈值类型(cv::THRESH_BINARY/cv::THRESH_BINARY_INV)
 * @note 自动处理彩色图像转灰度，自动校正blockSize为奇数
 */
int adaptiveThreshold(const cv::Mat &image, cv::Mat &dst, int blockSize = 11,
                      double C = 2, int adaptiveMethod = cv::ADAPTIVE_THRESH_GAUSSIAN_C,
                      int thresholdType = cv::THRESH_BINARY);

//-------------------------------------- 形态学操作 --------------------------------------

/**
 * @brief 膨胀操作
 * @param binImg 输入二值图像
 * @param[out] dst 输出图像
 * @param ksize 结构元素尺寸(奇数)
 * @param shape 结构元素类型(MORPH_RECT等)
 * @return 0: 成功, -1: 无效输入
 */
int dilate(const cv::Mat &binImg, cv::Mat &dst,
           int ksize = 3, int shape = cv::MORPH_RECT);

/**
 * @brief 开运算
 * @param binImg 输入二值图像
 * @param[out] dst 输出图像
 * @param ksize 结构元素尺寸
 * @param shape 结构元素类型
 * @return 0: 成功, -1: 无效输入
 */
int morphOpen(const cv::Mat &binImg, cv::Mat &dst,
              int ksize = 3, int shape = cv::MORPH_RECT);

/**
 * @brief 腐蚀操作
 * @param binImg 输入二值图像(单通道)
 * @param[out] dst 输出图像
 * @param ksize 结构元素尺寸(建议奇数)
 * @param shape 结构元素类型(cv::MORPH_RECT/cv::MORPH_CROSS/cv::MORPH_ELLIPSE)
 * @return 0: 成功, -1: 输入为空或非单通道图像
 */
int erode(const cv::Mat &binImg, cv::Mat &dst,
          int ksize = 3, int shape = cv::MORPH_RECT);

/**
 * @brief 闭运算
 * @param binImg 输入二值图像
 * @param[out] dst 输出图像
 * @param ksize 结构元素尺寸(建议奇数)
 * @param shape 结构元素类型
 * @return 0: 成功, -1: 输入无效
 */
int close(const cv::Mat &binImg, cv::Mat &dst,
          int ksize = 3, int shape = cv::MORPH_RECT);

// ================================= 轮廓处理 =================================
struct ContourConfig
{
    int retrievalMode = cv::RETR_EXTERNAL;
    int approximationMethod = cv::CHAIN_APPROX_SIMPLE;
    cv::Scalar color = cv::Scalar(0, 255, 0);
    int thickness = 2;
};

/**
 * @brief 查找轮廓
 * @param binaryImage 输入二值图像(单通道)
 * @param[out] contours 输出轮廓点集
 * @param config 配置参数(检索模式/近似方法)
 * @return 0: 成功, -1: 输入图像无效
 * @see cv::findContours
 */
int findContours(const cv::Mat &binaryImage,
                 std::vector<std::vector<cv::Point>> &contours,
                 ContourConfig config = ContourConfig());

/**
 * @brief 绘制轮廓
 * @param image 输入输出图像(三通道BGR格式)
 * @param contours 要绘制的轮廓集合
 * @param config 绘制配置(颜色/线宽)
 * @return 0: 成功, -1: 输入图像无效
 * @note 会直接修改输入图像
 */
int drawContours(cv::Mat &image,
                 const std::vector<std::vector<cv::Point>> &contours,
                 ContourConfig config = ContourConfig());

//-------------------------------------- Gamma变换 --------------------------------------

/**
 * @brief Gamma校正
 * @param image 输入图像
 * @param[out] dst 输出图像
 * @param gamma 校正系数(>1变暗, <1变亮)
 * @return 0: 成功, -1: 空图像
 */
int gammaCorrect(const cv::Mat &image, cv::Mat &dst, double gamma = 1.0);

/**
 * @brief 分段线性Gamma校正
 * @param image 输入图像(支持三通道/单通道)
 * @param[out] dst 输出图像
 * @param src1 第一段输入拐点(0-255)
 * @param dst1 第一段输出拐点(0-255)
 * @param src2 第二段输入拐点(src1 < src2 < 255)
 * @param dst2 第二段输出拐点(dst1 < dst2 < 255)
 * @return 0: 成功, -1: 输入参数不合法
 * @note 实现三段式线性变换：前段(src1,dst1)/中段(src2,dst2)/后段
 */
int gamma_picewiseLinear(const cv::Mat &image, cv::Mat &dst,
                         int src1 = 50, int dst1 = 30,
                         int src2 = 200, int dst2 = 220);

} // namespace cvbag