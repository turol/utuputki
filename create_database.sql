CREATE TABLE IF NOT EXISTS media (
	  id             INTEGER PRIMARY KEY
	, status         INTEGER NOT NULL DEFAULT 0
	, url            TEXT    UNIQUE NOT NULL
	, filename       TEXT
	, title          TEXT
	, length         INTEGER
	, filesize       INTEGER
	, metadata       TEXT
	, metadataTime   TIMESTAMP
	, errorMessage   TEXT
	  CHECK (status >= 0 AND status <= 3)
);


CREATE TABLE IF NOT EXISTS playlist (
	  id             INTEGER PRIMARY KEY
	, media          INTEGER NOT NULL
	, queueTime      TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
	, FOREIGN KEY (media) REFERENCES media
);


CREATE TABLE IF NOT EXISTS history (
	  id            INTEGER PRIMARY KEY
	, media         INTEGER NOT NULL
	, queueTime     TIMESTAMP NOT NULL
	, startTime     TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
	, skipCount     INTEGER NOT NULL DEFAULT 0
	, skipsNeeded   INTEGER NOT NULL DEFAULT 0
	, endTime       TIMESTAMP
	, finishReason  INTEGER
	, FOREIGN KEY (media) REFERENCES media
	  CHECK (finishReason >= 0 AND finishReason <= 1)
);
