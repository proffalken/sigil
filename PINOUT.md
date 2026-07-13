# Sigil Hardware Pinout & Wiring Reference

---

## Cat5 Cable — Controller to Relay

Pairs 2, 3, and 4 are bonded together for the 12V bus to maximise current capacity and minimise voltage drop over runs up to 10m. 5V is generated locally at each relay node using an MP1584EN buck converter fed from the 12V rail.

| Pair | Wires | Signal |
|------|-------|--------|
| 1 | Blue / Blue-White | RS485 A / RS485 B |
| 2 | Orange / Orange-White | 12V+ / GND |
| 3 | Green / Green-White | 12V+ / GND (bonded with pair 2) |
| 4 | Brown / Brown-White | 12V+ / GND (bonded with pairs 2 & 3) |

**Local 5V regulation:** MP1584EN buck converter module (input 12V, output trimmed to 5V before connecting load).

---

## Relay ↔ End Device — Pogo-Pin Cable

Colours are defined from the **relay end**. TX and RX swap at the end device end; the cable colours remain the same throughout.

| Colour | Relay end | End device end |
|--------|-----------|----------------|
| RED    | +5V       | +5V            |
| YELLOW | TX        | RX             |
| WHITE  | RX        | TX             |
| BLACK  | GND       | GND            |

---

## End Device — UART Pin Mapping

Default pins from the `SigilDevice::begin()` example. Pass different values if your board layout requires it.

| Pin | Signal | Pogo-pin wire colour |
|-----|--------|----------------------|
| 9   | RX     | YELLOW               |
| 10  | TX     | WHITE                |
| +5V | Power  | RED                  |
| GND | Ground | BLACK                |

---

## Relay — UART Pin Mapping

| Pin | Signal | Connection |
|-----|--------|------------|
| 22  | RX     | RS485 bus  |
| 21  | TX     | RS485 bus  |
| 16  | RX     | End device (pogo pin — WHITE) |
| 17  | TX     | End device (pogo pin — YELLOW) |
