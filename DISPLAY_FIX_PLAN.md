# Display Fix Plan — AXS15231B QSPI Driver

## Board Info

- **Board:** Guition JC3248W535C_I_Y (Board C, `/dev/ttyACM0`)
- **Display IC:** AXS15231B (QSPI, 4 data lines)
- **Resolution:** 320x480 portrait (native), 480x320 landscape (via rotation)
- **Pins:** CS=45, CLK=47, D0=21, D1=48, D2=40, D3=39, BL=1

## QSPI Protocol (from ArduinoGFX source)

Register writes and pixel data use different QSPI framing:

| Operation | cmd | addr | flags | Data format |
|-----------|-----|------|-------|-------------|
| Register write (C8D8) | `0x02` | `LCD_CMD << 8` | `MULTILINE_CMD \| MULTILINE_ADDR` | Big-endian |
| Register write (C8D16) | `0x02` | `LCD_CMD << 8` | `MULTILINE_CMD \| MULTILINE_ADDR` | Big-endian |
| Register write (C8D16D16) | `0x02` | `LCD_CMD << 8` | `MULTILINE_CMD \| MULTILINE_ADDR` | Big-endian |
| Pixel data (first chunk) | `0x32` | `0x003C00` | `SPI_TRANS_MODE_QIO` | Big-endian (byte-swapped) |
| Pixel data (continuation) | — | — | `MODE_QIO \| VAR_CMD \| VAR_ADDR \| VAR_DUMMY` | Big-endian (byte-swapped) |

- CS: Manual GPIO control (`spics_io_num = -1`)
- Bus: permanently acquired via `spi_device_acquire_bus()`
- SPI config: `command_bits=8, address_bits=24, dummy_bits=0, mode=0, HALFDUPLEX`

## Root Cause: Byte-Order Mismatch

The ESP32-S3 is little-endian. The framebuffer stores RGB565 pixels as `[low_byte, high_byte]`. The AXS15231B expects pixels in big-endian order `[high_byte, low_byte]` over QSPI.

ArduinoGFX handles this by byte-swapping each pixel in `writePixels()` and `writeRepeat()` using the `MSB_16_SET(var, val)` macro: `var = (val >> 8) | (val << 8)`.

Our driver was sending raw little-endian pixels, causing the display to interpret the byte-swapped values as colors. Example:

| Intended color | RGB565 hex | Display sees (no swap) | Display sees (with swap) |
|---------------|-----------|----------------------|------------------------|
| Pink 0xF79F | `[9F, F7]` | R=19, G=63, B=23 (green) | R=30, G=60, B=31 (pink/white) |
| Red 0xF800 | `[00, F8]` | R=0, G=0, B=0 (black!) | R=31, G=0, B=0 (red) |
| Cyan 0x07FF | `[FF, 07]` | R=31, G=63, B=7 (yellow) | R=0, G=63, B=31 (cyan) |

## Root Cause: PSRAM Cache Coherency

ArduinoGFX allocates its pixel transfer buffer in **internal DMA SRAM**:
```cpp
_buffer = (uint8_t *)heap_caps_aligned_alloc(16, ESP32QSPI_MAX_PIXELS_AT_ONCE * 2, MALLOC_CAP_DMA);
```

Our framebuffer lives in PSRAM (8MB). When we modified the PSRAM framebuffer in-place (byte-swap), the CPU cache held the modified values but the SPI DMA controller read stale data from physical PSRAM. Result: black screen.

A separate allocation (even in PSRAM) works because it gets clean, freshly-written cache lines.

## Reference Implementations Studied

