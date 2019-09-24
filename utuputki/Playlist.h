#ifndef PLAYLIST_H
#define PLAYLIST_H


#include <cassert>
#include <cstdint>
#include <string>

#include <tl/optional.hpp>

#include "utuputki/Media.h"
#include "utuputki/Timestamp.h"


namespace utuputki {


class Database;


class PlaylistItemId {
	uint64_t  id;

	// only Database may create these
	PlaylistItemId()                                       = delete;

	explicit PlaylistItemId(uint64_t id_)
	: id(id_)
	{
		assert(id != 0);
	}

	friend class Database;

public:


	PlaylistItemId(const PlaylistItemId &other)            = default;
	PlaylistItemId &operator=(const PlaylistItemId &other) = default;

	PlaylistItemId(PlaylistItemId &&other)                 = default;
	PlaylistItemId &operator=(PlaylistItemId &&other)      = default;

	~PlaylistItemId()                                      = default;


	bool operator==(const PlaylistItemId &other) const {
		return id == other.id;
	}


	bool operator!=(const PlaylistItemId &other) const {
		return id != other.id;
	}


	std::string toString() const {
		return std::to_string(id);
	}
};


class HistoryItemId {
	uint64_t  id;

	// only Database may create these
	HistoryItemId()                                       = delete;

	explicit HistoryItemId(uint64_t id_)
	: id(id_)
	{
		assert(id != 0);
	}

	friend class Database;

public:


	HistoryItemId(const HistoryItemId &other)            = default;
	HistoryItemId &operator=(const HistoryItemId &other) = default;

	HistoryItemId(HistoryItemId &&other)                 = default;
	HistoryItemId &operator=(HistoryItemId &&other)      = default;

	~HistoryItemId()                                     = default;


	bool operator==(const HistoryItemId &other) const {
		return id == other.id;
	}


	bool operator!=(const HistoryItemId &other) const {
		return id != other.id;
	}


	std::string toString() const {
		return std::to_string(id);
	}
};


enum class PlaylistItemStatus : uint8_t {
	  Initial
	, Downloading
	, Finished
	, Failed
};


enum class HistoryStatus : uint8_t {
	  Completed
	, Skipped
};


struct PlaylistItem {
	PlaylistItemId  id;
	MediaId         media;
	Timestamp       queueTime;


	PlaylistItem(const PlaylistItemId &id_, const MediaId &media_)
	: id(id_)
	, media(media_)
	{
	}

	PlaylistItem()                                     = delete;

	PlaylistItem(const PlaylistItem &other)            = default;
	PlaylistItem &operator=(const PlaylistItem &other) = default;

	PlaylistItem(PlaylistItem &&other)                 = default;
	PlaylistItem &operator=(PlaylistItem &&other)      = default;

	~PlaylistItem()                                    = default;
};


struct PlaylistItemMedia : public PlaylistItem, public MediaInfo {
	using PlaylistItem::PlaylistItem;


	PlaylistItemMedia()                                          = delete;

	PlaylistItemMedia(const PlaylistItemMedia &other)            = default;
	PlaylistItemMedia &operator=(const PlaylistItemMedia &other) = default;

	PlaylistItemMedia(PlaylistItemMedia &&other)                 = default;
	PlaylistItemMedia &operator=(PlaylistItemMedia &&other)      = default;

	~PlaylistItemMedia()                                         = default;
};


struct HistoryItem {
	HistoryItemId                id;
	MediaId                      media;
	Timestamp                    queueTime;
	Timestamp                    startTime;
	Timestamp                    endTime;
	tl::optional<HistoryStatus>  historyStatus;
	unsigned int                 skipCount;
	unsigned int                 skipsNeeded;


	HistoryItem(const HistoryItemId &id_, const MediaId &media_)
	: id(id_)
	, media(media_)
	, skipCount(0)
	, skipsNeeded(0)
	{
	}

	HistoryItem()                                    = delete;

	HistoryItem(const HistoryItem &other)            = default;
	HistoryItem &operator=(const HistoryItem &other) = default;

	HistoryItem(HistoryItem &&other)                 = default;
	HistoryItem &operator=(HistoryItem &&other)      = default;

	~HistoryItem()                                   = default;

};


struct HistoryItemMedia : public HistoryItem, public MediaInfo {
	using HistoryItem::HistoryItem;


	HistoryItemMedia()                                         = delete;

	HistoryItemMedia(const HistoryItemMedia &other)            = default;
	HistoryItemMedia &operator=(const HistoryItemMedia &other) = default;

	HistoryItemMedia(HistoryItemMedia &&other)                 = default;
	HistoryItemMedia &operator=(HistoryItemMedia &&other)      = default;

	~HistoryItemMedia()                                        = default;
};


}  // namespace utuputki


#endif  // PLAYLIST_H
