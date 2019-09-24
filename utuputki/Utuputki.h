#ifndef UTUPUTKI_H
#define UTUPUTKI_H


#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <tl/optional.hpp>

#include "utuputki/Media.h"
#include "utuputki/Playlist.h"


namespace utuputki {


class BadHostException final : public std::runtime_error {
public:

	using std::runtime_error::runtime_error;
};


struct MediaInfo;


class Utuputki {
	struct UtuputkiImpl;
	std::unique_ptr<UtuputkiImpl> impl;


	Utuputki(const Utuputki &other)            = delete;
	Utuputki &operator=(const Utuputki &other) = delete;

	Utuputki(Utuputki &&other)                 = delete;
	Utuputki &operator=(Utuputki &&other)      = delete;

public:

	explicit Utuputki();

	~Utuputki();

	void run();

	bool shouldReExec() const;

	void addMedia(const std::string &mediaURL);

	MediaInfoId getOrAddMediaByURL(const std::string &url);

	void addToPlaylist(MediaId media);

	std::vector<PlaylistItemMedia> getPlaylist();

	std::vector<MediaInfoId> getAllMedia();

	void updateMediaInfo(MediaInfoId &media);

	tl::optional<HistoryItemMedia> popNextPlaylistItem();

	// not const because it updates the skip count
	void playlistItemFinished(HistoryItemMedia &item, HistoryStatus finishReason);

	tl::optional<HistoryItemMedia> getNowPlaying() const;

	std::vector<HistoryItemMedia> getHistory();

	std::string getCacheDirectory() const;

	void skipVideo(const std::string &media, const std::string &client);
};


}  // namespace utuputki


#endif  // UTUPUTKI_H
