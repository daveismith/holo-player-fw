# Host link: I2C

!!! note "Not in the firmware yet"
    This page describes the I2C link ahead of the firmware, for review. UART and RS485 work now.

On I2C, the board is a target on the host's bus, like a sensor, at an address of its own (0x42
unless changed). It suits a host that already runs an I2C bus through the dome. It speaks
[native frames](../reference/host-protocol.md#i2c) through a few registers, and pulls an
interrupt line low when it has an event for the host.

This bus is separate from the board's own I2C bus, which serves its touch controller and IMU.
Their addresses don't matter here.

## Four wires

| Board | P2 pin | Host bus |
|---|---|---|
| GPIO21, SDA | 11 | SDA |
| GPIO33, SCL | 12 | SCL |
| GPIO15, IRQ | 7 | An input, with a pull-up (optional) |
| GND | 1 or 5 | GND |

## Pull-ups

An I2C bus needs one pull-up resistor on SDA and one on SCL, to the bus voltage. The board has
none on these pins, so the bus must have them somewhere: on the host, on another device, or added.

- **4.7 kΩ** to 3.3 V for 100 kHz on short wires; 2.2 kΩ for longer ones, or 400 kHz.
- **Only one set on the bus**, or at least not so many in parallel that the total drops below
  about 1 kΩ.

## 3.3 V and 5 V

**The board must never see 5 V on SDA or SCL.** On a 5 V bus (an Arduino Uno, most MarcDuinos),
put a bidirectional I2C level shifter between the bus and the board: a PCA9306, or a BSS138
module. Its 3.3 V side takes the board and 3.3 V pull-ups, and its 5 V side the host.

!!! warning "Arduino's Wire library pulls the bus up to 5 V"
    On an AVR, `Wire.begin()` turns on the chip's own pull-ups, to 5 V. They are weak, but enough
    to put 5 V on a board wired straight to the bus. Always use a level shifter with a 5 V host.

## Speed and length

- **100 kHz** is the speed to use. 400 kHz should work on short wires, and will be confirmed when
  the I2C link is built.
- **I2C is for short runs**, a metre or so. Through a slip ring or across a whole droid, use
  [RS485](host-rs485.md).
- **Clock stretching.** The board may hold SCL low briefly at the start of a read. Most hosts
  wait for it. A Raspberry Pi's I2C hardware doesn't handle stretching reliably: on a Pi, use
  the `i2c-gpio` overlay, a software bus that does.

## The interrupt line

The board pulls IRQ low while it has events the host hasn't read, and lets it go when there are
none. It's open drain: the host needs a pull-up on it, which a microcontroller's internal one
usually covers. The IRQ lines of several boards can share one host input.

After an interrupt, the host reads each board's `STATUS` register, and reads `EVENT` from each
one with `pending`. A host that doesn't wire IRQ can read `STATUS` now and then instead.

## Turning it on

```text
link mode i2c -a 0x42
restart
```

Each board on a bus needs its own address, from 0x08 to 0x77, and one no other device on the bus
uses.

## Registers

| Register | Access | Holds |
|---|---|---|
| 0x00 `STATUS` | read, 14 bytes | The protocol version, what the board is doing, and how many events are waiting |
| 0x10 `COMMAND` | write | A command: apply a scene, set the LEDs, move the holo… |
| 0x11 `RESULT` | read | The last command's result |
| 0x20 `EVENT` | read, 8 bytes | The oldest event not yet read |
| 0x30 `VERSION` | read, 32 bytes | The firmware version |

[The protocol reference](../reference/host-protocol.md#i2c) has their exact contents.

## A first test, from an Arduino

Apply the scene in slot 3, and read the result:

```cpp
#include <Wire.h>

const uint8_t HOLO = 0x42;

uint16_t crc16(const uint8_t *p, size_t n) {       // CRC-16/CCITT-FALSE
  uint16_t crc = 0xFFFF;
  while (n--) {
    crc ^= (uint16_t)*p++ << 8;
    for (int i = 0; i < 8; i++) crc = crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1;
  }
  return crc;
}

void setup() {
  Serial.begin(115200);
  Wire.begin();

  uint8_t cmd[] = {0x10, /* seq */ 1, /* SCENE */ 0x10, /* slot */ 3, 0, 0};
  uint16_t crc = crc16(cmd + 1, 3);
  cmd[4] = crc & 0xFF;
  cmd[5] = crc >> 8;
  Wire.beginTransmission(HOLO);
  Wire.write(cmd, sizeof(cmd));
  Wire.endTransmission();

  uint8_t status = 0xFE;
  while (status == 0xFE) {                         // still working
    delay(5);
    Wire.beginTransmission(HOLO);
    Wire.write(0x11);                              // RESULT
    Wire.endTransmission(false);                   // repeated start
    Wire.requestFrom(HOLO, (uint8_t)6);
    Wire.read();                                   // seq
    Wire.read();                                   // type + 0x80
    status = Wire.read();
    Wire.read();                                   // pending
    Wire.read(); Wire.read();                      // crc16
  }
  Serial.println(status == 0 ? "scene 3 applied" : "scene 3 failed");
}

void loop() {}
```

(The host above is 3.3 V, or behind a level shifter.)

From a Pi, `i2cget -y 1 0x42 0x00` reads the first byte of `STATUS`, the protocol version, which
is 1. That's a quick way to check the wiring before writing any code.
