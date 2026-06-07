import binascii
import csv
import ctypes
import json
import math
import os
import struct
import sys
import time
import zlib
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, HTTPServer

import numpy as np


def norm_sq(values):
    # Multiplicação matricial
    return float(values @ values)


def cgnr(h, g, tol=1e-4, max_iter=10):
    _, cols = h.shape
    f = np.zeros(cols)
    r = g.copy()
    # Multiplicação matricial
    z = h.T @ r
    p = z.copy()
    prev_norm = math.sqrt(norm_sq(r))
    final_norm = prev_norm
    iterations = 0

    for i in range(max_iter):
        iterations = i + 1
        # Multiplicação matricial
        w = h @ p
        numer = norm_sq(z)
        denom = norm_sq(w)
        if denom == 0.0:
            break

        alpha = numer / denom
        f += alpha * p
        r -= alpha * w

        # Multiplicação matricial
        z_next = h.T @ r
        beta = 0.0 if numer == 0.0 else norm_sq(z_next) / numer
        p = z_next + beta * p
        z = z_next

        curr_norm = math.sqrt(norm_sq(r))
        final_norm = curr_norm
        if abs(curr_norm - prev_norm) < tol:
            break
        prev_norm = curr_norm

    return f, iterations, final_norm


def cgne(h, g, tol=1e-4, max_iter=10):
    _, cols = h.shape
    f = np.zeros(cols)
    r = g.copy()
    # Multiplicação matricial
    p = h.T @ r
    prev_norm = math.sqrt(norm_sq(r))
    final_norm = prev_norm
    iterations = 0

    for i in range(max_iter):
        iterations = i + 1
        rtr = norm_sq(r)
        ptp = norm_sq(p)
        if ptp == 0.0:
            break

        alpha = rtr / ptp
        f += alpha * p
        # Multiplicação matricial
        r -= alpha * (h @ p)

        rtr_next = norm_sq(r)
        beta = 0.0 if rtr == 0.0 else rtr_next / rtr
        # Multiplicação matricial
        p = (h.T @ r) + beta * p

        curr_norm = math.sqrt(norm_sq(r))
        final_norm = curr_norm
        if abs(curr_norm - prev_norm) < tol:
            break
        prev_norm = curr_norm

    return f, iterations, final_norm


def iso_now():
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def memory_mb():
    if sys.platform.startswith("win"):
        class ProcessMemoryCounters(ctypes.Structure):
            _fields_ = [
                ("cb", ctypes.c_ulong),
                ("PageFaultCount", ctypes.c_ulong),
                ("PeakWorkingSetSize", ctypes.c_size_t),
                ("WorkingSetSize", ctypes.c_size_t),
                ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                ("PagefileUsage", ctypes.c_size_t),
                ("PeakPagefileUsage", ctypes.c_size_t),
            ]

        counters = ProcessMemoryCounters()
        counters.cb = ctypes.sizeof(counters)
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        psapi = ctypes.WinDLL("psapi", use_last_error=True)
        kernel32.GetCurrentProcess.restype = ctypes.c_void_p
        psapi.GetProcessMemoryInfo.argtypes = [
            ctypes.c_void_p,
            ctypes.POINTER(ProcessMemoryCounters),
            ctypes.c_ulong,
        ]
        psapi.GetProcessMemoryInfo.restype = ctypes.c_int

        process = kernel32.GetCurrentProcess()
        ok = psapi.GetProcessMemoryInfo(process, ctypes.byref(counters), counters.cb)
        return counters.WorkingSetSize / 1024.0 / 1024.0 if ok else 0.0

    try:
        import resource

        value = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
        if sys.platform == "darwin":
            return value / 1024.0 / 1024.0
        return value / 1024.0
    except Exception:
        return 0.0


def png_chunk(kind, payload):
    body = kind + payload
    crc = binascii.crc32(body) & 0xFFFFFFFF
    return struct.pack(">I", len(payload)) + body + struct.pack(">I", crc)


def save_image(values, path, width, height):
    matrix = np.zeros((height, width))
    flat = matrix.ravel()
    count = min(len(flat), len(values))
    flat[:count] = values[:count]

    # Valor absoluto e transposta da matriz
    matrix = np.abs(matrix).T
    height, width = matrix.shape

    min_value = 0.0
    max_value = float(np.max(matrix))
    if min_value == max_value:
        max_value = min_value + 1.0

    pixels = np.clip((matrix - min_value) / (max_value - min_value) * 255, 0, 255).astype(np.uint8)

    raw = bytearray()
    for row in pixels:
        raw.append(0)
        raw.extend(row.tobytes())

    png = bytearray(b"\x89PNG\r\n\x1a\n")
    png.extend(png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0)))
    png.extend(png_chunk(b"IDAT", zlib.compress(bytes(raw))))
    png.extend(png_chunk(b"IEND", b""))

    with open(path, "wb") as output:
        output.write(png)


# Método auxiliar para CSV
def append_csv(path, metas):
    exists = os.path.exists(path)
    with open(path, "a", newline="", encoding="utf-8") as output:
        writer = csv.writer(output)
        if not exists:
            writer.writerow([
                "timestamp", "algorithm", "method", "elapsed_ms", "cpu_seconds",
                "memory_usage_mb", "image_width", "image_height", "iterations",
                "final_res_norm", "image_path", "seed",
            ])
        for meta in metas:
            writer.writerow([
                time.time_ns(), meta["algorithm"], meta["method"], meta["elapsed_ms"],
                meta["cpu_seconds"], meta["memory_usage_mb"], meta["image_width"], meta["image_height"],
                meta["iterations"], meta["final_res_norm"], meta["image_path"], meta["seed"],
            ])


