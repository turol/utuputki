#include <atomic>
#include <condition_variable>
#include <thread>

#include <vlcpp/vlc.hpp>

#include "utuputki/Config.h"
#include "utuputki/Logger.h"
#include "utuputki/Player.h"
#include "utuputki/Utuputki.h"

#include "standby.png.h"


namespace utuputki {


struct MediaHelper {
	size_t offset;
};


static int mediaOpen(void * /* opaque */, void **datap, uint64_t *sizep) {
	MediaHelper *hlp = new MediaHelper;
	hlp->offset = 0;

	*datap = hlp;
	*sizep = standby_png_length;

	return 0;
}


static ssize_t mediaRead(void *opaque, unsigned char *buf, size_t len) {
	size_t offset = static_cast<MediaHelper *>(opaque)->offset;

	if (offset > standby_png_length) {
		return -1;
	}

	size_t bytes = std::min(len, size_t(standby_png_length) - offset);
	memcpy(buf, &standby_png[offset], bytes);

	static_cast<MediaHelper *>(opaque)->offset = offset + bytes;

	return bytes;
}


static int mediaSeek(void *opaque, uint64_t offset) {
	if (offset >= standby_png_length) {
		return -1;
	}

	static_cast<MediaHelper *>(opaque)->offset = offset;

	return 0;
}


static void mediaClose(void *opaque) {
	delete static_cast<MediaHelper *>(opaque);
}


struct Player::PlayerImpl {
	Utuputki                        &utuputki;
	bool                            fullscreen;
	bool                            normalizeVolume;
	std::string                     audioDevice;
	LogLevel                        vlcLogLevel;
	VLC::Instance                   instance;

	VLC::Media                      standby;

	std::atomic<bool>               shutdownFlag;

	std::mutex                      helpMutex;
	std::condition_variable         helpCV;
	bool                            onStandby;
	bool                            skipped;


	PlayerImpl()                                   = delete;

	PlayerImpl(const PlayerImpl &other)            = delete;
	PlayerImpl &operator=(const PlayerImpl &other) = delete;

	PlayerImpl(PlayerImpl &&other)                 = delete;
	PlayerImpl &operator=(PlayerImpl &&other)      = delete;

	PlayerImpl(Utuputki &utuputki_, const Config &config);

	~PlayerImpl();


	void logCallback(int level, const libvlc_log_t *context, std::string message);

	void videoFinishCallback() {
		LOG_DEBUG("videoFinishCallback");

		// standby image also send finish callbacks sometime
		// we must wake thread anyway to put it back

		std::unique_lock<std::mutex> lock(helpMutex);
		helpCV.notify_one();
	}

	void run();

	void skipCurrent();

