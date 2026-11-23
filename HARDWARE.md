# Numpad30 — Tổng kết phần cứng

## 1. Tổng quan

| Mục | Thông số |
|---|---|
| Vi điều khiển | Raspberry Pi **RP2350** (Pico 2) |
| Bo mạch | **Waveshare RP2350-Zero** |
| Bộ nhớ | Flash 2 MB |
| Nguồn | USB-C 5 V (VBUS) |
| Màn hình | OLED SSD1306 **128×64**, I²C, địa chỉ 0x3C |
| Bàn phím | Ma trận **5 hàng × 7 cột** có diode, 35 vị trí / **30 phím vật lý** |
| Âm thanh | I2S → **MAX98357A** → loa 4 Ω |
| Lưu trữ | Khe thẻ **microSD** (FAT16/FAT32) |
| Cảm biến | Gyro **MPU-6515 / MPU-6505** |

> Bo mạch hiện tại **không dùng còi buzzer** và **không dùng module MP3 MH2024K**. Mọi âm thanh đều phát qua MAX98357A.

---

## 2. Các module và sơ đồ đấu dây

### 2.1 OLED SSD1306 128×64

| OLED | RP2350-Zero |
|---|---|
| SDA | **GP12** |
| SCL | **GP13** |
| VCC | 3V3 |
| GND | GND |

Bus I²C thứ nhất, tốc độ 400 kHz, địa chỉ 0x3C.

### 2.2 Gyro MPU-6515 (bus I²C riêng)

| MPU-6515 | RP2350-Zero |
|---|---|
| SDA | **GP26** |
| SCL | **GP27** |
| AD0 | **GND** → địa chỉ 0x68 (nối 3V3 → 0x69) |
| VCC | 3V3 |
| GND | GND |

Dùng riêng bus I²C thứ hai, **không chung** với OLED. Tốc độ 400 kHz.

### 2.3 Âm thanh — MAX98357A (I2S)

| MAX98357A | RP2350-Zero | Ghi chú |
|---|---|---|
| **BCLK** | **GP16** | Xung bit clock I2S |
| **LRC / LRCK** | **GP17** | Chọn kênh trái/phải |
| **DIN** | **GP18** | Dữ liệu âm thanh I2S |
| VIN | **5 V (VBUS)** | |
| GND | GND | Phải chung với bo mạch |
| **SD / SD_MODE** | **Nối thẳng vào VIN (5 V)** | Bắt buộc; nếu treo hoặc nối 3,3 V thì ampli ở chế độ tắt → **không có tiếng** |
| GAIN | Để treo (9 dB) hoặc nối GND (15 dB) | |
| OUT+ | Loa 4 Ω | |
| OUT− | Loa 4 Ω | **Không nối OUT− xuống GND** |

Loa 4 Ω nối **giữa OUT+ và OUT−**. GP16 vốn là chân LED RGB trên board — LED đó **đã bị ngắt** để dùng chân này cho I2S.

### 2.4 Khe thẻ microSD (SPI)

| microSD (khe trần 8 chân) | RP2350-Zero |
|---|---|
| **CS / CD** (chân 2) | **GP21** |
| **DI / MOSI** (chân 3) | **GP19** |
| **DO / MISO** (chân 7) | **GP20** |
| **SCK / CLK** (chân 5) | **GP22** |
| **VDD** (chân 4) | **3,3 V** ⚠️ |
| GND (chân 6) | GND |
| DAT1 (chân 8), DAT2 (chân 1) | Để trống |

**Lưu ý quan trọng:**

- Số chân ở bảng trên theo chuẩn khe microSD (1 DAT2, 2 CD/CS, 3 CMD/MOSI, 4 VDD, 5 CLK, 6 VSS, 7 DAT0/MISO, 8 DAT1); hãy đối chiếu với datasheet của khe đang dùng.
- Thẻ trần **chỉ dùng 3,3 V**. Cắm 5 V vào chân 4 là hỏng thẻ. Chỉ cấp 5 V nếu module thẻ có regulator riêng.
- Nên có điện trở **10 kΩ từ CS lên 3V3** để thẻ không bị chọn lúc cấp nguồn.
- Dây **SCK ngắn**, tất cả GND phải chung.
- Thẻ định dạng **FAT16/FAT32**, tên file dạng 8.3 (ví dụ `0001.MP3`, `57CUIN~1.FLA`). Hỗ trợ MP3 và FLAC.

### 2.5 Các chân không còn sử dụng

| GPIO | Từng dùng cho | Hiện tại |
|---|---|---|
| GP14 | Còi buzzer PWM | Trống |
| GP15 | Module MP3 (BUSY) | Trống |
| GP28 | Module MP3 (UART TX) | Trống |
| GP29 | Module MP3 (UART RX) | Trống |
| GP16 | LED RGB onboard | Đã ngắt, dùng cho I2S |
| GP23 | — | Board không có chân này |

---

## 3. Bảng phân bổ GPIO

