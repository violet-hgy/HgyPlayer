# FFmpeg 学习文档（从打开视频开始）

> 面向本工程 **HgyPlayer**：用库方式调用 FFmpeg（`libavformat` / `libavcodec` / `libswscale` / `libswresample`），对照 `media/FFmpegPlayer.cpp` 阅读效果最好。  
> 目标：搞清「打开一个视频文件之后，数据是怎么一步步变成画面和声音的」。

---

## 1. 先建立整体图景

一个媒体文件（如 `demo.mp4`）通常可以拆成三层概念：

| 概念 | 通俗理解 | FFmpeg 里常见对象 |
|------|----------|-------------------|
| **容器（Container / Format）** | 盒子，装多条流和元数据 | `AVFormatContext`、`AVInputFormat` |
| **流（Stream）** | 盒子里的一轨：视频轨、音频轨、字幕轨 | `AVStream` |
| **编码（Codec）** | 这一轨怎么压缩的：H.264、AAC… | `AVCodec`、`AVCodecContext`、`AVCodecParameters` |

播放链路可以记成一句话：

```text
打开文件 → 解析容器/流 → 打开解码器 → 循环读包(demux)
         → 解码成帧 →（视频转 RGB / 音频重采样）→ 按时钟显示/播放
```

对应本工程：

```text
MainWindow（UI）
    └── FFmpegPlayer（门面，主线程）
            └── PlayerWorker（解码线程）
                    openFile / runLoop / decodeVideo / decodeAudio / updateClock / seek
```

---

## 2. 打开视频：从文件到「可读的容器」

### 2.1 核心 API

```c
AVFormatContext *fmt = nullptr;
int ret = avformat_open_input(&fmt, path, nullptr, nullptr);
if (ret < 0) { /* 失败：路径、权限、格式不支持等 */ }

ret = avformat_find_stream_info(fmt, nullptr);
if (ret < 0) { /* 失败：探针流信息失败 */ }
```

| 函数 | 作用 |
|------|------|
| `avformat_open_input` | 打开输入：识别封装格式，建立 `AVFormatContext` |
| `avformat_find_stream_info` | **探测**各路流：时长、码率、编解码参数等（可能读一部分数据） |

本工程对应：`PlayerWorker::openFile()` 里的「打开容器」阶段。

### 2.2 打开之后你得到了什么？

`AVFormatContext` 上常用字段：

- `iformat`：输入格式（如 `mov,mp4,m4a,3gp,3g2,mj2`）
- `duration`：容器时长（单位 `AV_TIME_BASE`，常为 \(1/1000000\) 秒）
- `bit_rate`：估算总码率
- `nb_streams` / `streams[]`：所有流

时长换算到毫秒（本工程写法）：

```c
durationMs = fmt->duration / (AV_TIME_BASE / 1000);
```

关闭时：

```c
avformat_close_input(&fmt);  // 内部会释放 fmt
```

---

## 3. 解析流：找到视频轨和音频轨

### 3.1 选「最好」的流

```c
int videoStream = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
int audioStream = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
```

- 返回值是 `streams[]` 的下标（`< 0` 表示没有该类型流）
- 纯音乐文件可以只有音频；无声视频可以只有视频

### 3.2 每条流上要看的参数

`AVStream *st = fmt->streams[i];`  
`AVCodecParameters *par = st->codecpar;`

| 字段 | 含义 |
|------|------|
| `par->codec_type` | 视频 / 音频 / 字幕… |
| `par->codec_id` | 编码 ID（如 `AV_CODEC_ID_H264`） |
| `st->time_base` | **该流的时间基**（PTS 单位） |
| 视频：`width/height`、`format`（像素格式） | |
| 音频：`sample_rate`、`ch_layout`、`format`（采样格式） | |

本工程把这些信息填进与 FFmpeg 解耦的 `MediaInfo` / `MediaStreamInfo`（见 `buildInfo()` / `makeStreamInfo()`），UI 只读这些结构，不直接碰 FFmpeg 头文件。

