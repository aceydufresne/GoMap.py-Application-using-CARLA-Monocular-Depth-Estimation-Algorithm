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
#include <sqlite3.h>
#include "edgeEncoder.h"
#include <stdexcept>
#include <map>
#include <utility>


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


struct TokenMerge {
    int firstToken;
    int secondToken;
    int frequency;
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



struct matchCandidate {
    std::string meshID;
    std::string imageID;

    int matchedFunctions = 0;
    int matchedNeighbors = 0;

    double score = 0.0;
};


struct PathRecord {
    std::string meshID;
    std::string imageID;

    int componentID;
    int pathID;

    std::vector<int> functionIDs;
    std::vector<int> patternIDs;
    std::vector<FunctionNeighbor> neighbors;
};


struct TokenDataset {
    std::vector<functionPattern> patterns;
    std::vector<PathRecord> paths;
};

//this is passed to the bpy script to create the pointcloud
struct PointMatch {
    int patternID = -1;

    std::string meshID;
    std::string imageID;

    int componentID = -1;
    int pathID = -1;
    int functionID = -1;

    std::string type;
    double distance = 0.0;
};



struct EdgeFunction {
    std::string meshID;
    std::string imageID;

    int componentID;
    int pathID;
    int functionID;
    int sequenceIndex;

    std::string type;
    std::vector<double> descriptor;
    json parameters;
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


//temp export function, no heavy calculations
std::vector<PointMatch> findPoints(
    const json& input,
    const TokenDataset& dataset,
    const std::vector<matchCandidate>& candidates
) {
    std::vector<PointMatch> results;
    std::unordered_set<int> selectedPatterns;

    if (candidates.empty())
        return results;

    const std::string bestMeshID = candidates.front().meshID;

    double diagonal = std::hypot(
        input.at("width").get<double>(),
        input.at("height").get<double>()
    );

    for (const auto& path : input.at("piecewisePaths")) {
        for (const auto& segment : path.at("segments")) {

            std::string type = segment.at("type").get<std::string>();
            const json& function = segment.at("data");

            std::vector<double> descriptor =
                createDescriptor(type, function, diagonal);

            int bestPatternID = -1;
            double bestDistance = std::numeric_limits<double>::infinity();
            const functionPattern* bestPattern = nullptr;

            // Find the closest matching function pattern
            for (const auto& pattern : dataset.patterns) {
                if (pattern.type != type)
                    continue;

                bool matched = false;
                double distance = descriptorDistance(
                    descriptor, pattern.descriptor
                );

                if (type == "line") {
                    matched = matchLinear(
                        descriptor, pattern.descriptor, 10.0, 0.10
                    );
                }
                else if (type == "bezier") {
                    matched = distance <= 0.05;
                }

                if (matched && distance < bestDistance) {
                    bestDistance = distance;
                    bestPatternID = pattern.patternID;
                    bestPattern = &pattern;
                }
            }

            if (bestPattern == nullptr)
                continue;

            // Return one occurrence per distinct pattern
            if (!selectedPatterns.insert(bestPatternID).second)
                continue;

            const FunctionOccurrence* selectedOccurrence = nullptr;

            // Prefer an occurrence from the highest-ranked mesh
            for (const auto& occurrence : bestPattern->occurrences) {
                if (occurrence.meshID == bestMeshID) {
                    selectedOccurrence = &occurrence;
                    break;
                }
            }

            // Fall back to another mesh when necessary
            if (selectedOccurrence == nullptr &&
                !bestPattern->occurrences.empty()) {
                selectedOccurrence = &bestPattern->occurrences.front();
            }

            if (selectedOccurrence == nullptr)
                continue;

            PointMatch result;
            result.patternID = bestPatternID;
            result.meshID = selectedOccurrence->meshID;
            result.imageID = selectedOccurrence->imageID;
            result.componentID = selectedOccurrence->componentID;
            result.pathID = selectedOccurrence->pathID;
            result.functionID = selectedOccurrence->functionID;
            result.type = type;
            result.distance = bestDistance;

            results.push_back(std::move(result));
        }
    }

    return results;
}



std::vector<EdgeFunction> loadDatasetEdges(const fs::path& datasetRoot) {
    std::vector<EdgeFunction> functions;

    for (const auto& entry : fs::recursive_directory_iterator(datasetRoot)) {

        if (!entry.is_regular_file() ||
            entry.path().extension() != ".json" ||
            entry.path().filename() == "index.json") {
            continue;
        }

        std::ifstream file(entry.path());

        if (!file) {
            continue;
        }

        try {
            json data;
            file >> data;

            std::string meshID =data.at("meshID").get<std::string>();
            std::string imageID =data.at("imageID").get<std::string>();

            double width = data.value("width", 512.0);
            double height = data.value("height", 512.0);
            double diagonal = std::hypot(width, height);

            for (const auto& path : data.at("piecewisePaths")) {
                int componentID = path.at("edgeID").get<int>();
                int pathID = path.at("pathID").get<int>();
                int sequenceIndex = 0;

                for (const auto& segment : path.at("segments")) {

                    std::string type =segment.at("type").get<std::string>();
                    const json& function = segment.at("data");
                    std::vector<double> descriptor =createDescriptor(type, function, diagonal);

                    if (descriptor.empty()) {
                        continue;
                    }

                    EdgeFunction record;

                    record.meshID = meshID;
                    record.imageID = imageID;

                    record.componentID = componentID;
                    record.pathID = pathID;

                    record.functionID =function.at("id").get<int>();

                    record.sequenceIndex = sequenceIndex++;
                    record.type = type;
                    record.descriptor = std::move(descriptor);
                    record.parameters = function;

                    functions.push_back(std::move(record));
                }
            }

        } catch (const std::exception& e) {
            std::cerr << "Error loading "  << entry.path() << ": "  << e.what() << '\n';
        }
    }

    return functions;
}



int getOrCreateToken(
    sqlite3* db,
    const std::string& type,
    const std::vector<double>& descriptor,
    double angleTolerance = 10.0 * 3.14159265358979323846 / 180.0,
    double lengthTolerance = 0.10,
    double bezierTolerance = 0.05
) {
    if (type != "line" && type != "bezier") {
        throw std::invalid_argument("Unsupported token type: " + type);
    }

    const char* searchSQL = R"SQL(
        SELECT tokenID, descriptor
        FROM tokens
        WHERE type = ?;
    )SQL";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, searchSQL, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(db));
    }

