#ifndef _AUDIO_CAPTURE_H_
#define _AUDIO_CAPTURE_H_

#include "common_types.h"

/* ===================================================================
 * AudioCapture — ALSA PCM 音频采集模块
 *
 * 封装 ALSA snd_pcm_t，通过 void* 隐藏 ALSA 内部类型，
 * 上层调用者无需引入 <alsa/asoundlib.h>。
 * =================================================================== */

/* 默认音频参数 */
#define AUDIO_CAPTURE_DEFAULT_DEVICE     "hw:0,0"
#define AUDIO_CAPTURE_DEFAULT_SAMPLE_RATE 16000
#define AUDIO_CAPTURE_DEFAULT_CHANNELS    1

/* 音频采集句柄 */
struct AudioCapture {
    void*      pcm_handle;     /* snd_pcm_t*, 不暴露 ALSA 类型给上层 */
    unsigned int sample_rate;  /* 采样率, 默认 16000 */
    int        channels;       /* 声道数, 默认 1 (mono) */
    int        format;         /* PCM 格式, SND_PCM_FORMAT_S16_LE */
    size_t     buffer_bytes;   /* 单次读取缓冲区大小 (字节) */
};

/* ---- API ---- */

/**
 * audio_capture_init — 初始化 AudioCapture 结构体字段为默认值
 *
 * @param ac          AudioCapture 实例指针
 * @param device      未使用（保留参数，实际 open 时传入）
 * @param sample_rate 采样率，0 表示使用默认值 16000
 * @param channels    声道数，0 表示使用默认值 1
 * @return            0 成功, <0 错误码
 */
int audio_capture_init(AudioCapture* ac, const char* device,
                       int sample_rate, int channels);

/**
 * audio_capture_open — 打开 ALSA PCM 采集设备
 *
 * @param ac     AudioCapture 实例指针
 * @param device PCM 设备名，如 "hw:0,0"
 * @return       0 成功, ERR_AUDIO_OPEN 失败
 */
int audio_capture_open(AudioCapture* ac, const char* device);

/**
 * audio_capture_set_params — 设置 PCM 硬件参数 (采样率/格式/声道)
 *
 * 必须在 audio_capture_open() 之后调用。
 *
 * @param ac  AudioCapture 实例指针
 * @return    0 成功, ERR_AUDIO_PARAMS 失败
 */
int audio_capture_set_params(AudioCapture* ac);

/**
 * audio_capture_read — 从 PCM 设备读取一帧音频数据
 *
 * 每次读取一个 period 的 PCM 数据。
 *
 * @param ac        AudioCapture 实例指针
 * @param buf       用户提供的缓冲区
 * @param max_bytes 缓冲区最大字节数
 * @return          >0 实际读取字节数, <0 ERR_AUDIO_READ
 */
ssize_t audio_capture_read(AudioCapture* ac, char* buf, size_t max_bytes);

/**
 * audio_capture_close — 关闭 PCM 设备
 *
 * @param ac  AudioCapture 实例指针
 * @return    0 成功, <0 错误码
 */
int audio_capture_close(AudioCapture* ac);

/**
 * audio_capture_deinit — 释放资源 (内部调用 close)
 *
 * @param ac  AudioCapture 实例指针
 * @return    0 成功, <0 错误码
 */
int audio_capture_deinit(AudioCapture* ac);

#endif /* _AUDIO_CAPTURE_H_ */
