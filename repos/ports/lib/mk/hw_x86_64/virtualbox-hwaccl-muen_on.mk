include $(REP_DIR)/lib/mk/virtualbox-common.inc

SRC_CC = sup.cc

INC_DIR += $(call select_from_repositories,src/lib/libc)
INC_DIR += $(REP_DIR)/src/virtualbox
INC_DIR += $(VBOX_DIR)/VMM/include

vpath sup.cc $(REP_DIR)/src/virtualbox/muen/
