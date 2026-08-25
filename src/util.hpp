#ifndef UTIL_HPP_INCLUDED
#define UTIL_HPP_INCLUDED

#include <string>
#include <sstream>
#include <vector>

namespace vkBasalt
{
    void addUniqueCString(std::vector<const char*>& stringVector, const char* addString);

    enum class Color
    {
        defaultColor,

        black,
        red,
        green,
        yellow,
        blue,
        magenta,
        cyan,
        white
    };

    void outputInColor(const std::string& output, Color foreground = Color::white, Color background = Color::black);

    template<typename T>
    std::string convertToString(T object)
    {
        std::stringstream ss;
        ss << object;
        return ss.str();
    }
} // namespace vkBasalt

#endif // UTIL_HPP_INCLUDED
