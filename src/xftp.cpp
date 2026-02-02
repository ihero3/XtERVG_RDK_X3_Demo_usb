#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>
#include <signal.h>
#include <math.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <getopt.h>
#include <time.h>
#include <fcntl.h>
#include <sys/ioctl.h>
//请把目前项目功能通过rtsp获取流修改成通过uvc获取视频流，目前项目里面通过rtsp获取到的视频流是h264的流，uvc获取到的流是普通yuv流，所以通过提供的链接文档，进行添加流编码，编码成h264流，别的功能不变。项目运行在这个文档描述的RDK X3的ubuntu系统中，详细请参考链接文档https://developer.d-robotics.cc/rdk_doc/Quick_start/hardware_introduction/rdk_x3
// 目标平台为 Linux（RDK X3 Ubuntu），仅保留 Linux 平台的头文件与类型
#include <argp.h>
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <dlfcn.h>

// RDK X3 平台特定头文件
#include "hb_comm_venc.h"
#include "hb_venc.h"
#include "hb_vdec.h"
#include "hb_vio_interface.h"
#include "hb_sys.h"
#include "hb_vp_api.h"
#include "hb_vin_api.h"
#include "hb_vps_api.h"
#include "hb_mipi_api.h"
#include "hb_common.h"
#include "hb_type.h"
#include "hb_errno.h"
#include "hb_comm_video.h"
#include "sp_bpu.h"
#include "sp_vio.h"
#include "sp_display.h"
#include "sp_codec.h"
#include "sp_sys.h"
#include "hb_common_vot.h"
#include <dnn/hb_dnn.h>
#include <dnn/hb_sys.h>

// 确保类型定义一致
typedef VIDEO_STREAM_S VENC_STREAM_S;

// 全局变量定义

#include <algorithm>
#include <future>
#include <vector>
#include <string>
#include <deque>
#include <thread>
#include <iostream>
#include <iomanip>
#include <chrono>

// 本地头文件
#include "http.h"
#include "sqlite.h"
#include "sps_parser.h"
#include "string_split.h"
#include "rtsp_utils.h"
#include "xftp_live_sdk.h"
#include "xttp_rtc_sdk.h"
#include "frame_cir_buff.h"
#include "annotation_info.h"
#include "fcos_post_process.hpp"

using namespace std;

#define MSGID_NUM 32
#define XTTP_RETRY_MAX 3
#define MIN_PACKET_SIZE 480
#define SCRIPT_INNER_TYPE 0x01
#define ONE_MILLION_BASE 1000000

// 应用KEY
#define APP_KEY "x697889e34984d"
// 应用SECRET
#define APP_SECRET "c390a16a191070df91b38620d2381531"
// 应用LICENSE
#define LICENSE_KEY "3d86aeed470c944c84c282b70ea71500"

#define MODEL_FILE "/usr/local/xt/models/fcos_512x512_nv12.bin"

#define WEB_AGENT "19188888888"
#define SRS_AGENT "19199999999"

typedef struct {
	hbDNNTensor *payload;
	std::chrono::system_clock::time_point start_time;
} bpu_work;
typedef struct {
	int channel;
	int picWidth;
	int picHeight;
	PAYLOAD_TYPE_E enType;
	pthread_mutex_t init_lock;
	pthread_cond_t init_cond;
} SAMPLE_ATTR_S;

int g_msgid_cur = 0, g_is_transfer_to_mp4 = 0, g_index = 0, g_is_check_video_pulling = 0, g_is_check_video_pull_pid = 0, 
	g_is_sending = 0, g_is_video_has_started = 0,
	g_is_online = 0, g_xttp_login_times = 0;
char g_msg_ids[MSGID_NUM][33] = {0};
char g_channel_no[128] = {0};
char g_stream_url[1500] = {0};
char g_stream_protocol[16] = "uvc";
char g_rtsp_play_url[1500] = {0};
char g_rtsp_url[1200] = {0};
char g_rtsp_user[128] = {0};
char g_rtsp_pwd[128] = {0};
char g_rtsp_server_ip[512] = {0};
uint16_t g_rtsp_port = 0;
uint16_t g_v_width = 1920, g_v_height = 1080;
uint16_t g_download_port = 0;
uint16_t g_remote_server_port = 0;
uint32_t g_uidn = 0, g_ssrc = 0;
char g_remote_server_name[128] = {0};
char g_remote_file_path[128] = {0};
char g_peer_name[256] = {0};
char g_recv_msg[1500] = {0};
char g_sid[256] = {0};
char g_stream_name[256] = {0};
char g_web_server[32] = {0};
uint16_t g_web_port = 0;
uint8_t xftp_frame_buffer[1024*1024] = {0};

bpu_module *g_bpu_handle = NULL;
hb_vio_buffer_t g_feedback_buf;
hb_vio_buffer_t g_chn_3_out_buf;
std::atomic_bool fcos_finish;
std::deque<bpu_work> fcos_work_deque;
VIDEO_STREAM_S g_pstStream;

int g_eos = 0;
int g_count = 0;
int g_bufSize = 0;
int g_mmz_index = 0;
int g_mmz_cnt = 0;
int g_mmz_size = 0;
int g_mmz_stride = 0;
char* g_mmz_vaddr[5] = {NULL};
uint64_t g_mmz_paddr[5];

int g_vdecChn = 0;
int g_buf_is_alloc = 0;
int g_should_exit_main = 0;
int g_is_living = 0;
int g_feed_is_over = 1;
int g_do_post_is_over = 1;
int g_is_stop = 0;
uint32_t g_cur_bpu_ts = 0;
uint32_t g_frame_seqno = 0;
long g_bpu_and_push_exit_ts = 0;
pthread_t g_bpu_and_push_tid = 0;

int g_is_open_started = 0;
int g_vinChn = 0;
int g_is_uvc_running = 0;
pthread_t g_uvc_thread = 0;

// UVC相关函数
int start_uvc_stream(void);
void *uvc_thread_func(void *arg);
int init_venc(int width, int height);
int deinit_venc(void);
int yuv_to_h264_nv12(uint8_t *y_ptr, uint8_t *uv_ptr, uint8_t **h264_data, int *h264_len, int width, int height);
int start_bpu_and_push(void);
int send_stream_to_bpu(uint8_t *buffer, int len);
int uvc_nv12_to_vps(uint8_t *y_ptr, uint8_t *uv_ptr, uint32_t timestamp, int width, int height);

// HB VENC (GPU) state
static int g_venc_inited = 0;
static VENC_CHN g_venc_chn = 0;
// whether we allocated MMZ buffers for VENC
static int g_venc_mmz_alloc = 0;

void stop_session(void);
void myStopXttpCallback(void);
void video_session_did_stop_cb(void);

#ifdef __cplusplus
	extern "C" {
#endif
int ion_alloc_phy(int size, int *fd, char **vaddr, uint64_t * paddr);
#ifdef __cplusplus
	}
#endif

// 视频流推到流媒体服务器
int add_xftp_frame(const char *h264oraac, int insize, int type, uint32_t timestamp)
{
	uint8_t nalu_type = 0;
	uint8_t send_buffer[1500] = {0};
	uint16_t send_len = 0;

	if (!h264oraac || insize <= 0 || type <= 0) {
		fprintf(stderr, "[add_xftp_frame] error: h264oraac:%p, insize:%d, type:%d, g_start_vts:%ld, return -1;\n", h264oraac, insize, type, g_start_vts);
		return -1;
	}

	nalu_type = h264oraac[0] & 0x1F;
	if (nalu_type == 0x01 && insize < MIN_PACKET_SIZE) {
		memcpy(send_buffer, h264oraac, insize);
		send_len = MIN_PACKET_SIZE;
		MuxToXtvf((const char *)send_buffer, send_len, type, (int)timestamp);
	} else {
		MuxToXtvf(h264oraac, insize, type, (int)timestamp);
	}

	return 0;
}

// 视频编码相关定义

// 初始化视频编码器（使用 HB 硬件编码）
// Initialize HB GPU encoder channel
int init_venc(int width, int height)
{
	if (g_venc_inited) return 0;

	// Ensure VP is initialized (some platforms require VP before VENC)
	VP_CONFIG_S vpConf;
	memset(&vpConf, 0, sizeof(vpConf));
	vpConf.u32MaxPoolCnt = 32;
	HB_VP_SetConfig(&vpConf);
	int s32Ret = HB_VP_Init();
	if (s32Ret != 0) {
		fprintf(stderr, "[init_venc] HB_VP_Init failed s32Ret = %d !\n", s32Ret);
		// continue, sometimes VP may already be inited elsewhere
	}

	if (HB_VENC_Module_Init() != 0) {
		fprintf(stderr, "[init_venc] HB_VENC_Module_Init failed\n");
		return -1;
	}

	VENC_CHN_ATTR_S stChnAttr;
	memset(&stChnAttr, 0, sizeof(stChnAttr));
	stChnAttr.stVencAttr.enType = PT_H264;
	stChnAttr.stVencAttr.u32PicWidth = width;
	stChnAttr.stVencAttr.u32PicHeight = height;
	stChnAttr.stVencAttr.enMirrorFlip = DIRECTION_NONE;
	stChnAttr.stVencAttr.enRotation = CODEC_ROTATION_0;
	stChnAttr.stVencAttr.stCropCfg.bEnable = HB_FALSE;
	// Use NV12 input for VENC (supported values per HB VENC spec)
	stChnAttr.stVencAttr.enPixelFormat = HB_PIXEL_FORMAT_NV12;
	// Follow vendor sample: use external frame buffers and reasonable buffer counts
	stChnAttr.stVencAttr.bExternalFreamBuffer = HB_TRUE;
	// choose buffer counts aligned with sample code
	stChnAttr.stVencAttr.u32FrameBufferCount = 3;
	stChnAttr.stVencAttr.u32BitStreamBufferCount = 3;
	// set stream buffer size based on resolution (1024 aligned) like sample
	{
		int streambufSize = (width * height * 3 / 2 + 1024) & ~0x3ff;
		stChnAttr.stVencAttr.u32BitStreamBufSize = streambufSize;
	}
	stChnAttr.stVencAttr.bEnableUserPts = HB_TRUE;
	stChnAttr.stVencAttr.stAttrH264.h264_profile = (VENC_H264_PROFILE_E)0;
	stChnAttr.stVencAttr.stAttrH264.h264_level = (HB_H264_LEVEL_E)0;

	stChnAttr.stGopAttr.u32GopPresetIdx = 2;
	stChnAttr.stGopAttr.s32DecodingRefreshType = 2;

	stChnAttr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;

	g_venc_chn = 0;
	int ret = HB_VENC_CreateChn(g_venc_chn, &stChnAttr);
	if (ret != 0) {
		fprintf(stderr, "[init_venc] HB_VENC_CreateChn failed ret=%d\n", ret);
		HB_VENC_Module_Uninit();
		return -2;
	}

	// After creation, set RC params from defaults and adjust
	VENC_RC_ATTR_S *pstRcParam = &(stChnAttr.stRcAttr);
	s32Ret = HB_VENC_GetRcParam(g_venc_chn, pstRcParam);
	pstRcParam->stH264Cbr.u32BitRate = 3000; // kbps
	pstRcParam->stH264Cbr.u32FrameRate = 30;
	pstRcParam->stH264Cbr.u32IntraPeriod = 30;
	pstRcParam->stH264Cbr.u32VbvBufferSize = 3000;
	HB_VENC_SetChnAttr(g_venc_chn, &stChnAttr);

	// Allocate MMZ buffers for sending frames if not yet allocated (use aligned stride)
	if (g_mmz_cnt == 0) {
		int stride = ((width + 15) / 16) * 16; /* 16-align stride */
		g_mmz_stride = stride;
		g_mmz_size = stride * height * 3 / 2;
		g_mmz_cnt = 5;
		for (int i = 0; i < g_mmz_cnt; i++) {
			s32Ret = HB_SYS_Alloc(&g_mmz_paddr[i], (void **)&g_mmz_vaddr[i], g_mmz_size);
			if (s32Ret == 0) {
				fprintf(stderr, "[init_venc] mmzAlloc paddr = 0x%lx, vaddr = %p i = %d stride=%d size=%d\n", (unsigned long)g_mmz_paddr[i], g_mmz_vaddr[i], i, stride, g_mmz_size);
			}
		}
		g_venc_mmz_alloc = 1;
	} 

	VENC_RECV_PIC_PARAM_S recvParam;
	recvParam.s32RecvPicNum = 0; // unchangable
	ret = HB_VENC_StartRecvFrame(g_venc_chn, &recvParam);
	if (ret != 0) {
		fprintf(stderr, "[init_venc] HB_VENC_StartRecvFrame failed ret=%d\n", ret);
		HB_VENC_DestroyChn(g_venc_chn);
		HB_VENC_Module_Uninit();
		return -3;
	}

	g_venc_inited = 1;
	return 0;
}

