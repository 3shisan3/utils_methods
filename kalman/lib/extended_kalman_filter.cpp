#include "extended_kalman_filter.h"

namespace fusion {

ExtendedKalmanFilter::ExtendedKalmanFilter(int state_dim, int obs_dim)
    : state_dim_(state_dim)
    , obs_dim_(obs_dim)
    , initialized_(false)
{
    x_ = VectorXd::Zero(state_dim);
    P_ = MatrixXd::Identity(state_dim, state_dim);
}

void ExtendedKalmanFilter::initialize(const VectorXd &x0, const MatrixXd &P0)
{
    if (x0.size() != state_dim_ || P0.rows() != state_dim_ || P0.cols() != state_dim_)
    {
        throw std::invalid_argument("EKF: dimension mismatch in initialize");
    }

    x_ = x0;
    P_ = P0;
    initialized_ = true;
    enforceNumericalStability();
}

void ExtendedKalmanFilter::setStateTransition(StateFunction f, JacobianFunction F)
{
    f_ = std::move(f);
    F_ = std::move(F);
}

void ExtendedKalmanFilter::setObservationModel(ObservationFunction h, ObservationJacobian H)
{
    h_ = std::move(h);
    H_ = std::move(H);
}

void ExtendedKalmanFilter::predict(double dt, const MatrixXd &Q)
{
    if (!initialized_)
    {
        throw std::runtime_error("EKF: not initialized");
    }

    if (!f_ || !F_)
    {
        throw std::runtime_error("EKF: state transition function not set");
    }

    // 非线性状态预测
    x_ = f_(x_, dt);

    // 计算线性化矩阵
    MatrixXd F = F_(x_, dt);

    // 协方差预测
    P_ = F * P_ * F.transpose() + Q;

    enforceNumericalStability();
}

void ExtendedKalmanFilter::update(const VectorXd &z, const MatrixXd &R)
{
    if (!initialized_)
    {
        throw std::runtime_error("EKF: not initialized");
    }

    if (!h_ || !H_)
    {
        throw std::runtime_error("EKF: observation model not set");
    }

    if (z.size() != obs_dim_)
    {
        throw std::invalid_argument("EKF: observation dimension mismatch");
    }

    // 预测观测
    VectorXd z_pred = h_(x_);

    // 计算线性化观测矩阵
    MatrixXd H = H_(x_);

    // 创新
    VectorXd y = z - z_pred;

    // 创新协方差
    MatrixXd S = H * P_ * H.transpose() + R;

    if (S.determinant() < 1e-12)
    {
        throw std::runtime_error("EKF: innovation covariance is singular");
    }

    // 卡尔曼增益
    MatrixXd K = P_ * H.transpose() * S.inverse();

    // 状态更新
    x_ = x_ + K * y;

    // 协方差更新（Joseph形式）
    int n = state_dim_;
    MatrixXd I = MatrixXd::Identity(n, n);
    MatrixXd IKH = I - K * H;
    P_ = IKH * P_ * IKH.transpose() + K * R * K.transpose();

    enforceNumericalStability();
}

void ExtendedKalmanFilter::reset()
{
    initialized_ = false;
    x_.setZero();
    P_.setIdentity();
}

void ExtendedKalmanFilter::enforceNumericalStability()
{
    enforceSymmetry(P_);
    enforcePositiveDefinite(P_);
}

MatrixXd ExtendedKalmanFilter::numericalJacobian(StateFunction f, const VectorXd &x,
                                                 double dt, double eps)
{
    int n = x.size();
    MatrixXd J = MatrixXd::Zero(n, n);

    for (int i = 0; i < n; ++i)
    {
        VectorXd x_plus = x;
        VectorXd x_minus = x;
        x_plus(i) += eps;
        x_minus(i) -= eps;

        VectorXd f_plus = f(x_plus, dt);
        VectorXd f_minus = f(x_minus, dt);

        J.col(i) = (f_plus - f_minus) / (2 * eps);
    }

    return J;
}

MatrixXd ExtendedKalmanFilter::numericalObservationJacobian(ObservationFunction h,
                                                            const VectorXd &x, double eps)
{
    int n = x.size();
    int m = h(x).size();
    MatrixXd J = MatrixXd::Zero(m, n);

    for (int i = 0; i < n; ++i)
    {
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

} // namespace fusion