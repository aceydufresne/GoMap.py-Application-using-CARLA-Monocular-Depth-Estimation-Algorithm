#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <utility>
#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <opencv2/core.hpp>
#include "edgeEncoder.h"

nlohmann::json encodeImage(const std::string& imagePath);

constexpr double angleBucketWidth = 10.0;
constexpr int angleBucketCount = 18;
constexpr double lengthBucketWidth = 0.01;
constexpr double lengthTolerance = 0.10;
constexpr double neighborTolerancePixels = 5.0;

struct PathToken {
    int tokenID;
    int functionID;
};

struct LineDescriptor {
    double angleDegrees;
    double normalizedLength;
};

struct LineBucketRange {
    std::array<int, 3> angles;
    int minLength;
    int maxLength;
};

LineBucketRange lineBucketRange(const LineDescriptor& input) {
    const int center = static_cast<int>(input.angleDegrees / angleBucketWidth);
    const double shortest = input.normalizedLength * (1.0 - lengthTolerance);
    const double longest = input.normalizedLength / (1.0 - lengthTolerance);
    return {
        {(center + angleBucketCount - 1) % angleBucketCount,
         center, (center + 1) % angleBucketCount},
        static_cast<int>(shortest / lengthBucketWidth),
        static_cast<int>(longest / lengthBucketWidth)
    };
}

struct LineDifference {
    double angle;
    double length;
};


struct LineSegment {
    cv::Point2d start;
    cv::Point2d end;
    std::optional<cv::Point2d> control;

    cv::Point2d pointAt(double t) const {
        if (control)
            return (1.0 - t) * (1.0 - t) * start
                 + 2.0 * t * (1.0 - t) * *control + t * t * end;
        return (1.0 - t) * start + t * end;
    }
};

struct LineTransform {
    double rotationRadians;
    double scale;
    cv::Point2d translation;

    cv::Point2d apply(const cv::Point2d& p) const {
        const double c = std::cos(rotationRadians);
        const double s = std::sin(rotationRadians);
        return scale * cv::Point2d(c * p.x - s * p.y, s * p.x + c * p.y)
             + translation;
    }
};

struct LineComparison {
    LineTransform transform;
    double substitutionCost;  // 0 = close agreement, 1 = full substitution
};

std::optional<LineDifference> compareLines(LineDescriptor a, LineDescriptor b) {
    const double delta = std::abs(a.angleDegrees - b.angleDegrees);
    const double angle = std::min(delta, 180.0 - delta);
    const double length = std::abs(a.normalizedLength - b.normalizedLength)
        / std::max(a.normalizedLength, b.normalizedLength);
    if (angle <= angleBucketWidth && length <= lengthTolerance)
        return LineDifference{angle, length};
    return std::nullopt;
}

struct FunctionNeighbor {
    int functionID;
    int neighborID;
    double distancePixels;
};


struct LineMatch {
    std::string imagePath;
    int functionID;
    double angleDifference;
    double relativeLengthDifference;
};



void execute(sqlite3* db, const char* sql) {
    if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(db));
}

class Statement {
public:
    Statement(sqlite3* db, const char* sql) : db_(db) {
        sqlite3_stmt* statement = nullptr;
        check(sqlite3_prepare_v2(db, sql, -1, &statement, nullptr), SQLITE_OK);
        statement_.reset(statement);
    }

    sqlite3_stmt* get() const { return statement_.get(); }

    void bind(int index, const std::string& value) {
        check(sqlite3_bind_text(get(), index, value.c_str(), -1, SQLITE_TRANSIENT), SQLITE_OK);
    }

    void bind(int index, int value) {
        check(sqlite3_bind_int(get(), index, value), SQLITE_OK);
    }

    void bind(int index, double value) {
        check(sqlite3_bind_double(get(), index, value), SQLITE_OK);
    }
   void write() {
        check(sqlite3_step(get()), SQLITE_DONE);
        check(sqlite3_reset(get()), SQLITE_OK);
        check(sqlite3_clear_bindings(get()), SQLITE_OK);
    }

    bool next() {
        const int result = sqlite3_step(get());
        if (result == SQLITE_DONE)
            return false;
        check(result, SQLITE_ROW);
        return true;
    }
private:
    void check(int result, int expected) const {
        if (result != expected)
            throw std::runtime_error(sqlite3_errmsg(db_));
    }

    sqlite3* db_;
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>
        statement_{nullptr, sqlite3_finalize};
};

std::vector<LineMatch> findSimilarLines(sqlite3* db, const LineDescriptor& input) {
    const auto range = lineBucketRange(input);

    Statement query(db, R"SQL(
        SELECT image_path, function_id, angle_degrees, normalized_length
        FROM line_buckets
        WHERE angle_bucket IN (?, ?, ?)
          AND length_bucket BETWEEN ? AND ?;
    )SQL");

    query.bind(1, range.angles[0]);
    query.bind(2, range.angles[1]);
    query.bind(3, range.angles[2]);
    query.bind(4, range.minLength);
    query.bind(5, range.maxLength);

    std::vector<LineMatch> matches;
    while (query.next()) {
        sqlite3_stmt* row = query.get();
        const auto difference = compareLines(input, {
            sqlite3_column_double(row, 2), sqlite3_column_double(row, 3)
        });
        if (difference) {
            matches.push_back({
                reinterpret_cast<const char*>(sqlite3_column_text(row, 0)),
                sqlite3_column_int(row, 1),
                difference->angle,
                difference->length
            });
        }
    }
    std::sort(matches.begin(), matches.end(), [](const LineMatch& a, const LineMatch& b) {
        return std::tie(a.angleDifference, a.relativeLengthDifference, a.imagePath, a.functionID)
             < std::tie(b.angleDifference, b.relativeLengthDifference, b.imagePath, b.functionID);
    });
    return matches;
}


std::optional<LineDescriptor> describeLine(const functionData& function, double imageDiagonal) {
    if (function.isBezier)
        return std::nullopt;
    const double dx = function.end.x - function.start.x;
    const double dy = function.end.y - function.start.y;
    const double length = std::hypot(dx, dy);

    if (imageDiagonal <= 0.0 || length == 0.0)
        return std::nullopt;

    const double degrees = std::atan2(dy, dx) * 180.0 / std::acos(-1.0);
    const double orientation = std::fmod(degrees + 180.0, 180.0);

    return LineDescriptor{orientation, length / imageDiagonal};
}

void initializeLineBuckets(sqlite3* db) {
    execute(db, R"SQL(
        CREATE TABLE IF NOT EXISTS line_buckets (
            image_path TEXT NOT NULL,
            function_id INTEGER NOT NULL,
            angle_degrees REAL NOT NULL,
            normalized_length REAL NOT NULL,
            angle_bucket INTEGER NOT NULL,
            length_bucket INTEGER NOT NULL,
            PRIMARY KEY (image_path, function_id),
            FOREIGN KEY (image_path, function_id)
                REFERENCES image_functions(image_path, function_id)
                ON DELETE CASCADE
        );

        CREATE INDEX IF NOT EXISTS idx_line_buckets_geometry
            ON line_buckets(angle_bucket, length_bucket);
    )SQL");
}


void saveLineBucket(Statement& insert,const std::string& imagePath, const functionData& function, double imageDiagonal) {
    const auto descriptor = describeLine(function, imageDiagonal);
    if (!descriptor)
        return;

    insert.bind(1, imagePath);
    insert.bind(2, function.id);
    insert.bind(3, descriptor->angleDegrees);
    insert.bind(4, descriptor->normalizedLength);
    insert.bind(5, static_cast<int>(
        descriptor->angleDegrees / angleBucketWidth));
    insert.bind(6, static_cast<int>(
        descriptor->normalizedLength / lengthBucketWidth));
    insert.write();
}


std::vector<FunctionNeighbor> findFunctionNeighbors(
    const std::vector<functionData>& functions,
    double tolerance = neighborTolerancePixels
) {
    std::vector<FunctionNeighbor> neighbors;
    for (std::size_t i = 0; i < functions.size(); ++i) {
        const auto& first = functions[i];
        for (std::size_t j = i + 1; j < functions.size(); ++j) {
            const auto& second = functions[j];
            if (first.componentID != second.componentID)
                continue;

            const double distance = std::min({
                cv::norm(first.start - second.start),
                cv::norm(first.start - second.end),
                cv::norm(first.end - second.start),
                cv::norm(first.end - second.end)
            });
            if (distance <= tolerance) {
                neighbors.push_back({first.id, second.id, distance});
                neighbors.push_back({second.id, first.id, distance});
            }
        }
    }
    return neighbors;
}


