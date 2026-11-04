#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cmath>
#include <nlohmann/json.hpp>
#include <limits>
#include "edgeEncoder.h"

namespace fs = std::filesystem;
using json = nlohmann::json;
//error with imports
constexpr double PI = 3.14159265358979323846;

//struct for the bucket system look-up table
struct FeatureIndex {
    std::unordered_map<int, std::vector<int>> components;
    std::unordered_map<int, std::vector<int>> endpoints;
    std::unordered_map<int, std::vector<int>> junctions;
    std::unordered_map<int, std::vector<int>> loops;
    std::unordered_map<int, std::vector<int>> contourLength;
    std::unordered_map<int, std::vector<int>> enclosedArea;
};


//to better match functions in input
struct FunctionNeighbor {
    int functionID;
    int patternID = -1;

    //node system
    int relativePosition;
};

//keep track of functions that appear often, and their corresponding 3d data
struct FunctionOccurrence {
    std::string meshID;
    std::string imageID;

    int componentID;
    int pathID;
    int functionID;
};

struct FunctionPattern {
    int patternID;
    std::string type;
    std::vector<double> descriptor;
    std::vector<FunctionOccurrence> occurrences;
};


struct PathRecord {
    std::string meshID;
    std::string imageID;

    int componentID;
    int pathID;

    std::vector<int> functionIDs;
    std::vector<int> patternIDs;
};




std::vector<double> createDescriptor(const std::string& type, const json& function, double imageDiagonal) {
    std::vector<double> descriptor;

    if (imageDiagonal <= 0.0) {
        return descriptor;
    }
    if (type == "line") {
        const auto& start = function.at("start");
        const auto& end = function.at("end");

        double dx = end.at(0).get<double>() - start.at(0).get<double>();
        double dy = end.at(1).get<double>() - start.at(1).get<double>();

        double length = std::hypot(dx, dy);
        double angle = std::atan2(dy, dx);

        // Direction-independent orientation: [0, PI)
        angle = std::fmod(angle + PI, PI);

        descriptor = {angle / PI, length / imageDiagonal};
    }

    else if (type == "bezier") {
        const auto& p0 = function.at("p0");
        const auto& p1 = function.at("p1");
        const auto& p2 = function.at("p2");

        double x0 = p0.at(0).get<double>();
        double y0 = p0.at(1).get<double>();

        double x1 = p1.at(0).get<double>();
        double y1 = p1.at(1).get<double>();

        double x2 = p2.at(0).get<double>();
        double y2 = p2.at(1).get<double>();

        descriptor = {
            (x1 - x0) / imageDiagonal,
            (y1 - y0) / imageDiagonal,
            (x2 - x0) / imageDiagonal,
            (y2 - y0) / imageDiagonal
        };
    }

    return descriptor;
}




double descriptorDistance(const std::vector<double>& a, const std::vector<double>& b) {
    if (a.size() != b.size() || a.empty())
        return std::numeric_limits<double>::infinity();

    double sum = 0.0;

    for (size_t i = 0; i < a.size(); i++) {
        double diff = a[i] - b[i];

        // Circular orientation for linear functions
        if (a.size() == 2 && i == 0)
            diff = std::min(std::abs(diff),
                            1.0 - std::abs(diff));

        sum += diff * diff;
    }

    return std::sqrt(sum);
}



bool matchLinear(const std::vector<double>& a, const std::vector<double>& b, double angleThreshold, double lengthTolerance) {
    if(a.size() != 2 || b.size() != 2){
        return false;
    }

    // Angle difference
    double angleDiff = std::abs(a[0] - b[0]);
    angleDiff = std::min(angleDiff, 1.0 - angleDiff);
    double angleDegrees = angleDiff * 180.0;
    // relative length difference
    double maxLength = std::max(a[1], b[1]);

    if(maxLength <= 0.0)
        return false;
    double lengthDiff = std::abs(a[1] - b[1]) / maxLength;

    return angleDegrees <= angleThreshold && lengthDiff <= lengthTolerance;
}






