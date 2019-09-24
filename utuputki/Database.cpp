#include <sqlpp11/sqlite3/sqlite3.h>
#include <sqlpp11/sqlpp11.h>

#include <list>
#include <mutex>

#include "utuputki/Config.h"
#include "utuputki/Database.h"
#include "utuputki/Logger.h"

#include "DatabaseGenerated.h"
#include "create_database.sql.h"


using namespace sqlpp;


namespace utuputki {


static std::chrono::time_point<std::chrono::system_clock, std::chrono::microseconds> timeToDB(Timestamp t) {
	return std::chrono::time_point_cast<std::chrono::microseconds>(t);
}


static Timestamp timeFromDB(std::chrono::time_point<std::chrono::system_clock, std::chrono::microseconds> t) {
	return std::chrono::time_point_cast<Duration>(t);
}


static MediaStatus makeMediaStatus(int value) {
	assert(value >= 0);
	assert(value <= static_cast<int>(MediaStatus::Failed));

	return static_cast<MediaStatus>(value);
};


template<typename M, typename Row> void mediaFromRow(M &mediaInfo, const Row &row) {
	mediaInfo.status       = makeMediaStatus(row.status);
	mediaInfo.url          = row.url;
	mediaInfo.filename     = row.filename;
	mediaInfo.title        = row.title;
	mediaInfo.length       = row.length;
	mediaInfo.filesize     = row.filesize;
	mediaInfo.metadata     = row.metadata;
	mediaInfo.metadataTime = timeFromDB(row.metadataTime);
	mediaInfo.errorMessage = row.errorMessage;
}


struct Database::DatabaseImpl {
	typedef  sqlpp::sqlite3::connection  Connection;

	std::string                        dbFilename;
	sqlpp::sqlite3::connection_config  dbConfig;

	bool                               debugReverse;

	std::mutex                         dbMutex;
	Connection                         db;

	Media                              media;
	Playlist                           playlist;
	History                            history;


	DatabaseImpl()                                     = delete;

	DatabaseImpl(const DatabaseImpl &other)            = delete;
	DatabaseImpl &operator=(const DatabaseImpl &other) = delete;

	DatabaseImpl(DatabaseImpl &&other)                 = delete;
	DatabaseImpl &operator=(DatabaseImpl &&other)      = delete;

	explicit DatabaseImpl(const Config &config);

	~DatabaseImpl();


	template <typename T, typename F> T transactionValue(F && f) {
		std::unique_lock<std::mutex> lock(dbMutex);
		auto tx = start_transaction(db);

		try {
			auto result = f(db);

			tx.commit();

			return result;
		} catch (...) {
			tx.rollback();

			throw;
		}
	}


	template <typename F> void transaction(F && f) {
		std::unique_lock<std::mutex> lock(dbMutex);
		auto tx = start_transaction(db);

		try {
			f(db);

			tx.commit();
		} catch (...) {
			tx.rollback();

			throw;
		}
	}


	MediaInfoId getOrAddMediaByURL(const std::string &url);

	void addToPlaylist(MediaId media);

	std::vector<PlaylistItemMedia> getPlaylist();

	std::vector<HistoryItemMedia> getHistory();

	std::vector<MediaInfoId> getAllMedia();

	void updateMediaInfo(MediaInfoId &media);

	MediaInfoId getMediaInfo(MediaId id);

	tl::optional<HistoryItemMedia> popNextPlaylistItem();