void initializeFunctionTables(sqlite3* db) {
    execute(db, R"SQL(
        PRAGMA foreign_keys = ON;

        CREATE TABLE IF NOT EXISTS image_functions (
            image_path TEXT NOT NULL,
            function_id INTEGER NOT NULL,
            component_id INTEGER NOT NULL,
            path_id INTEGER NOT NULL,
            a REAL NOT NULL,
            b REAL NOT NULL,
            c REAL NOT NULL,
            start_x REAL NOT NULL,
            start_y REAL NOT NULL,
            end_x REAL NOT NULL,
            end_y REAL NOT NULL,
            PRIMARY KEY (image_path, function_id)
        );
        CREATE TABLE IF NOT EXISTS image_function_neighbors (
            image_path TEXT NOT NULL,
            function_id INTEGER NOT NULL,
            neighbor_id INTEGER NOT NULL,
            distance_pixels REAL NOT NULL CHECK (distance_pixels >= 0),
            PRIMARY KEY (image_path, function_id, neighbor_id),
            FOREIGN KEY (image_path, function_id)
                REFERENCES image_functions(image_path, function_id) ON DELETE CASCADE,
            FOREIGN KEY (image_path, neighbor_id)
                REFERENCES image_functions(image_path, function_id) ON DELETE CASCADE,
            CHECK (function_id <> neighbor_id)
        );

        CREATE INDEX IF NOT EXISTS idx_image_function_neighbors_reverse
            ON image_function_neighbors(image_path, neighbor_id);

        CREATE TABLE IF NOT EXISTS bezier_functions (
            image_path TEXT NOT NULL,
            function_id INTEGER NOT NULL CHECK (function_id < 0),
            control_x REAL NOT NULL,
            control_y REAL NOT NULL,
            PRIMARY KEY (image_path, function_id),
            FOREIGN KEY (image_path, function_id)
                REFERENCES image_functions(image_path, function_id) ON DELETE CASCADE
        );
    )SQL");
}

void saveImageFunctions(
    sqlite3* db,
    const std::string& imagePath,
    const std::vector<functionData>& functions,
    const std::vector<FunctionNeighbor>& neighbors,
    double imageDiagonal
) {
    execute(db, "BEGIN IMMEDIATE;");
    try {
        Statement remove(db, "DELETE FROM image_functions WHERE image_path = ?;");
        remove.bind(1, imagePath);
        remove.write();

        Statement insertFunction(db, R"SQL(
            INSERT INTO image_functions (
                image_path, function_id, component_id, path_id, a, b, c,
                start_x, start_y, end_x, end_y
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
        )SQL");
        Statement insertBucket(db, R"SQL(
            INSERT INTO line_buckets (
                image_path, function_id, angle_degrees, normalized_length,
                angle_bucket, length_bucket
            ) VALUES (?, ?, ?, ?, ?, ?);
        )SQL");
        Statement insertCurve(db, "INSERT INTO bezier_functions VALUES (?, ?, ?, ?);");
        for (const auto& function : functions) {
            insertFunction.bind(1, imagePath);
            insertFunction.bind(2, function.id);
            insertFunction.bind(3, function.componentID);
            insertFunction.bind(4, function.pathID);
            insertFunction.bind(5, function.a);
            insertFunction.bind(6, function.b);
            insertFunction.bind(7, function.c);
            insertFunction.bind(8, function.start.x);
            insertFunction.bind(9, function.start.y);
            insertFunction.bind(10, function.end.x);
            insertFunction.bind(11, function.end.y);
            insertFunction.write();
            saveLineBucket(insertBucket, imagePath, function, imageDiagonal);
            if (function.isBezier) {
                insertCurve.bind(1, imagePath);
                insertCurve.bind(2, function.id);
                insertCurve.bind(3, function.control.x);
                insertCurve.bind(4, function.control.y);
                insertCurve.write();
            }
        }
        Statement insertNeighbor(db, R"SQL(
            INSERT INTO image_function_neighbors (
                image_path, function_id, neighbor_id, distance_pixels
            ) VALUES (?, ?, ?, ?);
        )SQL");
        for (const auto& neighbor : neighbors) {
            insertNeighbor.bind(1, imagePath);
            insertNeighbor.bind(2, neighbor.functionID);
            insertNeighbor.bind(3, neighbor.neighborID);
            insertNeighbor.bind(4, neighbor.distancePixels);
            insertNeighbor.write();
        }
        execute(db, "COMMIT;");
    } catch (...) {
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw;
    }
}

bool hasBezierTable(sqlite3* db) {
    Statement table(db, "SELECT 1 FROM sqlite_master WHERE type='table' AND name='bezier_functions';");
    return table.next();
}

struct NearbyFunction {functionData function; double distancePixels;};

std::vector<NearbyFunction> lookupFunctionNeighbors(sqlite3* db, const std::string& imagePath, int functionID) {
    Statement query(db, R"SQL(
        SELECT f.function_id, f.component_id, f.path_id, f.a, f.b, f.c,
               f.start_x, f.start_y, f.end_x, f.end_y, n.distance_pixels
        FROM image_function_neighbors AS n
        JOIN image_functions AS f
          ON f.image_path = n.image_path AND f.function_id = n.neighbor_id
        WHERE n.image_path = ? AND n.function_id = ?
        ORDER BY n.distance_pixels, n.neighbor_id;
    )SQL");
    query.bind(1, imagePath);
    query.bind(2, functionID);

    std::vector<NearbyFunction> neighbors;
    while (query.next()) {
        sqlite3_stmt* row = query.get();
        functionData function;
        function.id = sqlite3_column_int(row, 0);
        function.componentID = sqlite3_column_int(row, 1);
        function.pathID = sqlite3_column_int(row, 2);
        function.a = sqlite3_column_double(row, 3);
        function.b = sqlite3_column_double(row, 4);
        function.c = sqlite3_column_double(row, 5);
        function.start = {sqlite3_column_double(row, 6), sqlite3_column_double(row, 7)};
        function.end = {sqlite3_column_double(row, 8), sqlite3_column_double(row, 9)};
        neighbors.push_back({function, sqlite3_column_double(row, 10)});
    }
    if (hasBezierTable(db)) {
        for (auto& neighbor : neighbors) {
            Statement curve(db, "SELECT control_x, control_y FROM bezier_functions WHERE image_path=? AND function_id=?;");
            curve.bind(1, imagePath);
            curve.bind(2, neighbor.function.id);
            if (curve.next()) {
                neighbor.function.isBezier = true;
                neighbor.function.control = {sqlite3_column_double(curve.get(), 0), sqlite3_column_double(curve.get(), 1)};
            }
        }
    }
    return neighbors;
}

void printFunction(const functionData& function) {
    std::cout << "Function " << function.id
              << " | Component " << function.componentID
              << " | Path " << function.pathID;
    if (function.isBezier)
        std::cout << "\n  Quadratic Bezier control: " << function.control;
    else
        std::cout << "\n  Equation: " << function.a << "x + "
                  << function.b << "y + " << function.c << " = 0";
    std::cout << "\n  Start: " << function.start << " | End: " << function.end << '\n';
}


std::string normalizePath(const std::filesystem::path& path) {
    return std::filesystem::absolute(path).lexically_normal().make_preferred().string();
}

void indexImage(sqlite3* db, const std::string& imagePath, bool printGeometry) {
    cv::Size imageSize;
    const auto functions = encodeObjectFunctions(imagePath, imageSize);
    const auto neighbors = findFunctionNeighbors(functions);
    const double diagonal = std::hypot(imageSize.width, imageSize.height);
    saveImageFunctions(db, imagePath, functions, neighbors, diagonal);

    if (printGeometry) {
        for (const auto& function : functions)
            printFunction(function);
    }
    std::cout << "Saved: " << imagePath
              << " | Functions: " << functions.size()
              << " | Neighbor pairs: " << neighbors.size() / 2 << '\n';
}

int processDataset(sqlite3* db, const std::filesystem::path& datasetRoot) {
    namespace fs = std::filesystem;
    std::size_t indexed = 0;
    std::size_t failed = 0;
    for (const auto& entry : fs::recursive_directory_iterator(datasetRoot)) {
        if (!entry.is_regular_file())
            continue;

        std::string extension = entry.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension != ".png" && extension != ".jpg" && extension != ".jpeg")
            continue;

        try {
            indexImage(db, normalizePath(entry.path()), false);
            ++indexed;
        } catch (const std::exception& error) {
            std::cerr << "Failed: " << entry.path() << " | " << error.what() << '\n';
            ++failed;
        }
    }
    std::cout << "Dataset: " << indexed << " indexed, " << failed << " failed.\n";
    if (indexed == 0 && failed == 0)
        std::cerr << "No supported images found in " << datasetRoot << '\n';
    return indexed > 0 && failed == 0 ? 0 : 1;
}

