import json
import os
import random
import time
import urllib.request


DEFAULT_MATRIX = "Dados/Dados Modelo 2/H-2.csv/H-2.csv"
DEFAULT_SIGNALS = [
    "Dados/Dados Modelo 2/g-30x30-1.csv",
    "Dados/Dados Modelo 2/g-30x30-2.csv",
]


# Função auxiliar, para encontrar os caminhos dos arquivos
def find_from_parents(relative):
    base = os.getcwd()
    while True:
        candidate = os.path.join(base, relative)
        if os.path.exists(candidate):
            return candidate

        parent = os.path.dirname(base)
        if parent == base:
            break
        base = parent

    raise FileNotFoundError(f"arquivo nao encontrado: {relative}")


# Caminho da pasta Dados
def base_config_path():
    return os.path.join(find_from_parents("Dados"), "base_config.txt")


# Cria o arquivo base_config.txt com configurações padrão caso não exista
def create_base_config(path):
    seed = time.time_ns()
    rng = random.Random(seed)

    with open(path, "w", encoding="utf-8") as output:
        output.write(f"seed={seed}\n")
        output.write(f"matrix={DEFAULT_MATRIX}\n")
        output.write("image_width=30\n")
        output.write("image_height=30\n")
        output.write("max_iter=10\n")
        output.write("tol=0.0001\n")
        output.write(f"signal={DEFAULT_SIGNALS[0]},delay_ms=0\n")
        output.write(f"signal={DEFAULT_SIGNALS[1]},delay_ms={rng.randint(250, 1000)}\n")


def parse_base_config():
    # Pega o caminho do arquivo
    path = base_config_path()
    # Caso não exista, cria o arquivo
    if not os.path.exists(path):
        create_base_config(path)

    config = {
        "seed": 0,
        "matrix": "",
        "image_width": 30,
        "image_height": 30,
        "max_iter": 10,
        "tol": 1e-4,
        "signals": [],
        "path": path,
    }

    # Lê o arquivo
    with open(path, "r", encoding="utf-8-sig") as input_file:
        for raw_line in input_file:
            line = raw_line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue

            key, value = [part.strip() for part in line.split("=", 1)]
            if key == "seed":
                config["seed"] = int(value)
            elif key == "matrix":
                config["matrix"] = value
            elif key == "image_width":
                config["image_width"] = int(value)
            elif key == "image_height":
                config["image_height"] = int(value)
            elif key == "max_iter":
                config["max_iter"] = int(value)
            elif key == "tol":
                config["tol"] = float(value)
            elif key == "signal":
                config["signals"].append(parse_signal(value))

    if not config["seed"] or not config["matrix"] or not config["signals"]:
        raise ValueError(f"arquivo base incompleto: {path}")

    return config


# Formata sinal lido do arquivo base_config.txt
def parse_signal(value):
    path = value
    delay_ms = 0

    # Primeiro tem o caminho do sinal, depois pode ter uma configuração extra (delay_ms) separada por vírgula
    if "," in value:
        path, extra = value.split(",", 1)
        extra = extra.strip()
        if extra.startswith("delay_ms="):
            delay_ms = int(extra.split("=", 1)[1])

    return {"path": path.strip(), "delay_ms": delay_ms}


# Formata numeros vindos do csv
def parse_csv_numbers(line):
    return [float(cell) for cell in line.replace(";", ",").split(",") if cell.strip()]


# Leitura vetor csv(linha)
def read_vector_csv(path):
    values = []
    with open(path, "r", encoding="utf-8-sig") as input_file:
        for line in input_file:
            values.extend(parse_csv_numbers(line))
    return values


# Leitura da matriz csv
def read_matrix_csv(path):
    matrix = []
    columns = 0

    # Leitura do arquivo
    with open(path, "r", encoding="utf-8-sig") as input_file:
        for line in input_file:
            row = parse_csv_numbers(line)
            if not row:
                continue

            if columns == 0:
                columns = len(row)
            elif len(row) != columns:
                raise ValueError(f"matriz com colunas inconsistentes: {path}")

            matrix.append(row)

    return matrix


# Formato da resposta em JSON
def build_payload(config, h, g, signal_path):
    return {
        "g": g,
        "H": h,
        "params": {
            "max_iter": config["max_iter"],
            "tol": config["tol"],
        },
        "image_size": config["image_width"] * config["image_height"],
        "image_width": config["image_width"],
        "image_height": config["image_height"],
        "seed": config["seed"],
        "meta": {
            "signal_path": signal_path,
            "matrix_path": config["matrix"],
            "base_config": config["path"],
        },
    }


# Método post para enviar ao servidor
def post_json(host, port, path, payload):
    body = json.dumps(payload, ensure_ascii=True).encode("utf-8")
    request = urllib.request.Request(
        f"http://{host}:{port}{path}",
        data=body,
        headers={"Content-Type": "application/json"},
        method="POST",
    )

    with urllib.request.urlopen(request, timeout=None) as response:
        return response.read().decode("utf-8")


def run_client(host, port):
    # Pega as configurações do arquivo base_config.txt. Nela está a seed que também será usada no projeto C++
    config = parse_base_config()
    h_path = find_from_parents(config["matrix"])
    h = read_matrix_csv(h_path)

    # Printa insformações no terminal/console
    print(f"Arquivo base: {config['path']}")
    print(f"Seed: {config['seed']}")
    print(f"Matriz H: {h_path}")

    for signal in config["signals"]:
        g_path = find_from_parents(signal["path"])
        g = read_vector_csv(g_path)

        if len(g) != len(h):
            raise ValueError(f"g tem {len(g)} valores, mas H tem {len(h)} linhas")

        if signal["delay_ms"] > 0:
            time.sleep(signal["delay_ms"] / 1000.0)

        # Cria a resposta
        payload = build_payload(config, h, g, signal["path"])
        # Envia a resposta em formato JSON
        reply = post_json(host, port, "/reconstruct", payload)
        print(f"Resposta para {os.path.basename(g_path)}: {reply}")
