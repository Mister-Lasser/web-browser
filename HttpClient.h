#pragma once
#include <optional>
#include <string>

class URL;

class HttpClient {
public:
    std::optional<std::string> get(const URL& url);
};
