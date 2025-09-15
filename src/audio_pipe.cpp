// SPDX-License-Identifier: MIT
#include "audio_pipe.hpp"
#include "mod_audio_stream.h"
#include "stream_utils.hpp"

#include "switch.h"
#include "switch_buffer.h"
#include <cassert>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

/* discard incoming messages over the socket that are longer than this */
/* Bytes per minute = (sample rate x bit depth x number of channels x 60 seconds) / 8 */
#define MAX_RECV_BUF_SIZE (16000 * 16 * 1 * 60 * 5 / 8)     //  5 mins worth of l16 16k data  ~19 mb.
#define RECV_BUF_REALLOC_SIZE (16000 * 16 * 1 * 60 * 1 / 8) //  1 mins worth of l16 16k data  ~1.9 mb

namespace
{
static const char *basicAuthUser = std::getenv("MOD_AUDIO_STREAM_HTTP_AUTH_USER");
static const char *basicAuthPassword = std::getenv("MOD_AUDIO_STREAM_HTTP_AUTH_PASSWORD");
} // namespace

// remove once we update to lws with this helper
static int dch_lws_http_basic_auth_gen(const char *user, const char *pw, char *buf, size_t len)
{
    size_t n = strlen(user), m = strlen(pw);
    char b[128];

    if (len < 6 + ((4 * (n + m + 1)) / 3) + 1)
        return 1;

    memcpy(buf, "Basic ", 6);

    n = lws_snprintf(b, sizeof(b), "%s:%s", user, pw);
    if (n >= sizeof(b) - 2)
        return 2;

    lws_b64_encode_string(b, n, buf + 6, len - 6);
    buf[len - 1] = '\0';

    return 0;
}

