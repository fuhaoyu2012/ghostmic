#include <stdio.h>
#include <stdlib.h>
#include <stdint.h> 
#include <portaudio.h>
#include <string.h>

#define SAMPLE_RATE     44100   // CD音质
#define FRAMES_PER_BUFFER 512   // 处理512个样本
#define NUM_SECONDS     5       // 录制5s
#define NUM_CHANNELS    1       // one 麦克风

#pragma pack(push, 1) // 取消struct填充
typedef struct {
        char riff_id[4];        // "RIFF"
        uint32_t riff_size;     // 文件总大小 - 8
        char wave_id[4];        // "WAVE"

        char fmt_id[4];         // "fmt "
        uint32_t fmt_size;      // 16（PCM格式）
        uint16_t audio_format;  // 1 = PCM（无压缩）
        uint16_t num_channels;  // 声道数
        uint32_t sample_rate;   // 采样率 uint32_t
        uint32_t byte_rate;     // 每秒字节数 uint32_t
        uint16_t block_align;   // 每个样本的字节
        uint16_t bits_per_sample; // 16位

        char data_id[4];        // "data"
        uint32_t data_size;     // 音频数据大小
} WavHeader;
#pragma pack(pop) // end

typedef struct {
        FILE *file;             // file pointer
        WavHeader header;       // WAV头
        uint32_t total_samples; // 已录制的样本数
        int is_recording;       // 判断是否在录制 is_resording 
} RecordData;                   // RecordData（类型名大写）

// to called by the portaudio engine whenever it has captured audio data or more audio data for output
static int recordCallback(const void *inputBuffer,         
                          void *outputBuffer,               
                          unsigned long framesPerBuffer,    
                          const PaStreamCallbackTimeInfo *timeInfo,
                          PaStreamCallbackFlags statusFlags,
                          void *userData)
{
        // we not only need this args
        (void)outputBuffer;
        (void)timeInfo;
        (void)statusFlags;

        RecordData *data = (RecordData *)userData;          // recordData → RecordData
        const int16_t *samples = (const int16_t *)inputBuffer; // inputBuffer

        if (inputBuffer == NULL) { // 没有写入，麦克风数据为空  inputBuffer
                int16_t silence[framesPerBuffer];           // farmeCount → framesPerBuffer
                memset(silence, 0, sizeof(silence));        // 写入静音
                fwrite(silence, sizeof(int16_t), framesPerBuffer, data->file); // farmeCount → framesPerBuffer
                data->total_samples += framesPerBuffer;     // farmeCount → framesPerBuffer
                return paContinue;
        }

        fwrite(samples, sizeof(int16_t), framesPerBuffer * NUM_CHANNELS, data->file); // fwritea → fwrite，farmeCount → framesPerBuffer，data->fiel → data->file
        data->total_samples += framesPerBuffer * NUM_CHANNELS; // data->file += ... → data->total_samples += ...

        float seconds_recorded = (float)data->total_samples / SAMPLE_RATE / NUM_CHANNELS;
        if (seconds_recorded >= NUM_SECONDS) {             
                data->is_recording = 0;
                return paComplete;  // 告诉 PortAudio：我录完了，可以停了
        }

        return paContinue;  // 继续录制下一帧
}

void writeWavHeader(FILE *file, uint32_t total_samples) {
        WavHeader header = {0};

        // RIFF 块
        memcpy(header.riff_id, "RIFF", 4);
        header.riff_size = 36 + total_samples * sizeof(int16_t);  // 总文件大小 - 8
        memcpy(header.wave_id, "WAVE", 4);

        // fmt 块
        memcpy(header.fmt_id, "fmt ", 4);
        header.fmt_size = 16;
        header.audio_format = 1;        // PCM
        header.num_channels = NUM_CHANNELS;
        header.sample_rate = SAMPLE_RATE;
        header.byte_rate = SAMPLE_RATE * NUM_CHANNELS * sizeof(int16_t);
        header.block_align = NUM_CHANNELS * sizeof(int16_t);  
        header.bits_per_sample = 16;

        // data 块
        memcpy(header.data_id, "data", 4);
        header.data_size = total_samples * sizeof(int16_t);

        // 回到文件开头写入头
        fseek(file, 0, SEEK_SET);
        fwrite(&header, sizeof(WavHeader), 1, file);
}