int refreshReferenceCurves(sqlite3* db) {
    initializeFunctionTables(db);
    std::vector<std::string> paths;
    Statement images(db, "SELECT DISTINCT image_path FROM image_functions ORDER BY image_path;");
    while (images.next())
        paths.emplace_back(reinterpret_cast<const char*>(sqlite3_column_text(images.get(), 0)));

    std::size_t completed = 0, failed = 0, curveCount = 0;
    for (const auto& imagePath : paths) {
        try {
            std::filesystem::path source(imagePath);
            const std::string oldRoot = "F:\\CARLA set\\";
            if (!std::filesystem::is_regular_file(source) && imagePath.rfind(oldRoot, 0) == 0)
                source = "F:\\CARLA\\" + imagePath.substr(oldRoot.size());
            cv::Size imageSize;
            const auto encoded = encodeObjectFunctions(source.string(), imageSize);
            std::vector<functionData> functions;
            Statement lines(db, "SELECT function_id, component_id, path_id, start_x, start_y, end_x, end_y FROM image_functions WHERE image_path=? AND function_id>=0;");
            lines.bind(1, imagePath);
            while (lines.next()) {
                functionData line;
                auto* row = lines.get();
                line.id = sqlite3_column_int(row, 0);
                line.componentID = sqlite3_column_int(row, 1);
                line.pathID = sqlite3_column_int(row, 2);
                line.start = {sqlite3_column_double(row, 3), sqlite3_column_double(row, 4)};
                line.end = {sqlite3_column_double(row, 5), sqlite3_column_double(row, 6)};
                functions.push_back(line);
            }
            for (const auto& function : encoded) {
                if (function.isBezier)
                    functions.push_back(function);
            }
            const auto neighbors = findFunctionNeighbors(functions);
            execute(db, "BEGIN IMMEDIATE;");
            try {
                Statement remove(db, "DELETE FROM image_functions WHERE image_path=? AND function_id<0;");
                remove.bind(1, imagePath);
                remove.write();
                Statement insert(db, "INSERT INTO image_functions VALUES (?, ?, ?, ?, 0, 0, 0, ?, ?, ?, ?);");
                Statement control(db, "INSERT INTO bezier_functions VALUES (?, ?, ?, ?);");
                for (const auto& function : functions) {
                    if (!function.isBezier)
                        continue;
                    insert.bind(1, imagePath);
                    insert.bind(2, function.id);
                    insert.bind(3, function.componentID);
                    insert.bind(4, function.pathID);
                    insert.bind(5, function.start.x);
                    insert.bind(6, function.start.y);
                    insert.bind(7, function.end.x);
                    insert.bind(8, function.end.y);
                    insert.write();
                    control.bind(1, imagePath);
                    control.bind(2, function.id);
                    control.bind(3, function.control.x);
                    control.bind(4, function.control.y);
                    control.write();
                }
                Statement link(db, "INSERT INTO image_function_neighbors VALUES (?, ?, ?, ?);");
                for (const auto& neighbor : neighbors) {
                    if (neighbor.functionID >= 0 && neighbor.neighborID >= 0)
                        continue;
                    link.bind(1, imagePath);
                    link.bind(2, neighbor.functionID);
                    link.bind(3, neighbor.neighborID);
                    link.bind(4, neighbor.distancePixels);
                    link.write();
                }
                execute(db, "COMMIT;");
            } catch (...) {
                sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
                throw;
            }
            curveCount += std::count_if(encoded.begin(), encoded.end(), [](const auto& f) { return f.isBezier; });
            ++completed;
            std::cout << "Refreshed curves: " << completed << "/" << paths.size() << " | " << imagePath << std::endl;
        } catch (const std::exception& error) {
            ++failed;
            std::cerr << "Curve refresh failed: " << imagePath << " | " << error.what() << '\n';
        }
    }
    std::cout << "Curve refresh: " << completed << " images, " << curveCount << " curves, " << failed << " failed.\n";
    return failed == 0 && completed > 0 ? 0 : 1;
}

struct NeighborLink {
    int functionID;
    unsigned endpoints;
};

using NeighborGraph = std::unordered_map<int, std::vector<NeighborLink>>;
using LineCandidates = std::vector<std::vector<LineMatch>>;

unsigned attachedEndpoints(
    const cv::Point2d& start, const cv::Point2d& end,
    const cv::Point2d& neighborStart, const cv::Point2d& neighborEnd
) {
    const bool atStart = std::min(cv::norm(start - neighborStart),
                                  cv::norm(start - neighborEnd)) <= neighborTolerancePixels;
    const bool atEnd = std::min(cv::norm(end - neighborStart),
                                cv::norm(end - neighborEnd)) <= neighborTolerancePixels;
    return (atStart ? 1u : 0u) | (atEnd ? 2u : 0u);
}

NeighborGraph loadReferenceNeighbors(sqlite3* db, const std::string& imagePath) {
    Statement query(db, R"SQL(
        SELECT n.function_id, n.neighbor_id,
               f.start_x, f.start_y, f.end_x, f.end_y,
               g.start_x, g.start_y, g.end_x, g.end_y
        FROM image_function_neighbors AS n
        JOIN image_functions AS f
          ON f.image_path = n.image_path AND f.function_id = n.function_id
        JOIN image_functions AS g
          ON g.image_path = n.image_path AND g.function_id = n.neighbor_id
        WHERE n.image_path = ?
        ORDER BY n.function_id, n.neighbor_id;
    )SQL");
    query.bind(1, imagePath);

    NeighborGraph graph;
    while (query.next()) {
        auto* row = query.get();
        const auto point = [row](int column) {
            return cv::Point2d(sqlite3_column_double(row, column),
                               sqlite3_column_double(row, column + 1));
        };
        graph[sqlite3_column_int(row, 0)].push_back({
            sqlite3_column_int(row, 1),
            attachedEndpoints(point(2), point(4), point(6), point(8))
        });
    }
    return graph;
}

const std::vector<NeighborLink>& neighborsOf(const NeighborGraph& graph, int id) {
    static const std::vector<NeighborLink> empty;
    const auto found = graph.find(id);
    return found == graph.end() ? empty : found->second;
}

std::size_t countMatchingNeighbors(
    const std::vector<NeighborLink>& input,
    const std::vector<NeighborLink>& reference,
    const std::unordered_map<int, std::size_t>& inputIndex,
    const LineCandidates& candidates,
    bool reversed
) {
    std::unordered_set<int> used;
    for (const auto& link : input) {
        // Bits 0 and 1 represent the anchor's start and end, respectively.
        const unsigned endpoints = reversed
            ? ((link.endpoints & 1u) << 1) | ((link.endpoints & 2u) >> 1)
            : link.endpoints;
        const auto& allowed = candidates[inputIndex.at(link.functionID)];
        for (const auto& other : reference) {
            if ((endpoints & other.endpoints) == 0 || used.count(other.functionID))
                continue;
            const auto sameFunction = [&other](const LineMatch& candidate) {
                return candidate.functionID == other.functionID;
            };
            if (std::any_of(allowed.begin(), allowed.end(), sameFunction)) {
                used.insert(other.functionID);
                break;
            }
        }
    }
    return used.size();
}

struct FunctionAssignment {
    LineMatch match;
    std::size_t matchingNeighbors;
    double score;
};

struct ReferenceAssignments {
    std::string imagePath;
    std::vector<std::optional<FunctionAssignment>> byInput;
    std::size_t matchedFunctions = 0;
    std::size_t unmatchedFunctions = 0;
    std::size_t remainingFunctions = 0;
    double score = 0.0;
};

ReferenceAssignments assignFunctions(
    const std::string& referencePath,
    const std::vector<functionData>& functions,
    const std::unordered_map<int, std::size_t>& inputIndex,
    const NeighborGraph& inputNeighbors,
    const NeighborGraph& referenceNeighbors,
    const LineCandidates& candidates
) {
    const std::size_t totalFunctions = functions.size();
    ReferenceAssignments result;
    result.imagePath = referencePath;
    result.byInput.resize(totalFunctions);
    result.remainingFunctions = totalFunctions;

    std::vector<std::size_t> order(totalFunctions);
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&candidates](std::size_t a, std::size_t b) {
        return candidates[a].size() < candidates[b].size();
    });

    std::unordered_set<int> assigned;
    for (std::size_t index : order) {
        const auto& neighbors = neighborsOf(inputNeighbors, functions[index].id);
        auto& best = result.byInput[index];
        for (const auto& candidate : candidates[index]) {
            if (assigned.count(candidate.functionID))
                continue;
            const auto& reference = neighborsOf(referenceNeighbors, candidate.functionID);
            const auto matched = std::max(
                countMatchingNeighbors(neighbors, reference, inputIndex, candidates, false),
                countMatchingNeighbors(neighbors, reference, inputIndex, candidates, true)
            );
            const double geometry = 1.0 - 0.5 * (
                candidate.angleDifference / angleBucketWidth
                + candidate.relativeLengthDifference / lengthTolerance
            );
            const double bonus = neighbors.empty() ? 0.0
                : static_cast<double>(matched) / neighbors.size();
            const double score = geometry + bonus;
            if (!best || score > best->score)
                best = FunctionAssignment{candidate, matched, score};
        }
        if (best) {
            assigned.insert(best->match.functionID);
            ++result.matchedFunctions;
            result.score += best->score;
        } else {
            ++result.unmatchedFunctions;
        }
        --result.remainingFunctions;
    }
    return result;
}

