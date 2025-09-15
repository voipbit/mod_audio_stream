#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <g711.h>
#include <list>
#include <mutex>
#include <regex>
#include <speex/speex_config_types.h>
#include <sstream>
#include <string.h>
#include <string>
#include <switch_json.h>
#include <thread>

#include "audio_pipe.hpp"
#include "base64.hpp"
#include "lws_glue.h"
#include "mod_audio_stream.h"
#include "stream_utils.hpp"
#include "switch.h"
#include "switch_buffer.h"
#include "switch_cJSON.h"
#include "switch_types.h"
#include "switch_utils.h"

#define RTP_PACKETIZATION_PERIOD 20

extern "C"
{
    SWITCH_STANDARD_SCHED_FUNC(stream_timeout_callback)
    {
        switch_core_session_t *session;
        stream_identifier_t *args = (stream_identifier_t *)task->cmd_arg;
        switch_core_session_message_t msg = {0};
        switch_status_t status;
        if ((session = switch_core_session_locate(args->session_id)))
        {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session),
                              SWITCH_LOG_INFO,
                              "mod_audio_stream(%s) Timer Invoked for session_id(%s)\n",
                              args->stream_id,
                              args->session_id);
            char reason[] = "TIMEOUT REACHED";
            status = do_graceful_shutdown(session, args->stream_id, reason);
            switch_core_session_rwunlock(session);
        }
    }

    SWITCH_STANDARD_SCHED_FUNC(heartbeat_callback)
    {
        switch_event_t *event;
        switch_core_session_t *session;
        stream_identifier_t *args = (stream_identifier_t *)task->cmd_arg;
        switch_core_session_message_t msg = {0};
        switch_channel_t *channel;
        /* fixed heartbeat interval */
        if ((session = switch_core_session_locate(args->session_id)))
        {
            channel = switch_core_session_get_channel(session);
            if (switch_ivr_uuid_exists(args->session_id))
            {
                switch_event_create_subclass(&event, SWITCH_EVENT_SESSION_HEARTBEAT, EVENT_STREAM_HEARTBEAT);
                switch_event_create(&event, SWITCH_EVENT_SESSION_HEARTBEAT);
                switch_channel_event_set_data(channel, event);
                switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "Application", "mod_audio_stream");
                switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "stream_id", args->stream_id);
                switch_event_fire(&event);
            }
            switch_core_session_rwunlock(session);
        }
        if (switch_ivr_uuid_exists(args->session_id))
        {
            task->runtime = switch_epoch_time_now(NULL) + 60;
        }
    }
}

