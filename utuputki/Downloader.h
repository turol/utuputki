#ifndef DOWNLOADER_H
#define DOWNLOADER_H


#include <memory>

#include "Media.h"


namespace utuputki {


class Config;
class Utuputki;


class Downloader {
	struct DownloaderImpl;
	std::unique_ptr<DownloaderImpl> impl;


	Downloader()                                   = delete;

	Downloader(const Downloader &other)            = delete;
	Downloader &operator=(const Downloader &other) = delete;

	Downloader(Downloader &&other)                 = delete;
	Downloader &operator=(Downloader &&other)      = delete;

public:

	Downloader(Utuputki &utuputki, const Config &config);

	~Downloader();

	void startThreads();

	MediaInfoId addMedia(const std::string &mediaURL);

	std::string getCacheDirectory() const;
};


}  // namespace utuputki


#endif  // DOWNLOADER_H