int deinit_venc(void)
{
	if (!g_venc_inited) return 0;
	HB_VENC_StopRecvFrame(g_venc_chn);
	HB_VENC_DestroyChn(g_venc_chn);
	HB_VENC_Module_Uninit();

	// free MMZ buffers if we allocated them here
	if (g_venc_mmz_alloc && g_mmz_cnt > 0) {
		for (int i = 0; i < g_mmz_cnt; i++) {
			if (g_mmz_paddr[i] || g_mmz_vaddr[i]) {
				HB_SYS_Free(g_mmz_paddr[i], g_mmz_vaddr[i]);
				g_mmz_paddr[i] = 0;
				g_mmz_vaddr[i] = NULL;
			}
		}
		g_mmz_cnt = 0;
		g_mmz_size = 0;
		g_venc_mmz_alloc = 0;
	}

	g_venc_inited = 0;
	return 0;
}

int yuv_to_h264_nv12(uint8_t *y_ptr, uint8_t *uv_ptr, uint8_t **h264_data, 
		int *h264_len, int width, int height)
{
	if (!y_ptr || !uv_ptr || !h264_data || !h264_len) return -1;
	if (!g_venc_inited) {
		if (init_venc(width, height) != 0) return -2;
	}

	int y_size = width * height;
	int uv_size = width * height / 2;
	int total_size = y_size + uv_size;

	VIDEO_FRAME_S stFrame;
	memset(&stFrame, 0, sizeof(stFrame)); // 先清零，避免脏数据

	// Use allocated MMZ buffer if available
	int idx = g_mmz_index % g_mmz_cnt;
	if (g_mmz_cnt > 0 && g_mmz_vaddr[idx] != NULL && g_mmz_paddr[idx] != 0) {
		/* Use aligned stride and copy per-line to MMZ */
		int stride = g_mmz_stride ? g_mmz_stride : (((width + 15) / 16) * 16);
		int y_plane_size = stride * height;
		int uv_plane_size = stride * (height / 2); // NV12的UV平面高度是原图的1/2
		uint8_t *dst = (uint8_t *)g_mmz_vaddr[idx];
		/* copy Y plane line by line */
		for (int r = 0; r < height; r++) {
			memcpy(dst + r * stride, y_ptr + r * width, width);
		}
		/* copy UV plane line by line */
		uint8_t *dst_uv = dst + y_plane_size;
		for (int r = 0; r < height / 2; r++) {
			memcpy(dst_uv + r * stride, uv_ptr + r * width, width);
		}

		// ===================== 兼容SDK版本的修改（只保留支持的字段） =====================
		// 1. 填充YUV平面信息（NV12只有2个平面，第3个平面强制置0/NULL，核心修正）
		// Y平面（第0个平面）
		stFrame.stVFrame.phy_ptr[0] = g_mmz_paddr[idx];
		stFrame.stVFrame.vir_ptr[0] = (hb_char *)g_mmz_vaddr[idx];
		// UV交织平面（第1个平面）
		stFrame.stVFrame.phy_ptr[1] = g_mmz_paddr[idx] + y_plane_size;
		stFrame.stVFrame.vir_ptr[1] = (hb_char *)dst_uv;
		// NV12无第2个平面，必须置0/NULL（解决格式混乱问题，关键）
		stFrame.stVFrame.phy_ptr[2] = 0;
		stFrame.stVFrame.vir_ptr[2] = NULL;

		// 2. 填充帧核心参数（与Venc通道配置对齐，均为当前SDK支持的字段）
		stFrame.stVFrame.size = y_plane_size + uv_plane_size; // 总大小正确
		stFrame.stVFrame.width = width; // 与通道配置一致
		stFrame.stVFrame.height = height; // 与通道配置一致
		stFrame.stVFrame.pix_format = HB_PIXEL_FORMAT_NV12; // 格式正确，当前SDK支持
		stFrame.stVFrame.stride = stride; // 水平步长（对齐后的宽度）
		stFrame.stVFrame.vstride = height; // 垂直步长（使用SDK支持的默认值，避免报错）
		stFrame.stVFrame.pts = getTimeMsec(); // 时间戳（保留有效值，避免0值）
		stFrame.stVFrame.frame_end = HB_FALSE; // 保留原字段（若仍报错可删除，优先保证编译）

		// ===================== 兼容修改结束 =====================

		int ret = HB_VENC_SendFrame(g_venc_chn, &stFrame, 2000);
		if (ret != 0) {
			fprintf(stderr, "[yuv_to_h264_nv12] HB_VENC_SendFrame failed g_venc_chn=%d, ret=%d\n", 
						g_venc_chn, ret);
			return -3;
		}

		g_mmz_index = (g_mmz_index + 1) % g_mmz_cnt;

		VIDEO_STREAM_S stStream;
		memset(&stStream, 0, sizeof(stStream));
		ret = HB_VENC_GetStream(g_venc_chn, &stStream, 2000);
		if (ret != 0) {
			fprintf(stderr, "[yuv_to_h264_nv12] HB_VENC_GetStream failed ret=%d\n", ret);
			return -4;
		}

		uint32_t sz = stStream.pstPack.size;
		hb_char *src = stStream.pstPack.vir_ptr;
		if (sz > 0 && src) {
			*h264_data = (uint8_t *)malloc(sz);
			if (!*h264_data) {
				HB_VENC_ReleaseStream(g_venc_chn, &stStream);
				return -5;
			}
			memcpy(*h264_data, src, sz);
			*h264_len = sz;
		} else {
			*h264_data = NULL;
			*h264_len = 0;
		}

		HB_VENC_ReleaseStream(g_venc_chn, &stStream);
		return 0;
	} else {
		// fallback: use vir pointers directly (legacy behavior)
		stFrame.stVFrame.vir_ptr[0] = (hb_char *)y_ptr;
		stFrame.stVFrame.vir_ptr[1] = (hb_char *)uv_ptr;
		stFrame.stVFrame.vir_ptr[2] = NULL; // 修正：第2平面置NULL，兼容SDK
		stFrame.stVFrame.phy_ptr[0] = 0;
		stFrame.stVFrame.phy_ptr[1] = 0;
		stFrame.stVFrame.phy_ptr[2] = 0;
		stFrame.stVFrame.size = total_size;
		stFrame.stVFrame.width = width;
		stFrame.stVFrame.height = height;
		stFrame.stVFrame.pix_format = HB_PIXEL_FORMAT_NV12; 
		stFrame.stVFrame.stride = width;
		stFrame.stVFrame.vstride = height;
		stFrame.stVFrame.pts = getTimeMsec();
		stFrame.stVFrame.frame_end = HB_FALSE;

		int ret = HB_VENC_SendFrame(g_venc_chn, &stFrame, 2000);
		if (ret != 0) {
			fprintf(stderr, "[yuv_to_h264_nv12] HB_VENC_SendFrame failed ret=%d\n", ret);
			return -3;
		}

		VIDEO_STREAM_S stStream;
		memset(&stStream, 0, sizeof(stStream));
		ret = HB_VENC_GetStream(g_venc_chn, &stStream, 2000);
		if (ret != 0) {
			fprintf(stderr, "[yuv_to_h264_nv12] HB_VENC_GetStream failed ret=%d\n", ret);
			return -4;
		}

		uint32_t sz = stStream.pstPack.size;
		hb_char *src = stStream.pstPack.vir_ptr;
		if (sz > 0 && src) {
			*h264_data = (uint8_t *)malloc(sz);
			if (!*h264_data) {
				HB_VENC_ReleaseStream(g_venc_chn, &stStream);
				return -5;
			}
			memcpy(*h264_data, src, sz);
			*h264_len = sz;
		} else {
			*h264_data = NULL;
			*h264_len = 0;
		}

		HB_VENC_ReleaseStream(g_venc_chn, &stStream);
		return 0;
	}
}

