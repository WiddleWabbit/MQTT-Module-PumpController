# Pump module firmware

How the firmware runs, and the lookup pages, are in [docs/home.md](docs/home.md).

# Getting Started

Burn the fuses over SerialUPDI. This does not upload the image.

```text
pio run -e fuses -t fuses
```

Upload the image over the same port.

```text
pio run -e Upload_UPDI -t upload
```

Desktop tests, with no board attached:

```text
pio test -e native
```