void searchImage(sqlite3* db, const std::string& imagePath) {
    cv::Size imageSize;
    const auto functions = encodeFunctions(imagePath, imageSize);
    const double diagonal = std::hypot(imageSize.width, imageSize.height);
    const std::size_t totalFunctions = functions.size();

    std::unordered_map<int, std::size_t> inputIndex;
    for (std::size_t i = 0; i < totalFunctions; ++i)
        inputIndex.emplace(functions[i].id, i);

    NeighborGraph inputNeighbors;
    for (const auto& link : findFunctionNeighbors(functions)) {
        const auto& anchor = functions[inputIndex.at(link.functionID)];
        const auto& neighbor = functions[inputIndex.at(link.neighborID)];
        inputNeighbors[link.functionID].push_back({
            link.neighborID,
            attachedEndpoints(anchor.start, anchor.end, neighbor.start, neighbor.end)
        });
    }

    // Keep bucket candidates and neighbor rows on the same SQLite snapshot.
    execute(db, "BEGIN;");
    std::map<std::string, LineCandidates> byReference;
    std::size_t candidateCount = 0;
    for (std::size_t i = 0; i < totalFunctions; ++i) {
        const auto descriptor = describeLine(functions[i], diagonal);
        if (!descriptor)
            continue;
        auto matches = findSimilarLines(db, *descriptor);
        candidateCount += matches.size();
        for (auto& match : matches) {
            auto entry = byReference.try_emplace(match.imagePath, totalFunctions).first;
            entry->second[i].push_back(std::move(match));
        }
    }

    std::vector<ReferenceAssignments> results;
    for (const auto& [referencePath, candidates] : byReference) {
        const auto referenceNeighbors = loadReferenceNeighbors(db, referencePath);
        results.push_back(assignFunctions(
            referencePath, functions, inputIndex, inputNeighbors, referenceNeighbors, candidates
        ));
    }
    execute(db, "COMMIT;");

    std::stable_sort(results.begin(), results.end(), [](const auto& a, const auto& b) {
        return std::tie(a.matchedFunctions, a.score) > std::tie(b.matchedFunctions, b.score);
    });

    std::cout << "Search: " << totalFunctions << " input lines, "
              << candidateCount << " candidate matches across "
              << results.size() << " reference images.\n";
    if (results.empty())
        std::cout << "No reference candidates | Unmatched: " << totalFunctions << '\n';

    for (const auto& result : results) {
        std::cout << "Reference: " << std::quoted(result.imagePath)
                  << " | Total: " << totalFunctions
                  << " | Matched: " << result.matchedFunctions
                  << " | Unmatched: " << result.unmatchedFunctions
                  << " | Remaining: " << result.remainingFunctions
                  << " | Score: " << result.score << '\n';
        for (std::size_t i = 0; i < totalFunctions; ++i) {
            const auto& assignment = result.byInput[i];
            std::cout << "  Input function " << functions[i].id << " -> ";
            if (!assignment) {
                std::cout << "unmatched\n";
                continue;
            }
            std::cout << "reference function " << assignment->match.functionID
                      << " | Matching neighbors: " << assignment->matchingNeighbors
                      << " | Angle difference: " << assignment->match.angleDifference << " deg"
                      << " | Relative length difference: " << assignment->match.relativeLengthDifference
                      << '\n';
        }
    }
}

class LineVocabulary {
public:
    void add(int tokenID, LineDescriptor descriptor) {
        const int angle = static_cast<int>(descriptor.angleDegrees / angleBucketWidth);
        const int length = static_cast<int>(descriptor.normalizedLength / lengthBucketWidth);
        buckets_[{angle, length}].push_back({tokenID, descriptor});
    }

    std::optional<int> lookup(LineDescriptor descriptor) const {
        const auto range = lineBucketRange(descriptor);
        std::optional<int> best;
        double bestDistance = 0.0;
        for (int angle : range.angles) {
            auto bucket = buckets_.lower_bound({angle, range.minLength});
            for (; bucket != buckets_.end() && bucket->first.first == angle
                 && bucket->first.second <= range.maxLength; ++bucket) {
                for (const auto& token : bucket->second) {
                    const auto difference = compareLines(descriptor, token.descriptor);
                    if (!difference)
                        continue;
                    const double distance = std::hypot(
                        difference->angle / angleBucketWidth,
                        difference->length / lengthTolerance
                    );
                    if (!best || std::tie(distance, token.id) < std::tie(bestDistance, *best)) {
                        best = token.id;
                        bestDistance = distance;
                    }
                }
            }
        }
        return best;
    }

private:
    struct Token {
        int id;
        LineDescriptor descriptor;
    };
    std::map<std::pair<int, int>, std::vector<Token>> buckets_;
};

void buildLineVocabulary(sqlite3* db) {
    execute(db, "PRAGMA foreign_keys = ON;");
    execute(db, "BEGIN IMMEDIATE;");
    int tokenCount = 0;
    std::size_t occurrenceCount = 0;
    try {
        execute(db, R"SQL(
            CREATE TABLE IF NOT EXISTS line_tokens (
                token_id INTEGER PRIMARY KEY,
                angle_degrees REAL NOT NULL,
                normalized_length REAL NOT NULL
            );

            CREATE TABLE IF NOT EXISTS line_token_occurrences (
                image_path TEXT NOT NULL,
                function_id INTEGER NOT NULL,
                token_id INTEGER NOT NULL,
                PRIMARY KEY (image_path, function_id),
                FOREIGN KEY (image_path, function_id)
                    REFERENCES line_buckets(image_path, function_id) ON DELETE CASCADE,
                FOREIGN KEY (token_id)
                    REFERENCES line_tokens(token_id) ON DELETE CASCADE
            );

            CREATE INDEX IF NOT EXISTS idx_line_token_occurrences_token
                ON line_token_occurrences(token_id, image_path);

            DELETE FROM line_tokens;
        )SQL");

        LineVocabulary vocabulary;
        Statement lines(db, R"SQL(
            SELECT image_path, function_id, angle_degrees, normalized_length
            FROM line_buckets
            ORDER BY image_path, function_id;
        )SQL");
        Statement insertToken(db, R"SQL(
            INSERT INTO line_tokens (token_id, angle_degrees, normalized_length)
            VALUES (?, ?, ?);
        )SQL");

        // Create a representative for each line that no existing token covers.
        while (lines.next()) {
            auto* row = lines.get();
            const LineDescriptor descriptor{
                sqlite3_column_double(row, 2), sqlite3_column_double(row, 3)
            };
            if (vocabulary.lookup(descriptor))
                continue;
            vocabulary.add(++tokenCount, descriptor);
            insertToken.bind(1, tokenCount);
            insertToken.bind(2, descriptor.angleDegrees);
            insertToken.bind(3, descriptor.normalizedLength);
            insertToken.write();
        }
        if (tokenCount == 0)
            throw std::runtime_error("No bucketed lines to tokenize. Index the reference images first.");

        // Assign against the complete vocabulary, using the same rule as queries.
        Statement references(db, R"SQL(
            SELECT image_path, function_id, angle_degrees, normalized_length
            FROM line_buckets;
        )SQL");
        Statement insertOccurrence(db, R"SQL(
            INSERT INTO line_token_occurrences (image_path, function_id, token_id)
            VALUES (?, ?, ?);
        )SQL");
        while (references.next()) {
            auto* row = references.get();
            const auto token = vocabulary.lookup({
                sqlite3_column_double(row, 2), sqlite3_column_double(row, 3)
            });
            if (!token)
                throw std::runtime_error("No token assigned to an indexed line.");
            insertOccurrence.bind(1, std::string(
                reinterpret_cast<const char*>(sqlite3_column_text(row, 0))));
            insertOccurrence.bind(2, sqlite3_column_int(row, 1));
            insertOccurrence.bind(3, *token);
            insertOccurrence.write();
            ++occurrenceCount;
        }
        execute(db, "COMMIT;");
    } catch (...) {
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw;
    }

    Statement common(db, R"SQL(
        SELECT token_id, COUNT(*), COUNT(DISTINCT image_path)
        FROM line_token_occurrences
        GROUP BY token_id
        HAVING COUNT(DISTINCT image_path) >= 2
        ORDER BY COUNT(DISTINCT image_path) DESC, COUNT(*) DESC, token_id;
    )SQL");
    std::size_t commonCount = 0;
    while (common.next()) {
        auto* row = common.get();
        ++commonCount;
        std::cout << "Token " << sqlite3_column_int(row, 0)
                  << " | Functions: " << sqlite3_column_int64(row, 1)
                  << " | Reference images: " << sqlite3_column_int64(row, 2) << '\n';
    }
    std::cout << "Tokenizer: " << occurrenceCount << " lines, " << tokenCount
              << " tokens, " << commonCount << " shared across at least two images.\n";
}

LineVocabulary loadLineVocabulary(sqlite3* db) {
    Statement table(db, "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'line_tokens';");
    if (!table.next())
        throw std::runtime_error("No token vocabulary. Run --tokenize first.");

    LineVocabulary vocabulary;
    Statement tokens(db, "SELECT token_id, angle_degrees, normalized_length FROM line_tokens;");
    if (!tokens.next())
        throw std::runtime_error("No tokens found. Run --tokenize first.");
    do {
        auto* row = tokens.get();
        vocabulary.add(sqlite3_column_int(row, 0), {
            sqlite3_column_double(row, 1), sqlite3_column_double(row, 2)
        });
    } while (tokens.next());
    return vocabulary;
}

void tokenizeImage(sqlite3* db, const std::string& imagePath) {
    const auto vocabulary = loadLineVocabulary(db);
    cv::Size imageSize;
    const auto functions = encodeFunctions(imagePath, imageSize);
    const double diagonal = std::hypot(imageSize.width, imageSize.height);
    for (const auto& function : functions) {
        const auto descriptor = describeLine(function, diagonal);
        const auto token = descriptor ? vocabulary.lookup(*descriptor) : std::nullopt;
        std::cout << "Function " << function.id << " | Component " << function.componentID
                  << " | Path " << function.pathID << " | Token: ";
        if (token)
            std::cout << *token << '\n';
        else
            std::cout << "unknown\n";
    }
}


enum class EditKind { Match, Substitute, Delete, Insert };

struct EditStep {
    EditKind kind;
    std::optional<int> inputFunctionID;
    std::optional<int> referenceFunctionID;
};