// UVC线程函数
static void yuyv_to_nv12(const uint8_t *src, uint8_t *dst, int width, int height)
{
	uint8_t *y = dst;
	uint8_t *uv = dst + width * height;
	for (int j = 0; j < height; j += 2) {
		for (int i = 0; i < width; i += 2) {
			int idx00 = (j * width + i) * 2;
			int idx01 = idx00 + 2;
			int idx10 = idx00 + width * 2;
			int idx11 = idx10 + 2;

			uint8_t y00 = src[idx00];
			uint8_t u00 = src[idx00 + 1];
			uint8_t y01 = src[idx01];
			uint8_t v00 = src[idx01 + 1];

			uint8_t y10 = src[idx10];
			uint8_t u10 = src[idx10 + 1];
			uint8_t y11 = src[idx11];
			uint8_t v10 = src[idx11 + 1];

			y[j * width + i] = y00;
			y[j * width + i + 1] = y01;
			y[(j + 1) * width + i] = y10;
			y[(j + 1) * width + i + 1] = y11;

			uint8_t u = (uint8_t)(((int)u00 + (int)u10) / 2);
			uint8_t v = (uint8_t)(((int)v00 + (int)v10) / 2);

			int uv_index = (j / 2) * width + i;
			uv[uv_index] = u;
			uv[uv_index + 1] = v;
		}
	}
}

void *uvc_thread_func(void *arg)
{
	int ret;
	int rt = 0;
	enum v4l2_buf_type v4l2_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	uint8_t *h264_data = NULL;
	int h264_len = 0;
	uint32_t timestamp;
	int uvc_fd = -1;
	struct v4l2_format fmt;
	int frame_size = g_v_width * g_v_height * 3 / 2; // 默认 NV12 大小

	// 定义缓冲区结构
	struct buffer {
		void *start;
		size_t length;
	};

	struct buffer *buffers = NULL;
	unsigned int n_buffers = 0;
	int actual_pixfmt = V4L2_PIX_FMT_NV12;


	// 打开UVC设备
	uvc_fd = open("/dev/video8", O_RDWR | O_NONBLOCK);
	if (uvc_fd < 0) {
		fprintf(stderr, "[uvc_thread_func] Failed to open /dev/video0: %s\n", strerror(errno));
		goto exit;
	}

	// 设置视频格式，优先 NV12，失败则尝试 YUYV
	memset(&fmt, 0, sizeof(fmt));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = g_v_width;
	fmt.fmt.pix.height = g_v_height;
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12;
	fmt.fmt.pix.field = V4L2_FIELD_NONE;

	if (ioctl(uvc_fd, VIDIOC_S_FMT, &fmt) < 0) {
		fprintf(stderr, "[uvc_thread_func] VIDIOC_S_FMT NV12 failed: %s, try YUYV\n", strerror(errno));
		// 尝试 YUYV
		memset(&fmt, 0, sizeof(fmt));
		fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		fmt.fmt.pix.width = g_v_width;
		fmt.fmt.pix.height = g_v_height;
		fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
		fmt.fmt.pix.field = V4L2_FIELD_NONE;
		if (ioctl(uvc_fd, VIDIOC_S_FMT, &fmt) < 0) {
			fprintf(stderr, "[uvc_thread_func] VIDIOC_S_FMT YUYV also failed: %s\n", strerror(errno));
			goto exit;
		} else {
			actual_pixfmt = V4L2_PIX_FMT_YUYV;
		}
	} else {
		actual_pixfmt = fmt.fmt.pix.pixelformat;
	}

	frame_size = fmt.fmt.pix.sizeimage;

	// 请求缓冲区
	struct v4l2_requestbuffers req;
	memset(&req, 0, sizeof(req));
	req.count = 4;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	if (ioctl(uvc_fd, VIDIOC_REQBUFS, &req) < 0) {
		fprintf(stderr, "[uvc_thread_func] Failed to request buffers: %s\n", strerror(errno));
		goto exit;
	}

	// 分配缓冲区
	buffers = (struct buffer *)calloc(req.count, sizeof(struct buffer));
	if (!buffers) {
		fprintf(stderr, "[uvc_thread_func] Out of memory\n");
		goto exit;
	}

	// 映射缓冲区
	for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
		struct v4l2_buffer buf;
		memset(&buf, 0, sizeof(buf));

		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = n_buffers;

		if (ioctl(uvc_fd, VIDIOC_QUERYBUF, &buf) < 0) {
			fprintf(stderr, "[uvc_thread_func] Failed to query buffer: %s\n", strerror(errno));
			goto exit;
		}

		buffers[n_buffers].length = buf.length;
		buffers[n_buffers].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, uvc_fd, buf.m.offset);

		if (MAP_FAILED == buffers[n_buffers].start) {
			fprintf(stderr, "[uvc_thread_func] Failed to mmap buffer: %s\n", strerror(errno));
			goto exit;
		}
	}

	// 将缓冲区放入队列
	for (unsigned int i = 0; i < n_buffers; ++i) {
		struct v4l2_buffer buf;
		memset(&buf, 0, sizeof(buf));

		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;

		if (ioctl(uvc_fd, VIDIOC_QBUF, &buf) < 0) {
			fprintf(stderr, "[uvc_thread_func] Failed to queue buffer: %s\n", strerror(errno));
			goto exit;
		}
	}

	// 启动流
	if (ioctl(uvc_fd, VIDIOC_STREAMON, &v4l2_type) < 0) {
		fprintf(stderr, "[uvc_thread_func] Failed to start stream: %s\n", strerror(errno));
		goto exit;
	}

	// 初始化视频编码器
	ret = init_venc(g_v_width, g_v_height);
	if (ret != 0) {
		fprintf(stderr, "[uvc_thread_func] init_venc failed, ret=%d\n", ret);
		goto exit;
	}

	// 主循环
	while (g_is_running && !g_should_exit_main) {
		fd_set fds;
		struct timeval tv;
		int r;

		FD_ZERO(&fds);
		FD_SET(uvc_fd, &fds);

		// 设置超时时间
		tv.tv_sec = 2;
		tv.tv_usec = 0;

		r = select(uvc_fd + 1, &fds, NULL, NULL, &tv);
		if (r < 0) {
			if (errno == EINTR) {
				continue;
			}
			fprintf(stderr, "[uvc_thread_func] select error: %s\n", strerror(errno));
			break;
		} else if (r == 0) {
			fprintf(stderr, "[uvc_thread_func] select timeout\n");
			continue;
		}

		// 读取缓冲区
		struct v4l2_buffer buf;
		memset(&buf, 0, sizeof(buf));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;

		if (ioctl(uvc_fd, VIDIOC_DQBUF, &buf) < 0) {
			if (errno == EAGAIN) {
				continue;
			}
			fprintf(stderr, "[uvc_thread_func] Failed to dequeue buffer: %s\n", strerror(errno));
			break;
		}

		// 开启视频帧解码并进行推理线程
		if (g_is_open_started == 0) {
			g_is_open_started = 1;
			ret = start_bpu_and_push();
			fprintf(stderr, "[uvc_thread_func] start_bpu_and_push(0) = %d\n", ret);
		}

		timestamp = getTimeMsec();

		// 获取帧数据并根据实际像素格式处理
		uint8_t *nv12_tmp = NULL;
		uint8_t *y_ptr = NULL;
		uint8_t *uv_ptr = NULL;

		if (actual_pixfmt == V4L2_PIX_FMT_NV12) {
			y_ptr = (uint8_t *)buffers[buf.index].start;
			uv_ptr = y_ptr + g_v_width * g_v_height;
		} else {
			fprintf(stderr, "[uvc_thread_func] Unsupported pixel format: 0x%x\n", actual_pixfmt);
			if (ioctl(uvc_fd, VIDIOC_QBUF, &buf) < 0) {
				fprintf(stderr, "[uvc_thread_func] Failed to queue buffer: %s\n", strerror(errno));
				break;
			}
			continue;
		}

		// 直接将UVC的NV12数据送入VPS处理，跳过解码步骤
		uint32_t bpu_timestamp = timestamp - g_start_vts;
		ret = uvc_nv12_to_vps(y_ptr, uv_ptr, bpu_timestamp, g_v_width, g_v_height);
		if (ret != 0) {
			fprintf(stderr, "[uvc_thread_func] uvc_nv12_to_vps failed, ret=%d\n", ret);
		}

		// 将NV12数据编码为H264
		ret = yuv_to_h264_nv12(y_ptr, uv_ptr, &h264_data, &h264_len, g_v_width, g_v_height);
		if (ret != 0 || !h264_data || h264_len <= 0) {
			fprintf(stderr, "[uvc_thread_func] yuv_to_h264 failed, ret=%d\n", ret);
			if (nv12_tmp) free(nv12_tmp);
			// 放回缓冲区
			if (ioctl(uvc_fd, VIDIOC_QBUF, &buf) < 0) {
				fprintf(stderr, "[uvc_thread_func] Failed to queue buffer: %s\n", strerror(errno));
				break;
			}
			continue;
		}

		// 处理编码后的H264数据
		if (h264_data && h264_len > 0) {
			// 将视频帧推送到流媒体服务器
			add_xftp_frame((char *)h264_data, h264_len, XTVF_VIDEO_TYPE, bpu_timestamp);
		}

		free(h264_data);
		h264_data = NULL;
		if (nv12_tmp) { free(nv12_tmp); nv12_tmp = NULL; }

		// 放回缓冲区
		if (ioctl(uvc_fd, VIDIOC_QBUF, &buf) < 0) {
			fprintf(stderr, "[uvc_thread_func] Failed to queue buffer: %s\n", strerror(errno));
			break;
		}
	}

exit:
	// 停止流
	if (uvc_fd >= 0) {
		if (ioctl(uvc_fd, VIDIOC_STREAMOFF, &v4l2_type) < 0) {
			fprintf(stderr, "[uvc_thread_func] Failed to stop stream: %s\n", strerror(errno));
		}
	}

	// 释放缓冲区
	if (buffers) {
		for (unsigned int i = 0; i < n_buffers; ++i) {
			if (buffers[i].start) {
				munmap(buffers[i].start, buffers[i].length);
			}
		}
		free(buffers);
		buffers = NULL;
	}

	// 关闭设备
	if (uvc_fd >= 0) {
		close(uvc_fd);
		uvc_fd = -1;
	}

	// 停止编码器
	deinit_venc();

	video_session_did_stop_cb();

	fprintf(stderr, "[uvc_thread_func] exit\n");
	return NULL;
}

// 启动UVC视频流
int start_uvc_stream(void)
{
    int ret;
	// 预先创建/截断本地 H264 输出文件，确保文件可见
	// FILE *fp = fopen("/tmp/uvc_output.h264", "wb");
	// if (fp) fclose(fp);
	FILE *fp = NULL;
	// 创建UVC线程
	ret = pthread_create(&g_uvc_thread, NULL, uvc_thread_func, NULL);
    if (ret != 0) {
        fprintf(stderr, "[start_uvc_stream] pthread_create failed, ret=%d\n", ret);
        return -1;
    }

	g_is_uvc_running = 1;
	fprintf(stderr, "[start_uvc_stream] uvc thread started, tid=%lu\n", (unsigned long)g_uvc_thread);
	return 0;
}
// 推理结果推到流媒体服务器
int add_script_frame(const char *script_data, int script_len, int inner_type, uint32_t timestamp)
{
	if (!script_data || script_len <= 0) {
		fprintf(stderr, "[add_script_frame] error: script_data:%p, insize:%d, inner_type:%d, return -1;\n", script_data, script_len, inner_type);
		return -1;
	}

	return MuxScriptToXtvf(script_data, script_len, inner_type, timestamp);
}

