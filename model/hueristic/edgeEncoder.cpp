#include<opencv2/opencv.hpp>
#include<iostream>
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
#include <opencv2/ximgproc.hpp>

//pixels with edge neighbors that are not lines
struct RegionConnection {
    cv::Point inside;
    cv::Point outside;
};

struct PixelRegion {
    int id;
    int componentID;

    std::vector<cv::Point> pixels;
    std::vector<RegionConnection> connections;
};

//group of pixels that share a connection
struct EdgeLine {
    int id;
    int componentID;

    std::vector<cv::Point> pixels;

    bool closed = false;

    // Eventually store fitted function here
};

//create a directional component for lines
// An individual ordered path
struct TracedLine {
    int id;
    int componentID;

    std::vector<cv::Point> pixels;

    bool closed = false;
};

//class types for specific pixels in a line
enum class PointType {
    ENDPOINT,
    INTERMEDIATE,
    JUNCTION,
    ISOLATED
};

struct EdgePoint {
    int x;
    int y;

    std::vector<cv::Point> neighbors;
    PointType type = PointType::ISOLATED;
};

// A group of connected edge pixels
struct EdgeComponent {
    int id;

    std::vector<EdgePoint> points;
    std::vector<cv::Point> endpoints;
    std::vector<cv::Point> junctions;
};

struct EdgeGraph {
    std::vector<EdgeComponent> components;
    std::vector<TracedLine> tracedLines;

    std::vector<PixelRegion> regions;
};

using EdgeKey = std::pair<uint64_t, uint64_t>;

struct Edges{
    int x;
    int y;
    int strength;
};

struct EdgeData {
    int height = 0;
    int width = 0;
    std::vector<std::pair<double, double>> pixel_dist;
};



struct PixelPairHash {
    size_t operator()(
        const std::pair<uint64_t, uint64_t>& edge
    ) const {
        size_t h1 = std::hash<uint64_t>{}(edge.first);
        size_t h2 = std::hash<uint64_t>{}(edge.second);

        return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    }
};

uint64_t pixelKey(int x, int y) {
    return (uint64_t(uint32_t(y)) << 32) | uint32_t(x);
}

//sobel edge detection over canny, because all the images are generated and already have clean noise free edges
//'&' use reference rather than creating a copy of the memory location
EdgeData edge_encoder(std::string& path)
{
   //'cv' means use the function from the dependency
    cv::Mat img = cv::imread(path, cv::IMREAD_GRAYSCALE);
    if (img.empty()){
        std::cout<<"Found the error!"<<std::endl;
        return {};
    }
    //specialized array for images, different to how we did in c
    cv::Mat sobelx, sobely, gradient;

    cv::Sobel(img, sobelx, CV_64F, 1, 0, 3);
    cv::Sobel(img, sobely, CV_64F, 0, 1, 3);

    std::cout<<"Found error after sobel detection!"<<std::endl;

    cv::magnitude(sobelx, sobely, gradient);
    //holds the pixel data
    cv::Mat gradient_abs;
    cv::convertScaleAbs(gradient, gradient_abs);
    //display
    std::cout<<"Found error while displaying image!"<<std::endl;
    cv::imshow("Example Edge", gradient_abs);
    cv::waitKey(0);
    //display the matrix of edges
    //cv::convertScaleAbs(gradient, gradient_abs);
    //std::cout << gradient_abs << std::endl;
    //std::cout << gradient_abs << std::endl;

    int height = gradient_abs.rows;
    int width = gradient_abs.cols;
    //set those values for the struct
    EdgeData edgeData;
    edgeData.height = height;
    edgeData.width = width;

    std::cout<<"Height:"<<height<<"\n";
    std::cout<<"Width:"<<width<<"\n";

    //find center
    double center_x = (width-1) / 2.0;
    double center_y = (height-1) / 2.0;
    int threshold = 50;
    //find distance between edge pixels and center point
    std::vector<cv::Point> edge_pixels = {};
    std::vector<std::pair<double, double>> pixel_dist;
    for(int i = 0; i<height; i++){
        for(int j = 0; j < width; j++){
            
            //gradient change in a pixel
            uchar gradVal = gradient_abs.at<uchar>(i, j);
            if (gradVal >= threshold){
                edge_pixels.push_back(cv::Point(j,i));
                //use manhanttan distance, so we can rebuild the structure
                double temp_x = j;
                double temp_y = i;
                double manhattan_x = center_x - temp_x;
                double manhattan_y = center_y - temp_y;
                //double manhattan_tot = manhattan_x + manhattan_y;
                std::pair<double, double> temp_dist = {manhattan_x, manhattan_y};
                //matrix of all distance of edge pixels
                edgeData.pixel_dist.push_back(temp_dist);
            }
        }
    }
    return edgeData;
}