struct PathAlignment {
    std::size_t distance;
    std::vector<EditStep> steps;
};

PathAlignment alignTokenPaths(
    const std::vector<PathToken>& input,
    const std::vector<PathToken>& reference
) {
    const std::size_t n = input.size();
    const std::size_t m = reference.size();
    std::vector<std::vector<std::size_t>> cost(
        n + 1, std::vector<std::size_t>(m + 1)
    );

    for (std::size_t i = 0; i <= n; ++i)
        cost[i][0] = i;
    for (std::size_t j = 0; j <= m; ++j)
        cost[0][j] = j;

    const auto sameToken = [&](std::size_t i, std::size_t j) {
        return input[i].tokenID >= 0
            && input[i].tokenID == reference[j].tokenID;
    };

    for (std::size_t i = 1; i <= n; ++i) {
        for (std::size_t j = 1; j <= m; ++j) {
            const std::size_t substitution = sameToken(i - 1, j - 1) ? 0 : 1;
            cost[i][j] = std::min({
                cost[i - 1][j] + 1,
                cost[i][j - 1] + 1,
                cost[i - 1][j - 1] + substitution
            });
        }
    }

    PathAlignment result{cost[n][m], {}};
    std::size_t i = n;
    std::size_t j = m;

    while (i > 0 || j > 0) {
        if (i > 0 && j > 0) {
            const bool same = sameToken(i - 1, j - 1);
            if (cost[i][j] == cost[i - 1][j - 1] + (same ? 0 : 1)) {
                result.steps.push_back({
                    same ? EditKind::Match : EditKind::Substitute,
                    input[i - 1].functionID,
                    reference[j - 1].functionID
                });
                --i;
                --j;
                continue;
            }
        }

        if (i > 0 && cost[i][j] == cost[i - 1][j] + 1) {
            result.steps.push_back({
                EditKind::Delete, input[--i].functionID, std::nullopt
            });
        } else {
            result.steps.push_back({
                EditKind::Insert, std::nullopt, reference[--j].functionID
            });
        }
    }

    std::reverse(result.steps.begin(), result.steps.end());
    return result;
}

std::optional<LineComparison> compareLineGeometry(
    const LineSegment& input,
    const LineSegment& reference,
    const std::vector<std::pair<LineSegment, LineSegment>>& pairedNeighbors = {}
) {
    const cv::Point2d a = input.end - input.start;
    const cv::Point2d b = reference.end - reference.start;
    const double inputLength = cv::norm(a);
    const double referenceLength = cv::norm(b);

    if (inputLength <= 1e-12 || referenceLength <= 1e-12)
        return std::nullopt;

    const double pi = std::acos(-1.0);
    const double scale = referenceLength / inputLength;
    const auto inputMidpoint = (input.start + input.end) * 0.5;
    const auto referenceMidpoint = (reference.start + reference.end) * 0.5;

    std::optional<LineComparison> best;

    for (int direction : {1, -1}) {
        const cv::Point2d target = b * static_cast<double>(direction);
        const double cross = a.x * target.y - a.y * target.x;
        const double rotation = std::atan2(cross, a.dot(target));

        LineTransform transform{rotation, scale, {0.0, 0.0}};
        transform.translation = referenceMidpoint - transform.apply(inputMidpoint);

        // Example tuning: 45 degrees or a 2x scale change maxes out
        // the corresponding part of the anchor cost.
        const double angleCost = std::min(1.0, std::abs(rotation) / (pi / 4.0));
        const double scaleCost = std::min(1.0, std::abs(std::log2(scale)));
        const double anchorCost = 0.5 * (angleCost + scaleCost);

        double cost = anchorCost;
        if (!pairedNeighbors.empty()) {
            double totalNeighborCost = 0.0;
            for (const auto& [from, to] : pairedNeighbors) {
                const auto start = transform.apply(from.start);
                const auto end = transform.apply(from.end);

                const double forward = cv::norm(start - to.start)
                                     + cv::norm(end - to.end);
                const double reverse = cv::norm(start - to.end)
                                     + cv::norm(end - to.start);
                const double meanError = 0.5 * std::min(forward, reverse);

                // Full penalty at a mean error of 25% of the anchor length.
                totalNeighborCost += std::min(1.0, meanError / (0.25 * referenceLength));
            }

            const double neighborCost = totalNeighborCost / pairedNeighbors.size();
            cost = 0.85 * anchorCost + 0.15 * neighborCost;
        }

        if (!best || cost < best->substitutionCost)
            best = LineComparison{transform, cost};
    }

    return best;
}


struct TokenPath {
    std::vector<PathToken> tokens;
    std::vector<LineSegment> geometry;
};

struct PatternImage {
    std::vector<TokenPath> paths;
    std::size_t lineCount = 0;
};

PatternImage encodeTokenPaths(const std::string& imagePath, const LineVocabulary& vocabulary) {
    const auto edge = encodeImage(imagePath);
    const double diagonal = std::hypot(edge.at("width").get<double>(),
                                       edge.at("height").get<double>());
    PatternImage image;
    for (const auto& path : edge.at("piecewisePaths")) {
        TokenPath run;
        const auto finishRun = [&] {
            if (!run.tokens.empty()) {
                image.paths.push_back(std::move(run));
                run = TokenPath{};
            }
        };

        for (const auto& segment : path.at("segments")) {
            if (segment.at("type") != "line") {
                finishRun();
                continue;
            }
            const auto& data = segment.at("data");
            const auto point = [&data](const char* name) {
                const auto& p = data.at(name);
                return cv::Point2d(p.at(0).get<double>(), p.at(1).get<double>());
            };
            functionData line;
            line.start = point("start");
            line.end = point("end");
            const auto descriptor = describeLine(line, diagonal);
            if (!descriptor) {
                finishRun();
                continue;
            }
            run.tokens.push_back({
                vocabulary.lookup(*descriptor).value_or(-1), data.at("id").get<int>()
            });
            run.geometry.push_back({line.start * (1.0 / diagonal), line.end * (1.0 / diagonal)});
            ++image.lineCount;
        }
        finishRun();
    }
    return image;
}

double weightedPathDistance(const TokenPath& input, const TokenPath& reference) {
    std::vector<double> previous(reference.tokens.size() + 1);
    std::iota(previous.begin(), previous.end(), 0.0);
    std::vector<double> current(previous.size());

    for (std::size_t i = 0; i < input.tokens.size(); ++i) {
        current[0] = static_cast<double>(i + 1);
        for (std::size_t j = 0; j < reference.tokens.size(); ++j) {
            const int token = input.tokens[i].tokenID;
            double substitution = 0.0;
            if (token < 0 || token != reference.tokens[j].tokenID) {
                const auto geometry = compareLineGeometry(input.geometry[i], reference.geometry[j]);
                substitution = geometry ? geometry->substitutionCost : 1.0;
            }
            current[j + 1] = std::min({
                previous[j + 1] + 1.0, current[j] + 1.0, previous[j] + substitution
            });
        }
        previous.swap(current);
    }
    return previous.back();
}

using CostMatrix = std::vector<std::vector<double>>;

// Minimum-cost one-to-one assignment for a square matrix (Hungarian algorithm).
// Padding rows or columns account for paths that occur in only one image.
std::vector<std::size_t> minimumPathAssignment(const CostMatrix& costs) {
    const std::size_t n = costs.size();
    std::vector<double> rowPotential(n + 1), columnPotential(n + 1);
    std::vector<std::size_t> columnOwner(n + 1), predecessor(n + 1);
    for (std::size_t row = 1; row <= n; ++row) {
        columnOwner[0] = row;
        std::size_t column = 0;
        std::vector<double> slack(n + 1, std::numeric_limits<double>::infinity());
        std::vector<bool> visited(n + 1, false);
        do {
            visited[column] = true;
            const std::size_t owner = columnOwner[column];
            std::size_t next = 0;
            double delta = std::numeric_limits<double>::infinity();
            for (std::size_t j = 1; j <= n; ++j) {
                if (visited[j])
                    continue;
                const double reduced = costs[owner - 1][j - 1]
                    - rowPotential[owner] - columnPotential[j];
                if (reduced < slack[j]) {
                    slack[j] = reduced;
                    predecessor[j] = column;
                }
                if (slack[j] < delta) {
                    delta = slack[j];
                    next = j;
                }
            }
            for (std::size_t j = 0; j <= n; ++j) {
                if (visited[j]) {
                    rowPotential[columnOwner[j]] += delta;
                    columnPotential[j] -= delta;
                } else {
                    slack[j] -= delta;
                }
            }
            column = next;
        } while (columnOwner[column] != 0);

        do {
            const std::size_t previous = predecessor[column];
            columnOwner[column] = columnOwner[previous];
            column = previous;
        } while (column != 0);
    }

    std::vector<std::size_t> assignment(n);
    for (std::size_t column = 1; column <= n; ++column)
        assignment[columnOwner[column] - 1] = column - 1;
    return assignment;
}

struct PatternResult {
    std::string imagePath;
    std::size_t referenceLines = 0;
    std::size_t exactMatches = 0;
    std::size_t edits = 0;
    double weightedCost = 0.0;
    double normalizedCost = 0.0;
};

