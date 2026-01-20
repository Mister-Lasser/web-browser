#include "url.h"
#include "HttpClient.h"

#include <iostream>
#include <string>


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
	URL url{ argv[1] };
	HttpClient client;

	auto body = client.get(url);

	if (body)
		show(*body);
}