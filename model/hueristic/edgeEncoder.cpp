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
#include <deque>

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

//the nodes for the tree that will be used to traverse edge neighbors and assign functions to
struct PixelNode{
    cv::Point pixel;
    std::vector<int> neighbors;
};

//graph structure to create a tree that maps the edge map
struct EdgeGraph{
    int componentID = -1;
    std::vector<PixelNode> nodes;
};

//potential pixels be considered in draw line
struct candidate{
    int index;
    double accuracy;

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


std::vector<EdgeGraph> map(const std::vector<EdgeData>& components) {
    std::vector<EdgeGraph> graphs;
    //loop through components of the edge map
    for (const EdgeData& component : components) {
        EdgeGraph graph;
        graph.componentID = component.id;
        std::unordered_map<int64_t, int> lookup;

        auto key = [](int x, int y) -> int64_t {
            return (static_cast<int64_t>(static_cast<uint32_t>(y)) << 32) | static_cast<uint32_t>(x);
        };
        //iterate throught the pixels of each component
        for (const cv::Point& pixel : component.pixels) {
            PixelNode node;
            node.pixel = pixel;
            //search for edge neighbors
            int index = static_cast<int>(graph.nodes.size());
            graph.nodes.push_back(node);
            //add pixel coordinates to the map
            lookup[key(pixel.x, pixel.y)] = index;
        }
        //search only the connecting pixels
        for (int i = 0; i < static_cast<int>(graph.nodes.size()); i++) {
            cv::Point current = graph.nodes[i].pixel;

            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (dx == 0 && dy == 0)
                        continue;

                    auto it = lookup.find(
                        key(current.x + dx, current.y + dy)
                    );

                    if (it != lookup.end()) {
                        graph.nodes[i].neighbors.push_back(it->second);
                    }
                }
            }
        }
        graphs.push_back(std::move(graph));
    }

    return graphs;
}



double checkAccuracy(const funcData& function,const std::vector<cv::Point>& pixels, const imageData& image) {
    double baseTolerance = 1.0;
    double diagonal = std::hypot(static_cast<double>(image.width), static_cast<double>(image.height));
    double referenceDiagonal = std::hypot(512.0, 512.0);
    double tolerance = std::max(1.0, baseTolerance * diagonal / referenceDiagonal);


    if (pixels.empty() || tolerance < 0.0) {
        return 0.0;
    }
    // normalize the line equation
    double denominator = std::sqrt(
        function.a * function.a +
        function.b * function.b
    );
    if (denominator < 1e-12) {
        return 0.0;
    }
    int matchingPixels = 0;

    // check each proposed pixel
    for (const cv::Point& pixel : pixels) {
        double distance = std::abs(function.a * pixel.x +function.b * pixel.y +function.c) / denominator;
        if (distance <= tolerance) {
            matchingPixels++;
        }}
    // return fraction of matching pixels
    return static_cast<double>(matchingPixels) / pixels.size();
}




