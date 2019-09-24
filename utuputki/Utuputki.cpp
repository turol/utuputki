#include <algorithm>
#include <atomic>
#include <mutex>
#include <unordered_set>

#include <signal.h>
#include <sys/resource.h>

#include "utuputki/Config.h"
#include "utuputki/Database.h"
#include "utuputki/Downloader.h"
#include "utuputki/Logger.h"
#include "utuputki/Player.h"
#include "utuputki/Utuputki.h"
#include "utuputki/WebServer.h"


namespace utuputki {


struct Utuputki::UtuputkiImpl {
	static UtuputkiImpl *global;
	static void sigHUPHandler(int signo, siginfo_t *info, void *context);
	static void sigIntHandler(int signo, siginfo_t *info, void *context);


	Config                          config;
	Logger                          logger;
	Database                        database;
	Downloader                      downloader;
	WebServer                       webServer;
	Player                          player;

	struct sigaction                oldSigactionHUP;
	struct sigaction                oldSigactionInt;

	// c++17 TODO shared_mutex (rwlock)
	std::mutex                      nowPlayingMutex;
	tl::optional<HistoryItemMedia>  nowPlaying;
	std::unordered_set<std::string>  skips;

	std::atomic<unsigned int>        shutdownCounter;
	std::atomic<bool>                reExecFlag;


	explicit UtuputkiImpl(Utuputki &utuputki);

	UtuputkiImpl()                                     = delete;

	UtuputkiImpl(const UtuputkiImpl &other)            = delete;
	UtuputkiImpl &operator=(const UtuputkiImpl &other) = delete;

	UtuputkiImpl(UtuputkiImpl &&other)                 = delete;
	UtuputkiImpl &operator=(UtuputkiImpl &&other)      = delete;

	~UtuputkiImpl();


	void shutdown(bool immediate);

	void reExec(bool immediate);

	tl::optional<HistoryItemMedia> popNextPlaylistItem();

	void playlistItemFinished(HistoryItemMedia &item, HistoryStatus finishReason);

	tl::optional<HistoryItemMedia> getNowPlaying();

	std::vector<HistoryItemMedia> getHistory();

	unsigned int calculateNeededSkips();

