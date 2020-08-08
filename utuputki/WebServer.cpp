#include <cctype>
#include <cstring>

#include <mutex>
#include <unordered_set>

#include <CivetServer.h>
#include <date/date.h>
#include <date/tz.h>
#include <inja/inja.hpp>

#include "utuputki/Config.h"
#include "utuputki/Logger.h"
#include "utuputki/Utuputki.h"
#include "utuputki/WebServer.h"

#include "listMedia.template.h"
#include "footer.template.h"
#include "header.template.h"
#include "history.template.h"
#include "playlist.template.h"
#include "utuputki.css.h"
#include "utuputki.js.h"


using namespace nlohmann;


namespace nlohmann {


template <>
struct adl_serializer<utuputki::Timestamp> {

	static void to_json(json& j, const utuputki::Timestamp &t) {
		j = date::format("%FT%T%Z", t);
	}

};


template <typename T>
struct adl_serializer<tl::optional<T> > {

	static void to_json(json& j, const tl::optional<T> &t) {
		if (t) {
			j = *t;
		}
	}

};


}  // namespace nlohmann


namespace utuputki {


enum class Format : uint8_t {
	  HTML
	, JSON
	, PrettyJSON
};


enum class MIMEType : uint8_t {
	  ApplicationJSON
	, TextCSS
	, TextHTML
	, TextJavaScript
	, TextPlain
};


std::array<const char *, 5> mimeTypeStrings = { "application/json", "text/css", "text/html", "text/javascript", "text/plain" };


static const char *mimeTypeString(MIMEType t) {
	auto index = static_cast<unsigned int>(t);
	assert(index < mimeTypeStrings.size());

	return mimeTypeStrings[index];
}


std::array<const char *, 3> formatNames = { "html", "json", "prettyjson"};


static Format getFormatParameter(struct mg_connection *conn, Format def) {
	std::string format;
	if (!CivetServer::getParam(conn, "format", format)) {
		return def;
	}

	std::transform(format.begin(), format.end(), format.begin(), ::tolower);

	for (unsigned int i = 0; i < formatNames.size(); i++) {
		if (format == formatNames[i]) {
			return static_cast<Format>(i);
		}
	}

	return def;
}


std::array<const char *, 4> statusNames = { "Fetching metadata", "Downloading", "Ready", "Failed" };


static const char *statusString(MediaStatus s) {
	auto index = static_cast<unsigned int>(s);
	assert(index < statusNames.size());

	return statusNames[index];
}


static std::string formatLength(unsigned int seconds) {
	if (seconds < 3600) {
		unsigned int minutes = seconds / 60;
		seconds              = seconds - minutes* 60;

		return fmt::format("{}:{:>02}", minutes, seconds);
	} else {
		unsigned int hours   = seconds / 3600;
		assert(hours > 0);
		seconds              = seconds - hours * 3600;

		unsigned int minutes = seconds / 60;
		seconds              = seconds - minutes* 60;
		return fmt::format("{}:{:>02}:{:>02}", hours, minutes, seconds);
	}
}


static json jsonFromMediaInfo(const MediaInfo &item) {
	auto j = json {
		  { "status",          item.status                }
		, { "statusString",    statusString(item.status)  }
		, { "url",             item.url                   }
		, { "filename",        item.filename              }
		, { "title",           item.title                 }
		, { "lengthSeconds",   item.length                }
		, { "lengthReadable",  formatLength(item.length)  }
		, { "filesize",        item.filesize              }
		, { "metadataTime",    item.metadataTime          }
		, { "errorMessage",    item.errorMessage          }
	};

	// failed media might not have metadata
	if (!item.metadata.empty()) {
		j["metadata"] =        json::parse(item.metadata);
	} else {
		j["metadata"] =        json {};
	}

	return j;
}


void to_json(json &j, const MediaInfoId &item) {
	j = jsonFromMediaInfo(item);
	j["id"]            = item.id.toString();
}


void to_json(json &j, const PlaylistItemMedia &item) {
	j = jsonFromMediaInfo(item);
	j["id"]            = item.id.toString();
	j["queueTime"]     = item.queueTime;
}


void to_json(json &j, const HistoryItemMedia &item) {
	j = jsonFromMediaInfo(item);
	j["mediaId"]       = item.media.toString();
	j["startTime"]     = item.startTime;
	j["endTime"]       = item.endTime;
	j["historyStatus"] = item.historyStatus;
	j["skipCount"]     = item.skipCount;
	j["skipsNeeded"]   = item.skipsNeeded;
	j["id"]            = item.id.toString();
	j["queueTime"]     = item.queueTime;
}


struct WebServer::WebServerImpl {

