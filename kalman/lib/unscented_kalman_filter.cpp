#include "unscented_kalman_filter.h"

namespace fusion {

UnscentedKalmanFilter::UnscentedKalmanFilter(int state_dim, int obs_dim,
                                             double alpha, double beta, double kappa)
    : state_dim_(state_dim)
    , obs_dim_(obs_dim)
    , alpha_(alpha)
    , beta_(beta)
    , kappa_(kappa)
    , initialized_(false)
{

    // 计算缩放参数
    lambda_ = alpha * alpha * (state_dim + kappa) - state_dim;
    num_sigma_ = 2 * state_dim + 1;

    // 计算权重
    computeWeights();

    // 初始化状态
    x_ = VectorXd::Zero(state_dim);
    P_ = MatrixXd::Identity(state_dim, state_dim);
}

void UnscentedKalmanFilter::initialize(const VectorXd &x0, const MatrixXd &P0)
{
    if (x0.size() != state_dim_ || P0.rows() != state_dim_ || P0.cols() != state_dim_)
    {
        throw std::invalid_argument("UKF: dimension mismatch in initialize");
    }

    x_ = x0;
    P_ = P0;
    initialized_ = true;
    enforceNumericalStability();
}

void UnscentedKalmanFilter::setStateTransition(StateFunction f)
{
    f_ = std::move(f);
}

void UnscentedKalmanFilter::setObservationModel(ObservationFunction h)
{
    h_ = std::move(h);
}

void UnscentedKalmanFilter::predict(double dt, const MatrixXd &Q)
{
    if (!initialized_)
    {
        throw std::runtime_error("UKF: not initialized");
    }

    if (!f_)
    {
        throw std::runtime_error("UKF: state transition function not set");
    }

    // 1. 生成Sigma点
    std::vector<VectorXd> sigma_points = generateSigmaPoints();

    // 2. 传播Sigma点
    std::vector<VectorXd> sigma_points_pred;
    sigma_points_pred.reserve(num_sigma_);
    for (const auto &sigma : sigma_points)
    {
        sigma_points_pred.push_back(f_(sigma, dt));
    }

    // 3. 计算预测均值和协方差
    VectorXd x_pred = VectorXd::Zero(state_dim_);
    for (int i = 0; i < num_sigma_; ++i)
    {
        x_pred += weights_mean_[i] * sigma_points_pred[i];
    }

    MatrixXd P_pred = MatrixXd::Zero(state_dim_, state_dim_);
    for (int i = 0; i < num_sigma_; ++i)
    {
        VectorXd diff = sigma_points_pred[i] - x_pred;
        P_pred += weights_cov_[i] * diff * diff.transpose();
    }

    // 4. 添加过程噪声
    P_pred += Q;

    // 5. 更新状态
    x_ = x_pred;
    P_ = P_pred;

    enforceNumericalStability();
}

void UnscentedKalmanFilter::update(const VectorXd &z, const MatrixXd &R)
{
    if (!initialized_)
    {
        throw std::runtime_error("UKF: not initialized");
    }

    if (!h_)
    {
        throw std::runtime_error("UKF: observation model not set");
    }

    if (z.size() != obs_dim_)
    {
        throw std::invalid_argument("UKF: observation dimension mismatch");
    }

    // 1. 生成Sigma点
    std::vector<VectorXd> sigma_points = generateSigmaPoints();

    // 2. 通过观测模型传播Sigma点
    std::vector<VectorXd> sigma_points_obs;
    sigma_points_obs.reserve(num_sigma_);
    for (const auto &sigma : sigma_points)
    {
        sigma_points_obs.push_back(h_(sigma));
    }

    // 3. 计算观测预测
    VectorXd z_pred = VectorXd::Zero(obs_dim_);
    for (int i = 0; i < num_sigma_; ++i)
    {
        z_pred += weights_mean_[i] * sigma_points_obs[i];
    }

    // 4. 计算观测协方差和互协方差
    MatrixXd Pzz = MatrixXd::Zero(obs_dim_, obs_dim_);
    MatrixXd Pxz = MatrixXd::Zero(state_dim_, obs_dim_);

    for (int i = 0; i < num_sigma_; ++i)
    {
        VectorXd diff_z = sigma_points_obs[i] - z_pred;
        VectorXd diff_x = sigma_points[i] - x_;

        Pzz += weights_cov_[i] * diff_z * diff_z.transpose();
        Pxz += weights_cov_[i] * diff_x * diff_z.transpose();
    }

    // 添加观测噪声
    Pzz += R;

    if (Pzz.determinant() < 1e-12)
    {
        throw std::runtime_error("UKF: innovation covariance is singular");
    }

    // 5. 卡尔曼增益
    MatrixXd K = Pxz * Pzz.inverse();

    // 6. 状态更新
    VectorXd y = z - z_pred;
    x_ = x_ + K * y;

    // 7. 协方差更新
    P_ = P_ - K * Pzz * K.transpose();

    enforceNumericalStability();
}

void UnscentedKalmanFilter::reset()
{
    initialized_ = false;
    x_.setZero();
    P_.setIdentity();
}

void UnscentedKalmanFilter::computeWeights()
{
    weights_mean_.resize(num_sigma_);
    weights_cov_.resize(num_sigma_);

    double lambda_div = 1.0 / (state_dim_ + lambda_);
    weights_mean_[0] = lambda_ * lambda_div;
    weights_cov_[0] = weights_mean_[0] + (1 - alpha_ * alpha_ + beta_);

    double w = 0.5 * lambda_div;
    for (int i = 1; i < num_sigma_; ++i)
    {
        weights_mean_[i] = w;
        weights_cov_[i] = w;
    }
}

std::vector<VectorXd> UnscentedKalmanFilter::generateSigmaPoints() const
{
    std::vector<VectorXd> sigma_points;
    sigma_points.reserve(num_sigma_);

    // 计算矩阵平方根 sqrt((n+lambda)*P)
    double scale = std::sqrt(state_dim_ + lambda_);
    MatrixXd sqrtP;

    try
    {
        sqrtP = P_.llt().matrixL() * scale;
    }
    catch (const std::exception &)
    {
        // 如果LLT失败，使用LDLT
        sqrtP = P_.ldlt().matrixL() * scale;
    }

    // 第一个Sigma点：均值点
    sigma_points.push_back(x_);

    // 生成其余Sigma点
    for (int i = 0; i < state_dim_; ++i)
    {
        sigma_points.push_back(x_ + sqrtP.col(i));
        sigma_points.push_back(x_ - sqrtP.col(i));
    }

    return sigma_points;
}

void UnscentedKalmanFilter::enforceNumericalStability()
{
    enforceSymmetry(P_);
    enforcePositiveDefinite(P_);
}

} // namespace fusion