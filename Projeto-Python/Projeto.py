import argparse
import binascii
import csv
import json
import math
import os
import struct
import sys
import time
import zlib
from datetime import datetime, timezone

import numpy as np


def norm_sq(values):
    return float(values @ values)


def cgnr(h, g, tol=1e-4, max_iter=10):
    """Resolve Hf = g pelo metodo CGNR."""
    rows, cols = h.shape
    f = np.zeros(cols, dtype=float)
    r = g.copy()
    z = h.T @ r
    p = z.copy()
    prev_norm = norm_sq(r)
    final_norm = math.sqrt(prev_norm)
    iterations = 0

    for i in range(max_iter):
        iterations = i + 1
        w = h @ p
        numer = norm_sq(z)
        denom = norm_sq(w)
        if denom == 0.0:
            break

        alpha = numer / denom
        f += alpha * p
        r -= alpha * w

        z_next = h.T @ r
        beta = 0.0 if numer == 0.0 else norm_sq(z_next) / numer
        p = z_next + beta * p
        z = z_next

        curr_norm = norm_sq(r)
        final_norm = math.sqrt(curr_norm)
        if abs(curr_norm - prev_norm) < tol:
            break
        prev_norm = curr_norm

    return f, iterations, final_norm


def cgne(h, g, tol=1e-4, max_iter=10):
    """Resolve Hf = g pelo metodo CGNE."""
    rows, cols = h.shape
    f = np.zeros(cols, dtype=float)
    r = g.copy()
    p = h.T @ r
    prev_norm = norm_sq(r)
    final_norm = math.sqrt(prev_norm)
    iterations = 0

    for i in range(max_iter):
        iterations = i + 1
        rtr = norm_sq(r)
        ptp = norm_sq(p)
        if ptp == 0.0:
            break

        alpha = rtr / ptp
        f += alpha * p
        r -= alpha * (h @ p)

        rtr_next = norm_sq(r)
        beta = 0.0 if rtr == 0.0 else rtr_next / rtr
        p = (h.T @ r) + beta * p

        curr_norm = norm_sq(r)
        final_norm = math.sqrt(curr_norm)
        if abs(curr_norm - prev_norm) < tol:
            break
        prev_norm = curr_norm

    return f, iterations, final_norm


def iso_now():
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def memory_mb():
    try:
        import resource

        usage = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
        if sys.platform == "darwin":
            return float(usage) / 1024.0 / 1024.0
        return float(usage) / 1024.0
    except Exception:
        return 0.0


def png_chunk(kind, payload):
    body = kind + payload
    return struct.pack(">I", len(payload)) + body + struct.pack(">I", binascii.crc32(body) & 0xFFFFFFFF)


def save_image(values, path, size_pixels):
    if values.size == 0 or size_pixels <= 0:
        return

    width = int(math.sqrt(size_pixels))
    height = width
    if width * width != size_pixels:
        width = size_pixels
        height = 1

    pixels = np.zeros(width * height, dtype=np.uint8)
    count = min(pixels.size, values.size)
    min_value = float(np.min(values))
    max_value = float(np.max(values))
    if min_value == max_value:
        max_value = min_value + 1.0

    normalized = (values[:count] - min_value) / (max_value - min_value)
    pixels[:count] = np.clip(normalized * 255.0, 0, 255).astype(np.uint8)

    raw = bytearray()
    for y in range(height):
        raw.append(0)
        start = y * width
        raw.extend(pixels[start : start + width].tobytes())

    png = bytearray(b"\x89PNG\r\n\x1a\n")
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0)
    png.extend(png_chunk(b"IHDR", ihdr))
    png.extend(png_chunk(b"IDAT", zlib.compress(bytes(raw))))
    png.extend(png_chunk(b"IEND", b""))

    with open(path, "wb") as output:
        output.write(png)