	class UtuputkiServer final : public CivetServer {
	public:

		WebServer::WebServerImpl *impl;


		UtuputkiServer()                                       = delete;

		UtuputkiServer(const UtuputkiServer &other)            = delete;
		UtuputkiServer &operator=(const UtuputkiServer &other) = delete;

		UtuputkiServer(UtuputkiServer &&other)                 = delete;
		UtuputkiServer &operator=(UtuputkiServer &&other)      = delete;

		UtuputkiServer(const std::vector<std::string> &options, WebServer::WebServerImpl *impl_)
		: CivetServer(options)
		, impl(impl_)
		{
			assert(impl);
		}


		~UtuputkiServer() {
			assert(impl);
			impl = nullptr;
		}
	};


	class StaticHandler final : public CivetHandler {
		StaticHandler(const StaticHandler &other)            = delete;
		StaticHandler &operator=(const StaticHandler &other) = delete;

		StaticHandler(StaticHandler &&other)                 = delete;
		StaticHandler &operator=(StaticHandler &&other)      = delete;


		const unsigned char  *content;
		unsigned int         length;
		MIMEType             mimeType;


	public:

		StaticHandler(const unsigned char *content_, unsigned int length_, MIMEType mimeType_)
		: content(content_)
		, length(length_)
		, mimeType(mimeType_)
		{
		}


		bool handleGet(CivetServer * /* server */, struct mg_connection *conn) override final {
			int retval = mg_send_http_ok(conn, mimeTypeString(mimeType), length);
			if (retval < 0) {
				LOG_ERROR("mg_send_http_ok failed: {}", retval);
				return true;
			}

			retval = mg_write(conn, content, length);
			if (retval < 0) {
				LOG_ERROR("mg_write failed: {}", retval);
				return true;
			}

			return true;
		}

	};


	class RequestHandler : public CivetHandler {
		RequestHandler(const RequestHandler &other)            = delete;
		RequestHandler &operator=(const RequestHandler &other) = delete;

		RequestHandler(RequestHandler &&other)                 = delete;
		RequestHandler &operator=(RequestHandler &&other)      = delete;


		template <typename F> bool process(F f, const char *reqType, CivetServer *server_, struct mg_connection *conn) {
			assert(server_);
			assert(conn);

			auto utuServer       = static_cast<UtuputkiServer *>(server_);
			assert(utuServer);

			WebServerImpl *impl_ = utuServer->impl;
			assert(impl_);

			auto info = mg_get_request_info(conn);
			std::string client(info->remote_addr);

			LOG_DEBUG("{} from \"{}\"", reqType, client);

			if (impl_->forwarders.find(client) != impl_->forwarders.end()) {
				const char *forwarded_for = CivetServer::getHeader(conn, "X-Forwarded-For");
				if (forwarded_for) {
					LOG_DEBUG("x-forwarded-for: \"{}\"", forwarded_for);

					client = forwarded_for;
				} else {
					LOG_DEBUG("not forwarded");
				}
			}

			{
				std::unique_lock<std::mutex> lock(impl_->clientsMutex);
				// insert client if not already exist
				auto it = impl_->clients.find(client);
				if (it == impl_->clients.end()) {
					LOG_DEBUG("new client {}", client);
					ClientData d;
					d.lastActive = Timestamp::clock::now();
					impl_->clients.emplace(client, d);
				} else {
					// update timestamp
					it->second.lastActive = Timestamp::clock::now();
				}

				impl_->cleanupClients(lock);
			}

			try {
				return f(this, impl_, client, conn);
			} catch (std::exception &e) {
				LOG_ERROR("Exception from {}: {}", this->name(), e.what());

				return sendError(conn, 500, impl_->debugMode ? e.what() : "Internal Server Error");
			} catch (...) {
				LOG_ERROR("Unknown exception from {}", this->name());

				return sendError(conn, 500, impl_->debugMode ? "Unknown exception" : "Internal Server Error");
			}

			return true;
		}


