# TODO

## Speed and stability test to the PC

A probe from the Switch to the paired host, run at the highest rate the link will carry.

Use it before a stream, and again when a stream falls apart, to see whether the path itself can hold the bitrate. Report throughput, loss, and jitter to `192.168.50.115` (or whichever host is selected). The result should say if the Wi-Fi hop is the limit, separate from decode and the GPU.

## On-the-fly logger for Bad connection

`StreamingView` draws a fixed "Bad connection..." while `MoonlightSession::connection_status_is_poor()` is set from `CONN_STATUS_POOR`. That line does not say what failed.

Replace it with a live reason, and keep the message on screen for 10 seconds after the fault, even if the flag clears sooner:

- Network chip: loss, jitter, or throughput below the requested bitrate
- CPU: decode or the stream thread pegged
- GPU: present or RCAS missed the 60 Hz flip
- Which of those hit 100%, and which one did not keep up

The 10 second hold is so a one-frame spike can still be read.

## Better logging

`write_log` exists and defaults to off. The messages behind it are scattered `Logger` lines. A later reader, including an AI, cannot reconstruct a fault from them.

When something goes wrong, write one session log that states the facts in order:

- When it happened, and the host, resolution, fps, codec, bitrate, and frame-buffer setting in effect
- Network: throughput, loss, jitter, and whether the link was below the requested bitrate
- CPU and GPU: what was at 100%, and what missed the 60 Hz flip
- Switch clocks at that moment, in MHz: CPU, GPU, RAM, and every other clock the console reports. Handheld and docked clocks differ, so write the measured number, next to the load, not a profile name
- Decoder: hardware or software, queue depth, dropped frames, and the Bad connection reason above
- The seconds before the fault, not only the single error line

Plain sentences, with the numbers next to them. No need to guess which counter a bare integer was.
