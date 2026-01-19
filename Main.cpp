#include <iostream>
#include <boost/asio.hpp>
#include <string>
#include <string_view>
#include <cassert>
#include <array>
#include <exception>
#include <fstream>
#include <map>
#include <cctype>
#include <algorithm>

namespace Helper
{
	std::string strip(const std::string& input)
	{
		auto start_it{ input.begin() };
		auto end_it{ input.rbegin() };

		while (std::isspace(static_cast<unsigned char>(*start_it))) 
			++start_it;

		if (start_it != input.end())
		{
			while (std::isspace(*end_it)) ++end_it;
		}

		return std::string(start_it, end_it.base());
	}
}

class URL
{
using TCP = boost::asio::ip::tcp;

private:
	std::string m_url{};	//class will own the url
	std::string_view m_host{}, m_path{}, m_scheme{};

public:

	URL(std::string url)
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

	void parse_headers(std::map<std::string, std::string>& map, const std::string& content)
	{
		std::size_t pos{ content.find(":") };
	
		std::string header{ content.substr(0, pos) }, value{ content.substr(pos + 1) };
		std::transform(
			header.begin(), header.end()
			, header.begin()
			, [](unsigned char c) { return std::tolower(c); }
		);
		map.emplace(
			 header
			,Helper::strip(value)
		);
	}

	std::optional<std::string> request()
	{
		boost::asio::io_context ioc;
		TCP::resolver resolver{ ioc };
		TCP::resolver::results_type endpoints{ resolver.resolve(m_host, "80") };
		TCP::socket socket{ ioc };

		boost::asio::connect(socket, endpoints);

		std::string request{ std::format("GET {} HTTP/1.0\r\n", m_path) };
		request.append(std::format("Host: {}\r\n", m_host));
		request.append("\r\n");

		boost::asio::write(socket, boost::asio::buffer(request));

		std::array<char, 1024> buf;
		std::string recvBuf;
		bool headersParsed{};
		std::map<std::string, std::string> responseHeaders{};
		std::string content{};
		try
		{
			while (true)
			{
				boost::system::error_code error;

				std::size_t len = socket.read_some(boost::asio::buffer(buf), error);

				if (error == boost::asio::error::eof)
					break;
				else if (error)
					throw boost::system::system_error(error);

				recvBuf.append(buf.data(), len);

				if (!headersParsed)
				{
					std::size_t pos{ recvBuf.find("\r\n\r\n") };
					if (pos != std::string::npos)
					{
						std::string headers{ recvBuf.substr(0, pos) };

						std::istringstream iss{ headers };
						std::string line;

						std::getline(iss, line);

						while (std::getline(iss, line))
						{
							if (line.ends_with('\r'))
								line.pop_back();
							parse_headers(responseHeaders, line);
						}

						recvBuf.erase(0, pos + 4);
						headersParsed = true;

						content.append(recvBuf);
						recvBuf.clear();
					}
				}
				else
				{
					content.append(recvBuf);
					recvBuf.clear();
				}
			}
		}
		catch (std::exception& e)
		{
			std::cerr << "Exception in thread: " << e.what() << '\n';
		}

		assert(!responseHeaders.contains("transfer-encoding"));
		assert(!responseHeaders.contains("content-encoding"));

		responseHeaders.emplace("content", content);

		return responseHeaders["content"];
	}

	friend void show(std::string&);

};

void show(std::string& body)
{
	bool inTag{};
	for (char ch : body)
	{
		if (ch == '<')
			inTag = true;
		else if (ch == '>')
			inTag = false;
		else if (!inTag)
			std::cout << ch;
	}

	std::cout << std::endl;
}

int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		std::cout << "Usage: web-browser <url>\n";
		return 0;
	}

	URL url{ argv[1] };
	
	auto body{ url.request() };
	if (body)
		show(*body);
	//std::cout << *url.request() << '\n';
}