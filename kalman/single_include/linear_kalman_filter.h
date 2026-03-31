/***************************************************************
Copyright (c) 2022-2030, shisan233@sszc.live.
SPDX-License-Identifier: MIT 
File:        linear_kalman_filter.h
Version:     1.0
Author:      cjx
start date: 2026-3-28
Description: 线性卡尔曼滤波器实现
    适用于线性高斯系统，状态向量为 [x, y, vx, vy]
Version history

[序号]    |   [修改日期]  |   [修改者]   |   [修改内容]
1             2026-3-28      cjx            create

*****************************************************************/

#ifndef FUSION_LINEAR_KALMAN_FILTER_H
#define FUSION_LINEAR_KALMAN_FILTER_H

#include <cmath>
#include <limits>
#include <stdexcept>

#include <Eigen/Dense>

namespace fusion {

/**
 * @brief 线性卡尔曼滤波器类
 * 
 * 状态方程: x_k = F * x_{k-1} + w_k, w_k ~ N(0, Q)
 * 观测方程: z_k = H * x_k + v_k, v_k ~ N(0, R)
 * 
 * 状态向量: [x, y, vx, vy]^T
 *   - x, y: 位置坐标（米）
 *   - vx, vy: 速度分量（米/秒）
 */
class LinearKalmanFilter {
public:
    // 类型定义，方便使用
    using Vector4d = Eigen::Vector4d;
    using Matrix4d = Eigen::Matrix4d;
    using Vector2d = Eigen::Vector2d;
    using Matrix2d = Eigen::Matrix2d;
    
    /**
     * @brief 构造函数
     */
    LinearKalmanFilter() : initialized_(false) {
        x_.setZero();
        P_.setIdentity();
    }
    
    /**
     * @brief 初始化滤波器
     * @param x0 初始X位置（米）
     * @param y0 初始Y位置（米）
     * @param vx0 初始X速度（米/秒）
     * @param vy0 初始Y速度（米/秒）
     * @param P0 初始协方差矩阵（默认对角矩阵，对角线元素100）
     */
    void initialize(double x0, double y0, double vx0, double vy0,
                    const Matrix4d& P0 = Matrix4d::Identity() * 100.0) {
        x_(0) = x0;
        x_(1) = y0;
        x_(2) = vx0;
        x_(3) = vy0;
        P_ = P0;
        initialized_ = true;
        
        // 确保协方差矩阵对称
        P_ = (P_ + P_.transpose()) / 2.0;
    }
    
    /**
     * @brief 预测步骤
     * @param dt 时间间隔（秒），必须为正数
     * @param Q 过程噪声协方差矩阵
     * @throws std::invalid_argument 当dt <= 0时抛出
     */
    void predict(double dt, const Matrix4d& Q) {
        if (!initialized_) {
            return;
        }
        
        if (dt <= 0) {
            throw std::invalid_argument("LinearKalmanFilter::predict: dt must be positive");
        }
        
        // 状态转移矩阵（匀速运动模型）
        Matrix4d F = Matrix4d::Identity();
        F(0, 2) = dt;  // x = x + vx * dt
        F(1, 3) = dt;  // y = y + vy * dt
        
        // 状态预测：x_k = F * x_{k-1}
        x_ = F * x_;
        
        // 协方差预测：P_k = F * P_{k-1} * F^T + Q
        P_ = F * P_ * F.transpose() + Q;
        
        // 保持协方差矩阵的对称性（数值稳定性）
        P_ = (P_ + P_.transpose()) / 2.0;
    }
    
    /**
     * @brief 更新步骤（使用位置观测）
     * @param z 观测位置 (x, y)
     * @param R 观测噪声协方差矩阵
     * @throws std::runtime_error 当创新协方差矩阵奇异时抛出
     */
    void update(const Vector2d& z, const Matrix2d& R) {
        if (!initialized_) {
            return;
        }
        
        // 观测矩阵：只观测位置
        Eigen::Matrix<double, 2, 4> H;
        H << 1, 0, 0, 0,
             0, 1, 0, 0;
        
        // 创新（残差）：z - H*x
        Vector2d y = z - H * x_;
        
        // 创新协方差：S = H*P*H^T + R
        Eigen::Matrix<double, 2, 2> S = H * P_ * H.transpose() + R;
        
        // 检查矩阵是否可逆
        if (S.determinant() < 1e-12) {
            throw std::runtime_error("LinearKalmanFilter::update: Innovation covariance matrix is singular");
        }
        
        // 卡尔曼增益：K = P*H^T * S^{-1}
        Eigen::Matrix<double, 4, 2> K = P_ * H.transpose() * S.inverse();
        
        // 状态更新：x = x + K*y
        x_ = x_ + K * y;
        
        // 协方差更新（Joseph形式，数值稳定性更好）
        // P = (I - K*H) * P * (I - K*H)^T + K*R*K^T
        Matrix4d I = Matrix4d::Identity();
        Matrix4d IKH = I - K * H;
        P_ = IKH * P_ * IKH.transpose() + K * R * K.transpose();
        
        // 保持对称性
        P_ = (P_ + P_.transpose()) / 2.0;
    }
    
