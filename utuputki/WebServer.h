#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <memory>

#include "utuputki/Media.h"


namespace utuputki {


class Config;
class Utuputki;


class WebServer {
	struct WebServerImpl;
	std::unique_ptr<WebServerImpl> impl;


	WebServer()                                  = delete;

	WebServer(const WebServer &other)            = delete;
	WebServer &operator=(const WebServer &other) = delete;

	WebServer(WebServer &&other)                 = delete;
	WebServer &operator=(WebServer &&other)      = delete;

public:


	WebServer(Utuputki &utuputki, const Config &config);

	~WebServer();

	void notifyAddedToPlaylist(const MediaInfoId &media);

	void notifyNowPlaying(const HistoryItemMedia &media);

	void notifyPlaylistItemFinished(const HistoryItemMedia &media);

	unsigned int getNumActiveClients();

	void startServer();
};


}  // namespace utuputki


#endif  // WEBSERVER_H