---

## 4. 打开解码器：为流准备「解压机」

### 4.1 步骤

```c
const AVCodec *dec = avcodec_find_decoder(par->codec_id);
AVCodecContext *ctx = avcodec_alloc_context3(dec);
avcodec_parameters_to_context(ctx, par);   // 把流参数拷到解码上下文
ctx->thread_count = 2;                    // 可选：多线程解码
avcodec_open2(ctx, dec, nullptr);
```

本工程：`PlayerWorker::openCodec()`。

### 4.2 为什么参数在 `codecpar`，解码却用 `AVCodecContext`？

- **`codecpar`**：容器探测得到的「这轨是什么」
- **`AVCodecContext`**：真正解码时的运行时状态（缓冲区、线程、打开的解码器实例）

用完后：

```c
avcodec_free_context(&ctx);
```

---

## 5. 时间：PTS、时间基、毫秒

播放器几乎所有「同步」问题都和时间有关。

### 5.1 时间基（time_base）

流上的 `PTS/DTS` 是整数，真实时间要乘时间基：

\[
t_{\text{秒}} = \text{pts} \times \frac{\text{time\_base.num}}{\text{time\_base.den}}
\]

本工程统一转成毫秒：

```c
qint64 tsToMs(qint64 ts, AVRational tb)
{
    if (ts == AV_NOPTS_VALUE) return -1;
    return av_rescale_q(ts, tb, AVRational{1, 1000});
}
```

### 5.2 PTS 与 DTS（入门级理解）

| 名称 | 含义 |
|------|------|
| **DTS** | Decode Time Stamp，解码顺序时间 |
| **PTS** | Presentation Time Stamp，**显示/播放**时间 |

B 帧存在时，解码顺序 ≠ 显示顺序，所以两者可能不同。显示画面一般以 **PTS**（或 `best_effort_timestamp`）为准。

### 5.3 `AV_NOPTS_VALUE`

表示「这一帧/包没有有效时间戳」。播放器要能容忍，并尽量用邻近帧或墙钟补救。

---

## 6. 解复用（Demux）：循环读「包」

打开并建好解码器后，进入播放主循环（本工程 `runLoop()`）：

```c
AVPacket *pkt = av_packet_alloc();
while (...) {
    int ret = av_read_frame(fmt, pkt);
    if (ret == AVERROR_EOF) { /* 文件读完 */ break; }
    if (ret < 0) { /* 读错误 */ break; }

    if (pkt->stream_index == videoStream)
        decodeVideo(pkt);
    else if (pkt->stream_index == audioStream)
        decodeAudio(pkt);

    av_packet_unref(pkt);  // 释放本次包引用的数据
}
```

| 概念 | 说明 |
|------|------|
| **AVPacket** | 压缩数据的一包（可能是一帧，也可能是几帧的一部分） |
| **av_read_frame** | 从容器里按交错顺序取出下一包 |
| **stream_index** | 这包属于哪条流 |

EOF 之后，视频解码器里可能还有残帧，需要 `avcodec_send_packet(ctx, nullptr)` 做 **flush**，再 `receive_frame` 把尾帧拿完（本工程 EOF 分支就是在做这件事）。

---

## 7. 解码视频：Packet → Frame → 画面

### 7.1 发送 / 接收模型（现代 API）

```c
avcodec_send_packet(vdec, pkt);      // 把压缩包送进解码器
for (;;) {
    int ret = avcodec_receive_frame(vdec, frame);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
    if (ret < 0) break;
    // 得到一帧 AVFrame：像素在 frame->data[]，尺寸 width/height
}
```

- `EAGAIN`：还要继续送包，或暂时没有输出帧——都是正常现象
- 一包可能解出 0/1/多帧；也可能多包才出一帧

本工程：`decodeVideo()`。

### 7.2 像素格式与「转成能显示的图」