namespace
{
static const char *requestedBufferSecs = std::getenv("MOD_AUDIO_STREAM_BUFFER_SECS");
static int nAudioBufferSecs = std::max(1, std::min(requestedBufferSecs ? ::atoi(requestedBufferSecs) : 40, 40));
static const char *requestedNumServiceThreads = std::getenv("MOD_AUDIO_STREAM_SERVICE_THREADS");
static const char *mySubProtocolName = std::getenv("MOD_AUDIO_STREAM_SUBPROTOCOL_NAME")
                                           ? std::getenv("MOD_AUDIO_STREAM_SUBPROTOCOL_NAME")
                                           : "audio.freeswitch.org";
static unsigned int nServiceThreads =
    std::max(1, std::min(requestedNumServiceThreads ? ::atoi(requestedNumServiceThreads) : 2, 5));
static unsigned int idxCallCount = 0;
static uint32_t play_count = 0;

static switch_status_t
g711u_decode(const void *encoded_data, uint32_t encoded_data_len, void *decoded_data, uint32_t *decoded_data_len)
{
    short *dbuf;
    unsigned char *ebuf;
    uint32_t i;

    dbuf = (short *)decoded_data;
    ebuf = (unsigned char *)encoded_data;

    for (i = 0; i < encoded_data_len; i++)
    {
        dbuf[i] = ulaw_to_linear(ebuf[i]);
    }

    *decoded_data_len = i * 2;

    return SWITCH_STATUS_SUCCESS;
}

// Send Events On Service URL, statusCallbackURL.
void sendIncorrectPayloadEvent(private_data_t *tech_pvt,
                               switch_core_session_t *session,
                               const char *payload,
                               const char *reason)
{
    if (tech_pvt->invalid_stream_input_notified != 0)
        return;

    cJSON *data = cJSON_CreateObject();
    char *result = NULL;
    cJSON *member = NULL;

    tech_pvt->invalid_stream_input_notified = 1;
    std::stringstream msg;
    AudioPipe *audio_pipe_ptr = static_cast<AudioPipe *>(tech_pvt->audio_pipe_ptr);
    switch_log_printf(SWITCH_CHANNEL_LOG,
                      SWITCH_LOG_INFO,
                      "mod_audio_stream:(%s) - Invalid message received(%.300s)\n",
                      tech_pvt->stream_id,
                      payload);

    member = cJSON_CreateString("incorrectPayload");
    cJSON_AddItemToObject(data, "event", member);

    member = cJSON_CreateString(audio_pipe_ptr->m_streamid.c_str());
    cJSON_AddItemToObject(data, "stream_id", member);

    member = cJSON_CreateString(payload);
    cJSON_AddItemToObject(data, "payload", member);

    member = cJSON_CreateNumber(audio_pipe_ptr->getSequenceNumber());
    cJSON_AddItemToObject(data, "sequenceNumber", member);

    result = cJSON_Print(data);
    cJSON_Delete(data);

    if (result == NULL)
    {
        return;
    }

    audio_pipe_ptr->increaseSequenceNumber();
    audio_pipe_ptr->addEventBuffer(std::string(result));
    free(result);

    data = cJSON_CreateObject();

    member = cJSON_CreateString(audio_pipe_ptr->m_streamid.c_str());
    cJSON_AddItemToObject(data, "stream_id", member);

    member = cJSON_CreateString(reason);
    cJSON_AddItemToObject(data, "reason", member);

    result = cJSON_Print(data);
    cJSON_Delete(data);

    if (result == NULL)
    {
        return;
    }

    tech_pvt->response_handler(session, EVENT_INVALID_STREAM_INPUT, result);
    free(result);
    return;
}

void storePayload(private_data_t *tech_pvt,
                  switch_core_session_t *session,
                  std::string rawAudio,
                  streaming_codec_t codec,
                  int rcvd_samplerate,
                  int current_samplerate)
{
    switch_size_t written = 0;
    int err;
    switch_mutex_lock(tech_pvt->write_buffer_mutex);

    if (codec == ULAW)
    {
        uint32_t len;
        uint16_t decoded_data[rawAudio.size() * 2 + 1];
        g711u_decode(rawAudio.data(), rawAudio.size(), &decoded_data, &len);
        written = switch_buffer_write(tech_pvt->write_buffer, decoded_data, len);
        tech_pvt->stream_input_received += len;
    }
    else if (codec == L16)
    {
        if (rcvd_samplerate != current_samplerate)
        {
            // Resampling..
            spx_int16_t out[rawAudio.size()];
            spx_uint32_t out_len = rawAudio.size();
            spx_uint32_t ip_len = rawAudio.size();
            std::ofstream op;
            size_t input_len = rawAudio.size() / sizeof(uint16_t);
            uint16_t input[input_len];
            memcpy((void *)input, rawAudio.data(), rawAudio.size());
            ip_len = input_len;

            if (!tech_pvt->resampler_outbound)
            {
                tech_pvt->resampler_outbound =
                    speex_resampler_init(1, rcvd_samplerate, current_samplerate, SWITCH_RESAMPLE_QUALITY, &err);
                switch_log_printf(
                    SWITCH_CHANNEL_SESSION_LOG(session),
                    SWITCH_LOG_INFO,
                    "mod_audio_stream(%s): initializing resampler for streamIn. rcvd(%d) cur(%d) err(%d)\n",
                    tech_pvt->stream_id,
                    rcvd_samplerate,
                    current_samplerate,
                    err);
            }

            speex_resampler_process_interleaved_int(
                tech_pvt->resampler_outbound, (const spx_int16_t *)input, &ip_len, out, &out_len);
            written = switch_buffer_write(tech_pvt->write_buffer, out, sizeof(spx_int16_t) * out_len);
            tech_pvt->stream_input_received += sizeof(spx_int16_t) * out_len;
        }
        else
        {
            written = switch_buffer_write(tech_pvt->write_buffer, rawAudio.data(), rawAudio.size());
            tech_pvt->stream_input_received += rawAudio.size();
        }
    }
    switch_mutex_unlock(tech_pvt->write_buffer_mutex);
}

void processClearEvent(private_data_t *tech_pvt, switch_core_session_t *session, cJSON *checkpoint)
{
    std::stringstream msg;
    stream_checkpoints_t *temp = NULL;
    AudioPipe *audio_pipe_ptr = static_cast<AudioPipe *>(tech_pvt->audio_pipe_ptr);

    switch_mutex_lock(tech_pvt->write_buffer_mutex);
    switch_log_printf(SWITCH_CHANNEL_LOG,
                      SWITCH_LOG_INFO,
                      "%s mod_audio_stream(%s) - clearing all buffers. at(%d) played(%d)\n",
                      AUDIO_STREAM_LOGGING_PREFIX,
                      tech_pvt->stream_id,
                      tech_pvt->stream_input_received,
                      tech_pvt->stream_input_played);
    switch_buffer_zero(tech_pvt->write_buffer);

    // clear all the checkpoints.
    while (NULL != tech_pvt->checkpoints)
    {
        temp = tech_pvt->checkpoints;
        tech_pvt->checkpoints = tech_pvt->checkpoints->next;
        switch_log_printf(SWITCH_CHANNEL_LOG,
                          SWITCH_LOG_INFO,
                          "%s mod_audio_stream(%s) - clearing checkpoint(%s)\n",
                          AUDIO_STREAM_LOGGING_PREFIX,
                          tech_pvt->stream_id,
                          temp->name);
        free(temp->name);
        free(temp);
    }
    tech_pvt->stream_input_played = 0;
    tech_pvt->stream_input_received = 0;

    switch_mutex_unlock(tech_pvt->write_buffer_mutex);

    msg << "{"
           "\"sequenceNumber\":"
        << audio_pipe_ptr->getSequenceNumber()
        << ","
           "\"streamId\":\""
        << tech_pvt->stream_id
        << "\","
           "\"event\":\"media.cleared\""
           "}";
    audio_pipe_ptr->addEventBuffer(msg.str());
    audio_pipe_ptr->increaseSequenceNumber();
    msg.str("");

    msg << "{";
    msg << "\"streamId\":\"" << audio_pipe_ptr->m_streamid << "\",";
    msg << "\"event\":\"media.cleared\"";
    msg << "}";

    tech_pvt->response_handler(session, EVENT_CLEARED_AUDIO, msg.str().c_str());
}

void processCheckpointEvent(private_data_t *tech_pvt, switch_core_session_t *session, cJSON *checkpoint)
{
    const char *name = cJSON_GetObjectCstr(checkpoint, "name");
    switch_codec_t *read_codec;
    int samples = 0;
    if (name == NULL)
    {
        // TODO:: confirm
        switch_log_printf(SWITCH_CHANNEL_LOG,
                          SWITCH_LOG_INFO,
                          "mod_audio_stream(%s) recieved checkpoint without name. Ignoring.\n",
                          tech_pvt->stream_id);
        return;
    }
    switch_log_printf(SWITCH_CHANNEL_LOG,
                      SWITCH_LOG_INFO,
                      "mod_audio_stream(%s) - processing checkpoint. %s\n",
                      tech_pvt->stream_id,
                      name);

    switch_mutex_lock(tech_pvt->write_buffer_mutex);
    if (tech_pvt->stream_input_received == 0)
    {
        switch_log_printf(SWITCH_CHANNEL_LOG,
                          SWITCH_LOG_ERROR,
                          "mod_audio_stream(%s) recieved checkpoint(%s) without prior media messages. Ignoring.\n",
                          tech_pvt->stream_id,
                          name);
        switch_mutex_unlock(tech_pvt->write_buffer_mutex);
        return;
    }

    stream_checkpoints_t *new_checkpoint = (stream_checkpoints_t *)malloc(sizeof(stream_checkpoints_t));
    new_checkpoint->name = (char *)malloc(strlen(name) + 1);
    strcpy(new_checkpoint->name, name);

    new_checkpoint->position = tech_pvt->stream_input_received;
    new_checkpoint->next = NULL;
    new_checkpoint->tail = new_checkpoint;

    // Add checkpoints to tail.
    if (NULL != tech_pvt->checkpoints)
    {
        tech_pvt->checkpoints->tail->next = new_checkpoint;
        tech_pvt->checkpoints->tail = new_checkpoint;
    }
    else
    {
        tech_pvt->checkpoints = new_checkpoint;
    }
    switch_log_printf(SWITCH_CHANNEL_LOG,
                      SWITCH_LOG_INFO,
                      "(%s) mod_audio_stream(%s) buffer_len(%d) checkpoint_at(%d) name(%s)\n",
                      AUDIO_STREAM_LOGGING_PREFIX,
                      tech_pvt->stream_id,
                      tech_pvt->stream_input_received,
                      new_checkpoint->position,
                      name);

    switch_mutex_unlock(tech_pvt->write_buffer_mutex);
}

void processPlayAudioEvent(private_data_t *tech_pvt, switch_core_session_t *session, const char *payload, cJSON *json)
{
    // dont send actual audio bytes in event message
    int rcvd_samplerate = 8000;
    int current_samplerate = rcvd_samplerate;
    streaming_codec_t codec = L16;
    switch_codec_t *read_codec;
    switch_channel_t *channel = NULL;

    /* Get channel var */
    if (!(channel = switch_core_session_get_channel(session)))
    {
        lwsl_err("mod_audio_stream(%s): processPlayAudioEvent: unable to get the channel.", tech_pvt->stream_id);
        return;
    }

    cJSON *jsonMedia = cJSON_GetObjectItem(json, "media");
    if (jsonMedia == NULL)
    {
        switch_log_printf(SWITCH_CHANNEL_LOG,
                          SWITCH_LOG_ERROR,
                          "mod_audio_stream:(%s) - missing data payload in media.play event.\n",
                          tech_pvt->stream_id);
        sendIncorrectPayloadEvent(tech_pvt, session, payload, "media key not available");
        return;
    }

    const char *jsonPayload = cJSON_GetObjectCstr(jsonMedia, "payload");
    if (jsonPayload == NULL)
    {
        switch_log_printf(SWITCH_CHANNEL_LOG,
                          SWITCH_LOG_ERROR,
                          "mod_audio_stream:(%s) - 'payload' not available.\n",
                          tech_pvt->stream_id);
        sendIncorrectPayloadEvent(tech_pvt, session, payload, "payload not available");
        return;
    }

    const char *jsonContentType = cJSON_GetObjectCstr(jsonMedia, "contentType");
    if (jsonContentType == NULL)
    {
        switch_log_printf(SWITCH_CHANNEL_LOG,
                          SWITCH_LOG_ERROR,
                          "mod_audio_stream:(%s) - 'contentType' not given.\n",
                          tech_pvt->stream_id);
        sendIncorrectPayloadEvent(tech_pvt, session, payload, "Incorrect ContentType");
        return;
    }

    cJSON *jsonSampleRate = cJSON_GetObjectItem(jsonMedia, "sampleRate");
    if (jsonSampleRate == NULL)
    {
        switch_log_printf(SWITCH_CHANNEL_LOG,
                          SWITCH_LOG_ERROR,
                          "mod_audio_stream:(%s) - 'sampleRate' not given.\n",
                          tech_pvt->stream_id);
        sendIncorrectPayloadEvent(tech_pvt, session, payload, "sampleRate not available");
        return;
    }

    rcvd_samplerate = jsonSampleRate->valueint;
    if (rcvd_samplerate != 8000 && rcvd_samplerate != 16000)
    {
        switch_log_printf(SWITCH_CHANNEL_LOG,
                          SWITCH_LOG_DEBUG,
                          "mod_audio_stream:(%s) - samplerate (%d) unsupported. defaulting to (8000)\n",
                          tech_pvt->stream_id,
                          rcvd_samplerate);
        rcvd_samplerate = 8000;
    }

    if (0 == strcmp(jsonContentType, "audio/x-l16"))
    {
        codec = L16;
        switch_log_printf(SWITCH_CHANNEL_LOG,
                          SWITCH_LOG_INFO,
                          "mod_audio_stream(%s): received content type (%s).\n",
                          tech_pvt->stream_id,
                          jsonContentType);
    }
    else if (0 == strcmp(jsonContentType, "audio/x-mulaw"))
    {
        switch_log_printf(SWITCH_CHANNEL_LOG,
                          SWITCH_LOG_INFO,
                          "mod_audio_stream(%s): received content type (%s).\n",
                          tech_pvt->stream_id,
                          jsonContentType);
        codec = ULAW;
        if (rcvd_samplerate != 8000)
        {
            switch_log_printf(SWITCH_CHANNEL_LOG,
                              SWITCH_LOG_ERROR,
                              "mod_audio_stream(%s): Unsupported combination of codec(%s), samplerate (%d)\n",
                              tech_pvt->stream_id,
                              jsonContentType,
                              rcvd_samplerate);
            sendIncorrectPayloadEvent(tech_pvt, session, payload, "Unsupported combination of codec, samplerate");
            return;
        }
    }
    else if (0 == strcmp(jsonContentType, "raw") || 0 == strcmp(jsonContentType, "wav"))
    {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session),
                          SWITCH_LOG_INFO,
                          "mod_audio_stream(%s): received '%s' contentType. setting default codec to l16.\n",
                          tech_pvt->stream_id,
                          jsonContentType);
        codec = L16;
    }
    else
    {
        switch_log_printf(SWITCH_CHANNEL_LOG,
                          SWITCH_LOG_ERROR,
                          "mod_audio_stream:(%s) - unsupported contentType: %s\n",
                          tech_pvt->stream_id,
                          jsonContentType);
        sendIncorrectPayloadEvent(tech_pvt, session, payload, "Invalid Content type");
        return;
    }

    std::string rawAudio = base64::base64_decode(jsonPayload);
    read_codec = switch_core_session_get_read_codec(session);
    if (NULL != read_codec && read_codec->implementation != NULL)
    {
        current_samplerate = read_codec->implementation->actual_samples_per_second;
    }

    storePayload(tech_pvt, session, rawAudio, codec, rcvd_samplerate, current_samplerate);
}

