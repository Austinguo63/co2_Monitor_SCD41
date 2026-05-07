# CO2 Monitor for ESP32 + SCD41

Home CO2 monitor based on `ESP32 + SCD41 + SSD1306 OLED`, designed to run in `USB local control mode` by default.

The device keeps Wi-Fi and hotspot disabled by default. Connect it to your computer over USB and run the local bridge to view live readings, history charts, CSV exports, runtime settings, outdoor calibration, and factory reset in a browser at `localhost`.

## Features

- USB local web control, no cloud and no Wi-Fi required
- Live CO2 / temperature / humidity / 60-minute average readings
- History charts for `24h / 7d / 30d / 6mo`
- CSV export for chart aggregates and last 24 hours of raw samples
- SCD41 outdoor calibration and factory reset
- OLED display for current CO2, 60-minute average, and 24-hour stats

## Wiring

### SCD41

- `VCC -> 3V3`
- `GND -> GND`
- `SDA -> GPIO21`
- `SCL -> GPIO22`

### SSD1306 0.96 OLED

- `VCC -> 3V3`
- `GND -> GND`
- `SDA -> GPIO18`
- `SCL -> GPIO19`

If these pins do not fit your ESP32 board, update [pins.h](/Users/caesar/Downloads/co2_Monitor/include/pins.h).

## Build and Flash

1. Install `PlatformIO`
2. Flash the firmware

```bash
pio run -t upload
```

Note: in the current setup, the web UI is served by the local computer bridge, so `uploadfs` is no longer required.

## Local Web UI

### Daily use

1. Connect the device to your computer over USB
2. Open the project directory
3. Run:

```bash
python3 tools/local_ui.py --open
```

4. Open `http://127.0.0.1:8765` in your browser
5. Press `Ctrl+C` in the terminal when finished

### If the serial port is not detected automatically

List the available serial ports first:

```bash
python3 -m serial.tools.list_ports
```

Then specify the port with a placeholder instead of a machine-specific name:

```bash
python3 tools/local_ui.py --port <PORT> --baud 115200 --listen 127.0.0.1:8765 --open
```

Notes:

- The bridge will try to find a single USB serial device automatically
- The device time is synced from the computer after the first connection
- If the device reboots after power loss, reconnect the local bridge to restore absolute time
- Keep `<PORT>` as a placeholder in shared docs and commits instead of using a local device name

## Storage Strategy

- No timestamped history is written before absolute time is available
- Last 24 hours of raw samples
- `24h = 5-minute buckets`
- `7d = 30-minute buckets`
- `30d = 2-hour buckets`
- `6mo = 12-hour buckets`

Long-term history keeps aggregated data only, which avoids wasting flash on full-resolution year-round raw logs.

---

家用 CO2 监测器，基于 `ESP32 + SCD41 + SSD1306 OLED`，默认工作在 `USB 本地控制模式`。

设备本体默认关闭 Wi‑Fi / 热点。把设备通过 USB 接到电脑后，运行本地代理即可在 `localhost` 页面上查看实时数据、历史图表、导出 CSV、修改运行参数，以及执行室外校准和工厂重置。

## 功能

- USB 本地网页控制，不依赖云、不依赖 Wi‑Fi
- 实时显示 CO2 / 温度 / 湿度 / 60 分钟平均
- 历史图表支持 `24h / 7d / 30d / 6mo`
- CSV 导出：当前图表聚合数据、最近 24 小时原始数据
- SCD41 室外校准和工厂重置
- OLED 显示当前 CO2、60 分钟平均、24 小时统计

## 接线

### SCD41

- `VCC -> 3V3`
- `GND -> GND`
- `SDA -> GPIO21`
- `SCL -> GPIO22`

### SSD1306 0.96 OLED

- `VCC -> 3V3`
- `GND -> GND`
- `SDA -> GPIO18`
- `SCL -> GPIO19`

如果你的 ESP32 板子这些引脚不方便，只需要修改 [pins.h](/Users/caesar/Downloads/co2_Monitor/include/pins.h)。

## 构建与烧录

1. 安装 `PlatformIO`
2. 烧录固件

```bash
pio run -t upload
```

说明：当前模式下网页由电脑端本地代理提供，不再需要 `uploadfs` 上传网页资源到板子。

## 本地网页访问

### 每次日常使用

1. 用 USB 把设备接到电脑
2. 进入项目目录
3. 运行：

```bash
python3 tools/local_ui.py --open
```

4. 浏览器访问 `http://127.0.0.1:8765`
5. 用完后在终端按 `Ctrl+C` 停掉本地代理

### 如果自动找不到串口

先查看当前可用串口：

```bash
python3 -m serial.tools.list_ports
```

再用占位符方式指定端口，不要把你自己机器上的端口名写进文档或提交记录：

```bash
python3 tools/local_ui.py --port <PORT> --baud 115200 --listen 127.0.0.1:8765 --open
```

说明：

- 代理会自动尝试寻找唯一的 USB 串口设备
- 首次连上后会把电脑当前时间同步给设备
- 如果设备断电重启，需要重新连上本地代理，绝对时间才会恢复
- README 和分享给别人的命令里只保留 `<PORT>` 这种占位符，不写你本机的具体设备名

## 存储策略

- 没有绝对时间前，不写入带时间戳的历史点
- 最近 24 小时原始点
- `24h = 5 分钟桶`
- `7d = 30 分钟桶`
- `30d = 2 小时桶`
- `6mo = 12 小时桶`

长期历史只保留聚合结果，避免在 4MB Flash 上保存不必要的全年原始数据。

## License

This project is licensed under the MIT License. See [LICENSE](/Users/caesar/Downloads/co2_Monitor/LICENSE).
