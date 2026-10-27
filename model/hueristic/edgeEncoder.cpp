#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/ximgproc.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include <iostream>
#include <string>
#include <vector>
#include <tuple>
#include <utility>
#include <unordered_map>
#include <algorithm>
#include <cstdint>
#include <queue>
#include <unordered_set>
#include <cmath>
#include <list>
#include <iterator>
#include <limits>
#include <iomanip>


//the intial datastructure for pixel information
struct EdgeData {
    int id = -1;
    std::vector<cv::Point> pixels;
    //pixels are ordered along the x axis
    std::vector<cv::Point> directionalPixels;
};

//height and width
struct imageData{
    int width = 0;
    int height = 0;
    cv::Mat originalImage;
    cv::Mat edgeMap;
    cv::Mat skeleton;
};

struct funcData{
    int id = -1;
    int edgeID = -1;
    //instead of y = mx + b, we use the general line equation
    double a = 0.0;
    double b = 0.0;
    double c = 0.0;
    //amount of pixels within an edge struct that allign with the function accuracy
    double accuracy = 0.0;
};

cv::Mat sobelEdgeDetector(const std::string& path){
    //load the image in grayscale
    cv:: Mat image = cv::imread(path, cv::IMREAD_GRAYSCALE);
    if(image.empty()){
        std::cerr<<"missing input image\n";
        return cv::Mat();
    }
    //sobel edge detector
    cv::Mat gradientX;
    cv::Mat gradientY;
    cv::Sobel(image,gradientX, CV_64F, 1, 0, 3);
    cv::Sobel(image,gradientY, CV_64F, 0, 1, 3);
    cv::Mat magnitude;
    cv::magnitude(gradientX,gradientY,magnitude);
    //convert to 8 bit
    cv::Mat edgeMap;
    cv::convertScaleAbs(magnitude,edgeMap);
    //skeletolize the graph
    cv::Mat binaryMap;
    cv::threshold(edgeMap, binaryMap, 50, 255, cv::THRESH_BINARY);
    cv::Mat skeleton;
    cv::ximgproc::thinning(binaryMap, skeleton, cv::ximgproc::THINNING_ZHANGSUEN);
    return skeleton;
}




std::vector<EdgeData> groupPixels(const cv::Mat& skeleton){
    std::vector<EdgeData> components;
    int componentID = 0;

    if(skeleton.empty() || skeleton.type() != CV_8UC1){
        return components;
    }
    //keep track of neighbors so pixels are only added once, no overlap
    cv::Mat visited = cv::Mat::zeros(skeleton.size(), CV_8UC1);

    //iterate through the entire array
    for(int y = 0; y < skeleton.rows; ++y){
        for(int x = 0; x < skeleton.cols; ++x){
            //sanity check
            if(skeleton.at<uchar>(y,x) == 0 || visited.at<uchar>(y,x) != 0){
                continue;
            }

        EdgeData data;
        data.id = componentID++;
        cv::Point start(x,y);
        //add starting point
        data.pixels.emplace_back(start);
        visited.at<uchar>(start.y, start.x) = 1;

        //memory system for pixel paths we have not explored yet
        std::queue<cv::Point> pixelQueue;
        pixelQueue.push(start);

        

    while(!pixelQueue.empty()){
        cv::Point current = pixelQueue.front();
        pixelQueue.pop();

        //search the surrounding eight neightbors to find another edge pixel
        for(int i = -1; i <= 1; ++i){
            for(int j = -1; j <= 1; ++j){

                if(j == 0 && i == 0){
                    continue;
                }
                int newX = current.x + j;
                int newY = current.y + i;
                //make sure the neighbors are in the image bounds
                if(newX < 0 || newX >= skeleton.cols || newY < 0 || newY >= skeleton.rows){
                    continue;
                }

                //neighbor check
                //check we have not visited this pixel before
                if(skeleton.at<uchar>(newY, newX) != 0 && visited.at<uchar>(newY, newX) == 0){
                    //add them to the edge structure
                    data.pixels.emplace_back(newX, newY);
                    //make sure we dont keep adding the same neighbor pixels that have been discovered
                    visited.at<uchar>(newY, newX) = 1;
                    //this pixel's neighbors are added to the queue to search, bfs
                    pixelQueue.emplace(newX, newY);
                }
            }
        }
    }
        components.push_back(std::move(data));
        }
    }
    return components;
}


std::vector<EdgeFunction> drawLine(const std::vector<EdgeData> & components){
    std::vector<EdgeFunctions> functions;
    for(const EdgeData& component: components){
        //a component needs to pixels to be a line
        if(component.pixels.size() < 2){
            continue;
        }
        
    }
}



int main(){
    std::string path = R"(F:\CARLA set\renders\sketchfab\0a1b51688d5f474bba3d306b872a3dc8\003.png)";
    cv::Mat skeleton = sobelEdgeDetector(path);
    std::vector<EdgeData> components = groupPixels(skeleton);


    cv::imshow("Skeleton", skeleton);
    cv::waitKey(0);
    cv::destroyAllWindows();
}