	void skipVideo(const std::string &media, const std::string &client);
};


Utuputki::UtuputkiImpl::UtuputkiImpl(Utuputki &utuputki)
: config("utuputki.conf")
, logger(config)
, database(config)
, downloader(utuputki, config)
, webServer(utuputki, config)
, player(utuputki, config)
, shutdownCounter(0)
, reExecFlag(false)
{
	memset(&oldSigactionInt, 0, sizeof(oldSigactionInt));

	if (config.getBool("global", "setcoreulimit", true)) {
		LOG_INFO("set core ulimit");

		struct rlimit limit;
		memset(&limit, 0, sizeof(limit));

		int retval = getrlimit(RLIMIT_CORE, &limit);
		if (retval != 0) {
			LOG_ERROR("getrlimit failed: {} {}", errno, strerror(errno));
			throw std::runtime_error("getrlimit failed");
		}

		if (limit.rlim_max == 0) {
			LOG_ERROR("rlim_max is 0, can't set ulimit. Raise it or disable global.setcoreulimit");
			throw std::runtime_error("rlimit_max is 0");
		}

		limit.rlim_cur = limit.rlim_max;
		retval = setrlimit(RLIMIT_CORE, &limit);
		if (retval != 0) {
			LOG_ERROR("setrlimit failed: {} {}", errno, strerror(errno));
			throw std::runtime_error("setrlimit failed");
		}
	}
}


Utuputki::UtuputkiImpl::~UtuputkiImpl() {
}


void Utuputki::UtuputkiImpl::shutdown(bool immediate) {
	player.shutdown(immediate);
}


void Utuputki::UtuputkiImpl::reExec(bool immediate) {
	player.shutdown(immediate);

	reExecFlag = true;
}


tl::optional<HistoryItemMedia> Utuputki::UtuputkiImpl::popNextPlaylistItem() {
	auto item = database.popNextPlaylistItem();

	{
		std::unique_lock<std::mutex> lock(nowPlayingMutex);
		assert(!nowPlaying);
		nowPlaying = item;
		assert(skips.empty());
	}

	if (item) {
		LOG_INFO("Starting playback of \"{}\" ({} id {})", item->title, item->url, item->media.toString());
		webServer.notifyNowPlaying(*item);
	}

	return item;
}


void Utuputki::UtuputkiImpl::playlistItemFinished(HistoryItemMedia &item, HistoryStatus finishReason) {
	unsigned int numSkips = 0;

	{
		std::unique_lock<std::mutex> lock(nowPlayingMutex);
		numSkips   = skips.size();
		nowPlaying = tl::optional<HistoryItemMedia>();
		skips.clear();
	}

	item.skipCount   = numSkips;
	item.skipsNeeded = calculateNeededSkips();
	item.historyStatus = finishReason;

	LOG_INFO("\"{}\" ({} id {}) finished playing", item.title, item.url, item.media.toString());

	database.playlistItemFinished(item);

	webServer.notifyPlaylistItemFinished(item);
}


tl::optional<HistoryItemMedia> Utuputki::UtuputkiImpl::getNowPlaying() {
	tl::optional<HistoryItemMedia> result;

	{
		std::unique_lock<std::mutex> lock(nowPlayingMutex);
		result = nowPlaying;

		if (result) {
            assert(result->skipCount == skips.size());
		}
	}

	if (result) {
		result->skipsNeeded = calculateNeededSkips();
	}

	return result;
}


unsigned int Utuputki::UtuputkiImpl::calculateNeededSkips() {
	unsigned int numClients = webServer.getNumActiveClients();

	unsigned int neededSkips = (numClients + 1) / 2;
	if (neededSkips < 1) {
		neededSkips = 1;
	}

	return neededSkips;
}


void Utuputki::UtuputkiImpl::skipVideo(const std::string &media, const std::string &client) {
	LOG_DEBUG("skipVideo {}  {}", media, client);

	unsigned int neededSkips = calculateNeededSkips();

	bool doSkip = false;

	{
		std::unique_lock<std::mutex> lock(nowPlayingMutex);

		if (!nowPlaying) {
			LOG_DEBUG("skipVideo with no video playing");

			return;
		}

		if (media != nowPlaying->media.toString()) {
			LOG_DEBUG("skipVideo mismatch: skip {} but now playing {}", media, nowPlaying->media.toString());
			return;
		}

		auto i = skips.insert(client);
		if (!i.second) {
			LOG_DEBUG("{} tried to skip {} but already skipped", client, media);
		}

		nowPlaying->skipCount   = skips.size();
		nowPlaying->skipsNeeded = neededSkips;
		if (nowPlaying->skipCount >= nowPlaying->skipsNeeded) {
			doSkip = true;
		}
	}

	if (doSkip) {
		player.skipCurrent();
	}
}


Utuputki::UtuputkiImpl *Utuputki::UtuputkiImpl::global = nullptr;


void Utuputki::UtuputkiImpl::sigIntHandler(int signo, siginfo_t *info, void *context) {
	// on first one queue shutdown
	// on second one shutdown immediately
	unsigned int oldValue = global->shutdownCounter++;

	global->shutdown(oldValue > 0);

	if (global->oldSigactionInt.sa_flags & SA_SIGINFO) {
		if (global->oldSigactionInt.sa_sigaction) {
			global->oldSigactionInt.sa_sigaction(signo, info, context);
		} else {
			global->oldSigactionInt.sa_handler(signo);
		}
	}
}


void Utuputki::UtuputkiImpl::sigHUPHandler(int signo, siginfo_t *info, void *context) {
	// on first one queue shutdown
	// on second one shutdown immediately
	unsigned int oldValue = global->shutdownCounter++;

	global->reExec(oldValue > 0);

	if (global->oldSigactionInt.sa_flags & SA_SIGINFO) {
		if (global->oldSigactionInt.sa_sigaction) {
			global->oldSigactionInt.sa_sigaction(signo, info, context);
		} else {
			global->oldSigactionInt.sa_handler(signo);
		}
	}
}



Utuputki::Utuputki()
: impl(new UtuputkiImpl(*this))
{
	assert(!UtuputkiImpl::global);
	UtuputkiImpl::global = impl.get();

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = &UtuputkiImpl::sigIntHandler;
	sa.sa_flags     = SA_SIGINFO;

	sigaction(SIGINT, &sa, &impl->oldSigactionInt);

	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = &UtuputkiImpl::sigHUPHandler;
	sa.sa_flags     = SA_SIGINFO;

	sigaction(SIGHUP, &sa, &impl->oldSigactionInt);
}


Utuputki::~Utuputki() {
	assert(UtuputkiImpl::global);

	sigaction(SIGINT, &impl->oldSigactionInt, nullptr);
	UtuputkiImpl::global = nullptr;
}


void Utuputki::run() {
	assert(impl);

	impl->webServer.startServer();
	impl->downloader.startThreads();
	impl->player.run();
}



bool Utuputki::shouldReExec() const {
	assert(impl);

	return impl->reExecFlag;
}


void Utuputki::addMedia(const std::string &mediaURL) {
	assert(impl);

	auto media = impl->downloader.addMedia(mediaURL);

		addToPlaylist(media.id);
}


MediaInfoId Utuputki::getOrAddMediaByURL(const std::string &url) {
	assert(impl);
	assert(!url.empty());

	return impl->database.getOrAddMediaByURL(url);
}


void Utuputki::addToPlaylist(MediaId media) {
	assert(impl);

	impl->webServer.notifyAddedToPlaylist(impl->database.getMediaInfo(media));

	return impl->database.addToPlaylist(media);
}


std::vector<PlaylistItemMedia> Utuputki::getPlaylist() {
	assert(impl);

	return impl->database.getPlaylist();
}


std::vector<MediaInfoId> Utuputki::getAllMedia(){
	assert(impl);

	return impl->database.getAllMedia();
}


void Utuputki::updateMediaInfo(MediaInfoId &media) {
	assert(impl);

	impl->database.updateMediaInfo(media);

	if (media.status == MediaStatus::Ready) {
		// notify player, if it's on standby it might decide to wake up now
		impl->player.notifyMediaUpdate();
	}
}


tl::optional<HistoryItemMedia> Utuputki::popNextPlaylistItem() {
	assert(impl);

	return impl->popNextPlaylistItem();
}


void Utuputki::playlistItemFinished(HistoryItemMedia &item, HistoryStatus finishReason) {
	assert(impl);

	return impl->playlistItemFinished(item, finishReason);
}


tl::optional<HistoryItemMedia> Utuputki::getNowPlaying() const {
	assert(impl);

	return impl->getNowPlaying();
}


std::vector<HistoryItemMedia> Utuputki::getHistory() {
	assert(impl);

	return impl->database.getHistory();
}


std::string Utuputki::getCacheDirectory() const {
	assert(impl);

	return impl->downloader.getCacheDirectory();
}


void Utuputki::skipVideo(const std::string &media, const std::string &client) {
	assert(impl);

	return impl->skipVideo(media, client);
}


}  // namespace utuputki
