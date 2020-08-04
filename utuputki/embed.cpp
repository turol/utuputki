#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <sys/stat.h>

#include <memory>
#include <string>
#include <sstream>
#include <vector>

#include <fmt/format.h>


struct FILEDeleter {
	void operator()(FILE *f) { fclose(f); }
};


std::vector<char> readFile(std::string filename) {
	std::unique_ptr<FILE, FILEDeleter> file(fopen(filename.c_str(), "rb"));

	if (!file) {
		// TODO: better exception
		throw std::runtime_error("file not found " + filename);
	}

	int fd = fileno(file.get());
	if (fd < 0) {
		// TODO: better exception
		throw std::runtime_error("no fd");
	}

	struct stat statbuf;
	memset(&statbuf, 0, sizeof(struct stat));
	int retval = fstat(fd, &statbuf);
	if (retval < 0) {
		// TODO: better exception
		throw std::runtime_error("fstat failed");
	}

	unsigned int filesize = static_cast<unsigned int>(statbuf.st_size);
	std::vector<char> buf(filesize, '\0');

	size_t ret = fread(&buf[0], 1, filesize, file.get());
	if (ret != filesize)
	{
		// TODO: better exception
		throw std::runtime_error("fread failed");
	}

	return buf;
}


void writeFile(const std::string &filename, const void *contents, size_t size) {
	std::unique_ptr<FILE, FILEDeleter> file(fopen(filename.c_str(), "wb"));

	fwrite(contents, 1, size, file.get());
}


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

