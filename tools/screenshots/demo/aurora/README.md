# Aurora

A small weather station: readings from a few sensors, a forecast
for the next hours and a Qt Quick view of both.

## Building

```sh
cmake -B build -G Ninja
cmake --build build
./build/aurora --station oslo
```

## Layout

- `src/` — the readings and the forecast (C++)
- `qml/` — the views
- `tools/fetch_readings.py` — pulls a day of readings as JSON
