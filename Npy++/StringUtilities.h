#pragma once

#include <iterator>
#include <sstream>
#include <string>
#include <string_view>

namespace utils
{
	template<class ContainerT>
	ContainerT Tokenize(const std::string& str)
	{
		ContainerT out;

		std::istringstream iss(str);
		std::copy(std::istream_iterator<std::string>(iss), std::istream_iterator<std::string>(), std::back_inserter(out));

		return out;
	}

	template<class ContainerT>
	ContainerT Tokenize(const std::string_view str, const char delim)
	{
		ContainerT out;

		size_t start = 0;
		while (start <= str.size())
		{
			const size_t pos = str.find(delim, start);
			if (pos == std::string_view::npos)
			{
				if (start < str.size())
					out.emplace_back(str.substr(start));
				break;
			}
			out.emplace_back(str.substr(start, pos - start));
			start = pos + 1;
		}

		return out;
	}
}	 // namespace utils
