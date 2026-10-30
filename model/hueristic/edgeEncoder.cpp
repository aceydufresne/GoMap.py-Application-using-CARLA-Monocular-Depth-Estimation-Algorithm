#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/ximgproc.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/geometry/2d.hpp>

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
#include <stack>
#include <variant>


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
    //include bounds
    cv::Point start;
    cv::Point end;
    //list of corresponding pixels
    std::vector<cv::Point> pixels;
    int pathID = -1;
};

//node struct for a graph tree
struct PixelNode {
    int id = -1;
    cv::Point pixel;
    std::vector<int> neighbors;
};

//used to keep track of which pixels have been visited
struct EdgePath {
    int id = -1;
    std::vector<int> nodeIDs;
    bool closed = false;
};

//tree structure to map out the edgemap
struct EdgeGraph {
    int id = -1;
    int componentID = -1;
    std::vector<PixelNode> nodes;
    std::vector<EdgePath> paths;
};

//temp structure to ensure the function is fitting correctly
struct IntersectionData {
    int function1 = -1;
    int function2 = -1;
    cv::Point2d point;
};

//multiple functions into one curve
struct bezierData {
    int id = -1;
    int edgeID = -1;
    int pathID = -1;
    int startLineID = -1;
    int endLineID = -1;

    //control points
    cv::Point2d p0;
    cv::Point2d p1;
    cv::Point2d p2;

    //how many of a functions points fit in the new curve
    double accuracy = 0.0;

    //original list
    std::vector<cv::Point> pixels;
};

//piecewise approx for curves
struct piecewiseData {
    int edgeID = -1;
    int pathID = -1;

    std::vector<std::variant<funcData, bezierData>> segments;
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
    double minVal, maxVal;
    cv::minMaxLoc(edgeMap, &minVal, &maxVal);

    std::cout << "Minimum gradient: " << minVal << std::endl;
    std::cout << "Maximum gradient: " << maxVal << std::endl;

    //skeletolize the graph
    cv::Mat binaryMap;
    cv::threshold(
        image,
        binaryMap,
        200,
        255,
        cv::THRESH_BINARY_INV
    );

    cv::Mat skeleton;
    cv::ximgproc::thinning(
        binaryMap,
        skeleton,
        cv::ximgproc::THINNING_ZHANGSUEN
    );

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

                    // Don't connect a pixel to itself
                    if (dx == 0 && dy == 0)
                        continue;
                    // Find neighboring pixel
                    auto it = lookup.find(key(current.x + dx, current.y + dy));

