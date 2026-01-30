CXX = g++
CXX_FLAGS = -g -O3
CMP_FLAGS = $(CXX_FLAGS) -c
EXE_FLAGS = $(CXX_FLAGS) -o

# Build for RDK X3 GPU encoder (hb_venc)
CMP_FLAGS += -DUSE_HB

XFTP = xftp
MONITOR = monitor
EXECS = $(XFTP) $(MONITOR)
XFTP_OBJS = xftp.o string_split.o sqlite.o http.o thread.o sps_parser.o frame_cir_buff.o annotation_info.o fcos_post_process.o
MONITOR_OBJS = monitor.o config_file.o string_split.o sqlite.o http.o

LIB_PATH := ../libs /usr/local/xt/lib
LIB_PATH += /usr/lib /usr/lib/hbmedia
LIBS := $(addprefix -L,$(LIB_PATH))
LIBS += -lxftp_live -lxttp_rtc -lrtsp_utils -lcurl -lcjson -lsqlite3 -pthread -lm -lstdc++
LIBS += -lspcdev -lhbmem -lion -lhbmedia -lvio -ldl

# try to link libhb_venc if available (RDK X3 device). If not found, skip it.
HBVENC_PATH1 := /usr/lib/libhb_venc.so
HBVENC_PATH2 := /usr/lib/hbmedia/libhb_venc.so
ifeq ($(wildcard $(HBVENC_PATH1)),)
ifeq ($(wildcard $(HBVENC_PATH2)),)
$(warning libhb_venc not found; building without -lhb_venc. On RDK X3, run make there to link GPU encoder.)
else
LIBS += -L/usr/lib/hbmedia -lhb_venc
endif
else
LIBS += -L/usr/lib -lhb_venc
endif

INC_PATH := ../include /usr/local/xt/include
INC_PATH += /usr/include /usr/include/vio
INCS := $(addprefix -I,$(INC_PATH))

INSTALL_DIR = /usr/local/xt/bin

.PHONY: all clean

all: $(XFTP) $(MONITOR)

$(XFTP): $(XFTP_OBJS)
	$(CXX) $(EXE_FLAGS) $@ $^ $(LIBS)

$(MONITOR): $(MONITOR_OBJS)
	$(CXX) $(EXE_FLAGS) $@ $^ $(LIBS)

%.o: %.cpp
	$(CXX) $(CMP_FLAGS) $(INCS) -o $@ $<

clean:
	rm -f $(EXECS) *.o

install:
	cp -f $(EXECS) $(INSTALL_DIR)
