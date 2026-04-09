# CO2 Monitor for ESP32 + SCD41

家用 CO2 监测器，基于 `ESP32 + SCD41 + SSD1306 OLED`，提供本地 Web UI、Wi‑Fi 配网、OLED 实时显示、历史图表和 CSV 导出。

## 功能

- 首次开机无 Wi‑Fi 时自动进入热点配网模式
- Web UI 可设置刷新周期、报警阈值、报警延迟、OLED 模式
- 历史图表支持 `24h / 7d / 30d / 6mo`
- 本地 CSV 导出：当前图表聚合数据、最近 24 小时原始数据
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

3. 上传网页资源到 LittleFS

```bash
pio run -t uploadfs
```

4. 打开串口监视器

```bash
pio device monitor
```

## Web 访问

- 没有 Wi‑Fi 配置时，设备会开一个热点，例如 `CO2-Monitor-12AB34`
- 手机连接该热点后，访问 `http://192.168.4.1`
- 配好家里 Wi‑Fi 后，设备会尝试连入局域网
- 连网成功后可通过设备 IP 或 `http://co2-monitor.local` 访问

## 存储策略

- 最近 24 小时原始点
- `24h = 5 分钟桶`
- `7d = 30 分钟桶`
- `30d = 2 小时桶`
- `6mo = 12 小时桶`

长期历史只保留聚合结果，避免在 4MB Flash 上保存不必要的全年原始数据。
