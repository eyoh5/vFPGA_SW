ECHO = @
CXX = g++ -o 
CPPFLAGS = -DOPTION3=1 -DACL_USE_DMA=1 -DACL_COMPILER_IS_MSVC=0 -Wall -Wno-unknown-pragmas -Wno-delete-non-virtual-dtor -D__USE_XOPEN2K8 -Werror -DACL_HAS_STDLIB_STDIO -DACL_HOST_RUNTIME_IS_STATIC=0 -DACL_OPENCL_HOST_SYS=linux -DACL_OPENCL_HOST_BIT=64 -DACL_TARGET_SYS=linux -DACL_TARGET_BIT=64 -DLINUX -I. -I../../include -I$(INTELFPGAOCLSDKROOT)/host/include
CXXFLAGS = -fPIC -m64

LINKER = g++
LINKER_LIBDIRARGS = -L$(TOP_DEST_DIR)/linux64/lib -L$(INTELFPGAOCLSDKROOT)/host/linux64/lib
LINKER_ARGS = -z noexecstack -Wl,-z,relro,-z,now -Wl,-Bsymbolic -fPIC -Wl,--no-undefined -Wl,--exclude-libs,ALL -m64
LINK_LIBS = -lalteracl -lelf -lalterahalmmd -l$(MMD_LIB_NAME)
INCORPORATE_LIBS = 
PREBUILT_OBJS = 

PERVASIVE_DEPENDENCIES = ../../include/aocl_mmd.h

MT = 

EXEFILE = diagnose 

OBJ_EXT = o
OBJS = $(patsubst %.cpp,%.$(OBJ_EXT),$(wildcard *.cpp))
OBJ_DIR?= .
OBJ_FILES = $(addprefix $(OBJ_DIR)$(PS),$(OBJS))




CUR_REL_PATH = ../..

INSTALL_DIR = $(TOP_DEST_DIR)$(PS)linux64$(PS)libexec
TARGET_FILES = $(EXEFILE)
SUB_DIRS =

include $(CUR_REL_PATH)/Makefile.common
