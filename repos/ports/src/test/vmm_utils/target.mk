TARGET = test-vmm_utils
SRC_CC = main.cc
LIBS  += base-nova

REQUIRES = nova

vpath %.cc $(PRG_DIR)/..

CC_CXX_WARN_STRICT =
