/***************************************************************
Copyright (c) 2022-2030, shisan233@sszc.live.
SPDX-License-Identifier: MIT 
File:        extended_kalman_filter.h
Version:     1.0
Author:      cjx
start date: 2026-3-28
Description: 扩展卡尔曼滤波器实现
    适用于非线性系统，通过一阶泰勒展开线性化
Version history

[序号]    |   [修改日期]  |   [修改者]   |   [修改内容]
1             2026-3-28      cjx            create

*****************************************************************/

#ifndef FUSION_EXTENDED_KALMAN_FILTER_H
#define FUSION_EXTENDED_KALMAN_FILTER_H

#include <cmath>
#include <functional>
#include <stdexcept>

#include <Eigen/Dense>

namespace fusion {

/**
 * @brief 扩展卡尔曼滤波器类
 * 
 * 状态方程: x_k = f(x_{k-1}, u_k) + w_k, w_k ~ N(0, Q)
 * 观测方程: z_k = h(x_k) + v_k, v_k ~ N(0, R)
 * 
 * 通过一阶泰勒展开进行线性化：
 *   F = ∂f/∂x | x_{k-1}
 *   H = ∂h/∂x | x_k^-
 */
class ExtendedKalmanFilter {
public:
    // 类型定义
    using VectorXd = Eigen::VectorXd;
    using MatrixXd = Eigen::MatrixXd;
    
    // 函数对象类型定义
    using StateFunction = std::function<VectorXd(const VectorXd&, double)>;           // 状态转移函数 f(x, dt)
    using JacobianFunction = std::function<MatrixXd(const VectorXd&, double)>;        // 状态雅可比 F(x, dt)
    using ObservationFunction = std::function<VectorXd(const VectorXd&)>;             // 观测函数 h(x)
    using ObservationJacobian = std::function<MatrixXd(const VectorXd&)>;             // 观测雅可比 H(x)
    
    /**
     * @brief 构造函数
     * @param state_dim 状态维度
     * @param obs_dim 观测维度
     */
    ExtendedKalmanFilter(int state_dim, int obs_dim)
        : state_dim_(state_dim), obs_dim_(obs_dim), initialized_(false) {
        x_ = VectorXd::Zero(state_dim);
        P_ = MatrixXd::Identity(state_dim, state_dim);
    }
    
    /**
     * @brief 初始化滤波器
     * @param x0 初始状态向量
     * @param P0 初始协方差矩阵
     */
    void initialize(const VectorXd& x0, const MatrixXd& P0) {
        if (x0.size() != state_dim_ || P0.rows() != state_dim_ || P0.cols() != state_dim_) {
            throw std::invalid_argument("Dimension mismatch in initialize");
        }
        
        x_ = x0;
        P_ = P0;
        initialized_ = true;
        
        // 确保协方差对称
        P_ = (P_ + P_.transpose()) / 2.0;
    }
    
    /**
     * @brief 设置状态转移函数及其雅可比
     * @param f 非线性状态转移函数 f(x, dt)
     * @param F 状态转移雅可比矩阵 F = ∂f/∂x
     */
    void setStateTransition(StateFunction f, JacobianFunction F) {
        f_ = f;
        F_ = F;
    }
    
    /**
     * @brief 设置观测模型及其雅可比
     * @param h 非线性观测函数 h(x)
     * @param H 观测雅可比矩阵 H = ∂h/∂x
     */
    void setObservationModel(ObservationFunction h, ObservationJacobian H) {
        h_ = h;
        H_ = H;
    }
    
    /**
     * @brief 预测步骤
     * @param dt 时间间隔（秒）
     * @param Q 过程噪声协方差矩阵
     * @throws std::runtime_error 如果函数未设置或未初始化
     */
    void predict(double dt, const MatrixXd& Q) {
        if (!initialized_) {
            throw std::runtime_error("EKF not initialized");
        }
        
        if (!f_ || !F_) {
            throw std::runtime_error("State transition function not set");
        }
        
        // 1. 非线性状态预测：x_k^- = f(x_{k-1}, dt)
        x_ = f_(x_, dt);
        
        // 2. 计算线性化状态转移矩阵：F = ∂f/∂x
        MatrixXd F = F_(x_, dt);
        
        // 3. 协方差预测：P_k^- = F * P_{k-1} * F^T + Q
        P_ = F * P_ * F.transpose() + Q;
        
        // 保持对称性
        P_ = (P_ + P_.transpose()) / 2.0;
    }
    