PatternResult comparePatternImages(
    const PatternImage& input, const PatternImage& reference, const std::string& imagePath
) {
    auto reversed = reference.paths;
    for (auto& path : reversed) {
        std::reverse(path.tokens.begin(), path.tokens.end());
        std::reverse(path.geometry.begin(), path.geometry.end());
    }
    const std::size_t n = std::max(input.paths.size(), reference.paths.size());
    CostMatrix ordinary(n, std::vector<double>(n));
    CostMatrix weighted = ordinary;
    std::vector<std::vector<std::size_t>> exact(n, std::vector<std::size_t>(n));
    const auto countMatches = [](const PathAlignment& alignment) {
        return static_cast<std::size_t>(std::count_if(
            alignment.steps.begin(), alignment.steps.end(),
            [](const EditStep& step) { return step.kind == EditKind::Match; }
        ));
    };

    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            if (i >= input.paths.size() || j >= reference.paths.size()) {
                const auto size = i < input.paths.size() ? input.paths[i].tokens.size()
                                                       : reference.paths[j].tokens.size();
                ordinary[i][j] = weighted[i][j] = static_cast<double>(size);
                continue;
            }
            const auto& a = input.paths[i];
            const auto& b = reference.paths[j];
            const auto forward = alignTokenPaths(a.tokens, b.tokens);
            const auto backward = alignTokenPaths(a.tokens, reversed[j].tokens);
            ordinary[i][j] = static_cast<double>(std::min(forward.distance, backward.distance));
            if (forward.distance == backward.distance)
                exact[i][j] = std::max(countMatches(forward), countMatches(backward));
            else
                exact[i][j] = countMatches(forward.distance < backward.distance ? forward : backward);
            weighted[i][j] = std::min(weightedPathDistance(a, b), weightedPathDistance(a, reversed[j]));
        }
    }

    // Among equally short ordinary alignments, prefer more exact token matches.
    CostMatrix ordinaryRanking = ordinary;
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j)
            ordinaryRanking[i][j] -= static_cast<double>(exact[i][j]) / (input.lineCount + 1.0);
    }
    const auto ordinaryAssignment = minimumPathAssignment(ordinaryRanking);
    const auto weightedAssignment = minimumPathAssignment(weighted);
    PatternResult result{imagePath, reference.lineCount};
    for (std::size_t i = 0; i < n; ++i) {
        result.edits += static_cast<std::size_t>(ordinary[i][ordinaryAssignment[i]]);
        result.exactMatches += exact[i][ordinaryAssignment[i]];
        result.weightedCost += weighted[i][weightedAssignment[i]];
    }
    result.normalizedCost = result.weightedCost / (input.lineCount + reference.lineCount);
    return result;
}

void searchPatterns(sqlite3* db, const std::string& imagePath) {
    execute(db, "BEGIN;");
    const auto vocabulary = loadLineVocabulary(db);
    const auto input = encodeTokenPaths(imagePath, vocabulary);
    if (input.lineCount == 0)
        throw std::runtime_error("The input image has no eligible line segments.");

    using TokenAssignments = std::unordered_map<int, int>;
    std::map<std::string, TokenAssignments> references;
    Statement indexed(db, R"SQL(
        SELECT b.image_path, b.function_id, o.token_id
        FROM line_buckets AS b
        LEFT JOIN line_token_occurrences AS o
          ON o.image_path = b.image_path AND o.function_id = b.function_id;
    )SQL");
    while (indexed.next()) {
        auto* row = indexed.get();
        if (sqlite3_column_type(row, 2) == SQLITE_NULL)
            throw std::runtime_error("Some reference lines have no assigned token. Rerun --tokenize.");
        const std::string path = reinterpret_cast<const char*>(sqlite3_column_text(row, 0));
        references[path].emplace(sqlite3_column_int(row, 1), sqlite3_column_int(row, 2));
    }
    execute(db, "COMMIT;");
    if (references.empty())
        throw std::runtime_error("No tokenized reference images found.");

    std::vector<PatternResult> results;
    for (const auto& [path, expected] : references) {
        try {
            const auto reference = path == imagePath ? input : encodeTokenPaths(path, vocabulary);
            if (reference.lineCount != expected.size())
                throw std::runtime_error("Indexed line count differs from the source image.");
            for (const auto& run : reference.paths) {
                for (const auto& line : run.tokens) {
                    const auto stored = expected.find(line.functionID);
                    if (stored == expected.end() || stored->second != line.tokenID)
                        throw std::runtime_error("Source lines differ from the stored token assignments.");
                }
            }
            results.push_back(comparePatternImages(input, reference, path));
        } catch (const std::exception& error) {
            throw std::runtime_error("Reference " + path + " | " + error.what());
        }
    }
    std::sort(results.begin(), results.end(), [](const PatternResult& a, const PatternResult& b) {
        if (a.normalizedCost != b.normalizedCost)
            return a.normalizedCost < b.normalizedCost;
        if (a.exactMatches != b.exactMatches)
            return a.exactMatches > b.exactMatches;
        return std::tie(a.edits, a.imagePath) < std::tie(b.edits, b.imagePath);
    });
    std::cout << "Pattern search: " << input.lineCount << " input lines across "
              << input.paths.size() << " paths, " << results.size() << " reference images.\n";
    for (const auto& result : results) {
        std::cout << "Reference: " << std::quoted(result.imagePath)
                  << " | Reference lines: " << result.referenceLines
                  << " | Exact matches: " << result.exactMatches
                  << " | Edits: " << result.edits
                  << " | Weighted cost: " << result.weightedCost
                  << " | Normalized cost: " << result.normalizedCost << '\n';
    }
}

struct ObjectLine {
    int id;
    LineSegment geometry;
    double length;
    double weight = 1.0;
};

double objectFunctionLength(const LineSegment& geometry) {
    if (!geometry.control)
        return cv::norm(geometry.end - geometry.start);
    double length = 0.0;
    auto previous = geometry.start;
    for (int sample = 1; sample <= 16; ++sample) {
        const auto point = geometry.pointAt(sample / 16.0);
        length += cv::norm(point - previous);
        previous = point;
    }
    return length;
}

struct ObjectMatch {
    std::size_t inputIndex;
    std::size_t referenceIndex;
    double error;
};

struct ObjectDetection {
    std::string referencePath;
    LineTransform transform;
    std::vector<ObjectMatch> matches;
    double score;
};

using ObjectNeighbors = std::unordered_map<int, std::unordered_set<int>>;

double scoreObjectMatches(
    const std::vector<ObjectMatch>& matches,
    const std::vector<ObjectLine>& input,
    const std::vector<ObjectLine>& reference,
    const ObjectNeighbors& inputNeighbors,
    const ObjectNeighbors& referenceNeighbors
) {
    double score = 0.0;
    for (std::size_t i = 0; i < matches.size(); ++i) {
        const auto& a = matches[i];
        const auto& line = reference[a.referenceIndex];
        score += line.weight / (1.0 + a.error);
        const auto in = inputNeighbors.find(input[a.inputIndex].id);
        const auto ref = referenceNeighbors.find(line.id);
        if (in == inputNeighbors.end() || ref == referenceNeighbors.end())
            continue;
        for (std::size_t j = 0; j < i; ++j) {
            const auto& b = matches[j];
            if (in->second.count(input[b.inputIndex].id)
                && ref->second.count(reference[b.referenceIndex].id))
                score += 0.15;
        }
    }
    return score;
}

std::vector<ObjectMatch> verifyObjectPlacement(
    const LineTransform& transform,
    const std::vector<ObjectLine>& input,
    const std::vector<ObjectLine>& reference
) {
    constexpr double endpointTolerance = 5.0;
    std::vector<ObjectMatch> candidates;
    for (std::size_t j = 0; j < reference.size(); ++j) {
        const auto start = transform.apply(reference[j].geometry.start);
        const auto end = transform.apply(reference[j].geometry.end);
        for (std::size_t i = 0; i < input.size(); ++i) {
            const auto& line = input[i].geometry;
            if (line.control.has_value() != reference[j].geometry.control.has_value())
                continue;
            const auto distanceSquared = [](cv::Point2d a, cv::Point2d b) {
                const auto delta = a - b;
                return delta.dot(delta);
            };
            double error = std::min(
                std::max(distanceSquared(start, line.start), distanceSquared(end, line.end)),
                std::max(distanceSquared(start, line.end), distanceSquared(end, line.start))
            );
            if (error <= endpointTolerance * endpointTolerance && line.control) {
                double forward = 0.0, reversed = 0.0;
                for (int sample = 0; sample <= 16; ++sample) {
                    const double t = sample / 16.0;
                    const auto point = transform.apply(reference[j].geometry.pointAt(t));
                    forward = std::max(forward, distanceSquared(point, line.pointAt(t)));
                    reversed = std::max(reversed, distanceSquared(point, line.pointAt(1.0 - t)));
                }
                error = std::min(forward, reversed);
            }
            if (error <= endpointTolerance * endpointTolerance)
                candidates.push_back({i, j, std::sqrt(error)});
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        return std::tie(a.error, a.inputIndex, a.referenceIndex)
             < std::tie(b.error, b.inputIndex, b.referenceIndex);
    });
    std::vector<bool> usedInput(input.size()), usedReference(reference.size());
    std::vector<ObjectMatch> matches;
    for (const auto& match : candidates) {
        if (usedInput[match.inputIndex] || usedReference[match.referenceIndex])
            continue;
        usedInput[match.inputIndex] = usedReference[match.referenceIndex] = true;
        matches.push_back(match);
    }
    return matches;
}

