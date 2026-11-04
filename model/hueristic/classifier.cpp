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

struct functionPattern {
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

struct matchCandidate {
    std::string meshID;
    std::string imageID;

    int matchedFunctions = 0;
    int matchedNeighbors = 0;

    double score = 0.0;
};

struct TokenDataset {
    std::vector<functionPattern> patterns;
    std::vector<PathRecord> paths;
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






TokenDataset vectorizeToken(const fs::path& datasetRoot){
    //example output
    fs::path outputPath = datasetRoot / "function_patterns.txt";
    std::vector<functionPattern> patterns;
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
                    //std::cout << "First pass starting\n";
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
                        //std::cout << "second loop pass starting\n";
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
                            //std::cout << "259\n";
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
                            functionPattern pattern;
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
    return {std::move(patterns), std::move(pathRecords)};
}



int verifyNeighbors(const PathRecord& inputPath, const PathRecord& candidatePath, int inputIndex, int candidateIndex) {
    int matches = 0;
    const auto& input = inputPath.patternIDs;
    const auto& candidate = candidatePath.patternIDs;
    //check the preceding neighbor
    if (inputIndex > 0 && candidateIndex > 0) {
        if (input[inputIndex - 1] == candidate[candidateIndex - 1]) {
            matches++;
        }
    }
    //Check the following neighbor
    if (inputIndex + 1 < static_cast<int>(input.size()) &&
        candidateIndex + 1 < static_cast<int>(candidate.size())) {

        if (input[inputIndex + 1] == candidate[candidateIndex + 1]) {
            matches++;
        }
    }
    return matches;
}




std::vector<matchCandidate> match(const json& input, const std::vector<functionPattern>& patterns, const std::vector<PathRecord>& datasetPaths) {

    double diagonal = std::hypot(input.at("width").get<double>(), input.at("height").get<double>());
    std::vector<matchCandidate> candidates;

    //traverse through components
    for (const auto& path : input.at("piecewisePaths")) {
        //create a record for the current input path
        PathRecord inputPath;
        inputPath.componentID = path.at("edgeID").get<int>();
        inputPath.pathID = path.at("pathID").get<int>();
        const auto& segments = path.at("segments");

        for (const auto& segment : segments) {
            std::string type = segment.at("type").get<std::string>();
            const json& function = segment.at("data");

            std::vector<double> descriptor = createDescriptor(type, function, diagonal);
            int matchedPatternID = -1;

            for (const functionPattern& pattern : patterns) {
                if (pattern.type != type) {
                    continue;
                }
                bool matched = false;
                if (type == "line") {
                    matched = matchLinear(descriptor, pattern.descriptor, 10.0, 0.10);
                }
                else if (type == "bezier") {
                    matched =
                        descriptorDistance(descriptor, pattern.descriptor) <= 0.05;
                }
                if (matched) {
                    matchedPatternID = pattern.patternID;
                    break;
                }
            }
            inputPath.functionIDs.push_back(function.at("id").get<int>());
            inputPath.patternIDs.push_back(matchedPatternID);
        }

        //iterate through segments while tracking their positions
        for (size_t inputIndex = 0; inputIndex < segments.size(); ++inputIndex) {
            const auto& segment = segments[inputIndex];
            std::string type = segment.at("type");
            const json& function = segment.at("data");

            std::vector<double> descriptor = createDescriptor(type, function, diagonal);
             //begin comparisions with the current function dataset
            for(const functionPattern& pattern: patterns){
                if(pattern.type != type){
                    continue;
                }
                bool matched = false;
                //linear
                if(type == "line"){
                    matched = matchLinear(descriptor, pattern.descriptor, 10.0, 0.10);
                }
                else if(type == "bezier"){
                    double distance = descriptorDistance(descriptor, pattern.descriptor);
                    matched = (distance <= 0.05);
                }
                //dont have this line in the set yet
                if(!matched){
                    continue;
                }
                for(const auto& occurrence: pattern.occurrences){
                    //check neighbors
                    int neighborMatches = 0;
                    //existing cnadidates
                    
                    auto pathIt = std::find_if(datasetPaths.begin(), datasetPaths.end(), [&](const PathRecord& record) {
                        return record.meshID == occurrence.meshID && record.imageID == occurrence.imageID && record.componentID == occurrence.componentID && record.pathID == occurrence.pathID;
                    });

                    int candidateIndex = -1;

                    if (pathIt != datasetPaths.end()) {
                        const PathRecord& candidatePath = *pathIt;
                        auto functionIt = std::find(candidatePath.functionIDs.begin(), candidatePath.functionIDs.end(), occurrence.functionID);

                        if (functionIt != candidatePath.functionIDs.end()) {
                            candidateIndex = static_cast<int>(std::distance(candidatePath.functionIDs.begin(), functionIt));
                        }
                    }
                    if (pathIt != datasetPaths.end() && candidateIndex >= 0) {
                        neighborMatches = verifyNeighbors(
                            inputPath,
                            *pathIt,
                            static_cast<int>(inputIndex),
                            candidateIndex
                        );
                    }
                    auto it = std::find_if(candidates.begin(),candidates.end(),[&](const matchCandidate& candidate) {
                        //if the matching function also has matching neighbors with the input
                        return candidate.meshID == occurrence.meshID && candidate.imageID == occurrence.imageID;
                        });
                    if(it == candidates.end()){
                        matchCandidate candidate;
                        candidate.meshID = occurrence.meshID;
                        candidate.imageID = occurrence.imageID;
                        candidates.push_back(candidate);
                        it = std::prev(candidates.end());
                    }
                    it->matchedFunctions++;
                    //keep track of the matching neighbors as well
                    it->matchedNeighbors+=neighborMatches;
                }
            }
        }
    
           
        }
    
    //soft ranking based on how many matching neighbors a function has
    for(matchCandidate& candidate: candidates){
        candidate.score = candidate.matchedFunctions + (2.0 * candidate.matchedNeighbors);
    }
    //find highest scoring edgemap
    std::sort(
        candidates.begin(), candidates.end(), [](const matchCandidate& a, const matchCandidate& b) {
            return a.score > b.score;
        }
    );


    return candidates;
}



int main() {
    fs::path datasetRoot =
        R"(F:\CARLA set\sets\test_edges)";

    // Load the existing 20-image dataset
    TokenDataset dataset = vectorizeToken(datasetRoot);

    // New image not included in the dataset
    fs::path imagePath =
        R"(F:\CARLA set\renders\github\b684b5a1e0c38b6c1186c934f5d59fe4ed2afcf92c7d8329f0b837e880e30272\011.png)";

    // Encode the new image into edge metadata
    std::cout << "Encoding image..." << std::endl;
    json inputMetadata = encodeImage(imagePath.string());

    // Match against existing dataset
    std::cout << "Matching functions..." << std::endl;
    std::vector<matchCandidate> candidates = match(
    inputMetadata,
    dataset.patterns,
    dataset.paths
    );

    // Display ranked candidates
    std::cout << "\nTotal candidates: " << candidates.size() << '\n';

    for (size_t i = 0; i < candidates.size(); ++i) {
        const matchCandidate& candidate = candidates[i];

        std::cout
            << "Rank: " << i + 1
            << " | Mesh: " << candidate.meshID
            << " | Image: " << candidate.imageID
            << " | Functions: " << candidate.matchedFunctions
            << " | Neighbors: " << candidate.matchedNeighbors
            << " | Score: " << candidate.score
            << '\n';
    }

    return 0;
}
