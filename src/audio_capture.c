/* ===================================================================
 * audio_capture.c — ALSA PCM 音频采集实现
 *
 * 目标平台: Linux ARM aarch64
 * 依赖: libasound (ALSA)
 * =================================================================== */

#include "audio_capture.h"
#include "error_codes.h"

#include <alsa/asoundlib.h>
#include <string.h>
#include <stdlib.h>

/* ---- init ---- */
int audio_capture_init(AudioCapture* ac, const char* device,
                       int sample_rate, int channels)
{
    if (ac == NULL) {
        LOG_ERROR("audio_capture_init: NULL pointer");
        return ERR_AUDIO_OPEN;
    }

    (void)device; /* 保留参数，实际 open 时使用 */

    memset(ac, 0, sizeof(*ac));
    ac->pcm_handle  = NULL;
    ac->sample_rate = (sample_rate > 0) ? (unsigned int)sample_rate
                                        : AUDIO_CAPTURE_DEFAULT_SAMPLE_RATE;
    ac->channels    = (channels > 0) ? channels
                                     : AUDIO_CAPTURE_DEFAULT_CHANNELS;
    ac->format      = SND_PCM_FORMAT_S16_LE;
    ac->buffer_bytes = 0;

    LOG_DEBUG("audio_capture_init: rate=%u ch=%d",
              ac->sample_rate, ac->channels);
    return ERR_OK;
}

/* ---- open ---- */
int audio_capture_open(AudioCapture* ac, const char* device)
{
    int rc;
    snd_pcm_t* handle = NULL;

    if (ac == NULL) {
        LOG_ERROR("audio_capture_open: NULL pointer");
        return ERR_AUDIO_OPEN;
    }

    if (device == NULL) {
        device = AUDIO_CAPTURE_DEFAULT_DEVICE;
    }

    rc = snd_pcm_open(&handle, device, SND_PCM_STREAM_CAPTURE, 0);
    if (rc < 0) {
        LOG_ERROR("audio_capture_open: snd_pcm_open '%s' failed: %s",
                  device, snd_strerror(rc));
        return ERR_AUDIO_OPEN;
    }

    ac->pcm_handle = (void*)handle;
    LOG_INFO("audio_capture_open: device '%s' opened", device);
    return ERR_OK;
}

/* ---- set_params ---- */
int audio_capture_set_params(AudioCapture* ac)
{
    int rc;
    snd_pcm_t* handle;
    snd_pcm_hw_params_t* hw_params = NULL;
    unsigned int rate;
    snd_pcm_uframes_t period_size;
    int dir;

    if (ac == NULL || ac->pcm_handle == NULL) {
        LOG_ERROR("audio_capture_set_params: handle not open");
        return ERR_AUDIO_PARAMS;
    }

    handle = (snd_pcm_t*)ac->pcm_handle;

    snd_pcm_hw_params_alloca(&hw_params);

    rc = snd_pcm_hw_params_any(handle, hw_params);
    if (rc < 0) {
        LOG_ERROR("audio_capture_set_params: snd_pcm_hw_params_any: %s",
                  snd_strerror(rc));
        return ERR_AUDIO_PARAMS;
    }

    /* 设置交错模式 */
    rc = snd_pcm_hw_params_set_access(handle, hw_params,
                                       SND_PCM_ACCESS_RW_INTERLEAVED);
    if (rc < 0) {
        LOG_ERROR("audio_capture_set_params: set_access: %s",
                  snd_strerror(rc));
        return ERR_AUDIO_PARAMS;
    }

    /* 设置格式 S16_LE */
    rc = snd_pcm_hw_params_set_format(handle, hw_params,
                                       (snd_pcm_format_t)ac->format);
    if (rc < 0) {
        LOG_ERROR("audio_capture_set_params: set_format: %s",
                  snd_strerror(rc));
        return ERR_AUDIO_PARAMS;
    }

    /* 设置声道数 */
    rc = snd_pcm_hw_params_set_channels(handle, hw_params,
                                         (unsigned int)ac->channels);
    if (rc < 0) {
        LOG_ERROR("audio_capture_set_params: set_channels: %s",
                  snd_strerror(rc));
        return ERR_AUDIO_PARAMS;
    }

    /* 设置采样率 */
    rate = ac->sample_rate;
    rc = snd_pcm_hw_params_set_rate_near(handle, hw_params, &rate, &dir);
    if (rc < 0) {
        LOG_ERROR("audio_capture_set_params: set_rate_near: %s",
                  snd_strerror(rc));
        return ERR_AUDIO_PARAMS;
    }
    if (rate != ac->sample_rate) {
        LOG_WARN("audio_capture_set_params: rate adjusted %u -> %u",
                 ac->sample_rate, rate);
        ac->sample_rate = rate;
    }

    /* 获取 period 大小并写入硬件参数 */
    rc = snd_pcm_hw_params(handle, hw_params);
    if (rc < 0) {
        LOG_ERROR("audio_capture_set_params: snd_pcm_hw_params: %s",
                  snd_strerror(rc));
        return ERR_AUDIO_PARAMS;
    }

    /* 查询实际 period_size 用于计算 buffer_bytes */
    rc = snd_pcm_hw_params_get_period_size(hw_params, &period_size, &dir);
    if (rc < 0) {
        LOG_ERROR("audio_capture_set_params: get_period_size: %s",
                  snd_strerror(rc));
        return ERR_AUDIO_PARAMS;
    }

    /* buffer_bytes = period_frames * (bytes_per_sample * channels) */
    ac->buffer_bytes = (size_t)period_size * (size_t)(snd_pcm_format_physical_width(
                         (snd_pcm_format_t)ac->format) / 8) * (size_t)ac->channels;

    LOG_INFO("audio_capture_set_params: rate=%u ch=%d period_frames=%lu buf_bytes=%zu",
             ac->sample_rate, ac->channels, (unsigned long)period_size,
             ac->buffer_bytes);
    return ERR_OK;
}

