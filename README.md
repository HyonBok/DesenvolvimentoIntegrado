# Projeto para Desenvolvimento Integrado de Sistemas

Projeto de reconstrucao de imagens usando CGNE e CGNR em duas linguagens:

- C++: linguagem compilada e fortemente tipada.
- Python: linguagem interpretada e nao fortemente tipada.

O projeto usa os dados de exemplo da pasta `Dados`, principalmente o Modelo 2 (`30x30`). Os clientes C++ e Python usam o mesmo arquivo base `Dados/base_config.txt`, que guarda a seed, a matriz `H`, a sequencia de sinais `g`, os intervalos entre envios e os parametros dos algoritmos.

Se `Dados/base_config.txt` nao existir, o cliente C++ ou o cliente Python cria automaticamente um arquivo base com os dados do Modelo 2.

## Estrutura

```text
.
|-- Dados/
|   |-- base_config.txt
|   |-- Dados Modelo 1/
|   `-- Dados Modelo 2/
|-- Projeto-Python/
|   |-- Main.py
|   |-- Server.py
|   `-- Client.py
`-- Projeto-Cpp/
    |-- client/
    |   |-- client.cpp
    |   `-- Makefile
    `-- server/
        |-- server.cpp
        |-- recon.cpp
        |-- recon.hpp
        |-- image_utils.cpp
        |-- image_utils.hpp
        `-- Makefile
```

## Arquivo Base

Exemplo de `Dados/base_config.txt`:

```text
seed=1780691941587657800
matrix=Dados/Dados Modelo 2/H-2.csv/H-2.csv
image_width=30
image_height=30
max_iter=10
tol=0.0001
signal=Dados/Dados Modelo 2/g-30x30-1.csv,delay_ms=0
signal=Dados/Dados Modelo 2/g-30x30-2.csv,delay_ms=307
```

Esse arquivo garante que os fluxos C++ e Python usem a mesma seed, a mesma matriz `H` e a mesma sequencia de sinais.

## Requisitos

### Python

- Python 3.10 ou superior
- NumPy

```bash
pip install numpy
```

### C++

- Compilador C++17
- Windows: `ws2_32` e `psapi`
- Linux/macOS: sockets POSIX

## Executar Python

O arquivo principal do Python e `Main.py`.

Servidor Python:

```bash
python Projeto-Python/Main.py --server
```

Cliente Python, em outro terminal:

```bash
python Projeto-Python/Main.py --client
```

O servidor Python atende `POST /reconstruct`, executa CGNE e CGNR em Python, gera imagens, metadados e relatorio.

## Executar C++

Compile e execute servidor:

```bash
cd Projeto-Cpp/server
make run build
```

Compile e execute o cliente:

```bash
cd Projeto-Cpp/client
make run build
```

O servidor C++ executa CGNE e CGNR em C++.

## Entrada HTTP

Formato esperado:

```json
{
  "g": [1.0, 2.0, 3.0],
  "H": [
    [1.0, 0.0],
    [0.0, 1.0],
    [1.0, 1.0]
  ],
  "params": {
    "max_iter": 10,
    "tol": 0.0001
  },
  "image_size": 4,
  "image_width": 2,
  "image_height": 2,
  "seed": 123456
}
```

Nos dados do Modelo 2, `H` e retangular (`27904 x 900`) e a imagem reconstruida e `30 x 30`.

## Saidas

Cada servidor gera uma pasta de saida no diretorio em que foi executado.

Arquivos principais:

- `img_cpp_CGNE_*.png` e `img_cpp_CGNR_*.png`: imagens C++.
- `img_python_CGNE_*.png` e `img_python_CGNR_*.png`: imagens Python.
- `*.png.json`: metadados de cada imagem.
- `report_comparison.csv`: relatorio em CSV com tempo, CPU, memoria, iteracoes, residuo e caminho da imagem.
- `report_comparison.txt`: relatorio textual simples para comparar os resultados.

## Criterios de Parada

Os algoritmos param quando:

- a variacao do residuo fica abaixo de `tol`; ou
- o numero de iteracoes chega a `max_iter`.

No arquivo base padrao:

```json
{
  "max_iter": 10,
  "tol": 0.0001
}
```