                    if (it == lookup.end())
                        continue;
                    // Check diagonal connections
                    if (dx != 0 && dy != 0) {

                        bool horizontalExists =
                            lookup.find(key(current.x + dx, current.y)) != lookup.end();
                        bool verticalExists =
                            lookup.find(key(current.x, current.y + dy)) != lookup.end();

                        // Remove redundant diagonal connections
                        if (horizontalExists || verticalExists)
                            continue;
                    }
                    // Add valid neighboring pixel
                    graph.nodes[i].neighbors.push_back(it->second);
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



//function to reduce the components into paths,
std::vector<EdgePath> findPaths(const EdgeGraph& graph, int startNode){
    //list of completed paths
    std::vector<EdgePath> paths;
    //track current partial paths we are traversing
    std::unordered_set<uint64_t> visited;
    //potential paths that havent been checked
    std::stack<std::pair<int, int>> pending;

    if(startNode < 0 || startNode >= static_cast<int>(graph.nodes.size())){
        return paths;
    }
    auto edgeKey = [](int a, int b) -> uint64_t {
        uint32_t low = static_cast<uint32_t>(std::min(a, b));
        uint32_t high = static_cast<uint32_t>(std::max(a, b));

        return (static_cast<uint64_t>(low) << 32) | high;
    };
    //first path
    EdgePath path;
    path.id = 0;
    path.nodeIDs.push_back(startNode);
    //retrieve neighbors
    int currentNode = startNode;
    
    //keep moving forward through the tree
    while(true){
        const PixelNode& current = graph.nodes[currentNode];
        int nextNode = -1;

        if (!path.closed) {
            for (int neighborID : current.neighbors) {
                uint64_t key = edgeKey(currentNode, neighborID);

                if (visited.count(key)) {
                    continue;
                }
                if (nextNode == -1) {
                    nextNode = neighborID;
                }
                else {
                    pending.push({currentNode, neighborID});
                }
            }
        }
        //endpoint
        if(nextNode == -1){
            //check if any neighboring paths havent been checked
            if(path.nodeIDs.size() > 1){
                paths.push_back(std::move(path));
            }
            bool foundPending = false;
            //check neighbors in pending
            while(!pending.empty()){
                auto[fromNode, toNode] = pending.top();
                pending.pop();
                //if node is already been checked skip
                if(visited.count(edgeKey(fromNode, toNode))){
                    continue;
                }
                //new path
                path =  EdgePath{};
                path.id = static_cast<int> (paths.size());
                path.nodeIDs.push_back(fromNode);
                visited.insert(edgeKey(fromNode, toNode));
                currentNode = toNode;
                path.nodeIDs.push_back(currentNode);
                foundPending = true;
                break;
            }
            //no unexplored paths left
            if(!foundPending){
                break;
            }
            continue;
        }
        //mark as visited
        visited.insert(edgeKey(currentNode, nextNode));
        currentNode = nextNode;
        //update path
        path.nodeIDs.push_back(currentNode);
        //cant move forward
        if (currentNode == path.nodeIDs.front()) {
            path.closed = true;
        }
    }

    return paths;
}



std::vector<funcData> drawLine(const std::vector<EdgeGraph>& graphs, const imageData& image){
    //store all final functions
    std::vector<funcData> functions;
    //traverse ordered components
    const double accuracyThresh = 0.75;
    for(const EdgeGraph& graph: graphs){
        //iterate through paths
        for (const EdgePath& path : graph.paths){
            if(path.nodeIDs.size() < 2){
                continue;
            }
            //start point found in findPaths
            int head = 0;
            //sliding endpoint
            int tempTail = 1;
            //first function slope
            cv::Point p1 = graph.nodes[path.nodeIDs[head]].pixel;
            //cv::Point p2 = graph.nodes[path.nodeIDs[tail]].pixel;
            //cv fitline, param1 = default, param2 = distance tolerance, param3 = angle tolerance
            std::vector<cv::Point> linePixels;
            linePixels.push_back(p1);

            //keep track of the last function and pixels in case this pixel does not work
            double lastA = 0.0;
            double lastB = 0.0;
            double lastC = 0.0;
            double lastAccuracy = 0.0;
            bool hasValidFit = false;

            while(tempTail<static_cast<int>(path.nodeIDs.size())){
                cv::Point current = graph.nodes[path.nodeIDs[tempTail]].pixel;
                linePixels.push_back(current);
                //keeping the function updated with the cv function
                if(linePixels.size()>=2){
                    //cv function representation
                    cv::Vec4f fittedLine;
                    cv::fitLine(linePixels, fittedLine, cv::DIST_L2, 0, 0.01,0.01);
                    //convert into function struct format
                    double vx = fittedLine[0];
                    double vy = fittedLine[1];
                    double x0 = fittedLine[2];
                    double y0 = fittedLine[3];
                    //Ax + Bx + c = 0
                    double A = -vy;
                    double B = vx;
                    double C = vy * x0 - vx * y0;

                    int matchedPixels = 0;
                    double tolerance = 1.0;
                    //check the new accuracy with the additional pixels
                    for(const cv::Point& pixel : linePixels){
                        double distance = std::abs(A * pixel.x + B * pixel.y + C);
                        //calculate tolerance with range of image eventually
                        if(distance <= tolerance){
                            matchedPixels++;
                        }
                    }
                    double accuracy = static_cast<double>(matchedPixels) / linePixels.size();
                    if(accuracy >= accuracyThresh){
                        //allow new edge pixel
                        lastA = A;
                        lastB = B;
                        lastC = C;
                        lastAccuracy = accuracy;
                        hasValidFit = true;
                    }
                    else{
                        //pixel doesnt belong to this function
                        linePixels.pop_back();
                        if(hasValidFit){
                            funcData function;
                            function.id = static_cast<int>(functions.size());
                            function.edgeID = graph.componentID;
                            function.pathID = path.id;
                            function.a = lastA;
                            function.b = lastB;
                            function.c = lastC;
                            function.accuracy = lastAccuracy;
                        
                            //keep track of endpoints
                            function.start = linePixels.front();
                            function.end = linePixels.back();
                            function.pixels = linePixels;
                            functions.push_back(function);

                            //the last function is correct, go back to that group of pixels
                            head = tempTail - 1;
                            linePixels.clear();
                            cv::Point newHead = graph.nodes[path.nodeIDs[head]].pixel;
                            linePixels.push_back(newHead);
                            hasValidFit = false;
                            lastA = 0.0;
                            lastB = 0.0;
                            lastC = 0.0;
                            lastAccuracy = 0.0;
                            tempTail--;
                        }
                        else {
                            // No valid previous segment: advance the head
                            head = tempTail;

                            linePixels.clear();
                            linePixels.push_back(current);
                        }
                    }
                    tempTail++;
                }

            }
            
            //function that had no bad pixels
            if (hasValidFit) {
            funcData function;

            function.id = static_cast<int>(functions.size());
            function.edgeID = graph.componentID;
            function.pathID = path.id;

            function.a = lastA;
            function.b = lastB;
            function.c = lastC;
            function.accuracy = lastAccuracy;
            //keep track of endpoints
            function.start = linePixels.front();
            function.end = linePixels.back();
            function.pixels = linePixels;

            functions.push_back(function);
            }
        }
    }

    return functions;
}


double checkCurveAccuracy(const bezierData& curve,double pixelTolerance){
    if(curve.pixels.empty()){
        return 0.0;
    }

    const int samples = std::max(1000,static_cast<int>(curve.pixels.size()) * 2);
    std::vector<cv::Point2d> curvePoints;
    curvePoints.reserve(samples + 1);

    for(int i = 0; i <= samples; i++){
        double t = static_cast<double>(i) / samples;
        double u = 1.0 - t;

        cv::Point2d point =
            (u * u) * curve.p0 +
            (2.0 * u * t) * curve.p1 +
            (t * t) * curve.p2;

        curvePoints.push_back(point);
    }

    int matchedPixels = 0;
    double toleranceSquared = pixelTolerance * pixelTolerance;

    // Compare each original pixel against the curve
    for(const cv::Point& pixel : curve.pixels){
        cv::Point2d original(pixel.x, pixel.y);
        double minimumDistanceSquared =
            std::numeric_limits<double>::infinity();

        for(const cv::Point2d& point : curvePoints){

            cv::Point2d difference = original - point;
            double distanceSquared =
                difference.dot(difference);
            minimumDistanceSquared =std::min(minimumDistanceSquared, distanceSquared);
        }
        if(minimumDistanceSquared <= toleranceSquared){
            matchedPixels++;
        }
    }
    return static_cast<double>(matchedPixels) /
           curve.pixels.size();
}


//determine angle threshold, if we need a curve or not
double calculateAngle(const funcData& first, const funcData& second) {
    cv::Point2d v1 = cv::Point2d(first.end - first.start);
    cv::Point2d v2 = cv::Point2d(second.end - second.start);

    double length1 = cv::norm(v1);
    double length2 = cv::norm(v2);

    if (length1 < 1e-9 || length2 < 1e-9)
        return 0.0;
    //find the angle difference between two connecting functions
    double cosine = v1.dot(v2) / (length1 * length2);
    //if its within tolerance, we assume it can be replaced with a quadriatic
    cosine = std::clamp(cosine, -1.0, 1.0);

    return std::acos(cosine) * 180.0 / CV_PI;
}



cv::Point2d calculateControlPoint(const std::vector<cv::Point>& pixels) {
    int n = static_cast<int>(pixels.size());
    cv::Point2d p0 = pixels.front();
    cv::Point2d p2 = pixels.back();

    double numeratorX = 0.0;
    double numeratorY = 0.0;
    double denominator = 0.0;

    for(int i = 0; i < n; i++) {
        double t = static_cast<double>(i) / (n - 1);
        double u = 1.0 - t;

        double weight = 2.0 * u * t;
        cv::Point2d expected =(u * u) * p0 + (t * t) * p2;
        cv::Point2d difference = cv::Point2d(pixels[i]) - expected;

        numeratorX += weight * difference.x;
        numeratorY += weight * difference.y;
        denominator += weight * weight;
    }

    if(denominator < 1e-9)
        return (p0 + p2) * 0.5;

    return cv::Point2d(numeratorX / denominator, numeratorY / denominator);
}




//replace linear functions with quadriatic curves
//q(x) = ax^2 + bx + c
std::vector<bezierData> createCurve(const std::vector<EdgeGraph>& graphs, const std::vector<funcData>& functions, const imageData& image){
    //final curves
    std::vector<bezierData> curves;
    //checks
    const double minAngleTolerance = 2.0;
    //a 90 degree angle shouldnt have a curve, neither should a 180degree angle
    //we need a range
    const double maxAngleTolerance = 80.0;
    const double accuracyThreshold = 0.75;
    const double pixelTolerance = 1.0;

    //iterate through the components
    for(const EdgeGraph& graph: graphs){
        //traverse connecting paths
        for(const EdgePath& path: graph.paths){
            //make sure there is enough pixels for a curve
            if(path.nodeIDs.size()< 3){
                continue;
            }
            //group functions in this path
            std::vector<const funcData*> pathLines;
            //iterate through individual paths
            for(const funcData& function : functions){
                //make sure we are checking the current function for the path and component
                if(function.edgeID == graph.componentID && function.pathID == path.id){
                    pathLines.push_back(&function);
                }
            }
            int lineCount = static_cast<int>(pathLines.size());
            //combine at least 2 functions, header--> sliding tail
            if(lineCount < 2){
                continue;
            }
            int head = 0;
            //always the function inf front of the head, or merged heads
            int tempTail = head + 1;
            bezierData lastValidCurve;
            bool hasValidCurve = false;
            int lastValidTail = -1;
            //begin merging
            while(head < lineCount - 1){

                if(tempTail >= lineCount){
                    if(hasValidCurve){
                        lastValidCurve.id = static_cast<int>(curves.size());
                        lastValidCurve.edgeID = graph.componentID;

                        curves.push_back(lastValidCurve);

                        head = lastValidTail + 1;
                        hasValidCurve = false;
                        lastValidTail = -1;

                        tempTail = head + 1;
                        continue;
                    }
                    break;
                }

                const funcData& previousLine = *pathLines[tempTail - 1];
                const funcData& nextLine = *pathLines[tempTail];
                //find current angle difference between head and tail
                //determine if we need a curve here
                double angle = calculateAngle(previousLine, nextLine);
                //only potential curve within a specific range
                //adjust min and max for specific use case
                if(angle >= minAngleTolerance && angle<= maxAngleTolerance || hasValidCurve && angle <= maxAngleTolerance){
                    //potential curve predicted
                    bezierData candidate;
                    
                    candidate.edgeID = graph.componentID;
                    candidate.pathID = path.id;
                    candidate.startLineID = pathLines[head]->id;
                    candidate.endLineID = pathLines[tempTail]->id;
                    //add the second functions pixels
                    for(int i = head; i <= tempTail; i++){
                        const funcData& line = *pathLines[i];

                        for(const cv::Point& pixel : line.pixels){
                            if(candidate.pixels.empty() || pixel != candidate.pixels.back()){
                                candidate.pixels.push_back(pixel);
                            }
                        }
                    }
                    //new endpoints
                    candidate.p0 = candidate.pixels.front();
                    candidate.p2 = candidate.pixels.back();

                    //find the center weight for the bezier curve
                    candidate.p1 = calculateControlPoint(candidate.pixels);
                    candidate.accuracy = checkCurveAccuracy(candidate, pixelTolerance);

                    std::cout << "Lines "
                    << candidate.startLineID << "-"
                    << candidate.endLineID
                    << " | Accuracy: " << candidate.accuracy
                    << " | Pixels: " << candidate.pixels.size()
                    << std::endl;


                    if(candidate.accuracy >= accuracyThreshold){
                        //use piecewise function
                        lastValidCurve = candidate;
                        hasValidCurve = true;
                        lastValidTail = tempTail;
                        tempTail++;
                    }
                    else{
                        //failed candidate
                        if(hasValidCurve){
                            lastValidCurve.id = static_cast<int>(curves.size());
                            lastValidCurve.edgeID = graph.componentID;
                            curves.push_back(lastValidCurve);
                            head = lastValidTail + 1;
                        }
                        else{
                            //move head
                            head++;
                        }
                        tempTail = head + 1;
                        //previous function failed
                        hasValidCurve = false;
                        lastValidTail = -1;
                    }

                }
                else{
                    //linear function
                    if(hasValidCurve == true){
                        lastValidCurve.id = static_cast<int>(curves.size());
                        lastValidCurve.edgeID = graph.componentID;
                        curves.push_back(lastValidCurve);
                        head = lastValidTail + 1;
                    }
                    else{
                        head++;
                    }
                    tempTail = head + 1;
                    hasValidCurve = false;
                    lastValidTail = -1;
                }

            }

        }
    }
    return curves;
    }



//reduce quadriatic and linear functions by merging them together into a piecewise function
std::vector<piecewiseData> reduceEdges(const std::vector<EdgeGraph>& graphs, const std::vector<funcData>& functions, const std::vector<bezierData>& curves){
    std::vector<piecewiseData>result;
    //traverse through components
    for(const EdgeGraph& graph: graphs){
        //iterate through paths
        for(const EdgePath& path: graph.paths){
            piecewiseData piecewise;
            piecewise.edgeID = graph.componentID;
            piecewise.pathID = path.id;
            //group linear functions from the path
            std::vector<const funcData*> pathLines;
            //iterate through each function in a path
            for(const funcData& function: functions){
                if(function.edgeID == graph.componentID && function.pathID == path.id){
                    pathLines.push_back(&function);
                }
            }
            //ordered functions
            int i = 0;
            while(i<static_cast<int>(pathLines.size())){
                const funcData& currentLine = *pathLines[i];
                //check bezier connects
                const bezierData* matchingCurve = nullptr;
                //check the bezier curves
                for(const bezierData& curve: curves){
                    if(curve.edgeID == graph.componentID && curve.pathID == path.id && curve.startLineID == currentLine.id){
                        matchingCurve = &curve;
                        break;
                    }
                }
                if(matchingCurve != nullptr){
                    //if the bezier starts at the end of the linear function, use the bezier approximation rather than the linear function
                    piecewise.segments.push_back(*matchingCurve);

                    while(i < static_cast<int>(pathLines.size()) && pathLines[i]->id <= matchingCurve->endLineID){
                        i++;
                    }
                }
                else{
                    piecewise.segments.push_back(currentLine);
                    i++;
                }
            }
            if(!piecewise.segments.empty()){
                result.push_back(std::move(piecewise));
            }
        }
    }
    return result;
}




cv::Mat displayBezier(
    const std::vector<bezierData>& curves,
    const imageData& image
){
    cv::Mat display = cv::Mat::zeros(
        image.height,
        image.width,
        CV_8UC3
    );

    for(const bezierData& curve : curves){

        cv::Point previous = curve.p0;

        for(int i = 1; i <= 1000; i++){

            double t = static_cast<double>(i) / 1000.0;
            double u = 1.0 - t;

            cv::Point2d point =
                (u * u) * curve.p0 +
                (2.0 * u * t) * curve.p1 +
                (t * t) * curve.p2;

            cv::line(
                display,
                previous,
                cv::Point(cvRound(point.x), cvRound(point.y)),
                cv::Scalar(0, 255, 0),
                1
            );

            previous = cv::Point(
                cvRound(point.x),
                cvRound(point.y)
            );
        }
    }

    return display;
}






void displayFunctions(
    const std::vector<funcData>& functions,
    const imageData& image
) {
    // Create a black canvas
    cv::Mat display = cv::Mat::zeros(
        image.height,
        image.width,
        CV_8UC3
    );

    cv::RNG rng(12345);

    for (const funcData& function : functions) {

        // Assign a different color to each function
        cv::Scalar color(
            rng.uniform(80, 256),
            rng.uniform(80, 256),
            rng.uniform(80, 256)
        );

        // Draw the fitted line segment
        cv::Point2d start = function.start;
        cv::Point2d end = function.end;

        // Project each endpoint onto the fitted line
        double denominator =
            function.a * function.a +
            function.b * function.b;

        if (denominator < 1e-12) {
            continue;
        }

        auto project = [&](const cv::Point2d& p) {
            double distance =
                (function.a * p.x +
                function.b * p.y +
                function.c) / denominator;

            return cv::Point2d(
                p.x - function.a * distance,
                p.y - function.b * distance
            );
        };

        cv::Point2d fittedStart = project(start);
        cv::Point2d fittedEnd = project(end);

        cv::line(
            display,
            fittedStart,
            fittedEnd,
            color,
            2,
            cv::LINE_AA
        );

        // Label the function
        cv::Point midpoint(
            (function.start.x + function.end.x) / 2,
            (function.start.y + function.end.y) / 2
        );

        cv::putText(
            display,
            std::to_string(function.id),
            midpoint,
            cv::FONT_HERSHEY_SIMPLEX,
            0.4,
            color,
            1,
            cv::LINE_AA
        );
    }
    cv::imshow("Fitted Functions", display);
}







int main() {

    std::string path = R"(F:\CARLA set\sets\modifiedCurve.jpg)";

    // 1. Generate the skeleton
    cv::Mat skeleton = sobelEdgeDetector(path);

    std::cout << "Skeleton pixels: "
            << cv::countNonZero(skeleton) << std::endl;

    std::cout << "Skeleton dimensions: "
            << skeleton.cols << " x "
            << skeleton.rows << std::endl;


    // 2. Group connected pixels
    std::vector<EdgeData> components = groupPixels(skeleton);

    // 3. Initialize image data
    imageData image;

    image.originalImage = cv::imread(path);

    if(image.originalImage.empty()){
        std::cerr << "Failed to load image!" << std::endl;
        return 1;
    }

    image.width = image.originalImage.cols;
    image.height = image.originalImage.rows;
    image.skeleton = skeleton;

    // 4. Convert components into graphs
    std::vector<EdgeGraph> graphs = map(components);

    // 5. Find paths within each graph
    for(EdgeGraph& graph : graphs){

        if(graph.nodes.empty()){
            continue;
        }

        graph.paths = findPaths(graph, 0);
    }

    // 6. Fit linear functions
    std::vector<funcData> functions = drawLine(graphs, image);

    // 7. Fit quadratic Bézier curves
    std::vector<bezierData> curves =
        createCurve(graphs, functions, image);

    // 8. Display results
    std::cout << "\nOriginal linear functions: "
              << functions.size() << std::endl;

    std::cout << "Accepted Bezier curves: "
              << curves.size() << std::endl;

    for(const bezierData& curve : curves){

        std::cout << "Curve " << curve.id
                  << " | Component: " << curve.edgeID
                  << " | Path: " << curve.pathID
                  << " | Replaces lines: "
                  << curve.startLineID << " - "
                  << curve.endLineID
                  << " | Pixels: " << curve.pixels.size()
                  << " | Accuracy: " << curve.accuracy
                  << std::endl;
    }

    // 9. Display skeleton
    cv::imshow("Skeleton", skeleton);
    cv::Mat bezierDisplay = displayBezier(curves, image);

    cv::imshow("Bezier Curves", bezierDisplay);

    cv::waitKey(0);
    cv::destroyAllWindows();

    return 0;
}
