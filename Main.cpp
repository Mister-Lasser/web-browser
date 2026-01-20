#define BOOST_ASIO_USE_OPENSSL

#include <iostream>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <string>
#include <string_view>
#include <cassert>
#include <array>
#include <exception>
#include <map>
#include <cctype>
#include <algorithm>
#include <boost/asio/ssl/stream_base.hpp>
#include <openssl/ssl.h>
#include <windows.h>
#include <shellapi.h>


namespace Helper
{
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

	void parse_headers(auto& map, const std::string& content)
	{
		if (content.empty() || content == "\r")
			return;

		std::size_t pos{ content.find(":") };
		if (pos == std::string::npos)
			return;
	
		std::string header{ content.substr(0, pos) }, value{ content.substr(pos + 1) };
		std::transform(
			header.begin(), header.end()
			, header.begin()
			, [](unsigned char c) { return std::tolower(c); }
		);
		map[header] = Helper::strip(value);
	}

	std::optional<std::size_t> getContentLength(
		const std::map<std::string, std::string>& headers)
	{
		if (auto it = headers.find("content-length"); it != headers.end())
		{
			return static_cast<std::size_t>(std::stoull(it->second));
		}
		return std::nullopt;
	}


	//factoring out request/response logic
	template <typename Stream>
	std::string read_response(Stream& stream, auto& responseHeaders)
	{
		std::array<char, 1024> buf;
		std::string recvBuf, content;
		bool headersParsed{ false };

		std::optional<std::size_t> contentLength;
		std::size_t bytesRead = 0;

		while (true)
		{
			boost::system::error_code ec;
			std::size_t len = stream.read_some(boost::asio::buffer(buf), ec);

			if (ec == boost::asio::error::eof)
				break;
			if (ec)
				throw boost::system::system_error(ec);

			recvBuf.append(buf.data(), len);

			if (!headersParsed)
			{
				auto pos = recvBuf.find("\r\n\r\n");
				if (pos != std::string::npos)
				{
					std::string headers{  };

					std::istringstream iss{ recvBuf.substr(0, pos) };
					std::string line;

					//status line
					if (std::getline(iss, line))
					{
						if (line.ends_with('\r'))
							line.pop_back();

						if (line.rfind("HTTP/", 0) == 0)
							responseHeaders["status"] = Helper::strip(line);
					}

					//status check
					int status{};
					if (auto it{ responseHeaders.find("status") }; it != responseHeaders.end())
					{
						std::istringstream issm{ it->second };
						std::string http{};
						issm >> http >> status;

						if (status / 100 != 2)
						{
							throw std::runtime_error(
								"HTTP request failed: " + it->second
							);
						}
					}

					//headers
					while (std::getline(iss, line))
					{
						if (line.ends_with('\r'))
							line.pop_back();
						parse_headers(responseHeaders, line);
					}

					recvBuf.erase(0, pos + 4);
					headersParsed = true;
					contentLength = getContentLength(responseHeaders);
					content.append(recvBuf);
					recvBuf.clear();
				}
			}
			else
			{
				if (contentLength)
				{
					std::size_t toCopy = std::min(
						recvBuf.size(),
						*contentLength - bytesRead
					);

					content.append(recvBuf.data(), toCopy);
					bytesRead += toCopy;
					recvBuf.erase(0, toCopy);

					if (bytesRead >= *contentLength)
						break;
				}
				else
				{
					content.append(recvBuf);
					recvBuf.clear();
				}
			}
		}
		return content;
	}

	void formRequest(std::string& request)
	{
		request = std::format(
			"GET {} HTTP/1.0\r\n"
			"Host: {}\r\n"
			"Accept-Encoding: identity\r\n"
			"Connection: close\r\n"
			"User-Agent: some\r\n"
			"\r\n",
			m_path, m_host
		);
	}
	


	std::optional<std::string> request()
	{
		namespace SSL = boost::asio::ssl;
		std::map<std::string, std::string> responseHeaders{};
		std::string content{};

		boost::asio::io_context ioc;
		TCP::resolver resolver{ ioc };

		std::string_view port{ (m_scheme == "https") ? "443" : "80" };
		TCP::resolver::results_type endpoints{ resolver.resolve(m_host, port) };
		TCP::socket socket{ ioc };

		std::string request{};
		formRequest(request);

		if (m_scheme == "http")
		{
			boost::asio::connect(socket, endpoints);
			boost::asio::write(socket, boost::asio::buffer(request));
			content = read_response(socket, responseHeaders);
		}
		else
		{
			SSL::context ctx{ SSL::context::tls_client };

			//load CA bundle
			//ctx.set_default_verify_paths();
			try
			{
				ctx.load_verify_file("C:/Program Files/OpenSSL-Win64/cert/cacert.pem");
			}
			catch (std::exception& e)
			{
				std::cerr << "Error is: " << e.what() << '\n';
				return std::nullopt;
			}


			SSL::stream<TCP::socket> stream{ ioc, ctx };

			stream.set_verify_mode(SSL::verify_peer);
			stream.set_verify_callback(
				SSL::host_name_verification(std::string(m_host))
			);

			boost::asio::connect(stream.next_layer(), endpoints);

			if (!SSL_set_tlsext_host_name(stream.native_handle(), std::string(m_host).c_str()))
			{
				throw boost::system::system_error(
					static_cast<int>(::ERR_get_error()),
					boost::asio::error::get_ssl_category()
				);
			}

			boost::system::error_code ec;
			stream.handshake(SSL::stream_base::client, ec);
			if (ec)
			{
				std::cerr << "TLS handshake failed: " << ec.message() << "\n";
			}


			boost::asio::write(stream, boost::asio::buffer(request));
			content = read_response(stream, responseHeaders);

			boost::system::error_code errC;
			stream.shutdown(errC);  // ignore eof

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

void load(URL& url)
{
	auto body{ url.request() };
	if (body)
		show(*body);
}

int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		std::cout << "Usage: web-browser <url>\n";
		return 0;
	}

	URL url{ argv[1] };
	
	load(url);

	return 0;
}