    sqlite3_bind_text(
        stmt, 1, type.c_str(), -1, SQLITE_TRANSIENT
    );

    int existingID = -1;
    int result;

    try {
        while ((result = sqlite3_step(stmt)) == SQLITE_ROW) {
            int tokenID = sqlite3_column_int(stmt, 0);

            const char* descriptorText =
                reinterpret_cast<const char*>(
                    sqlite3_column_text(stmt, 1)
                );

            if (!descriptorText) continue;

            std::vector<double> stored =
                json::parse(descriptorText).get<std::vector<double>>();

            bool matches = false;

            if (type == "line" &&
                descriptor.size() == 2 &&
                stored.size() == 2) {

                // Angles are normalized by PI and direction-independent.
                double angleDiff = std::abs(descriptor[0] - stored[0]);
                angleDiff = std::min(angleDiff, 1.0 - angleDiff);

                double lengthDiff =
                    std::abs(descriptor[1] - stored[1]);

                double scale = std::max(
                    {std::abs(descriptor[1]),
                     std::abs(stored[1]), 1e-9}
                );

                matches =
                    angleDiff <= angleTolerance / 3.14159265358979323846 &&
                    lengthDiff <= lengthTolerance * scale;

            } else if (type == "bezier" &&
                       descriptor.size() == stored.size() &&
                       !descriptor.empty()) {

                double squaredDistance = 0.0;

                for (size_t i = 0; i < descriptor.size(); ++i) {
                    double diff = descriptor[i] - stored[i];
                    squaredDistance += diff * diff;
                }

                matches = std::sqrt(squaredDistance) <= bezierTolerance;
            }

            if (matches) {
                existingID = tokenID;
                break;
            }
        }

        if (existingID == -1 && result != SQLITE_DONE) {
            throw std::runtime_error(sqlite3_errmsg(db));
        }

    } catch (...) {
        sqlite3_finalize(stmt);
        throw;
    }

    sqlite3_finalize(stmt);

    if (existingID != -1) {
        return existingID;
    }

    // No matching token: create a new persistent ID.
    // SQLite chooses an unused positive ID above the current maximum.
    const char* insertSQL = R"SQL(
        INSERT INTO tokens (type, descriptor)
        VALUES (?, ?);
    )SQL";

    if (sqlite3_prepare_v2(db, insertSQL, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(db));
    }

    std::string descriptorJSON = json(descriptor).dump();

    sqlite3_bind_text(
        stmt, 1, type.c_str(), -1, SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        stmt, 2, descriptorJSON.c_str(), -1, SQLITE_TRANSIENT
    );

    result = sqlite3_step(stmt);

    if (result != SQLITE_DONE) {
        std::string error = sqlite3_errmsg(db);
        sqlite3_finalize(stmt);
        throw std::runtime_error(error);
    }

    sqlite3_finalize(stmt);

    return static_cast<int>(sqlite3_last_insert_rowid(db));
}





