#ifndef EDGE_ENCODER_H
#define EDGE_ENCODER_H

#include <string>
#include <nlohmann/json.hpp>

// Interface declaration
nlohmann::json encodeImage(const std::string& imagePath);

#endif
