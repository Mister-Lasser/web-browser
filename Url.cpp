#include "Url.h"
#include <cassert>

std::string_view URL::host() const { return m_host; }
std::string_view URL::path() const { return m_path; }
std::string_view URL::scheme() const { return m_scheme; }

URL::URL(std::string url)
	: m_url{ std::move(url) }
{
	std::string_view sv{ m_url };

	std::size_t scheme_end{ sv.find("://") };

	m_scheme = sv.substr(0, scheme_end);
	assert(m_scheme == "http" || m_scheme == "https");

	sv.remove_prefix(scheme_end + 3);	//remove the scheme and the '://'

	//if the url only has host and no path, add a slash at the end
	if (!sv.contains('/'))
	{
		m_url.push_back('/');
		sv = m_url;
		sv.remove_prefix(scheme_end + 3);
	}

	std::size_t slash_pos{ sv.find('/') };

	m_host = sv.substr(0, slash_pos);
	m_path = sv.substr(slash_pos);
}