TokenDataset tokenizeFunctions(
    sqlite3* db,
    const std::vector<EdgeFunction>& functions
) {
    TokenDataset dataset;

    // SQLite tokenID -> index in dataset.patterns
    std::unordered_map<int, size_t> tokenIndex;

    for (const EdgeFunction& function : functions) {

        // Retrieve existing token or create a new one in SQLite
        int tokenID = getOrCreateToken(
            db,
            function.type,
            function.descriptor
        );

        size_t patternIndex;

        auto it = tokenIndex.find(tokenID);

        if (it == tokenIndex.end()) {

            // First occurrence of this token in the current dataset
            functionPattern pattern;

            pattern.patternID = tokenID;
            pattern.type = function.type;
            pattern.descriptor = function.descriptor;

            patternIndex = dataset.patterns.size();

            dataset.patterns.push_back(std::move(pattern));

            tokenIndex[tokenID] = patternIndex;

        } else {
            // Token already encountered in this dataset
            patternIndex = it->second;
        }

        // Record this function's occurrence
        FunctionOccurrence occurrence;

        occurrence.meshID = function.meshID;
        occurrence.imageID = function.imageID;
        occurrence.componentID = function.componentID;
        occurrence.pathID = function.pathID;
        occurrence.functionID = function.functionID;

        dataset.patterns[patternIndex].occurrences.push_back(
            std::move(occurrence)
        );
    }

    return dataset;
}



void buildNeighbors(std::vector<PathRecord>& paths) {

    for (PathRecord& path : paths) {
        path.neighbors.clear();
        const auto& tokens = path.patternIDs;

        for (size_t i = 0; i < tokens.size(); i++) {
            int currentToken = tokens[i];
            // Previous neighbor
            if (i > 0) {
                path.neighbors.push_back({
                    currentToken,
                    tokens[i - 1],
                    -1
                });
            }
            // Next neighbor
            if (i + 1 < tokens.size()) {
                path.neighbors.push_back({
                    currentToken,
                    tokens[i + 1],
                    +1
                });
            }
        }
    }
}


void initializeDatabase(sqlite3* db) {
    const char* sql = R"SQL(
        PRAGMA foreign_keys = ON;

        CREATE TABLE IF NOT EXISTS merged_path_functions (
            pathKey INTEGER NOT NULL,
            sequenceIndex INTEGER NOT NULL,
            tokenID INTEGER NOT NULL,
            startIndex INTEGER NOT NULL,
            endIndex INTEGER NOT NULL,

            PRIMARY KEY(pathKey, sequenceIndex),
            FOREIGN KEY(pathKey) REFERENCES paths(pathKey),
            FOREIGN KEY(tokenID) REFERENCES tokens(tokenID)
        );


        CREATE TABLE IF NOT EXISTS tokens (
            tokenID INTEGER PRIMARY KEY,
            type TEXT NOT NULL,
            descriptor TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS paths (
            pathKey INTEGER PRIMARY KEY,
            meshID TEXT NOT NULL,
            imageID TEXT NOT NULL,
            componentID INTEGER NOT NULL,
            pathID INTEGER NOT NULL,
            UNIQUE(meshID, imageID, componentID, pathID)
        );

        CREATE TABLE IF NOT EXISTS occurrences (
            occurrenceID INTEGER PRIMARY KEY,
            tokenID INTEGER NOT NULL,
            pathKey INTEGER NOT NULL,
            functionID INTEGER NOT NULL,
            sequenceIndex INTEGER NOT NULL,
            parameters TEXT NOT NULL,

            FOREIGN KEY(tokenID) REFERENCES tokens(tokenID),
            FOREIGN KEY(pathKey) REFERENCES paths(pathKey),
            UNIQUE(pathKey, sequenceIndex)
        );

        CREATE TABLE IF NOT EXISTS path_functions (
            pathKey INTEGER NOT NULL,
            sequenceIndex INTEGER NOT NULL,
            tokenID INTEGER NOT NULL,

            PRIMARY KEY(pathKey, sequenceIndex),

            FOREIGN KEY(pathKey) REFERENCES paths(pathKey),
            FOREIGN KEY(tokenID) REFERENCES tokens(tokenID)
        );

        CREATE INDEX IF NOT EXISTS idx_occurrences_token
            ON occurrences(tokenID);

        CREATE INDEX IF NOT EXISTS idx_path_functions_token
            ON path_functions(tokenID);

        CREATE TABLE IF NOT EXISTS token_merges (
            mergeID INTEGER PRIMARY KEY AUTOINCREMENT,
            firstToken INTEGER NOT NULL,
            secondToken INTEGER NOT NULL,
            compositeToken INTEGER NOT NULL UNIQUE,
            frequency INTEGER NOT NULL,
            createdAt TEXT DEFAULT CURRENT_TIMESTAMP,

            UNIQUE(firstToken, secondToken),

            FOREIGN KEY(firstToken) REFERENCES tokens(tokenID),
            FOREIGN KEY(secondToken) REFERENCES tokens(tokenID)
        );
    )SQL";

    char* errorMessage = nullptr;

    int result = sqlite3_exec(
        db,
        sql,
        nullptr,
        nullptr,
        &errorMessage
    );

    if (result != SQLITE_OK) {
        std::cerr << "SQLite error: "
                  << (errorMessage ? errorMessage : "Unknown error")
                  << '\n';

        sqlite3_free(errorMessage);
        throw std::runtime_error("Database initialization failed");
    }
}