void draw_edges(const EdgeData& data)
{
    cv::Mat edge_frame = cv::Mat::zeros(
        data.height,
        data.width,
        CV_8UC1
    );

    double center_x = (data.width - 1) / 2.0;
    double center_y = (data.height - 1) / 2.0;

    for (const auto& [dx, dy] : data.pixel_dist) {
        int x = static_cast<int>(std::round(center_x - dx));
        int y = static_cast<int>(std::round(center_y - dy));

        if (x >= 0 && x < data.width &&
            y >= 0 && y < data.height) {
            edge_frame.at<uchar>(y, x) = 255;
        }
    }

    cv::imshow("Reconstructed Edges", edge_frame);
    cv::waitKey(0);
}

EdgeData normalize(const EdgeData& data)
{
    EdgeData normalizedData;

    normalizedData.width = data.width;
    normalizedData.height = data.height;

    double centerX = data.width / 2.0;
    double centerY = data.height / 2.0;

    for (const auto& [dx, dy] : data.pixel_dist) {

        double normalizedX = dx / centerX;
        double normalizedY = dy / centerY;

        normalizedData.pixel_dist.emplace_back(
            normalizedX,
            normalizedY
        );
    }

    return normalizedData;
}

int find_matches(const EdgeData& data, const EdgeData& input){
//sliding window
int input_height = input.height;
int input_width = input.width;
int window_height = input_height;
int window_width =input_width;
int stride_x = std::max(1, window_width / 10);
int stride_y = std::max(1, window_height / 10);
bool match = false;
double bestMatch = 0.0;

while(match != true){
    for(int i = 0; i<= input_height; i+=stride_y){
        for(int j = 0; j<= input_width; j+= stride_x){

        }
    }
}
return 1;
}

EdgeGraph draw_component(const cv::Mat& edges) {

    EdgeGraph graph;

    int height = edges.rows;
    int width = edges.cols;

    cv::Mat visited = cv::Mat::zeros(
        height, width, CV_8UC1
    );

    int lineID = 0;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {

            if (edges.at<uchar>(y, x) == 0 ||
                visited.at<uchar>(y, x) != 0) {
                continue;
            }

            EdgeComponent line;
            line.id = lineID++;

            std::queue<cv::Point> queue;
            queue.push(cv::Point(x, y));

            visited.at<uchar>(y, x) = 1;

            while (!queue.empty()) {

                cv::Point current = queue.front();
                queue.pop();

                EdgePoint point;
                point.x = current.x;
                point.y = current.y;

                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {

                        if (dx == 0 && dy == 0) {
                            continue;
                        }

                        int nx = current.x + dx;
                        int ny = current.y + dy;

                        if (nx < 0 || nx >= width ||
                            ny < 0 || ny >= height) {
                            continue;
                        }

                        if (edges.at<uchar>(ny, nx) != 0) {

                            point.neighbors.push_back(
                                cv::Point(nx, ny)
                            );

                            if (visited.at<uchar>(ny, nx) == 0) {

                                visited.at<uchar>(ny, nx) = 1;

                                queue.push(
                                    cv::Point(nx, ny)
                                );
                            }
                        }
                    }
                }

                int connections = point.neighbors.size();

                if (connections == 0) {
                    point.type = PointType::ISOLATED;
                }
                else if (connections == 1) {
                    point.type = PointType::ENDPOINT;

                    line.endpoints.push_back(
                        cv::Point(point.x, point.y)
                    );
                }
                else if (connections == 2) {
                    point.type = PointType::INTERMEDIATE;
                }
                else {
                    point.type = PointType::JUNCTION;

                    line.junctions.push_back(
                        cv::Point(point.x, point.y)
                    );
                }

                line.points.push_back(point);
            }

            graph.components.push_back(std::move(line));
        }
    }

    return graph;
}

