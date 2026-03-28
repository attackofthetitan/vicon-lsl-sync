#!/usr/bin/env python3import argparse
import sys
import time
import argparse

import numpy as np
import pandas as pd
import pylsl


def resolve_streams(stream_types: list[str], timeout: float = 10.0) -> list[pylsl.StreamInfo]:
    all_streams = []
    for stype in stream_types:
        print(f"Looking for {stype} streams")
        streams = pylsl.resolve_byprop("type", stype, timeout=timeout)
        if streams:
            for s in streams:
                print(f"  Found {s.name()} - {s.channel_count()} channels")
                all_streams.append(s)
        else:
            print(f"  No {stype} streams found")
    return all_streams


def get_channel_labels(inlet: pylsl.StreamInlet) -> list[str]:
    info = inlet.info()
    labels = []
    channels = info.desc().child("channels")
    if channels.empty():
        return [f"ch{i}" for i in range(info.channel_count())]

    ch = channels.child("channel")
    while not ch.empty():
        label = ch.child_value("label")
        labels.append(label if label else f"ch{len(labels)}")
        ch = ch.next_sibling("channel")
    return labels


def record_streams(stream_infos: list[pylsl.StreamInfo], duration: float) -> dict[str, pd.DataFrame]:
    inlets = []
    stream_names = []

    for info in stream_infos:
        inlet = pylsl.StreamInlet(
            info,
            processing_flags=pylsl.proc_clocksync | pylsl.proc_dejitter
        )
        inlet.open_stream()
        inlets.append(inlet)
        stream_names.append(info.name())

    print(f"\nRecording {len(inlets)} streams for {duration}s")

    stream_data: dict[str, dict] = {}
    for name, inlet in zip(stream_names, inlets):
        labels = get_channel_labels(inlet)
        stream_data[name] = {
            "timestamps": [],
            "samples": [],
            "labels": labels,
        }

    start_time = time.time()
    sample_counts = {name: 0 for name in stream_names}

    while time.time() - start_time < duration:
        for name, inlet in zip(stream_names, inlets):
            while True:
                sample, timestamp = inlet.pull_sample(timeout=0.0)
                if sample is None:
                    break
                stream_data[name]["timestamps"].append(timestamp)
                stream_data[name]["samples"].append(sample)
                sample_counts[name] += 1

        time.sleep(0.001)

    elapsed = time.time() - start_time
    print(f"Done, {elapsed:.1f}s recorded")
    for name, count in sample_counts.items():
        rate = count / elapsed if elapsed > 0 else 0
        print(f"  {name} - {count} samples, {rate:.1f} Hz")

    result = {}
    for name, data in stream_data.items():
        if data["timestamps"]:
            df = pd.DataFrame(data["samples"], columns=data["labels"])
            df.insert(0, "timestamp", data["timestamps"])
            result[name] = df
        else:
            print(f"  No data from {name}")

    for inlet in inlets:
        inlet.close_stream()

    return result


def align_by_nearest_timestamp(dataframes: dict[str, pd.DataFrame], reference: str | None = None, tolerance: float = 0.01, ) -> pd.DataFrame:
    if len(dataframes) == 0:
        return pd.DataFrame()

    if len(dataframes) == 1:
        name = list(dataframes.keys())[0]
        df = dataframes[name].copy()
        df.columns = [f"{name}_{c}" if c != "timestamp" else c for c in df.columns]
        return df

    if reference is None:
        reference = max(dataframes, key=lambda k: len(dataframes[k]))
    if reference not in dataframes:
        raise ValueError(f"Reference stream {reference} not found")

    ref_df = dataframes[reference].copy()
    ref_timestamps = ref_df["timestamp"].values
    ref_df.columns = [f"{reference}_{c}" if c != "timestamp" else c for c in ref_df.columns]

    for name, df in dataframes.items():
        if name == reference:
            continue

        other_timestamps = df["timestamp"].values
        other_cols = [c for c in df.columns if c != "timestamp"]
        prefixed_cols = [f"{name}_{c}" for c in other_cols]

        matched_data = np.full((len(ref_timestamps), len(other_cols)), np.nan)

        if len(other_timestamps) > 0:
            for i, ref_t in enumerate(ref_timestamps):
                idx = np.searchsorted(other_timestamps, ref_t)

                best_idx = None
                best_diff = tolerance + 1

                for candidate in [idx - 1, idx]:
                    if 0 <= candidate < len(other_timestamps):
                        diff = abs(other_timestamps[candidate] - ref_t)
                        if diff < best_diff:
                            best_diff = diff
                            best_idx = candidate

                if best_idx is not None and best_diff <= tolerance:
                    matched_data[i, :] = df.iloc[best_idx][other_cols].values

        matched_df = pd.DataFrame(matched_data, columns=prefixed_cols)
        ref_df = pd.concat([ref_df, matched_df], axis=1)

    return ref_df


def main():
    parser = argparse.ArgumentParser(description="Receive and align LSL streams")
    parser.add_argument("--duration", type=float, default=30.0, help="Recording duration in seconds")
    parser.add_argument("--output", type=str, default="recording.csv", help="Output CSV path")
    parser.add_argument("--tolerance", type=float, default=0.01, help="Alignment tolerance in seconds")
    parser.add_argument("--timeout", type=float, default=10.0, help="Stream resolution timeout")
    parser.add_argument("--types", nargs="+", default=["MoCap", "Gaze"], help="LSL stream types to look for")
    args = parser.parse_args()

    stream_infos = resolve_streams(args.types, timeout=args.timeout)
    if not stream_infos:
        print("No streams found")
        sys.exit(1)

    dataframes = record_streams(stream_infos, args.duration)
    if not dataframes:
        print("No data recorded")
        sys.exit(1)

    print(f"\nAligning streams, tolerance {args.tolerance}s")
    aligned = align_by_nearest_timestamp(dataframes, tolerance=args.tolerance)
    print(f"Result - {aligned.shape[0]} rows, {aligned.shape[1]} columns")

    aligned.to_csv(args.output, index=False)
    print(f"Saved to {args.output}")

    for name, df in dataframes.items():
        individual_path = args.output.replace(".csv", f"_{name}.csv")
        df.to_csv(individual_path, index=False)
        print(f"Saved {name} to {individual_path}")


if __name__ == "__main__":
    main()
