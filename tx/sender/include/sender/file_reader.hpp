#pragma once 

#include <vector>
#include <cstdint>
#include <string>

/**
 * Reads a file entire content into memory
 * 
 * @param path Filesystem path to the file to read
 * @return All bytes in the file, or empty on failure
 */
std::vector<uint8_t> read_file(const std::string& file_path);