void processIncomingMessage(private_data_t *tech_pvt, switch_core_session_t *session, const char *message)
{
    cJSON *json = NULL;
    const char *event = NULL;
    switch_channel_t *channel = NULL;

    /* Get channel var */
    if (!(channel = switch_core_session_get_channel(session)))
    {
        lwsl_err("mod_audio_stream(%s): processIncomingMessage: unable to get the channel.", tech_pvt->stream_id);
        return;
    }

    json = cJSON_Parse(message);
    if (json == NULL)
    {
        switch_log_printf(SWITCH_CHANNEL_LOG,
                          SWITCH_LOG_ERROR,
                          "mod_audio_stream(%s) - could not parse message: %s\n",
                          tech_pvt->stream_id,
                          message);
        sendIncorrectPayloadEvent(tech_pvt, session, message, "Invalid Json");
        return;
    }

    event = cJSON_GetObjectCstr(json, "event");
    if (event == NULL)
    {
        switch_log_printf(SWITCH_CHANNEL_LOG,
                          SWITCH_LOG_ERROR,
                          "mod_audio_stream(%s) - could not parse message: %s\n",
                          tech_pvt->stream_id,
                          message);
        sendIncorrectPayloadEvent(tech_pvt, session, message, "No event key");
        return;
    }

    switch_log_printf(SWITCH_CHANNEL_LOG,
                      SWITCH_LOG_DEBUG,
                      "mod_audio_stream:(%s) - received %s event.\n",
                      tech_pvt->stream_id,
                      event);

    if (strcmp(event, "media.play") == 0)
    {
        switch_log_printf(SWITCH_CHANNEL_LOG,
                          SWITCH_LOG_DEBUG,
                          "mod_audio_stream:(%s) - processing %s event.\n",
                          tech_pvt->stream_id,
                          event);
        processPlayAudioEvent(tech_pvt, session, message, json);
    }
    else if (strcmp(event, "media.checkpoint") == 0)
    {
        switch_log_printf(SWITCH_CHANNEL_LOG,
                          SWITCH_LOG_INFO,
                          "%s mod_audio_stream:(%s) - processing %s event.\n",
                          AUDIO_STREAM_LOGGING_PREFIX,
                          tech_pvt->stream_id,
                          event);
        processCheckpointEvent(tech_pvt, session, json);
    }
    else if (strcmp(event, "media.clear") == 0)
    {
        switch_log_printf(SWITCH_CHANNEL_LOG,
                          SWITCH_LOG_INFO,
                          "%s mod_audio_stream:(%s) - processing %s event.\n",
                          AUDIO_STREAM_LOGGING_PREFIX,
                          tech_pvt->stream_id,
                          event);
        processClearEvent(tech_pvt, session, json);
    }
    else if (strcmp(event, "transcription.send") == 0)
    {
        switch_log_printf(SWITCH_CHANNEL_LOG,
                          SWITCH_LOG_INFO,
                          "%s mod_audio_stream:(%s) - processing %s event.\n",
                          AUDIO_STREAM_LOGGING_PREFIX,
                          tech_pvt->stream_id,
                          event);
        // Forward transcription data as an event
        tech_pvt->response_handler(session, EVENT_TRANSCRIPTION_RECEIVED, message);
    }
    else
    {
        switch_log_printf(SWITCH_CHANNEL_LOG,
                          SWITCH_LOG_ERROR,
                          "%s mod_audio_stream(%s) - unsupported msg type %s\n",
                          AUDIO_STREAM_LOGGING_PREFIX,
                          tech_pvt->stream_id,
                          event);
        sendIncorrectPayloadEvent(tech_pvt, session, message, "Invalid event");
    }
    cJSON_Delete(json);
}

