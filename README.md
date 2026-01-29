Viewer for wireless screen casting from New 3DS/New 2DS to PC (Windows/Linux/macOS)

See https://wiki.hacks.guide/wiki/3DS:Wireless_streaming for more info on setup and instructions.

Intended for use with [NTR-HR](https://github.com/xzn/ntr-hr).

## Guide for settings

Default should work fine for most scenarios. You can customize them as needed.

| Setting | Instruction |
| --- | --- |
| View Mode | Choose the screens to display, and whether to display them in separate windows. |
| Upscaling Filter | Apply post-processing pixel filter. |
| 3DS IP | IP address of the 3DS console you intend to stream from. Can try Auto-Detect or select from drop down menu. |
| Viewer IP | IP address of the network interface the viewer should listen from. Auto-Select should work most of the time. |
| Viewer Port | For use with multiple 3DS devices streaming at once to the same PC. Each should have a different port. Otherwise leave at default. |
| Prioritize Top Screen | See Priority Screen Factor |
| Priority Screen Factor | Prioritized screen will have frame rate multiple of the other screen at specified factor. If factor is **0**, only the prioritized screen is streamed. |
| Compression Format and Protocol | **JPEG Compat** for the original protocol, for compatibility with other viewers. **JPEG (RS)** for an alternative protocol that attempts to reduce dropped frames. **JPEG (RS, Delta)** for alternative encoding that in most cases improves picture quality for the same bandwidth, at the cost of lower maximum frame rate. **Uncompressed** and **Lossless** for lossless encoding.<sup>1</sup> *Only effective with latest NTR-HR.* |
| JPEG Quality | For JPEG mode: should be **95** or lower for decent frame rate. Below **55** the picture quality notably suffers. *In case of JPEG Delta, this specifies the maximum quality NTR will auto-adjust to.* |
| Color Quality Bias | For lossless mode: negative value reduces image quality to improve performance. |
| Bandwidth Limit | Caps streaming bandwidth to specified number. Lower value can help with congestion and lag but will result in lower frame rate. |
| **Default** | Reset viewer settings. |
| **Connect** | Attempts to start remote play and/or apply any changed settings. |
| Input Redirection | Choose a connected gamepad for input redirection. |
| Swap A/B X/Y | Enabled by default. Buttons glyphs will be mismatched but positions will be correct. |
| Bottom Screen Cursor | Size of mouse cursor for bottom screen input redirection. |
| Ambience Quality | Quality of background blur |
| Ambience Blur | Strength of background blur |
| Press **F** to toggle fullscreen | Toggle viewer's fullscreen display. Works with multiple monitors as well. |

<sup>1</sup> See below for expected performance for each setting.

## Interface usage

- Right-click anywhere in the top screen window to hide/unhide settings window.
- Left-click/drag on bottom screen display for input redirection of bottom touch screen.
- Keyboard (tab) navigation is supported for Remote Play settings window. **Esc** to hide/unhide window.

## Expected frame rate

Tested on home menu with default viewer settings, except for Priority Factor, which is set to 1 instead of the default.

Assumes optimal Wi-Fi network condition.

| Protocol and Format | Expected max frame rate (top + bottom screen) |
| --- | --- |
| JPEG Compat | 120 |
| JPEG (RS) | 120 |
| JPEG (RS, Delta) | 90 ~ 110 |
| Uncompressed | 15 |
| Lossless (RS) | 30 |
| Lossless (RS, Delta) | 30 ~ 60 |
| JPEG Compat (Old 3DS) | 3 |
| Uncompressed (Old 3DS) | 3 |

Note: For true lossless image, you need to set Color Quality Bias to 0, and set Chroma Subsampling to Off in the NTR-HR menu (in Remote Play -> Advanced Options). Frame rate will be very low.

## FAQ

### How do I get audio with the stream?

- Audio capture is **NOT** supported. A workaround is use a 3.5 mm audio aux cable to connect from your 3DS's audio jack to your PC's line-in for capture.

### Are only **New 3DS/New 2DS** supported?

- Old (original) 3DS are **NOT** supported. Neither are 2DS.

### Anyway to stream DS games, DSiWare, and GBA games?

- This will **NOT** be supported, as 3DS' CPU run in a compatibility mode that loses all the normal 3DS computing power.

### How to fix Pokemon games hanging when loading a save when Remote Play is enabled?

- Enabling Input Redirection or Debugger in Luma3DS/Rosalina on your 3DS is the recommended method.

- Alternatively use NFC patch in NTR-HR menu if you are using that. The caveat is that this will disable Reliable Stream.

## Additional info

### Input redirection

- Can be enabled in Luma3DS/Rosalina's menu. See https://wiki.hacks.guide/wiki/3DS:Input_redirection

### Reliable Stream

- The title bar text changes depending on the current streaming method.
- **JPEG Compat** will be displayed if Reliable Stream is not enabled or not supported.
- **JPEG RS** will be displayed if Reliable Stream is On.
- **JPEG RS, Delta** will be displayed if On + Delta is enabled and supported.

### macOS quarantine bypass

- Run `xattr -r -d com.apple.quarantine NTRViewer-HR-macOS-Universal.zip` on the file downloaded.

### Asahi Linux port publish

- This applies to other arm64 based Linux distro as well. When using muvm/fex to run the application, you will need to publish the viewer's port, e.g.: `muvm -p 8001/udp FEXInterpreter $(realpath ./ntrviewer)`

### Additional filters options

- Selected shaders were downloaded from https://github.com/libretro/slang-shaders, where you can find more.
- Manually edit the `rashader.json` file to make options appear within the application. Similarly for placebo shaders with `placebo.json`.

## Feedback

If you have any further questions, feel free to post in discussion at https://github.com/xzn/ntrviewer-hr/discussions

For bugs and other issues you can open an issue at https://github.com/xzn/ntrviewer-hr/issues

## License and credits

This project is licensed under MIT license. See LICENSE file for more info.

Third party code are licensed under various licenses. See individual files and license files in the subfolders for detail.
