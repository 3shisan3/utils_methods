/***************************************************************
Copyright (c) 2022-2030, shisan233@sszc.live.
All rights reserved.
File:        beziercurve.h
Version:     1.0
Author:      cjx
start date: 2025-02-22
Description: 贝塞尔曲线绘制点位获取接口
Version history

[序号]    |   [修改日期]  |   [修改者]   |   [修改内容]
1             2025-02-22      cjx        create

*****************************************************************/

#pragma once

#include <cmath>
#include <vector>

// 二维点结构体
struct Point
{
    double x, y;
    Point(double x = 0, double y = 0) : x(x), y(y) {}
};

/**
 * @brief 二阶贝塞尔曲线
 * @param {返回的点的数组长度} num
 * @param {起点} point1
 * @param {控制点} cpoint
 * @param {终点} point2
 */
static std::vector<Point> getBezierPoints(Point point1, Point point2, Point cpoint, int num = 20)
{
    std::vector<Point> pointList;

    double x1 = point1.x, y1 = point1.y;
    double x2 = point2.x, y2 = point2.y;
    double cx = cpoint.x, cy = cpoint.y;

    // double z1 = point1.z, z2 = point2.z;

    double t = 0; // t范围【0，1】
    for (int i = 1; i <= num; i++)
    {
        // 用i当作t，算出点坐标，放入数组
        t = (double)i / num;
        double x = pow(1 - t, 2) * x1 + 2 * t * (1 - t) * cx + pow(t, 2) * x2;
        double y = pow(1 - t, 2) * y1 + 2 * t * (1 - t) * cy + pow(t, 2) * y2;

        // double z = z1 + (z2 - z1) * i / num;
        // pointList.emplace_back(Point{x, y, z});
        pointList.emplace_back(Point{x, y});
    }

    return pointList;
}

/**
 * @brief 三阶贝塞尔曲线
 * @param {返回的点的数组长度} num
 * @param {起点} point1
 * @param {控制点1} cpoint2
 * @param {控制点2} cpoint2
 * @param {终点} point2
 */
static std::vector<Point> getBezierPoints(Point point1, Point point2,
                                          Point cpoint1, Point cpoint2,
                                          int num = 20)
{
    std::vector<Point> pointList;

    double x1 = point1.x, y1 = point1.y;
    double x2 = point2.x, y2 = point2.y;
    double cx1 = cpoint1.x, cy1 = cpoint1.y;
    double cx2 = cpoint2.x, cy2 = cpoint2.y;

    // double z1 = point1.z, z2 = point2.z;

    double t = 0; // t范围【0，1】
    for (int i = 1; i <= num; i++)
    {
        // 用i当作t，算出点坐标，放入数组
        t = (double)i / num;
        double x = pow(1 - t, 3) * x1 + 3 * t * pow(1 - t, 2) * cx1 + 3 * pow(t, 2) * (1 - t) * cx2 + pow(t, 3) * x2;
        double y = pow(1 - t, 3) * y1 + 3 * t * pow(1 - t, 2) * cy1 + 3 * pow(t, 2) * (1 - t) * cy2 + pow(t, 3) * y2;

        // double z = z1 + (z2 - z1) * i / num;
        // pointList.emplace_back(Point{x, y, z});
        pointList.emplace_back(Point{x, y});
    }

    return pointList;
}