| GPIO | Chức năng hiện tại |
|---|---|
| GP0 | Ma trận phím **R0** (ra) |
| GP1 | Ma trận phím **R1** (ra) |
| GP2 | Ma trận phím **R2** (ra) |
| GP3 | Ma trận phím **R3** (ra) |
| GP4 | Ma trận phím **R4** (ra) |
| GP5 | Ma trận phím **C0** (vào) |
| GP6 | Ma trận phím **C1** (vào) |
| GP7 | Ma trận phím **C2** (vào) |
| GP8 | Ma trận phím **C3** (vào) |
| GP9 | Ma trận phím **C4** (vào) |
| GP10 | Ma trận phím **C5** (vào) |
| GP11 | Ma trận phím **C6** (vào) |
| GP12 | OLED SDA |
| GP13 | OLED SCL |
| GP14 | Trống |
| GP15 | Trống |
| GP16 | I2S **BCLK** |
| GP17 | I2S **LRCK** |
| GP18 | I2S **DIN** |
| GP19 | Thẻ SD **MOSI** |
| GP20 | Thẻ SD **MISO** |
| GP21 | Thẻ SD **CS** |
| GP22 | Thẻ SD **SCK** |
| GP23 | Không có trên board |
| GP24 | Trống |
| GP25 | Trống |
| GP26 | Gyro SDA |
| GP27 | Gyro SCL |
| GP28 | Trống |
| GP29 | Trống |

> Các chân SPI0 có chức năng cố định: **GP19 = TX (MOSI), GP20 = RX (MISO), GP21 = CS, GP22 = SCK**. Không hoán đổi tùy ý.

---

## 4. Ma trận phím 5 × 7

### 4.1 Nguyên lý đấu

- **5 hàng R0–R4**: chân ra (GP0–GP4).
- **7 cột C0–C6**: chân vào (GP5–GP11), có điện trở kéo xuống.
- Mỗi công tắc mắc **diode 1N4148**: **anode về công tắc, cathode (vạch) về cột** — hướng **ROW → COL**.
- Quét từng hàng một, đọc 7 cột. Diode chống hiện tượng phím ma (ghosting).

```
   GP5   GP6   GP7   GP8   GP9   GP10  GP11
    C0    C1    C2    C3    C4    C5    C6
      │     │     │     │     │     │     │
GP0 ──┼─────┼─────┼─────┼─────┼─────┼─────┤  R0
      │  ▷  │  ▷  │  ▷  │  ▷  │  ▷  │  ▷  │   ▷ = diode 1N4148
GP1 ──┼─────┼─────┼─────┼─────┼─────┼─────┤  R1   anode ở công tắc,
      │  ▷  │  ▷  │  ▷  │  ▷  │  ▷  │  ▷  │        cathode ở cột
GP2 ──┼─────┼─────┼─────┼─────┼─────┼─────┤  R2
      │  ▷  │  ▷  │  ▷  │  ▷  │  ▷  │  ▷  │
GP3 ──┼─────┼─────┼─────┼─────┼─────┼─────┤  R3
      │  ▷  │  ▷  │  ▷  │  ▷  │  ▷  │  ▷  │
GP4 ──┼─────┼─────┼─────┼─────┼─────┼─────┤  R4
```

### 4.2 Bố trí 30 phím

```
      C0        C1        C2        C3        C4        C5        C6
R0  Ctrl+C   Ctrl+V    Ctrl+X    [ Num ]      /         *         -
R1  Ctrl+Z   Ctrl+Y    Ctrl+A       7         8         9         +
R2  Ctrl+S   Alt+Tab   Win+D        4         5         6       trống
R3  trống    Esc       trống      End        Ins       Del     [ Enter ]
R4    ←        ↓         →      [ 0 rộng 2U ] trống     .       trống
```

- Ô **không lắp công tắc**: **R3C0, R3C2**.
- Ô **bị phím 2U chiếm** (không có công tắc riêng): **R2C6** (dưới phím +), **R4C4** (cạnh phím 0), **R4C6** (dưới phím Enter). Phím Enter 2U nằm ở R3C6.
- Phím **+** và **Enter** cao 2U; phím **0** rộng 2U.
- Cột trái là nhóm phím tắt/macro, giữa là cụm mũi tên, phải là numpad.
- Giữ **Num** → lớp phím phụ (FN). Giữ Num + **Enter** (R3C6) → lớp nhạc cụ. Phím **.** trên FN dùng để vào chế độ nạp firmware.

---

## 5. Sơ đồ kết nối tổng thể

```
   USB-C 5V ──────────► VBUS
   GND ───────────────► GND
                         │
   Ma trận 5x7 ── GP0–GP11
   OLED SSD1306 ─ GP12 / GP13   (I²C, 0x3C)
   MAX98357A ─── GP16 / GP17 / GP18  → loa 4Ω
   microSD ───── GP19 / GP20 / GP21 / GP22  (SPI, thẻ 3,3V)
   Gyro MPU ──── GP26 / GP27   (I²C riêng, 0x68)
                         │
                  RP2350-Zero
```

**Tóm tắt nguồn:**

- USB-C 5 V cấp cho bo mạch và cho MAX98357A (VIN).
- OLED, gyro, thẻ SD dùng **3,3 V** từ bo mạch.
- Tất cả thiết bị phải **chung GND**.