	protected:

		bool sendError(struct mg_connection *conn, int errorCode, const std::string &message) {
			int retval = mg_send_http_error(conn, errorCode, "%s", message.c_str());
			if (retval < 0) {
				LOG_ERROR("mg_send_http_error failed: {}", retval);
				return true;
			}

			return true;
		}


		bool sendOK(struct mg_connection *conn, MIMEType mimeType, const std::string &contents) {
			int retval = mg_send_http_ok(conn, mimeTypeString(mimeType), contents.size());
			if (retval < 0) {
				LOG_ERROR("mg_send_http_ok failed: {}", retval);
				return true;
			}

			retval = mg_write(conn, contents.data(), contents.size());
			if (retval < 0) {
				LOG_ERROR("mg_write failed: {}", retval);
				return true;
			}

			return true;
		}


		bool sendRedirect(struct mg_connection *conn, const std::string &target) {
			int retval = mg_send_http_redirect(conn, target.c_str(), 302);
			if (retval < 0) {
				LOG_ERROR("mg_send_http_redirect: {}", retval);
				return true;
			}

			return true;
		}

	public:

		RequestHandler() {}

		virtual const char *name() const = 0;

		bool handleGet(CivetServer *server_, struct mg_connection *conn) override final {
			return process(std::mem_fn<bool(WebServerImpl *, const std::string &, struct mg_connection *)>(&RequestHandler::handleGet), "GET", server_, conn);
		}


		virtual bool handleGet(WebServerImpl * /* impl_ */, const std::string & /* client */, struct mg_connection * /* conn */) {
			return false;
		}


		bool handlePost(CivetServer *server_, struct mg_connection *conn) override final {
			return process(std::mem_fn<bool(WebServerImpl *, const std::string &, struct mg_connection *)>(&RequestHandler::handlePost), "POST", server_, conn);
		}


		virtual bool handlePost(WebServerImpl * /* impl_ */, const std::string & /* client */, struct mg_connection * /* conn */) {
			return false;
		}
	};



	class HistoryHandler final : public RequestHandler {
		HistoryHandler(const HistoryHandler &other)            = delete;
		HistoryHandler &operator=(const HistoryHandler &other) = delete;

		HistoryHandler(HistoryHandler &&other)                 = delete;
		HistoryHandler &operator=(HistoryHandler &&other)      = delete;

	public:

		HistoryHandler() {
		}


		const char *name() const override {
			return "history";
		}


		bool handleGet(WebServerImpl *impl_, const std::string & /* client */, struct mg_connection *conn) override {
			json jsonData;

			jsonData["title"]          = "Utuputki history";
			auto history = json::array();
			for (const auto &historyItem : impl_->utuputki.getHistory()) {
				json historyJson = historyItem;
				historyJson["startTimeReadable"]  = impl_->formatLocalTime(historyItem.startTime);
				historyJson["endTimeReadable"]    = impl_->formatLocalTime(historyItem.endTime);

				if (historyItem.skipCount > 0) {
					if (historyItem.skipCount >= historyItem.skipsNeeded) {
						historyJson["finishReason"]    = fmt::format("Skipped ({} / {})", historyItem.skipCount, historyItem.skipsNeeded);
					} else {
						historyJson["finishReason"]    = "Finished";
					}
				} else {
					historyJson["finishReason"]    = "Finished";
				}

				history.push_back(std::move(historyJson));
			}

			jsonData["history"]        = std::move(history);
			jsonData["refreshSeconds"] = 60;

			MIMEType mimeType = MIMEType::TextHTML;
			std::string output;

			Format fmt = getFormatParameter(conn, Format::HTML);
			std::tie(output, mimeType) = impl_->formatOutput(jsonData, fmt, impl_->getHistoryTemplate());

			return sendOK(conn, mimeType, output);
		}
	};