解码出来常常是 **YUV420P** 等格式，Qt `QLabel` / `QImage` 需要 **RGB**。  
用 **libswscale**：

```c
SwsContext *sws = sws_getCachedContext(..., srcFmt, ..., AV_PIX_FMT_RGB32, SWS_BILINEAR, ...);
sws_scale(sws, frame->data, frame->linesize, 0, height, dst, dstStride);
```

本工程：`convertVideoFrame()` → 得到 `QImage`，再 `emit frameReady(...)` 给 UI。

记忆口令：

> **解码**解决「压缩 → 原始像素」；**swscale** 解决「像素格式/尺寸转换」。

---

## 8. 解码音频：Packet → Frame → PCM

### 8.1 同样是 send / receive

```c
avcodec_send_packet(adec, pkt);
avcodec_receive_frame(adec, frame);
// frame 里是 planar/packed 的采样数据，格式可能是 fltp 等
```

### 8.2 重采样（libswresample）

声卡/Qt `QAudioSink` 通常要固定格式，例如：

- 采样格式：`S16`（有符号 16-bit）
- 声道：立体声
- 采样率：设备采样率（如 48000）

```c
swr_alloc_set_opts2(...);
swr_init(swr);
swr_convert(swr, &out, maxOut, (const uint8_t**)frame->extended_data, frame->nb_samples);
```

本工程注意点（踩坑经验）：

- **不要**只在 `open` 时用 `codecCtx->sample_fmt` 初始化 swr——打开时经常是 `NONE`
- 应在 **第一帧真实 `AVFrame` 到来后** 再 `ensureSwrFromFrame()`（懒加载）

解码线程产出 PCM 后，本工程把 PCM 丢到主线程队列，由 `QAudioSink` 播放（因为 `QAudioSink` 依赖主线程事件循环）。

---

## 9. 时钟同步：画面什么时候该显示？

如果「解出来就立刻画」，视频会越播越快/乱跳。需要 **媒体时钟**。

本工程简化模型：

1. 用墙钟 `QElapsedTimer` + `clockBaseMs` 模拟「当前应播放到的媒体时间」
2. 视频帧带着 `ptsMs`，在 `waitForPts(ptsMs)` 里等到时钟追上再显示
3. 显示后用 `updateClock(ptsMs)` 校正媒体时钟；漂移过大则重新锚定墙钟

```text
墙钟时间 ≈ clockBaseMs + wallTimer.elapsed()
若 ptsMs > 当前墙钟 → sleep 一小段再显示
若媒体 PTS 与墙钟差太多 → 重锚 clockBaseMs
```

纯音频文件没有视频帧驱动时钟时，会在音频路径里用音频 PTS 更新时钟。

---

## 10. Seek：跳转到任意时间

### 10.1 核心 API

```c
// 目标毫秒 → AV_TIME_BASE 时间戳
int64_t ts = av_rescale_q(targetMs, {1,1000}, {1, AV_TIME_BASE});
avformat_seek_file(fmt, -1, INT64_MIN, ts, INT64_MAX, 0);
// 或回退：
av_seek_frame(fmt, -1, ts, AVSEEK_FLAG_BACKWARD);

avcodec_flush_buffers(vdec);
avcodec_flush_buffers(adec);
```

### 10.2 Seek 后为什么还要「丢弃旧帧」？

Seek 通常落到 **关键键帧**，再从关键键帧往前解码到目标点。  
这段过渡里会出现 **PTS < 目标时间** 的帧。若用它们去更新时钟，进度会跳回、甚至卡住。

本工程做法：

- `m_discardUntilPtsMs = targetMs`：丢掉目标点之前的帧
- `m_seekSerial++`：等待中的 `waitForPts` 发现 serial 变了，丢弃当前帧
- 门面层 `seekingToMs`：过滤 seek 完成前过期的 `positionChanged`，避免进度条回跳

### 10.3 播放中 Seek 的线程注意

