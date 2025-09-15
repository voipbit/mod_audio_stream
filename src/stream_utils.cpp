// SPDX-License-Identifier: MIT
#include "stream_utils.hpp"
#include "base64.hpp"
#include <fstream>
#include <switch.h>

Buffer::Buffer(std::string &stream_id, size_t max_len, int step_buffer_len, int step_time_increase)
    : stream_identifier_(stream_id), freeswitch_buffer_(nullptr),
      time_step_increment_(step_time_increase * 1000), // Convert to microseconds
      generated_chunk_count_(0), transmitted_chunk_count_(0), degradation_notification_sent_(1)
{
    maximum_capacity_bytes_ = max_len;
    switch_buffer_create_dynamic(&freeswitch_buffer_, max_len / 10, max_len / 10, max_len);
    chunk_size_bytes_ = step_buffer_len;
    current_usage_bytes_ = 0;
    start_time_ = switch_micro_time_now();
    generated_time_ = switch_micro_time_now();
    last_send_time_ = generated_time_;
    generated_chunk_count_ = 0;
    transmitted_chunk_count_ = 0;
}

Buffer::~Buffer()
{
    if (freeswitch_buffer_)
        switch_buffer_destroy(&freeswitch_buffer_);
}

bool Buffer::read(void **data, size_t *datalen)
{
    switch_size_t actual_read;
    uint8_t *buffer_copy = (uint8_t *)malloc(sizeof(uint8_t) * chunk_size_bytes_);
    actual_read = switch_buffer_read(freeswitch_buffer_, buffer_copy, chunk_size_bytes_);
    if (actual_read != chunk_size_bytes_)
    {
        free(buffer_copy);
        *datalen = 0;
        return false;
    }
    current_usage_bytes_ = switch_buffer_inuse(freeswitch_buffer_);
    *data = buffer_copy;
    *datalen = chunk_size_bytes_;
    last_send_time_ += time_step_increment_;
    transmitted_chunk_count_ += 1;
    return true;
}

bool Buffer::write(void *data)
{
    switch_size_t w;
    if (!(w = switch_buffer_write(freeswitch_buffer_, data, chunk_size_bytes_)))
    {
        switch_log_printf(SWITCH_CHANNEL_LOG,
                          SWITCH_LOG_ERROR,
                          "mod_audio_stream(%s) Buffer:write failed.",
                          stream_identifier_.c_str());
        return false;
    }
    current_usage_bytes_ = switch_buffer_inuse(freeswitch_buffer_);
    generated_time_ += time_step_increment_;
    generated_chunk_count_ += 1;
    return true;
}

char *generate_json_data_event(client_event_type_t event,
                               int sequenceNumber,
                               std::string &uuid,
                               std::string &streamid,
                               std::string &track,
                               Buffer *buffer,
                               std::string &extraHeaders,
                               streaming_codec_t codec,
                               int sampling)
{
    cJSON *data = cJSON_CreateObject();
    char *result = NULL;
    cJSON *member = NULL;
    void *payload = NULL;

    switch (event)
    {
        case CLIENT_EVENT_START:
        {
            cJSON *start;
            cJSON *media_format;

            member = cJSON_CreateNumber(sequenceNumber);
            cJSON_AddItemToObject(data, "sequenceNumber", member);

            member = cJSON_CreateString("start");
            cJSON_AddItemToObject(data, "event", member);

            start = cJSON_AddObjectToObject(data, "start");

            member = cJSON_CreateString(uuid.c_str());
            cJSON_AddItemToObject(start, "callId", member);

            member = cJSON_CreateString(streamid.c_str());
            cJSON_AddItemToObject(start, "stream_id", member);

            {
                cJSON *tracks = cJSON_AddArrayToObject(start, "tracks");
                if (track == "both")
                {
                    member = cJSON_CreateString("inbound");
                    cJSON_AddItemToArray(tracks, member);
                    member = cJSON_CreateString("outbound");
                    cJSON_AddItemToArray(tracks, member);
                }
                else
                {
                    member = cJSON_CreateString(track.c_str());
                    cJSON_AddItemToArray(tracks, member);
                }
            }

            media_format = cJSON_AddObjectToObject(start, "mediaFormat");
            if (codec == L16)
            {
                member = cJSON_CreateString("audio/x-l16");
            }
            else
            {
                member = cJSON_CreateString("audio/x-mulaw");
            }
            cJSON_AddItemToObject(media_format, "encoding", member);

            member = cJSON_CreateNumber(sampling);
            cJSON_AddItemToObject(media_format, "sampleRate", member);

            if (extraHeaders.length() > 0)
            {
                member = cJSON_CreateString(extraHeaders.c_str());
                cJSON_AddItemToObject(data, "extra_headers", member);
            }
            break;
        }
        case CLIENT_EVENT_MEDIA:
        {
            char time_str[15];
            size_t datalen = 0;
            buffer->read(&payload, &datalen);
            if (payload == NULL || datalen == 0)
            {
                cJSON_Delete(data);
                return NULL;
            }
            cJSON *media;
            member = cJSON_CreateNumber(sequenceNumber);
            cJSON_AddItemToObject(data, "sequenceNumber", member);
            member = cJSON_CreateString(streamid.c_str());
            cJSON_AddItemToObject(data, "stream_id", member);
            member = cJSON_CreateString("media");
            cJSON_AddItemToObject(data, "event", member);
            media = cJSON_AddObjectToObject(data, "media");
            member = cJSON_CreateString(track.c_str());
            cJSON_AddItemToObject(media, "track", member);
            snprintf(time_str, 14, "%ld", buffer->last_send_time_);
            member = cJSON_CreateString(time_str);
            cJSON_AddItemToObject(media, "timestamp", member);
            member = cJSON_CreateNumber(buffer->transmitted_chunk_count_);
            cJSON_AddItemToObject(media, "chunk", member);
            std::string payload_str = base64::base64_encode((unsigned char *)payload, datalen);
            member = cJSON_CreateString(payload_str.c_str());
            cJSON_AddItemToObject(media, "payload", member);
            if (extraHeaders.length() > 0)
            {
                member = cJSON_CreateString(extraHeaders.c_str());
                cJSON_AddItemToObject(data, "extra_headers", member);
            }
            break;
        }
        case CLIENT_EVENT_STOP:
        {
            cJSON *stop;
            member = cJSON_CreateNumber(sequenceNumber);
            cJSON_AddItemToObject(data, "sequenceNumber", member);
            member = cJSON_CreateString(streamid.c_str());
            cJSON_AddItemToObject(data, "stream_id", member);
            member = cJSON_CreateString("stop");
            cJSON_AddItemToObject(data, "event", member);
            stop = cJSON_AddObjectToObject(data, "stop");
            member = cJSON_CreateString(uuid.c_str());
            cJSON_AddItemToObject(stop, "callId", member);
            if (extraHeaders.length() > 0)
            {
                member = cJSON_CreateString(extraHeaders.c_str());
                cJSON_AddItemToObject(data, "extra_headers", member);
            }
            break;
        }
    }

    result = cJSON_Print(data);
    cJSON_Delete(data);
    if (result == NULL)
    {
        fprintf(stderr, "Failed to print monitor.\n");
        return NULL;
    }
    if (payload != NULL)
        free(payload);
    return result;
}
