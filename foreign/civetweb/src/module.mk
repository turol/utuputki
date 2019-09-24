sp             := $(sp).x
dirstack_$(sp) := $(d)
d              := $(dir)


SUBDIRS:= \
	# empty line

DIRS:=$(addprefix $(d)/,$(SUBDIRS))

$(eval $(foreach directory, $(DIRS), $(call directory-module,$(directory)) ))


FILES:= \
	CivetServer.cpp \
	civetweb.c \
	# empty line


SRC_$(d):=$(addprefix $(d)/,$(FILES))


SRC_civetweb:=$(SRC_$(d))


d  := $(dirstack_$(sp))
sp := $(basename $(sp))