static void
eventCallback(const char *session_id, const char *stream_id, AudioPipe::NotifyEvent_t event, const char *message)
{
    switch_core_session_t *session = switch_core_session_locate(session_id);
    if (session)
    {
        switch_channel_t *channel = switch_core_session_get_channel(session);
        switch_media_bug_t *bug = (switch_media_bug_t *)switch_channel_get_private(channel, stream_id);
        if (bug)
        {
            media_bug_callback_args_t *bug_args = (media_bug_callback_args_t *)switch_core_media_bug_get_user_data(bug);
            private_data_t *tech_pvt = (bug_args) ? bug_args->private_data_ptr : NULL;
            if (tech_pvt)
            {
                switch (event)
                {
                    case AudioPipe::CONNECT_SUCCESS:
                    {
                        std::stringstream json;
                        stream_identifier_t *id;
                        switch_time_t pulse;

                        json << "{\"streamId\":\"" << tech_pvt->stream_id << "\"}";
                        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session),
                                          SWITCH_LOG_INFO,
                                          "mod_audio_stream(%s) connection successful\n",
                                          tech_pvt->stream_id);
                        tech_pvt->response_handler(session, EVENT_CONNECT_SUCCESS, (char *)json.str().c_str());

                        AudioPipe *audio_pipe_ptr = static_cast<AudioPipe *>(tech_pvt->audio_pipe_ptr);
                        tech_pvt->is_started = 1;
                        tech_pvt->start_time = switch_epoch_time_now(NULL);

                        if (audio_pipe_ptr->m_audio_buffer)
                            audio_pipe_ptr->m_audio_buffer->set_start_time(tech_pvt->start_time);

                        if (audio_pipe_ptr->m_ob_audio_buffer)
                            audio_pipe_ptr->m_ob_audio_buffer->set_start_time(tech_pvt->start_time);

                        switch_time_t when = tech_pvt->start_time + tech_pvt->stream_end_timeout;

                        pulse = tech_pvt->start_time + 60;

                        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session),
                                          SWITCH_LOG_INFO,
                                          "mod_audio_stream(%s) Adding timer (%lld)\n",
                                          tech_pvt->stream_id,
                                          when);

                        id = (stream_identifier_t *)malloc(sizeof(stream_identifier_t));
                        strcpy(id->session_id, switch_core_session_get_uuid(session));
                        strcpy(id->stream_id, tech_pvt->stream_id);

                        tech_pvt->task_id = switch_scheduler_add_task(pulse,
                                                                      heartbeat_callback,
                                                                      "mod_audio_stream",
                                                                      switch_core_session_get_uuid(session),
                                                                      0,
                                                                      (void *)id,
                                                                      SSHF_NONE);
                        tech_pvt->stream_timeout_task_id =
                            switch_scheduler_add_task(when,
                                                      stream_timeout_callback,
                                                      "mod_audio_stream",
                                                      switch_core_session_get_uuid(session),
                                                      0,
                                                      (void *)id,
                                                      SSHF_FREE_ARG);
                        /* dummy message.. */
                        audio_pipe_ptr->addEventBuffer("");
                        break;
                    }
                    case AudioPipe::CONNECT_FAIL:
                    {
                        // first thing: we can no longer access the AudioPipe
                        std::stringstream json;
                        json << "{\"streamId\":\"" << tech_pvt->stream_id << "\",\"reason\":\"" << message << "\"}";
                        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session),
                                          SWITCH_LOG_NOTICE,
                                          "mod_audio_stream(%s) connection failed: %s\n",
                                          tech_pvt->stream_id,
                                          message);
                        if (tech_pvt->audio_pipe_ptr != nullptr)
                        {
                            delete static_cast<AudioPipe *>(tech_pvt->audio_pipe_ptr);
                            tech_pvt->audio_pipe_ptr = nullptr;
                        }
                        tech_pvt->response_handler(session, EVENT_CONNECT_FAIL, (char *)json.str().c_str());
                        if (!tech_pvt->channel_closing)
                        {
                            strcpy(tech_pvt->stream_termination_reason, TERMINATION_REASON_CONNECTION_ERROR);
                            stream_session_cleanup(
                                session, tech_pvt->stream_id, NULL, 0, (tech_pvt->is_started == 1) ? 0 : 1);
                        }
                    }
                    break;
                    case AudioPipe::CONNECTION_TIMEOUT:
                    {
                        // first thing: we can no longer access the AudioPipe
                        std::stringstream json;
                        json << "{\"streamId\":\"" << tech_pvt->stream_id << "\",\"reason\":\"" << message << "\"}";
                        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session),
                                          SWITCH_LOG_NOTICE,
                                          "mod_audio_stream(%s) connection timedout: %s\n",
                                          tech_pvt->stream_id,
                                          message);
                        tech_pvt->response_handler(session, EVENT_CONNECT_TIMEOUT, (char *)json.str().c_str());
                        strcpy(tech_pvt->stream_termination_reason, TERMINATION_REASON_CONNECTION_ERROR);
                        stream_session_graceful_shutdown(session, tech_pvt->stream_id);
                    }
                    break;
                    case AudioPipe::CONNECTION_DEGRADED:
                    {
                        std::stringstream json;
                        json << "{\"streamId\":\"" << tech_pvt->stream_id << "\"}";
                        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session),
                                          SWITCH_LOG_ERROR,
                                          "mod_audio_stream(%s) connection degraded: %s\n",
                                          tech_pvt->stream_id,
                                          message);
                        tech_pvt->response_handler(session, EVENT_CONNECT_DEGRADED, (char *)json.str().c_str());
                    }
                    break;
                    case AudioPipe::CONNECTION_DROPPED:
                    {
                        tech_pvt->audio_pipe_ptr = nullptr;
                        std::stringstream json;
                        json << "{\"streamId\":\"" << tech_pvt->stream_id << "\"}";
                        tech_pvt->response_handler(session, EVENT_DISCONNECT, (char *)json.str().c_str());
                        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session),
                                          SWITCH_LOG_NOTICE,
                                          "mod_audio_stream(%s) connection dropped from far end\n",
                                          tech_pvt->stream_id);
                        if (!tech_pvt->channel_closing)
                        {
                            strcpy(tech_pvt->stream_termination_reason, TERMINATION_REASON_CONNECTION_ERROR);
                            stream_session_cleanup(session, tech_pvt->stream_id, NULL, 0, 0);
                        }
                        break;
                    }
                    case AudioPipe::CONNECTION_CLOSED_GRACEFULLY:
                    {
                        // Y NOT use tech_pvt->mutex here.?? -> stream_frame
                        // first thing: we can no longer access the AudioPipe
                        tech_pvt->audio_pipe_ptr = nullptr;
                        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session),
                                          SWITCH_LOG_INFO,
                                          "mod_audio_stream(%s) connection closed gracefully\n",
                                          tech_pvt->stream_id);
                        if (!tech_pvt->channel_closing)
                            stream_session_cleanup(session, tech_pvt->stream_id, NULL, 0, 0);
                        break;
                    }
                    case AudioPipe::MESSAGE:
                    {
                        processIncomingMessage(tech_pvt, session, message);
                        break;
                    }
                }
            }
        }
        switch_core_session_rwunlock(session);
    }
    else
    {
        lwsl_notice("mod_audio_stream: (%s) [eventCallback] unable to locate the session (%s). event(%d)",
                    stream_id,
                    session_id,
                    event);
    }
}

