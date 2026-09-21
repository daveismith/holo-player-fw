# Configuration

Build-time settings live in two places: choices you are meant to change, in `idf.py menuconfig`,
and the settings the firmware needs to work at all, in `sdkconfig.defaults`.

`sdkconfig` itself is **not committed**. Everything the build needs is in `sdkconfig.defaults`, so
a fresh clone builds correctly and a stale `sdkconfig` can always be deleted and regenerated.

## menuconfig

### Board: Waveshare ESP32-S3-Touch-LCD-1.28

| Option | Default | Does |
|---|---|---|
| `BOARD_LCD_PCLK_MHZ` | 80 | LCD SPI clock, 10–80 MHz |
| `BOARD_TOUCH_ENABLE` | on | Compile in the touch driver and the `touch` command |
| `BOARD_TOUCH_AUTOSTART` | off | Start touch reporting at boot rather than on `touch on` |

The clock default is not arbitrary. The GC9A01 has no tearing-effect line on this board, so
nothing synchronises a frame write to the panel's own ~60 Hz refresh. A full 240×240 RGB565 frame
is 921,600 bits: **23 ms at 40 MHz**, which is longer than one refresh, so a new frame is seen
sweeping down the screen — and **11.5 ms at 80 MHz**, which fits inside one. Drop to 40 only if
the picture shows corruption at 80.

### LED strip (NeoPixel)

| Option | Default | Does |
|---|---|---|
| `LEDS_GPIO` | 16 | Data line; 16 is P2 pin 8 |
| `LEDS_COUNT` | 16 | LEDs on the strip |
| `LEDS_SPEED` | 800 kHz | 800 kHz for WS2812/WS2812B/SK6812; 400 kHz for WS2811 |
| `LEDS_ORDER` | GRB | Colour order on the wire; RGB if red and green come out swapped |
| `LEDS_BRIGHTNESS` | 100 | Brightness at boot, 1–100 |

!!! warning "Two of these have consequences off the board"
    400 kHz on a WS2812 makes **every** frame full white, including the frame that turns the strip
    off. And at 100% brightness sixteen LEDs can draw about 1 A, which is more than the P2
    header's VSYS pin should be asked for — keep it at 30 or below unless the strip has its own
    supply. Both are covered in [Wiring the LED ring](../connect/leds.md).

### Holoprojector servos

| Option | Default | Does |
|---|---|---|
| `HOLO_SERVO1_GPIO` | 17 | P2 pin 9 |
| `HOLO_SERVO2_GPIO` | 18 | P2 pin 10 |
| `HOLO_SERVO1_MIN_US` / `_MAX_US` | 1150 / 1900 | Servo 1's working travel |
| `HOLO_SERVO2_MIN_US` / `_MAX_US` | 1300 / 1950 | Servo 2's working travel |
| `HOLO_PAN` | Servo 1 | Which servo pans; the other tilts |
| `HOLO_SERVO_ABS_MIN_US` / `_MAX_US` | 1000 / 2000 | Absolute limits no servo is driven outside |

The minimum of a working range is CLOSED — full left for pan, full down for tilt. Each servo's
travel must lie inside the absolute range. These defaults are the servos' real travel rather than
a guess, which is why motion works before anything is calibrated; see
[The holoprojector](../use/holo.md#calibrating-the-travel).

### holo-player-fw

| Option | Default | Does |
|---|---|---|
| `CONSOLE_STORE_HISTORY` | on | Keep command history in `/data/history.txt` across restarts |
| `CONSOLE_IGNORE_EMPTY_LINES` | on | Ignore blank input rather than treating it as end-of-input |

### Console WiFi commands

The `cmd_wifi` component adds its own menu, for the NVS namespace saved networks are kept in and
how hard the board tries to reconnect when a link drops.

## sdkconfig.defaults

What is set, and why it matters here:

| Area | Setting | Why |
|---|---|---|
| Target | `esp32s3` | The only supported chip |
| Optimisation | `COMPILER_OPTIMIZATION_PERF` (`-O2`) | Video decoding is the hot path, and the decoder benchmark is only fair at `-O2` |
| Flash | 16 MB, QIO, 80 MHz | The board's part |
| PSRAM | Quad, 80 MHz, `SPIRAM_USE_MALLOC` | Holds a whole frame in the fallback decode path |
| CPU | 240 MHz | Frame budget |
| Partitions | Custom, `partitions.csv` | Two OTA slots and an 11 MB volume |
| Rollback | `BOOTLOADER_APP_ROLLBACK_ENABLE` | Console-installed images boot on trial; see [Updating over serial](../use/ota.md#rollback) |
| Console | UART0 at 115200, secondary console off | The CH343P bridge is the board's only USB path |
| UART | `UART_ISR_IN_IRAM` | Keeps the RX FIFO drained during XMODEM transfers even while a flash operation has the cache off |
| FreeRTOS | Trace facility, stats formatting, run-time stats | `tasks` and `top` |
| Flash counters | `SPI_FLASH_ENABLE_COUNTERS` | `flash-stats` |
| Core dumps | To the `coredump` partition, checked at boot | Post-mortem after a crash |
| Touch | `ESP_LCD_TOUCH_CST816S_DISABLE_READ_ID` | A CST816S only answers on I2C while touched, so reading its ID at init always fails |
| IPv6 | `LWIP_IPV6_AUTOCONFIG` | SLAAC addresses as well as link-local |
| LittleFS | 4096-byte cache, 256-byte read/write, PSRAM-prefer, `LITTLEFS_MMAP_PARTITION` | Tuned for sequential reads of large clips |

The LittleFS settings are the ones worth understanding. The component's defaults — 128-byte reads
and 512-byte caches — measure about 2 MB/s. Memory-mapping the partition serves reads through the
flash cache rather than `esp_flash_read()`, which would stall **both** cores for the duration of
every read, exactly as a write does. See
[Video internals](video-internals.md#flash-writes-and-late-frames).

## Partition table

`partitions.csv`, derived from the Flash_PNG layout less `phy_init` (the PHY init data is built
into the application). `nvs` takes its 4 KB so that `otadata` keeps its original offset.

| Name | Type | Offset | Size | Holds |
|---|---|---|---|---|
| `nvs` | data | 36K | 20K | Saved Wi-Fi networks, servo calibration |
| `otadata` | data | 56K | 8K | Which application slot boots |
| `ota_0` | app | 64K | 2304K | Application slot 0 |
| `ota_1` | app | — | 2304K | Application slot 1 |
| `coredump` | data | — | 64K | Core dumps |
| `storage` | data | 5M | 11M | LittleFS, mounted at `/data` |

Two application slots of 2.25 MB each are what make updating over the console possible: the image
being received always goes into the slot that is not running.
