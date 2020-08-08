#include <cstdio>
#include <cstdlib>

#include <sstream>

#include <fmt/format.h>

#include "utuputki/Utils.h"


int main(int argc, char *argv[]) {
	if (argc != 3) {
		printf("Usage: %s infile outfile\n", argv[0]);
		return 1;
	}

	try {
		std::string inFile(argv[1]);
		std::string outFile(argv[2]);

		auto contents = readFile(inFile);

		std::string f;
		auto lastSlash = inFile.find_last_of('/');
		if (lastSlash == std::string::npos) {
			f = inFile;
		} else {
			f = inFile.substr(lastSlash + 1);
		}

		std::string identifier;
		for (char c : f) {
			if (isalnum(static_cast<unsigned char>(c))) {
				identifier += c;
			} else {
				identifier += "_";
			}
		}

		std::stringstream s;

		s << fmt::format("static const unsigned char {}[] = \n\t\"", identifier);

		std::vector<char> outBuffer;
		outBuffer.reserve(contents.size() * 5);

		for (unsigned int i = 0; i < contents.size(); i++) {
			char c = contents[i];
			if (c >= 32 && c <= 126 && c != '"' && c != '\\' && c != '?' && c != ':' && c != '%') {
				outBuffer.push_back(contents[i]);
			} else if (c == '\n') {
				outBuffer.push_back('\\');
				outBuffer.push_back('n');
			} else if (c == '\r') {
				outBuffer.push_back('\\');
				outBuffer.push_back('r');
			} else if (c == '\t') {
				outBuffer.push_back('\\');
				outBuffer.push_back('t');
			} else if (c == '"') {
				outBuffer.push_back('\\');
				outBuffer.push_back('"');
			} else if (c == '\\') {
				outBuffer.push_back('\\');
				outBuffer.push_back('\\');
			} else {
				outBuffer.push_back('\\');
				outBuffer.push_back('0' + ((c & 0700) >> 6));
				outBuffer.push_back('0' + ((c & 0070) >> 3));
				outBuffer.push_back('0' + ((c & 0007) >> 0));
			}
		}

		outBuffer.push_back('\0');
		s << outBuffer.data();

		s << "\";\n";

		s << fmt::format("static const size_t {}_length = {};\n", identifier, contents.size());

		std::string str = s.str();

		writeFile(outFile, str.c_str(), str.size());

		return 0;
	} catch (std::exception &s) {
		printf("std::exception \"%s\"\n", s.what());
		return 1;
	} catch (...) {
		printf("unknown exception\n");
		return 1;
	}
}

