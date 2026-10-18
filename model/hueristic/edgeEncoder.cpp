#include<opencv2/opencv.hpp>
#include<iostream>
#include <string>
#include <vector>
#include <tuple>
#include <utility>

//group of pixels that share a connection
struct EdgeLine {
    int id;

    std::vector<cv::Point> pixels;

    cv::Point start;
    cv::Point end;

    bool closed = false;
};

enum class PointType {
    ENDPOINT,
    INTERMEDIATE,
    JUNCTION,
    ISOLATED
};

struct EdgeGraph {
    std::vector<EdgeLine> lines;
};

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

struct EdgePoint {
    int x;
    int y;

    std::vector<cv::Point> neighbors;
};

int contour_detection(std::string& path){
    return 1;
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
int stride_x = std::max(1, windowWidth/10);
int stide_y = std::max(1, window_height/10);
bool match = false;
double bestMatch = 0.0;

while(match != true){
    for(int i = 0; i<= input_height; i+=stride_y){
        for(int j = 0; j<= input_width; j+= stride_x){

        }
    }
}
}

EdgeGraph draw_line(const cv::Mat& edges) {

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

            EdgeLine line;
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

                // Check all 8 neighboring pixels
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

                // Classify AFTER checking all neighbors
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

                // Add each pixel exactly once
                line.points.push_back(point);
            }

            // Save the completed connected group
            graph.lines.push_back(line);
        }
    }

    return graph;
}


int main()
{
    //raw string, no escape keys
    std::cout << "Program started!" << std::endl;
    std::string testPath = R"(F:\CARLA set\renders\sketchfab\0a1b51688d5f474bba3d306b872a3dc8\003.png)";
    std::string testInput = R"(C:\Users\Acey\Downloads\box.jpg)";
    EdgeData edges = edge_encoder(testPath);
    EdgeData inputTest = edge_encoder(testInput);

    if (edges.width > 0 && edges.height > 0) {
        draw_edges(edges);
    }

    test_edges = normalize(edges);
    test_inputTest = normalize(edges);

}