Worker 若阻塞在 `runLoop` / `waitForPts`，主线程 `Qt::QueuedConnection` 投递的槽可能迟迟执行不到。  
因此本工程在 **正在跑** 时用 `requestSeek()` 置标志，由循环内 `consumeCommands()` 执行真正的 `performSeek_l()`。

---

## 11. 资源生命周期清单（建议背下来）

| 对象 | 分配 | 释放 |
|------|------|------|
| `AVFormatContext` | `avformat_open_input` | `avformat_close_input` |
| `AVCodecContext` | `avcodec_alloc_context3` + `open2` | `avcodec_free_context` |
| `AVPacket` | `av_packet_alloc` | `av_packet_free`；每次用后 `av_packet_unref` |
| `AVFrame` | `av_frame_alloc` | `av_frame_free`；每次用后 `av_frame_unref` |
| `SwsContext` | `sws_getContext` / `getCachedContext` | `sws_freeContext` |
| `SwrContext` | `swr_alloc_set_opts2` | `swr_free` |

原则：**谁创建谁释放；循环里用 unref，退出时用 free。**

---

## 12. 和本工程代码的对照表

| 学习步骤 | 本工程函数 / 位置 |
|----------|-------------------|
| 打开容器、探测流 | `PlayerWorker::openFile` |
| 打开解码器 | `PlayerWorker::openCodec` |
| 填充媒体信息 | `PlayerWorker::buildInfo` |
| 解复用循环 | `PlayerWorker::runLoop` |
| 解码并输出画面 | `decodeVideo` + `convertVideoFrame` |
| 解码并输出 PCM | `decodeAudio` + `ensureSwrFromFrame` |
| 媒体时钟 | `updateClock` / `waitForPts` |
| Seek | `requestSeek` / `performSeek_l` / `FFmpegPlayer::seek` |
| UI 只订阅信号 | `MainWindow` ← `frameReady` / `positionChanged` |

库链接与运行时 DLL（Windows）见根目录 `CMakeLists.txt`：链接 `avformat/avcodec/avutil/swscale/swresample`，运行目录只拷贝对应最小 DLL 集合。

---

## 13. 建议动手练习顺序

1. **只打开 + 打印信息**  
   `open_input` → `find_stream_info` → 打印每路 `codec_id`、宽高、采样率、时长。  
   （可参考 `FFmpegVideoParser`。）

2. **只抽一帧缩略图**  
   找到视频流 → 打开解码器 → `read_frame` 直到解出一帧 → swscale → 存成图片。

3. **只播视频无声**  
   循环 demux + decodeVideo + 按 PTS sleep 显示。

4. **加上音频**  
   decodeAudio + swr + 音频设备；处理主线程/解码线程分工。

5. **加上 Seek**  
   `seek_file` + flush + 丢弃旧 PTS + UI 进度不回跳。

按这个顺序，你就走完了本播放器的核心路径。

---

## 14. 常用库一览

| 库 | 职责 |
|----|------|
| **libavformat** | 打开文件、解复用、Seek、写封装 |
| **libavcodec** | 编解码 |
| **libavutil** | 时间基、内存、错误码、通道布局等工具 |
| **libswscale** | 图像缩放与像素格式转换 |
| **libswresample** | 音频重采样 / 声道 / 采样格式转换 |

命令行工具（本工程运行不依赖，但学习很有用）：

- `ffprobe file.mp4`：看容器与流信息  
- `ffplay file.mp4`：参考播放行为  
- `ffmpeg -i in.mp4 ...`：转码/抽帧

---

## 15. 延伸阅读

- 官方文档（本仓库已 vendored）：`ffmpeg-master-latest-win64-lgpl-shared/doc/`
- 本工程踩坑记录：`doc/问题排查与解决方案.md`
- 播放器实现：`media/FFmpegPlayer.h`、`media/FFmpegPlayer.cpp`

---

*文档与 HgyPlayer 当前实现同步：从「打开视频」到「解码、同步、Seek」的一条完整学习路径。*
