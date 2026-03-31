/***************************************************************
Copyright (c) 2022-2030, shisan233@sszc.live.
SPDX-License-Identifier: MIT
File:        linear_kalman_filter.h
Version:     1.0
Author:      cjx
start date: 2026-3-28
Description: 线性卡尔曼滤波器
    状态向量: [x, y, vx, vy]^T  适用于匀速运动模型的线性系统
Version history

[序号]    |   [修改日期]  |   [修改者]   |   [修改内容]
1             2026-3-28      cjx            create

*****************************************************************/

#ifndef FUSION_LINEAR_KALMAN_FILTER_H
#define FUSION_LINEAR_KALMAN_FILTER_H

#include "kalman_filter_types.h"

namespace fusion {

/**
 * @brief 线性卡尔曼滤波器类
 *
 * 使用条件：
 * 1. 系统是线性的
 * 2. 噪声是高斯白噪声
 *
 * 状态方程: x_k = F * x_{k-1} + w_k
 * 观测方程: z_k = H * x_k + v_k
 */
class LinearKalmanFilter
{
public:
    using VectorState = Vector4d;
    using MatrixState = Matrix4d;
    using VectorObs = Vector2d;
    using MatrixObs = Matrix2d;

    /**
     * @brief 构造函数
     */
    LinearKalmanFilter();

    /**
     * @brief 初始化滤波器
     * @param x0 初始位置X (m)
     * @param y0 初始位置Y (m)
     * @param vx0 初始速度X (m/s)
     * @param vy0 初始速度Y (m/s)
     * @param P0 初始协方差矩阵（可选）
     */
    void initialize(double x0, double y0, double vx0, double vy0,
                    const MatrixState &P0 = MatrixState::Identity() * 100.0);

    /**
     * @brief 使用向量初始化
     */
    void initialize(const VectorState &x0, const MatrixState &P0);

    /**
     * @brief 预测步骤
     * @param dt 时间间隔（秒）
     * @param Q 过程噪声协方差矩阵
     */
    void predict(double dt, const MatrixState &Q);

    /**
     * @brief 更新步骤（位置观测）
     * @param z 观测位置 (x, y)
     * @param R 观测噪声协方差矩阵
     */
    void update(const VectorObs &z, const MatrixObs &R);

    /**
     * @brief 完整状态更新（位置+速度观测）
     * @param z 观测状态 (x, y, vx, vy)
     * @param R 观测噪声协方差矩阵
     */
    void updateFull(const VectorState &z, const MatrixState &R);

    // ========== 状态获取 ==========
    const VectorState &getState() const { return x_; }
    const MatrixState &getCovariance() const { return P_; }
    VectorObs getPosition() const { return x_.head<2>(); }
    VectorObs getVelocity() const { return x_.tail<2>(); }
    double getSpeed() const { return x_.tail<2>().norm(); }
    double getCourse() const;
    bool isInitialized() const { return initialized_; }

    // ========== 预测功能 ==========
    VectorObs predictPosition(double dt) const;

    // ========== 不确定性 ==========
    double getPositionUncertainty() const;
    double getVelocityUncertainty() const;

    // ========== 异常检测 ==========
    double getMahalanobisDistance(const VectorObs &z, const MatrixObs &R) const;
    VectorObs getResidual(const VectorObs &z) const;

    // ========== 其他 ==========
    void reset();

private:
    void updateInternal(const VectorState &z, const MatrixState &R, const MatrixState &H);
    void enforceNumericalStability();

    VectorState x_;    // 状态向量 [x, y, vx, vy]
    MatrixState P_;    // 协方差矩阵
    bool initialized_; // 是否已初始化
};

} // namespace fusion

#endif // FUSION_LINEAR_KALMAN_FILTER_H