std::vector<funcData> drawLine(const std::vector<EdgeGraph>& graphs, const imageData& image){

    std::vector<funcData> functions;
    //create a unique id for the future functions
    auto connectionKey = [](int a, int b) -> uint64_t {
        uint32_t low = static_cast<uint32_t>(std::min(a, b));
        uint32_t high = static_cast<uint32_t>(std::max(a, b));

        return (static_cast<uint64_t>(low) << 32) | high;
    };


    //traverse edge components
    for(const EdgeGraph&graph: graphs){
        //a line must have 2 endpoints
        if(graph.nodes.size() < 2){
            continue;
        }
        //keep track of which pixels have been assigned a function
        std::unordered_set<uint64_t> visited;
        //search for starting points
        for(int i = 0; i < static_cast<int>(graph.nodes.size()); i++){
            //we need to determine which pixel makes the best starting point
            int startIndex = i;
            const PixelNode& startNode = graph.nodes[startIndex];

            //check neighbors for potential paths forward
            for(int neighborIndex: startNode.neighbors){
                if (visited.count(connectionKey(startIndex, neighborIndex))) {
                    continue;
                }

                std::deque<int> currentPath;
                currentPath.push_back(startIndex);
                currentPath.push_back(neighborIndex);

                int head = currentPath.front();
                //tail is incremented until the function is bounded
                int tempTail = currentPath.back();

                const cv::Point& p1 = graph.nodes[startIndex].pixel;
                const cv::Point& p2 = graph.nodes[neighborIndex].pixel;
                //setup initial function
                funcData currentFunction;
                currentFunction.id = -1;
                currentFunction.edgeID = graph.componentID;
                //calculate Ax, Bx and const c
                currentFunction.a = p1.y - p2.y;
                currentFunction.b = p2.x - p1.x;
                currentFunction.c = p1.x * p2.y - p2.x * p1.y;
                //accuracy is 100%, since it only has 2 points
                currentFunction.accuracy = 1.0;

                //for backtracking to find optimal branch
                std::vector<std::vector<candidate>> candidateHistory;
                std::deque<int> bestPath = currentPath;
                double bestPathAccuracy = currentFunction.accuracy;
                //limit how long we spend searching for the best path
                int forwardIterations = 0;
                const int maxIterations = 1000;
                while(forwardIterations++ < maxIterations){
                    //sorted by id
                    std::vector<candidate> candidates;
                    //check for neighbors of sliding endpoint
                    for(int nextIndex: graph.nodes[tempTail].neighbors){
                        if (visited.count(connectionKey(tempTail, nextIndex))) {
                            continue;
                        }
                        //dont keep searching a visited pixel
                        if(std::find(currentPath.begin(), currentPath.end(), nextIndex) != currentPath.end()){
                            continue;
                        }
                        std::vector<cv::Point> testPixels;
                        //path of neighbors
                        for(int index: currentPath){
                            testPixels.push_back(graph.nodes[index].pixel);
                            //test neighbor
                            }
                            testPixels.push_back(graph.nodes[nextIndex].pixel);
                            //check the fit of the current function
                            double accuracy = checkAccuracy(currentFunction, testPixels, image);
                            //replace if better than previous neighbor
                            if(accuracy >= .75){
                                candidates.push_back({nextIndex, accuracy});
                    }

                    }
                    //backtrack to find a potential better function
                    if(candidates.empty()){

                        if (currentPath.size() > bestPath.size()) {
                            bestPath = currentPath;
                            bestPathAccuracy = currentFunction.accuracy;
                        }

                        bool foundAlt = false;
                        while(!candidateHistory.empty()){
                            //start iterating through the previous branches
                            std::vector<candidate>& previousCandidates = candidateHistory.back();
                            //move backward once
                            currentPath.pop_back();
                            tempTail = currentPath.back();
                            //check if a branch exists
                            if(!previousCandidates.empty()){
                                candidate selected = previousCandidates.front();
                                previousCandidates.erase(previousCandidates.begin());
                                //move temp tail
                                tempTail = currentPath.back();
                                currentPath.push_back(selected.index);
                                tempTail = selected.index;
                                currentFunction.accuracy = selected.accuracy;
                                foundAlt = true;
                                break;
                            }
                            candidateHistory.pop_back();
                        }
                        if(!foundAlt){
                            break;
                        }
                        continue;
                    }
                    candidate selected = candidates.front();
                    // Remove selected candidate from remaining alternatives
                    candidates.erase(candidates.begin());
                    // Save remaining candidates for backtracking
                    candidateHistory.push_back(candidates);
                    // Advance the endpoint
                    currentPath.push_back(selected.index);
                    tempTail = selected.index;
                    currentFunction.accuracy = selected.accuracy;
                }

                currentPath = bestPath;
                currentFunction.accuracy = bestPathAccuracy;
                std::vector<std::vector<candidate>> headHistory;
                std::deque<int> bestHeadPath = currentPath;
                double bestHeadAccuracy = currentFunction.accuracy;

                                //limit how long we spend searching for the best path
                int forwardIterations = 0;
                const int maxIterations = 1000;
                while (forwardIterations++ < maxIterations) {
                    int head = currentPath.front();
                    std::vector<candidate> candidates;

                    for (int nextIndex : graph.nodes[head].neighbors) {
                        if (visited.count(connectionKey(head, nextIndex))) {
                            continue;
                        }
                        if (std::find(currentPath.begin(),currentPath.end(),nextIndex) != currentPath.end()) {
                            continue;
                        }
                        std::vector<cv::Point> testPixels;

                        for (int index : currentPath) {
                            testPixels.push_back(graph.nodes[index].pixel);
                        }
                        testPixels.push_back(graph.nodes[nextIndex].pixel);
                        double accuracy = checkAccuracy(currentFunction,testPixels,image);

                        if (accuracy >= 0.75) {
                            candidates.push_back({nextIndex, accuracy});
                        }
                    }
                        if (candidates.empty()) {
                            if (currentPath.size() > bestHeadPath.size()) {
                                bestHeadPath = currentPath;
                                bestHeadAccuracy = currentFunction.accuracy;
                            }

                            bool foundAlt = false;

                            while (!headHistory.empty()) {
                                std::vector<candidate>& previousCandidates = headHistory.back();
                                currentPath.pop_front();

                                if (!previousCandidates.empty()) {
                                    candidate selected = previousCandidates.front();
                                    previousCandidates.erase(previousCandidates.begin());
                                    currentPath.push_front(selected.index);
                                    currentFunction.accuracy = selected.accuracy;

                                    foundAlt = true;
                                    break;
                                }

                                headHistory.pop_back();
                            }
                            if (!foundAlt) {
                                break;
                            }
                            continue;
                        }
                        candidate selected = candidates.front();
                        candidates.erase(candidates.begin());
                        headHistory.push_back(candidates);
                        currentPath.push_front(selected.index);
                        currentFunction.accuracy = selected.accuracy;
                }
                currentPath = bestHeadPath;
                currentFunction.accuracy = bestHeadAccuracy;

                if (currentPath.size() >= 2) {
                    for (size_t j = 1; j < currentPath.size(); j++) {
                        int a = currentPath[j - 1];
                        int b = currentPath[j];
                        visited.insert(connectionKey(a, b));
                    }
                    currentFunction.id = static_cast<int>(functions.size());
                    functions.push_back(currentFunction);
                }
            
            }


        }
    }
    return functions;
}



int main() {
    std::string path = R"(F:\CARLA set\renders\sketchfab\0a1b51688d5f474bba3d306b872a3dc8\003.png)";

    cv::Mat skeleton = sobelEdgeDetector(path);

    // Group connected pixels
    std::vector<EdgeData> components = groupPixels(skeleton);

    // Convert components into graphs with neighbor connections
    std::vector<EdgeGraph> graphs = map(components);

    imageData image;
    image.originalImage = cv::imread(path);

    if (image.originalImage.empty()) {
        std::cerr << "Failed to load image!" << std::endl;
        return 1;
    }

    image.width = image.originalImage.cols;
    image.height = image.originalImage.rows;
    image.skeleton = skeleton;

    // Fit functions to the graph
    std::vector<funcData> functions = drawLine(graphs, image);

    std::cout << "Total functions: " << functions.size() << std::endl;

    cv::imshow("Skeleton", skeleton);
    cv::waitKey(0);
    cv::destroyAllWindows();

    return 0;
}
