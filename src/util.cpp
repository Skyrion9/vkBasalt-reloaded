#include "util.hpp"

#include <iostream>
#include <unistd.h>
#include <cstring>
#include <array>

namespace vkBasalt
{
    void addUniqueCString(std::vector<const char*>& stringVector, const char* addString)
    {
        for (const char* other : stringVector)
        {
            if (strcmp(other, addString) == 0)
            {
                return;
            }
        }
        stringVector.push_back(addString);
    }

    void outputInColor(std::string output, Color foreground, Color background)
    {
        std::array<std::string, 2> magicNumbers;
        size_t magicCount = 0;
        switch (foreground)
        {
            case Color::black:   magicNumbers[magicCount++] = "30"; break;
            case Color::red:     magicNumbers[magicCount++] = "31"; break;
            case Color::green:   magicNumbers[magicCount++] = "32"; break;
            case Color::yellow:  magicNumbers[magicCount++] = "33"; break;
            case Color::blue:    magicNumbers[magicCount++] = "34"; break;
            case Color::magenta: magicNumbers[magicCount++] = "35"; break;
            case Color::cyan:    magicNumbers[magicCount++] = "36"; break;
            case Color::white:   magicNumbers[magicCount++] = "37"; break;
            default: break;
        }
        switch (background)
        {
            case Color::black:   magicNumbers[magicCount++] = "40"; break;
            case Color::red:     magicNumbers[magicCount++] = "41"; break;
            case Color::green:   magicNumbers[magicCount++] = "42"; break;
            case Color::yellow:  magicNumbers[magicCount++] = "43"; break;
            case Color::blue:    magicNumbers[magicCount++] = "44"; break;
            case Color::magenta: magicNumbers[magicCount++] = "45"; break;
            case Color::cyan:    magicNumbers[magicCount++] = "46"; break;
            case Color::white:   magicNumbers[magicCount++] = "47"; break;
            default: break;
        }
        std::string magicString = "";
        for (size_t i = 0; i < magicCount; i++)
        {
            if (i > 0) magicString += ";";
            magicString += magicNumbers[i];
        }
        if (magicString.size() == 0 || !isatty(fileno(stdout)))
        {
            std::cout << output << '\n';
        }
        else
        {
            std::cout << "\033[" << magicString << "m" << output << "\033[0m" << '\n';
        }
    }
} // namespace vkBasalt