sqlite3* openDatabase(const std::string& databasePath) {

    sqlite3* db = nullptr;

    if (sqlite3_open(databasePath.c_str(), &db) != SQLITE_OK) {
        std::string error =
            db ? sqlite3_errmsg(db) : "Cannot allocate database";

        if (db) {
            sqlite3_close(db);
        }

        throw std::runtime_error(error);
    }
    try {
        initializeDatabase(db);
    } catch (...) {
        sqlite3_close(db);
        throw;
    }

    return db;
}


void saveTokenDataset(
    sqlite3* db,
    const TokenDataset& dataset,
    const std::vector<EdgeFunction>& functions
) {
    // Helper for executing SQL statements
    auto execute = [&](const char* sql) {
        char* error = nullptr;

        if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
            std::string message = error ? error : "Unknown SQLite error";
            sqlite3_free(error);
            throw std::runtime_error(message);
        }
    };

    // Helper for preparing statements
    auto prepare = [&](const char* sql) -> sqlite3_stmt* {
        sqlite3_stmt* stmt = nullptr;

        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            throw std::runtime_error(sqlite3_errmsg(db));
        }

        return stmt;
    };

    // Helper for executing a prepared statement
    auto step = [&](sqlite3_stmt* stmt) {
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            throw std::runtime_error(sqlite3_errmsg(db));
        }

        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    };

    sqlite3_stmt* tokenStmt = nullptr;
    sqlite3_stmt* pathStmt = nullptr;
    sqlite3_stmt* pathKeyStmt = nullptr;
    sqlite3_stmt* occurrenceStmt = nullptr;
    sqlite3_stmt* pathFunctionStmt = nullptr;

    try {
        execute("BEGIN IMMEDIATE TRANSACTION;");

        tokenStmt = prepare(R"(
            INSERT INTO tokens (tokenID, type, descriptor)
            VALUES (?, ?, ?)
            ON CONFLICT(tokenID) DO NOTHING;
        )");

        pathStmt = prepare(R"(
            INSERT INTO paths (meshID, imageID, componentID, pathID)
            VALUES (?, ?, ?, ?)
            ON CONFLICT(meshID, imageID, componentID, pathID)
            DO NOTHING;
        )");

        pathKeyStmt = prepare(R"(
            SELECT pathKey FROM paths
            WHERE meshID = ?
              AND imageID = ?
              AND componentID = ?
              AND pathID = ?;
        )");

        occurrenceStmt = prepare(R"(
            INSERT INTO occurrences (
                tokenID, pathKey, functionID,
                sequenceIndex, parameters
            )
            VALUES (?, ?, ?, ?, ?)
            ON CONFLICT(pathKey, sequenceIndex) DO UPDATE SET
                tokenID = excluded.tokenID,
                functionID = excluded.functionID,
                parameters = excluded.parameters;
        )");

        pathFunctionStmt = prepare(R"(
            INSERT INTO path_functions (
                pathKey, sequenceIndex, tokenID
            )
            VALUES (?, ?, ?)
            ON CONFLICT(pathKey, sequenceIndex) DO UPDATE SET
                tokenID = excluded.tokenID;
        )");

        // 1. Save token vocabulary
        for (const functionPattern& token : dataset.patterns) {

            std::string descriptor = json(token.descriptor).dump();

            sqlite3_bind_int(tokenStmt, 1, token.patternID);
            sqlite3_bind_text(tokenStmt, 2, token.type.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(tokenStmt, 3, descriptor.c_str(), -1, SQLITE_TRANSIENT);

            step(tokenStmt);
        }

        // Build a lookup from source function to token ID
        // This avoids searching every token for every function.
        std::unordered_map<std::string, int> functionTokens;

        auto makeKey = [](
            const std::string& mesh,
            const std::string& image,
            int component,
            int path,
            int function
        ) {
            return json::array({
                mesh, image, component, path, function
            }).dump();
        };

        for (const functionPattern& token : dataset.patterns) {
            for (const FunctionOccurrence& occurrence : token.occurrences) {
                functionTokens[makeKey(
                    occurrence.meshID,
                    occurrence.imageID,
                    occurrence.componentID,
                    occurrence.pathID,
                    occurrence.functionID
                )] = token.patternID;
            }
        }

        // 2. Save reconstructed paths
        for (const PathRecord& path : dataset.paths) {

            sqlite3_bind_text(pathStmt, 1, path.meshID.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(pathStmt, 2, path.imageID.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(pathStmt, 3, path.componentID);
            sqlite3_bind_int(pathStmt, 4, path.pathID);

            step(pathStmt);
        }

        // 3. Save every original function occurrence
        for (const EdgeFunction& function : functions) {

            std::string key = makeKey(
                function.meshID,
                function.imageID,
                function.componentID,
                function.pathID,
                function.functionID
            );

            auto tokenIt = functionTokens.find(key);

            if (tokenIt == functionTokens.end()) {
                throw std::runtime_error(
                    "Missing token assignment for function: " + key
                );
            }

            int tokenID = tokenIt->second;

            // Ensure the function's path exists, including
            // paths that may not yet be in dataset.paths.
            sqlite3_bind_text(pathStmt, 1, function.meshID.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(pathStmt, 2, function.imageID.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(pathStmt, 3, function.componentID);
            sqlite3_bind_int(pathStmt, 4, function.pathID);

            step(pathStmt);

            // Retrieve the database-generated pathKey
            sqlite3_bind_text(pathKeyStmt, 1, function.meshID.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(pathKeyStmt, 2, function.imageID.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(pathKeyStmt, 3, function.componentID);
            sqlite3_bind_int(pathKeyStmt, 4, function.pathID);

            if (sqlite3_step(pathKeyStmt) != SQLITE_ROW) {
                throw std::runtime_error(
                    "Failed to retrieve pathKey: " + key
                );
            }

            int pathKey = sqlite3_column_int(pathKeyStmt, 0);

            sqlite3_reset(pathKeyStmt);
            sqlite3_clear_bindings(pathKeyStmt);

            std::string parameters = function.parameters.dump();

            sqlite3_bind_int(occurrenceStmt, 1, tokenID);
            sqlite3_bind_int(occurrenceStmt, 2, pathKey);
            sqlite3_bind_int(occurrenceStmt, 3, function.functionID);
            sqlite3_bind_int(occurrenceStmt, 4, function.sequenceIndex);
            sqlite3_bind_text(
                occurrenceStmt, 5,
                parameters.c_str(), -1, SQLITE_TRANSIENT
            );

            step(occurrenceStmt);

            // The sequence index preserves the original path order.
            sqlite3_bind_int(pathFunctionStmt, 1, pathKey);
            sqlite3_bind_int(pathFunctionStmt, 2, function.sequenceIndex);
            sqlite3_bind_int(pathFunctionStmt, 3, tokenID);

            step(pathFunctionStmt);
        }

        execute("COMMIT;");

        std::cout << "Saved "
                  << dataset.patterns.size()
                  << " tokens and "
                  << functions.size()
                  << " function occurrences to SQLite.\n";

    } catch (...) {
        // Finalize statements before rolling back.
        sqlite3_finalize(tokenStmt);
        sqlite3_finalize(pathStmt);
        sqlite3_finalize(pathKeyStmt);
        sqlite3_finalize(occurrenceStmt);
        sqlite3_finalize(pathFunctionStmt);

        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw;
    }

    sqlite3_finalize(tokenStmt);
    sqlite3_finalize(pathStmt);
    sqlite3_finalize(pathKeyStmt);
    sqlite3_finalize(occurrenceStmt);
    sqlite3_finalize(pathFunctionStmt);
}



std::vector<TokenMerge> findMergeCandidates(
    sqlite3* db,
    int minimumFrequency = 2
) {
    std::vector<TokenMerge> candidates;

    const char* sql = R"SQL(
        SELECT
            a.tokenID,
            b.tokenID,
            COUNT(*) AS frequency
        FROM path_functions a
        JOIN path_functions b
            ON a.pathKey = b.pathKey
            AND b.sequenceIndex = a.sequenceIndex + 1
        GROUP BY a.tokenID, b.tokenID
        HAVING COUNT(*) >= ?
        ORDER BY frequency DESC, a.tokenID, b.tokenID;
    )SQL";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(db));
    }

    sqlite3_bind_int(stmt, 1, minimumFrequency);

    int result;

    while ((result = sqlite3_step(stmt)) == SQLITE_ROW) {
        candidates.push_back({
            sqlite3_column_int(stmt, 0),
            sqlite3_column_int(stmt, 1),
            sqlite3_column_int(stmt, 2)
        });
    }

    if (result != SQLITE_DONE) {
        std::string error = sqlite3_errmsg(db);
        sqlite3_finalize(stmt);
        throw std::runtime_error(error);
    }

    sqlite3_finalize(stmt);
    return candidates;
}


void mergeTokens(sqlite3* db, int minimumFrequency = 2) {
    if (minimumFrequency < 2) {
        throw std::invalid_argument("Minimum frequency must be at least 2");
    }

    std::vector<TokenMerge> candidates =
        findMergeCandidates(db, minimumFrequency);

    std::cout << "Merge candidates: "
              << candidates.size() << std::endl;

    if (candidates.empty()) {
        std::cout << "No qualifying token pairs found.\n";
        return;
    }

    auto execute = [&](const char* sql) {
        char* error = nullptr;

        if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
            std::string message = error ? error : "SQLite error";
            sqlite3_free(error);
            throw std::runtime_error(message);
        }
    };

    execute("BEGIN IMMEDIATE TRANSACTION;");

    sqlite3_stmt* insertToken = nullptr;
    sqlite3_stmt* insertMerge = nullptr;
    sqlite3_stmt* findExisting = nullptr;

    try {
        const char* tokenSQL = R"SQL(
            INSERT INTO tokens (tokenID, type, descriptor)
            VALUES (?, 'composite', ?);
        )SQL";

        const char* mergeSQL = R"SQL(
            INSERT INTO token_merges
                (firstToken, secondToken, compositeToken, frequency)
            VALUES (?, ?, ?, ?);
        )SQL";

        const char* existingSQL = R"SQL(
            SELECT compositeToken
            FROM token_merges
            WHERE firstToken = ? AND secondToken = ?;
        )SQL";

        auto prepare = [&](const char* sql, sqlite3_stmt** stmt) {
            if (sqlite3_prepare_v2(db, sql, -1, stmt, nullptr) != SQLITE_OK) {
                throw std::runtime_error(sqlite3_errmsg(db));
            }
        };

        prepare(tokenSQL, &insertToken);
        prepare(mergeSQL, &insertMerge);
        prepare(existingSQL, &findExisting);

        int nextTokenID = 0;

        sqlite3_stmt* maxStmt = nullptr;

        prepare(
            R"SQL(
                SELECT MAX(id) FROM (
                    SELECT tokenID AS id FROM tokens
                    UNION ALL
                    SELECT compositeToken AS id FROM token_merges
                );
            )SQL",
            &maxStmt
        );

        if (sqlite3_step(maxStmt) == SQLITE_ROW &&
            sqlite3_column_type(maxStmt, 0) != SQLITE_NULL) {
            nextTokenID = sqlite3_column_int(maxStmt, 0) + 1;
        }

        sqlite3_finalize(maxStmt);

        int created = 0;
        int existing = 0;

        for (const TokenMerge& candidate : candidates) {
            sqlite3_reset(findExisting);
            sqlite3_clear_bindings(findExisting);

            sqlite3_bind_int(findExisting, 1, candidate.firstToken);
            sqlite3_bind_int(findExisting, 2, candidate.secondToken);

            int result = sqlite3_step(findExisting);

            if (result == SQLITE_ROW) {
                ++existing;
                continue;
            }

            if (result != SQLITE_DONE) {
                throw std::runtime_error(sqlite3_errmsg(db));
            }

            int compositeID = nextTokenID++;

            // Store the two child IDs as the composite descriptor.
            std::string descriptor = json::array({
                candidate.firstToken,
                candidate.secondToken
            }).dump();

            sqlite3_reset(insertToken);
            sqlite3_clear_bindings(insertToken);

            sqlite3_bind_int(insertToken, 1, compositeID);
            sqlite3_bind_text(
                insertToken, 2,
                descriptor.c_str(), -1, SQLITE_TRANSIENT
            );

            if (sqlite3_step(insertToken) != SQLITE_DONE) {
                throw std::runtime_error(sqlite3_errmsg(db));
            }

            sqlite3_reset(insertMerge);
            sqlite3_clear_bindings(insertMerge);

            sqlite3_bind_int(insertMerge, 1, candidate.firstToken);
            sqlite3_bind_int(insertMerge, 2, candidate.secondToken);
            sqlite3_bind_int(insertMerge, 3, compositeID);
            sqlite3_bind_int(insertMerge, 4, candidate.frequency);

            if (sqlite3_step(insertMerge) != SQLITE_DONE) {
                throw std::runtime_error(sqlite3_errmsg(db));
            }

            std::cout
                << "Created composite token " << compositeID
                << " = [" << candidate.firstToken
                << ", " << candidate.secondToken << "]"
                << " | Frequency: " << candidate.frequency
                << '\n';

            ++created;
        }

        sqlite3_finalize(insertToken);
        sqlite3_finalize(insertMerge);
        sqlite3_finalize(findExisting);

        insertToken = nullptr;
        insertMerge = nullptr;
        findExisting = nullptr;

        execute("COMMIT;");

        std::cout << "New composite tokens: " << created << '\n';
        std::cout << "Existing merges skipped: " << existing << '\n';

    } catch (...) {
        sqlite3_finalize(insertToken);
        sqlite3_finalize(insertMerge);
        sqlite3_finalize(findExisting);

        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw;
    }
}



