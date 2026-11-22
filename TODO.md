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
