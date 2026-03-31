/***************************************************************
Copyright (c) 2022-2030, shisan233@sszc.live.
SPDX-License-Identifier: MIT 
File:        kalman_filter_types.h
Version:     1.0
Author:      cjx
start date: 2026-3-28
Description: 卡尔曼滤波器通用类型定义
Version history

[序号]    |   [修改日期]  |   [修改者]   |   [修改内容]
1             2026-3-28      cjx            create

*****************************************************************/

#ifndef FUSION_KALMAN_FILTER_TYPES_H
#define FUSION_KALMAN_FILTER_TYPES_H

#include <cmath>
#include <limits>
#include <stdexcept>

#include <Eigen/Dense>

namespace fusion {

// 常用类型别名
using Vector2d = Eigen::Vector2d;
using Vector3d = Eigen::Vector3d;
using Vector4d = Eigen::Vector4d;
using VectorXd = Eigen::VectorXd;

using Matrix2d = Eigen::Matrix2d;
using Matrix3d = Eigen::Matrix3d;
using Matrix4d = Eigen::Matrix4d;
using MatrixXd = Eigen::MatrixXd;

// 常量定义
constexpr double PI = 3.14159265358979323846;
constexpr double DEG_TO_RAD = PI / 180.0;
constexpr double RAD_TO_DEG = 180.0 / PI;

/**
 * @brief 确保矩阵对称
 */
template<typename MatrixType>
void enforceSymmetry(MatrixType& P) {
    P = (P + P.transpose()) / 2.0;
}

/**
 * @brief 确保协方差矩阵正定
 */
template<typename MatrixType>
void enforcePositiveDefinite(MatrixType& P, double eps = 1e-12) {
    for (int i = 0; i < P.rows(); ++i) {
        if (P(i, i) < eps) {
            P(i, i) = eps;
        }
    }
}

/**
 * @brief 马氏距离计算
 */
template<typename VectorType, typename MatrixType>
double mahalanobisDistance(const VectorType& y, const MatrixType& S) {
    return std::sqrt(y.transpose() * S.inverse() * y);
}

} // namespace fusion

#endif // FUSION_KALMAN_FILTER_TYPES_H