#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <cmath>
#include <optional>
#include <sqlite3.h>

#include "edgeEncoder.h"


#include <cmath>
#include <optional>

constexpr double angleBucketWidth = 10.0;
constexpr int angleBucketCount = 18;
constexpr double lengthBucketWidth = 0.01;
constexpr double lengthTolerance = 0.10;

struct LineDescriptor {
    double angleDegrees;
    double normalizedLength;
};

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



std::vector<LineMatch> findSimilarLines(sqlite3* db, const LineDescriptor& input) {
    const int center = static_cast<int>(
        input.angleDegrees / angleBucketWidth);
    const int previous = (center + angleBucketCount - 1) % angleBucketCount;
    const int next = (center + 1) % angleBucketCount;

    const double shortest = input.normalizedLength * (1.0 - lengthTolerance);
    const double longest = input.normalizedLength / (1.0 - lengthTolerance);

    Statement query(db, R"SQL(
        SELECT image_path, function_id, angle_degrees, normalized_length
        FROM line_buckets
        WHERE angle_bucket IN (?, ?, ?)
          AND length_bucket BETWEEN ? AND ?;
    )SQL");

    query.bind(1, previous);
    query.bind(2, center);
    query.bind(3, next);
    query.bind(4, static_cast<int>(shortest / lengthBucketWidth));
    query.bind(5, static_cast<int>(longest / lengthBucketWidth));

    std::vector<LineMatch> matches;
    while (query.next()) {
        sqlite3_stmt* row = query.get();
        const double angle = sqlite3_column_double(row, 2);
        const double length = sqlite3_column_double(row, 3);

        const double delta = std::abs(angle - input.angleDegrees);
        const double angleDifference = std::min(delta, 180.0 - delta);
        const double lengthDifference =
            std::abs(length - input.normalizedLength) /
            std::max(length, input.normalizedLength);

        if (angleDifference <= 10.0 && lengthDifference <= lengthTolerance) {
            matches.push_back({
                reinterpret_cast<const char*>(sqlite3_column_text(row, 0)),
                sqlite3_column_int(row, 1),
                angleDifference,
                lengthDifference
            });
        }
    }
    return matches;
}


std::optional<LineDescriptor> describeLine(const functionData& function, double imageDiagonal) {
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


std::vector<FunctionNeighbor> findFunctionNeighbors(const std::vector<functionData>& functions, double tolerance = 5.0) {
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
    )SQL");
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
void saveImageFunctions(sqlite3* db, const std::string& imagePath, const std::vector<functionData>& functions, const std::vector<FunctionNeighbor>& neighbors) {
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
    return neighbors;
}

void printFunction(const functionData& function) {
    std::cout << "Function " << function.id
              << " | Component " << function.componentID
              << " | Path " << function.pathID
              << "\n  Equation: " << function.a << "x + "
              << function.b << "y + " << function.c << " = 0"
              << "\n  Start: " << function.start << " | End: " << function.end << '\n';
}


void processDataset(sqlite3* db, const std::string& datasetRoot) {
    namespace fs = std::filesystem;

    for (const auto& entry :
         fs::recursive_directory_iterator(datasetRoot)) {

        if (!entry.is_regular_file())
            continue;

        std::string extension = entry.path().extension().string();

        if (extension != ".png" &&
            extension != ".jpg" &&
            extension != ".jpeg")
            continue;

        try {
            std::string imagePath = entry.path().string();

            auto functions = encodeFunctions(imagePath);
            auto neighbors = findFunctionNeighbors(functions);

            saveImageFunctions(db, imagePath, functions, neighbors);

            std::cout << "Saved: " << imagePath
                      << " | Functions: " << functions.size()
                      << " | Neighbor pairs: " << neighbors.size() / 2
                      << '\n';

        } catch (const std::exception& e) {
            std::cerr << "Failed: " << entry.path()
                      << " | " << e.what() << '\n';
        }
    }
}



int main(int argc, char* argv[]) {
    const bool lookup = argc > 1 && std::string(argv[1]) == "--lookup";
    if ((lookup && argc != 5) || (!lookup && argc != 3)) {
        std::cerr << "Usage:\n"
                  << "  classifierModel IMAGE DATABASE\n"
                  << "  classifierModel --lookup IMAGE DATABASE FUNCTION_ID\n";
        return 1;
    }

    try {
        const int firstArg = lookup ? 2 : 1;
        const std::string imagePath =
            std::filesystem::absolute(argv[firstArg]).lexically_normal().string();
        sqlite3* connection = nullptr;
        const int flags = lookup ? SQLITE_OPEN_READONLY
                                 : SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
        const int result = sqlite3_open_v2(argv[firstArg + 1], &connection, flags, nullptr);
        std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db(connection, sqlite3_close);
        if (result != SQLITE_OK)
            throw std::runtime_error(db ? sqlite3_errmsg(db.get()) : sqlite3_errstr(result));

        if (lookup) {
            const std::string argument = argv[4];
            std::size_t parsed = 0;
            const int functionID = std::stoi(argument, &parsed);
            if (parsed != argument.size() || functionID < 0)
                throw std::runtime_error("FUNCTION_ID must be a nonnegative integer.");

            Statement exists(db.get(),
                "SELECT 1 FROM image_functions WHERE image_path = ? AND function_id = ?;");
            exists.bind(1, imagePath);
            exists.bind(2, functionID);
            if (!exists.next())
                throw std::runtime_error("No stored function with that ID for this image.");

            const auto neighbors = lookupFunctionNeighbors(db.get(), imagePath, functionID);
            std::cout << "Function " << functionID << " has " << neighbors.size() << " neighbors.\n";
            for (const auto& neighbor : neighbors) {
                printFunction(neighbor.function);
                std::cout << "  Endpoint distance: " << neighbor.distancePixels << " px\n";
            }
        } else {
            const auto functions = encodeFunctions(imagePath);
            const auto neighbors = findFunctionNeighbors(functions);
            initializeFunctionTables(db.get());
            saveImageFunctions(db.get(), imagePath, functions, neighbors);
            for (const auto& function : functions)
                printFunction(function);
            std::cout << "Saved " << functions.size() << " functions and "
                      << neighbors.size() / 2 << " neighbor pairs.\n";
        }
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
