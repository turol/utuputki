sp             := $(sp).x
dirstack_$(sp) := $(d)
d              := $(dir)


SUBDIRS:= \
	civetweb \
	CxxUrl \
	date \
	fmt \
	# empty line

DIRS:=$(addprefix $(d)/,$(SUBDIRS))

$(eval $(foreach directory, $(DIRS), $(call directory-module,$(directory)) ))


FILES:= \
	# empty line


SRC_$(d):=$(addprefix $(d)/,$(FILES))


DEPENDS_libvlcpp:=libvlc


d  := $(dirstack_$(sp))
sp := $(basename $(sp))
