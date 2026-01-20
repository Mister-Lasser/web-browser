#pragma once
#include <string>
#include <string_view>

class URL {
public:
    explicit URL(std::string url);

    std::string_view scheme() const;
    std::string_view host() const;
    std::string_view path() const;

private:
    std::string m_url;
    std::string_view m_scheme, m_host, m_path;
};
