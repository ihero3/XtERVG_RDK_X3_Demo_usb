CXX = g++
CXX_FLAGS = -g -O3
CMP_FLAGS = $(CXX_FLAGS) -c
EXE_FLAGS = $(CXX_FLAGS) -o

# Build for RDK X3 GPU encoder (hb_venc)
CMP_FLAGS += -DUSE_HB

XFTP = xftp
MONITOR = monitor
XFTP_USB = xftp_usb
EXECS = $(XFTP) $(MONITOR) $(XFTP_USB) 
XFTP_OBJS = xftp.o string_split.o sqlite.o http.o thread.o sps_parser.o frame_cir_buff.o annotation_info.o fcos_post_process.o
MONITOR_OBJS = monitor.o config_file.o string_split.o sqlite.o http.o 
XFTP_USB_OBJS = yuv_to_h264.o

LIB_PATH := ../libs /usr/local/xt/lib
LIB_PATH += /usr/lib /usr/lib/hbmedia
LIBS := $(addprefix -L,$(LIB_PATH))
LIBS += -lxftp_live -lxttp_rtc -lrtsp_utils -lcurl -lcjson -lsqlite3 -pthread -lm -lstdc++
LIBS += -lspcdev -lhbmem -lion -lhbmedia -lvio -ldl

INC_PATH := ../include /usr/local/xt/include
INC_PATH += /usr/include /usr/include/vio
INCS := $(addprefix -I,$(INC_PATH))

INSTALL_DIR = /usr/local/xt/bin

.PHONY: all clean

all: $(XFTP) $(MONITOR)	 $(XFTP_USB)

$(XFTP): $(XFTP_OBJS)
	$(CXX) $(EXE_FLAGS) $@ $^ $(LIBS)

$(MONITOR): $(MONITOR_OBJS)
	$(CXX) $(EXE_FLAGS) $@ $^ $(LIBS)

$(XFTP_USB): $(XFTP_USB_OBJS)
	$(CXX) $(EXE_FLAGS) $@ $^ $(LIBS)	
	
%.o: %.cpp
	$(CXX) $(CMP_FLAGS) $(INCS) -o $@ $<

clean:
	rm -f $(EXECS) *.o

install:
	cp -f $(EXECS) $(INSTALL_DIR)