/* ---- read ---- */
ssize_t audio_capture_read(AudioCapture* ac, char* buf, size_t max_bytes)
{
    snd_pcm_t* handle;
    snd_pcm_sframes_t frames;
    size_t bytes_per_frame;

    if (ac == NULL || ac->pcm_handle == NULL || buf == NULL) {
        LOG_ERROR("audio_capture_read: invalid state");
        return ERR_AUDIO_READ;
    }

    handle = (snd_pcm_t*)ac->pcm_handle;

    bytes_per_frame = (size_t)(snd_pcm_format_physical_width(
                        (snd_pcm_format_t)ac->format) / 8) * (size_t)ac->channels;

    if (max_bytes < bytes_per_frame) {
        LOG_ERROR("audio_capture_read: buffer too small");
        return ERR_AUDIO_READ;
    }

    /* 计算可读取的最大帧数 */
    snd_pcm_uframes_t max_frames = (snd_pcm_uframes_t)(max_bytes / bytes_per_frame);

    frames = snd_pcm_readi(handle, buf, max_frames);
    if (frames < 0) {
        /* xrun 恢复 */
        if (frames == -EPIPE) {
            LOG_WARN("audio_capture_read: xrun, preparing recovery");
            snd_pcm_prepare(handle);
            /* 重试一次 */
            frames = snd_pcm_readi(handle, buf, max_frames);
            if (frames < 0) {
                LOG_ERROR("audio_capture_read: recovery failed: %s",
                          snd_strerror((int)frames));
                return ERR_AUDIO_READ;
            }
        } else {
            LOG_ERROR("audio_capture_read: snd_pcm_readi: %s",
                      snd_strerror((int)frames));
            return ERR_AUDIO_READ;
        }
    }

    return (ssize_t)((size_t)frames * bytes_per_frame);
}

/* ---- close ---- */
int audio_capture_close(AudioCapture* ac)
{
    if (ac == NULL) {
        return ERR_OK;
    }

    if (ac->pcm_handle != NULL) {
        snd_pcm_t* handle = (snd_pcm_t*)ac->pcm_handle;
        snd_pcm_drain(handle);
        snd_pcm_close(handle);
        ac->pcm_handle = NULL;
        LOG_INFO("audio_capture_close: device closed");
    }

    return ERR_OK;
}

/* ---- deinit ---- */
int audio_capture_deinit(AudioCapture* ac)
{
    int rc;

    rc = audio_capture_close(ac);
    if (rc != ERR_OK) {
        return rc;
    }

    if (ac != NULL) {
        ac->buffer_bytes = 0;
        ac->sample_rate  = 0;
        ac->channels     = 0;
    }

    return ERR_OK;
}
