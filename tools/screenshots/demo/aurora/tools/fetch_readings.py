#!/usr/bin/env python3
"""Pulls a day of readings for a station and prints them as JSON."""

import argparse
import json
import math
import random
from datetime import datetime, timedelta


def reading(at: datetime, base: float) -> dict:
    hour = at.hour + at.minute / 60
    daily = math.sin((hour - 9) / 24 * 2 * math.pi)
    return {
        "time": at.isoformat(timespec="minutes"),
        "temperature": round(base + 6 * daily, 1),
        "humidity": round(70 - 15 * daily + random.uniform(-3, 3)),
        "pressure": round(1013 + random.gauss(0, 1.5), 1),
        "wind": round(abs(random.gauss(4, 2)), 1),
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("station")
    parser.add_argument("--base", type=float, default=8.0)
    args = parser.parse_args()

    start = datetime.now().replace(minute=0, second=0, microsecond=0)
    readings = [reading(start - timedelta(hours=h), args.base)
                for h in reversed(range(24))]
    print(json.dumps({"station": args.station, "readings": readings},
                     indent=2))


if __name__ == "__main__":
    main()