int AudioPipe::lws_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len)
{

    struct AudioPipe::lws_per_vhost_data *vhd =
        (struct AudioPipe::lws_per_vhost_data *)lws_protocol_vh_priv_get(lws_get_vhost(wsi), lws_get_protocol(wsi));
    AudioPipe **ppAp = (AudioPipe **)user;

    switch (reason)
    {
        case LWS_CALLBACK_PROTOCOL_INIT:
            vhd = (struct AudioPipe::lws_per_vhost_data *)lws_protocol_vh_priv_zalloc(
                lws_get_vhost(wsi), lws_get_protocol(wsi), sizeof(struct AudioPipe::lws_per_vhost_data));
            vhd->context = lws_get_context(wsi);
            vhd->protocol = lws_get_protocol(wsi);
            vhd->vhost = lws_get_vhost(wsi);
            break;

        case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER:
        {
            AudioPipe *ap = findPendingConnect(wsi);
            if (ap && ap->hasBasicAuth())
            {
                unsigned char **p = (unsigned char **)in, *end = (*p) + len;
                char b[128];
                std::string username, password;

                ap->getBasicAuth(username, password);
                lwsl_notice("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER username: %s, "
                            "password: xxxxxx\n",
                            username.c_str());
                if (dch_lws_http_basic_auth_gen(username.c_str(), password.c_str(), b, sizeof(b)))
                    break;
                if (lws_add_http_header_by_token(
                        wsi, WSI_TOKEN_HTTP_AUTHORIZATION, (unsigned char *)b, strlen(b), p, end))
                    return -1;
            }
        }
        break;

        case LWS_CALLBACK_EVENT_WAIT_CANCELLED:
            processPendingConnects(vhd);
            processPendingDisconnects(vhd);
            processPendingWrites();
            break;

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        {
            AudioPipe *ap = findAndRemovePendingConnect(wsi);
            if (!ap)
            {
                ap = findAndRemovePendingReconnect(wsi);
                if (!ap)
                {
                    lwsl_err(
                        "AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_CONNECTION_ERROR unable to find wsi %p.\n",
                        wsi);
                    return 0;
                }
            }
            if (ap->m_connection_attempts <= MAX_CONNECTION_ATTEMPTS)
            {
                /* static struct sul_user_data custom_sul; */
                lwsl_notice("%s: mod_audio_stream:(%s) connection error(%s).. retrying again. current attempts(%d)",
                            AUDIO_STREAM_LOGGING_PREFIX,
                            ap->m_streamid.c_str(),
                            ((in == NULL) ? "" : (char *)in),
                            ap->m_connection_attempts);
                struct sul_user_data *custom_sul;
                custom_sul = (struct sul_user_data *)malloc(sizeof(struct sul_user_data));
                memset(custom_sul, 0, sizeof(struct sul_user_data));
                custom_sul->sul.list.owner = NULL;
                custom_sul->sul.list.prev = NULL;
                custom_sul->sul.list.next = NULL;
                custom_sul->ap = ap;
                ap->m_state = LWS_CLIENT_FAILED;
                lws_sul_schedule(ap->m_vhd->context,
                                 0,
                                 &custom_sul->sul,
                                 ap->reconnect,
                                 RECONNECTION_DELAY_SECONDS * LWS_US_PER_SEC);
                return 0;
            }
            ap->m_state = LWS_CLIENT_FAILED;
            ap->m_callback(ap->m_uuid.c_str(), ap->m_streamid.c_str(), AudioPipe::CONNECT_FAIL, (char *)in);
        }
        break;

        case LWS_CALLBACK_CLIENT_ESTABLISHED:
        {
            AudioPipe *ap = findAndRemovePendingConnect(wsi);
            if (!ap)
            {
                ap = findAndRemovePendingReconnect(wsi);
                if (!ap)
                {
                    lwsl_err("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_ESTABLISHED. unable to find wsi %p.\n",
                             wsi);
                    return 0;
                }
            }
            *ppAp = ap;
            ap->m_vhd = vhd;
            ap->m_connection_attempts = 0;
            ap->m_state = LWS_CLIENT_CONNECTED;
            if (!ap->m_stream_started)
            {
                ap->m_callback(ap->m_uuid.c_str(), ap->m_streamid.c_str(), AudioPipe::CONNECT_SUCCESS, NULL);
                ap->m_stream_started = true;
            }
        }
        break;
        case LWS_CALLBACK_CLIENT_CLOSED:
        {
            AudioPipe *ap = *ppAp;
            if (!ap)
            {
                lwsl_err("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_CLOSED unable to find wsi %p..\n", wsi);
                return 0;
            }
            if (ap->isGracefulShutdown() || ap->m_state == LWS_CLIENT_DISCONNECTING)
            {
                // closed by us
                ap->m_callback(
                    ap->m_uuid.c_str(), ap->m_streamid.c_str(), AudioPipe::CONNECTION_CLOSED_GRACEFULLY, NULL);
            }
            else if (ap->m_state == LWS_CLIENT_CONNECTED)
            {
                // closed by far end
                if (ap->m_connection_attempts <= MAX_CONNECTION_ATTEMPTS)
                {
                    struct sul_user_data *custom_sul = (struct sul_user_data *)malloc(sizeof(struct sul_user_data));
                    memset(custom_sul, 0, sizeof(struct sul_user_data));
                    custom_sul->sul.list.owner = NULL;
                    custom_sul->sul.list.prev = NULL;
                    custom_sul->sul.list.next = NULL;
                    custom_sul->ap = ap;
                    ap->m_state = LWS_CLIENT_DISCONNECTED;

                    lwsl_notice("%s: mod_audio_stream(%s):(%s) connection closed by far end.. retrying again. current "
                                "attempts(%d)",
                                AUDIO_STREAM_LOGGING_PREFIX,
                                ap->m_streamid.c_str(),
                                ap->m_uuid.c_str(),
                                ap->m_connection_attempts);
                    lws_sul_schedule(ap->m_vhd->context,
                                     0,
                                     &(custom_sul->sul),
                                     ap->reconnect,
                                     RECONNECTION_DELAY_SECONDS * LWS_US_PER_SEC);
                    return 0;
                }
                lwsl_notice("mod_audio_stream(%s): (%s) socket closed by far end.\n",
                            ap->m_streamid.c_str(),
                            ap->m_uuid.c_str());
                ap->m_callback(ap->m_uuid.c_str(), ap->m_streamid.c_str(), AudioPipe::CONNECTION_DROPPED, NULL);
            }
            lwsl_notice("%s: mod_audio_stream(%s): (%s) connection disconnected.\n",
                        AUDIO_STREAM_LOGGING_PREFIX,
                        ap->m_streamid.c_str(),
                        ap->m_uuid.c_str());

            ap->m_state = LWS_CLIENT_DISCONNECTED;

            // NB: after receiving any of the events above, any holder of a
            // pointer or reference to this object must treat is as no longer valid

            *ppAp = NULL;
            delete ap;
        }
        break;

        case LWS_CALLBACK_CLIENT_RECEIVE:
        {
            AudioPipe *ap = *ppAp;
            if (!ap)
            {
                lwsl_err("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_RECEIVE unable to find wsi %p..\n", wsi);
                return 0;
            }

            if (!ap->m_is_bidirectional)
            {
                lwsl_notice("mod_audio_stream(%s) is not of type bidirectonal.\n", ap->m_streamid.c_str());
                return 0;
            }

            if (lws_frame_is_binary(wsi))
            {
                lwsl_err("mod_audio_stream:(%s) received binary frame, discarding.\n", ap->m_streamid.c_str());
                return 0;
            }

            if (lws_is_first_fragment(wsi))
            {
                lwsl_debug("mod_audio_stream(%s) stream-in: first fragment recieved\n", ap->m_streamid.c_str());
                if (nullptr != ap->m_recv_buf)
                {
                    lwsl_err(
                        "mod_audio_stream:(%s) first fragment received before prev final, discarding older data.\n",
                        ap->m_streamid.c_str());
                    free(ap->m_recv_buf);
                }
                // allocate a buffer for the entire chunk of memory needed
                ap->m_recv_buf_len = len + lws_remaining_packet_payload(wsi);
                ap->m_recv_buf = (uint8_t *)malloc(ap->m_recv_buf_len);
                ap->m_recv_buf_ptr = ap->m_recv_buf;
            }

            size_t write_offset = ap->m_recv_buf_ptr - ap->m_recv_buf;
            size_t remaining_space = ap->m_recv_buf_len - write_offset;

            if (remaining_space < len)
            {
                lwsl_notice("mod_audio_stream(%s) buffer realloc needed.\n", ap->m_streamid.c_str());
                size_t newlen = ap->m_recv_buf_len + RECV_BUF_REALLOC_SIZE;
                if (newlen > MAX_RECV_BUF_SIZE)
                {
                    free(ap->m_recv_buf);
                    ap->m_recv_buf = ap->m_recv_buf_ptr = nullptr;
                    ap->m_recv_buf_len = 0;
                    lwsl_notice("mod_audio_stream(%s): max buffer exceeded, truncating message.\n",
                                ap->m_streamid.c_str());
                }
                else
                {
                    ap->m_recv_buf = (uint8_t *)realloc(ap->m_recv_buf, newlen);
                    if (nullptr != ap->m_recv_buf)
                    {
                        ap->m_recv_buf_len = newlen;
                        ap->m_recv_buf_ptr = ap->m_recv_buf + write_offset;
                    }
                }
            }

            if (nullptr != ap->m_recv_buf)
            {
                if (len > 0)
                {
                    memcpy(ap->m_recv_buf_ptr, in, len);
                    ap->m_recv_buf_ptr += len;
                }
                if (lws_is_final_fragment(wsi))
                {
                    lwsl_debug("mod_audio_stream(%s): stream-in: final fragment recieved\n", ap->m_streamid.c_str());
                    if (nullptr != ap->m_recv_buf)
                    {
                        std::string msg((char *)ap->m_recv_buf, ap->m_recv_buf_ptr - ap->m_recv_buf);
                        ap->m_callback(ap->m_uuid.c_str(), ap->m_streamid.c_str(), AudioPipe::MESSAGE, msg.c_str());
                        if (nullptr != ap->m_recv_buf)
                            free(ap->m_recv_buf);
                    }
                    else
                    {
                        lwsl_err("mod_audio_stream:(%s) payload not recieved.\n", ap->m_streamid.c_str());
                    }
                    ap->m_recv_buf = ap->m_recv_buf_ptr = nullptr;
                    ap->m_recv_buf_len = 0;
                }
            }
        }
        break;

        case LWS_CALLBACK_CLIENT_WRITEABLE:
        {
            /* lwsl_err("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_WRITEABLE"); */
            AudioPipe *ap = *ppAp;
            if (!ap)
            {
                lwsl_err("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_WRITEABLE unable to find wsi %p..\n", wsi);
                return 0;
            }
            switch_time_t cur_time;
            if (ap->isGracefulShutdown())
            {
                cur_time = switch_epoch_time_now(NULL);
                if (cur_time >= ap->m_gracefulShutdown_at + 60)
                {
                    ap->m_state = LWS_CLIENT_DISCONNECTING;
                    lwsl_err("mod_audio_stream(%s): (%s) waited for too long. closing the connection.\n",
                             ap->m_streamid.c_str(),
                             ap->m_uuid.c_str());
                    lws_close_reason(wsi, LWS_CLOSE_STATUS_NORMAL, NULL, 0);
                    return -1;
                }
                /* no data available on both buffers, */
                if (ap->allBuffersAreEmpty() && !ap->m_lastMsgSent)
                {
                    char *payload = generate_json_data_event(CLIENT_EVENT_STOP,
                                                             ap->m_sequenceNumber,
                                                             ap->m_uuid,
                                                             ap->m_streamid,
                                                             ap->m_track,
                                                             NULL,
                                                             ap->m_extra_headers,
                                                             ap->m_codec,
                                                             ap->m_sampling);
                    int n = strlen(payload);
                    uint8_t buf[n + LWS_PRE];
                    memcpy(buf + LWS_PRE, payload, n);
                    free(payload);
                    ap->increaseSequenceNumber();
                    int sent = lws_write(wsi, buf + LWS_PRE, n, LWS_WRITE_TEXT);
                    if (sent < n)
                    {
                        lwsl_err("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_WRITEABLE %s attemped to send %lu "
                                 "only sent %d wsi %p..\n",
                                 ap->m_uuid.c_str(),
                                 n,
                                 sent,
                                 wsi);
                    }
                    ap->m_lastMsgSent = true;
                    ap->m_state = LWS_CLIENT_DISCONNECTING;
                    lwsl_notice("mod_audio_stream(%s) stop message sent.\n", ap->m_streamid.c_str());
                    lws_callback_on_writable(wsi);
                    return 0;
                }
            }

            {
                if (!ap->m_firstMsgSent)
                {
                    char *payload = generate_json_data_event(CLIENT_EVENT_START,
                                                             ap->m_sequenceNumber,
                                                             ap->m_uuid,
                                                             ap->m_streamid,
                                                             ap->m_track,
                                                             NULL,
                                                             ap->m_extra_headers,
                                                             ap->m_codec,
                                                             ap->m_sampling);
                    int n = strlen(payload);
                    uint8_t buf[n + LWS_PRE];
                    memcpy(buf + LWS_PRE, payload, n);
                    free(payload);
                    ap->increaseSequenceNumber();
                    int sent = lws_write(wsi, buf + LWS_PRE, n, LWS_WRITE_TEXT);
                    if (sent < n)
                    {
                        lwsl_err("AudioPipe::lws_service_thread LWS_CALLBACK_CLIENT_WRITEABLE %s attemped to send %lu "
                                 "only sent %d wsi %p..\n",
                                 ap->m_uuid.c_str(),
                                 n,
                                 sent,
                                 wsi);
                    }
                    ap->m_firstMsgSent = true;
                    lwsl_notice("mod_audio_stream(%s) First message sent for strameid.\n", ap->m_streamid.c_str());
                    lws_callback_on_writable(wsi);
                    return 0;
                }
            }
            // check for events to send
            {
                std::string data = ap->getEventData();
                if (data != "")
                {
                    uint8_t buf[data.length() + LWS_PRE];
                    memcpy(buf + LWS_PRE, data.c_str(), data.length());
                    int n = data.length();
                    int m = lws_write(wsi, buf + LWS_PRE, n, LWS_WRITE_TEXT);
                    if (m < n)
                    {
                        return -1;
                    }

                    // there may be audio data, but only one write per writeable event
                    // get it next time
                    lws_callback_on_writable(wsi);

                    return 0;
                }
            }

            if (ap->m_state == LWS_CLIENT_DISCONNECTING)
            {
                lwsl_notice("%s: mod_audio_stream(%s): (%s) closing the websocket connection.",
                            AUDIO_STREAM_LOGGING_PREFIX,
                            ap->m_streamid.c_str(),
                            ap->m_uuid.c_str());
                lws_close_reason(wsi, LWS_CLOSE_STATUS_NORMAL, NULL, 0);
                return -1;
            }

            {
                std::string inbound_str = "inbound";
                std::string outbound_str = "outbound";
                Buffer *audioBuffer;
                int type = 0;

                if (ap->needsBothTracks())
                {
                    if (ap->m_switch)
                    {
                        type = 1;
                        audioBuffer = ap->m_ob_audio_buffer;
                    }
                    else
                    {
                        type = 0;
                        audioBuffer = ap->m_audio_buffer;
                    }
                }
                else
                {
                    type = (ap->m_track == "inbound") ? 0 : 1;
                    audioBuffer = ap->m_audio_buffer;
                }

                /* lwsl_err("lws_callback type(%d)", type); */
                if (NULL == audioBuffer)
                    return 0;

                if (audioBuffer->try_lock())
                {
                    char *payload = generate_json_data_event(CLIENT_EVENT_MEDIA,
                                                             ap->m_sequenceNumber,
                                                             ap->m_uuid,
                                                             ap->m_streamid,
                                                             (type == 0) ? inbound_str : outbound_str,
                                                             audioBuffer,
                                                             ap->m_extra_headers,
                                                             ap->m_codec,
                                                             ap->m_sampling);

                    if (!payload)
                    {
                        /* lwsl_err("mod_audio_stream:(%s) AudioPipe::lws_service_thread: no payload to send",
                         * ap->m_streamid.c_str()); */

                        if (ap->needsBothTracks())
                            ap->m_switch = !ap->m_switch;
                        if (ap->isGracefulShutdown())
                            lws_callback_on_writable(wsi);

                        audioBuffer->unlock();
                        return 0;
                    }

                    int n = strlen(payload);
                    uint8_t buf[n + LWS_PRE];
                    memcpy(buf + LWS_PRE, payload, n);
                    free(payload);
                    ap->increaseSequenceNumber();
                    int sent = lws_write(wsi, buf + LWS_PRE, n, LWS_WRITE_TEXT);
                    if (sent < n)
                    {
                        lwsl_err("mod_audio_stream(%s) AudioPipe::lws_service_thread: attemped to send (%lu) only sent "
                                 "(%d) wsi %p..\n",
                                 ap->m_streamid.c_str(),
                                 n,
                                 sent,
                                 wsi);
                    }

                    if (ap->needsBothTracks())
                        ap->m_switch = !ap->m_switch;
                    lws_callback_on_writable(wsi);

                    audioBuffer->unlock();
                }
            }
            return 0;
        }
        break;

        default:
            break;
    }
    return lws_callback_http_dummy(wsi, reason, user, in, len);
}