	class ListMediaHandler final : public RequestHandler {
		ListMediaHandler(const ListMediaHandler &other)            = delete;
		ListMediaHandler &operator=(const ListMediaHandler &other) = delete;

		ListMediaHandler(ListMediaHandler &&other)                 = delete;
		ListMediaHandler &operator=(ListMediaHandler &&other)      = delete;

	public:

		ListMediaHandler() {
		}


		const char *name() const override {
			return "listMedia";
		}


		bool handleGet(WebServerImpl *impl_, const std::string & /* client */, struct mg_connection *conn) override {
			json jsonData;

			jsonData["title"]          = "Utuputki media";
			jsonData["allMedia"]       = impl_->utuputki.getAllMedia();
			jsonData["refreshSeconds"] = 60;

			MIMEType mimeType = MIMEType::TextHTML;
			std::string output;

			Format fmt = getFormatParameter(conn, Format::HTML);
			std::tie(output, mimeType) = impl_->formatOutput(jsonData, fmt, impl_->getListMediaTemplate());

			return sendOK(conn, mimeType, output);
		}
	};


	class SkipHandler final : public RequestHandler {
		SkipHandler(const SkipHandler &other)            = delete;
		SkipHandler &operator=(const SkipHandler &other) = delete;

		SkipHandler(SkipHandler &&other)                 = delete;
		SkipHandler &operator=(SkipHandler &&other)      = delete;

	public:

		SkipHandler() {
		}


		const char *name() const override {
			return "skip";
		}


		bool handlePost(WebServerImpl *impl_, const std::string &client, struct mg_connection *conn) override {
			std::string media;
			bool found = CivetServer::getParam(conn, "media", media);
			if (found) {
				impl_->utuputki.skipVideo(media, client);
			}

			// redirect back to playlist
			return sendRedirect(conn, "/");
		}
	};


	class PlaylistHandler final : public RequestHandler {
		PlaylistHandler(const PlaylistHandler &other)            = delete;
		PlaylistHandler &operator=(const PlaylistHandler &other) = delete;

		PlaylistHandler(PlaylistHandler &&other)                 = delete;
		PlaylistHandler &operator=(PlaylistHandler &&other)      = delete;

	public:

		PlaylistHandler() {
		}


		const char *name() const override {
			return "playlist";
		}


