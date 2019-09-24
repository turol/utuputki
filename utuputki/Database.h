#ifndef DATABASE_H
#define DATABASE_H


#include <memory>
#include <string>
#include <vector>

#include <tl/optional.hpp>

#include "utuputki/Media.h"
#include "utuputki/Playlist.h"


namespace utuputki {


class Config;
struct MediaInfo;


class Database {
	struct DatabaseImpl;
	std::unique_ptr<DatabaseImpl> impl;


	Database()                                 = delete;

	Database(const Database &other)            = delete;
	Database &operator=(const Database &other) = delete;

	Database(Database &&other)                 = delete;
	Database &operator=(Database &&other)      = delete;

public:


	explicit Database(const Config &config);

	~Database();


	MediaInfoId getOrAddMediaByURL(const std::string &url);

	void addToPlaylist(MediaId media);

	// media is not const, it can be changed in case of duplicates with different URLs
	void updateMediaInfo(MediaInfoId &media);

	MediaInfoId getMediaInfo(MediaId id);

	std::vector<MediaInfoId> getAllMedia();

	std::vector<PlaylistItemMedia> getPlaylist();

	std::vector<HistoryItemMedia> getHistory();

	void skip();

	void getSkipCount();

	tl::optional<HistoryItemMedia> popNextPlaylistItem();

	void playlistItemFinished(const HistoryItemMedia &item);
};


}  // namespace utuputki


#endif  // DATABASE_H
