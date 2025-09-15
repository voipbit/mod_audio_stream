// SPDX-License-Identifier: MIT
#ifndef __AUDIO_PIPE_HPP__
#define __AUDIO_PIPE_HPP__

#include <list>
#include <mutex>
#include <string>
#include <vector>

#include <libwebsockets.h>
#include <switch.h>
#include <switch_buffer.h>

#include "stream_utils.hpp"

class AudioPipe
{
  public:
    // holds inbound, outbound,  inbound incase of both
    Buffer *m_audio_buffer;
    // only used in case of "both" tracks, outbound stream
    Buffer *m_ob_audio_buffer;
    bool m_switch;
    int m_is_bidirectional;
    std::string m_uuid;
    std::string m_streamid;
    streaming_codec_t m_codec;
    int m_connection_attempts;
    std::mutex m_events_mutex;
    std::vector<std::string> m_events;

    enum LwsState_t
    {
        LWS_CLIENT_IDLE,
        LWS_CLIENT_CONNECTING,
        LWS_CLIENT_CONNECTED,
        LWS_CLIENT_FAILED,
        LWS_CLIENT_DISCONNECTING,
        LWS_CLIENT_DISCONNECTED,
        LWS_CLIENT_RECONNECTING
    };
    enum NotifyEvent_t
    {
        CONNECT_SUCCESS,
        CONNECT_FAIL,
        CONNECTION_DROPPED,
        CONNECTION_CLOSED_GRACEFULLY,
        CONNECTION_TIMEOUT,
        CONNECTION_DEGRADED,
        MESSAGE
    };
    typedef void (*log_emit_function)(int level, const char *line);
    typedef void (*notifyHandler_t)(const char *session_id,
                                    const char *stream_id,
                                    NotifyEvent_t event,
                                    const char *message);
    notifyHandler_t m_callback;

    struct lws_per_vhost_data
    {
        struct lws_context *context;
        struct lws_vhost *vhost;
        const struct lws_protocols *protocol;
    };

    static void initialize(const char *protocolName, unsigned int nThreads, int loglevel, log_emit_function logger);
    static void deinitialize();
    static bool lws_service_thread(unsigned int nServiceThread);

    // constructor
    AudioPipe(const char *uuid,
              const char *stream_id,
              const char *host,
              unsigned int port,
              const char *path,
              int sslFlags,
              size_t bufLen,
              const char *username,
              const char *password,
              notifyHandler_t callback,
              const char *track,
              const char *extraHeaders,
              streaming_codec_t codec,
              int sampling,
              int is_bidirectional);
    ~AudioPipe();

    LwsState_t getLwsState(void)
    {
        return m_state;
    }
    void connect(void);
    bool allBuffersAreEmpty();
    bool getEventBuffer();
    bool addEventBuffer(std::string data);
    std::string getEventData();

    static void reconnect(lws_sorted_usec_list_t *sul);

    bool hasBasicAuth(void)
    {
        return !m_username.empty() && !m_password.empty();
    }

    void getBasicAuth(std::string &username, std::string &password)
    {
        username = m_username;
        password = m_password;
    }

    void graceful_shutdown();

    bool isGracefulShutdown(void)
    {
        return m_gracefulShutdown;
    }

    bool isFirstMessageSent(void)
    {
        return m_firstMsgSent;
    }

    bool needsBothTracks(void)
    {
        return m_bothTracks;
    }

    int getSequenceNumber()
    {
        return m_sequenceNumber;
    }

    void increaseSequenceNumber()
    {
        m_sequenceNumber = m_sequenceNumber + 1;
    }

    void close();

    // no default constructor or copying
    AudioPipe() = delete;
    AudioPipe(const AudioPipe &) = delete;
    void operator=(const AudioPipe &) = delete;
    void addPendingWrite(AudioPipe *ap);

  private:
    static int lws_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len);
    static bool lws_initialized;
    static bool lws_stopping;
    static unsigned int nchild;
    static struct lws_context *contexts[];
    static unsigned int numContexts;
    static std::string protocolName;
    static std::mutex mutex_connects;
    static std::mutex mutex_reconnects;
    static std::mutex mutex_disconnects;
    static std::mutex mutex_writes;
    static std::list<AudioPipe *> pendingConnects;
    static std::list<AudioPipe *> pendingReconnects;
    static std::list<AudioPipe *> pendingDisconnects;
    static std::list<AudioPipe *> pendingWrites;
    static log_emit_function logger;

    static AudioPipe *findAndRemovePendingConnect(struct lws *wsi);
    static AudioPipe *findAndRemovePendingReconnect(struct lws *wsi);
    static AudioPipe *findPendingConnect(struct lws *wsi);
    static void addPendingConnect(AudioPipe *ap);
    static void addPendingDisconnect(AudioPipe *ap);
    static void processPendingConnects(lws_per_vhost_data *vhd);
    static void processPendingDisconnects(lws_per_vhost_data *vhd);
    static void processPendingWrites(void);

    bool connect_client(struct lws_per_vhost_data *vhd);

    LwsState_t m_state;
    int m_sampling;
    std::string m_host;
    unsigned int m_port;
    std::string m_path;
    std::string m_track;
    std::string m_extra_headers;
    int m_sslFlags;
    struct lws *m_wsi;
    int m_sequenceNumber;
    size_t m_audio_buffer_max_len;
    uint8_t *m_recv_buf;
    size_t m_recv_buf_len;
    uint8_t *m_recv_buf_ptr;
    struct lws_per_vhost_data *m_vhd;
    log_emit_function m_logger;
    std::string m_username;
    std::string m_password;
    bool m_gracefulShutdown;
    switch_time_t m_gracefulShutdown_at;
    bool m_firstMsgSent;
    bool m_lastMsgSent;
    bool m_bufferEmpty;
    bool m_bothTracks;
    bool m_stream_started;
};

struct sul_user_data
{
    struct lws_sorted_usec_list sul;
    AudioPipe *ap;
};
#endif