	void notifyMediaUpdate();
};


Player::PlayerImpl::PlayerImpl(Utuputki &utuputki_, const Config &config)
: utuputki(utuputki_)
, fullscreen(config.getBool("player", "fullscreen", true))
, normalizeVolume(config.getBool("player", "normalizeVolume", true))
, audioDevice(config.get("player", "audioDevice", ""))
, vlcLogLevel(parseLogLevel(config.get("player", "vlcLogLevel", "error")))
, instance(0, nullptr)
, standby(instance, mediaOpen, mediaRead, mediaSeek, mediaClose)
, shutdownFlag(false)
, onStandby(true)
, skipped(false)
{
	instance.logSet(std::bind(&PlayerImpl::logCallback, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

	LOG_INFO("Audio filters:");
	for (const auto &af : instance.audioFilterList()) {
		LOG_INFO(" {}", af.name());
		LOG_INFO("   {}", af.shortname());
		LOG_INFO("   {}", af.longname());
		LOG_INFO("   {}", af.help());
	}

	LOG_INFO("Video filters:");
	for (const auto &vf : instance.videoFilterList()) {
		LOG_INFO(" {}", vf.name());
		LOG_INFO("   {}", vf.shortname());
		LOG_INFO("   {}", vf.longname());
		LOG_INFO("   {}", vf.help());
	}

	LOG_INFO("Audio outputs:");
	for (const auto &ao : instance.audioOutputList()) {
		LOG_INFO(" {}", ao.name());
		LOG_INFO("   {}", ao.description());
		auto outputs = instance.audioOutputDeviceList(ao.name());
		if (!outputs.empty()) {
			LOG_INFO("   Outputs:");
			unsigned int count = 0;
			for (const auto &dev : outputs) {
				LOG_INFO("     {}: \"{}\"\t{}", count, dev.device(), dev.description());
				count++;
			}
		} else {
            LOG_INFO("   No outputs");
		}
	}

	LOG_INFO("Renderer discoverers:");
	for (const auto &r : instance.rendererDiscoverers()) {
		LOG_INFO(" \"{}\"  {}", r.name(), r.longName());
	}

	for (auto cat : { VLC::MediaDiscoverer::Category::Devices, VLC::MediaDiscoverer::Category::Lan, VLC::MediaDiscoverer::Category::Podcasts, VLC::MediaDiscoverer::Category::Localdirs }) {
		LOG_INFO("Media discoverers in category {}:", int(cat));
		for (const auto &d : instance.mediaDiscoverers(cat)) {
			LOG_INFO(" \"{}\"  {}", d.name(), d.longName());
		}
	}
}


Player::PlayerImpl::~PlayerImpl() {
}


void Player::PlayerImpl::logCallback(int level, const libvlc_log_t * /* context */, std::string message) {
	auto l = utuputki::LogLevel::Error;
	switch (level) {
	case LIBVLC_DEBUG:
		l = utuputki::LogLevel::Debug;
		break;

	case LIBVLC_NOTICE:
		l = utuputki::LogLevel::Info;
		break;

	case LIBVLC_WARNING:
		l = utuputki::LogLevel::Warning;
		break;

	case LIBVLC_ERROR:
		l = utuputki::LogLevel::Error;
		break;
	}

	if (l < vlcLogLevel) {
		return;
	}

	Logger::message(l, "VLC: {}", message);
}

Player::Player(Utuputki &utuputki_, const Config &config)
: impl(new PlayerImpl(utuputki_, config))
{
}


Player::~Player() {
}


void Player::run() {
	assert(impl);

	impl->run();
}


void Player::PlayerImpl::run() {
	VLC::MediaPlayer mediaPlayer(instance);
	mediaPlayer.setFullscreen(fullscreen);

	if (!audioDevice.empty()) {
		LOG_INFO("setting audio device to \"{}\"...", audioDevice);
		mediaPlayer.outputDeviceSet(audioDevice);
	}

	if (normalizeVolume) {
		// TODO: enable volume normalization filter
		// not supported by libvlc, need to use libvlccore
		// the following appears to be out of date:
		// https://forum.videolan.org/viewtopic.php?t=121953
	}

	mediaPlayer.eventManager().onEndReached(std::bind(&PlayerImpl::videoFinishCallback, this));

	std::string cacheDirectory = utuputki.getCacheDirectory();

	VLC::Media                      currentMedia;
	while (true) {
		if (shutdownFlag) {
			break;
		}

		auto currentlyPlaying = utuputki.popNextPlaylistItem();

		{
			std::unique_lock<std::mutex> lock(helpMutex);
			if (currentlyPlaying) {
				currentMedia = VLC::Media(instance, cacheDirectory + "/" + currentlyPlaying->filename, VLC::Media::FromType::FromPath);
				mediaPlayer.setMedia(currentMedia);
				onStandby = false;
			} else {
				mediaPlayer.setMedia(standby);
				onStandby = true;
			}
			mediaPlayer.play();
			skipped = false;

			helpCV.wait(lock);
		}

		if (currentlyPlaying) {
			utuputki.playlistItemFinished(*currentlyPlaying, skipped ? HistoryStatus::Skipped : HistoryStatus::Completed);
		}
	}

	mediaPlayer.stop();
}


void Player::PlayerImpl::skipCurrent() {
	std::unique_lock<std::mutex> lock(helpMutex);
	skipped = true;
	helpCV.notify_one();

}


void Player::PlayerImpl::notifyMediaUpdate() {
	LOG_DEBUG("notifyMediaUpdate");

	std::unique_lock<std::mutex> lock(helpMutex);
	if (onStandby) {
		LOG_DEBUG("notifyMediaUpdate notify_one");
		helpCV.notify_one();
	}
}


void Player::shutdown(bool immediate) {
	assert(impl);

	impl->shutdownFlag = true;

	{
		std::unique_lock<std::mutex> lock(impl->helpMutex);
		if (immediate || impl->onStandby) {
			impl->helpCV.notify_one();
		}
	}
}


void Player::notifyMediaUpdate() {
	assert(impl);

	impl->notifyMediaUpdate();
}


void Player::skipCurrent() {
	assert(impl);

	impl->skipCurrent();
}


}  // namespace utuputki