switch_status_t stream_data_init(private_data_t *tech_pvt,
                                 char *stream_id,
                                 switch_core_session_t *session,
                                 char *service_url,
                                 char *host,
                                 unsigned int port,
                                 char *path,
                                 int sslFlags,
                                 streaming_codec_t codec,
                                 int sampling,
                                 int desiredSampling,
                                 int channels,
                                 char *track,
                                 int is_bidirectional,
                                 int timeout,
                                 char *metadata,
                                 response_handler_t response_handler)
{

    const char *username = nullptr;
    const char *password = nullptr;
    int err;
    switch_codec_implementation_t read_impl;
    switch_channel_t *channel = switch_core_session_get_channel(session);

    switch_core_session_get_read_impl(session, &read_impl);

    if (username = switch_channel_get_variable(channel, "MOD_AUDIO_BASIC_AUTH_USERNAME"))
    {
        password = switch_channel_get_variable(channel, "MOD_AUDIO_BASIC_AUTH_PASSWORD");
    }

    memset(tech_pvt, 0, sizeof(private_data_t));

    strncpy(tech_pvt->session_id, switch_core_session_get_uuid(session), MAX_SESSION_ID_LENGTH);
    strncpy(tech_pvt->host, host, MAX_WEBSOCKET_URL_LENGTH);
    strncpy(tech_pvt->service_url, service_url, MAX_WEBSOCKET_URL_LENGTH + MAX_WEBSOCKET_PATH_LENGTH);
    strncpy(tech_pvt->track, track, 16);
    tech_pvt->port = port;
    strncpy(tech_pvt->path, path, MAX_WEBSOCKET_PATH_LENGTH);
    tech_pvt->sampling = desiredSampling;
    tech_pvt->response_handler = response_handler;
    tech_pvt->playout = NULL;
    tech_pvt->channels = channels;
    tech_pvt->id = ++idxCallCount;
    tech_pvt->task_id = 0;
    tech_pvt->audio_paused = 0;
    tech_pvt->graceful_shutdown = 0;
    tech_pvt->stream_end_timeout = timeout;
    tech_pvt->is_bidirectional = is_bidirectional;
    tech_pvt->is_started = 0;
    tech_pvt->resampler = NULL;
    tech_pvt->resampler_outbound = NULL;
    tech_pvt->play_count = 0;
    tech_pvt->channel_closing = 0;
    tech_pvt->invalid_stream_input_notified = 0;
    strncpy(tech_pvt->stream_id, stream_id, MAX_SESSION_ID_LENGTH);

    if (metadata)
        strncpy(tech_pvt->initial_metadata, metadata, MAX_METADATA_LENGTH);

    if (is_bidirectional)
    {
        // TODO:: size can be increased
        int len = desiredSampling / 10 * 2 * channels; // 100 ms
        switch_mutex_init(&tech_pvt->write_buffer_mutex, SWITCH_MUTEX_NESTED, switch_core_session_get_pool(session));
        switch_buffer_create_dynamic(&tech_pvt->write_buffer, len, len, 0);
        tech_pvt->stream_input_received = 0;
        tech_pvt->stream_input_played = 0;
        tech_pvt->checkpoints = NULL;
    }

    size_t buflen = (L16_FRAME_SIZE_8KHZ_20MS * desiredSampling / 8000 * channels * 1000 / RTP_PACKETIZATION_PERIOD *
                     nAudioBufferSecs);

    if (codec == ULAW)
    {
        buflen = (ULAW_FRAME_SIZE_8KHZ_20MS * desiredSampling / 8000 * channels * 1000 / RTP_PACKETIZATION_PERIOD *
                  nAudioBufferSecs);
    }

    AudioPipe *ap = new AudioPipe(tech_pvt->session_id,
                                  tech_pvt->stream_id,
                                  host,
                                  port,
                                  path,
                                  sslFlags,
                                  buflen,
                                  username,
                                  password,
                                  eventCallback,
                                  tech_pvt->track,
                                  tech_pvt->initial_metadata,
                                  codec,
                                  desiredSampling,
                                  is_bidirectional);

    if (!ap)
    {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Error allocating AudioPipe\n");
        return SWITCH_STATUS_FALSE;
    }

    tech_pvt->audio_pipe_ptr = static_cast<void *>(ap);

    switch_mutex_init(&tech_pvt->mutex, SWITCH_MUTEX_NESTED, switch_core_session_get_pool(session));
    if (desiredSampling == sampling)
    {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session),
                          SWITCH_LOG_INFO,
                          "mod_audio_stream(%s) (%u) no resampling needed for this call\n",
                          tech_pvt->stream_id,
                          tech_pvt->id);
    }
    else
    {

        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session),
                          SWITCH_LOG_INFO,
                          "mod_audio_stream(%s) (%u) resampling from %u to %u\n",
                          tech_pvt->stream_id,
                          tech_pvt->id,
                          sampling,
                          desiredSampling);

        tech_pvt->resampler = speex_resampler_init(channels, sampling, desiredSampling, SWITCH_RESAMPLE_QUALITY, &err);
        if (0 != err)
        {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session),
                              SWITCH_LOG_ERROR,
                              "Error initializing resampler: %s.\n",
                              speex_resampler_strerror(err));
            return SWITCH_STATUS_FALSE;
        }
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session),
                          SWITCH_LOG_DEBUG,
                          "mod_audio_stream(%s) tech_pvt->track(%s) track(%s)\n",
                          tech_pvt->stream_id,
                          tech_pvt->track,
                          track);

        if (0 == strcmp(tech_pvt->track, "both"))
        {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session),
                              SWITCH_LOG_INFO,
                              "mod_audio_stream(%s) (%u) resampling from %u to %u\n",
                              tech_pvt->stream_id,
                              tech_pvt->id,
                              sampling,
                              desiredSampling);
            tech_pvt->resampler_outbound =
                speex_resampler_init(channels, sampling, desiredSampling, SWITCH_RESAMPLE_QUALITY, &err);
            if (0 != err)
            {
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session),
                                  SWITCH_LOG_ERROR,
                                  "mod_audio_stream(%s) Error initializing resampler: %s.\n",
                                  tech_pvt->stream_id,
                                  speex_resampler_strerror(err));
                return SWITCH_STATUS_FALSE;
            }
        }
    }

    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%u) stream_data_init\n", tech_pvt->id);
    return SWITCH_STATUS_SUCCESS;
}

