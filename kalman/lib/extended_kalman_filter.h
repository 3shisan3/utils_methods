/***************************************************************
Copyright (c) 2022-2030, shisan233@sszc.live.
SPDX-License-Identifier: MIT
File:        extended_kalman_filter.h
Version:     1.0
Author:      cjx
start date: 2026-3-28
Description: 扩展卡尔曼滤波器
    适用于非线性系统，通过一阶泰勒展开线性化
Version history

[序号]    |   [修改日期]  |   [修改者]   |   [修改内容]
1             2026-3-28      cjx            create

*****************************************************************/

#ifndef FUSION_EXTENDED_KALMAN_FILTER_H
#define FUSION_EXTENDED_KALMAN_FILTER_H

#include "kalman_filter_types.h"

#include <functional>

namespace fusion {

/**
 * @brief 扩展卡尔曼滤波器
 * 
 * 状态方程: x_k = f(x_{k-1}, dt) + w_k
 * 观测方程: z_k = h(x_k) + v_k
 * 
 * 通过计算雅可比矩阵进行线性化
 */
class ExtendedKalmanFilter {
public:
    // 函数类型定义
    using StateFunction = std::function<VectorXd(const VectorXd&, double)>;
    using JacobianFunction = std::function<MatrixXd(const VectorXd&, double)>;
    using ObservationFunction = std::function<VectorXd(const VectorXd&)>;
    using ObservationJacobian = std::function<MatrixXd(const VectorXd&)>;
    
    /**
     * @brief 构造函数
     * @param state_dim 状态维度
     * @param obs_dim 观测维度
     */
    ExtendedKalmanFilter(int state_dim, int obs_dim);
    
    /**
     * @brief 析构函数
     */
    ~ExtendedKalmanFilter() = default;
    
    /**
     * @brief 初始化滤波器
     * @param x0 初始状态向量
     * @param P0 初始协方差矩阵
     */
    void initialize(const VectorXd& x0, const MatrixXd& P0);
    
    /**
     * @brief 设置状态转移函数
     * @param f 非线性状态转移函数 f(x, dt)
     * @param F 雅可比矩阵 F = ∂f/∂x
     */
    void setStateTransition(StateFunction f, JacobianFunction F);
    
    /**
     * @brief 设置观测模型
     * @param h 非线性观测函数 h(x)
     * @param H 观测雅可比矩阵 H = ∂h/∂x
     */
    void setObservationModel(ObservationFunction h, ObservationJacobian H);
    
    /**
     * @brief 预测步骤
     * @param dt 时间间隔（秒）
     * @param Q 过程噪声协方差矩阵
     */
    void predict(double dt, const MatrixXd& Q);
    
    /**
     * @brief 更新步骤
     * @param z 观测向量
     * @param R 观测噪声协方差矩阵
     */
    void update(const VectorXd& z, const MatrixXd& R);
    
    // ========== 状态获取 ==========
    const VectorXd& getState() const { return x_; }
    const MatrixXd& getCovariance() const { return P_; }
    int getStateDim() const { return state_dim_; }
    int getObsDim() const { return obs_dim_; }
    bool isInitialized() const { return initialized_; }
    
    // ========== 其他 ==========
    void reset();
    
    // ========== 静态工具函数 ==========
    /**
     * @brief 数值微分计算雅可比矩阵
     */
    static MatrixXd numericalJacobian(StateFunction f, const VectorXd& x, double dt, 
                                       double eps = 1e-6);
    
    /**
     * @brief 数值微分计算观测雅可比
     */
    static MatrixXd numericalObservationJacobian(ObservationFunction h, const VectorXd& x, 
                                                  double eps = 1e-6);
    
private:
    void enforceNumericalStability();
    
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