def append_csv(path, metas):
    exists = os.path.exists(path)
    with open(path, "a", newline="", encoding="utf-8") as output:
        writer = csv.writer(output)
        if not exists:
            writer.writerow(
                [
                    "timestamp",
                    "algorithm",
                    "method",
                    "start_iso",
                    "end_iso",
                    "start_ms",
                    "end_ms",
                    "elapsed_ms",
                    "cpu_seconds",
                    "memory_mb",
                    "size_pixels",
                    "iterations",
                    "final_res_norm",
                    "image_path",
                ]
            )
        timestamp = time.time_ns()
        for meta in metas:
            writer.writerow(
                [
                    timestamp,
                    meta["algorithm"],
                    meta["method"],
                    meta["start_iso"],
                    meta["end_iso"],
                    meta["start_ms"],
                    meta["end_ms"],
                    meta["elapsed_ms"],
                    meta["cpu_seconds"],
                    meta["memory_mb"],
                    meta["size_pixels"],
                    meta["iterations"],
                    meta["final_res_norm"],
                    meta["image_path"],
                ]
            )


def run_method(method, h, g, params, output_dir, image_size):
    start_iso = iso_now()
    start_ms = time.time_ns() // 1_000_000
    start_cpu = time.process_time()

    if method == "CGNR":
        f, iterations, final_norm = cgnr(h, g, params["tol"], params["max_iter"])
    else:
        f, iterations, final_norm = cgne(h, g, params["tol"], params["max_iter"])

    cpu_seconds = time.process_time() - start_cpu
    end_ms = time.time_ns() // 1_000_000
    end_iso = iso_now()
    image_path = os.path.join(output_dir, f"img_python_{method}_{time.time_ns()}.png")
    save_image(f, image_path, image_size)

    meta = {
        "algorithm": "python",
        "method": method,
        "start_iso": start_iso,
        "end_iso": end_iso,
        "start_ms": start_ms,
        "end_ms": end_ms,
        "elapsed_ms": end_ms - start_ms,
        "cpu_seconds": cpu_seconds,
        "memory_mb": memory_mb(),
        "size_pixels": image_size,
        "iterations": iterations,
        "final_res_norm": final_norm,
        "image_path": image_path,
    }

    with open(image_path + ".json", "w", encoding="utf-8") as output:
        json.dump(meta, output, ensure_ascii=True)
        output.write("\n")

    return meta


def reconstruct(input_path, output_dir, csv_path=None):
    with open(input_path, "r", encoding="utf-8-sig") as input_file:
        payload = json.load(input_file)

    g = np.asarray(payload["g"], dtype=float)
    h = np.asarray(payload["H"], dtype=float)
    if h.ndim != 2:
        raise ValueError("H precisa ser uma matriz")
    if g.shape[0] != h.shape[0]:
        raise ValueError(f"g tem {g.shape[0]} valores, mas H tem {h.shape[0]} linhas")

    params = payload.get("params", {})
    normalized_params = {
        "max_iter": int(params.get("max_iter", 10)),
        "tol": float(params.get("tol", 1e-4)),
    }
    image_size = int(payload.get("image_size", h.shape[1]))

    os.makedirs(output_dir, exist_ok=True)
    metas = [
        run_method("CGNE", h, g, normalized_params, output_dir, image_size),
        run_method("CGNR", h, g, normalized_params, output_dir, image_size),
    ]
    if csv_path:
        append_csv(csv_path, metas)
    return metas


def demo():
    h = np.array([[3.0, 2.0], [2.0, 6.0]])
    g = np.array([2.0, -8.0])
    f, iterations, residual = cgnr(h, g)
    print("Solution:", f)
    print("Iterations:", iterations)
    print("Final residual:", residual)


def main():
    parser = argparse.ArgumentParser(description="Reconstrucao de imagens em Python.")
    parser.add_argument("--input", help="JSON recebido pelo servidor")
    parser.add_argument("--output-dir", default="output", help="pasta para imagens e metadados")
    parser.add_argument("--append-csv", help="CSV comparativo para anexar os resultados")
    args = parser.parse_args()

    if not args.input:
        demo()
        return

    metas = reconstruct(args.input, args.output_dir, args.append_csv)
    print(json.dumps(metas, ensure_ascii=True))


if __name__ == "__main__":
    main()
