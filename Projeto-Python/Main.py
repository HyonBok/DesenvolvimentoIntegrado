import argparse

from Client import run_client
from Server import run_server


def main():
    parser = argparse.ArgumentParser(description="Reconstrucao de imagens em Python.")
    parser.add_argument("--server", action="store_true", help="inicia o servidor HTTP Python")
    parser.add_argument("--client", action="store_true", help="executa o cliente HTTP Python")
    parser.add_argument("--output-dir", default="output", help="pasta de saida")
    parser.add_argument("--host", default="localhost", help="host do servidor")
    parser.add_argument("--port", type=int, default=8081, help="porta do servidor")
    args = parser.parse_args()

    if args.server:
        run_server(args.host, args.port, args.output_dir)
    elif args.client:
        run_client(args.host, args.port)
    else:
        print("Use --server ou --client.")


if __name__ == "__main__":
    main()
