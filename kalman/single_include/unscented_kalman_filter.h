/***************************************************************
Copyright (c) 2022-2030, shisan233@sszc.live.
SPDX-License-Identifier: MIT 
File:        unscented_kalman_filter.h
Version:     1.0
Author:      cjx
start date: 2026-3-28
Description: 无迹卡尔曼滤波器实现
    适用于强非线性系统，通过Sigma点采样避免线性化
Version history

[序号]    |   [修改日期]  |   [修改者]   |   [修改内容]
1             2026-3-28      cjx            create

*****************************************************************/

#ifndef FUSION_UNSCENTED_KALMAN_FILTER_H
#define FUSION_UNSCENTED_KALMAN_FILTER_H

#include <cmath>
#include <stdexcept>
#include <vector>

#include <Eigen/Dense>

namespace fusion {

/**
 * @brief 无迹卡尔曼滤波器类
 * 
 * 核心思想：使用Sigma点集来近似非线性变换后的分布
 * 优点：不需要计算雅可比矩阵，精度可达二阶
 * 缺点：计算量随状态维度线性增长（2n+1个Sigma点）
 */
class UnscentedKalmanFilter {
public:
    // 类型定义
    using VectorXd = Eigen::VectorXd;
    using MatrixXd = Eigen::MatrixXd;
    using StateFunction = std::function<VectorXd(const VectorXd&, double)>;  // 状态转移函数
    using ObservationFunction = std::function<VectorXd(const VectorXd&)>;     // 观测函数
    
