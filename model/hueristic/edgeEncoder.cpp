#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/ximgproc.hpp>

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


//this is for when we transform the pixels coordinates into function plots
struct FittedLine {
    int id;
    int componentID;

    std::vector<cv::Point> pixels;

    double slope = 0.0;
    double intercept = 0.0;
    //70% accuracy of all pixels in the component.
    double accuracy = 0.0;

    bool vertical = false;
};

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

    std::vector<FittedLine> fittedLines;
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



EdgeGraph convert_graph(EdgeGraph graph) {
    const double minimumAccuracy = 0.70;
    const double tolerance = 1.0; // Perpendicular distance in image pixels.
    graph.fittedLines.clear();
    int nextID = 0;

    // Fit a robust, constant-direction line, including vertical lines.
    struct Fit {
        cv::Point2d origin;
        cv::Point2d direction;
        double accuracy = 0.0;
    };
    auto distance = [](const cv::Point& p, const Fit& f) {
        return std::abs((p.x - f.origin.x) * f.direction.y -
                        (p.y - f.origin.y) * f.direction.x);
    };
    auto fit = [&](const std::vector<cv::Point>& pixels) {
        Fit result;
        if (pixels.size() == 1) {
            result.origin = cv::Point2d(pixels.front().x, pixels.front().y);
            result.direction = cv::Point2d(1.0, 0.0);
            result.accuracy = 1.0;
            return result;
        }

    double meanX = 0.0;
    double meanY = 0.0;

    for (const cv::Point& p : pixels) {
        meanX += p.x;
        meanY += p.y;
    }

    meanX /= pixels.size();
    meanY /= pixels.size();

    double xx = 0.0;
    double xy = 0.0;
    double yy = 0.0;

    for (const cv::Point& p : pixels) {
        double dx = p.x - meanX;
        double dy = p.y - meanY;

        xx += dx * dx;
        xy += dx * dy;
        yy += dy * dy;
    }

    double angle = 0.5 * std::atan2(
        2.0 * xy,
        xx - yy
    );

    result.origin = cv::Point2d(meanX, meanY);

    result.direction = cv::Point2d(
        std::cos(angle),
        std::sin(angle)
    );

        size_t matches = 0;
        for (const auto& p : pixels)
            if (distance(p, result) <= tolerance) ++matches;
        result.accuracy = static_cast<double>(matches) / pixels.size();
        return result;
    };
    auto save = [&](int componentID, const std::vector<cv::Point>& pixels) {
        const Fit model = fit(pixels);
        FittedLine line{};
        line.id = nextID++;
        line.componentID = componentID;
        line.pixels = pixels;
        line.accuracy = model.accuracy;
        line.vertical = std::abs(model.direction.x) < 1e-6;
        if (line.vertical) {
            // For vertical lines, intercept stores x = intercept.
            line.intercept = model.origin.x;
        } else {
            line.slope = model.direction.y / model.direction.x;
            line.intercept = model.origin.y - line.slope * model.origin.x;
        }
        graph.fittedLines.push_back(std::move(line));
    };

    for (const EdgeComponent& component : graph.components) {
        std::unordered_map<uint64_t, const EdgePoint*> lookup;
        for (const auto& p : component.points)
            lookup[pixelKey(p.x, p.y)] = &p;
        auto key = [](const cv::Point& p) { return pixelKey(p.x, p.y); };
        auto edgeKey = [&](const cv::Point& a, const cv::Point& b) -> EdgeKey {
            const uint64_t ka = key(a), kb = key(b);
            return {std::min(ka, kb), std::max(ka, kb)};
        };
        std::unordered_set<EdgeKey, PixelPairHash> used;
        auto neighbors = [&](const cv::Point& p) {
            std::vector<cv::Point> result;
            const auto it = lookup.find(key(p));
            if (it != lookup.end()) {
                for (const auto& n : it->second->neighbors)
                    if (lookup.count(key(n)) && n != p &&
                        !used.count(edgeKey(p, n))) result.push_back(n);
            }
            return result;
        };

        auto trace = [&](const cv::Point& start) {
            std::vector<cv::Point> pixels{start};
            // Pointers refer to stable EdgePoint objects, never vector elements.
            const EdgePoint* tail = lookup.at(key(start));
            const EdgePoint* tempHead = tail;
            cv::Point current = start;
            while (true) {
                const auto options = neighbors(current);
                if (options.empty()) break;

                // BFS through a junction cluster. For each exit, retain its
                // shortest route independently. Do not consume rejected routes.
                std::vector<std::vector<cv::Point>> routes;
                std::queue<std::vector<cv::Point>> pending;
                std::unordered_set<uint64_t> discovered{key(current)};
                for (const auto& n : options) {
                    if (discovered.insert(key(n)).second) pending.push({n});
                }
                while (!pending.empty()) {
                    auto route = std::move(pending.front());
                    pending.pop();
                    const cv::Point end = route.back();
                    const auto next = neighbors(end);
                    if (lookup.at(key(end))->neighbors.size() <= 2 || next.empty()) {
                        routes.push_back(std::move(route));
                        continue;
                    }
                    bool expanded = false;
                    for (const auto& n : next) {
                        if (!discovered.insert(key(n)).second) continue;
                        auto extension = route;
                        extension.push_back(n);
                        pending.push(std::move(extension));
                        expanded = true;
                    }
                    if (!expanded) routes.push_back(std::move(route));
                }

                size_t best = routes.size();
                double bestError = 0.0;
                for (size_t i = 0; i < routes.size(); ++i) {
                    auto trial = pixels;
                    bool accepted = true;
                    double error = 0.0;
                    for (const auto& p : routes[i]) {
                        trial.push_back(p);
                        const Fit model = fit(trial);
                        // Require 70% of the PREVIOUS pixels, and explicitly
                        // require the new head and fixed tail to remain inliers.
                        size_t previousMatches = 0;
                        for (size_t j = 0; j + 1 < trial.size(); ++j)
                            if (distance(trial[j], model) <= tolerance)
                                ++previousMatches;
                        if (static_cast<double>(previousMatches) /
                                (trial.size() - 1) < minimumAccuracy ||
                            distance(p, model) > tolerance ||
                            distance(cv::Point(tail->x, tail->y), model) > tolerance) {
                            accepted = false;
                            break;
                        }
                        error += distance(p, model);
                    }
                    if (accepted && (best == routes.size() ||
                        routes[i].size() < routes[best].size() ||
                        (routes[i].size() == routes[best].size() && error < bestError))) {
                        best = i;
                        bestError = error;
                    }
                }
                if (best == routes.size()) {
                    // A cluster exit can fail even when its first few pixels
                    // fit. Try each immediate neighbor before splitting.
                    for (const auto& n : options) {
                        auto trial = pixels;
                        trial.push_back(n);
                        const Fit model = fit(trial);
                        size_t matches = 0;
                        for (const auto& p : pixels)
                            if (distance(p, model) <= tolerance) ++matches;
                        if (static_cast<double>(matches) / pixels.size() >= minimumAccuracy &&
                            distance(n, model) <= tolerance &&
                            distance(cv::Point(tail->x, tail->y), model) <= tolerance) {
                            routes.push_back({n});
                            best = routes.size() - 1;
                            break;
                        }
                    }
                }
                if (best == routes.size()) {
                    save(component.id, pixels);
                    // Share the last accepted pixel to keep segments connected.
                    pixels.assign(1, current);
                    tail = lookup.at(key(current));
                    // Two distinct pixels always define an acceptable line.
                    const cv::Point n = options.front();
                    used.insert(edgeKey(current, n));
                    pixels.push_back(n);
                    current = n;
                    tempHead = lookup.at(key(current));
                    continue;
                }
                for (const auto& p : routes[best]) {
                    used.insert(edgeKey(current, p));
                    pixels.push_back(p);
                    current = p;
                    tempHead = lookup.at(key(current));
                }
                if (tempHead == tail) break; // Closed loop.
            }
            if (pixels.size() > 1) save(component.id, pixels);
        };

        // Start at endpoints, then cover branches, cycles and cluster remnants.
        for (const auto& p : component.points) {
            const cv::Point start(p.x, p.y);
            if (p.neighbors.empty()) save(component.id, {start});
            else if (p.neighbors.size() == 1 && !neighbors(start).empty()) trace(start);
        }
        for (const auto& p : component.points) {
            const cv::Point start(p.x, p.y);
            while (!neighbors(start).empty()) trace(start);
        }
    }
    return graph;
}