void destroy_tech_pvt(private_data_t *tech_pvt)
{
    switch_log_printf(
        SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "%s (%u) destroy_tech_pvt\n", tech_pvt->session_id, tech_pvt->id);
    stream_checkpoints_t *temp = NULL;
    if (tech_pvt->resampler)
    {
        speex_resampler_destroy(tech_pvt->resampler);
        tech_pvt->resampler = nullptr;
    }
    if (tech_pvt->resampler_outbound)
    {
        speex_resampler_destroy(tech_pvt->resampler_outbound);
        tech_pvt->resampler_outbound = nullptr;
    }
    if (tech_pvt->mutex)
    {
        switch_mutex_destroy(tech_pvt->mutex);
        tech_pvt->mutex = nullptr;
    }
    if (tech_pvt->write_buffer)
    {
        switch_buffer_destroy(&tech_pvt->write_buffer);
        tech_pvt->write_buffer = nullptr;
    }
    while (NULL != tech_pvt->checkpoints)
    {
        temp = tech_pvt->checkpoints;
        tech_pvt->checkpoints = tech_pvt->checkpoints->next;
        free(temp->name);
        free(temp);
    }
}

void lws_logger(int level, const char *line)
{
    switch_log_level_t llevel = SWITCH_LOG_DEBUG;

    switch (level)
    {
        case LLL_ERR:
            llevel = SWITCH_LOG_ERROR;
            break;
        case LLL_WARN:
            llevel = SWITCH_LOG_WARNING;
            break;
        case LLL_NOTICE:
            llevel = SWITCH_LOG_NOTICE;
            break;
        case LLL_INFO:
            llevel = SWITCH_LOG_INFO;
            break;
    }
    switch_log_printf(SWITCH_CHANNEL_LOG, llevel, "%s\n", line);
}
} // namespace

