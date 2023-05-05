#include <dirent.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <iomanip>
#include <list>
#include <mutex>
#include <thread>

#include <fmt/ostream.h>

#include <pybind11/embed.h>

#include <url.hpp>

#include "utuputki/Config.h"
#include "utuputki/Downloader.h"
#include "utuputki/Logger.h"
#include "utuputki/Media.h"
#include "utuputki/Utuputki.h"


namespace py = pybind11;


class PythonLogger {
	PythonLogger(const PythonLogger &)            = delete;
	PythonLogger &operator=(const PythonLogger &) = delete;

	PythonLogger(PythonLogger &&)                 = delete;
	PythonLogger &operator=(PythonLogger &&)      = delete;

public:
	PythonLogger()  = default;

	~PythonLogger() = default;

	void debug(const std::string &message);

	void error(const std::string &message);

	void warning(const std::string &message);
};


void PythonLogger::debug(const std::string &message) {
	LOG_DEBUG(message);
}


void PythonLogger::error(const std::string &message) {
	LOG_ERROR(message);
}


void PythonLogger::warning(const std::string &message) {
	LOG_WARNING(message);
}


PYBIND11_EMBEDDED_MODULE(utuputki_dl, m) {
	pybind11::class_<PythonLogger>(m, "Logger")
	    .def(py::init<>())
	    .def("debug",   &PythonLogger::debug)
	    .def("error",   &PythonLogger::error)
	    .def("warning", &PythonLogger::warning);
};


namespace utuputki {


struct Downloader::DownloaderImpl {
	Utuputki                         &utuputki;

	unsigned int                     maxLength;
	unsigned int                     maxFileSize;
	unsigned int                     maxWidth;
	unsigned int                     maxHeight;
	unsigned int                     maxFPS;
	unsigned int                     maxAudioBitrate;
	unsigned int                     maxVideoBitrate;

	std::string                      format;

	std::string                      cacheDirectory;
	std::string                      tempDirectory;

	Duration                         maxMetadataAge;

	bool                             verbose;

	py::scoped_interpreter           interpreter;
	pybind11::module                 jsonModule;
	pybind11::module                 utuputkiModule;
	pybind11::module                 youtubeDLModule;
	// after python is initialized we hold the GIL
	// release it so threads can acquire it
	// use scoped helper to make sure destructor happens automagically
	py::gil_scoped_release           releaseGIL;

	std::unordered_set<std::string>  hostWhitelist;

	std::mutex                       metadataMutex;
	std::condition_variable          metadataCV;
	bool                             shutdownMetadata;
	std::list<MediaInfoId>           metadataQueue;
	std::thread                      metadataThread;

	std::mutex                       downloaderMutex;
	std::condition_variable          downloaderCV;
	bool                             shutdownDownloader;
	std::list<MediaInfoId>           downloaderQueue;
	std::thread                      downloaderThread;

	bool                             threadsStarted;


	DownloaderImpl()                                       = delete;

	DownloaderImpl(const DownloaderImpl &other)            = delete;
	DownloaderImpl &operator=(const DownloaderImpl &other) = delete;

	DownloaderImpl(DownloaderImpl &&other)                 = delete;
	DownloaderImpl &operator=(DownloaderImpl &&other)      = delete;

	DownloaderImpl(Utuputki &utuputki_, const Config &config);

	~DownloaderImpl();


	std::string checkDirectory(const std::string &dir, const std::string &type);

	void startThreads();

	void metadataThreadFunc();

	void downloaderThreadFunc();

	pybind11::dict createDownloaderOptions();

	tl::optional<MediaInfoId> popMetadataQueue();

	tl::optional<MediaInfoId> popDownloadQueue();

	template <typename T, typename F> T valueWithGIL(F &&f) {
		T retval;
		{
			py::gil_scoped_acquire acquire;
			retval = f();
		}
		return retval;
	}

	template <typename F> void withGIL(F &&f) {
		py::gil_scoped_acquire acquire;
		f();
	}

	MediaInfoId addMedia(const std::string &mediaURL);