		bool handleGet(WebServerImpl *impl_, const std::string & /* client */, struct mg_connection *conn) override {
			json jsonData;

			jsonData["title"]      = "Utuputki playlist";
			tl::optional<HistoryItemMedia> nowPlaying = impl_->utuputki.getNowPlaying();
			jsonData["nowPlaying"] = nowPlaying;

			unsigned int refreshSeconds = 60;
			unsigned int left           = 0;
			if (nowPlaying) {
				Duration elapsed = Timestamp::clock::now() - nowPlaying->startTime;
				// c++17 TODO: time_point ceil
				unsigned int elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
				jsonData["nowPlaying"]["elapsed"]        = formatLength(elapsedSeconds);
				jsonData["nowPlaying"]["elapsedSeconds"] = elapsedSeconds;

				left   = nowPlaying->length - elapsedSeconds;
				jsonData["nowPlaying"]["left"]           = formatLength(left);
				jsonData["nowPlaying"]["leftSeconds"]    = left;

				// make sure we autorefresh just after finishing this one
				refreshSeconds = std::min(refreshSeconds, left + 1);
			}

			json playlist = impl_->utuputki.getPlaylist();

			// hax to fix webpage where nothing is playing but playlist has stuff
			// tends to happen after skip
			if (!nowPlaying && !playlist.empty()) {
				refreshSeconds = 1;
			}

			jsonData["refreshSeconds"] = refreshSeconds;

			// calculate start times
			unsigned int cumulativeLength = 0;
			Timestamp now = Timestamp::clock::now();
			for (auto &item : playlist) {
				item["cumulativeLength"]          = cumulativeLength;
				item["cumulativeLengthReadable"]  = formatLength(cumulativeLength);

				unsigned int start = left + cumulativeLength;
				Timestamp startTime = now + std::chrono::seconds(start);
				item["start"]                     = start;
				item["startReadable"]             = formatLength(start);
				item["startTime"]                 = startTime;
				item["startTimeReadable"]         = impl_->formatLocalTime(startTime);

				cumulativeLength += static_cast<unsigned int>(item["lengthSeconds"]);
			}
			jsonData["playlist"] = playlist;

			MIMEType mimeType = MIMEType::TextHTML;
			std::string output;

			Format fmt = getFormatParameter(conn, Format::HTML);
			std::tie(output, mimeType) = impl_->formatOutput(jsonData, fmt, impl_->getPlaylistTemplate());

			return sendOK(conn, mimeType, output);
		}
	};


	class AddMediaHandler final : public RequestHandler {
		AddMediaHandler(const AddMediaHandler &other)            = delete;
		AddMediaHandler &operator=(const AddMediaHandler &other) = delete;

		AddMediaHandler(AddMediaHandler &&other)                 = delete;
		AddMediaHandler &operator=(AddMediaHandler &&other)      = delete;

	public:

		AddMediaHandler() {
		}


		const char *name() const override {
			return "addMedia";
		}


		bool handlePost(WebServerImpl *impl_, const std::string & /* client */, struct mg_connection *conn) override {
			std::string media;
			bool has = UtuputkiServer::getParam(conn, "media", media);
			if (!has) {
				return sendError(conn, 400, "No media key");
			}

			if (!media.empty()) {
				try {
					impl_->utuputki.addMedia(media);
				} catch (BadHostException &e) {
					return sendError(conn, 403, e.what());
				} catch (std::exception &e) {
					LOG_ERROR("Exception from {}: {}", this->name(), e.what());

					return sendError(conn, 500, impl_->debugMode ? e.what() : "Internal Server Error");
				} catch (...) {
					LOG_ERROR("Unknown exception from {}", this->name());

					return sendError(conn, 500, "Internal server error");
				}
			}

			// redirect back to playlist
			return sendRedirect(conn, "/");
		}
	};


	struct ClientData {
		Timestamp  lastActive;
	};


	Utuputki                                     &utuputki;

	std::vector<std::string>                     serverOptions;
	bool                                         debugMode;

	std::unordered_set<std::string>              forwarders;

	std::unique_ptr<UtuputkiServer>              server;

	inja::Environment                            environment;

	inja::Template                               playlistTemplate;
	PlaylistHandler                              playlistHandler;

	inja::Template                               historyTemplate;
	HistoryHandler                               historyHandler;

	AddMediaHandler                              addMediaHandler;

	inja::Template                               listMediaTemplate;
	ListMediaHandler                             listMediaHandler;

	SkipHandler                                  skipHandler;

	StaticHandler                                cssHandler;
	StaticHandler                                jsHandler;

	const date::time_zone                        *localTimeZone;

	std::mutex                                   clientsMutex;
	std::unordered_map<std::string, ClientData>  clients;
	Duration                                     clientTimeout;
	Timestamp                                    nextClientCleanup;


#ifdef OVERRIDE_TEMPLATES


	inja::Template                               getTemplate(const std::string & /* name */, const inja::Template &defaultTemplate) {
		// TODO: check file exist, timestamp
		return defaultTemplate;
	}

