#ifndef UTILS_H
#define UTILS_H


#include <string>
#include <vector>


std::vector<char> readFile(std::string filename);
void writeFile(const std::string &filename, const void *contents, size_t size);


#endif  // UTILS_H
