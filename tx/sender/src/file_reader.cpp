#include "sender/file_reader.hpp"

#include <fstream>
#include <vector>

std::vector<uint8_t> read_file(const std::string& path){
    //read raw bytes exactly as they are on disk
    // receive cursor to the file.
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }

    //get size of the file by putting pointer at the end
    file.seekg(0, std::ios::end);
    ssize_t size = file.tellg();
    if(size < 0) {
        return {};
    }
    file.seekg(0, std::ios::beg);
    
    //allocate RAM and initialize to zero
    std::vector<uint8_t> buffer(size);
    // read the file into the buffer
    file.read(reinterpret_cast<char*>(buffer.data()), size);
    return buffer;
}