# Relatório
def append_text_report(path, metas):
    with open(path, "a", encoding="utf-8") as output:
        output.write(f"Seed: {metas[0]['seed']}\n")
        output.write("Resultados Python\n")
        for meta in metas:
            output.write(
                f"- {meta['method']} | tempo_ms={meta['elapsed_ms']} "
                f"| cpu_s={meta['cpu_seconds']:.6f} "
                f"| memoria_mb={meta['memory_usage_mb']:.6f} "
                f"| iteracoes={meta['iterations']} "
                f"| residuo={meta['final_res_norm']:.12g} "
                f"| imagem={meta['image_path']}\n"
            )
        if len(metas) >= 2:
            first, second = metas[0], metas[1]
            output.write("Comparacao simples\n")
            output.write(
                f"- {first['method']} vs {second['method']} | "
                f"tempo_ms={abs(first['elapsed_ms'] - second['elapsed_ms'])} "
                f"| cpu_s={abs(first['cpu_seconds'] - second['cpu_seconds']):.6f} "
                f"| memoria_mb={abs(first['memory_usage_mb'] - second['memory_usage_mb']):.6f} "
                f"| iteracoes={abs(first['iterations'] - second['iterations'])} "
                f"| residuo={abs(first['final_res_norm'] - second['final_res_norm']):.12g}\n"
            )
        output.write("\n")


# Processamento de reconstrução da imagem
def run_method(method, h, g, params, output_dir, width, height, seed):
    # Variáveis auxiliares para comparação depois
    start_ms = time.time_ns() // 1_000_000
    start_cpu = time.process_time()
    memory_before = memory_mb()

    if method == "CGNE":
        f, iterations, residual = cgne(h, g, params["tol"], params["max_iter"])
    else:
        f, iterations, residual = cgnr(h, g, params["tol"], params["max_iter"])

    # Gerar imagem
    image_path = os.path.join(output_dir, f"img_python_{method}_{time.time_ns()}.png")
    save_image(f, image_path, width, height)
    memory_after = memory_mb()
    end_ms = time.time_ns() // 1_000_000

    # Metadados para comparação e relatório
    meta = {
        "algorithm": "python",
        "method": method,
        "start_iso": iso_now(),
        "end_iso": iso_now(),
        "start_ms": start_ms,
        "end_ms": end_ms,
        "elapsed_ms": end_ms - start_ms,
        "cpu_seconds": time.process_time() - start_cpu,
        "memory_usage_mb": max(0.0, memory_after - memory_before),
        "image_width": width,
        "image_height": height,
        "size_pixels": width * height,
        "iterations": iterations,
        "final_res_norm": residual,
        "image_path": image_path,
        "seed": seed,
    }

    # Atualiza o arquivo JSON com os metadados da execução
    with open(image_path + ".json", "w", encoding="utf-8") as output:
        json.dump(meta, output, ensure_ascii=True)
        output.write("\n")

    return meta


# Processamento dados do cliente
def reconstruct_payload(payload, output_dir, csv_path=None):
    # Dados
    g = np.asarray(payload["g"], dtype=float)
    h = np.asarray(payload["H"], dtype=float)

    if h.ndim != 2:
        raise ValueError("H precisa ser uma matriz")
    if len(g) != h.shape[0]:
        raise ValueError("g precisa ter a mesma quantidade de valores que as linhas de H")

    params = payload.get("params", {})
    params = {
        "max_iter": int(params.get("max_iter", 10)),
        "tol": float(params.get("tol", 1e-4)),
    }

    image_size = int(payload.get("image_size", h.shape[1]))
    width = int(payload.get("image_width", int(math.sqrt(image_size))))
    height = int(payload.get("image_height", width))
    seed = int(payload.get("seed", 0))

    # Cria diretorio de saída, caso não exista
    os.makedirs(output_dir, exist_ok=True)
    metas = [
        run_method("CGNE", h, g, params, output_dir, width, height, seed),
        run_method("CGNR", h, g, params, output_dir, width, height, seed),
    ]

    # Relatório de comparação em CSV e TXT
    if csv_path:
        append_csv(csv_path, metas)
    append_text_report(os.path.join(output_dir, "report_comparison.txt"), metas)

    return metas


# Handler do servidor HTTP Python
class Handler(BaseHTTPRequestHandler):
    output_dir = None
    csv_path = None

    # Único método POST, para receber os dados do cliente e processar a reconstrução da imagem
    def do_POST(self):
        # Verificação simples, apenas para validar requisição
        if self.path != "/reconstruct":
            self.send_response(404)
            self.end_headers()
            self.wfile.write(b"not found\n")
            return

        try:
            length = int(self.headers.get("Content-Length", "0"))
            body = self.rfile.read(length).decode("utf-8-sig")
            metas = reconstruct_payload(json.loads(body), self.output_dir, self.csv_path)
            response = json.dumps(metas, ensure_ascii=True).encode("utf-8")

            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(response)))
            self.end_headers()
            self.wfile.write(response)
        except Exception as err:
            message = f"bad request: {err}\n".encode("utf-8")
            self.send_response(400)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(message)))
            self.end_headers()
            self.wfile.write(message)


# Inicia o servidor HTTP Python
def run_server(host, port, output_dir):
    # Caminho relatório CSV
    csv_path = os.path.join(output_dir, "report_comparison_python.csv")

    # Inicia Handler
    Handler.output_dir = output_dir
    Handler.csv_path = csv_path

    # Cria diretorio, caso não exista
    os.makedirs(output_dir, exist_ok=True)
    # Instancia do servidor
    server = HTTPServer((host, port), Handler)
    print(f"Servidor ligado na porta {host}:{port}")
    server.serve_forever()
