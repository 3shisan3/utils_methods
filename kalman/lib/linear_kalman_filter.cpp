#include "linear_kalman_filter.h"

namespace fusion {

LinearKalmanFilter::LinearKalmanFilter()
    : initialized_(false)
{
    x_.setZero();
    P_.setIdentity();
}

void LinearKalmanFilter::initialize(double x0, double y0, double vx0, double vy0,
                                    const MatrixState &P0)
{
    VectorState x0_vec;
    x0_vec << x0, y0, vx0, vy0;
    initialize(x0_vec, P0);
}

void LinearKalmanFilter::initialize(const VectorState &x0, const MatrixState &P0)
{
    x_ = x0;
    P_ = P0;
    initialized_ = true;
    enforceNumericalStability();
}

void LinearKalmanFilter::predict(double dt, const MatrixState &Q)
{
    if (!initialized_)
    {
        throw std::runtime_error("LinearKalmanFilter: not initialized");
    }

    if (dt <= 0)
    {
        throw std::invalid_argument("LinearKalmanFilter: dt must be positive");
    }

    // 状态转移矩阵（匀速模型）
    MatrixState F = MatrixState::Identity();
    F(0, 2) = dt; // x = x + vx * dt
    F(1, 3) = dt; // y = y + vy * dt

    // 状态预测
    x_ = F * x_;

    // 协方差预测
    P_ = F * P_ * F.transpose() + Q;

    enforceNumericalStability();
}

void LinearKalmanFilter::update(const VectorObs &z, const MatrixObs &R)
{
    if (!initialized_)
    {
        throw std::runtime_error("LinearKalmanFilter: not initialized");
    }

    // 观测矩阵：只观测位置
    Eigen::Matrix<double, 2, 4> H;
    H << 1, 0, 0, 0,
        0, 1, 0, 0;

    // 创新
    VectorObs y = z - H * x_;

    // 创新协方差
    MatrixObs S = H * P_ * H.transpose() + R;

    if (S.determinant() < 1e-12)
    {
        throw std::runtime_error("LinearKalmanFilter: innovation covariance is singular");
    }

    // 卡尔曼增益
    Eigen::Matrix<double, 4, 2> K = P_ * H.transpose() * S.inverse();

    // 状态更新
    x_ = x_ + K * y;

    // 协方差更新（Joseph形式，数值稳定）
    MatrixState I = MatrixState::Identity();
    MatrixState IKH = I - K * H;
    P_ = IKH * P_ * IKH.transpose() + K * R * K.transpose();

    enforceNumericalStability();
}

void LinearKalmanFilter::updateFull(const VectorState &z, const MatrixState &R)
{
    if (!initialized_)
    {
        throw std::runtime_error("LinearKalmanFilter: not initialized");
    }

    // 观测矩阵：观测全部状态
    MatrixState H = MatrixState::Identity();
    updateInternal(z, R, H);
}

void LinearKalmanFilter::updateInternal(const VectorState &z, const MatrixState &R,
                                        const MatrixState &H)
{
    VectorState y = z - H * x_;
    MatrixState S = H * P_ * H.transpose() + R;

    if (S.determinant() < 1e-12)
    {
        throw std::runtime_error("LinearKalmanFilter: innovation covariance is singular");
    }

    MatrixState K = P_ * H.transpose() * S.inverse();

    x_ = x_ + K * y;

    MatrixState I = MatrixState::Identity();
    MatrixState IKH = I - K * H;
    P_ = IKH * P_ * IKH.transpose() + K * R * K.transpose();

    enforceNumericalStability();
}

double LinearKalmanFilter::getCourse() const
{
    if (!initialized_)
        return 0.0;

    double course = std::atan2(x_(2), x_(3)) * RAD_TO_DEG;
    if (course < 0)
        course += 360.0;
    return course;
}

VectorObs LinearKalmanFilter::predictPosition(double dt) const
{
    if (!initialized_)
        return VectorObs::Zero();
    return x_.head<2>() + x_.tail<2>() * dt;
}

double LinearKalmanFilter::getPositionUncertainty() const
{
    if (!initialized_)
        return 999.0;
    return std::sqrt(P_(0, 0) + P_(1, 1));
}

double LinearKalmanFilter::getVelocityUncertainty() const
{
    if (!initialized_)
        return 99.0;
    return std::sqrt(P_(2, 2) + P_(3, 3));
}

double LinearKalmanFilter::getMahalanobisDistance(const VectorObs &z, const MatrixObs &R) const
{
    if (!initialized_)
        return std::numeric_limits<double>::max();

    Eigen::Matrix<double, 2, 4> H;
    H << 1, 0, 0, 0,
        0, 1, 0, 0;

    VectorObs y = z - H * x_;
    MatrixObs S = H * P_ * H.transpose() + R;

    return mahalanobisDistance(y, S);
}

VectorObs LinearKalmanFilter::getResidual(const VectorObs &z) const
{
    if (!initialized_)
        return VectorObs::Zero();

    Eigen::Matrix<double, 2, 4> H;
    H << 1, 0, 0, 0,
        0, 1, 0, 0;

    return z - H * x_;
}

void LinearKalmanFilter::reset()
{
    initialized_ = false;
    x_.setZero();
    P_.setIdentity();
}

void LinearKalmanFilter::enforceNumericalStability()
{
    enforceSymmetry(P_);
    enforcePositiveDefinite(P_);
}

} // namespace fusion