	void playlistItemFinished(const HistoryItemMedia &item);
};


Database::DatabaseImpl::DatabaseImpl(const Config &config)
: dbFilename(config.get("database", "file", "utuputki.sqlite"))
, dbConfig(dbFilename, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE)
, debugReverse(config.getBool("database", "reverse", false))
, db(dbConfig)
{
	LOG_INFO("Opening database {}", dbFilename);
	LOG_INFO("Sqlite header version {}", sqlite3_version);
	LOG_INFO("Sqlite linked library version {}", sqlite3_libversion());

	static_assert(SQLITE_VERSION_NUMBER >= 300300, "sqlite 3.3 required for CREATE TABLE IF NOT EXISTS");
	// TODO: do we need a tighter check here?
	if (sqlite3_libversion_number() < SQLITE_VERSION_NUMBER) {
		throw std::runtime_error(fmt::format("Runtime sqlite version {} is older than linktime version {}", sqlite3_version, sqlite3_libversion()));
	}

	// don't instantly fail on busy
	db.execute("PRAGMA busy_timeout = 1000;");

	// enable foreign key constraints
	db.execute("PRAGMA foreign_keys = ON;");

	// debugging option
	if (debugReverse) {
		LOG_DEBUG("PRAGMA reverse_unordered_selects = ON");
		db.execute("PRAGMA reverse_unordered_selects = ON;");
	}

	// create tables
	// uses CREATE TABLE IF NOT EXISTS so should be idempotent
	std::string createTables(reinterpret_cast<const char *>(&create_database_sql[0]), create_database_sql_length);
	while (true) {
		auto semicol = createTables.find_first_of(';');
		if (semicol == std::string::npos) {
			break;
		}

		// TODO: c++17 string_view
		std::string table = createTables.substr(0, semicol);
		db.execute(table);
		createTables = createTables.substr(semicol + 1);
	}
}


Database::DatabaseImpl::~DatabaseImpl() {
}


Database::Database(const Config &config)
: impl(new DatabaseImpl(config))
{
}


Database::~Database() {
}


MediaInfoId Database::getOrAddMediaByURL(const std::string &url) {
	assert(impl);
	assert(!url.empty());

	return impl->getOrAddMediaByURL(url);
}


void Database::addToPlaylist(MediaId mediaId) {
	assert(impl);

	return impl->addToPlaylist(mediaId);
}


std::vector<PlaylistItemMedia> Database::getPlaylist() {
	assert(impl);

	return impl->getPlaylist();
}


std::vector<HistoryItemMedia> Database::getHistory() {
	assert(impl);

	return impl->getHistory();
}


std::vector<MediaInfoId> Database::getAllMedia() {
	assert(impl);

	return impl->getAllMedia();
}


void Database::updateMediaInfo(MediaInfoId &media) {
	assert(impl);

	return impl->updateMediaInfo(media);
}


MediaInfoId Database::getMediaInfo(MediaId id) {
	assert(impl);

	return impl->getMediaInfo(id);
}


tl::optional<HistoryItemMedia> Database::popNextPlaylistItem() {
	assert(impl);

	return impl->popNextPlaylistItem();
}


void Database::playlistItemFinished(const HistoryItemMedia &item) {
	assert(impl);

	return impl->playlistItemFinished(item);
}


MediaInfoId Database::DatabaseImpl::getOrAddMediaByURL(const std::string &url) {
	return transactionValue<MediaInfoId>([&] (Connection &conn) {
		auto stmt = conn.prepare(select(all_of(media))
		                        .from(media)
		                        .where(media.url == parameter(media.url))
		                        );

		stmt.params.url = url;

		auto result = conn(stmt);

		if (result.empty()) {
			// does not exist yet, create it
			// must use prepared statement because sqlpp11 doesn't handle
			// strings correctly without it
			auto ins = conn.prepare(insert_into(media)
			                       .set(media.url = parameter(media.url))
			                       );

			ins.params.url = url;
			auto newId = conn(ins);

			// fetch the newly added row
			result = conn(select(all_of(media))
			             .from(media)
			             .where(media.id == newId)
			             );
			assert(!result.empty());
		}

		const auto &row = result.front();

		MediaInfoId ret(MediaId(row.id));
		mediaFromRow(ret, row);

		// must be the only one
		result.pop_front();
		assert(result.empty());

		return ret;
	});
}


void Database::DatabaseImpl::addToPlaylist(MediaId mediaId) {
	assert(mediaId.id != 0);

	transaction([&mediaId, this] (Connection &conn) {
		auto sel = conn.prepare(select(playlist.id)
		                        .from(playlist)
		                        .where(playlist.media == parameter(playlist.media))
		                       );

		sel.params.media = mediaId.id;
		auto result = conn(sel);
		if (!result.empty()) {
			LOG_INFO("{} is already on playlist", mediaId.id);
			return;
		}

		auto ins = conn.prepare(insert_into(playlist)
		                        .set(playlist.media = parameter(playlist.media))
		                       );

		ins.params.media = mediaId.id;
		auto newId = conn(ins);

		LOG_DEBUG("new playlist id {}", newId);
	});
}


std::vector<PlaylistItemMedia> Database::DatabaseImpl::getPlaylist() {
	return transactionValue<std::vector<PlaylistItemMedia> > ([&] (Connection &conn) {
		std::vector<PlaylistItemMedia> retval;
		for (const auto &row : conn(select(playlist.id
										, playlist.media
										, playlist.queueTime
										, media.status
										, media.url
										, media.filename
										, media.title
										, media.length
										, media.filesize
										, media.metadata
										, media.metadataTime
										, media.errorMessage
									   )
								  .from(playlist
										.join(media)
										.on(playlist.media == media.id)
									   )
								  .unconditionally()
								  .order_by(playlist.queueTime.asc())
								)) {
			PlaylistItemMedia p(PlaylistItemId(row.id), MediaId(row.media));
			p.queueTime = timeFromDB(row.queueTime);
			mediaFromRow(p, row);
			retval.emplace_back(std::move(p));
		}

		return retval;
	});
}


std::vector<HistoryItemMedia> Database::DatabaseImpl::getHistory() {
	return transactionValue<std::vector<HistoryItemMedia> > ([&] (Connection &conn) {
		std::vector<HistoryItemMedia> retval;
		for (const auto &row : conn(select(history.id
										, history.media
										, history.queueTime
										, history.startTime
										, history.endTime
										, history.finishReason
										, history.skipCount
										, history.skipsNeeded
										, media.status
										, media.url
										, media.filename
										, media.title
										, media.length
										, media.filesize
										, media.metadata
										, media.metadataTime
										, media.errorMessage
									   )
								  .from(history
										.join(media)
										.on(history.media == media.id)
									   )
								  .unconditionally()
								  .order_by(history.queueTime.asc())
								)) {
			HistoryItemMedia p(HistoryItemId(row.id), MediaId(row.media));
			p.queueTime     = timeFromDB(row.queueTime);
			p.startTime     = timeFromDB(row.startTime);
			p.endTime       = timeFromDB(row.endTime);
			p.historyStatus = static_cast<HistoryStatus>(int(row.finishReason));
			p.skipCount     = row.skipCount;
			p.skipsNeeded   = row.skipsNeeded;
			mediaFromRow(p, row);
			retval.emplace_back(std::move(p));
		}

		return retval;
	});
}


std::vector<MediaInfoId> Database::DatabaseImpl::getAllMedia() {
	return transactionValue<std::vector<MediaInfoId> > ([&] (Connection &conn) {
		std::vector<MediaInfoId> retval;
		for (const auto &row : conn(select(all_of(media))
								  .from(media)
								  .unconditionally()
								  .order_by(media.id.asc())
								)) {
			MediaInfoId m(MediaId(row.id));
			mediaFromRow(m, row);
			retval.emplace_back(std::move(m));
		}

		return retval;
	});
}


void Database::DatabaseImpl::updateMediaInfo(MediaInfoId &mediaInfo) {
	transaction([&] (Connection &conn) {
		auto oldResult = conn(select(all_of(media))
							.from(media)
							.where(media.id == mediaInfo.id.id)
						   );

		assert(!oldResult.empty());
		auto &old = oldResult.front();
		assert(mediaInfo.id.id == static_cast<unsigned int>(old.id));

		if (mediaInfo.url != static_cast<std::string>(old.url)) {
			// if url changed need to check if we should remove this one
			LOG_INFO("Media {} URL changed from \"{}\" to \"{}\"", mediaInfo.id.id, static_cast<std::string>(old.url), mediaInfo.url);
			// fetch old id
			auto otherGet = conn.prepare(select(all_of(media))
										 .from(media)
										 .where(media.url == parameter(media.url))
										);

			otherGet.params.url = mediaInfo.url;
			auto otherResult = conn(otherGet);

			if (!otherResult.empty()) {
				LOG_INFO("otherResult has things");

				const auto &otherRow = otherResult.front();
				int oldId            = otherRow.id;
				// history can't contain the new id yet

				// playlist can contain both the old and new ids
				// if dupes, need to remove second
				auto oldPlaylist = conn(select(playlist.id)
										.from(playlist)
										.where(playlist.media == oldId
											   || playlist.media == mediaInfo.id.id)
										.order_by(playlist.queueTime.asc())
									   );

				std::vector<int> oldIds;
				for (const auto &row : oldPlaylist) {
					oldIds.push_back(row.id);
				}

				LOG_DEBUG("oldIds.size() = {}", oldIds.size());
				if (oldIds.size() > 1) {
					assert(oldIds.size() == 2);
					conn(remove_from(playlist)
						 .where(playlist.id == oldIds[1])
						);
				}

				// update playlist to point old to new
				conn(update(playlist)
					 .set(playlist.media   =  oldId)
					 .where(playlist.media == mediaInfo.id.id)
					);

				// delete new
				conn(remove_from(media)
					 .where(media.id == mediaInfo.id.id)
					);

				// set id to old so the following update updates it
				mediaInfo.id = MediaId(oldId);
			} else {
				LOG_INFO("otherResult is empty");
			}
		}

		auto up = conn.prepare(update(media)
							 .set(media.status   = parameter(media.status)
								, media.url      = parameter(media.url)
								, media.filename = parameter(media.filename)
								, media.title    = parameter(media.title)
								, media.length   = parameter(media.length)
								, media.filesize = parameter(media.filesize)
								, media.metadata = parameter(media.metadata)
								, media.metadataTime = parameter(media.metadataTime)
								, media.errorMessage = parameter(media.errorMessage))
							 .where(media.id == parameter(media.id))
							);

		up.params.id           = mediaInfo.id.id;
		up.params.url          = mediaInfo.url;
		up.params.filename     = mediaInfo.filename;
		up.params.title        = mediaInfo.title;
		up.params.length       = mediaInfo.length;
		up.params.filesize     = mediaInfo.filesize;
		up.params.status       = static_cast<int>(mediaInfo.status);
		up.params.metadata     = mediaInfo.metadata;
		up.params.metadataTime = timeToDB(mediaInfo.metadataTime);
		up.params.errorMessage = mediaInfo.errorMessage;

		conn(up);

		// if status is failed remove from playlist
		if (mediaInfo.status == MediaStatus::Failed) {
			LOG_INFO("Media {} {} \"{}\" status is failed, removing from playlist", mediaInfo.id.id, mediaInfo.url, mediaInfo.title);
			conn(remove_from(playlist).where(playlist.media == mediaInfo.id.id));
		}

		// must be only one result from fetch
		oldResult.pop_front();
		assert(oldResult.empty());
	});
}


MediaInfoId Database::DatabaseImpl::getMediaInfo(MediaId id) {
	return transactionValue<MediaInfoId>([&] (Connection &conn) {
		MediaInfoId mediaInfo(id);

		auto result = conn(select(all_of(media))
						 .from(media)
						 .where(media.id == id.id)
						);

		if (result.empty()) {
			throw std::runtime_error("No media for MediaId (how did you make that?)");
		}

		const auto &row = result.front();
		mediaFromRow(mediaInfo, row);

		return mediaInfo;
	});
}


tl::optional<HistoryItemMedia> Database::DatabaseImpl::popNextPlaylistItem() {
	try {
		return transactionValue<tl::optional<HistoryItemMedia> >([&] (Connection &conn) {
			auto result = conn(select(playlist.id
								  , playlist.media
								  , playlist.queueTime
								  , media.status
								  , media.url
								  , media.filename
								  , media.title
								  , media.length
								  , media.filesize
								  , media.metadata
								  , media.metadataTime
								  , media.errorMessage
								 )
							 .from(playlist
								   .join(media)
								   .on(playlist.media == media.id)
								  )
							 .where(media.status == static_cast<int>(MediaStatus::Ready))
							 .order_by(playlist.queueTime.asc())
							 .limit(1U)
							);

			if (result.empty()) {
				return tl::optional<HistoryItemMedia>();
			}

			const auto &row = result.front();

			conn(remove_from(playlist).where(playlist.id == row.id));
			auto historyId = conn(insert_into(history).set(history.media     = row.media
													   , history.queueTime = row.queueTime));

			HistoryItemMedia p(HistoryItemId(historyId), MediaId(row.media));
			p.queueTime = timeFromDB(row.queueTime);
			p.startTime = Timestamp::clock::now();
			mediaFromRow(p, row);

			return tl::optional<HistoryItemMedia>(p);
		});
	} catch (sqlpp::exception &e) {
		LOG_ERROR("caught sqlpp_exception in popNextPlaylistItem: {}", e.what());
		return tl::optional<HistoryItemMedia>();
	} catch (...) {
        LOG_ERROR("caught unknown exception in popNextPlaylistItem");
		return tl::optional<HistoryItemMedia>();
	}
}


void Database::DatabaseImpl::playlistItemFinished(const HistoryItemMedia &item) {
	transaction([&] (Connection &conn) {
		auto up = conn.prepare(update(history)
							 .set(history.endTime      = parameter(history.endTime)
								, history.finishReason = parameter(history.finishReason)
								, history.skipCount    = parameter(history.skipCount)
								, history.skipsNeeded  = parameter(history.skipsNeeded))
							 .where(history.id == parameter(history.id))
							);

		up.params.id           = item.id.id;
		up.params.endTime      = std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::system_clock::now());
		if (item.historyStatus) {
			up.params.finishReason = static_cast<int>(*item.historyStatus);
		}
		up.params.skipCount    = item.skipCount;
		up.params.skipsNeeded  = item.skipsNeeded;

		conn(up);
	});
}


}  // namespace utuputki