    /**
     * @brief 构造函数
     * @param state_dim 状态维度
     * @param obs_dim 观测维度
     * @param alpha 控制Sigma点的分布范围（通常 1e-4 到 1）
     * @param beta 处理先验分布的信息（高斯分布最优为2）
     * @param kappa 次级缩放参数（通常为0）
     */
    UnscentedKalmanFilter(int state_dim, int obs_dim, 
                          double alpha = 1e-3, double beta = 2.0, double kappa = 0.0)
        : state_dim_(state_dim), obs_dim_(obs_dim), 
          alpha_(alpha), beta_(beta), kappa_(kappa), initialized_(false) {
        
        // 计算缩放参数
        lambda_ = alpha * alpha * (state_dim + kappa) - state_dim;
        num_sigma_ = 2 * state_dim + 1;
        
        // 计算Sigma点权重
        weights_mean_.resize(num_sigma_);
        weights_cov_.resize(num_sigma_);
        
        double lambda_div = 1.0 / (state_dim + lambda_);
        weights_mean_[0] = lambda_ * lambda_div;
        weights_cov_[0] = weights_mean_[0] + (1 - alpha * alpha + beta);
        
        double w = 0.5 * lambda_div;
        for (int i = 1; i < num_sigma_; ++i) {
            weights_mean_[i] = w;
            weights_cov_[i] = w;
        }
        
        // 初始化状态和协方差
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
     * @brief 设置状态转移函数
     * @param f 非线性状态转移函数 f(x, dt)
     */
    void setStateTransition(StateFunction f) {
        f_ = f;
    }
    
    /**
     * @brief 设置观测模型
     * @param h 非线性观测函数 h(x)
     */
    void setObservationModel(ObservationFunction h) {
        h_ = h;
    }
    
    /**
     * @brief 预测步骤
     * @param dt 时间间隔（秒）
     * @param Q 过程噪声协方差矩阵
     */
    void predict(double dt, const MatrixXd& Q) {
        if (!initialized_) {
            throw std::runtime_error("UKF not initialized");
        }
        
        if (!f_) {
            throw std::runtime_error("State transition function not set");
        }
        
        // 1. 生成Sigma点
        std::vector<VectorXd> sigma_points = generateSigmaPoints();
        
        // 2. 传播Sigma点（通过非线性状态转移函数）
        std::vector<VectorXd> sigma_points_pred;
        sigma_points_pred.reserve(num_sigma_);
        for (const auto& sigma : sigma_points) {
            sigma_points_pred.push_back(f_(sigma, dt));
        }
        
        // 3. 计算预测均值和协方差
        // 3.1 计算加权均值
        VectorXd x_pred = VectorXd::Zero(state_dim_);
        for (int i = 0; i < num_sigma_; ++i) {
            x_pred += weights_mean_[i] * sigma_points_pred[i];
        }
        
        // 3.2 计算加权协方差
        MatrixXd P_pred = MatrixXd::Zero(state_dim_, state_dim_);
        for (int i = 0; i < num_sigma_; ++i) {
            VectorXd diff = sigma_points_pred[i] - x_pred;
            P_pred += weights_cov_[i] * diff * diff.transpose();
        }
        
        // 3.3 添加过程噪声
        P_pred += Q;
        
        // 更新状态和协方差
        x_ = x_pred;
        P_ = P_pred;
        
        // 保持对称性
        P_ = (P_ + P_.transpose()) / 2.0;
    }
    
    /**
     * @brief 更新步骤
     * @param z 观测向量
     * @param R 观测噪声协方差矩阵
     */
    void update(const VectorXd& z, const MatrixXd& R) {
        if (!initialized_) {
            throw std::runtime_error("UKF not initialized");
        }
        
        if (!h_) {
            throw std::runtime_error("Observation model not set");
        }
        
        if (z.size() != obs_dim_) {
            throw std::invalid_argument("Observation dimension mismatch");
        }
        
        // 1. 生成Sigma点（基于当前状态）
        std::vector<VectorXd> sigma_points = generateSigmaPoints();
        
        // 2. 通过观测模型传播Sigma点
        std::vector<VectorXd> sigma_points_obs;
        sigma_points_obs.reserve(num_sigma_);
        for (const auto& sigma : sigma_points) {
            sigma_points_obs.push_back(h_(sigma));
        }
        
        // 3. 计算观测预测
        VectorXd z_pred = VectorXd::Zero(obs_dim_);
        for (int i = 0; i < num_sigma_; ++i) {
            z_pred += weights_mean_[i] * sigma_points_obs[i];
        }
        
        // 4. 计算观测协方差和互协方差
        MatrixXd Pzz = MatrixXd::Zero(obs_dim_, obs_dim_);  // 观测协方差
        MatrixXd Pxz = MatrixXd::Zero(state_dim_, obs_dim_); // 状态-观测互协方差
        
        for (int i = 0; i < num_sigma_; ++i) {
            VectorXd diff_z = sigma_points_obs[i] - z_pred;
            VectorXd diff_x = sigma_points[i] - x_;
            
            Pzz += weights_cov_[i] * diff_z * diff_z.transpose();
            Pxz += weights_cov_[i] * diff_x * diff_z.transpose();
        }
        
        // 添加观测噪声
        Pzz += R;
        
        // 检查矩阵可逆性
        if (Pzz.determinant() < 1e-12) {
            throw std::runtime_error("Innovation covariance matrix is singular");
        }
        
        // 5. 卡尔曼增益
        MatrixXd K = Pxz * Pzz.inverse();
        
        // 6. 状态更新
        VectorXd y = z - z_pred;
        x_ = x_ + K * y;
        
        // 7. 协方差更新
        P_ = P_ - K * Pzz * K.transpose();
        
        // 保持对称性和正定性
        P_ = (P_ + P_.transpose()) / 2.0;
        
        // 确保协方差矩阵正定（数值稳定性）
        for (int i = 0; i < state_dim_; ++i) {
            if (P_(i, i) < 1e-12) {
                P_(i, i) = 1e-12;
            }
        }
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
     * @brief 获取UKF参数
     */
    double getLambda() const { return lambda_; }
    int getNumSigmaPoints() const { return num_sigma_; }
    
private:
    /**
     * @brief 生成Sigma点集
     * @return Sigma点向量列表
     * 
     * Sigma点生成公式：
     *   χ₀ = x
     *   χᵢ = x + √((n+λ)P)ᵢ, i = 1,...,n
     *   χᵢ = x - √((n+λ)P)ᵢ₋ₙ, i = n+1,...,2n
     */
    std::vector<VectorXd> generateSigmaPoints() const {
        std::vector<VectorXd> sigma_points;
        sigma_points.reserve(num_sigma_);
        
        // 计算矩阵平方根（Cholesky分解）
        // sqrt((n+λ)P) 的列向量
        double scale = std::sqrt(state_dim_ + lambda_);
        MatrixXd sqrtP;
        
        // 使用LLT分解（Cholesky）
        try {
            sqrtP = P_.llt().matrixL() * scale;
        } catch (const std::exception& e) {
            // 如果LLT失败，尝试使用更稳定的LDLT
            sqrtP = P_.ldlt().matrixL() * scale;
        }
        
        // 第一个Sigma点：均值点
        sigma_points.push_back(x_);
        
        // 生成其余Sigma点
        for (int i = 0; i < state_dim_; ++i) {
            sigma_points.push_back(x_ + sqrtP.col(i));
            sigma_points.push_back(x_ - sqrtP.col(i));
        }
        
        return sigma_points;
    }
    
private:
    int state_dim_;              // 状态维度
    int obs_dim_;                // 观测维度
    double alpha_;               // 主缩放参数
    double beta_;                // 次级缩放参数（处理先验分布）
    double kappa_;               // 三级缩放参数
    double lambda_;              // 组合缩放参数
    int num_sigma_;              // Sigma点数量 = 2n + 1
    
    VectorXd x_;                 // 状态向量
    MatrixXd P_;                 // 协方差矩阵
    bool initialized_;           // 是否已初始化
    
    std::vector<double> weights_mean_;  // 均值权重
    std::vector<double> weights_cov_;   // 协方差权重
    
    StateFunction f_;            // 状态转移函数
    ObservationFunction h_;      // 观测函数
};

} // namespace fusion

#endif // FUSION_UNSCENTED_KALMAN_FILTER_H