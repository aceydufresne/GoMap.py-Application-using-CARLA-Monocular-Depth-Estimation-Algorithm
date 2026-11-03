#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cmath>


//struct for the bucket system look-up table
struct FeatureIndex {
    std::unordered_map<int, std::vector<int>> components;
    std::unordered_map<int, std::vector<int>> endpoints;
    std::unordered_map<int, std::vector<int>> junctions;
    std::unordered_map<int, std::vector<int>> loops;
    std::unordered_map<int, std::vector<int>> contourLength;
    std::unordered_map<int, std::vector<int>> enclosedArea;
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

    std::vector<double> descriptor;
    std::vector<FunctionOccurrence> occurrences;
};


void vectorizeToken(path){
    //temporary example output
    fs::path outputPath = datasetRoot / "function_patterns.txt";
    std::vector<FunctionPattern> patterns;
    int nextPatternID = 0;
    int totalImages = 0;
    int totalFunctions;
    //traverse through the example dataset
    for(const auto& entry: fs::recursive_directory_iterator(datasetRoot)){
        if(!entry.is_regular_file){
            continue;
        }
        //only searching the metadata
        if(entry.path().extension() != ".json"){
            continue;
        }
        if(entry.path().filename() =="index.json"){
            continue;
        }
        std::ifstream file(entry.path());
        //error handling
        if(!file){
            std::cerr<<"error opening"<<entry.path()<<std::endl;
            continue;
        }
        try{
            json data;
            file >> data;
            std::string meshID = data.at("meshID").get<std::string>();
            std::string imageID = data.at("imageID").get<std::string>();
            const auto& piecewisePaths = data.at("piecewisePaths");
            ++totalImages;
            for(const auto& path:piecewisePaths){
                int componentID = path.at("edgeID").get<int>();
                int pathID = path.at("pathID").get<int>();
                for(const auto& segment: path.at("segments")){
                    std::string type = segment.at("type").get<std::string>();
                    const auto& function = segment.at("data");

                    FunctionOccurrence occurrence;
                    occurrence.meshID = meshID;
                    occurrence.imageID = imageID;
                    occurrence.componentID = componentID;
                    occurence.pathID = pathID;
                    occurence.functionID = function.at("id").get<int>();

                    FunctionPattern pattern;
                    pattern.patternID = nextPatternID++;
                    pattern.type = type;
                }
            }
        }
    }
}


int main(){
    path = R"(F:\CARLA set\sets\test_edges)";
}
