ECHO = @
CXX = g++ -o 
CPPFLAGS = -DKERNEL_64BIT -O3 -DOPTION3=1 -DACL_USE_DMA=1 -DACL_COMPILER_IS_MSVC=0 -Wall -Wno-unknown-pragmas -Wno-delete-non-virtual-dtor -D__USE_XOPEN2K8 -DACL_HAS_STDLIB_STDIO -DACL_HOST_RUNTIME_IS_STATIC=0 -DACL_OPENCL_HOST_SYS=linux -DACL_OPENCL_HOST_BIT=64 -DACL_TARGET_SYS=linux -DACL_TARGET_BIT=64 -DLINUX -DACL_MAX_DEVICE=32 -I. -I../../include -I$(INTELFPGAOCLSDKROOT)/host/include/CL -I$(TOP_DEST_DIR)/linux64/driver
CXXFLAGS = -fPIC -m64

LINKER = g++
LINKER_LIBDIRARGS = -Llib -L$(INTELFPGAOCLSDKROOT)/host/linux64/lib
LINKER_ARGS = -lrt -pthread -z noexecstack -Wl,-z,relro,-z,now -Wl,-Bsymbolic -fPIC -Wl,--no-undefined -Wl,--exclude-libs,ALL -m64
INCORPORATE_LIBS = -lpkg_editor
PREBUILT_OBJS = 
LINK_LIBS = -ldl    -lelf 

DLLFILE = lib$(MMD_LIB_NAME).so
DLLLIBFILE = 
PERVASIVE_DEPENDENCIES = ../../include/aocl_mmd.h ../../include/pkg_editor.h $(TOP_DEST_DIR)/linux64/driver/hw_pcie_constants.h $(TOP_DEST_DIR)/linux64/driver/hw_pcie_dma.h $(TOP_DEST_DIR)/linux64/driver/pcie_linux_driver_exports.h $(TOP_DEST_DIR)/linux64/driver/version.h

OBJ_EXT = o
OBJS = $(patsubst %.cpp,%.$(OBJ_EXT),$(wildcard *.cpp))
OBJ_DIR?= .
OBJ_FILES = $(addprefix $(OBJ_DIR)$(PS),$(OBJS))



CUR_REL_PATH = ../..

INSTALL_DIR = $(TOP_DEST_DIR)$(PS)linux64$(PS)lib
TARGET_FILES = $(DLLFILE)
SPECIAL_INSTALL_FILE = 
SUB_DIRS =

include $(CUR_REL_PATH)/Makefile.common
