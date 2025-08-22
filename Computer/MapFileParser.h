#pragma once

#include <string>
#include <vector>
#include <cstdint>

struct SegmentInfo {
    std::string name;
    uint16_t start;
    uint16_t end;
    size_t size;
    
    SegmentInfo() : start(0), end(0), size(0) {}
};

class MapFileParser {
public:
    std::vector<SegmentInfo> parseMapFile(const std::string& mapFile);
    
    // Helper function to find a specific segment by name
    SegmentInfo* findSegment(std::vector<SegmentInfo>& segments, const std::string& name);

private:
    SegmentInfo parseSegmentLine(const std::string& line);
};