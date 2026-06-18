#include "lut_cube.hpp"

#include "logger.hpp"

#include <fstream>
#include <string>
#include <vector>
#include <cctype>
#include <algorithm> // Required for std::clamp

namespace vkBasalt
{
    // Initialize all primitive members to prevent undefined behavior
    LutCube::LutCube() 
        : size(0), 
          minX(0.0f), minY(0.0f), minZ(0.0f), 
          maxX(1.0f), maxY(1.0f), maxZ(1.0f),
          currentX(0), currentY(0), currentZ(0)
    {
    }

    LutCube::LutCube(const std::string& file) 
        : size(0), 
          minX(0.0f), minY(0.0f), minZ(0.0f), 
          maxX(1.0f), maxY(1.0f), maxZ(1.0f),
          currentX(0), currentY(0), currentZ(0)
    {
        std::ifstream cubeStream(file);
        if (!cubeStream.good())
        {
            Logger::err("lut cube file does not exist");
            return;
        }

        std::string line;

        while (std::getline(cubeStream, line))
        {
            parseLine(line);
        }
    }

    void LutCube::parseLine(std::string line)
    {
        if (line.length() == 0) return;
        if (line[0] == '#') return;

        if (line.find("LUT_3D_SIZE") != std::string::npos)
        {
            line = line.substr(line.find("LUT_3D_SIZE") + 11);
            line = skipWhiteSpace(line);
            size = std::stoi(line);

            colorCube = std::vector<unsigned char>(size * size * size * 4, 255);
            return;
        }
        if (line.find("DOMAIN_MIN") != std::string::npos)
        {
            line = line.substr(line.find("DOMAIN_MIN") + 10);
            splitTripel(line, minX, minY, minZ);
            return;
        }
        if (line.find("DOMAIN_MAX") != std::string::npos)
        {
            line = line.substr(line.find("DOMAIN_MAX") + 10);
            splitTripel(line, maxX, maxY, maxZ);
            return;
        }
        
        // Fixed: Recognize negative numbers, positive signs, and decimals
        if (!line.empty() && (std::isdigit(static_cast<unsigned char>(line[0])) || line[0] == '-' || line[0] == '+' || line[0] == '.'))
        {
            float         x, y, z;
            unsigned char outX, outY, outZ;
            splitTripel(line, x, y, z);
            clampTripel(x, y, z, outX, outY, outZ);
            writeColor(currentX, currentY, currentZ, outX, outY, outZ);
            
            if (currentX != size - 1)
            {
                currentX++;
            }
            else if (currentY != size - 1)
            {
                currentY++;
                currentX = 0;
            }
            else if (currentZ != size - 1)
            {
                currentZ++;
                currentX = 0;
                currentY = 0;
            }
            return;
        }
    }

    std::string LutCube::skipWhiteSpace(std::string text)
    {
        while (text.size() > 0 && (text[0] == ' ' || text[0] == '\t'))
        {
            text = text.substr(1);
        }
        return text;
    }

    // Fixed: Use std::stof's native 'pos' argument to completely eliminate std::string::npos crashes
    void LutCube::splitTripel(std::string tripel, float& x, float& y, float& z)
    {
        size_t pos = 0;
        
        tripel = skipWhiteSpace(tripel);
        x = std::stof(tripel, &pos);
        tripel = tripel.substr(pos);

        tripel = skipWhiteSpace(tripel);
        y = std::stof(tripel, &pos);
        tripel = tripel.substr(pos);

        tripel = skipWhiteSpace(tripel);
        z = std::stof(tripel, &pos);
    }

    // Fixed: Prevent divide-by-zero and use proper C++ casting and clamping
    void LutCube::clampTripel(float x, float y, float z, unsigned char& outX, unsigned char& outY, unsigned char& outZ)
    {
        float rangeX = maxX - minX;
        float rangeY = maxY - minY;
        float rangeZ = maxZ - minZ;

        // Prevent divide by zero if min == max
        if (rangeX == 0.0f) rangeX = 1.0f;
        if (rangeY == 0.0f) rangeY = 1.0f;
        if (rangeZ == 0.0f) rangeZ = 1.0f;

        outX = static_cast<unsigned char>(std::clamp(255.0f * (x / rangeX), 0.0f, 255.0f));
        outY = static_cast<unsigned char>(std::clamp(255.0f * (y / rangeY), 0.0f, 255.0f));
        outZ = static_cast<unsigned char>(std::clamp(255.0f * (z / rangeZ), 0.0f, 255.0f));
    }

    // Fixed: Add bounds checking to prevent heap buffer overflow on malformed files
    void LutCube::writeColor(int x, int y, int z, unsigned char r, unsigned char g, unsigned char b)
    {
        static const int colorSize = 4; // 4 bytes per point in the cube, rgba
        int locationR = (((z * size) + y) * size + x) * colorSize;

        if (locationR + 2 < static_cast<int>(colorCube.size()))
        {
            colorCube[locationR + 0] = r;
            colorCube[locationR + 1] = g;
            colorCube[locationR + 2] = b;
        }
    }
} // namespace vkBasalt
