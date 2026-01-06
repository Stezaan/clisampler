#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <cstring>
#include <fstream>  // Added missing include

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libavutil/avutil.h>
    #include <libswresample/swresample.h>
    #include <libavutil/opt.h>
    #include <libavutil/channel_layout.h>  // Added for channel layout
}

class AudioDownsampler {
private:
    std::vector<int16_t> audioData;
    uint32_t originalSampleRate;
    uint16_t channels;
    std::string formatName;
    
    bool convertAudio(const std::string& inputFile) {
        AVFormatContext* formatContext = nullptr;
        AVCodecContext* codecContext = nullptr;
        SwrContext* swrContext = nullptr;
        AVPacket* packet = nullptr;
        AVFrame* frame = nullptr;
        
        try {
            // Initialize FFmpeg
            avformat_network_init();
            
            // Open input file
            if (avformat_open_input(&formatContext, inputFile.c_str(), nullptr, nullptr) != 0) {
                std::cerr << "Error: Cannot open input file: " << inputFile << std::endl;
                return false;
            }
            
            // Find stream info
            if (avformat_find_stream_info(formatContext, nullptr) < 0) {
                std::cerr << "Error: Cannot find stream information" << std::endl;
                avformat_close_input(&formatContext);
                return false;
            }
            
            // Find audio stream
            int audioStreamIndex = -1;
            for (unsigned int i = 0; i < formatContext->nb_streams; i++) {
                if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                    audioStreamIndex = i;
                    break;
                }
            }
            
            if (audioStreamIndex == -1) {
                std::cerr << "Error: No audio stream found" << std::endl;
                avformat_close_input(&formatContext);
                return false;
            }
            
            // Get codec parameters
            AVCodecParameters* codecParams = formatContext->streams[audioStreamIndex]->codecpar;
            
            // Find decoder
            const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
            if (!codec) {
                std::cerr << "Error: Unsupported codec" << std::endl;
                avformat_close_input(&formatContext);
                return false;
            }
            
            // Create codec context
            codecContext = avcodec_alloc_context3(codec);
            if (!codecContext) {
                std::cerr << "Error: Cannot allocate codec context" << std::endl;
                avformat_close_input(&formatContext);
                return false;
            }
            
            // Copy codec parameters to context
            if (avcodec_parameters_to_context(codecContext, codecParams) < 0) {
                std::cerr << "Error: Failed to copy codec parameters" << std::endl;
                avcodec_free_context(&codecContext);
                avformat_close_input(&formatContext);
                return false;
            }
            
            // Open codec
            if (avcodec_open2(codecContext, codec, nullptr) < 0) {
                std::cerr << "Error: Cannot open codec" << std::endl;
                avcodec_free_context(&codecContext);
                avformat_close_input(&formatContext);
                return false;
            }
            
            // Store original parameters
            originalSampleRate = codecContext->sample_rate;
            channels = codecContext->ch_layout.nb_channels;
            formatName = avcodec_get_name(codecParams->codec_id);
            
            std::cout << "Input file information:\n";
            std::cout << "  Format: " << formatName << "\n";
            std::cout << "  Sample Rate: " << originalSampleRate << " Hz\n";
            std::cout << "  Channels: " << channels << "\n";
            if (formatContext->duration != AV_NOPTS_VALUE) {
                std::cout << "  Duration: " << formatContext->duration / AV_TIME_BASE << " seconds\n";
            }
            
            // Prepare resampler for conversion to S16 stereo
            swrContext = swr_alloc();
            
            // Set input parameters
            av_opt_set_chlayout(swrContext, "in_chlayout", &codecContext->ch_layout, 0);
            av_opt_set_int(swrContext, "in_sample_rate", codecContext->sample_rate, 0);
            av_opt_set_sample_fmt(swrContext, "in_sample_fmt", codecContext->sample_fmt, 0);
            
            // Set output parameters (stereo S16)
            AVChannelLayout out_chlayout = AV_CHANNEL_LAYOUT_STEREO;
            av_opt_set_chlayout(swrContext, "out_chlayout", &out_chlayout, 0);
            av_opt_set_int(swrContext, "out_sample_rate", codecContext->sample_rate, 0);
            av_opt_set_sample_fmt(swrContext, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
            
            if (swr_init(swrContext) < 0) {
                std::cerr << "Error: Cannot initialize resampler" << std::endl;
                swr_free(&swrContext);
                avcodec_free_context(&codecContext);
                avformat_close_input(&formatContext);
                return false;
            }
            
            // Allocate packet and frame
            packet = av_packet_alloc();
            frame = av_frame_alloc();
            
            if (!packet || !frame) {
                std::cerr << "Error: Cannot allocate packet/frame" << std::endl;
                throw std::runtime_error("Memory allocation failed");
            }
            
            // Read and decode audio
            std::vector<int16_t> tempBuffer;
            
            while (av_read_frame(formatContext, packet) >= 0) {
                if (packet->stream_index == audioStreamIndex) {
                    // Send packet to decoder
                    int ret = avcodec_send_packet(codecContext, packet);
                    if (ret < 0) {
                        av_packet_unref(packet);
                        continue;
                    }
                    
                    // Receive frames from decoder
                    while (avcodec_receive_frame(codecContext, frame) >= 0) {
                        // Calculate output samples
                        int64_t outSamples = swr_get_out_samples(swrContext, frame->nb_samples);
                        if (outSamples <= 0) continue;
                        
                        // Allocate output buffer
                        uint8_t* outData[1] = {nullptr};
                        int outLinesize;
                        
                        // Allocate buffer for output samples
                        int bufferSize = av_samples_alloc(outData, &outLinesize, 2, 
                                                         outSamples, AV_SAMPLE_FMT_S16, 0);
                        if (bufferSize < 0) {
                            continue;
                        }
                        
                        // Convert samples
                        int converted = swr_convert(swrContext, 
                                                   outData, outSamples,
                                                   (const uint8_t**)frame->data, frame->nb_samples);
                        
                        if (converted > 0) {
                            size_t samplesConverted = converted * 2; // 2 channels
                            size_t oldSize = tempBuffer.size();
                            tempBuffer.resize(oldSize + samplesConverted);
                            
                            // Copy converted data
                            memcpy(tempBuffer.data() + oldSize, outData[0], 
                                   samplesConverted * sizeof(int16_t));
                        }
                        
                        // Free allocated buffer
                        if (outData[0]) {
                            av_freep(&outData[0]);
                        }
                    }
                }
                av_packet_unref(packet);
            }
            
            // Flush decoder
            avcodec_send_packet(codecContext, nullptr);
            while (avcodec_receive_frame(codecContext, frame) >= 0) {
                // Process any remaining frames
                int64_t outSamples = swr_get_out_samples(swrContext, frame->nb_samples);
                if (outSamples > 0) {
                    uint8_t* outData[1] = {nullptr};
                    int outLinesize;
                    
                    int bufferSize = av_samples_alloc(outData, &outLinesize, 2, 
                                                     outSamples, AV_SAMPLE_FMT_S16, 0);
                    if (bufferSize >= 0) {
                        int converted = swr_convert(swrContext, 
                                                   outData, outSamples,
                                                   (const uint8_t**)frame->data, frame->nb_samples);
                        
                        if (converted > 0) {
                            size_t samplesConverted = converted * 2;
                            size_t oldSize = tempBuffer.size();
                            tempBuffer.resize(oldSize + samplesConverted);
                            memcpy(tempBuffer.data() + oldSize, outData[0], 
                                   samplesConverted * sizeof(int16_t));
                        }
                        
                        if (outData[0]) {
                            av_freep(&outData[0]);
                        }
                    }
                }
            }
            
            // Store the data
            audioData = std::move(tempBuffer);
            
            std::cout << "Successfully loaded " << audioData.size() << " samples (" 
                      << audioData.size() / 2 << " stereo frames)\n";
            
        } catch (const std::exception& e) {
            std::cerr << "Error during audio processing: " << e.what() << std::endl;
        }
        
        // Cleanup
        if (swrContext) swr_free(&swrContext);
        if (frame) av_frame_free(&frame);
        if (packet) av_packet_free(&packet);
        if (codecContext) avcodec_free_context(&codecContext);
        if (formatContext) avformat_close_input(&formatContext);
        
        return !audioData.empty();
    }
    
public:
    AudioDownsampler() : originalSampleRate(0), channels(0) {}
    