    /**
     * @brief 使用完整观测（位置+速度）更新
     * @param z 观测向量 (x, y, vx, vy)
     * @param R 观测噪声协方差矩阵
     */
    void updateFull(const Vector4d& z, const Matrix4d& R) {
        if (!initialized_) {
            return;
        }
        
        // 观测矩阵：观测所有状态
        Matrix4d H = Matrix4d::Identity();
        
        // 创新
        Vector4d y = z - H * x_;
        
        // 创新协方差
        Matrix4d S = H * P_ * H.transpose() + R;
        
        // 卡尔曼增益
        Matrix4d K = P_ * H.transpose() * S.inverse();
        
        // 状态更新
        x_ = x_ + K * y;
        
        // 协方差更新（Joseph形式）
        Matrix4d IKH = Matrix4d::Identity() - K * H;
        P_ = IKH * P_ * IKH.transpose() + K * R * K.transpose();
        P_ = (P_ + P_.transpose()) / 2.0;
    }
    
    /**
     * @brief 获取当前状态估计
     */
    const Vector4d& getState() const { return x_; }
    
    /**
     * @brief 获取当前协方差矩阵
     */
    const Matrix4d& getCovariance() const { return P_; }
    
    /**
     * @brief 获取估计位置
     */
    Vector2d getPosition() const { return x_.head<2>(); }
    
    /**
     * @brief 获取估计速度
     */
    Vector2d getVelocity() const { return x_.tail<2>(); }
    
    /**
     * @brief 获取速度大小（米/秒）
     */
    double getSpeed() const { return x_.tail<2>().norm(); }
    
    /**
     * @brief 获取航向（度，0-360）
     */
    double getCourse() const {
        if (!initialized_) return 0.0;
        
        double course = atan2(x_(2), x_(3)) * 180.0 / M_PI;
        if (course < 0) course += 360.0;
        return course;
    }
    
    /**
     * @brief 预测未来位置
     * @param dt 预测时间差（秒）
     * @return 预测位置 (x, y)
     */
    Vector2d predictPosition(double dt) const {
        if (!initialized_) return Vector2d::Zero();
        return x_.head<2>() + x_.tail<2>() * dt;
    }
    
    /**
     * @brief 获取残差（观测值与预测值之差）
     * @param z 观测值
     * @return 残差向量
     */
    Vector2d getResidual(const Vector2d& z) const {
        if (!initialized_) return Vector2d::Zero();
        Eigen::Matrix<double, 2, 4> H;
        H << 1, 0, 0, 0, 0, 1, 0, 0;
        return z - H * x_;
    }
    
    /**
     * @brief 计算马氏距离（用于异常值检测）
     * @param z 观测值
     * @param R 观测噪声协方差
     * @return 马氏距离，通常 > 3.0 表示异常值
     */
    double getMahalanobisDistance(const Vector2d& z, const Matrix2d& R) const {
        if (!initialized_) return std::numeric_limits<double>::max();
        
        Eigen::Matrix<double, 2, 4> H;
        H << 1, 0, 0, 0, 0, 1, 0, 0;
        
        Vector2d y = z - H * x_;
        Matrix2d S = H * P_ * H.transpose() + R;
        
        return std::sqrt(y.transpose() * S.inverse() * y);
    }
    
    /**
     * @brief 获取位置不确定度（米）
     */
    double getPositionUncertainty() const {
        if (!initialized_) return 999.0;
        return std::sqrt(P_(0, 0) + P_(1, 1));
    }
    
    /**
     * @brief 获取速度不确定度（米/秒）
     */
    double getVelocityUncertainty() const {
        if (!initialized_) return 99.0;
        return std::sqrt(P_(2, 2) + P_(3, 3));
    }
    
    /**
     * @brief 检查滤波器是否已初始化
     */
    bool isInitialized() const { return initialized_; }
    
    /**
     * @brief 重置滤波器
     */
    void reset() {
        initialized_ = false;
        x_.setZero();
        P_.setIdentity();
    }
    
private:
    Vector4d x_;        // 状态向量 [x, y, vx, vy]
    Matrix4d P_;        // 协方差矩阵
    bool initialized_;  // 是否已初始化
};

} // namespace fusion

#endif // FUSION_LINEAR_KALMAN_FILTER_H