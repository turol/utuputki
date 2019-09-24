sp             := $(sp).x
dirstack_$(sp) := $(d)
d              := $(dir)


FILES:= \
	Config.cpp \
	Database.cpp \
	Downloader.cpp \
	Logger.cpp \
	main.cpp \
	Player.cpp \
	Utuputki.cpp \
	WebServer.cpp \
	# empty line


$(dir)/Database.o: DatabaseGenerated.h create_database.sql.h

$(dir)/Player.o: standby.png.h

$(dir)/WebServer.o: listMedia.template.h footer.template.h header.template.h history.template.h playlist.template.h utuputki.css.h utuputki.js.h


DatabaseGenerated.h: $(TOPDIR)/create_database.sql
	$(TOPDIR)/foreign/sqlpp11/scripts/ddl2cpp $< DatabaseGenerated utuputki


SRC_$(d):=$(addprefix $(d)/,$(FILES))


embed_MODULES:=fmt
embed_SRC:=$(foreach f, embed.cpp, $(dir)/$(f))


utuputki_MODULES:=civetweb date fmt libvlcpp python sqlpp11 cxxurl
utuputki_SRC:=$(SRC_$(d))


PROGRAMS+= \
	embed \
	utuputki \
	# empty line


d  := $(dirstack_$(sp))
sp := $(basename $(sp))