// 分配内存空间
int prepare_user_buf(void *buf, uint32_t size_y, uint32_t size_uv)
{
	int ret;
	hb_vio_buffer_t *buffer = (hb_vio_buffer_t *)buf;

	if (!buffer) {
		return -1;
	}

	buffer->img_info.fd[0] = ion_open();
	buffer->img_info.fd[1] = ion_open();
	ret = ion_alloc_phy(size_y, &buffer->img_info.fd[0], &buffer->img_addr.addr[0], &buffer->img_addr.paddr[0]);
	if (ret) {
		fprintf(stderr, "prepare user buf error 1\n");
		return ret;
	}
	ret = ion_alloc_phy(size_uv, &buffer->img_info.fd[1], &buffer->img_addr.addr[1], &buffer->img_addr.paddr[1]);
	if (ret) {
		fprintf(stderr, "prepare user buf error 2\n");
		return ret;
	}

	return 0;
}
// 初始化 vps
void vps_small_init(void)
{
	VPS_GRP_ATTR_S grp_attr;
	VPS_CHN_ATTR_S chn_3_attr;

	memset(&grp_attr, 0, sizeof(VPS_GRP_ATTR_S));
	grp_attr.maxW = g_v_width;
	grp_attr.maxH = g_v_height;
	grp_attr.frameDepth = 8;
	HB_VPS_CreateGrp(0, &grp_attr);
	HB_SYS_SetVINVPSMode(0, VIN_OFFLINE_VPS_OFFINE);

	memset(&chn_3_attr, 0, sizeof(VPS_CHN_ATTR_S));
	chn_3_attr.enScale = 1;
	chn_3_attr.width = 512;
	chn_3_attr.height = 512;
	chn_3_attr.frameDepth = 8;
	HB_VPS_SetChnAttr(0, 3, &chn_3_attr);
	HB_VPS_EnableChn(0, 3);
	HB_VPS_StartGrp(0);
}
// 释放 vps
void vps_small_release(hb_vio_buffer_t* chn_3_out_buf)
{
	HB_VPS_DisableChn(0, 3);
	HB_VPS_StopGrp(0);
	HB_VPS_DestroyGrp(0);
}
// 获取推理结果推送给流媒体服务器
void fcos_do_post(void)
{
	bpu_image_info_t image_info;
	int i, rt = 0;
	ANNOTATION_ITEM this_item;
	ANNOTATION_ITEM_SET readout_set;
	uint8_t enc_buff[1500] = {0};
	int enc_buff_len = 0;

	g_do_post_is_over = 0;
	if (init_annotation_item_arr(&g_annotation_item_set)) {
		fprintf(stderr, "[fcos_do_post]--init_annotation_item_arr failed, exit\n");
		g_do_post_is_over = 1;
		return;
	}

	fprintf(stderr, "[fcos_do_post]--start...\n");
	image_info.m_model_h = 512;
	image_info.m_model_w = 512;
	image_info.m_ori_height = g_v_height;
	image_info.m_ori_width = g_v_width;
	std::vector<Detection> results; // 存储识别到的结果
	do {
		while (!fcos_work_deque.empty() && !g_is_stop) {
			results.clear();
			if (g_is_stop) {
				break;
			}

			auto work = fcos_work_deque.front();
			auto output = work.payload;
			auto stime = work.start_time;
			fcos_post_process(output, &image_info, results); // 从推理结果中获取识别到的结果
			fcos_work_deque.pop_front();
			if (reset_annotation_item_arr(&g_annotation_item_set)) {
				fprintf(stderr, "[fcos_do_post]--init_annotation_item_arr failed, break!\n");
				break;
			}
			for (i = 0; i < results.size(); i++) {
				this_item.id = results[i].id;
				this_item.conf_level_1m = results[i].score * ONE_MILLION_BASE;
				this_item.xmin_1m = results[i].bbox.xmin * ONE_MILLION_BASE;
				this_item.ymin_1m = results[i].bbox.ymin * ONE_MILLION_BASE;
				this_item.xmax_1m = results[i].bbox.xmax * ONE_MILLION_BASE;
				this_item.ymax_1m = results[i].bbox.ymax * ONE_MILLION_BASE;
				rt = add_annotioan_item_to_set(&g_annotation_item_set, &this_item);
				// fprintf(stderr, "timestamp=%u, i:%ld, id=%d->(%s, %f) <(%f, %f), (%f, %f)>\n", g_cur_bpu_ts, i, 
				// 			results[i].id, results[i].class_name, results[i].score, results[i].bbox.xmin, results[i].bbox.ymin,
				// 			results[i].bbox.xmax, results[i].bbox.ymax);
			}

			if (g_annotation_item_set.annotion_item_len) {
				g_annotation_item_set.anno_ts = g_cur_bpu_ts;
				// 将识别结果编码成xftp格式
				if (!(enc_buff_len = encode_annotation_set_buff(&g_annotation_item_set, enc_buff))) {
					fprintf(stderr, "[fcos_do_post] ERROR: failed in encode_annotation_set_buff\n");
				} else {
					// 将识别结果推送到流媒体服务器
					rt = add_script_frame((char *)enc_buff, enc_buff_len, SCRIPT_INNER_TYPE, g_cur_bpu_ts);
					// fprintf(stderr, "[fcos_do_post] add_script_frame=%d, enc_buff_len=%d, timestamp=%u\n", rt, enc_buff_len, g_cur_bpu_ts);
				}
			}
		}
	} while (!fcos_finish);
	fprintf(stderr, "[fcos_do_post]--exit--thread\n");
	g_do_post_is_over = 1;
}
// 从 VPS 获取缩放后的图像并BPU进行推理
void fcos_feed_bpu(void)
{
	int i, ret = 0;
	uint32_t size_y, size_uv;
	char * dsc = NULL;
	FRAME_INFO f_info;
	
	hbDNNTensor output_tensors[5][15];
	int cur_ouput_buf_idx = 0;
	g_feed_is_over = 0;
	fprintf(stderr, "[fcos_feed_bpu] start before sp_init_bpu_tensors\n");

	for (i = 0; i < 5; i++) {
		sp_init_bpu_tensors(g_bpu_handle, output_tensors[i]);
	}
	fprintf(stderr, "[fcos_feed_bpu] after sp_init_bpu_tensors\n");
	while (!g_is_stop) {
		bpu_work fcos_work;
		ret = HB_VPS_GetChnFrame(0, 3, &g_chn_3_out_buf, 2000);
		if (ret) {
			fprintf(stderr, "[fcos_feed_bpu] HB_VPS_GetChnFrame fail,ret = %d\n", ret);
			continue;
		}
		ret = frame_cir_buff_dequeue(&g_frame_cir_buff, &f_info);
		if (ret) {
			fprintf(stderr, "[fcos_feed_bpu]--failed--continue\n");
			continue;
		}
		g_cur_bpu_ts = f_info.timestamp;
		if (g_is_stop) {
			fprintf(stderr, "[fcos_feed_bpu]--break\n");
			break;
		}
		if (!dsc) {
			if (g_chn_3_out_buf.img_addr.width && g_chn_3_out_buf.img_addr.height) {
				dsc = (char*)calloc(FRAME_BUFFER_SIZE(g_chn_3_out_buf.img_addr.width, g_chn_3_out_buf.img_addr.height), 1);
				if (!dsc) {
					fprintf(stderr,"[fcos_feed_bpu] Failed to malloc dsc\n");
					goto END;
				}
				fprintf(stderr,"[fcos_feed_bpu] malloc dsc success\n");
			} else {
				fprintf(stderr,"[fcos_feed_bpu] Failed to malloc dsc(no width, no height)\n");
				goto END;
			}
		}
		memcpy((void*)dsc, g_chn_3_out_buf.img_addr.addr[0], g_chn_3_out_buf.img_addr.width * g_chn_3_out_buf.img_addr.height);
		memcpy((void*)(dsc + g_chn_3_out_buf.img_addr.width * g_chn_3_out_buf.img_addr.height), g_chn_3_out_buf.img_addr.addr[1], g_chn_3_out_buf.img_addr.width * g_chn_3_out_buf.img_addr.height / 2);
		g_bpu_handle->output_tensor = &output_tensors[cur_ouput_buf_idx][0];//get an tensor buffer from ring buffer
		//fprintf(stderr,"[fcos_feed_bpu] dsc cur_ouput_buf_idx:%d\n", cur_ouput_buf_idx);
		fcos_work.start_time = std::chrono::high_resolution_clock::now();
		sp_bpu_start_predict(g_bpu_handle, dsc);//start bpu predict
		fcos_work.payload = g_bpu_handle->output_tensor;//bpu processed tensor
		// fprintf(stderr, "fcos_work_deque.push_back--start_time=%ld, timestamp = %u\n", fcos_work.start_time, g_cur_bpu_ts);
		fcos_work_deque.push_back(fcos_work);//push back work struct to deque
		cur_ouput_buf_idx++;
		cur_ouput_buf_idx %= 5;

		HB_VPS_ReleaseChnFrame(0, 3, &g_chn_3_out_buf);
	}

	END:
	vps_small_release(&g_chn_3_out_buf);
	free(dsc);
	fcos_finish = true;
	for (size_t i = 0; i < 5; i++) {
		sp_deinit_bpu_tensor(output_tensors[i], 15);
	}
	fprintf(stderr, "[fcos_feed_bpu]--exit thread\n");
	g_feed_is_over = 1;
}
// 解码后视频帧发送到 vps 进行尺寸缩放
int vps_small_process(VIDEO_FRAME_S* stFrameInfo)
{
	int img_in_fd = 0, ret;
	static uint32_t size_y, size_uv;
	char file_name[64]; 
	
	if (!g_buf_is_alloc) {
		memset(&g_feedback_buf, 0, sizeof(hb_vio_buffer_t));
		size_y = g_v_width * g_v_height;
		size_uv = size_y / 2;
		ret = prepare_user_buf(&g_feedback_buf, size_y, size_uv);
		if (ret) {
			fprintf(stderr, "vps_small_process prepare_user_buf fail...\n");
			return -1;
		}
		g_feedback_buf.img_info.planeCount = 2;
		g_feedback_buf.img_info.img_format = 8;
		g_feedback_buf.img_addr.width = g_v_width;
		g_feedback_buf.img_addr.height = g_v_height;
		g_feedback_buf.img_addr.stride_size = g_v_width;
		g_buf_is_alloc = 1;
	}
	memcpy(g_feedback_buf.img_addr.addr[0], stFrameInfo->stVFrame.vir_ptr[0], size_y);
	memcpy(g_feedback_buf.img_addr.addr[1], stFrameInfo->stVFrame.vir_ptr[1], size_uv);
	ret = HB_VPS_SendFrame(0, &g_feedback_buf, -1);
	if (ret) {
		fprintf(stderr, "vps_small_process HB_VPS_SendFrame fail...\n");
		return -2;
	}
	
	return 0;
}