    bool loadAudioFile(const std::string& inputFile) {
        // Initialize FFmpeg
        av_log_set_level(AV_LOG_ERROR);
        
        // Try with FFmpeg
        if (convertAudio(inputFile)) {
            return true;
        }
        
        std::cerr << "FFmpeg failed, trying WAV fallback..." << std::endl;
        
        // Fallback to WAV-only implementation
        return loadWavFileFallback(inputFile);
    }
    
    // Simple WAV-only fallback implementation
    bool loadWavFileFallback(const std::string& filename) {
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Error: Cannot open WAV file: " << filename << std::endl;
            return false;
        }
        
        // Read RIFF header
        char riff[5] = {0}, wave[5] = {0};
        file.read(riff, 4);
        riff[4] = '\0';
        
        uint32_t fileSize;
        file.read(reinterpret_cast<char*>(&fileSize), 4);
        
        file.read(wave, 4);
        wave[4] = '\0';
        
        if (strcmp(riff, "RIFF") != 0 || strcmp(wave, "WAVE") != 0) {
            std::cerr << "Error: Not a valid WAV file" << std::endl;
            return false;
        }
        
        // Skip to data (simplified)
        file.seekg(36, std::ios::beg);
        
        // Read data size
        char dataId[5] = {0};
        uint32_t dataSize;
        file.read(dataId, 4);
        dataId[4] = '\0';
        file.read(reinterpret_cast<char*>(&dataSize), 4);
        