	inja::Template                               getPlaylistTemplate() {
		return getTemplate("playlist.template", playlistTemplate);
	}

	inja::Template                               getHistoryTemplate() {
		return getTemplate("history.template", historyTemplate);
	}

	inja::Template                               getListMediaTemplate() {
		return getTemplate("listMedia.template", listMediaTemplate);
	}

#else // OVERRIDE_TEMPLATES

	const inja::Template                         &getPlaylistTemplate() {
		return playlistTemplate;
	}

	const inja::Template                         &getHistoryTemplate() {
		return historyTemplate;
	}

	const inja::Template                         &getListMediaTemplate() {
		return listMediaTemplate;
	}


#endif // OVERRIDE_TEMPLATES


	WebServerImpl()                                      = delete;

	WebServerImpl(const WebServerImpl &other)            = delete;
	WebServerImpl &operator=(const WebServerImpl &other) = delete;

	WebServerImpl(WebServerImpl &&other)                 = delete;
	WebServerImpl &operator=(WebServerImpl &&other)      = delete;

	WebServerImpl(Utuputki &utuputki_, const Config &config);

	~WebServerImpl();


	unsigned int getNumActiveClients();

	void cleanupClients(std::unique_lock<std::mutex> &witness);

	void startServer();

	std::string formatLocalTime(Timestamp time) {
		return date::format("%X", make_zoned(localTimeZone, time));
	}

