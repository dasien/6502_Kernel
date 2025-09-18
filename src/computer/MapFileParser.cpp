#include "MapFileParser.h"
#include <fstream>
#include <sstream>
#include <iostream>

std::vector<SegmentInfo> MapFileParser::parseMapFile(const std::string &mapFile)
{
    std::vector<SegmentInfo> segments;
    std::ifstream file(mapFile);
    std::string line;
    bool inSegmentSection = false;

    if (!file.is_open())
    {
        std::cerr << "Error: Could not open map file: " << mapFile << std::endl;
        return segments;
    }

    while (std::getline(file, line))
    {
        // Look for segment list section
        if (line.find("Segment list:") != std::string::npos)
        {
            inSegmentSection = true;
            std::getline(file, line); // Skip "-------------"
            std::getline(file, line); // Skip header
            std::getline(file, line); // Skip "----------------------------------------------------"
            continue;
        }

        // Parse segment lines
        if (inSegmentSection && !line.empty() && line[0] != '-')
        {
            auto segment = parseSegmentLine(line);
            if (!segment.name.empty())
            {
                segments.push_back(segment);
                std::cout << "Parsed segment: " << segment.name
                        << " at $" << std::hex << segment.start
                        << "-$" << segment.end
                        << " (size: 0x" << segment.size << ")" << std::dec << std::endl;
            }
        }

        // Stop at empty line (end of segment section)
        if (inSegmentSection && line.empty())
        {
            break;
        }
    }

    return segments;
}

SegmentInfo *MapFileParser::findSegment(std::vector<SegmentInfo> &segments, const std::string &name)
{
    for (auto &segment: segments)
    {
        if (segment.name == name)
        {
            return &segment;
        }
    }
    return nullptr;
}

SegmentInfo MapFileParser::parseSegmentLine(const std::string &line)
{
    SegmentInfo segment;
    std::istringstream iss(line);
    std::string startStr, endStr, sizeStr, alignStr;

    // Parse: "CODE                  00F000  00FAB7  000AB8  00001"
    if (iss >> segment.name >> startStr >> endStr >> sizeStr >> alignStr)
    {
        try
        {
            segment.start = static_cast<uint16_t>(std::stoul(startStr, nullptr, 16));
            segment.end = static_cast<uint16_t>(std::stoul(endStr, nullptr, 16));
            segment.size = std::stoul(sizeStr, nullptr, 16);
        } catch (const std::exception &e)
        {
            std::cerr << "Error parsing segment line: " << line << std::endl;
            segment.name.clear(); // Mark as invalid
        }
    }

    return segment;
}