        if (strcmp(dataId, "data") != 0) {
            std::cerr << "Error: Cannot find data chunk" << std::endl;
            return false;
        }
        
        // Read audio data (assuming 16-bit stereo)
        channels = 2;
        originalSampleRate = 44100; // Default assumption
        audioData.resize(dataSize / sizeof(int16_t));
        file.read(reinterpret_cast<char*>(audioData.data()), dataSize);
        
        std::cout << "Loaded WAV file (simplified parser):\n";
        std::cout << "  Samples: " << audioData.size() << "\n";
        std::cout << "  Channels: 2 (assumed)\n";
        std::cout << "  Sample Rate: 44100 Hz (assumed)\n";
        
        return !audioData.empty();
    }
    
    std::vector<int16_t> downsample(uint32_t targetSampleRate) {
        if (audioData.empty()) {
            std::cerr << "Error: No audio data loaded" << std::endl;
            return {};
        }
        
        if (targetSampleRate == 0 || targetSampleRate >= originalSampleRate) {
            std::cerr << "Error: Target sample rate must be > 0 and < " << originalSampleRate 
                      << ". Got: " << targetSampleRate << std::endl;
            return audioData;
        }
        
        double ratio = static_cast<double>(originalSampleRate) / targetSampleRate;
        size_t newSize = static_cast<size_t>(audioData.size() / ratio);
        
        if (newSize == 0) {
            std::cerr << "Error: Downsampling would result in empty audio" << std::endl;
            return {};
        }
        
        std::vector<int16_t> downsampled(newSize);
        
        std::cout << "\nDownsampling:\n";
        std::cout << "  Original rate: " << originalSampleRate << " Hz\n";
        std::cout << "  Target rate: " << targetSampleRate << " Hz\n";
        std::cout << "  Ratio: " << ratio << "\n";
        std::cout << "  Original samples: " << audioData.size() << "\n";
        std::cout << "  New samples: " << newSize << "\n";
        
        // Simple decimation
        for (size_t i = 0; i < newSize; i++) {
            size_t srcIndex = static_cast<size_t>(i * ratio);
            if (srcIndex < audioData.size()) {
                downsampled[i] = audioData[srcIndex];
            }
        }
        
        std::cout << "Downsampling complete!\n";
        return downsampled;
    }
    
    bool saveAsWav(const std::string& filename, 
                   const std::vector<int16_t>& data, 
                   uint32_t sampleRate) {
        
        if (data.empty()) {
            std::cerr << "Error: No audio data to save" << std::endl;
            return false;
        }
        
        // Simple WAV header
        struct WavHeader {
            char riff[4] = {'R', 'I', 'F', 'F'};
            uint32_t chunkSize;
            char wave[4] = {'W', 'A', 'V', 'E'};
            char fmt[4] = {'f', 'm', 't', ' '};
            uint32_t fmtChunkSize = 16;
            uint16_t audioFormat = 1; // PCM
            uint16_t numChannels = 2; // Always stereo
            uint32_t sampleRate;
            uint32_t byteRate;
            uint16_t blockAlign;
            uint16_t bitsPerSample = 16;
            char data[4] = {'d', 'a', 't', 'a'};
            uint32_t dataSize;
        };
        
        WavHeader header;
        header.sampleRate = sampleRate;
        header.numChannels = 2;
        header.blockAlign = header.numChannels * sizeof(int16_t);
        header.byteRate = sampleRate * header.blockAlign;
        header.dataSize = data.size() * sizeof(int16_t);
        header.chunkSize = 36 + header.dataSize;
        
        std::ofstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Error: Cannot create output file: " << filename << std::endl;
            return false;
        }
        
        file.write(reinterpret_cast<const char*>(&header), sizeof(WavHeader));
        file.write(reinterpret_cast<const char*>(data.data()), header.dataSize);
        file.close();
        
        std::cout << "\nSaved audio to: " << filename << std::endl;
        std::cout << "  Format: 16-bit PCM WAV\n";
        std::cout << "  Sample Rate: " << sampleRate << " Hz\n";
        std::cout << "  Channels: 2 (stereo)\n";
        std::cout << "  Duration: " << (data.size() / 2.0 / sampleRate) << " seconds\n";
        
        return true;
    }
};