void encodeCompositeTokens(sqlite3* db) {

    struct MergeRule {
        int compositeID;
        int frequency;
    };

    struct PathToken {
        int tokenID;
        int startIndex;
        int endIndex;
    };

    auto check = [&](int rc) {
        if (rc != SQLITE_OK) {
            throw std::runtime_error(sqlite3_errmsg(db));
        }
    };

    auto execute = [&](const char* sql) {
        char* error = nullptr;
        int rc = sqlite3_exec(db, sql, nullptr, nullptr, &error);

        if (rc != SQLITE_OK) {
            std::string message = error ? error : "SQLite error";
            sqlite3_free(error);
            throw std::runtime_error(message);
        }
    };

    auto prepare = [&](const char* sql) {
        sqlite3_stmt* stmt = nullptr;
        check(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr));
        return stmt;
    };

    // Map original token pairs to composite IDs.
    std::map<std::pair<int, int>, MergeRule> rules;

    sqlite3_stmt* ruleStmt = prepare(
        "SELECT firstToken, secondToken, compositeToken, frequency "
        "FROM token_merges;"
    );

    try {
        int rc;

        while ((rc = sqlite3_step(ruleStmt)) == SQLITE_ROW) {
            int first = sqlite3_column_int(ruleStmt, 0);
            int second = sqlite3_column_int(ruleStmt, 1);

            rules[{first, second}] = {
                sqlite3_column_int(ruleStmt, 2),
                sqlite3_column_int(ruleStmt, 3)
            };
        }

        if (rc != SQLITE_DONE) {
            throw std::runtime_error(sqlite3_errmsg(db));
        }
    } catch (...) {
        sqlite3_finalize(ruleStmt);
        throw;
    }

    sqlite3_finalize(ruleStmt);

    // Load original sequences.
    std::map<int, std::vector<PathToken>> paths;

    sqlite3_stmt* pathStmt = prepare(
        "SELECT pathKey, sequenceIndex, tokenID "
        "FROM path_functions "
        "ORDER BY pathKey, sequenceIndex;"
    );

    try {
        int rc;

        while ((rc = sqlite3_step(pathStmt)) == SQLITE_ROW) {
            int pathKey = sqlite3_column_int(pathStmt, 0);
            int index = sqlite3_column_int(pathStmt, 1);
            int tokenID = sqlite3_column_int(pathStmt, 2);

            paths[pathKey].push_back({tokenID, index, index});
        }

        if (rc != SQLITE_DONE) {
            throw std::runtime_error(sqlite3_errmsg(db));
        }
    } catch (...) {
        sqlite3_finalize(pathStmt);
        throw;
    }

    sqlite3_finalize(pathStmt);

    // Build compressed paths in memory before modifying the DB.
    std::map<int, std::vector<PathToken>> compressedPaths;

    int originalCount = 0;
    int compressedCount = 0;
    int mergesApplied = 0;

    for (const auto& [pathKey, tokens] : paths) {

        auto& output = compressedPaths[pathKey];

        originalCount += static_cast<int>(tokens.size());

        size_t i = 0;

        while (i < tokens.size()) {

            if (i + 1 < tokens.size() &&
                tokens[i + 1].startIndex == tokens[i].endIndex + 1) {

                auto it = rules.find({
                    tokens[i].tokenID,
                    tokens[i + 1].tokenID
                });

                if (it != rules.end()) {

                    output.push_back({
                        it->second.compositeID,
                        tokens[i].startIndex,
                        tokens[i + 1].endIndex
                    });

                    mergesApplied++;
                    i += 2;
                    continue;
                }
            }

            output.push_back(tokens[i]);
            i++;
        }

        compressedCount += static_cast<int>(output.size());
    }

    // Save compressed sequences atomically.
    execute("BEGIN IMMEDIATE TRANSACTION;");

    sqlite3_stmt* insertStmt = nullptr;

    try {
        execute("DELETE FROM merged_path_functions;");

        insertStmt = prepare(
            "INSERT INTO merged_path_functions "
            "(pathKey, sequenceIndex, tokenID, startIndex, endIndex) "
            "VALUES (?, ?, ?, ?, ?);"
        );

        for (const auto& [pathKey, tokens] : compressedPaths) {

            for (size_t i = 0; i < tokens.size(); i++) {

                sqlite3_bind_int(insertStmt, 1, pathKey);
                sqlite3_bind_int(insertStmt, 2, static_cast<int>(i));
                sqlite3_bind_int(insertStmt, 3, tokens[i].tokenID);
                sqlite3_bind_int(insertStmt, 4, tokens[i].startIndex);
                sqlite3_bind_int(insertStmt, 5, tokens[i].endIndex);

                if (sqlite3_step(insertStmt) != SQLITE_DONE) {
                    throw std::runtime_error(sqlite3_errmsg(db));
                }

                sqlite3_reset(insertStmt);
                sqlite3_clear_bindings(insertStmt);
            }
        }

        sqlite3_finalize(insertStmt);
        insertStmt = nullptr;

        execute("COMMIT;");

    } catch (...) {
        if (insertStmt) sqlite3_finalize(insertStmt);
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw;
    }

    std::cout << "Original tokens: " << originalCount << '\n';
    std::cout << "Compressed tokens: " << compressedCount << '\n';
    std::cout << "Composite merges applied: " << mergesApplied << '\n';
}