void vectorizeToken(const fs::path& datasetRoot){
    //example output
    fs::path outputPath = datasetRoot / "function_patterns.txt";
    std::vector<FunctionPattern> patterns;
    std::vector<PathRecord> pathRecords;
    //traversal information
    int nextPatternID = 0;
    int totalImages = 0;
    int totalFunctions = 0;
    //judgement tolerance for angles
    double angleThreshold = 10.0;
    //tolernace for total arclength
    double lengthTolerance = 0.10;
    //traverse the meta data
    //iterate through each json file
    for(const auto& entry: fs::recursive_directory_iterator(datasetRoot)){
        //handle exceptiosn
        if(!entry.is_regular_file()){
            continue;
        }
        //multiple file types in the current dataset
        if(entry.path().extension() != ".json"){
            continue;
        }
        if(entry.path().filename() == "index.json"){
            continue;
        }
        std::ifstream file(entry.path());
        if(!file){
            continue;
        }
        try{
            //begin assignement
            json data;
            file >> data;
            std::string meshID = data.at("meshID").get<std::string>();
            std::string imageID = data.at("imageID").get<std::string>();
            double width = data.value("width", 512.0);
            double height = data.value("height", 512.0);
            double imageDiagonal = std::hypot(width, height);
            const auto& piecewisePaths = data.at("piecewisePaths");
            totalImages++;

            //iterate through components
            for(const auto& path : piecewisePaths) {
                    //collect the edgemap information
                    int componentID = path.at("edgeID").get<int>();
                    int pathID = path.at("pathID").get<int>();
                    PathRecord record;
                    record.meshID = meshID;
                    record.imageID = imageID;
                    record.componentID = componentID;
                    record.pathID = pathID;
                    //traverse paths
                    for(const auto& segment: path.at("segments")){
                        std::string type = segment.at("type").get<std::string>();
                        const auto& function = segment.at("data");
                        int functionID = function.at("id").get<int>();
                        //convert function
                        std::vector<double> descriptor = createDescriptor(type, function, imageDiagonal);
                        if(descriptor.empty()){
                            continue;
                        }
                        totalFunctions++;
                        //assign the found data
                        FunctionOccurrence occurrence;
                        occurrence.meshID = meshID;
                        occurrence.imageID = imageID;
                        occurrence.componentID = componentID;
                        occurrence.pathID = pathID;
                        occurrence.functionID = functionID;
                        //begin the sorting process
                        int matchedPattern = -1;

                        //collected functions
                        for(size_t i = 0; i < patterns.size(); i++){
                            if(patterns[i].type != type){
                                continue;
                            }
                            bool isMatch = false;
                            //'linear,' used as line in metadata
                            if(type == "line"){
                                //helper function to find relative distance between comparision functions
                                isMatch = matchLinear(descriptor, patterns[i].descriptor, angleThreshold, lengthTolerance);
                            }
                            else if(type == "bezier"){
                                //temporary helper function
                                isMatch = descriptorDistance(descriptor, patterns[i].descriptor) < 0.05;
                            }
                            if(isMatch){
                                matchedPattern = static_cast<int>(i);
                                break;
                            }
                        }
                        //now matching functions
                        if(matchedPattern == -1){
                            //move on
                            FunctionPattern pattern;
                            pattern.patternID = nextPatternID++;
                            pattern.type = type;
                            pattern.descriptor = descriptor;
                            pattern.occurrences.push_back(occurrence);
                            //add new function to the list
                            patterns.push_back(std::move(pattern));
                            matchedPattern = static_cast<int>(patterns.size()) - 1;

                        }
                        else{
                            //we found another matching function
                            patterns[matchedPattern].occurrences.push_back(occurrence);
                        }
                        record.functionIDs.push_back(functionID);
                        record.patternIDs.push_back(patterns[matchedPattern].patternID);
                    }
                    pathRecords.push_back(record);
                }
        }catch(const std::exception& e){
                std::cerr << "error" << entry.path() << ":" << e.what() << '\n';
            }
    }

}


void match(const fs::path& inputJSON, const std::vector<functionPattern>& patterns){
    
}


int main(){
    fs::path datasetRoot =R"(F:\CARLA set\sets\test_edges)";
    vectorizeToken(datasetRoot);

    return 0;
}
