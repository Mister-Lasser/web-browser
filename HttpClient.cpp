#include "HttpClient.h"
#include "Url.h"
#include "Helper.h"

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
#include <boost/asio/ssl/stream_base.hpp>
#include <openssl/ssl.h>

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

void formRequest(std::string& request, const URL& url)
{
	request = std::format(
		"GET {} HTTP/1.0\r\n"
		"Host: {}\r\n"
		"Accept-Encoding: gzip\r\n"
		"Connection: close\r\n"
		"User-Agent: zachero\r\n"
		"\r\n",
		url.path(), url.host()
	);
}

using TCP = boost::asio::ip::tcp;

std::optional<std::string> HttpClient::get(const URL& url)
{
	namespace SSL = boost::asio::ssl;
	std::map<std::string, std::string> responseHeaders{};
	std::string content{};

	boost::asio::io_context ioc;
	TCP::resolver resolver{ ioc };

	std::string_view port{ (url.scheme() == "https") ? "443" : "80"};
	TCP::resolver::results_type endpoints{ resolver.resolve(url.host(), port)};
	TCP::socket socket{ ioc };

	std::string request{};
	formRequest(request, url);

	if (url.scheme() == "http")
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
			SSL::host_name_verification(std::string(url.host()))
		);

		boost::asio::connect(stream.next_layer(), endpoints);

		if (!SSL_set_tlsext_host_name(stream.native_handle(), std::string(url.host()).c_str()))
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

		//boost::system::error_code errC;
		//stream.shutdown(errC); ignore eof

	}

	assert(!responseHeaders.contains("transfer-encoding"));
	assert(!responseHeaders.contains("content-encoding"));

	responseHeaders.emplace("content", content);

	return responseHeaders["content"];
}
