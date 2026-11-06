GPxx, GPIOxx == Dxx 

---

# pico1

```
TX       D0      -> RX(ESP 1,2,3,4)

SCLK0    D2      -> D12(ESP 1)
MOSI0    D3      -> D11(ESP 1)

CS0      D5      -> D10(ESP 1)
RDY0     D6      <- D13(ESP 1)

D+       D8      -> USB-A-D+
D-       D9      -> USB-A-D-

남는 SPI 포트는 3.5 TFT LCD 제어하는데 사용.(구현 제일 후순위)

```
---

# ESP1

```
SCLK1   D4      -> D12(ESP 2)
MOSI1   D5      -> D11(ESP 2)
CS1     D6      -> D10(ESP 2)
RDY1    D15     <- D16(ESP 2)

```
---

# ESP2

```
SCLK2   D4      -> D12(ESP 3)
MOSI2   D5      -> D11(ESP 3)
CS2     D6      -> D10(ESP 3)
RDY2    D15     <- D16(ESP 3)

```
---

# ESP3

```
SCLK3   D4      -> D12(ESP 4)
MOSI3   D5      -> D11(ESP 4)
CS3     D6      -> D10(ESP 4)
RDY3    D15     <- D16(ESP 4)

```
# ESP4

```
TX              -> RX(pico1)
```