	std::tuple<std::string, MIMEType> formatOutput(const json &jsonData, Format fmt, const inja::Template &htmlTemplate) {
		MIMEType mimeType = MIMEType::TextHTML;
		std::string output;

		switch (fmt) {
		case Format::HTML:
			output   = environment.render(htmlTemplate, jsonData);
			break;

		case Format::JSON:
			output   = jsonData.dump();
			mimeType = MIMEType::ApplicationJSON;
			break;

		case Format::PrettyJSON:
			output   = jsonData.dump(2, ' ', true, nlohmann::detail::error_handler_t::replace);
			mimeType = MIMEType::ApplicationJSON;
			break;

		}

		return std::make_tuple(output, mimeType);
	}
};


static std::vector<std::string> makeServerOptions(const Config &config) {
	std::vector<std::string> options;

	unsigned int port = config.get("webserver", "port", 8080);
	options.push_back("listening_ports");
	options.push_back(fmt::format("{}", port));

	unsigned numThreads = config.get("webserver", "numThreads", 50);
	options.push_back("num_threads");
	options.push_back(fmt::format("{}", numThreads));

	{
		std::string acl = config.get("webserver", "acl", "");
		if (!acl.empty()) {
			options.push_back("access_control_list");
			options.push_back("acl");
		}
	}

	if (config.getBool("webserver", "keepAlive", false)) {
		unsigned int timeout = config.get("webserver", "keepAliveTimeoutMS", 0);
		if (timeout == 0) {
			LOG_ERROR("keepAlive enabled but timeout is 0");
		} else {
			options.push_back("enable_keep_alive");
			options.push_back("yes");
			options.push_back("keep_alive_timeout_ms");
			options.push_back(fmt::format("{}", timeout));
		}
	}

	if (config.getBool("webserver", "websocketPingPong", false)) {
		unsigned int timeout = config.get("webserver", "webSocketTimeoutMS", 0);
		if (timeout == 0) {
			LOG_ERROR("websocketPingPong enabled but timeout is 0");
		} else {
			options.push_back("enable_websocket_ping_pong");
			options.push_back("yes");
			options.push_back("websocket_timeout_ms");
			options.push_back(fmt::format("{}", timeout));
		}
	}

	return options;

}


WebServer::WebServerImpl::WebServerImpl(Utuputki &utuputki_, const Config &config)
: utuputki(utuputki_)
, serverOptions(makeServerOptions(config))
, debugMode(config.getBool("webserver", "debug", false))
, playlistTemplate()
, historyTemplate()
, listMediaTemplate()
, cssHandler(utuputki_css, utuputki_css_length, MIMEType::TextCSS)
, jsHandler(utuputki_js, utuputki_js_length, MIMEType::TextJavaScript)
, localTimeZone(date::current_zone())
, clientTimeout(std::chrono::seconds(config.get("webserver", "clientTimeoutSeconds", 600)))
, nextClientCleanup(Timestamp::clock::now() + clientTimeout)
{
	environment.include_template("footer.template", environment.parse(nonstd::string_view(reinterpret_cast<const char *>(&footer_template[0]), footer_template_length)));
	environment.include_template("header.template", environment.parse(nonstd::string_view(reinterpret_cast<const char *>(&header_template[0]), header_template_length)));

	playlistTemplate  = environment.parse(nonstd::string_view(reinterpret_cast<const char *>(&playlist_template[0]), playlist_template_length));
	historyTemplate   = environment.parse(nonstd::string_view(reinterpret_cast<const char *>(&history_template[0]), history_template_length));
	listMediaTemplate = environment.parse(nonstd::string_view(reinterpret_cast<const char *>(&listMedia_template[0]), listMedia_template_length));

	{
		auto forwardersList = config.getList("webserver", "forwarders");
		forwarders.insert(forwardersList.begin(), forwardersList.end());
	}
}


void WebServer::WebServerImpl::startServer() {
	assert(!server);
	server.reset(new UtuputkiServer(serverOptions, this));
	assert(server);

	server->addHandler("/",              playlistHandler);
	server->addHandler("/addMedia",      addMediaHandler);
	server->addHandler("/history",       historyHandler);
	server->addHandler("/media",         listMediaHandler);
	server->addHandler("/playlist",      playlistHandler);
	server->addHandler("/skip",          skipHandler);
	server->addHandler("/utuputki.css",  cssHandler);
	server->addHandler("/utuputki.js",   jsHandler);
}


WebServer::WebServerImpl::~WebServerImpl() {
}


unsigned int WebServer::WebServerImpl::getNumActiveClients() {
	unsigned int n;

	{
		std::unique_lock<std::mutex> lock(clientsMutex);
		cleanupClients(lock);

		n = clients.size();
	}

	return n;
}


void WebServer::WebServerImpl::cleanupClients(std::unique_lock<std::mutex> &witness) {
	assert(witness.mutex());
	assert(witness.mutex() == &clientsMutex);

	if (Timestamp::clock::now() < nextClientCleanup) {
		// no need to clean up yet
		return;
	}

	auto now = Timestamp::clock::now();
	auto timeout = now - clientTimeout;

	LOG_DEBUG("client cleanup at {}, cleaning inactive since {}", formatLocalTime(now), formatLocalTime(timeout));
	unsigned int numCleaned = 0;
	for (auto it = clients.begin(); it != clients.end(); ) {
		auto lastActive = it->second.lastActive;
		if (lastActive < timeout) {
			LOG_DEBUG("timeouting {} (last active {})", it->first, formatLocalTime(lastActive));
			it = clients.erase(it);
			numCleaned++;
		} else {
			++it;
		}
	}

	LOG_DEBUG("cleanup up {} clients", numCleaned);

	nextClientCleanup = now + clientTimeout;
}


WebServer::WebServer(Utuputki &utuputki, const Config &config)
: impl(new WebServerImpl(utuputki, config))
{
}


WebServer::~WebServer() {
}


void WebServer::notifyAddedToPlaylist(const MediaInfoId & /* media */) {
	// TODO: websocket thing goes here
}


void WebServer::notifyNowPlaying(const HistoryItemMedia & /* media */) {
	// TODO: websocket thing goes here
}


void WebServer::notifyPlaylistItemFinished(const HistoryItemMedia & /* media */) {
	// TODO: websocket thing goes here
}


unsigned int WebServer::getNumActiveClients() {
	assert(impl);

	return impl->getNumActiveClients();
}


void WebServer::startServer() {
	assert(impl);

	impl->startServer();
}


}  // namespace utuputki
