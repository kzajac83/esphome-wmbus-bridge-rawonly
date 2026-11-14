"""Append raw wM-Bus captures received over MQTT to a CSV file."""

import argparse
import csv
from datetime import datetime, timezone
from getpass import getpass
import json
import os
from pathlib import Path
import re

import paho.mqtt.client as mqtt


FIELDS = (
    "logged_at_utc", "mqtt_topic", "uptime_ms", "chip", "listen_mode",
    "radio_mode", "rssi_dbm", "raw_len", "raw_hex",
)


def capture_row(topic, payload):
    packet = json.loads(payload)
    if not isinstance(packet, dict) or packet.get("event") != "radio_raw":
        raise ValueError("expected a radio_raw event")
    raw = packet["raw"]
    if (
        not isinstance(raw, str)
        or re.fullmatch(r"(?:[0-9a-fA-F]{2})+", raw) is None
        or packet["raw_len"] * 2 != len(raw)
    ):
        raise ValueError("invalid raw data or length")
    return (
        datetime.now(timezone.utc).isoformat(timespec="milliseconds"),
        topic, packet["uptime_ms"], packet["chip"], packet["listen_mode"],
        packet["mode"], packet["rssi"], packet["raw_len"], raw,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("broker")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--topic", default="wmbus/lilygo-t2-capture/raw")
    parser.add_argument("--user")
    parser.add_argument("--output", type=Path, default=Path("wmbus-capture.csv"))
    args = parser.parse_args()

    client = mqtt.Client(protocol=mqtt.MQTTv311)
    if args.user:
        password = os.environ.get("WMBUS_MQTT_PASSWORD")
        if password is None:
            password = getpass("MQTT password: ")
        client.username_pw_set(args.user, password)

    with args.output.open("a+", encoding="utf-8", newline="") as output:
        output.seek(0)
        header = next(csv.reader(output), None)
        if header is not None and tuple(header) != FIELDS:
            parser.error(f"{args.output} has a different CSV header")
        output.seek(0, os.SEEK_END)
        writer = csv.writer(output)
        if header is None:
            writer.writerow(FIELDS)
            output.flush()

        def on_connect(client, userdata, flags, result):
            if result != 0:
                raise RuntimeError(f"broker rejected the connection ({result})")
            client.subscribe(args.topic)

        def on_subscribe(client, userdata, mid, granted_qos):
            if tuple(granted_qos) != (0,):
                raise RuntimeError(f"broker rejected subscription to {args.topic}")
            print(f"{args.topic} -> {args.output}", flush=True)

        def on_message(client, userdata, message):
            if message.retain:
                return
            try:
                row = capture_row(message.topic, message.payload)
            except (ValueError, KeyError, TypeError) as error:
                print(f"Skipping malformed MQTT event: {error}", flush=True)
                return
            writer.writerow(row)
            output.flush()

        client.on_connect = on_connect
        client.on_subscribe = on_subscribe
        client.on_message = on_message
        client.connect(args.broker, args.port)
        try:
            client.loop_forever()
        except KeyboardInterrupt:
            pass
        finally:
            client.disconnect()


if __name__ == "__main__":
    main()