void printHelp() {
    std::cout << "CLI Audio Downsampler - v1.0\n";
    std::cout << "Usage: clisampler <input_file> <target_sample_rate> [output_file]\n";
    std::cout << "Examples:\n";
    std::cout << "  clisampler song.mp3 22050\n";
    std::cout << "  clisampler audio.wav 16000 output.wav\n";
    std::cout << "  clisampler music.flac 8000 low_quality.wav\n";
    std::cout << "\nSupported formats: MP3, WAV, FLAC, AAC, OGG, M4A, and more (via FFmpeg)\n";
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printHelp();
        return 1;
    }
    
    std::string inputFile = argv[1];
    uint32_t targetRate = std::stoi(argv[2]);
    std::string outputFile = (argc >= 4) ? argv[3] : "output.wav";
    
    AudioDownsampler downsampler;
    
    std::cout << "Loading audio file: " << inputFile << std::endl;
    
    if (!downsampler.loadAudioFile(inputFile)) {
        std::cerr << "Failed to load audio file. Please ensure:\n";
        std::cerr << "1. File exists and is accessible\n";
        std::cerr << "2. File is a valid audio format\n";
        std::cerr << "3. FFmpeg libraries are installed\n";
        return 1;
    }
    
    auto downsampled = downsampler.downsample(targetRate);
    
    if (downsampled.empty()) {
        std::cerr << "Downsampling failed" << std::endl;
        return 1;
    }
    
    if (!downsampler.saveAsWav(outputFile, downsampled, targetRate)) {
        std::cerr << "Failed to save output file" << std::endl;
        return 1;
    }
    
    std::cout << "\nDone! Output saved to: " << outputFile << std::endl;
    
    return 0;
}
