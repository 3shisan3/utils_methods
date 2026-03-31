/***************************************************************
Copyright (c) 2022-2030, shisan233@sszc.live.
SPDX-License-Identifier: MIT
File:        unscented_kalman_filter.h
Version:     1.0
Author:      cjx
start date: 2026-3-28
Description: 无迹卡尔曼滤波器
    适用于强非线性系统，通过Sigma点采样避免线性化
Version history

[序号]    |   [修改日期]  |   [修改者]   |   [修改内容]
1             2026-3-28      cjx            create

*****************************************************************/

#ifndef FUSION_UNSCENTED_KALMAN_FILTER_H
#define FUSION_UNSCENTED_KALMAN_FILTER_H

#include "kalman_filter_types.h"

#include <functional>
#include <vector>

namespace fusion {

/**
 * @brief 无迹卡尔曼滤波器
 *
 * 核心思想：使用Sigma点集近似非线性变换后的分布
 * 优点：无需计算雅可比，精度可达二阶
 */
class UnscentedKalmanFilter
{
public:
    // 函数类型定义
    using StateFunction = std::function<VectorXd(const VectorXd &, double)>;
    using ObservationFunction = std::function<VectorXd(const VectorXd &)>;

    /**
     * @brief 构造函数
     * @param state_dim 状态维度
     * @param obs_dim 观测维度
     * @param alpha 控制Sigma点分布范围（1e-4 ~ 1）
     * @param beta 处理先验分布信息（高斯最优为2）
     * @param kappa 次级缩放参数（通常为0）
     */
    UnscentedKalmanFilter(int state_dim, int obs_dim,
                          double alpha = 1e-3, double beta = 2.0, double kappa = 0.0);

    /**
     * @brief 析构函数
     */
    ~UnscentedKalmanFilter() = default;

    /**
     * @brief 初始化滤波器
     */
    void initialize(const VectorXd &x0, const MatrixXd &P0);

    /**
     * @brief 设置状态转移函数
     */
    void setStateTransition(StateFunction f);

    /**
     * @brief 设置观测模型
     */
    void setObservationModel(ObservationFunction h);

    /**
     * @brief 预测步骤
     */
    void predict(double dt, const MatrixXd &Q);

    /**
     * @brief 更新步骤
     */
    void update(const VectorXd &z, const MatrixXd &R);

    // ========== 状态获取 ==========
    const VectorXd &getState() const { return x_; }
    const MatrixXd &getCovariance() const { return P_; }
    int getStateDim() const { return state_dim_; }
    int getObsDim() const { return obs_dim_; }
    bool isInitialized() const { return initialized_; }

    // ========== 参数获取 ==========
    double getLambda() const { return lambda_; }
    int getNumSigmaPoints() const { return num_sigma_; }

    // ========== 其他 ==========
    void reset();

private:
    /**
     * @brief 生成Sigma点
     */
    std::vector<VectorXd> generateSigmaPoints() const;

    /**
     * @brief 计算权重
     */
    void computeWeights();

    /**
     * @brief 数值稳定性处理
     */
    void enforceNumericalStability();

    int state_dim_; // 状态维度
    int obs_dim_;   // 观测维度
    double alpha_;  // 主缩放参数
    double beta_;   // 次级缩放参数
    double kappa_;  // 三级缩放参数
    double lambda_; // 组合缩放参数 = alpha^2*(n+kappa) - n
    int num_sigma_; // Sigma点数量 = 2n + 1

    VectorXd x_;       // 状态向量
    MatrixXd P_;       // 协方差矩阵
    bool initialized_; // 是否已初始化

    std::vector<double> weights_mean_; // 均值权重
    std::vector<double> weights_cov_;  // 协方差权重

    StateFunction f_;       // 状态转移函数
    ObservationFunction h_; // 观测函数
};

} // namespace fusion

#endif // FUSION_UNSCENTED_KALMAN_FILTER_H