EdgeGraph trace_line(EdgeGraph graph) {

    int tracedID = 0;

    for (const EdgeComponent& component : graph.components) {

        std::unordered_map<uint64_t, const EdgePoint*> lookup;

        auto pixelKey = [](const cv::Point& p) -> uint64_t {
            return (uint64_t(uint32_t(p.y)) << 32) |
                    uint32_t(p.x);
        };

        auto connectionKey = [&](const cv::Point& a,
                                 const cv::Point& b) -> EdgeKey {

            uint64_t keyA = pixelKey(a);
            uint64_t keyB = pixelKey(b);

            return std::minmax(keyA, keyB);
        };

        for (const EdgePoint& point : component.points) {
            lookup[pixelKey(cv::Point(point.x, point.y))] = &point;
        }

        std::unordered_set<EdgeKey, PixelPairHash> visitedEdges;

        auto traceFrom = [&](const cv::Point& start,
                             const cv::Point& firstNeighbor) {

            TracedLine traced;
            traced.id = tracedID++;
            traced.componentID = component.id;
            traced.closed = false;

            traced.pixels.push_back(start);

            cv::Point previous = start;
            cv::Point current = firstNeighbor;

            visitedEdges.insert(connectionKey(previous, current));

            while (true) {

                traced.pixels.push_back(current);

                if (current == start) {
                    traced.closed = true;
                    break;
                }

                auto it = lookup.find(pixelKey(current));

                if (it == lookup.end()) {
                    break;
                }

                const EdgePoint* currentPoint = it->second;

                if (currentPoint->type == PointType::ENDPOINT ||
                    currentPoint->type == PointType::JUNCTION) {
                    break;
                }

                bool foundNext = false;
                cv::Point next;

                for (const cv::Point& neighbor :
                     currentPoint->neighbors) {

                    if (neighbor == previous) {
                        continue;
                    }

                    EdgeKey key = connectionKey(current, neighbor);

                    if (visitedEdges.count(key) != 0) {
                        continue;
                    }

                    next = neighbor;
                    visitedEdges.insert(key);
                    foundNext = true;
                    break;
                }

                if (!foundNext) {
                    break;
                }

                previous = current;
                current = next;
            }

            graph.tracedLines.push_back(std::move(traced));
        };

        for (const EdgePoint& point : component.points) {

            if (point.type != PointType::ENDPOINT &&
                point.type != PointType::JUNCTION) {
                continue;
            }

            cv::Point start(point.x, point.y);

            for (const cv::Point& neighbor : point.neighbors) {

                EdgeKey key = connectionKey(start, neighbor);

                if (visitedEdges.count(key) == 0) {
                    traceFrom(start, neighbor);
                }
            }
        }

        for (const EdgePoint& point : component.points) {

            cv::Point start(point.x, point.y);

            for (const cv::Point& neighbor : point.neighbors) {

                EdgeKey key = connectionKey(start, neighbor);

                if (visitedEdges.count(key) == 0) {
                    traceFrom(start, neighbor);
                }
            }
        }

        for (const EdgePoint& point : component.points) {

            if (point.type == PointType::ISOLATED) {

                TracedLine traced;
                traced.id = tracedID++;
                traced.componentID = component.id;
                traced.closed = false;

                traced.pixels.push_back(cv::Point(point.x, point.y));

                graph.tracedLines.push_back(std::move(traced));
            }
        }
    }

    return graph;
}