std::vector<LineTransform> voteForObjectPlacements(
    const std::vector<ObjectLine>& input, const std::vector<ObjectLine>& reference
) {
    struct Vote {
        LineTransform transform;
        std::size_t lastInput;
        double support;
        double lastWeight;
        std::size_t count;
    };
    constexpr double positionBin = 8.0;
    constexpr int rotationBins = 72;
    constexpr double logScaleBin = 0.10;
    constexpr std::size_t maxPlacementsPerView = 16;
    const double turn = 2.0 * std::acos(-1.0);
    // Anchor translation at the reference center to avoid origin-dependent voting.
    cv::Point2d center;
    for (const auto& line : reference)
        center += (line.geometry.start + line.geometry.end) * 0.5;
    center *= 1.0 / reference.size();

    std::map<std::array<int, 4>, Vote> bins;
    for (std::size_t i = 0; i < input.size(); ++i) {
        const auto& a = input[i];
        const auto inputDirection = a.geometry.end - a.geometry.start;
        const auto midpoint = (a.geometry.start + a.geometry.end) * 0.5;
        for (const auto& b : reference) {
            if (a.geometry.control.has_value() != b.geometry.control.has_value())
                continue;
            const double scale = a.length / b.length;
            if (scale < 0.25 || scale > 4.0)
                continue;
            const auto direction = b.geometry.end - b.geometry.start;
            const auto referenceMidpoint = (b.geometry.start + b.geometry.end) * 0.5;
            for (double sign : {1.0, -1.0}) {
                const auto target = inputDirection * sign;
                const double rotation = std::atan2(
                    direction.x * target.y - direction.y * target.x, direction.dot(target)
                );
                LineTransform placement{rotation, scale, {}};
                placement.translation = midpoint - placement.apply(referenceMidpoint);
                if (a.geometry.control
                    && cv::norm(placement.apply(b.geometry.pointAt(0.5)) - a.geometry.pointAt(0.5)) > 5.0)
                    continue;
                const auto location = placement.apply(center);
                const auto key = std::array<int, 4>{
                    static_cast<int>(std::lround(location.x / positionBin)),
                    static_cast<int>(std::lround(location.y / positionBin)),
                    (static_cast<int>(std::lround(rotation / turn * rotationBins))
                        + rotationBins) % rotationBins,
                    static_cast<int>(std::lround(std::log2(scale) / logScaleBin))
                };
                auto [it, inserted] = bins.try_emplace(key, Vote{placement, i, b.weight, b.weight, 1});
                if (inserted)
                    continue;
                auto& vote = it->second;
                if (vote.lastInput != i) {
                    vote.lastInput = i;
                    vote.lastWeight = 0.0;
                    ++vote.count;
                }
                if (b.weight > vote.lastWeight) {
                    vote.support += b.weight - vote.lastWeight;
                    vote.lastWeight = b.weight;
                }
            }
        }
    }

    std::vector<const Vote*> ranked;
    for (const auto& entry : bins) {
        if (entry.second.count >= 3)
            ranked.push_back(&entry.second);
    }
    const auto limit = std::min(ranked.size(), maxPlacementsPerView);
    std::partial_sort(ranked.begin(), ranked.begin() + limit, ranked.end(),
        [](const Vote* a, const Vote* b) { return a->support > b->support; });
    std::vector<LineTransform> placements;
    for (std::size_t i = 0; i < limit; ++i)
        placements.push_back(ranked[i]->transform);
    return placements;
}

void searchObjects(sqlite3* db, const std::string& imagePath,
                   const std::string& outputPath, const char* depthPath) {
    constexpr std::size_t minSupport = 4;
    constexpr double minCoverage = 0.35;
    cv::Size imageSize;
    const auto functions = encodeObjectFunctions(imagePath, imageSize);
    std::vector<ObjectLine> input;
    for (const auto& line : functions) {
        LineSegment geometry{line.start, line.end};
        if (line.isBezier)
            geometry.control = line.control;
        const double length = objectFunctionLength(geometry);
        if (length > 0.0 && cv::norm(line.end - line.start) > 0.0)
            input.push_back({line.id, geometry, length});
    }
    ObjectNeighbors inputNeighbors;
    for (const auto& link : findFunctionNeighbors(functions))
        inputNeighbors[link.functionID].insert(link.neighborID);

    struct ReferenceView {
        std::vector<ObjectLine> lines;
        ObjectNeighbors neighbors;
    };
    std::map<std::string, ReferenceView> references;
    // Score rarity by distinct meshes so repeated views of one mesh count once.
    // Token assignments are optional. The geometry search also works before tokenization.
    execute(db, "BEGIN;");
    std::unordered_map<int, std::unordered_set<std::string>> tokenMeshes;
    std::unordered_set<std::string> meshes;
    std::map<std::pair<std::string, int>, int> tokens;
    Statement table(db, "SELECT 1 FROM sqlite_master WHERE name = 'line_token_occurrences' AND type = 'table';");
    if (table.next()) {
        Statement assigned(db, "SELECT image_path, function_id, token_id FROM line_token_occurrences;");
        while (assigned.next()) {
            auto* row = assigned.get();
            const std::string path = reinterpret_cast<const char*>(sqlite3_column_text(row, 0));
            const auto mesh = std::filesystem::path(path).parent_path().parent_path().string();
            const int token = sqlite3_column_int(row, 2);
            tokens.emplace(std::make_pair(path, sqlite3_column_int(row, 1)), token);
            tokenMeshes[token].insert(mesh);
            meshes.insert(mesh);
        }
    }
    Statement lines(db, R"SQL(
        SELECT f.image_path, f.function_id, f.start_x, f.start_y, f.end_x, f.end_y
        FROM image_functions f JOIN line_buckets b USING (image_path, function_id)
        ORDER BY f.image_path, f.function_id;
    )SQL");
    while (lines.next()) {
        auto* row = lines.get();
        const std::string path = reinterpret_cast<const char*>(sqlite3_column_text(row, 0));
        const int id = sqlite3_column_int(row, 1);
        const LineSegment geometry{
            {sqlite3_column_double(row, 2), sqlite3_column_double(row, 3)},
            {sqlite3_column_double(row, 4), sqlite3_column_double(row, 5)}
        };
        const double length = cv::norm(geometry.end - geometry.start);
        if (length <= 0.0)
            continue;
        double weight = 1.0;
        const auto token = tokens.find({path, id});
        if (token != tokens.end()) {
            const auto frequency = tokenMeshes.at(token->second).size();
            weight += 0.25 * std::log((meshes.size() + 1.0) / (frequency + 1.0));
        }
        references[path].lines.push_back({id, geometry, length, weight});
    }
    Statement edges(db, "SELECT image_path, function_id, neighbor_id FROM image_function_neighbors;");
    while (edges.next()) {
        auto* row = edges.get();
        const auto path = reinterpret_cast<const char*>(sqlite3_column_text(row, 0));
        const auto view = references.find(path);
        if (view != references.end())
            view->second.neighbors[sqlite3_column_int(row, 1)].insert(sqlite3_column_int(row, 2));
    }
    if (hasBezierTable(db)) {
        Statement curves(db, R"SQL(
            SELECT f.image_path, f.function_id, f.start_x, f.start_y, f.end_x, f.end_y,
                   b.control_x, b.control_y
            FROM image_functions f JOIN bezier_functions b USING (image_path, function_id)
            ORDER BY f.image_path, f.function_id;
        )SQL");
        while (curves.next()) {
            auto* row = curves.get();
            const std::string path = reinterpret_cast<const char*>(sqlite3_column_text(row, 0));
            LineSegment geometry{
                {sqlite3_column_double(row, 2), sqlite3_column_double(row, 3)},
                {sqlite3_column_double(row, 4), sqlite3_column_double(row, 5)},
                cv::Point2d(sqlite3_column_double(row, 6), sqlite3_column_double(row, 7))
            };
            if (cv::norm(geometry.end - geometry.start) > 0.0)
                references[path].lines.push_back({sqlite3_column_int(row, 1), geometry, objectFunctionLength(geometry)});
        }
        // Curves can be the only geometry in a view, so load their links after their views exist.
        Statement links(db, "SELECT image_path, function_id, neighbor_id FROM image_function_neighbors WHERE function_id<0 OR neighbor_id<0;");
        while (links.next()) {
            const auto path = reinterpret_cast<const char*>(sqlite3_column_text(links.get(), 0));
            const auto view = references.find(path);
            if (view != references.end())
                view->second.neighbors[sqlite3_column_int(links.get(), 1)].insert(sqlite3_column_int(links.get(), 2));
        }
    }
    execute(db, "COMMIT;");

    std::vector<ObjectDetection> candidates;
    std::size_t scannedViews = 0;
    for (const auto& [path, view] : references) {
        if (++scannedViews % 100 == 0)
            std::cout << "Object search: " << scannedViews << "/" << references.size()
                      << " reference views checked" << std::endl;
        if (view.lines.size() < minSupport || input.size() < minSupport)
            continue;
        for (const auto& transform : voteForObjectPlacements(input, view.lines)) {
            auto matches = verifyObjectPlacement(transform, input, view.lines);
            if (matches.size() < minSupport || matches.size() < minCoverage * view.lines.size())
                continue;
            const double score = scoreObjectMatches(
                matches, input, view.lines, inputNeighbors, view.neighbors
            );
            candidates.push_back({path, transform, std::move(matches), score});
        }
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        return a.score > b.score;
    });

    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> depth(nullptr, sqlite3_close);
    if (depthPath) {
        sqlite3* connection = nullptr;
        const int status = sqlite3_open_v2(depthPath, &connection, SQLITE_OPEN_READONLY, nullptr);
        depth.reset(connection);
        if (status != SQLITE_OK)
            throw std::runtime_error(depth ? sqlite3_errmsg(depth.get()) : sqlite3_errstr(status));
    }
    nlohmann::json detections = nlohmann::json::array();
    std::vector<bool> claimed(input.size());
    for (const auto& candidate : candidates) {
        const auto overlap = std::count_if(candidate.matches.begin(), candidate.matches.end(),
            [&claimed](const ObjectMatch& match) { return claimed[match.inputIndex]; });
        if (overlap != 0)
            continue;

        const auto& view = references.at(candidate.referencePath);
        const auto path = std::filesystem::path(candidate.referencePath);
        auto matches = nlohmann::json::array();
        double left = imageSize.width, top = imageSize.height, right = 0.0, bottom = 0.0;
        for (const auto& match : candidate.matches) {
            claimed[match.inputIndex] = true;
            const auto& line = input[match.inputIndex];
            const int referenceId = view.lines[match.referenceIndex].id;
            const int samples = line.geometry.control ? 16 : 1;
            for (int sample = 0; sample <= samples; ++sample) {
                const auto point = line.geometry.pointAt(static_cast<double>(sample) / samples);
                left = std::min(left, point.x);
                top = std::min(top, point.y);
                right = std::max(right, point.x);
                bottom = std::max(bottom, point.y);
            }
            nlohmann::json entry{
                {"inputFunctionID", line.id}, {"referenceFunctionID", referenceId},
                {"type", line.geometry.control ? "bezier" : "line"},
                {"endpointErrorPixels", match.error}
            };
            if (line.geometry.control) {
                entry["inputNativeCurveID"] = -line.id - 1;
                entry["referenceNativeCurveID"] = -referenceId - 1;
                entry["curveErrorPixels"] = match.error;
            }
            if (depth) {
                Statement hits(depth.get(), R"SQL(
                    SELECT token_id, points FROM token_depth_occurrences
                    WHERE image_path = ? AND function_id = ?;
                )SQL");
                hits.bind(1, candidate.referencePath);
                hits.bind(2, referenceId);
                if (hits.next()) {
                    entry["tokenID"] = sqlite3_column_int(hits.get(), 0);
                    entry["referenceHits"] = nlohmann::json::parse(
                        reinterpret_cast<const char*>(sqlite3_column_text(hits.get(), 1))
                    );
                }
            }
            matches.push_back(std::move(entry));
        }
        const auto& t = candidate.transform;
        detections.push_back({
            {"meshID", path.parent_path().parent_path().filename().string()},
            {"referenceView", path.parent_path().filename().string()},
            {"referenceImage", candidate.referencePath},
            {"score", candidate.score},
            {"referenceCoverage", static_cast<double>(matches.size()) / view.lines.size()},
            {"boundsXYXY", {left, top, right, bottom}},
            {"referenceToInput", {{"rotationRadians", t.rotationRadians}, {"scale", t.scale},
                                  {"translation", {t.translation.x, t.translation.y}}}},
            {"matches", std::move(matches)}
        });
    }
    auto unassigned = nlohmann::json::array();
    for (std::size_t i = 0; i < input.size(); ++i) {
        if (!claimed[i])
            unassigned.push_back(input[i].id);
    }
    const nlohmann::json report{
        {"inputImage", imagePath}, {"detections", detections}, {"unassignedFunctionIDs", unassigned},
        {"inputFunctionCounts", {
            {"lines", std::count_if(input.begin(), input.end(), [](const auto& f) { return !f.geometry.control; })},
            {"bezier", std::count_if(input.begin(), input.end(), [](const auto& f) { return f.geometry.control.has_value(); })}
        }}
    };
    std::ofstream output(outputPath);
    if (!(output << report.dump(2) << '\n') || !(output.flush()))
        throw std::runtime_error("Unable to write object detections to " + outputPath);
    std::cout << "Object search: " << detections.size() << " detections, "
              << unassigned.size() << " unassigned functions (lines and Bezier curves).\n";
}