// 直接将UVC的NV12数据送入VPS处理
int uvc_nv12_to_vps(uint8_t *y_ptr, uint8_t *uv_ptr, uint32_t timestamp, int width, int height)
{
	int ret;
	static uint32_t size_y, size_uv;
	
	if (!g_buf_is_alloc) {
		memset(&g_feedback_buf, 0, sizeof(hb_vio_buffer_t));
		size_y = width * height;
		size_uv = size_y / 2;
		ret = prepare_user_buf(&g_feedback_buf, size_y, size_uv);
		if (ret) {
			fprintf(stderr, "uvc_nv12_to_vps prepare_user_buf fail...\n");
			return -1;
		}
		g_feedback_buf.img_info.planeCount = 2;
		g_feedback_buf.img_info.img_format = 8; // NV12格式
		g_feedback_buf.img_addr.width = width;
		g_feedback_buf.img_addr.height = height;
		g_feedback_buf.img_addr.stride_size = width;
		g_buf_is_alloc = 1;
	}
	
	// 直接将UVC的NV12数据复制到VPS缓冲区
	size_y = width * height;
	size_uv = size_y / 2;
	memcpy(g_feedback_buf.img_addr.addr[0], y_ptr, size_y);
	memcpy(g_feedback_buf.img_addr.addr[1], uv_ptr, size_uv);
	
	// 发送到VPS处理
	ret = HB_VPS_SendFrame(0, &g_feedback_buf, -1);
	if (ret) {
		fprintf(stderr, "uvc_nv12_to_vps HB_VPS_SendFrame fail...\n");
		return -2;
	}
	
	// 记录关键帧信息用于推理结果同步
	// 对于UVC流，我们可以将所有帧都视为关键帧，或者根据需要调整
	FRAME_INFO f_info;
	f_info.timestamp = timestamp;
	f_info.seqno = g_frame_seqno++;
	ret = frame_cir_buff_enqueue(&g_frame_cir_buff, &f_info);
	if (ret) {
		fprintf(stderr, "uvc_nv12_to_vps frame_cir_buff_enqueue fail...\n");
		return -3;
	}
	
	return 0;
}
// 获取解码后的图像并交给vps处理（UVC流不需要此功能）
void *get_decode_data(void *attr)
{
	// UVC流直接将NV12数据送入VPS，不需要此函数
	fprintf(stderr, "[get_decode_data] UVC stream does not use this function\n");
	pthread_exit(NULL);
	return 0;
}
// 将 h264 视频帧推给解码器（UVC流不需要此功能）
int send_stream_to_bpu(uint8_t *buffer, int len)
{
	// UVC流直接使用NV12数据，不需要将H264推给解码器
	fprintf(stderr, "[send_stream_to_bpu] UVC stream does not use this function\n");
	return 0;
}
// 初始化解码器channel
int sample_vdec_ChnAttr_init(VDEC_CHN_ATTR_S *pVdecChnAttr, PAYLOAD_TYPE_E enType, int picWidth, int picHeight)
{
	int streambufSize = 0;
	if (!pVdecChnAttr) {
		fprintf(stderr, "pVdecChnAttr is NULL!\n");
		return -1;
	}
	// 该步骤必不可少
	memset(pVdecChnAttr, 0, sizeof(VDEC_CHN_ATTR_S));
	// 设置解码模式分别为 PT_H264 PT_H265 PT_MJPEG 
	pVdecChnAttr->enType = enType;
	// 设置解码模式为帧模式
	pVdecChnAttr->enMode = VIDEO_MODE_FRAME;
	// 设置像素格式 NV12格式
	// Use NV12 as decoder output pixel format
	pVdecChnAttr->enPixelFormat = HB_PIXEL_FORMAT_NV12;
	// 输入buffer个数
	pVdecChnAttr->u32FrameBufCnt = 3;
	// 输出buffer个数
	pVdecChnAttr->u32StreamBufCnt = 3;
	// 输出buffer size，必须1024对齐
	pVdecChnAttr->u32StreamBufSize = (picWidth * picHeight * 3 / 2 + 1024) &~ 0x3ff;
	// 使用外部buffer
	pVdecChnAttr->bExternalBitStreamBuf  = HB_TRUE;
	if (enType == PT_H265) {
		// 使能带宽优化
		pVdecChnAttr->stAttrH265.bandwidth_Opt = HB_TRUE;
		// 普通解码模式，IPB帧解码
		pVdecChnAttr->stAttrH265.enDecMode = VIDEO_DEC_MODE_NORMAL;
		// 输出顺序按照播放顺序输出
		pVdecChnAttr->stAttrH265.enOutputOrder = VIDEO_OUTPUT_ORDER_DISP;
		// 不启用CLA作为BLA处理
		pVdecChnAttr->stAttrH265.cra_as_bla = HB_FALSE;
		// 配置tempral id为绝对模式
		pVdecChnAttr->stAttrH265.dec_temporal_id_mode = 0;
		// 保持2
		pVdecChnAttr->stAttrH265.target_dec_temporal_id_plus1 = 2;
	}
	if (enType == PT_H264) {
		// 使能带宽优化
		pVdecChnAttr->stAttrH264.bandwidth_Opt = HB_TRUE;
		// 普通解码模式，IPB帧解码
		pVdecChnAttr->stAttrH264.enDecMode = VIDEO_DEC_MODE_NORMAL;
		// 输出顺序按照解码顺序输出
		pVdecChnAttr->stAttrH264.enOutputOrder = VIDEO_OUTPUT_ORDER_DEC;
	}
	if (enType == PT_JPEG) {
		// 使用内部buffer
		pVdecChnAttr->bExternalBitStreamBuf  = HB_FALSE;
		// 配置镜像模式，不镜像
		pVdecChnAttr->stAttrJpeg.enMirrorFlip = DIRECTION_NONE;
		// 配置旋转模式，不旋转
		pVdecChnAttr->stAttrJpeg.enRotation = CODEC_ROTATION_0;
		// 配置crop，不启用
		pVdecChnAttr->stAttrJpeg.stCropCfg.bEnable = HB_FALSE;
	}
	return 0;
}
// 初始化解码器
int vdecode_init(void *attr)
{
	int s32Ret;
	SAMPLE_ATTR_S *sample_attr;
	VDEC_CHN_ATTR_S vdecChnAttr;

	sample_attr = (SAMPLE_ATTR_S *)attr;
	g_vdecChn = sample_attr->channel;
	pthread_mutex_lock(&sample_attr->init_lock);
	// 初始化channel属性
	s32Ret = sample_vdec_ChnAttr_init(&vdecChnAttr, sample_attr->enType, sample_attr->picWidth, sample_attr->picHeight);
	if (s32Ret) {
		fprintf(stderr, "[vdecode_init] sample_vdec_ChnAttr_init failded: %d\n", s32Ret);
		pthread_exit(NULL);
		return 0;
	}
	// 创建channel
	s32Ret = HB_VDEC_CreateChn(g_vdecChn, &vdecChnAttr);
	if (s32Ret != 0) {
		fprintf(stderr, "[vdecode_init] HB_VDEC_CreateChn %d failed, %x.\n", g_vdecChn, s32Ret);
		pthread_exit(NULL);
		return 0;
	}
	// 设置channel属性
	s32Ret = HB_VDEC_SetChnAttr(g_vdecChn, &vdecChnAttr);
	if (s32Ret != 0) {
		fprintf(stderr, "[vdecode_init] HB_VDEC_SetChnAttr failed\n");
		pthread_exit(NULL);
		return 0;
	}
	// 启动解码器接收视频帧
	s32Ret = HB_VDEC_StartRecvStream(g_vdecChn);
	if (s32Ret != 0) {
		fprintf(stderr, "[vdecode_init] HB_VDEC_StartRecvStream failed\n");
		pthread_exit(NULL);
		return 0;
	}
	pthread_cond_signal(&sample_attr->init_cond);
	pthread_mutex_unlock(&sample_attr->init_lock);
	fprintf(stderr, "[vdecode_init] end...\n");

	return 0;
}
// 初始化vps/bpu（UVC流不需要解码器）
int init_decode(void)
{
	int s32Ret, i;
	VP_CONFIG_S vpConf;

	memset(&vpConf, 0, sizeof(VP_CONFIG_S));

	// 初始化VP（视频处理）模块
	vpConf.u32MaxPoolCnt = 32;
	HB_VP_SetConfig(&vpConf);
	s32Ret = HB_VP_Init();
	fprintf(stderr, "[init_decode] HB_VP_Init s32Ret = %d !\n", s32Ret);

	frame_cir_buff_init(&g_frame_cir_buff);
	g_eos = 0;
	g_count = 0;
	g_bufSize = 0;
	g_mmz_index = 0;
	memset(&g_pstStream, 0, sizeof(VIDEO_STREAM_S));

	// 初始化vps
	vps_small_init();
	// 初始化模型文件
	g_bpu_handle = sp_init_bpu_module(MODEL_FILE);
	fprintf(stderr, "[init_decode] sp_init_bpu_module g_bpu_handle = %p\n", g_bpu_handle);    

	// 启动从VPS获取缩放后的图像并BPU进行推理的线程
	std::thread t1(fcos_feed_bpu);
	// 启动获取推理结果推送给流媒体服务器的线程
	std::thread t2(fcos_do_post);

	// 只等待BPU相关线程结束，跳过解码器线程
	fprintf(stderr, "[init_decode] before t1.join, t2.join ... ...\n");
	t1.join();
	t2.join();

	sp_release_bpu_module(g_bpu_handle);
	fprintf(stderr, "[init_decode] after sp_release_bpu_module\n");

	s32Ret = HB_VP_Exit();
	fprintf(stderr, "[init_decode] HB_VP_Exit: s32Ret = %d. Done !\n", s32Ret);

	return 0;
}
// 频帧解码并进行推理的执行线程
void *bpu_and_push(void *arg)
{
	int rt;
	char *url;

	init_decode();
	fprintf(stderr, "[bpu_and_push] after sp_release_vio_module, g_should_exit_main=%d\n", g_should_exit_main);
	if (g_should_exit_main) {
		g_is_running = 0;
	}

	g_is_stop = 0;
	fcos_finish = false;
	fcos_work_deque.clear();

	fprintf(stderr, "[bpu_and_push] Exit\n");
	g_bpu_and_push_tid = 0;
	g_bpu_and_push_exit_ts = getTimeMsec();
	pthread_exit(NULL);

	return 0;
}
// 开启视频帧解码并进行推理线程
int start_bpu_and_push(void)
{
	pthread_t pid;
	pthread_attr_t attr;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	fprintf(stderr, "[start_bpu_and_push] -----1 \n");
	if (pthread_create(&pid, &attr, bpu_and_push, NULL) != 0) {
		g_bpu_and_push_tid = 0;
		fprintf(stderr, "[start_bpu_and_push] bpu_and_push return -2\n");
		return -2;
	}
	g_bpu_and_push_tid = pid;

	usleep(100 * 1000);
	pthread_attr_destroy(&attr);
	return 0;
}