cv::Mat create_edge_map(const EdgeData& data) {

    cv::Mat edgeMap = cv::Mat::zeros(
        data.height,
        data.width,
        CV_8UC1
    );

    double centerX = data.width / 2.0;
    double centerY = data.height / 2.0;

    double originalCenterX = (data.width - 1) / 2.0;
    double originalCenterY = (data.height - 1) / 2.0;

    for (const auto& [dx, dy] : data.pixel_dist) {

        double originalDX = dx * centerX;
        double originalDY = dy * centerY;

        int x = static_cast<int>(
            std::round(originalCenterX - originalDX)
        );

        int y = static_cast<int>(
            std::round(originalCenterY - originalDY)
        );

        if (x >= 0 && x < data.width &&
            y >= 0 && y < data.height) {

            edgeMap.at<uchar>(y, x) = 255;
        }
    }

    return edgeMap;
}

//if a pixel has multiple neighbors, reduce the region of neighbors into one component
EdgeGraph reduce_junctions(EdgeGraph graph) {

    int regionID = 0;

    for (const EdgeComponent& component : graph.components) {
//search for components with many shared connections
        std::unordered_map<uint64_t, const EdgePoint*> lookup;
        std::unordered_set<uint64_t> visited;

        for (const EdgePoint& pixel : component.points) {
            lookup[pixelKey(pixel.x, pixel.y)] = &pixel;
        }

        for (const EdgePoint& pixel : component.points) {

            if (pixel.neighbors.size() < 4) {
                continue;
            }

            uint64_t key = pixelKey(pixel.x, pixel.y);

             //track which pixels have been accounted for
            if (visited.count(key)) {
                continue;
            }
             //reduce to one component, pixel region
            PixelRegion region;
            region.id = regionID++;
            region.componentID = component.id;

            //bfs to reconnect lines together
            std::queue<cv::Point> queue;
            queue.push(cv::Point(pixel.x, pixel.y));

            visited.insert(key);

            while (!queue.empty()) {
                cv::Point current = queue.front();
                queue.pop();
                region.pixels.push_back(current);

                uint64_t currentKey = pixelKey(current.x, current.y);
                const EdgePoint* currentPixel = lookup.at(currentKey);

                for (const cv::Point& neighbor : currentPixel->neighbors) {
                    uint64_t neighborKey = pixelKey(neighbor.x, neighbor.y);

                    if (visited.count(neighborKey)) {
                        continue;
                    }
                    auto it = lookup.find(neighborKey);

                    if (it == lookup.end()) {
                        continue;
                    }
                    const EdgePoint* neighborPixel = it->second;

                    if (neighborPixel->neighbors.size() >= 4) {
                        visited.insert(neighborKey);
                        queue.push(neighbor);
                    }
                }
            }

            graph.regions.push_back(std::move(region));
        }
    }

    return graph;
}


int main() {

    std::string path =
    R"(F:\CARLA set\renders\sketchfab\0a1b51688d5f474bba3d306b872a3dc8\003.png)";

    EdgeData data = edge_encoder(path);

    EdgeData normalized = normalize(data);

    cv::Mat edgeMap = create_edge_map(normalized);

    cv::imshow("Original Edge Map", edgeMap);

    cv::Mat skeleton;
    cv::ximgproc::thinning(edgeMap,skeleton,cv::ximgproc::THINNING_ZHANGSUEN);
    cv::imshow("Skeletonized Edge Map", skeleton);

    EdgeGraph graph = draw_component(skeleton);

    graph = reduce_junctions(std::move(graph));

    graph = trace_line(std::move(graph));

    cv::Mat regionDisplay;
    cv::cvtColor(skeleton, regionDisplay, cv::COLOR_GRAY2BGR);

    for (const PixelRegion& region : graph.regions) {
        for (const cv::Point& pixel : region.pixels) {
            regionDisplay.at<cv::Vec3b>(pixel.y, pixel.x) =
                cv::Vec3b(0, 0, 255);
        }
    }

    cv::imshow("Junction Regions", regionDisplay);


    cv::waitKey(0);
    cv::destroyAllWindows();

    return 0;
}
