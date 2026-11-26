#pragma once

#include <opencv2/core.hpp>
#include <string>
#include <vector>

struct functionData {
    int id = -1;
    int componentID = -1;
    int pathID = -1;

    double a = 0.0;
    double b = 0.0;
    double c = 0.0;

    cv::Point2d start;
    cv::Point2d end;
    std::vector<int> neighbors;
    bool isBezier = false;
    cv::Point2d control;
};

std::vector<functionData> encodeFunctions(const std::string& imagePath);
std::vector<functionData> encodeFunctions(const std::string& imagePath, cv::Size& imageSize);
// Curves use -(native ID + 1), keeping existing nonnegative line IDs stable.
std::vector<functionData> encodeObjectFunctions(const std::string& imagePath, cv::Size& imageSize);