// static members

struct lws_context *AudioPipe::contexts[] = {
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
unsigned int AudioPipe::numContexts = 0;
bool AudioPipe::lws_initialized = false;
bool AudioPipe::lws_stopping = false;
unsigned int AudioPipe::nchild = 0;
std::string AudioPipe::protocolName;
std::mutex AudioPipe::mutex_connects;
std::mutex AudioPipe::mutex_reconnects;
std::mutex AudioPipe::mutex_disconnects;
std::mutex AudioPipe::mutex_writes;
std::list<AudioPipe *> AudioPipe::pendingConnects;
std::list<AudioPipe *> AudioPipe::pendingReconnects;
std::list<AudioPipe *> AudioPipe::pendingDisconnects;
std::list<AudioPipe *> AudioPipe::pendingWrites;
AudioPipe::log_emit_function AudioPipe::logger;

void AudioPipe::processPendingConnects(lws_per_vhost_data *vhd)
{
    std::list<AudioPipe *> connects;
    {
        std::lock_guard<std::mutex> guard(mutex_connects);
        for (auto it = pendingConnects.begin(); it != pendingConnects.end(); ++it)
        {
            if ((*it)->m_state == LWS_CLIENT_IDLE || (*it)->m_state == LWS_CLIENT_RECONNECTING)
            {
                connects.push_back(*it);
                (*it)->m_state = LWS_CLIENT_CONNECTING;
            }
        }
    }
    for (auto it = connects.begin(); it != connects.end(); ++it)
    {
        AudioPipe *ap = *it;
        if (false == ap->connect_client(vhd))
        {
            struct sul_user_data *custom_sul;
            if (ap->m_connection_attempts > MAX_CONNECTION_ATTEMPTS)
            {
                lwsl_err("mod_audio_stream(%s): unable to connect to service url", ap->m_streamid.c_str());
                ap->m_state = LWS_CLIENT_FAILED;
                ap->m_callback(ap->m_uuid.c_str(),
                               ap->m_streamid.c_str(),
                               AudioPipe::CONNECT_FAIL,
                               "unable to connect to service url");
                return;
            }
            lwsl_err("%s mod_audio_stream(%s): unable to connect to service url. retrying..(%d)",
                     AUDIO_STREAM_LOGGING_PREFIX,
                     ap->m_streamid,
                     ap->m_connection_attempts);
            custom_sul = (struct sul_user_data *)malloc(sizeof(struct sul_user_data));
            memset(custom_sul, 0, sizeof(struct sul_user_data));
            custom_sul->sul.list.owner = NULL;
            custom_sul->sul.list.prev = NULL;
            custom_sul->sul.list.next = NULL;
            custom_sul->ap = ap;
            ap->m_state = LWS_CLIENT_FAILED;
            lws_sul_schedule(
                ap->m_vhd->context, 0, &custom_sul->sul, ap->reconnect, RECONNECTION_DELAY_SECONDS * LWS_US_PER_SEC);
        }
    }
}

void AudioPipe::processPendingDisconnects(lws_per_vhost_data *vhd)
{
    std::list<AudioPipe *> disconnects;
    {
        std::lock_guard<std::mutex> guard(mutex_disconnects);
        for (auto it = pendingDisconnects.begin(); it != pendingDisconnects.end(); ++it)
        {
            if ((*it)->m_state == LWS_CLIENT_DISCONNECTING)
                disconnects.push_back(*it);
        }
        pendingDisconnects.clear();
    }
    for (auto it = disconnects.begin(); it != disconnects.end(); ++it)
    {
        AudioPipe *ap = *it;
        if (NULL != ap && ap->m_wsi != nullptr)
        {
            lws_callback_on_writable(ap->m_wsi);
        }
        else
        {
            lwsl_debug("mod_audio_stream: processPendingDisconnects: null wsi");
        }
    }
}

void AudioPipe::processPendingWrites()
{
    std::list<AudioPipe *> writes;
    {
        std::lock_guard<std::mutex> guard(mutex_writes);
        for (auto it = pendingWrites.begin(); it != pendingWrites.end(); ++it)
        {
            if ((*it)->m_state == LWS_CLIENT_CONNECTED)
                writes.push_back(*it);
        }
        pendingWrites.clear();
    }
    for (auto it = writes.begin(); it != writes.end(); ++it)
    {
        AudioPipe *ap = *it;
        if (NULL != ap && ap->m_wsi != nullptr)
        {
            lws_callback_on_writable(ap->m_wsi);
        }
        else
        {
            lwsl_debug("mod_audio_stream: processPendingWrites: null wsi");
        }
    }
}

AudioPipe *AudioPipe::findAndRemovePendingReconnect(struct lws *wsi)
{
    AudioPipe *ap = NULL;
    std::list<AudioPipe *> toRemove;
    std::lock_guard<std::mutex> guard(mutex_reconnects);

    for (auto it = pendingReconnects.begin(); it != pendingReconnects.end() && !ap; ++it)
    {
        int state = (*it)->m_state;

        lwsl_debug("checking for pending reconnects (%p)", (*it));
        if ((*it)->m_wsi == nullptr)
            toRemove.push_back(*it);

        if ((state == LWS_CLIENT_RECONNECTING) && (*it)->m_wsi == wsi)
            ap = *it;
    }

    for (auto it = toRemove.begin(); it != toRemove.end(); ++it)
        pendingReconnects.remove(*it);

    if (ap)
    {
        pendingReconnects.remove(ap);
    }

    return ap;
}

AudioPipe *AudioPipe::findAndRemovePendingConnect(struct lws *wsi)
{
    AudioPipe *ap = NULL;
    std::lock_guard<std::mutex> guard(mutex_connects);
    std::list<AudioPipe *> toRemove;

    for (auto it = pendingConnects.begin(); it != pendingConnects.end() && !ap; ++it)
    {
        int state = (*it)->m_state;

        if ((*it)->m_wsi == nullptr)
            toRemove.push_back(*it);

        if ((state == LWS_CLIENT_CONNECTING) && (*it)->m_wsi == wsi)
            ap = *it;
    }

    for (auto it = toRemove.begin(); it != toRemove.end(); ++it)
        pendingConnects.remove(*it);

    if (ap)
    {
        pendingConnects.remove(ap);
    }

    return ap;
}

AudioPipe *AudioPipe::findPendingConnect(struct lws *wsi)
{
    AudioPipe *ap = NULL;
    std::lock_guard<std::mutex> guard(mutex_connects);

    for (auto it = pendingConnects.begin(); it != pendingConnects.end() && !ap; ++it)
    {
        int state = (*it)->m_state;
        if ((state == LWS_CLIENT_CONNECTING) && (*it)->m_wsi == wsi)
            ap = *it;
    }
    return ap;
}

void AudioPipe::addPendingConnect(AudioPipe *ap)
{
    {
        std::lock_guard<std::mutex> guard(mutex_connects);
        pendingConnects.push_back(ap);
        if (pendingConnects.size() > 100)
        {
            lwsl_err("mod_audio_stream(%s) : (%s) pending connects count high. (%d)\n",
                     ap->m_streamid.c_str(),
                     ap->m_uuid.c_str(),
                     pendingConnects.size());
        }
        lwsl_notice("mod_audio_stream(%s): %s after adding connect there are %lu pending connects\n",
                    ap->m_streamid.c_str(),
                    ap->m_uuid.c_str(),
                    pendingConnects.size());
    }
    lws_cancel_service(contexts[nchild++ % numContexts]);
}

void AudioPipe::addPendingDisconnect(AudioPipe *ap)
{
    ap->m_state = LWS_CLIENT_DISCONNECTING;
    {
        std::lock_guard<std::mutex> guard(mutex_disconnects);
        pendingDisconnects.push_back(ap);
        lwsl_notice("mod_audio_stream(%s) :%s after adding disconnect there are %lu pending disconnects\n",
                    ap->m_streamid.c_str(),
                    ap->m_uuid.c_str(),
                    pendingDisconnects.size());
    }
    lws_cancel_service(ap->m_vhd->context);
}

void AudioPipe::addPendingWrite(AudioPipe *ap)
{
    {
        std::lock_guard<std::mutex> guard(mutex_writes);
        pendingWrites.push_back(ap);
    }
    lws_cancel_service(ap->m_vhd->context);
}

bool AudioPipe::lws_service_thread(unsigned int nServiceThread)
{
    struct lws_context_creation_info info;

    const struct lws_protocols protocols[] = {{
                                                  protocolName.c_str(),
                                                  AudioPipe::lws_callback,
                                                  sizeof(void *),
                                                  1024 * 32,
                                              },
                                              {NULL, NULL, 0, 0}};

    memset(&info, 0, sizeof info);
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    info.ka_time = 60;               // tcp keep-alive timer
    info.ka_probes = 4;              // number of times to try ka before closing connection
    info.ka_interval = 5;            // time between ka's
    info.timeout_secs = 10;          // doc says timeout for "various processes involving network roundtrips"
    info.keepalive_timeout = 5;      // seconds to allow remote client to hold on to an idle HTTP/1.1 connection
    info.ws_ping_pong_interval = 20; // interval in seconds between sending PINGs on idle websocket connections
    info.timeout_secs_ah_idle = 10;  // secs to allow a client to hold an ah without using it

    lwsl_notice("AudioPipe::lws_service_thread creating context in service thread %d..\n", nServiceThread);

    contexts[nServiceThread] = lws_create_context(&info);
    if (!contexts[nServiceThread])
    {
        lwsl_err("AudioPipe::lws_service_thread failed creating context in service thread %d..\n", nServiceThread);
        return false;
    }

    int n;
    do
    {
        n = lws_service(contexts[nServiceThread], 50);
    } while (n >= 0 && !lws_stopping);

    lwsl_notice("AudioPipe::lws_service_thread ending in service thread %d\n", nServiceThread);
    lws_context_destroy(contexts[nServiceThread]);
    return true;
}

void AudioPipe::initialize(const char *protocol, unsigned int nThreads, int loglevel, log_emit_function logger)
{
    assert(!lws_initialized);
    assert(nThreads > 0 && nThreads <= 10);

    numContexts = nThreads;
    protocolName = protocol;
    lws_set_log_level(loglevel, logger);

    lwsl_notice("AudioPipe::initialize starting %d threads with subprotocol %s\n", nThreads, protocol);
    for (unsigned int i = 0; i < numContexts; i++)
    {
        std::thread t(&AudioPipe::lws_service_thread, i);
        t.detach();
    }
    lws_initialized = true;
}

void AudioPipe::deinitialize()
{
    assert(lws_initialized);
    lwsl_notice("AudioPipe::deinitialize\n");
    lws_stopping = true;
    lws_initialized = false;
}

// instance members
AudioPipe::AudioPipe(const char *uuid,
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
                     int is_bidirectional)
    : m_uuid(uuid), m_streamid(stream_id), m_host(host), m_port(port), m_path(path), m_sslFlags(sslFlags),
      m_audio_buffer_max_len(bufLen), m_callback(callback), m_track(track), m_extra_headers(extraHeaders), m_codec(L16),
      m_sampling(8000), m_gracefulShutdown(false), m_audio_buffer(NULL), m_ob_audio_buffer(NULL), m_recv_buf(nullptr),
      m_recv_buf_ptr(nullptr), m_state(LWS_CLIENT_IDLE), m_wsi(nullptr), m_vhd(nullptr), m_firstMsgSent(false),
      m_lastMsgSent(false), m_bothTracks(false), m_is_bidirectional(0), m_connection_attempts(0),
      m_stream_started(false)
{
    int step_frame_size;
    int ptime = 20;

    if (username && password)
    {
        m_username.assign(username);
        m_password.assign(password);
    }

    m_switch = true;
    m_sequenceNumber = 0;

    step_frame_size = L16_FRAME_SIZE_8KHZ_20MS * (sampling / 8000);

    if (codec == ULAW)
    {
        step_frame_size = ULAW_FRAME_SIZE_8KHZ_20MS * (sampling / 8000);
    }

    if (m_track == "both")
    {
        m_audio_buffer = new Buffer(m_streamid, m_audio_buffer_max_len, step_frame_size, ptime);
        m_ob_audio_buffer = new Buffer(m_streamid, m_audio_buffer_max_len, step_frame_size, ptime);
        m_bothTracks = true;
    }
    else
    {
        m_audio_buffer = new Buffer(m_streamid, m_audio_buffer_max_len, step_frame_size, ptime);
    }

    m_sampling = sampling;
    m_is_bidirectional = is_bidirectional;
    m_codec = codec;
}

AudioPipe::~AudioPipe()
{
    lwsl_notice("mod_audio_stream:(%s) callid(%s) deleting audiopipe.", m_streamid.c_str(), m_uuid.c_str());
    if (m_audio_buffer)
        delete m_audio_buffer;
    if (m_ob_audio_buffer)
        delete m_ob_audio_buffer;
    if (nullptr != m_recv_buf)
        free(m_recv_buf);
}

void AudioPipe::connect(void)
{
    addPendingConnect(this);
}

void AudioPipe::reconnect(lws_sorted_usec_list_t *sul)
{
    struct lws_client_connect_info i;
    struct sul_user_data *container = lws_container_of(sul, struct sul_user_data, sul);
    AudioPipe *ap = container->ap;
    ap->m_wsi = nullptr;

    lwsl_notice("%s mod_audio_stream(%s): reconnecting to host(%s) path(%s)",
                AUDIO_STREAM_LOGGING_PREFIX,
                ap->m_streamid.c_str(),
                ap->m_host.c_str(),
                ap->m_path.c_str());
    memset(&i, 0, sizeof(i));
    i.context = ap->m_vhd->context;
    i.port = ap->m_port;
    i.address = ap->m_host.c_str();
    i.path = ap->m_path.c_str();
    i.host = i.address;
    i.origin = i.address;
    i.ssl_connection = ap->m_sslFlags;
    i.protocol = protocolName.c_str();
    i.pwsi = &(ap->m_wsi);

    ap->m_connection_attempts++;
    ap->m_state = LWS_CLIENT_RECONNECTING;

    {
        std::lock_guard<std::mutex> guard(mutex_reconnects);
        pendingReconnects.push_back(ap);
    }

    ap->m_wsi = lws_client_connect_via_info(&i);
    free(container);
}

bool AudioPipe::connect_client(struct lws_per_vhost_data *vhd)
{
    assert(m_audio_buffer != nullptr);
    assert(m_vhd == nullptr);

    struct lws_client_connect_info i;

    memset(&i, 0, sizeof(i));
    i.context = vhd->context;
    i.port = m_port;
    i.address = m_host.c_str();
    i.path = m_path.c_str();
    i.host = i.address;
    i.origin = i.address;
    i.ssl_connection = m_sslFlags;
    i.protocol = protocolName.c_str();
    i.pwsi = &(m_wsi);

    m_state = LWS_CLIENT_CONNECTING;
    m_vhd = vhd;

    m_wsi = lws_client_connect_via_info(&i);
    lwsl_notice(
        "mod_audio_stream(%s) %s attempting connection, wsi is %p\n", m_streamid.c_str(), m_uuid.c_str(), m_wsi);

    m_connection_attempts++;
    return nullptr != m_wsi;
}

// Message will be sent on the websocket.
bool AudioPipe::addEventBuffer(std::string text)
{
    if (m_state != LWS_CLIENT_CONNECTED)
        return false;
    {
        std::lock_guard<std::mutex> lk(m_events_mutex);
        m_events.push_back(std::string(text));
    }
    addPendingWrite(this);
    return true;
}

std::string AudioPipe::getEventData()
{
    {
        std::lock_guard<std::mutex> lk(m_events_mutex);
        if (!m_events.empty())
        {
            std::string data = m_events.front();
            m_events.erase(m_events.begin());
            return data;
        }
    }
    return "";
}

bool AudioPipe::allBuffersAreEmpty()
{
    // data available on m_audio_buffer
    if (m_audio_buffer && m_audio_buffer->is_data_available())
    {
        return false;
    }

    // data available on m_ob_audio_buffer
    if (m_ob_audio_buffer && m_ob_audio_buffer->is_data_available())
    {
        return false;
    }
    return true;
}

void AudioPipe::close()
{
    if (m_state != LWS_CLIENT_CONNECTED)
    {
        lwsl_notice("mod_audio_stream: recieved close in unexpected state(%d).\n", m_state);
        return;
    }

    addPendingDisconnect(this);
}

void AudioPipe::graceful_shutdown()
{
    m_gracefulShutdown = true;
    m_gracefulShutdown_at = switch_epoch_time_now(NULL);
    addPendingWrite(this);
}