// 收到视频帧的回调
void video_session_did_received_cb(int type, uint8_t *h264oraac, int insize)
{
	int rt, video_width, video_height;
	uint32_t timestamp;
	FRAME_INFO f_info;

	if (!g_is_open_started) {
		// 从SPS中获取视频原始的分辨率
		if ((h264oraac[0] & 0x1F) == 0x07 && !parse_sps(h264oraac, insize, &video_width, &video_height)) {
			// 更新摄像头实际的分辨率
			updateMuxVideoMetaInfo(video_width, video_height);
			g_v_width = video_width;
			g_v_height = video_height;
			g_is_open_started = 1;
			// 开启视频帧解码并进行推理线程
			rt = start_bpu_and_push();
			fprintf(stderr, "[video_session_did_received_cb] start_bpu_and_push(0) = %d\n", rt);
		} else {
			fprintf(stderr, "[video_session_did_received_cb] h264oraac[0] = 0x0%d\n", h264oraac[0] & 0x1F);
			return ;
		}
	}
	if (h264oraac && insize > 0) {
		memcpy(&xftp_frame_buffer[4], h264oraac, insize);
		// 送到解码器解码，VPS压缩，BPU进行推理
		rt = send_stream_to_bpu(xftp_frame_buffer, insize + 4);
		if (!rt) {
			timestamp = getTimeMsec() - g_start_vts;
			if (((h264oraac[0] & 0x1F) == 0x01) || ((h264oraac[0] & 0x1F) == 0x05)) {
				f_info.timestamp = timestamp;
				f_info.seqno = g_frame_seqno++;
				rt = frame_cir_buff_enqueue(&g_frame_cir_buff, &f_info);
			}
			// 将视频帧推送到流媒体服务器
			add_xftp_frame((char *)h264oraac, insize, type, timestamp);
		}
	}
}
// 拉流结束的回调
void video_session_did_stop_cb(void)
{
	fprintf(stderr, "[video_session_did_stop_cb] ++++++++++++++++++++++++++++ \n");
}
// 启动视频拉流
int start_pull_video(void)
{
	int rt = 0;

	if (!strcmp(g_stream_protocol, "rtsp")) {
	// 	rt = start_open_rtsp_thread(g_rtsp_url, g_rtsp_port, g_rtsp_user, g_rtsp_pwd, g_rtsp_server_ip, video_session_did_received_cb, video_session_did_stop_cb);
	// 	if (rt) {
	// 		fprintf(stderr, "[start_pull_video] start_open_rtsp_thread failed. rt = %d\n", rt);
	// 		return -1;
	// 	}
	// 	fprintf(stderr, "[start_pull_video] start_open_rtsp_thread success = %d\n", rt);
	// } else if (!strcmp(g_stream_protocol, "uvc")) {
		rt = start_uvc_stream();
		if (rt) {
			fprintf(stderr, "[start_pull_video] start_uvc_stream failed. rt = %d\n", rt);
			return -2;
		}
		fprintf(stderr, "[start_pull_video] start_uvc_stream success = %d\n", rt);
	} else {
		fprintf(stderr, "[start_pull_video] error g_stream_protocol = %s\n", g_stream_protocol);
		return -3;
	}
	return rt;
}
// 通知对方已经开始推流
int send_session_info_to_receiver(char *receiver)
{
	int rt = 0;
	MSG_SENT_RESULT sent_result;
	char recver[32] = {0}, callback_msg[1024] = {0};

	if (!receiver || !strlen(receiver)) {
		fprintf(stderr, "[send_session_info_to_receiver] Error: Invalid param.\n");
		return -1;
	}
	if (g_start_vts > 0 && strlen(g_peer_name) && strlen(g_remote_server_name) && g_uidn && g_ssrc && g_download_port > 0) {
		if (!strcmp(receiver, WEB_AGENT)) {
			strcpy(recver, SRS_AGENT);
			sprintf(callback_msg, "type=6;control_type=7;uidn=%u;ssrc=%u;sid=%s;web_agent=%s;stream_name=%s;download_port=%d", g_uidn, g_ssrc, g_sid, g_peer_name, g_stream_name, g_download_port);
		} else if (strlen(receiver)) {
			strcpy(recver, receiver);
			sprintf(callback_msg, "type=6;control_type=2;uidn=%u;ssrc=%u;server_name=%s;download_port=%d", g_uidn, g_ssrc, g_remote_server_name, g_download_port);
		} else {
			fprintf(stderr, "[send_session_info_to_receiver] receiver --> %s\n", receiver);
			return -2;
		}
		fprintf(stderr, "[send_session_info_to_receiver] msg --> %s\n", callback_msg);
		rt = send_control_msg(callback_msg, recver, &sent_result);
		usleep(1000);
		rt = send_control_msg(callback_msg, recver, &sent_result);
		usleep(1000);
		rt = send_control_msg(callback_msg, recver, &sent_result);
		if (rt) {
			fprintf(stderr, "[send_session_info_to_receiver] send_control_msg failed, ret=%d\n", rt);
		}
	} else {
		fprintf(stderr, "[send_session_info_to_receiver] return -3 : uidn=%u | ssrc=%u |g_start_vts=%ld | g_remote_server_name=%s | g_download_port=%d |g_peer_name=%s\n",
			g_uidn, g_ssrc, g_start_vts, g_remote_server_name, g_download_port, g_peer_name);
		return -3;
	}

	return rt;
}
// live SDK推流初始化成功回调
void xftpDidStart(long uidn, long ssrc, const char *remoteFilePath, const char *remoteServerName, int remoteServerPort, int downloadPort)
{
	int rt = 0;

	fprintf(stderr, "[xftpDidStart] %ld | %ld | %s | %s | %d | %d\n", uidn, ssrc, remoteFilePath, remoteServerName, remoteServerPort, downloadPort);

	g_start_vts = getTimeMsec();
	g_uidn = uidn;
	g_ssrc = ssrc;
	g_download_port = downloadPort;
	g_remote_server_port = remoteServerPort;
	strcpy(g_remote_server_name, remoteServerName);
	strcpy(g_remote_file_path, remoteFilePath);

	g_is_living = 1;
	g_is_open_started = 0;
	// 启动UVC流获取和编码
	rt = start_pull_video();
	if (rt) {
		fprintf(stderr, "[xftpDidStart] start_pull_video failed. rt = %d\n", rt);
		return;
	}
	// 推送消息给观看端
	send_session_info_to_receiver(g_peer_name);
	g_is_sending = 1;
}
// live SDK推流结束回调
void xftpTransferSuccess(long uidn, long ssrc, const char *remoteFilePath, const char *remoteServerName, int remoteServerPort, int downloadPort)
{
	fprintf(stderr, "[xftpTransferSuccess] : %ld | %ld | %s | %s | %d | %d\n", uidn, ssrc, remoteFilePath, remoteServerName, remoteServerPort, downloadPort);

	g_start_vts = 0;
	g_download_port = 0;
	g_uidn = 0;
	g_ssrc = 0;
}
// live SDK推流失败回调
void xftpFailedState(int state, const char *msg)
{
	fprintf(stderr, "[xftpFailedState] state:%d, msg:%s\n", state, msg ? msg : "NULL");

	if (g_start_vts && state == 13) {
		g_start_vts = 0;
		stop_session();
	}
}
// 初始化推流SDK
int start_xftp_request(int width, int height)
{
	if (width <= 0 || height <= 0) {
		fprintf(stderr, "[start_xftp_request] invalid param!\n");
		return -1;
	}
	stopSend();
	closeXtvf();
	fprintf(stderr, "[start_xftp_request] Before initMuxToXtvfNew, g_xftp_server=%s，g_xftp_port=%d, g_xftp_user=%s, g_xftp_pwd=%s\n", g_xftp_server, g_xftp_port, g_xftp_user, g_xftp_pwd);
	// 启动live SDK，连接流媒体服务器，设置回调
	// 连接流媒体成功会回调 xftpDidStart, 此回调中去处理开启摄像头拉流/推理逻辑/推流至服务器
	// 连接流媒体失败会回调 xftpFailedState
	// 停止会回调 xftpTransferSuccess
	return initMuxToXtvfNew(NULL, 30, width, height, -1, -1, -1, 0, 0, g_xftp_user, g_xftp_pwd, g_xftp_server,
					g_xftp_port, 0, xftpDidStart, xftpFailedState, xftpTransferSuccess);
}
// 开启SDK推流
int start_live(void)
{
	return start_xftp_request(g_v_width, g_v_height);
}
// 停止SDK推流
int stop_xftp_session(void)
{
	closeXtvf();
	fprintf(stderr, "closeXtvf ... ... \n");
	stopSend();
	fprintf(stderr, "stopSend ... ... \n");

	return 0;
}
// SDK停止推流并截图
int stop_live(void)
{
	if (!strcmp(g_stream_protocol, "rtsp")) {
		stop_rtsp_over_tcp_thread(); // 停止 rtsp 拉流
	} else if (!strcmp(g_stream_protocol, "uvc")) {
		// 停止UVC视频流
		g_is_running = 0;
		if (g_uvc_thread) {
			pthread_join(g_uvc_thread, NULL);
			g_uvc_thread = 0;
		}
		g_is_uvc_running = 0;
	}
	stop_xftp_session();
	g_is_living = 0;

	return 0;
}
// 结束推流/推理
void stop_session(void)
{
	g_start_vts = 0;
	g_download_port = 0;
	g_uidn = 0;
	g_ssrc = 0;
	g_is_sending = 0;
	fprintf(stderr, "[stop_session] start ... ... \n");

	static int _is_stopping = 0;
	if (_is_stopping) {
		fprintf(stderr, "[stop_session] has been stopped\n");
		return ;
	}
	_is_stopping = 1;
	if (g_is_stop && fcos_finish && !g_is_living) {
		fprintf(stderr, "[stop_session] has been stopped, set g_is_stop=%d, fcos_finish=true\n", g_is_stop);
		_is_stopping = 0;
		return ;
	}
	fprintf(stderr, "[stop_session] set g_is_stop=%d, fcos_finish=true\n", g_is_stop);

	fprintf(stderr, "[stop_session] before stop_live\n");
	int rt = stop_live();
	fprintf(stderr, "[stop_session] stop_live rt = %d\n", rt);

	g_should_exit_main = 0;
	g_is_stop = 1;
	FRAME_INFO f_info = {.seqno = 0, .timestamp = 0};
	rt = frame_cir_buff_enqueue(&g_frame_cir_buff, &f_info);
	fcos_finish = true;
	bpu_work fcos_work;
	fcos_work_deque.push_back(fcos_work);
	_is_stopping = 0;
	fprintf(stderr, "[stop_session] END ... ... \n");
}
// 结束推流
void stop_session0(uint32_t uidn, uint32_t ssrc)
{
	fprintf(stderr, "[stop_session0] uidn = %u, g_uidn = %u, ssrc = %u, g_ssrc = %u\n", uidn, g_uidn, ssrc, g_ssrc);
	if (uidn != g_uidn || ssrc != g_ssrc) {
		return;
	}
	stop_session();
}

