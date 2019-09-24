#ifndef MEDIA_H
#define MEDIA_H


#include <cassert>
#include <cstdint>
#include <string>

#include "utuputki/Timestamp.h"


namespace utuputki {


class Database;


class MediaId {
	uint64_t  id;

	// only Database may create these
	MediaId()                                = delete;

	explicit MediaId(uint64_t id_)
	: id(id_)
	{
		assert(id != 0);
	}

	friend class Database;

public:


	MediaId(const MediaId &other)            = default;
	MediaId &operator=(const MediaId &other) = default;

	MediaId(MediaId &&other)                 = default;
	MediaId &operator=(MediaId &&other)      = default;

	~MediaId()                               = default;


	bool operator==(const MediaId &other) const {
		return id == other.id;
	}


	bool operator!=(const MediaId &other) const {
		return id != other.id;
	}


	std::string toString() const {
		return std::to_string(id);
	}
};


enum class MediaStatus : uint8_t {
	  Initial
	, Downloading
	, Ready
	, Failed
};


struct MediaInfo {
	MediaStatus   status;
	std::string   url;
	std::string   filename;
	std::string   title;
	unsigned int  length;  // in seconds
	unsigned int  filesize;  // in bytes
	std::string   metadata;
	Timestamp     metadataTime;
	std::string   errorMessage;


	MediaInfo()
	: status(MediaStatus::Initial)
	, length(0)
	, filesize(0)
	{
	}

	MediaInfo(const MediaInfo &other)            = default;
	MediaInfo &operator=(const MediaInfo &other) = default;

	MediaInfo(MediaInfo &&other)                 = default;
	MediaInfo &operator=(MediaInfo &&other)      = default;

	~MediaInfo()                                 = default;
};


struct MediaInfoId : public MediaInfo {
	MediaId       id;


	explicit MediaInfoId(const MediaId &id_)
	: id(id_)
	{
	}

	MediaInfoId(const MediaInfoId &other)            = default;
	MediaInfoId &operator=(const MediaInfoId &other) = default;

	MediaInfoId(MediaInfoId &&other)                 = default;
	MediaInfoId &operator=(MediaInfoId &&other)      = default;

	~MediaInfoId()                                   = default;
};


}  // namespace utuputki


#endif  // MEDIA_H