double calculate_accuracy(
    const std::vector<cv::Point>& visitedPixels,
    double m,
    double b,
    bool vertical,
    double verticalX,
    double tolerance = 1.0
) {
    if (visitedPixels.empty()) {
        return 0.0;
    }
    int matches = 0;

    for (const cv::Point& pixel : visitedPixels) {
        double distance;
        if (vertical) {
            distance = std::abs(pixel.x - verticalX);
        }
        else {
            distance = std::abs(
                m * pixel.x - pixel.y + b
            ) / std::sqrt(m * m + 1.0);
        }
        if (distance <= tolerance) {
            matches++;
        }
    }
    return static_cast<double>(matches)
         / visitedPixels.size();
}

EdgeGraph merge_lines(
    EdgeGraph graph,
    double maxEndpointGap = 2.0,    // Pixels
    double maxAngleDegrees = 10.0,
    double tolerance = 1.0,         // Perpendicular distance in pixels
    double minimumAccuracy = 0.70
) {
    if (maxEndpointGap < 0.0 ||
        maxAngleDegrees <= 0.0 || maxAngleDegrees > 90.0 ||
        tolerance <= 0.0 ||
        minimumAccuracy <= 0.0 || minimumAccuracy > 1.0) {
        CV_Error(cv::Error::StsBadArg, "Invalid merge_lines thresholds");
    }

    struct Model {
        cv::Point2d origin;
        cv::Point2d direction;
    };

    auto fit = [](const std::vector<cv::Point>& pixels) {
        cv::Vec4f result;
        cv::fitLine(
            pixels, result,
            cv::DIST_L1, 0.0, 0.01, 0.01
        );

        Model model;
        model.origin = cv::Point2d(result[2], result[3]);

        const double length = std::hypot(result[0], result[1]);
        model.direction = cv::Point2d(
            result[0] / length,
            result[1] / length
        );

        return model;
    };

    auto distance = [](const cv::Point& p, const Model& model) {
        const double dx = p.x - model.origin.x;
        const double dy = p.y - model.origin.y;

        return std::abs(
            dx * model.direction.y -
            dy * model.direction.x
        );
    };

    auto accuracy = [&](const std::vector<cv::Point>& pixels,
                        const Model& model) {
        size_t matches = 0;

        for (const auto& p : pixels) {
            if (distance(p, model) <= tolerance) {
                ++matches;
            }
        }

        return pixels.empty()
            ? 0.0
            : static_cast<double>(matches) / pixels.size();
    };

    // Find the full extent along the fitted direction.
    // This also works when the input pixels are not ordered.
    auto endpoints = [](const std::vector<cv::Point>& pixels,
                        const Model& model) {
        auto projection = [&](const cv::Point& p) {
            return (p.x - model.origin.x) * model.direction.x +
                   (p.y - model.origin.y) * model.direction.y;
        };

        double minimum = projection(pixels.front());
        double maximum = minimum;

        for (const auto& p : pixels) {
            const double t = projection(p);
            minimum = std::min(minimum, t);
            maximum = std::max(maximum, t);
        }

        return std::make_pair(
            model.origin + model.direction * minimum,
            model.origin + model.direction * maximum
        );
    };

    auto pointDistance = [](const cv::Point2d& a,
                            const cv::Point2d& b) {
        return std::hypot(a.x - b.x, a.y - b.y);
    };

    auto& lines = graph.fittedLines;
    const double radiansToDegrees = 180.0 / std::acos(-1.0);

    while (true) {
        // Cache fits once per merge iteration.
        std::vector<Model> models(lines.size());

        for (size_t i = 0; i < lines.size(); ++i) {
            if (lines[i].pixels.size() >= 2) {
                models[i] = fit(lines[i].pixels);
            }
        }

        size_t bestI = lines.size();
        size_t bestJ = lines.size();
        double bestScore = 0.0;
        FittedLine bestMerged{};

        for (size_t i = 0; i < lines.size(); ++i) {
            if (lines[i].pixels.size() < 2) {
                continue;
            }

            const auto endsA = endpoints(lines[i].pixels, models[i]);

            for (size_t j = i + 1; j < lines.size(); ++j) {
                if (lines[j].pixels.size() < 2 ||
                    lines[i].componentID != lines[j].componentID) {
                    continue;
                }

                // Absolute dot product treats opposite traversal
                // directions as the same line orientation.
                double cosine = std::abs(
                    models[i].direction.dot(models[j].direction)
                );
                cosine = std::max(0.0, std::min(1.0, cosine));

                const double angle =
                    std::acos(cosine) * radiansToDegrees;

                if (angle > maxAngleDegrees) {
                    continue;
                }

                const auto endsB =
                    endpoints(lines[j].pixels, models[j]);

                const double gap = std::min(
                    std::min(
                        pointDistance(endsA.first, endsB.first),
                        pointDistance(endsA.first, endsB.second)
                    ),
                    std::min(
                        pointDistance(endsA.second, endsB.first),
                        pointDistance(endsA.second, endsB.second)
                    )
                );

                if (gap > maxEndpointGap) {
                    continue;
                }

                // Combine pixels without counting shared pixels twice.
                std::vector<cv::Point> combined = lines[i].pixels;
                combined.insert(
                    combined.end(),
                    lines[j].pixels.begin(),
                    lines[j].pixels.end()
                );

                std::sort(
                    combined.begin(), combined.end(),
                    [](const cv::Point& a, const cv::Point& b) {
                        return a.x < b.x ||
                               (a.x == b.x && a.y < b.y);
                    }
                );

                combined.erase(
                    std::unique(combined.begin(), combined.end()),
                    combined.end()
                );

                const Model mergedModel = fit(combined);

                // Prevent a long segment from overwhelming a shorter one.
                if (accuracy(lines[i].pixels, mergedModel) < minimumAccuracy ||
                    accuracy(lines[j].pixels, mergedModel) < minimumAccuracy) {
                    continue;
                }

                const double mergedAccuracy =
                    accuracy(combined, mergedModel);

                if (mergedAccuracy < minimumAccuracy) {
                    continue;
                }

                double meanError = 0.0;
                for (const auto& p : combined) {
                    meanError += distance(p, mergedModel);
                }
                meanError /= combined.size();

                // Lower score means better compatibility.
                // Normalize terms so pixels and degrees are comparable.
                const double score =
                    angle / maxAngleDegrees +
                    gap / std::max(maxEndpointGap, 1e-9) +
                    meanError / tolerance;

                if (bestI != lines.size() && score >= bestScore) {
                    continue;
                }

                // Order pixels along the merged line so front/back
                // describe its full extent for drawing.
                std::sort(
                    combined.begin(), combined.end(),
                    [&](const cv::Point& a, const cv::Point& b) {
                        const double ta =
                            (a.x - mergedModel.origin.x) *
                                mergedModel.direction.x +
                            (a.y - mergedModel.origin.y) *
                                mergedModel.direction.y;

                        const double tb =
                            (b.x - mergedModel.origin.x) *
                                mergedModel.direction.x +
                            (b.y - mergedModel.origin.y) *
                                mergedModel.direction.y;

                        return ta < tb;
                    }
                );

                FittedLine merged{};
                merged.id = lines[i].id;
                merged.componentID = lines[i].componentID;
                merged.pixels = std::move(combined);
                merged.accuracy = mergedAccuracy;

                merged.vertical =
                    std::abs(mergedModel.direction.x) < 1e-6;

                if (merged.vertical) {
                    // Vertical-line convention: x = intercept.
                    merged.intercept = mergedModel.origin.x;
                } else {
                    merged.slope =
                        mergedModel.direction.y /
                        mergedModel.direction.x;

                    merged.intercept =
                        mergedModel.origin.y -
                        merged.slope * mergedModel.origin.x;
                }

                bestI = i;
                bestJ = j;
                bestScore = score;
                bestMerged = std::move(merged);
            }
        }

        if (bestI == lines.size()) {
            break;
        }

        lines[bestI] = std::move(bestMerged);
        lines.erase(lines.begin() + bestJ);
    }

    // Restore consecutive IDs after removing merged entries.
    for (size_t i = 0; i < lines.size(); ++i) {
        lines[i].id = static_cast<int>(i);
    }

    return graph;
}


