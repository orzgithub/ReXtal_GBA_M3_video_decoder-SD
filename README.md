# ReXtal – SD Card Edition

A fork of the original ReXtal project that adds SD card playback support for GBA flash carts made by ZaindORp. This version plays `.gbm` and `.gbs` video files (converted with [Ausar's video encoder](https://gbam3.ausar.xyz/) or the offcial M3 movie conventer) directly from SD media.

> **Original project**: https://github.com/ArcheyChen/ReXtal_GBA_M3_video_decoder  
> Reprinting or modification must credit the original source. Unauthorized commercial use is prohibited.

---

## Supported Hardware

This SD-enabled version has been tested and confirmed working on:

- **EZ Flash Omega / ODE** (EZODEB not tested – support unknown)
- **SuperCard MiniSD**
- **SuperCard Lite**
- **SuperChis**

Additionally, a **DLDI‑compatible build** is provided, which should work with many other flash carts that support DLDI patching. However, **not all DLDI implementations are compatible** – your mileage may vary.

---

## Two Playback Version

To accommodate different SD card speeds and buffering capabilities, this player offers two operating modes:

### 1. Real‑time Streaming Version
- **Fast loading** – starts playback almost instantly.
- **Drawback**: Relies on continuous SD card I/O. For high‑bitrate videos or complex scenes, the SD card's read speed may become a bottleneck, causing stuttering or frame drops.

### 2. Buffered Version
- **Smoother playback** – reads ahead into a buffer, reducing mid‑playback I/O delays.
- **Drawback**: Slower initial loading; requires per‑cartridge buffer tuning and is **not** compatible with the DLDI build. When the buffer runs out, there is a noticeable longer load time for the next segment.

Choose the version that best fits your cartridge and preferred trade‑off between startup speed and playback smoothness.

---

## Credits & Acknowledgements

This SD Card Edition is built upon the excellent work of **Ausar** (ArcheyChen).  
Please visit the original repository for the base decoder and core logic:  
👉 [https://github.com/ArcheyChen/ReXtal_GBA_M3_video_decoder](https://github.com/ArcheyChen/ReXtal_GBA_M3_video_decoder)

---

## License & Usage

- You **must** credit the original source ([https://github.com/ArcheyChen/ReXtal_GBA_M3_video_decoder](https://github.com/ArcheyChen/ReXtal_GBA_M3_video_decoder)) and this project when redistributing or modifying this work.
- Commercial use is **prohibited** without explicit permission from the original author.
- All derivative distributions based on this project must provide source code and use the same protocol as the project. Additional terms are allowed, but they cannot conflict with existing agreement content.

---

## Notes

- The DLDI version is provided as‑is – please test with your specific cart.
- For best results, use a high‑quality SD card with fast read speeds, especially in real‑time mode.

Enjoy watching on your GBA!