// 更新在线状态
int update_channel_online(int is_online)
{
	TABLE_DATA data;
	int rt, is_local_normal = 1;
	char update_sql_channel[1024] = {0}, select_sql_channel[1024] = {0}, url[1024] = {0};

	snprintf(update_sql_channel, sizeof(update_sql_channel) - 1, "update m_channel set is_normal = %d, is_online = %d where channel_no = '%s'", is_local_normal, is_online, g_channel_no);
	rt = write_data(update_sql_channel);
	if (rt) {
		fprintf(stderr, "[update_channel_online] write_data update %s online to %d error rt = %d\n", g_channel_no, is_online, rt);
		return -2;
	}
	if (!g_web_port || !strlen(g_web_server)) {
		fprintf(stderr, "[update_channel_online] empty g_web_port = %d, g_web_server = %s\n", g_web_port, g_web_server);
		return -3;
	}
	snprintf(url, sizeof(url) - 1, "http://%s:%d/live/channel/modChannelXtOnline?device_no=%s&is_online_xt=%d&is_normal=%d", g_web_server, g_web_port, g_xftp_user, is_online, is_local_normal);
	rt = httpRequest(url, NULL, NULL);
	if (rt < 0) {
		fprintf(stderr, "[update_channel_online] url = %s, modChannelXtOnline error rt = %d\n", url, rt);
		return -4;
	}

	return 0;
}
// 消息SDK初始化成功回调
void myRegisterSuccessCallback(int state, const char *from, const char *servername, const int serverport)
{
	g_is_online = 1;
	g_xttp_login_times = 0;
	update_channel_online(1);
	fprintf(stderr, "[myRegisterSuccessCallback] state=%d, from=%s, servername=%s, serverport=%d\n", state, from, servername, serverport);
}
// 消息SDK初始化失败回调
void myRegisterFailedCallback(int state, const char *msg)
{
	g_is_online = 0;
	update_channel_online(0);
	fprintf(stderr, "[myRegisterFailedCallback] times = %d, state=%d, msg=%s\n", g_xttp_login_times, state, msg);
}
// 消息SDK接收到消息回调
void myReceiveMsgCallback(const char *msg, const char *from, const char *msgid, int msg_type, const char *pid, const char *msgatime, int need_transfer_encode)
{
	MSG_SENT_RESULT sent_result;
	char msg_from[256] = {0}, response_msg[512] = {0};
	StringSplit *key_value_pairs = NULL, *item_pairs = NULL;
	StringSplitItem *item = NULL, *key = NULL, *value = NULL;
	StringSplitHandler msg_split_handler, key_value_split_handler;
	int i = 0, n = 0, rt = 0, type = -1, control_type = -1, is_online = -1;
	uint32_t uidn, ssrc;

	if (!msg || !from || !msgid || strlen(msg) >= 1500) {
		fprintf(stderr, "[myReceiveMsgCallback] invalid msg: %s, from: %s, msgid: %s\n", msg, from, msgid);
		return;
	}
	if (msgid) {
		// 消息去重
		for(; n < MSGID_NUM; n++){
			if (!strcmp(g_msg_ids[n], msgid)) {
				return;
			}
		}
		strcpy(g_msg_ids[g_msgid_cur], msgid);
		g_msgid_cur = (g_msgid_cur + 1) % MSGID_NUM;
	}
	fprintf(stderr, "[myReceiveMsgCallback] from = %s, msg = %s, msgid = %s\n", from, msg, msgid);
	strcpy(g_recv_msg, msg);
	if (init_string_split_handler(&msg_split_handler)) {
		fprintf(stderr, "[myReceiveMsgCallback] init_string_split_handler(1) failed!\n");
		return;
	}

	// 解析消息字段
	key_value_pairs = string_split_handle(';', g_recv_msg, &msg_split_handler);
	if (key_value_pairs->items != NULL) {
		for (i = 0; i < key_value_pairs->number; i++) {
			item = key_value_pairs->items[i];
			if (item != NULL && item->length) {
				if (init_string_split_handler(&key_value_split_handler)) {
					fprintf(stderr, "[myReceiveMsgCallback] init_string_split_handler(2) failed!\n");
					break;
				}

				if (strstr(item->str, "=")) {
					item_pairs = string_split_handle('=', item->str, &key_value_split_handler);
					if (item_pairs->items != NULL) {
						if (item_pairs->number != 2) {
							fprintf(stderr, "not key-value: number:%d\n", item_pairs->number);
							string_split_free(item_pairs, &key_value_split_handler);
							break;
						}
						key = item_pairs->items[0];
						value = item_pairs->items[1];

						if (!key->length || !value->length) {
							break;
						}
						if (!strcasecmp(key->str, "type")) {
							type = atoi(value->str);
						} else if (!strcasecmp(key->str, "control_type")) {
							control_type = atoi(value->str);
						} else if (!strcasecmp(key->str, "is_online")) {
							is_online = atoi(value->str);
						} else if (!strcasecmp(key->str, "from")) {
							strcpy(msg_from, value->str);
						} else if (!strcasecmp(key->str, "sid")) {
							strcpy(g_sid, value->str);
						} else if (!strcasecmp(key->str, "stream_name")) {
							strcpy(g_stream_name, value->str);
						} else if (!strcasecmp(key->str, "index")) {
							g_index = atoi(value->str);
						} else if (!strcasecmp(key->str, "uidn")) {
							uidn = atoi(value->str);
						} else if (!strcasecmp(key->str, "ssrc")) {
							ssrc = atoi(value->str);
						}
					} else {
						string_split_free(item_pairs, &key_value_split_handler);
						break;
					}
					string_split_free(item_pairs, &key_value_split_handler);
				} else {
					break;
				}
			} else {
				fprintf(stderr, "item:%d is NULL\n", i);
				break;
			}
		}
	}
	string_split_free(key_value_pairs, &msg_split_handler);
	if (type != 6){
		return;
	}

	switch (control_type) {
		case 1: //收到摄像头开始推流指令
		case 6:
			strcpy(g_peer_name, from);
			if (g_is_sending) { // 正在推流
				rt = send_session_info_to_receiver(g_peer_name);
				fprintf(stderr, "[myReceiveMsgCallback] %d send_session_info_to_receiver rt = %d, g_peer_name = %s\n", control_type, rt, g_peer_name);
			} else { // 开启推流
				if (g_bpu_and_push_tid) {
					fprintf(stderr, "[myReceiveMsgCallback] %d bpu_and_push is active(0), g_bpu_and_push_tid = %u\n", control_type, (unsigned)g_bpu_and_push_tid);
					break;
				}
				if (g_bpu_and_push_exit_ts && getTimeMsec() - g_bpu_and_push_exit_ts < 200) {
					fprintf(stderr, "[myReceiveMsgCallback] %d bpu_and_push is active(1), g_bpu_and_push_exit_ts = %ld\n", control_type, g_bpu_and_push_exit_ts);
					break;
				}

				fprintf(stderr, "[myReceiveMsgCallback] %d should start camera and start live.\n", control_type);
				// 初始化连接多媒体服务器
				rt = start_live(); // 启动SKDK推流
				fprintf(stderr, "[myReceiveMsgCallback] %d start_live() = %d, g_peer_name = %s\n", control_type, rt, g_peer_name);
			}
			break;
		case 24:
			// 关闭多媒体服务器的连接, 停止推流/推理
			stop_session0(uidn, ssrc);
			break;
		case 5: //对方询问是否在线
			if (strlen(msg_from)) {
				sprintf(response_msg, "type=6;control_type=4;from=%s;is_online=1", g_xftp_user);
				// 回复‘在线’消息
				rt = send_control_msg(response_msg, msg_from, &sent_result);
				if (rt) {
					fprintf(stderr, "[myReceiveMsgCallback] send_control_msg failed(%s), rt=%d\n", response_msg, rt);
				}
			} else {
				fprintf(stderr, "[myReceiveMsgCallback] Error: The message hasn't from info(%s)\n", g_recv_msg);
			}
			break;
		default:
			break;
	}
}
void myReceiveBinaryMsgCallback(uint8_t *data, int size, const char *from, const char *msgid, int type){}
void mySentMsgResponseCallback(const char *msgid, const char *pid, const char *msgatime){}
// 初始化消息SDK
int start_msg_client(void)
{
	int rt = -100;

	update_channel_online(0);
	fprintf(stderr, "[start_msg_client] Before start_xttp_client g_is_online = %d, g_xttp_port = %d, g_xttp_server = %s\n", g_is_online, g_xttp_port, g_xttp_server);
	if (!g_is_online) {
		// 启动消息SDK，连接消息服务器，设置消息回调
		rt = start_xttp_client(g_xftp_user, g_xftp_pwd, g_xttp_server, g_xttp_port, 
				0, myRegisterSuccessCallback, 
				myRegisterFailedCallback, myReceiveMsgCallback,
				myReceiveBinaryMsgCallback, mySentMsgResponseCallback, myStopXttpCallback);
		fprintf(stderr, "[start_msg_client] start_xttp_client rt = %d\n", rt);
	}
	return rt;
}
// 消息SDK停止回调, 重连消息服务器
void myStopXttpCallback(void)
{
	int rt = 0;

	fprintf(stderr, "[myStopXttpCallback] .............. \n");
	g_is_online = 0;
	update_channel_online(0);
	++g_xttp_login_times;
	if (g_xttp_login_times < XTTP_RETRY_MAX) {
		// 重连消息服务器
		rt = start_msg_client();
		fprintf(stderr, "[myStopXttpCallback] 0 start_msg_client rt=%d\n", rt);
	} else {
		g_xttp_login_times = 0;
		sleep(60);
		if (!g_is_online) {
			rt = start_msg_client();
			fprintf(stderr, "[myStopXttpCallback] 1 start_msg_client rt=%d\n", rt);
		}
	}
}

