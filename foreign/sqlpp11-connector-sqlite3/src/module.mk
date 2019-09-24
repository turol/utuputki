sp             := $(sp).x
dirstack_$(sp) := $(d)
d              := $(dir)


SUBDIRS:= \
	detail \
	# empty line

DIRS:=$(addprefix $(d)/,$(SUBDIRS))

$(eval $(foreach directory, $(DIRS), $(call directory-module,$(directory)) ))


FILES:= \
	bind_result.cpp \
	connection.cpp \
	prepared_statement.cpp \
	# empty line


SRC_$(d):=$(addprefix $(d)/,$(FILES)) $(SRC_$(d)/detail)

SRC_sqlpp11:=$(SRC_$(d))

DEPENDS_sqlpp11:=sqlite3


d  := $(dirstack_$(sp))
sp := $(basename $(sp))
