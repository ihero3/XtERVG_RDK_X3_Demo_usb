#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>
#include <signal.h>
#include <argp.h>
#include <math.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <errno.h>

#include <algorithm>
#include <future>
#include <vector>
#include <string>
#include <deque>
#include <thread>
#include <iostream>
#include <iomanip>
#include <chrono>

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

//vdecode vps start
#include <fcntl.h>
#include "hb_comm_venc.h"
#include "hb_venc.h"
#include "hb_vdec.h"
#include "hb_vio_interface.h"
#include "hb_sys.h"
#include "hb_vp_api.h"

//vps start
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <getopt.h>
#include "hb_vin_api.h"
#include "hb_vps_api.h"
#include "hb_mipi_api.h"
#include "hb_common.h"
#include "hb_type.h"
#include "hb_errno.h"
#include "hb_comm_video.h"
//vps end

//bpu start
#include "sp_bpu.h"
#include "sp_vio.h"
#include "sp_display.h"
#include "sp_codec.h"
#include "sp_sys.h"
#include "hb_common_vot.h"
#include <dnn/hb_dnn.h>
#include <dnn/hb_sys.h>
#include <time.h>
//bpu end

using namespace std;

#define MSGID_NUM 32
#define XTTP_RETRY_MAX 3
#define MIN_PACKET_SIZE 480
#define SCRIPT_INNER_TYPE 0x01
#define ONE_MILLION_BASE 1000000

// 应用KEY
#define APP_KEY ""
// 应用SECRET
#define APP_SECRET ""
// 应用LICENSE
#define LICENSE_KEY ""

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
	g_is_online = 0, g_xttp_login_times = 0, g_is_uvc_running = 0, g_is_running = 1;
char g_msg_ids[MSGID_NUM][33] = {0};
char g_channel_no[128] = {0};
char g_stream_url[1500] = {0};
char g_stream_protocol[16] = {0};
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
char g_v_channel[64] = "/dev/video0";
int g_vps_group_number = 0;

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
char* g_mmz_vaddr[5];
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
pthread_t g_bpu_and_push_tid = 0, g_uvc_thread = 0;

int g_is_open_started = 0;
long g_start_vts = 0;