void exportPointMatches(
    const std::vector<PointMatch>& matches,
    const fs::path& outputPath
) {
    json output;
    output["matches"] = json::array();

    for (const PointMatch& match : matches) {
        output["matches"].push_back({
            {"patternID", match.patternID},
            {"meshID", match.meshID},
            {"imageID", match.imageID},
            {"componentID", match.componentID},
            {"pathID", match.pathID},
            {"functionID", match.functionID},
            {"type", match.type},
            {"distance", match.distance}
        });
    }

    std::ofstream file(outputPath);

    if (!file) {
        throw std::runtime_error(
            "Failed to open output file: " + outputPath.string()
        );
    }

    file << output.dump(2);
}




int main(int argc, char* argv[]) {

if (argc > 1 && std::string(argv[1]) == "--build-database") {

    fs::path datasetRoot = R"(F:\CARLA set\sets\test_edges)";
    fs::path databasePath = R"(F:\CARLA set\model\tokens.db)";

    try {
        std::cout << "Loading dataset edges...\n";

        auto functions = loadDatasetEdges(datasetRoot);

        std::cout << "Functions loaded: "
                  << functions.size() << '\n';

        // Open database BEFORE tokenization
        sqlite3* db = openDatabase(databasePath.string());

        try {
            std::cout << "Tokenizing functions...\n";

            // SQLite connection is now passed to tokenizeFunctions
            auto dataset = tokenizeFunctions(db, functions);

            std::cout << "Unique tokens: "
                      << dataset.patterns.size() << '\n';

            saveTokenDataset(db, dataset, functions);

            // Discover frequently occurring pairs
            mergeTokens(db, 2);

            // Encode paths using the composite tokens
            encodeCompositeTokens(db);

        } catch (...) {
            sqlite3_close(db);
            throw;
        }

        sqlite3_close(db);

        std::cout << "Database successfully created!\n";
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}



    std::string imagePath;
    std::string outputPath;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--input" && i + 1 < argc) {
            imagePath = argv[++i];
        }
        else if (arg == "--output" && i + 1 < argc) {
            outputPath = argv[++i];
        }
    }

    if (imagePath.empty() || outputPath.empty()) {
        std::cerr
            << "Usage: classifier.exe --input IMAGE --output JSON\n";
        return 1;
    }

    json input = encodeImage(imagePath);

    std::string datasetRoot =
        R"(F:\CARLA set\sets\test_edges)";

    TokenDataset dataset = vectorizeToken(datasetRoot);

    std::vector<matchCandidate> candidates = match(
        input,
        dataset.patterns,
        dataset.paths
    );

    std::vector<PointMatch> selected = findPoints(
        input,
        dataset,
        candidates
    );

    exportPointMatches(selected, outputPath);

    std::cout << "Matched functions: "
              << selected.size() << std::endl;

    std::cout << "Exported matches to: "
              << outputPath << std::endl;

    return 0;
}