int main(__attribute__((unused)) int argc, __attribute__((unused)) char *argv[])
{
        PaStream *stream;       // Init stream
        PaError err;            // Init error
        RecordData data = {0};  // Init zero
		
		fprintf(stderr, "[DEBUG] 查看是否出缓冲区\n");

        printf("=== 麦克风录音程序 ===\n");
        printf("采样率: %d Hz\n", SAMPLE_RATE);
        printf("声道: %s\n", NUM_CHANNELS == 1 ? "单声道" : "立体声");
        printf("时长: %d 秒\n", NUM_SECONDS);
        printf("\n");

        err = Pa_Initialize();
        if (err != paNoError) {
                printf("PortAudio Error: %s\n", Pa_GetErrorText(err));
                return 1;   
        }

        printf("PortAudio Init!\n");

        //  列出所有音频设备（帮你找到麦克风）
        int numDevices = Pa_GetDeviceCount();
        printf("\n可用的音频设备:\n");
        for (int i = 0; i < numDevices; i++) {
                const PaDeviceInfo *deviceInfo = Pa_GetDeviceInfo(i);
                printf("  设备 %d: %s\n", i, deviceInfo->name);
                printf("    输入通道: %d, 输出通道: %d\n",
                       deviceInfo->maxInputChannels,
                       deviceInfo->maxOutputChannels);
        }

        // 打开录音文件
        data.file = fopen("recording.wav", "wb");
        if (!data.file) {
                fprintf(stderr, "无法创建文件 recording.wav\n");
                Pa_Terminate();
                return 1;
        }

        // 先预留44字节给 WAV 头，后面再回来填
        fseek(data.file, sizeof(WavHeader), SEEK_SET);
        data.is_recording = 1;  // ← 修正：is_recording 拼写

        // 4. 打开音频流（开始监听麦克风）
        // 参数解释：
        //   &stream          - 返回的流句柄（像文件描述符）
        //   paNoDevice       - 使用默认输入设备
        //   NUM_CHANNELS     - 单声道
        //   paInt16          - 16位整数格式
        //   SAMPLE_RATE      - 44100Hz
        //   FRAMES_PER_BUFFER- 每次回调512个样本
        //   recordCallback   - 数据来了调用这个函数
        //   &data            - 传给回调的用户数据
        err = Pa_OpenDefaultStream(&stream,
                                   NUM_CHANNELS,      // 输入通道数
                                   0,                 // 输出通道数（0=不播放）
                                   paInt16,           // 样本格式：16位整数
                                   SAMPLE_RATE,
                                   FRAMES_PER_BUFFER,
                                   recordCallback,   
                                   &data);
        if (err != paNoError) {
                fprintf(stderr, "打开音频流失败: %s\n", Pa_GetErrorText(err));
                fclose(data.file);
                Pa_Terminate();
                return 1;
        }
        printf("[OK] 音频流打开成功\n");

        // 开始录制
        printf("\n>>> 开始录制，请对着麦克风说话... <<<\n");
        err = Pa_StartStream(stream);
        if (err != paNoError) {
                fprintf(stderr, "开始录制失败: %s\n", Pa_GetErrorText(err));
                Pa_CloseStream(stream);
                fclose(data.file);
                Pa_Terminate();
                return 1;
        }

        // wait录制完成（阻塞直到回调返回 paComplete）
        while (Pa_IsStreamActive(stream)) {
                Pa_Sleep(100);  // 每100ms检查一次
        }

        printf(">>> 录制完成! <<<\n");

        // 清理：关闭流、写文件头、关闭文件
        Pa_StopStream(stream);
        Pa_CloseStream(stream);

        // 回到文件开头，写入正确的 WAV 
        writeWavHeader(data.file, data.total_samples);
        fclose(data.file);

        printf("\n文件已保存: recording.wav\n");
        printf("总样本数: %u\n", data.total_samples);
        printf("文件大小: %.2f KB\n", (36 + data.total_samples * 2) / 1024.0); 

        Pa_Terminate();
        return 0;
}