void stop_session(void);
void myStopXttpCallback(void);

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
// 推理结果推到流媒体服务器
int add_script_frame(const char *script_data, int script_len, int inner_type, uint32_t timestamp)
{
	if (!script_data || script_len <= 0) {
		fprintf(stderr, "[add_script_frame] error: script_data:%p, insize:%d, inner_type:%d, return -1;\n", script_data, script_len, inner_type);
		return -1;
	}

	return MuxScriptToXtvf(script_data, script_len, inner_type, timestamp);
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

// UVC线程函数声明
void *uvc_thread_func(void *arg);

int start_uvc_stream(void) {
    int ret;
    
    ret = pthread_create(&g_uvc_thread, NULL, uvc_thread_func, NULL);
    if (ret != 0) {
        fprintf(stderr, "[start_uvc_stream] pthread_create failed, ret=%d\n", ret);
        return -1;
    }

    g_is_uvc_running = 1;
    fprintf(stderr, "[start_uvc_stream] uvc thread started, tid=%lu\n", (unsigned long)g_uvc_thread);
    return 0;
}

// 启动 rtsp 拉流
int start_pull_video(void)
{
	int rt = 0;

	if (!strcmp(g_stream_protocol, "rtsp")) {
		//rt = start_open_rtsp_thread(g_rtsp_url, g_rtsp_port, g_rtsp_user, g_rtsp_pwd, g_rtsp_server_ip, video_session_did_received_cb, video_session_did_stop_cb);
		rt = start_uvc_stream();
        if (rt) {
			fprintf(stderr, "[start_pull_video] start_open_rtsp_thread failed. rt = %d\n", rt);
			return -1;
		}
		fprintf(stderr, "[start_pull_video] start_open_rtsp_thread success = %d\n", rt);
	} else {
		fprintf(stderr, "[start_pull_video] error g_stream_protocol = %s\n", g_stream_protocol);
		return -3;
	}
	return rt;
}

// UVC线程函数
void *uvc_thread_func(void *arg) {
    int ret;
    int rt = 0;
    enum v4l2_buf_type v4l2_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    uint8_t *h264_data = NULL;
    int h264_len = 0;
    uint32_t timestamp;
    int uvc_fd = -1;
    struct v4l2_format fmt;
    int frame_size = g_v_width * g_v_height * 3 / 2;

    struct buffer {
        void *start;
        size_t length;
    };

    struct buffer *buffers = NULL;
    unsigned int n_buffers = 0;

    // 打开UVC设备
    uvc_fd = open(g_v_channel, O_RDWR | O_NONBLOCK);
    if (uvc_fd < 0) {
        fprintf(stderr, "[uvc_thread_func] Failed to open %s: %s\n", g_v_channel, strerror(errno));
        goto exit;
    }

    // 设置视频格式为H264
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = g_v_width;
    fmt.fmt.pix.height = g_v_height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_H264;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (::ioctl(uvc_fd, VIDIOC_S_FMT, &fmt) < 0) {
        fprintf(stderr, "[uvc_thread_func] VIDIOC_S_FMT H264 failed: %s\n", strerror(errno));
        goto exit;
    }

    frame_size = fmt.fmt.pix.sizeimage;

    // 请求缓冲区
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (::ioctl(uvc_fd, VIDIOC_REQBUFS, &req) < 0) {
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

        if (::ioctl(uvc_fd, VIDIOC_QUERYBUF, &buf) < 0) {
            fprintf(stderr, "[uvc_thread_func] Failed to query buffer: %s\n", strerror(errno));
            goto exit;
        }

        buffers[n_buffers].length = buf.length;
        buffers[n_buffers].start = ::mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, uvc_fd, buf.m.offset);

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

        if (::ioctl(uvc_fd, VIDIOC_QBUF, &buf) < 0) {
            fprintf(stderr, "[uvc_thread_func] Failed to queue buffer: %s\n", strerror(errno));
            goto exit;
        }
    }

    // 启动流
    if (::ioctl(uvc_fd, VIDIOC_STREAMON, &v4l2_type) < 0) {
        fprintf(stderr, "[uvc_thread_func] Failed to start stream: %s\n", strerror(errno));
        goto exit;
    }

    // 主循环
    while (g_is_running && !g_should_exit_main) {
        fd_set fds;
        struct timeval tv;
        int r;

        FD_ZERO(&fds);
        FD_SET(uvc_fd, &fds);

        tv.tv_sec = 2;
        tv.tv_usec = 0;

        r = select(uvc_fd + 1, &fds, NULL, NULL, &tv);
        if (r < 0) {
            if (errno == EINTR) continue;
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

        if (::ioctl(uvc_fd, VIDIOC_DQBUF, &buf) < 0) {
            if (errno == EAGAIN) continue;
            fprintf(stderr, "[uvc_thread_func] Failed to dequeue buffer: %s\n", strerror(errno));
            break;
        }

        // 获取H264数据
        uint8_t *h264_buffer = (uint8_t *)buffers[buf.index].start;
        int h264_buffer_len = buf.bytesused;

        if (h264_buffer && h264_buffer_len > 0) {
            // 处理H264数据，与RTSP逻辑保持一致
            if (!g_is_open_started) {
                // 从SPS中获取视频原始的分辨率
                if ((h264_buffer[0] & 0x1F) == 0x07) {
                    int video_width, video_height;
                    if (!parse_sps(h264_buffer, h264_buffer_len, &video_width, &video_height)) {
                        // 更新摄像头实际的分辨率
                        updateMuxVideoMetaInfo(video_width, video_height);
                        g_v_width = video_width;
                        g_v_height = video_height;
                        g_is_open_started = 1;
                        // 开启视频帧解码并进行推理线程
                        rt = start_bpu_and_push();
                        fprintf(stderr, "[uvc_thread_func] start_bpu_and_push(0) = %d\n", rt);
                    }
                }
            }

            // 计算时间戳
            timestamp = getTimeMsec() - g_start_vts;

            // 送到解码器解码，VPS压缩，BPU进行推理
            memcpy(&xftp_frame_buffer[4], h264_buffer, h264_buffer_len);
            rt = send_stream_to_bpu(xftp_frame_buffer, h264_buffer_len + 4);
            if (!rt) {
                // 将视频帧推送到流媒体服务器
                add_xftp_frame((char *)h264_buffer, h264_buffer_len, 1, timestamp);
            }
        }

        // 放回缓冲区
        if (::ioctl(uvc_fd, VIDIOC_QBUF, &buf) < 0) {
            fprintf(stderr, "[uvc_thread_func] Failed to queue buffer: %s\n", strerror(errno));
            break;
        }
        
        usleep(33000); // 约30fps
    }

exit:
    // 停止流
    if (uvc_fd >= 0) {
        ::ioctl(uvc_fd, VIDIOC_STREAMOFF, &v4l2_type);
    }

    // 释放缓冲区
    if (buffers) {
        for (unsigned int i = 0; i < n_buffers; ++i) {
            if (buffers[i].start) {
                ::munmap(buffers[i].start, buffers[i].length);
            }
        }
        free(buffers);
    }

    // 关闭设备
    if (uvc_fd >= 0) close(uvc_fd);

    video_session_did_stop_cb();
    fprintf(stderr, "[uvc_thread_func] exit\n");
    return NULL;
}

// 主程序
int main(int argc, char *argv[])
{
	int rt, i = 3;

	if (argc != 5) {
		fprintf(stderr, "USAGE: %s channel_no video_width video_height v_channel\n", argv[0]);
		return -1;
	}
	g_v_width = atoi(argv[2]); // 视频帧宽度
	g_v_height = atoi(argv[3]); // 视频帧高度
    strcpy(g_v_channel, argv[4]);
	if (strlen(argv[1]) != 3 || g_v_width <= 0  || g_v_height <= 0 || strlen(g_v_channel) == 0) {
		fprintf(stderr, "USAGE: %s channel_no video_width video_height v_channel\n", argv[0]);
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

	while(!g_should_exit_main){
		sleep(1);
	}

	return 0;
}