	void metadataFromPython(MediaInfo &media, pybind11::object &downloader, const pybind11::dict &metadata) {
		media.url           = pybind11::cast<std::string>(metadata["webpage_url"]);
		media.filename      = pybind11::cast<std::string>(downloader.attr("prepare_filename")(metadata));
		media.title         = pybind11::cast<std::string>(metadata["title"]);
		media.length        = pybind11::cast<int        >(metadata["duration"]);
		media.metadata      = pybind11::cast<std::string>(jsonModule.attr("dumps")(metadata));
		media.metadataTime  = Timestamp::clock::now();
	}
};


std::string Downloader::DownloaderImpl::checkDirectory(const std::string &dir, const std::string &type) {
	assert(!dir.empty());
	assert(!type.empty());

	std::string ucase(type);
	ucase[0] = toupper(ucase[0]);

	struct stat statbuf;
	memset(&statbuf, 0, sizeof(statbuf));

	// check it exists
	int ret = stat(dir.c_str(), &statbuf);
	if (ret != 0) {
		LOG_ERROR("{} directory \"{}\" stat() failed: {}", ucase, dir, strerror(errno));
		throw std::system_error(errno, std::generic_category(), fmt::format("{} directory \"{}\" stat() failed", ucase, dir));
	}

	// it must be a directory
	if (!S_ISDIR(statbuf.st_mode)) {
		std::string err = fmt::format("{} directory \"{}\" is not a directory", ucase, dir);
		LOG_ERROR(err);
		throw std::runtime_error(err);
	}

	// check we have necessary permissions
	ret = access(dir.c_str(), R_OK | W_OK | X_OK);
	if (ret != 0) {
		std::string err = fmt::format("{} directory \"{}\" does not have necessary permissions", ucase, dir);
		LOG_ERROR(err);
		throw std::runtime_error(err);
	}

	// normalize with realpath
	char *result = realpath(dir.c_str(), nullptr);
	std::string newPath;
	if (result) {
		newPath = result;
		free(result);
		LOG_INFO("{} directory \"{}\" realpath: \"{}\"", ucase, dir, newPath);
	} else {
		std::string err = fmt::format("Failed to resolve {} directory \"{}\" to full path: {}", type, dir, strerror(errno));
		LOG_ERROR(err);
		throw std::system_error(errno, std::generic_category(), fmt::format("Failed to resolve {} directory \"{}\" to full path", type, dir));
	}

	return newPath;
}


Downloader::DownloaderImpl::DownloaderImpl(Utuputki &utuputki_, const Config &config)
: utuputki(utuputki_)
, maxLength(config.get("downloader",       "maxlength",       0))
, maxFileSize(config.get("downloader",     "maxfilesize",     0))
, maxWidth(config.get("downloader",        "maxwidth",        0))
, maxHeight(config.get("downloader",       "maxheight",       0))
, maxFPS(config.get("downloader",          "maxfps",          0))
, maxAudioBitrate(config.get("downloader", "maxaudiobitrate", 0))
, maxVideoBitrate(config.get("downloader", "maxvideobitrate", 0))
, cacheDirectory(config.get("downloader",  "cacheDir",        "cache"))
, tempDirectory(config.get("downloader",   "tempDir",         "/tmp"))
, maxMetadataAge(std::chrono::seconds(config.get("downloader", "maxmetadataage", 60)))
, verbose(config.getBool("downloader", "verbose", false))
, jsonModule(py::module::import("json"))
, utuputkiModule(py::module::import("utuputki_dl"))
, hostWhitelist({ "youtube.com", "www.youtube.com", "m.youtube.com", "youtu.be" })
, shutdownMetadata(false)
, shutdownDownloader(false)
, threadsStarted(false)
{
	std::string youtubeDlModuleName;

	withGIL([&] () {
		try {
			youtubeDLModule = py::module_::import("yt_dlp");
			LOG_INFO("Loaded yt-dlp");
			youtubeDlModuleName = "yt_dlp";
			return;
		} catch (py::error_already_set &e) {
			LOG_ERROR("Exception loading yt-dlp: {}", e.what());
		}

		try {
			youtubeDLModule = py::module_::import("youtube_dl");
			LOG_INFO("Loaded youtube-dl");
			youtubeDlModuleName = "youtube_dl";
			return;
		} catch (py::error_already_set &e) {
			LOG_ERROR("Exception loading youtube-dl: {}", e.what());
		}

		throw std::runtime_error("No yt-dlp or youtube-dl installed");
	});

	cacheDirectory = checkDirectory(cacheDirectory, "cache");
	tempDirectory  = checkDirectory(tempDirectory,  "temp");

	withGIL([&] () {
		try {
			LOG_INFO("youtube-dl version \"{}\"", pybind11::cast<std::string>(py::module::import((youtubeDlModuleName + ".version").c_str()).attr("__version__")));
		} catch (py::error_already_set &e) {
			LOG_WARNING("Couldn't get youtube-dl version: {}", e.what());
		}
	});

	LOG_INFO("Maximum length {}",        maxLength);
	LOG_INFO("Maximum file size {}",     maxFileSize);
	LOG_INFO("Maximum width {}",         maxWidth);
	LOG_INFO("Maximum height {}",        maxHeight);
	LOG_INFO("Maximum FPS {}",           maxFPS);
	LOG_INFO("Maximum audio bitrate {}", maxAudioBitrate);
	LOG_INFO("Maximum video bitrate {}", maxVideoBitrate);

	// build youtube_dl format selector string
	format = "bestvideo";

	std::string extWhitelist = config.get("downloader", "extensionWhitelist", "");
	if (!extWhitelist.empty()) {
		format += fmt::format("[ext={}]", extWhitelist);
	}

	std::string vcodec = config.get("downloader", "vcodec", "");
	if (!vcodec.empty()) {
		format += fmt::format("[vcodec={}]", vcodec);
	}

	if (maxFileSize != 0) {
		format += fmt::format("[filesize < {}]", maxFileSize);
	}

	if (maxWidth != 0) {
		format += fmt::format("[width <=? {}]", maxWidth);
	}

	if (maxHeight != 0) {
		format += fmt::format("[height <=? {}]", maxHeight);
	}

	if (maxFPS != 0) {
		format += fmt::format("[fps <=? {}]", maxFPS);
	}

	if (maxVideoBitrate != 0) {
		format += fmt::format("[vbr <=? {}]", maxVideoBitrate);
	}

	format += "+bestaudio";

	if (!extWhitelist.empty()) {
		format += fmt::format("[ext={}]", extWhitelist);
	}

	if (maxFileSize != 0) {
		format += fmt::format("[filesize < {}]", maxFileSize);
	}

	if (maxAudioBitrate != 0) {
		format += fmt::format("[abr <=? {}]", maxAudioBitrate);
	}

	format += "/best";

	LOG_DEBUG("youtube_dl format selector: \"{}\"", format);
}


Downloader::DownloaderImpl::~DownloaderImpl() {
	assert(threadsStarted);

	{
		std::unique_lock<std::mutex> lock(metadataMutex);
		shutdownMetadata = true;
		metadataCV.notify_one();
	}

	{
		std::unique_lock<std::mutex> lock(downloaderMutex);
		shutdownDownloader = true;
		downloaderCV.notify_one();
	}

	metadataThread.join();
	downloaderThread.join();
}


pybind11::dict Downloader::DownloaderImpl::createDownloaderOptions() {
	pybind11::dict downloaderOptions{};
	downloaderOptions["cachedir"]   = tempDirectory;
	downloaderOptions["format"]     = format;
	downloaderOptions["logger"]     = utuputkiModule.attr("Logger")();
	downloaderOptions["noplaylist"] = true;
	downloaderOptions["outtmpl"]    = "%(id)s.%(ext)s";
	downloaderOptions["verbose"]    = verbose;

	return downloaderOptions;
}


void Downloader::DownloaderImpl::downloaderThreadFunc() {
	while (true) {
		auto mediaOpt = popDownloadQueue();

		if (!mediaOpt) {
			break;
		}

		MediaInfoId &media = *mediaOpt;

		LOG_INFO("Downloading \"{}\" ({})", media.url, media.title);

		withGIL([&] () {
			try {
				// we can't keep the downloader object around outside the GIL region
				// it's destructor must be called with it held

				pybind11::dict options    = createDownloaderOptions();
				std::string finalFilename = cacheDirectory + "/" + media.filename;
				options["outtmpl"]        = finalFilename;

				auto downloader           = youtubeDLModule.attr("YoutubeDL")(options);
				auto metadata             = jsonModule.attr("loads")(media.metadata);
				auto now                  = Timestamp::clock::now();
				auto age                  = now - media.metadataTime;

				auto l = std::chrono::system_clock::to_time_t(media.metadataTime);
				LOG_DEBUG("metadata time: {}  age: {}  max: {}", std::put_time(std::localtime(&l), "%F %T"), age.count(), maxMetadataAge.count());
				if (age > maxMetadataAge) {
					LOG_INFO("Metadata for \"{}\" too old, redownload", media.url);
					metadata              = downloader.attr("extract_info")(media.url, false);

					metadataFromPython(media, downloader, metadata);
				}
				downloader.attr("process_video_result")(metadata);

				// youtube_dl sometimes lies about the file name, fix it
				int exists = access(finalFilename.c_str(), F_OK);
				if (exists == 0) {
					// success
					media.status = MediaStatus::Ready;
				} else {
					// try again with .mkv
					auto lastDot = media.filename.find_last_of('.');
					if (lastDot == std::string::npos) {
						// no extension, fail
						media.status       = MediaStatus::Failed;
						media.errorMessage = "File does not exist after download, filename has no extension";
					} else {
						std::string mkv = media.filename.substr(0, lastDot) + ".mkv";
						finalFilename   = cacheDirectory + "/" + mkv;
						LOG_DEBUG("recheck \"{}\"", mkv);
						exists = access(finalFilename.c_str(), F_OK);
						if (exists == 0) {
							LOG_INFO("Fixed \"{}\" extension to .mkv", media.filename);
							media.filename     = mkv;
							media.status       = MediaStatus::Ready;
						} else {
							media.status       = MediaStatus::Failed;
							media.errorMessage = "File does not exist after download, unable to fix filename";
						}
					}
				}

				if (media.status == MediaStatus::Failed) {
					LOG_ERROR("Failed to load {}: file does not exist after finishing", media.filename);
				}
			} catch (py::error_already_set &e) {
				LOG_ERROR("Caught python exception from downloader: {}", e.what());
				media.status = MediaStatus::Failed;
				media.errorMessage = e.what();
			} catch (std::exception &e) {
				LOG_ERROR("Caught std::exception from downloader: {}", e.what());
				media.status = MediaStatus::Failed;
				media.errorMessage = e.what();
			} catch (...) {
				LOG_ERROR("Caught unknown exception from downloader");
				media.status = MediaStatus::Failed;
				media.errorMessage = "Unknown exception from downloader";
			}
		} );

        try {
			utuputki.updateMediaInfo(media);
		} catch (std::exception &e) {
			LOG_ERROR("updateMediaInfo exception: \"{}\"", e.what());
		} catch (...) {
			LOG_ERROR("updateMediaInfo exception");
		}
	}
}


void Downloader::DownloaderImpl::metadataThreadFunc() {
	while (true) {
		auto mediaOpt = popMetadataQueue();

		if (!mediaOpt) {
			break;
		}

		MediaInfoId &media = *mediaOpt;

		LOG_DEBUG("Getting metadata for \"{}\"", media.url);
		withGIL([&] () {
			try {
				// we can't keep the downloader object around outside the GIL region
				// its destructor must be called with GIL held
				py::object downloader = youtubeDLModule.attr("YoutubeDL")(createDownloaderOptions());
				py::object result = downloader.attr("extract_info")(media.url, false);

				metadataFromPython(media, downloader, result);

				media.status        = MediaStatus::Downloading;
			} catch (std::exception &e) {
				media.status        = MediaStatus::Failed;
				media.errorMessage  = e.what();
			} catch (...) {
				media.status        = MediaStatus::Failed;
				media.errorMessage  = "Unknown exception from metadata downloader";
			}
		});

		if (media.length > maxLength) {
			LOG_INFO("Media {} \"{}\" length {} exceeds max length {}", media.url, media.title, media.length, maxLength);
			media.status       = MediaStatus::Failed;
			media.errorMessage = fmt::format("Too long ({} > {})", media.length, maxLength);
		}

		try {
			utuputki.updateMediaInfo(media);
		} catch (std::exception &e) {
			LOG_ERROR("updateMediaInfo exception: {}", e.what());
		} catch (...) {
			LOG_ERROR("updateMediaInfo unknown exception");
		}

		if (media.status == MediaStatus::Downloading) {
			std::unique_lock<std::mutex> lock(downloaderMutex);
			downloaderQueue.emplace_back(std::move(media));
			downloaderCV.notify_one();
		}
	}
}


tl::optional<MediaInfoId> Downloader::DownloaderImpl::popMetadataQueue() {
	std::unique_lock<std::mutex> lock(metadataMutex);

	while (true) {
		if (shutdownMetadata) {
			return tl::optional<MediaInfoId>();
		}

		if (!metadataQueue.empty()) {
			MediaInfoId retval = metadataQueue.front();
			metadataQueue.pop_front();

			return retval;
		}

		metadataCV.wait(lock);
	}
}


tl::optional<MediaInfoId> Downloader::DownloaderImpl::popDownloadQueue() {
	std::unique_lock<std::mutex> lock(downloaderMutex);

	while (true) {
		if (shutdownDownloader) {
			return tl::optional<MediaInfoId>();
		}

		if (!downloaderQueue.empty()) {
			MediaInfoId retval = downloaderQueue.front();
			downloaderQueue.pop_front();

			return retval;
		}

		downloaderCV.wait(lock);
	}
}


Downloader::Downloader(Utuputki &utuputki, const Config &config)
: impl(new DownloaderImpl(utuputki, config))
{

}


Downloader::~Downloader() {
}


MediaInfoId Downloader::addMedia(const std::string &mediaURL) {
	assert(impl);

	return impl->addMedia(mediaURL);
}


MediaInfoId Downloader::DownloaderImpl::addMedia(const std::string &mediaURL) {
	assert(!mediaURL.empty());

	LOG_INFO("addMedia \"{}\"", mediaURL);

	Url parsedURL(mediaURL);

	LOG_DEBUG("scheme: \"{}\"", parsedURL.scheme());
	LOG_DEBUG("host: \"{}\"",   parsedURL.host());
	LOG_DEBUG("path: \"{}\"",   parsedURL.path());

	// normalize protocol to https
	parsedURL.scheme("https");

	if (hostWhitelist.find(parsedURL.host()) == hostWhitelist.end()) {
		throw BadHostException(fmt::format("Host {} not whitelisted", parsedURL.host()));
	}

	std::string normalizedURL = parsedURL.str();
	auto media = utuputki.getOrAddMediaByURL(normalizedURL);
	switch (media.status) {
	case MediaStatus::Failed:
		// if state is errored, clear it and try again
		media.status = MediaStatus::Initial;
		utuputki.updateMediaInfo(media);

		// fallthrough

	case MediaStatus::Initial: {

		// new media, add to metadata queue
		std::unique_lock<std::mutex> lock(metadataMutex);

		bool wasEmpty = metadataQueue.empty();
		metadataQueue.push_back(media);
		if (wasEmpty) {
			metadataCV.notify_one();
		}
	}

	case MediaStatus::Downloading:
	case MediaStatus::Ready:
		break;
	}

	return media;
}


void Downloader::startThreads() {
	assert(impl);

	impl->startThreads();
}


void Downloader::DownloaderImpl::startThreads() {
	assert(!threadsStarted);

	// get initial list of metadata/download required media from db
	// wasteful to do it this way but eh
	// less methods we need to add to Utuputki and Database classes
	// the other threads are not started yet
	// so we can access the queues without locking
	for (auto &m : utuputki.getAllMedia()) {
		switch (m.status) {
		case MediaStatus::Initial:
			metadataQueue.emplace_back(std::move(m));
			break;

		case MediaStatus::Downloading:
			downloaderQueue.emplace_back(std::move(m));
			break;

		default:
			break;
		}
	}

	LOG_INFO("Initially need metadata for {} media", metadataQueue.size());
	LOG_INFO("Initially need to download {} media", downloaderQueue.size());

	metadataThread   = std::thread(std::bind(&DownloaderImpl::metadataThreadFunc,   this));
	downloaderThread = std::thread(std::bind(&DownloaderImpl::downloaderThreadFunc, this));

	threadsStarted = true;
}


std::string Downloader::getCacheDirectory() const {
	assert(impl);

	return impl->cacheDirectory;
}


}  // namespace utuputki