// 解析 rtsp URL
int get_rtsp_info(const char *url)
{
	char *ptr = NULL, *ptr2 = NULL, *real_url = NULL, user_auth_part[512] = {0}, port_str[512] = {0}, tmp_url[512] = {0};

	if (!url) {
		return -1;
	}
	strcpy(tmp_url, url);
	ptr = strstr(tmp_url, "rtsp://");
	if (!ptr) {
		fprintf(stderr, "Invalid format: No rtsp protocol info.\n");
		return -2;
	}

	ptr += strlen("rtsp://");
	ptr2 = strstr(ptr, "@");
	if (ptr2) {
		memcpy(user_auth_part, ptr, ptr2 - ptr);
		ptr = ptr2;
		ptr2 = strstr(user_auth_part, ":");
		if (!ptr2) {
			fprintf(stderr, "Invalid format: wrong auth info.\n");
			return -3;
		}
		memset(g_rtsp_user, 0, sizeof(g_rtsp_user));
		memcpy(g_rtsp_user, user_auth_part, ptr2 - user_auth_part);
		ptr2 += strlen(":");
		memset(g_rtsp_pwd, 0, sizeof(g_rtsp_pwd));
		memcpy(g_rtsp_pwd, ptr2, strlen(ptr2));
		ptr += strlen("@");
	}

	real_url = ptr;
	ptr2 = strstr(ptr, ":");
	if (ptr2) {
		memset(g_rtsp_server_ip, 0, sizeof(g_rtsp_server_ip));
		memcpy(g_rtsp_server_ip, ptr, ptr2 - ptr);
		ptr = ptr2 + strlen(":");
		ptr2 = strstr(ptr, "/");
		if (!ptr2) {
			fprintf(stderr, "Invalid format: wrong server and port info.\n");
			return -4;
		}
		memset(port_str, 0, sizeof(port_str));
		memcpy(port_str, ptr, ptr2 - ptr);

		g_rtsp_port = (uint16_t)atoi(port_str);
		if (!g_rtsp_port || g_rtsp_port > 65535) {
			fprintf(stderr, "Invalid format: wrong port info.\n");
			return -5;
		}
		ptr = ptr2 + strlen("/");
	} else {
		ptr2 = strstr(ptr, "/");
		if (!ptr2) {
			fprintf(stderr, "Invalid format: wrong server info.\n");
			return -6;
		}
		memset(g_rtsp_server_ip, 0, sizeof(g_rtsp_server_ip));
		memcpy(g_rtsp_server_ip, ptr, ptr2 - ptr);
		g_rtsp_port = 554;
		ptr = ptr2 + strlen("/");
	}
	memset(g_rtsp_url, 0, sizeof(g_rtsp_url));
	sprintf(g_rtsp_url, "rtsp://%s", real_url);
	strcpy(g_rtsp_play_url, url);

	return 0;
}
// 读取配置
int read_config_xtvf(const char *channel_no)
{
	int rt;
	TABLE_DATA data;
	char select_sql_server[128] = {0}, device_no[33] = {0}, tmp_channel_no[4] = {0}, select_sql_device[128] = {0}, select_sql_channel[128] = {0};

	if (!channel_no || !strlen(channel_no)) {
		fprintf(stderr, "[read_config_xtvf] param error.\n");
		return -1;
	}
	// 获取设备号
	snprintf(select_sql_device, sizeof(select_sql_device) - 1, "select id,device_no from m_device");
	rt = read_data(&data, select_sql_device);
	if (rt) {
		fprintf(stderr, "[read_config_xtvf] Not find device. rt = %d\n", rt);
		return -2;
	}
	strcpy(device_no, data.lines[0].fields[1].val);
	free(data.lines);

	// 获取通道号
	snprintf(select_sql_channel, sizeof(select_sql_channel) - 1, "select id,channel_no,channel_pwd,channel_sip,protocol,stream_url from m_channel where channel_no = \"%s\"", channel_no);
	rt = read_data(&data, select_sql_channel);
	if (rt) {
		fprintf(stderr, "[read_config_xtvf] Not find channel %s.\n", channel_no);
		return -3;
	}
	strncpy(tmp_channel_no, data.lines[0].fields[1].val, 3);
	snprintf(g_xftp_user, sizeof(g_xftp_user) - 1, "%s%s", device_no, tmp_channel_no);
	strcpy(g_xftp_pwd, data.lines[0].fields[2].val);
	strcpy(g_stream_protocol, data.lines[0].fields[4].val);
	strcpy(g_stream_url, data.lines[0].fields[5].val);
	free(data.lines);

	// 获取web服务器地址及端口
	snprintf(select_sql_server, sizeof(select_sql_server) - 1, "select id,ip,port,type from m_server where type = 'web'");
	rt = read_data(&data, select_sql_server);
	if (rt) {
		fprintf(stderr, "[read_config_xtvf] Not find web server ip and port. rt = %d\n", rt);
		return -4;
	}
	strcpy(g_web_server, data.lines[0].fields[1].val);
	g_web_port = atol(data.lines[0].fields[2].val);
	free(data.lines);

	// 获取消息服务器地址及端口
	snprintf(select_sql_server, sizeof(select_sql_server) - 1, "select id,ip,port,type from m_server where type = 'msg'");
	rt = read_data(&data, select_sql_server);
	if (rt) {
		fprintf(stderr, "[read_config_xtvf] Not find msg server ip and port. rt = %d\n", rt);
		return -5;
	}
	strcpy(g_xttp_server, data.lines[0].fields[1].val);
	g_xttp_port = atol(data.lines[0].fields[2].val);
	free(data.lines);

	// 获取流媒体服务器地址及端口
	snprintf(select_sql_server, sizeof(select_sql_server) - 1, "select id,ip,port,type from m_server where type = 'video'");
	rt = read_data(&data, select_sql_server);
	if (rt) {
		fprintf(stderr, "[read_config_xtvf] Not find video server ip and port. rt = %d\n", rt);
		return -6;
	}
	strcpy(g_xftp_server, data.lines[0].fields[1].val);
	g_xftp_port = atol(data.lines[0].fields[2].val);
	free(data.lines);

	if (!strcmp(g_stream_protocol, "rtsp")) {
		// 解析rtsp流地址
		rt = get_rtsp_info(g_stream_url);
		if (rt) {
			fprintf(stderr, "[read_config_xtvf] stream_url error. rt = %d, url = %s\n", rt, g_stream_url);
			return -7;
		}
	} else if (!strcmp(g_stream_protocol, "uvc")) {
		// UVC协议不需要解析URL，直接使用默认配置
		fprintf(stderr, "[read_config_xtvf] UVC protocol selected\n");
	} else {
		fprintf(stderr, "[read_config_xtvf] No the protocol = %s", g_stream_protocol);
		return -9;
	}

	return 0;
}
// 信号处理
void IntHandle(int signo)
{
	int rt = 0;

	if (SIGINT == signo || SIGTERM == signo) {
		g_should_exit_main = 1;
		g_is_stop = 1;
		fcos_finish = true;
		
		fprintf(stderr, "[IntHandle] before stop_session\n");
		// 停止live SDK
		stop_session();
		fprintf(stderr, "[IntHandle] stop_session after\n");
		// 停止消息 SDK
		rt = stop_xttp_client();
		fprintf(stderr, "[IntHandle] stop_xttp_client rt = %d\n", rt);
		if (g_feed_is_over && g_do_post_is_over) {
			g_is_running = 0;
		}
	}
}
// 主程序
int main(int argc, char *argv[])
{
	int rt, i = 3;

	if (argc != 4) {
		fprintf(stderr, "USAGE: %s channel_no video_width video_height\n", argv[0]);
		return -1;
	}
	g_v_width = atoi(argv[2]); // 视频帧宽度
	g_v_height = atoi(argv[3]); // 视频帧高度
	if (strlen(argv[1]) != 3 || g_v_width <= 0  || g_v_height <= 0) {
		fprintf(stderr, "USAGE: %s channel_no video_width video_height\n", argv[0]);
		return -2;
	}
	// 验证应用ID
	rt = initAppkeySecretLicense(APP_KEY, APP_SECRET, LICENSE_KEY);
	if (rt != 0) {
		fprintf(stderr, "[%s] initAppkeySecretLicense failed, rt = %d\n", argv[0], rt);
		return -3;
	}

	g_is_udp = 0;
	xftp_frame_buffer[0] = 0;
	xftp_frame_buffer[1] = 0;
	xftp_frame_buffer[2] = 0;
	xftp_frame_buffer[3] = 1;
	strcpy(g_channel_no, argv[1]); // 通道号
	// 读取配置信息，获取设备/通道号/服务器地址端口
	rt = read_config_xtvf(g_channel_no);
	if (rt) {
		fprintf(stderr, "[%s] read_config_xtvf failed, rt = %d\n", argv[0], rt);
	}
	// 登录信令服务器
	// 登录成功会回调 myRegisterSuccessCallback
	// 登录失败会回调 myRegisterFailedCallback
	// 收到消息会回调 myReceiveMsgCallback, 此回调中去处理收到相应消息的逻辑
	// 停止时会回调 myStopXttpCallback, 此回调中去处理消息服务重连的逻辑
	while(i--){
		rt = start_msg_client();
		fprintf(stderr, "[%s] 1 start start_msg_client, rt = %d\n", argv[0], rt);
		if (!rt) break;
		sleep(1);
	}
	if (rt) update_channel_online(0);

	signal(SIGINT, IntHandle);
	signal(SIGTERM, IntHandle);

	g_is_running = 1;
	while (g_is_running || !g_feed_is_over || !g_do_post_is_over) {
		sleep(1);
		if (!g_is_online) { // 不在线则需重新登陆
			rt = start_msg_client();
			fprintf(stderr, "[%s] 2 while start_msg_client, rt = %d\n", argv[0], rt);
		}
	}

	return 0;
}