extern "C"
{
    int parse_ws_uri(
        switch_channel_t *channel, const char *szServerUri, char *host, char *path, unsigned int *pPort, int *pSslFlags)
    {
        int offset;
        char server[MAX_WEBSOCKET_URL_LENGTH];
        int flags = LCCSCF_USE_SSL;

        if (switch_true(switch_channel_get_variable(channel, "MOD_AUDIO_STREAM_ALLOW_SELFSIGNED")))
        {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "parse_ws_uri - allowing self-signed certs\n");
            flags |= LCCSCF_ALLOW_SELFSIGNED;
        }
        if (switch_true(switch_channel_get_variable(channel, "MOD_AUDIO_STREAM_SKIP_SERVER_CERT_HOSTNAME_CHECK")))
        {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "parse_ws_uri - skipping hostname check\n");
            flags |= LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
        }
        if (switch_true(switch_channel_get_variable(channel, "MOD_AUDIO_STREAM_ALLOW_EXPIRED")))
        {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "parse_ws_uri - allowing expired certs\n");
            flags |= LCCSCF_ALLOW_EXPIRED;
        }

        // get the scheme
        strncpy(server, szServerUri, MAX_WEBSOCKET_URL_LENGTH);
        if (0 == strncmp(server, "https://", 8) || 0 == strncmp(server, "HTTPS://", 8))
        {
            *pSslFlags = flags;
            offset = 8;
            *pPort = 443;
        }
        else if (0 == strncmp(server, "wss://", 6) || 0 == strncmp(server, "WSS://", 6))
        {
            *pSslFlags = flags;
            offset = 6;
            *pPort = 443;
        }
        else if (0 == strncmp(server, "http://", 7) || 0 == strncmp(server, "HTTP://", 7))
        {
            offset = 7;
            *pSslFlags = 0;
            *pPort = 80;
        }
        else if (0 == strncmp(server, "ws://", 5) || 0 == strncmp(server, "WS://", 5))
        {
            offset = 5;
            *pSslFlags = 0;
            *pPort = 80;
        }
        else
        {
            switch_log_printf(SWITCH_CHANNEL_LOG,
                              SWITCH_LOG_NOTICE,
                              "parse_ws_uri - error parsing uri %s: invalid scheme\n",
                              szServerUri);
            return 0;
        }

        std::string strHost(server + offset);
        std::regex re("^(.+?):?(\\d+)?(/.*)?$");
        std::smatch matches;
        if (std::regex_search(strHost, matches, re))
        {
            strncpy(host, matches[1].str().c_str(), MAX_WEBSOCKET_URL_LENGTH);
            if (matches[2].str().length() > 0)
            {
                *pPort = atoi(matches[2].str().c_str());
            }
            if (matches[3].str().length() > 0)
            {
                strncpy(path, matches[3].str().c_str(), MAX_WEBSOCKET_PATH_LENGTH);
            }
            else
            {
                strcpy(path, "/");
            }
        }
        else
        {
            switch_log_printf(
                SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "parse_ws_uri - invalid format %s\n", strHost.c_str());
            return 0;
        }

        return 1;
    }

    switch_status_t stream_init()
    {
        switch_log_printf(SWITCH_CHANNEL_LOG,
                          SWITCH_LOG_NOTICE,
                          "mod_audio_stream: audio buffer (in secs):    %d secs\n",
                          nAudioBufferSecs);
        switch_log_printf(SWITCH_CHANNEL_LOG,
                          SWITCH_LOG_NOTICE,
                          "mod_audio_stream: sub-protocol:              %s\n",
                          mySubProtocolName);
        switch_log_printf(SWITCH_CHANNEL_LOG,
                          SWITCH_LOG_NOTICE,
                          "mod_audio_stream: lws service threads:       %d\n",
                          nServiceThreads);

        int logs = LLL_ERR | LLL_WARN | LLL_NOTICE;
        AudioPipe::initialize(mySubProtocolName, nServiceThreads, logs, lws_logger);
        return SWITCH_STATUS_SUCCESS;
    }

    switch_status_t stream_cleanup()
    {
        AudioPipe::deinitialize();
        return SWITCH_STATUS_SUCCESS;
    }

    switch_status_t stream_session_init(switch_core_session_t *session,
                                        char *stream_id,
                                        char *service_url,
                                        response_handler_t response_handler,
                                        uint32_t samples_per_second,
                                        char *host,
                                        unsigned int port,
                                        char *path,
                                        char *codec_str,
                                        int sampling,
                                        int sslFlags,
                                        int channels,
                                        char *track,
                                        int is_bidirectional,
                                        int timeout,
                                        char *metadata,
                                        void **ppUserData)
    {
        switch_status_t status;
        streaming_codec_t codec = L16;

        if (codec_str != NULL && 0 == strcmp(codec_str, "mulaw"))
        {
            codec = ULAW;
        }

        // allocate per-session data structure
        private_data_t *tech_pvt = (private_data_t *)switch_core_session_alloc(session, sizeof(private_data_t));
        if (!tech_pvt)
        {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "error allocating memory!\n");
            return SWITCH_STATUS_FALSE;
        }
        status = stream_data_init(tech_pvt,
                                  stream_id,
                                  session,
                                  service_url,
                                  host,
                                  port,
                                  path,
                                  sslFlags,
                                  codec,
                                  samples_per_second,
                                  sampling,
                                  channels,
                                  track,
                                  is_bidirectional,
                                  timeout,
                                  metadata,
                                  response_handler);

        if (SWITCH_STATUS_SUCCESS != status)
        {
            destroy_tech_pvt(tech_pvt);
            return SWITCH_STATUS_FALSE;
        }

        *ppUserData = tech_pvt;

        AudioPipe *audio_pipe_ptr = static_cast<AudioPipe *>(tech_pvt->audio_pipe_ptr);
        audio_pipe_ptr->connect();
        return SWITCH_STATUS_SUCCESS;
    }

    switch_status_t stream_ws_send_played_event(private_data_t *tech_pvt, const char *name)
    {
        if (!tech_pvt)
            return SWITCH_STATUS_FALSE;

        AudioPipe *audio_pipe_ptr = static_cast<AudioPipe *>(tech_pvt->audio_pipe_ptr);
        if (audio_pipe_ptr)
        {
            cJSON *data = cJSON_CreateObject();
            char *result = NULL;
            cJSON *member = NULL;

            member = cJSON_CreateString("playedStream");
            cJSON_AddItemToObject(data, "event", member);

            member = cJSON_CreateNumber(audio_pipe_ptr->getSequenceNumber());
            cJSON_AddItemToObject(data, "sequenceNumber", member);

            member = cJSON_CreateString(tech_pvt->stream_id);
            cJSON_AddItemToObject(data, "stream_id", member);

            member = cJSON_CreateString(name);
            cJSON_AddItemToObject(data, "name", member);

            result = cJSON_Print(data);
            cJSON_Delete(data);

            if (result == NULL)
            {
                return SWITCH_STATUS_FALSE;
            }

            audio_pipe_ptr->addEventBuffer(std::string(result));
            audio_pipe_ptr->increaseSequenceNumber();
            free(result);
        }

        return SWITCH_STATUS_SUCCESS;
    }

    switch_status_t stream_ws_close_connection(private_data_t *tech_pvt)
    {
        if (!tech_pvt)
            return SWITCH_STATUS_FALSE;

        AudioPipe *audio_pipe_ptr = static_cast<AudioPipe *>(tech_pvt->audio_pipe_ptr);
        if (audio_pipe_ptr)
        {
            tech_pvt->channel_closing = 1;
            audio_pipe_ptr->m_connection_attempts = MAX_CONNECTION_ATTEMPTS + 1;
            audio_pipe_ptr->close();
        }

        return SWITCH_STATUS_SUCCESS;
    }

    switch_status_t stream_session_cleanup(
        switch_core_session_t *session, char *stream_id, char *text, int channelIsClosing, int zeroBilling)
    {
        switch_channel_t *channel = switch_core_session_get_channel(session);
        switch_media_bug_t *bug = (switch_media_bug_t *)switch_channel_get_private(channel, stream_id);
        if (!bug)
        {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session),
                              SWITCH_LOG_DEBUG,
                              "stream_session_cleanup: no bug - websocket conection already closed\n");
            return SWITCH_STATUS_FALSE;
        }
        media_bug_callback_args_t *bug_args = (media_bug_callback_args_t *)switch_core_media_bug_get_user_data(bug);
        private_data_t *tech_pvt = (bug_args) ? bug_args->private_data_ptr : NULL;

        if (!tech_pvt)
        {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session),
                              SWITCH_LOG_DEBUG,
                              "stream_session_cleanup: private data is null. \n");
            return SWITCH_STATUS_FALSE;
        }

        if (switch_mutex_trylock(bug_args->mutex) != SWITCH_STATUS_SUCCESS)
        {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session),
                              SWITCH_LOG_DEBUG,
                              "mod_audio_stream(%s): stream_session_cleanup: lock not acquired, exiting.\n",
                              stream_id);
            return SWITCH_STATUS_SUCCESS;
        }

        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session),
                          SWITCH_LOG_INFO,
                          "mod_audio_stream(%s): stream_session_cleanup:  started.\n",
                          stream_id);
        uint32_t id = tech_pvt->id;
        if (tech_pvt->start_time == 0)
        {
            lwsl_err("mod_audio_stream: (%s) got stop before starting the stream.", stream_id);
            zeroBilling = 1;
        }

        tech_pvt->end_time = switch_epoch_time_now(NULL);

        switch_scheduler_del_task_id(tech_pvt->task_id);
        switch_scheduler_del_task_id(tech_pvt->stream_timeout_task_id);

        if (zeroBilling)
            tech_pvt->start_time = tech_pvt->end_time;

        switch_mutex_lock(tech_pvt->mutex);
        {
            bug = (switch_media_bug_t *)switch_channel_get_private(channel, stream_id);
            if (bug)
            {
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session),
                                  SWITCH_LOG_DEBUG,
                                  "mod_audio_stream(%s): Removing channel private value.\n",
                                  stream_id);
                switch_channel_set_private(channel, stream_id, NULL);
                if (!channelIsClosing)
                {
                    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session),
                                      SWITCH_LOG_INFO,
                                      "mod_audio_stream(%s): Removing bug.\n",
                                      stream_id);
                    switch_core_media_bug_remove(session, &bug);
                }
            }
        }

        struct playout *playout = tech_pvt->playout;
        while (playout)
        {
            std::remove(playout->file);
            free(playout->file);
            struct playout *tmp = playout;
            playout = playout->next;
            free(tmp);
        }

        destroy_tech_pvt(tech_pvt);
        bug_args->private_data_ptr = NULL;
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session),
                          SWITCH_LOG_INFO,
                          "mod_audio_stream(%s) stream_session_cleanup: connection closed\n",
                          stream_id);
        return SWITCH_STATUS_SUCCESS;
    }

    switch_status_t stream_session_send_text(switch_core_session_t *session, char *stream_id, char *text)
    {
        switch_channel_t *channel = switch_core_session_get_channel(session);
        switch_media_bug_t *bug = (switch_media_bug_t *)switch_channel_get_private(channel, stream_id);

        if (!bug)
        {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session),
                              SWITCH_LOG_ERROR,
                              "mod_audio_stream(%s) stream_session_send_text failed because no bug\n",
                              stream_id);
            return SWITCH_STATUS_FALSE;
        }

        media_bug_callback_args_t *bug_args = (media_bug_callback_args_t *)switch_core_media_bug_get_user_data(bug);
        private_data_t *tech_pvt = (bug_args) ? bug_args->private_data_ptr : NULL;

        if (!tech_pvt)
            return SWITCH_STATUS_FALSE;
        AudioPipe *audio_pipe_ptr = static_cast<AudioPipe *>(tech_pvt->audio_pipe_ptr);
        if (audio_pipe_ptr && text)
            audio_pipe_ptr->addEventBuffer(std::string(text));

        return SWITCH_STATUS_SUCCESS;
    }

    switch_status_t stream_session_pauseresume(switch_core_session_t *session, char *stream_id, int pause)
    {
        switch_channel_t *channel = switch_core_session_get_channel(session);
        switch_media_bug_t *bug = (switch_media_bug_t *)switch_channel_get_private(channel, stream_id);
        if (!bug)
        {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session),
                              SWITCH_LOG_ERROR,
                              "stream_session_pauseresume failed because no bug\n");
            return SWITCH_STATUS_FALSE;
        }
        media_bug_callback_args_t *bug_args = (media_bug_callback_args_t *)switch_core_media_bug_get_user_data(bug);
        private_data_t *tech_pvt = (bug_args) ? bug_args->private_data_ptr : NULL;

        if (!tech_pvt)
            return SWITCH_STATUS_FALSE;

        switch_core_media_bug_flush(bug);
        tech_pvt->audio_paused = pause;
        return SWITCH_STATUS_SUCCESS;
    }

    switch_status_t stream_session_graceful_shutdown(switch_core_session_t *session, char *stream_id)
    {
        switch_channel_t *channel = switch_core_session_get_channel(session);
        switch_media_bug_t *bug = (switch_media_bug_t *)switch_channel_get_private(channel, stream_id);
        if (!bug)
        {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session),
                              SWITCH_LOG_ERROR,
                              "stream_session_graceful_shutdown failed because no bug\n");
            return SWITCH_STATUS_FALSE;
        }
        media_bug_callback_args_t *bug_args = (media_bug_callback_args_t *)switch_core_media_bug_get_user_data(bug);
        private_data_t *tech_pvt = (bug_args) ? bug_args->private_data_ptr : NULL;

        if (!tech_pvt)
            return SWITCH_STATUS_FALSE;

        tech_pvt->graceful_shutdown = 1;

        AudioPipe *audio_pipe_ptr = static_cast<AudioPipe *>(tech_pvt->audio_pipe_ptr);
        if (audio_pipe_ptr)
            audio_pipe_ptr->graceful_shutdown();

        return SWITCH_STATUS_SUCCESS;
    }

    static switch_status_t
    g711u_encode(void *decoded_data, uint32_t decoded_data_len, void *encoded_data, uint32_t *encoded_data_len)
    {
        short *dbuf;
        unsigned char *ebuf;
        uint32_t i;

        dbuf = (short int *)decoded_data;
        ebuf = (unsigned char *)encoded_data;

        for (i = 0; i < decoded_data_len / sizeof(short); i++)
        {
            ebuf[i] = linear_to_ulaw(dbuf[i]);
        }

        *encoded_data_len = i;

        return SWITCH_STATUS_SUCCESS;
    }

    switch_bool_t stream_frame(switch_core_session_t *session, switch_media_bug_t *bug)
    {
        media_bug_callback_args_t *bug_args = (media_bug_callback_args_t *)switch_core_media_bug_get_user_data(bug);
        private_data_t *tech_pvt = (bug_args) ? bug_args->private_data_ptr : NULL;
        int type;
        Buffer *audioBuffer;
        SpeexResamplerState *resampler = NULL;

        if (!tech_pvt || tech_pvt->audio_paused || tech_pvt->graceful_shutdown || !tech_pvt->mutex)
            return SWITCH_TRUE;

        if (switch_mutex_trylock(tech_pvt->mutex) == SWITCH_STATUS_SUCCESS)
        {
            if (!tech_pvt->audio_pipe_ptr)
            {
                switch_mutex_unlock(tech_pvt->mutex);
                return SWITCH_TRUE;
            }
            AudioPipe *audio_pipe_ptr = static_cast<AudioPipe *>(tech_pvt->audio_pipe_ptr);
            if (audio_pipe_ptr->getLwsState() != AudioPipe::LWS_CLIENT_CONNECTED)
            {
                switch_mutex_unlock(tech_pvt->mutex);
                return SWITCH_TRUE;
            }

            if (audio_pipe_ptr->needsBothTracks())
            {
                if (bug_args->bug_type == MEDIA_BUG_INBOUND)
                {
                    type = 0;
                    audioBuffer = audio_pipe_ptr->m_audio_buffer;
                    resampler = tech_pvt->resampler;
                }
                else
                {
                    type = 1;
                    audioBuffer = audio_pipe_ptr->m_ob_audio_buffer;
                    resampler = tech_pvt->resampler_outbound;
                }
            }
            else
            {
                type = (bug_args->bug_type == MEDIA_BUG_INBOUND) ? 0 : 1;
                audioBuffer = audio_pipe_ptr->m_audio_buffer;
                resampler = tech_pvt->resampler;
            }

            if (audioBuffer->lock())
            {
                uint16_t data[SWITCH_RECOMMENDED_BUFFER_SIZE];
                uint16_t encoded_data[SWITCH_RECOMMENDED_BUFFER_SIZE];
                bool write_success = true;
                switch_frame_t frame = {};
                uint32_t encoded_data_len = 0;
                frame.data = data;
                frame.buflen = SWITCH_RECOMMENDED_BUFFER_SIZE;
                while (switch_core_media_bug_read(bug, &frame, SWITCH_TRUE) == SWITCH_STATUS_SUCCESS &&
                       !switch_test_flag((&frame), SFF_CNG))
                {
                    if (frame.datalen)
                    {
                        if (resampler != NULL)
                        {
                            spx_int16_t out[SWITCH_RECOMMENDED_BUFFER_SIZE];
                            spx_uint32_t out_len = SWITCH_RECOMMENDED_BUFFER_SIZE;
                            spx_uint32_t in_len = frame.samples;

                            speex_resampler_process_interleaved_int(
                                resampler, (const spx_int16_t *)frame.data, (spx_uint32_t *)&in_len, &out[0], &out_len);

                            write_success = audioBuffer->write(&out[0]);
                        }
                        else
                        {
                            if (audio_pipe_ptr->m_codec == ULAW)
                            {
                                g711u_encode(frame.data, frame.datalen, &encoded_data, &encoded_data_len);
                                write_success = audioBuffer->write(encoded_data);
                            }
                            else
                            {
                                write_success = audioBuffer->write(frame.data);
                            }
                        }

                        if (audioBuffer->current_usage_bytes_ >
                            (audioBuffer->maximum_capacity_bytes_ * (audioBuffer->degradation_notification_sent_ * .3)))
                        {
                            switch_log_printf(SWITCH_CHANNEL_LOG,
                                              SWITCH_LOG_ERROR,
                                              "(%s) notification (%d) degraded connection. buffer_used(%d) max_len(%d)",
                                              tech_pvt->stream_id,
                                              audioBuffer->degradation_notification_sent_,
                                              audioBuffer->current_usage_bytes_,
                                              audioBuffer->maximum_capacity_bytes_);
                            audio_pipe_ptr->m_callback(audio_pipe_ptr->m_uuid.c_str(),
                                                       audio_pipe_ptr->m_streamid.c_str(),
                                                       AudioPipe::CONNECTION_DEGRADED,
                                                       "");
                            audioBuffer->degradation_notification_sent_ += 1;
                        }

                        if (!write_success)
                        {
                            lwsl_err("mod_audio_stream(%s) buffer writing failed. shutdown.", tech_pvt->stream_id);
                            audio_pipe_ptr->m_callback(audio_pipe_ptr->m_uuid.c_str(),
                                                       audio_pipe_ptr->m_streamid.c_str(),
                                                       AudioPipe::CONNECTION_TIMEOUT,
                                                       "");
                        }
                    }
                }
                if (write_success)
                    audio_pipe_ptr->addPendingWrite(audio_pipe_ptr);
                audioBuffer->unlock();
            }
            switch_mutex_unlock(tech_pvt->mutex);
        }
        return SWITCH_TRUE;
    }
}
