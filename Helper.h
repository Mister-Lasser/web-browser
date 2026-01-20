#pragma once
#include <string>

namespace Helper {
    std::string strip(const std::string& input)
    {
		auto start_it = input.begin();
		auto end_it = input.end();

		while (start_it != end_it &&
			(std::isspace((unsigned char)*start_it) || *start_it == '\r'))
			++start_it;

		while (end_it != start_it &&
			std::isspace((unsigned char)*(end_it - 1)))
			--end_it;

		return std::string(start_it, end_it);
    }
}
