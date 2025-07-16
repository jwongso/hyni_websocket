#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <cstring>

/**
 * @struct hyni_audio_data
 * @brief Structure representing audio data received from a client
 */
struct hyni_audio_data {
    /// 16-bit PCM audio samples
    std::vector<int16_t> samples;

    /// Sample rate (typically 16000 Hz)
    uint32_t sample_rate = 16000;

    /// Number of channels (typically 1 for mono)
    uint16_t channels = 1;

    /// Session identifier for the client
    std::string session_id;

    /// Language hint for transcription (optional)
    std::string language;

    /// Timestamp when audio was captured (optional)
    uint64_t timestamp = 0;

    /**
     * @brief Default constructor
     */
    hyni_audio_data() = default;

    /**
     * @brief Constructs audio data with basic parameters
     */
    hyni_audio_data(std::vector<int16_t> pcm_samples, uint32_t rate = 16000, uint16_t ch = 1)
        : samples(std::move(pcm_samples)), sample_rate(rate), channels(ch) {}

    /**
     * @brief Checks if audio data is valid
     */
    bool is_valid() const {
        return !samples.empty() && sample_rate > 0 && channels > 0;
    }

    /**
     * @brief Gets duration of audio in milliseconds
     */
    double duration_ms() const {
        if (sample_rate == 0 || channels == 0) return 0.0;
        return (static_cast<double>(samples.size()) / (sample_rate * channels)) * 1000.0;
    }
};

// Optimized decode table (compile-time computed)
static constexpr int8_t decode_table[256] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  // 0-15
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  // 16-31
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,  // 32-47 ('+' and '/')
    52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,  // 48-63 ('0'-'9')
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14, // 64-79 ('A'-'O')
    15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,  // 80-95 ('P'-'Z')
    -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40, // 96-111 ('a'-'o')
    41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,  // 112-127 ('p'-'z')
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  // 128-143
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  // 144-159
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  // 160-175
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  // 176-191
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  // 192-207
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  // 208-223
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  // 224-239
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1   // 240-255
};

/**
     * @brief Fast in-place decode for int16_t audio data (specialized)
     * @param encoded Base64 encoded string
     * @param output Output vector to store decoded int16_t samples
     * @return true if successful, false if invalid input
     */
inline bool decode_audio_fast(const std::string& encoded, std::vector<int16_t>& output) {
    if (encoded.empty()) {
        output.clear();
        return true;
    }

    const size_t in_len = encoded.size();
    const size_t estimated_bytes = (in_len * 3) / 4;
    const size_t estimated_samples = estimated_bytes / sizeof(int16_t);

    // Pre-allocate output vector
    output.clear();
    output.reserve(estimated_samples + 1); // +1 for safety

    uint32_t val = 0;
    int bits = -8;
    uint8_t byte_buffer[2];
    int buffer_pos = 0;

    for (size_t i = 0; i < in_len; ++i) {
        const uint8_t c = encoded[i];
        if (c == '=') break;

        const int8_t decoded = decode_table[c];
        if (decoded == -1) continue;

        val = (val << 6) | decoded;
        bits += 6;

        if (bits >= 0) {
            const uint8_t byte = static_cast<uint8_t>((val >> bits) & 0xFF);
            byte_buffer[buffer_pos++] = byte;

            // When we have 2 bytes, convert to int16_t
            if (buffer_pos == 2) {
                int16_t sample;
                std::memcpy(&sample, byte_buffer, sizeof(int16_t));
                output.push_back(sample);
                buffer_pos = 0;
            }

            bits -= 8;
        }
    }

    // Handle leftover byte (should be rare for audio data)
    if (buffer_pos == 1) {
        // Pad with zero and add final sample
        byte_buffer[1] = 0;
        int16_t sample;
        std::memcpy(&sample, byte_buffer, sizeof(int16_t));
        output.push_back(sample);
    }

    return true;
}