int main() {

    std::string path =
        R"(F:\CARLA set\renders\sketchfab\0a1b51688d5f474bba3d306b872a3dc8\003.png)";

    // Step 1: Sobel edge detection
    EdgeData data = edge_encoder(path);

    if (data.width == 0 || data.height == 0) {
        return 1;
    }

    EdgeData normalized = normalize(data);

    cv::Mat edgeMap = create_edge_map(normalized);

    // Step 2: Skeletonize the edge map
    cv::Mat skeleton;

    cv::ximgproc::thinning(
        edgeMap,
        skeleton,
        cv::ximgproc::THINNING_ZHANGSUEN
    );

    // Step 3: Build the skeleton graph
    EdgeGraph graph = draw_component(skeleton);

    graph = reduce_junctions(std::move(graph));
    graph = trace_line(std::move(graph));

    // Step 4: Convert graph into fitted lines
    graph = convert_graph(std::move(graph));

    // Step 5: Create a blank image for fitted lines
    cv::Mat fittedMap = cv::Mat::zeros(
        skeleton.size(),
        CV_8UC1
    );

    // Draw each fitted line
    for (const FittedLine& line : graph.fittedLines) {

        if (line.pixels.empty()) {
            continue;
        }

        // Draw the fitted mathematical segment between
        // the first and last pixels
        cv::Point start = line.pixels.front();
        cv::Point end = line.pixels.back();

        cv::line(
            fittedMap,
            start,
            end,
            cv::Scalar(255),
            1,
            cv::LINE_8
        );
    }

    // Step 6: Display all three maps
    cv::imshow("1 - Original Edge Map", edgeMap);
    cv::imshow("2 - Skeleton Edge Map", skeleton);
    cv::imshow("3 - Fitted Edge Map", fittedMap);

    std::cout << "Fitted lines: "
              << graph.fittedLines.size()
              << std::endl;

    cv::waitKey(0);
    cv::destroyAllWindows();

    return 0;
}