| Repo | Chip | Bus | Notes |
|------|------|-----|-------|
| [me-processware/JC3248W535-Driver](https://github.com/me-processware/JC3248W535-Driver) | AXS15231B | Arduino_ESP32QSPI | Arduino_Canvas wrapper, same pins |
| [F1ATB/JC3248W535-Demo](https://github.com/F1ATB/JC3248W535-Demo) | AXS15231B | Arduino_ESP32QSPI | Minimal demo, rotation=1 landscape |
| [AudunKodehode/JC3248W535EN-Touch-LCD](https://github.com/AudunKodehode/JC3248W535EN-Touch-LCD) | AXS15231B | Arduino_ESP32QSPI | Full library, QR codes, JPEG, coordinate transforms |
| [ArduinoGFX Arduino_ESP32QSPI.cpp](https://github.com/moononournation/Arduino_GFX) | — | — | Reference QSPI protocol implementation |

All use identical pin assignments and bus configuration.

## Checklist

### Done
- [x] Created worktree on branch `feature/display-fix`
- [x] Tracked untracked display files into branch
- [x] Added Board C support to Makefile (`flash-c`, `lock-c`, etc.)
- [x] Diagnosed root cause: QSPI protocol, not standard SPI
- [x] Fetched and analyzed ArduinoGFX QSPI source code
- [x] Discovered correct QSPI framing: `cmd=0x02` for regs, `cmd=0x32/addr=0x003C00` for pixels
- [x] Rewrote driver with correct QSPI protocol
- [x] Build succeeds, flash succeeds
- [x] Display shows recognizable text ("TollGate", "starting") — protocol confirmed working
- [x] Identified byte-swap requirement (green text = wrong byte order)
- [x] Identified PSRAM cache coherency issue (in-place swap = black screen)
- [x] Studied 3 reference implementations + ArduinoGFX source
- [x] Text positions adjusted for 320x480 portrait centering

### In Progress
- [ ] Implement byte-swap using internal DMA buffer (like ArduinoGFX)

### TODO
- [ ] Restore render-on-change logic (proven correct, black screen was from swap not logic)
- [ ] Use saturated colors: cyan `0x07FF`, yellow `0xFFE0`, white `0xFFFF`
- [ ] Build, flash, verify correct colors and stable text
- [ ] Verify QR code rendering in READY state
- [ ] Verify payment/error screen states
- [ ] Remove debug log from flush
- [ ] Run `make test-unit` to check for regressions
- [ ] Commit working display driver
- [ ] Push to remote

## Implementation Plan

### 1. Internal DMA swap buffer in `axs15231b.c`

At init, allocate a static buffer:
```c
#define FLUSH_CHUNK_PIXELS 2048  // 4096 bytes, fits in internal DMA RAM
static uint8_t *s_swap_buf = NULL;

// In axs15231b_init():
s_swap_buf = heap_caps_aligned_alloc(16, FLUSH_CHUNK_PIXELS * 2, MALLOC_CAP_DMA);
```

### 2. Byte-swap flush loop

```c
void axs15231b_flush(void) {
    // ... CASET, RASET ...

    int total_pixels = s_width * s_height;
    int offset = 0;
    bool first = true;

    cs_low();
    while (offset < total_pixels) {
        int chunk = min(FLUSH_CHUNK_PIXELS, total_pixels - offset);

        // Byte-swap from PSRAM framebuffer into DMA buffer
        uint8_t *src = (uint8_t *)(s_fb + offset);
        for (int i = 0; i < chunk * 2; i += 2) {
            s_swap_buf[i] = src[i + 1];
            s_swap_buf[i + 1] = src[i];
        }

        // Send via QSPI
        spi_transaction_ext_t t = {0};
        if (first) {
            t.base.flags = SPI_TRANS_MODE_QIO;
            t.base.cmd = 0x32;
            t.base.addr = 0x003C00;
            first = false;
        } else {
            t.base.flags = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD |
                           SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
        }
        t.base.tx_buffer = s_swap_buf;
        t.base.length = chunk * 16;
        spi_device_polling_transmit(s_spi, (spi_transaction_t *)&t);

        offset += chunk;
    }
    cs_high();
}
```

### 3. Render-on-change in `display.c`

Only re-render when:
- `s_force_render` is set (state change, init)
- QR mode cycles (every 5s in READY state)

This eliminates the 1Hz full-screen redraw that caused text to "move around."

### 4. Color choices

| Element | Old color | New color | Reason |
|---------|-----------|-----------|--------|
| Boot title | `0xF79F` (near-white) | `0x07FF` (cyan) | High contrast on black |
| Boot subtitle | `0xB5B6` (gray) | `0xFFE0` (yellow) | Visible, warm accent |
| Ready label | `0xB5B6` | `0x07FF` | Consistent accent |
| Payment bg | `0x07E0` (green) | `0x07E0` | Keep — bright green is clear |
| Error bg | `0xF800` (red) | `0xF800` | Keep — bright red is clear |