    /**
     * @brief 更新步骤
     * @param z 观测向量
     * @param R 观测噪声协方差矩阵
     * @throws std::runtime_error 如果函数未设置或未初始化
     */
    void update(const VectorXd& z, const MatrixXd& R) {
        if (!initialized_) {
            throw std::runtime_error("EKF not initialized");
        }
        
        if (!h_ || !H_) {
            throw std::runtime_error("Observation model not set");
        }
        
        if (z.size() != obs_dim_) {
            throw std::invalid_argument("Observation dimension mismatch");
        }
        
        // 1. 预测观测：z_pred = h(x_k^-)
        VectorXd z_pred = h_(x_);
        
        // 2. 计算线性化观测矩阵：H = ∂h/∂x
        MatrixXd H = H_(x_);
        
        // 3. 创新（残差）：y = z - z_pred
        VectorXd y = z - z_pred;
        
        // 4. 创新协方差：S = H * P * H^T + R
        MatrixXd S = H * P_ * H.transpose() + R;
        
        // 检查矩阵可逆性
        if (S.determinant() < 1e-12) {
            throw std::runtime_error("Innovation covariance matrix is singular");
        }
        
        // 5. 卡尔曼增益：K = P * H^T * S^{-1}
        MatrixXd K = P_ * H.transpose() * S.inverse();
        
        // 6. 状态更新：x = x + K*y
        x_ = x_ + K * y;
        
        // 7. 协方差更新（Joseph形式，数值稳定性更好）
        int n = state_dim_;
        MatrixXd I = MatrixXd::Identity(n, n);
        MatrixXd IKH = I - K * H;
        P_ = IKH * P_ * IKH.transpose() + K * R * K.transpose();
        
        // 保持对称性
        P_ = (P_ + P_.transpose()) / 2.0;
    }
    
    /**
     * @brief 获取当前状态估计
     */
    const VectorXd& getState() const { return x_; }
    
    /**
     * @brief 获取当前协方差矩阵
     */
    const MatrixXd& getCovariance() const { return P_; }
    
    /**
     * @brief 获取状态维度
     */
    int getStateDim() const { return state_dim_; }
    
    /**
     * @brief 获取观测维度
     */
    int getObsDim() const { return obs_dim_; }
    
    /**
     * @brief 检查是否已初始化
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
    
    /**
     * @brief 数值微分计算雅可比矩阵（当解析雅可比难以获得时使用）
     * @param f 非线性函数
     * @param x 状态向量
     * @param dt 时间间隔（可选，用于状态转移函数）
     * @param eps 微小扰动步长
     * @return 雅可比矩阵
     */
    static MatrixXd numericalJacobian(StateFunction f, const VectorXd& x, double dt, double eps = 1e-6) {
        int n = x.size();
        MatrixXd J = MatrixXd::Zero(n, n);
        VectorXd x_plus, x_minus;
        
        for (int i = 0; i < n; ++i) {
            // 正向扰动
            VectorXd x1 = x;
            x1(i) += eps;
            x_plus = f(x1, dt);
            
            // 负向扰动
            VectorXd x2 = x;
            x2(i) -= eps;
            x_minus = f(x2, dt);
            
            // 中心差分近似
            J.col(i) = (x_plus - x_minus) / (2 * eps);
        }
        
        return J;
    }
    
    /**
     * @brief 数值微分计算观测雅可比
     * @param h 非线性观测函数
     * @param x 状态向量
     * @param eps 微小扰动步长
     * @return 观测雅可比矩阵
     */
    static MatrixXd numericalObservationJacobian(ObservationFunction h, const VectorXd& x, double eps = 1e-6) {
        int n = x.size();
        int m = h(x).size();
        MatrixXd J = MatrixXd::Zero(m, n);
        
        for (int i = 0; i < n; ++i) {
            VectorXd x_plus = x;
            VectorXd x_minus = x;
            x_plus(i) += eps;
            x_minus(i) -= eps;
            
            VectorXd h_plus = h(x_plus);
            VectorXd h_minus = h(x_minus);
            
            J.col(i) = (h_plus - h_minus) / (2 * eps);
        }
        
        return J;
    }
    
private:
    int state_dim_;          // 状态维度
    int obs_dim_;            // 观测维度
    VectorXd x_;             // 状态向量
    MatrixXd P_;             // 协方差矩阵
    bool initialized_;       // 是否已初始化
    
    StateFunction f_;        // 状态转移函数
    JacobianFunction F_;     // 状态雅可比
    ObservationFunction h_;  // 观测函数
    ObservationJacobian H_;  // 观测雅可比
};

} // namespace fusion

#endif // FUSION_EXTENDED_KALMAN_FILTER_H