int main(int argc, char* argv[]) {
    const std::string mode = argc > 1 ? argv[1] : "";

    const bool lookup = mode == "--lookup";
    const bool search = mode == "--search";
    const bool searchPatternsMode = mode == "--search-patterns";
    const bool searchObjectsMode = mode == "--search-objects";
    const bool buildDataset = mode == "--build-dataset";
    const bool tokenize = mode == "--tokenize";
    const bool refreshCurves = mode == "--refresh-curves";
    const bool tokenizeQuery = mode == "--tokenize-image";
    const bool indexSingle = !mode.empty() && mode.rfind("--", 0) != 0;

    const bool validArgs =
        (lookup && argc == 5) ||
        (searchObjectsMode && (argc == 5 || argc == 6)) ||
        ((search || searchPatternsMode || buildDataset || tokenizeQuery)
            && argc == 4) ||
        ((indexSingle || tokenize || refreshCurves) && argc == 3);

    if (!validArgs) {
        std::cerr
            << "Usage:\n"
            << "  classifierModel IMAGE DATABASE\n"
            << "  classifierModel --build-dataset DATASET_ROOT DATABASE\n"
            << "  classifierModel --search IMAGE DATABASE\n"
            << "  classifierModel --search-patterns IMAGE DATABASE\n"
            << "  classifierModel --search-objects IMAGE DATABASE OUTPUT_JSON [DEPTH_DATABASE]\n"
            << "  classifierModel --lookup IMAGE DATABASE FUNCTION_ID\n"
            << "  classifierModel --tokenize DATABASE\n"
            << "  classifierModel --refresh-curves DATABASE\n"
            << "  classifierModel --tokenize-image IMAGE DATABASE\n";
        return 1;
    }

    try {
        const int firstArg = indexSingle ? 1 : 2;
        const std::string inputPath =
            (tokenize || refreshCurves) ? "" : normalizePath(argv[firstArg]);
        const char* databasePath = argv[(tokenize || refreshCurves) ? 2 : firstArg + 1];

        int flags = SQLITE_OPEN_READONLY;
        if (tokenize || refreshCurves) {
            flags = SQLITE_OPEN_READWRITE;
        } else if (buildDataset || indexSingle) {
            flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
        }

        sqlite3* connection = nullptr;
        const int status = sqlite3_open_v2(
            databasePath, &connection, flags, nullptr
        );
        std::unique_ptr<sqlite3, decltype(&sqlite3_close)>
            db(connection, sqlite3_close);

        if (status != SQLITE_OK) {
            throw std::runtime_error(
                db ? sqlite3_errmsg(db.get()) : sqlite3_errstr(status)
            );
        }

        if (refreshCurves) {
            return refreshReferenceCurves(db.get());
        } else if (tokenize) {
            buildLineVocabulary(db.get());
        } else if (tokenizeQuery) {
            tokenizeImage(db.get(), inputPath);
        } else if (searchPatternsMode) {
            searchPatterns(db.get(), inputPath);
        } else if (searchObjectsMode) {
            const auto output = normalizePath(argv[4]);
            const auto sameFile = [&output](const std::string& path) {
                std::error_code error;
                return output == normalizePath(path)
                    || std::filesystem::equivalent(output, path, error);
            };
            if (sameFile(databasePath) || sameFile(inputPath)
                || (argc == 6 && sameFile(argv[5])))
                throw std::runtime_error("OUTPUT_JSON must be separate from the input files.");
            searchObjects(db.get(), inputPath, argv[4], argc == 6 ? argv[5] : nullptr);
        } else if (search) {
            searchImage(db.get(), inputPath);
        } else if (lookup) {
            const std::string argument = argv[4];
            std::size_t parsed = 0;
            const int functionID = std::stoi(argument, &parsed);

            if (parsed != argument.size())
                throw std::runtime_error("FUNCTION_ID must be an integer (negative IDs identify Bezier curves).");

            Statement exists(db.get(), R"SQL(
                SELECT 1 FROM image_functions
                WHERE image_path = ? AND function_id = ?;
            )SQL");
            exists.bind(1, inputPath);
            exists.bind(2, functionID);

            if (!exists.next())
                throw std::runtime_error("No stored function with that ID for this image.");

            const auto neighbors =
                lookupFunctionNeighbors(db.get(), inputPath, functionID);
            std::cout << "Function " << functionID << " has "
                      << neighbors.size() << " neighbors.\n";

            for (const auto& neighbor : neighbors) {
                printFunction(neighbor.function);
                std::cout << "  Endpoint distance: "
                          << neighbor.distancePixels << " px\n";
            }
        } else {
            initializeFunctionTables(db.get());
            initializeLineBuckets(db.get());

            if (buildDataset)
                return processDataset(db.get(), inputPath);

            indexImage(db.get(), inputPath, true);
        }
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
