# Desenvolvimento Integrado - Reconstrucao de Imagens

Este repositorio contem prototipos em Python e C++ para reconstrucao de imagens usando metodos de gradiente conjugado sobre equacoes normais. A especificacao pede uma linguagem interpretada e nao fortemente tipada, Python, e uma linguagem compilada e fortemente tipada, C++.

O fluxo atual recebe sinais `g`, usa uma matriz de modelo `H` vinda da pasta `Dados` e reconstrui uma imagem `f` usando CGNE e CGNR. O servidor compara a execucao em C++ e em Python com o mesmo payload.

## Estrutura do Repositorio

```text
.
|-- Dados/
|   |-- Dados Modelo 1/
|   `-- Dados Modelo 2/
|-- Projeto-Python/
|   `-- Projeto.py
`-- Projeto-Cpp/
    |-- CMakeLists.txt
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

## Estado Atual

O projeto em Python implementa `CGNE` e `CGNR`, recebe o mesmo JSON usado pelo servidor, gera imagens PNG, grava metadados JSON e anexa suas medicoes ao CSV comparativo.

O projeto em C++ possui:

- servidor HTTP simples na porta `8080`;
- endpoint `POST /reconstruct`;
- implementacoes de `CGNE_Cpp` e `CGNR_Cpp`;
- cliente que carrega `H` e sinais `g` da pasta `Dados`;
- envio de uma sequencia de sinais em intervalos aleatorios;
- geracao de imagens PNG em escala de cinza;
- metadados em JSON por imagem;
- relatorio comparativo em CSV para C++ e Python;
- medicao de tempo decorrido, tempo de CPU, memoria, iteracoes e residuo final.

O servidor C++ aciona a implementacao Python por subprocesso para comparar a versao compilada com a versao interpretada usando exatamente os mesmos dados. Se o executavel `python` nao estiver no `PATH`, defina a variavel de ambiente `PYTHON` com o caminho do interpretador. No Windows, o servidor tambem tenta usar `py -3` como alternativa.

## Requisitos

### Python

- Python 3.10 ou superior
- NumPy

Instalacao do NumPy, se necessario:

```bash
pip install numpy
```

### C++

- Compilador C++ com suporte a C++17
- CMake 3.16 ou superior, ou `make`/`mingw32-make`
- Windows: bibliotecas `ws2_32` e `psapi`, ja configuradas no CMake e nos Makefiles
- Linux/macOS: sockets POSIX padrao

## Como Executar o Projeto Python

Na raiz do repositorio:

```bash
python Projeto-Python/Projeto.py
```

Resposta esperada no modo demonstracao:

```text
Solution: [ 2. -2.]
Iterations: 3
Final residual: 4.559679095398286e-16
```

Para executar o Python com o mesmo JSON recebido pelo servidor:

```bash
python Projeto-Python/Projeto.py --input payload.json --output-dir output --append-csv output/report_comparison.csv
```

## Como Compilar o Projeto C++

### Usando CMake

```bash
cd Projeto-Cpp
cmake -S . -B build-local
cmake --build build-local
```

Se o CMake nao escolher um gerador automaticamente, use Ninja:

```bash
cd Projeto-Cpp
cmake -S . -B build-local -G Ninja
cmake --build build-local
```

Dependendo do gerador do CMake, os executaveis ficam diretamente em `build-local/` ou em uma subpasta de configuracao, como `build-local/Debug/` ou `build-local/Release/`.

## Como Executar o Fluxo Cliente-Servidor

1. Inicie o servidor em um terminal:

```bash
cd Projeto-Cpp/server
make run
```

Ou execute o binario gerado pelo CMake:

```bash
cd Projeto-Cpp
./build-local/recon_server
```

No Windows, o comando pode ser:

```powershell
cd Projeto-Cpp
.\build-local\Debug\recon_server.exe
```

2. Em outro terminal, execute o cliente:

```bash
cd Projeto-Cpp/client
make run
```

Ou execute o binario gerado pelo CMake:

```bash
cd Projeto-Cpp
./build-local/recon_client
```

No Windows, o comando pode ser:

```powershell
cd Projeto-Cpp
.\build-local\Debug\recon_client.exe
```

O cliente usa por padrao os dados do Modelo 2:

- `Dados/Dados Modelo 2/H-2.csv/H-2.csv`
- `Dados/Dados Modelo 2/g-30x30-1.csv`
- `Dados/Dados Modelo 2/g-30x30-2.csv`

Ele envia os sinais em sequencia, com intervalo aleatorio entre as requisicoes, e registra a ordem em `signal_sequence_<seed>.txt`.

## Entrada Esperada pelo Servidor

O servidor espera uma requisicao HTTP:

```http
POST /reconstruct
Content-Type: application/json
```

Com corpo JSON neste formato:

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
  "image_size": 2,
  "seed": 123456
}
```

Campos:

- `g`: vetor de sinal.
- `H`: matriz de modelo. Ela pode ser retangular; no Modelo 2, `H` tem `27904 x 900`.
- `params.max_iter`: limite de iteracoes. A especificacao pede ate 10 iteracoes.
- `params.tol`: tolerancia de parada. A especificacao pede erro menor que `1e-4`.
- `image_size`: quantidade de pixels usada para salvar a imagem reconstruida. No Modelo 2, o valor e `900`, gerando uma imagem `30 x 30`.
- `seed`: semente usada pelo cliente para rastreabilidade dos intervalos aleatorios.

## Resposta Esperada do Servidor

O servidor retorna um JSON com um item para cada algoritmo executado. Em uma requisicao normal, sao retornados quatro itens: `CGNE` e `CGNR` em C++, mais `CGNE` e `CGNR` em Python.

Exemplo de item:

```json
{
  "algorithm": "cpp",
  "method": "CGNE",
  "start_iso": "2026-05-30T12:00:00Z",
  "end_iso": "2026-05-30T12:00:01Z",
  "start_ms": 1780000000000,
  "end_ms": 1780000000100,
  "elapsed_ms": 100,
  "cpu_seconds": 0.08,
  "memory_mb": 180.5,
  "size_pixels": 900,
  "iterations": 10,
  "final_res_norm": 123.45,
  "image_path": "output/img_cpp_CGNE_1780000000000000000.png"
}
```

## Arquivos Gerados

Ao processar uma requisicao, o servidor cria a pasta `output/` dentro do diretorio em que ele foi executado.

Arquivos esperados:

- `output/img_cpp_CGNE_*.png`: imagem reconstruida pelo CGNE em C++.
- `output/img_cpp_CGNE_*.png.json`: metadados da imagem C++ CGNE.
- `output/img_cpp_CGNR_*.png`: imagem reconstruida pelo CGNR em C++.
- `output/img_cpp_CGNR_*.png.json`: metadados da imagem C++ CGNR.
- `output/img_python_CGNE_*.png`: imagem reconstruida pelo CGNE em Python.
- `output/img_python_CGNE_*.png.json`: metadados da imagem Python CGNE.
- `output/img_python_CGNR_*.png`: imagem reconstruida pelo CGNR em Python.
- `output/img_python_CGNR_*.png.json`: metadados da imagem Python CGNR.
- `output/report_comparison.csv`: relatorio comparativo com tempo, CPU, memoria, iteracoes, residuo final e caminho das imagens.

## Parametros Citados na Especificacao

O PDF tambem define calculos auxiliares:

- Fator de reducao: `C = ||H^T * H||_2`
- Coeficiente de regularizacao: `lambda = max(abs(H^T * g)) * 0.10`
- Erro: `epsilon = ||r_{i+1}||_2 - ||r_i||_2`
- Ganho de sinal: `gamma_l = 100 + (1 / 20) * l * sqrt(l)`

No codigo atual, os sinais `g` sao carregados diretamente dos arquivos de exemplo da pasta `Dados`. Os calculos de `C` e `lambda` ainda aparecem como requisito teorico, mas nao foram incorporados aos algoritmos porque nao sao necessarios para executar CGNE/CGNR com os dados fornecidos.

## Criterios de Parada

Pela especificacao, o servidor deve executar ate que:

- o erro seja menor que `1e-4`; ou
- o numero de iteracoes chegue a `10`.

No C++ e no Python, `max_iter` e `tol` sao recebidos em `params`. O cliente atual envia:

```json
{
  "max_iter": 10,
  "tol": 0.0001
}
```

## Pendencias

- Avaliar se os parametros `C` e `lambda` dos adendos precisam entrar na formulacao final ou apenas na documentacao teorica.
- Melhorar o controle de saturacao se o servidor passar a atender requisicoes em paralelo. Hoje o controle pratico e simples: servidor sequencial e cliente com intervalos aleatorios entre sinais.
- Opcional: permitir escolher Modelo 1 ou Modelo 2 por argumento de linha de comando no cliente.

## Observacoes para a Equipe

- O `.txt` da sequencia aleatoria e gerado pelo cliente como `signal_sequence_<seed>.txt`.
- O servidor compara C++ contra Python usando o mesmo `g`, a mesma `H` e os mesmos parametros.
- O cliente nao gera mais uma matriz `H` quadrada aleatoria de `2048 x 2048`; ele usa os dados de exemplo do Modelo 2.
- Para imagens mais interpretaveis, mantenha `image_size` igual ao numero de colunas de `H` quando esse numero for um quadrado perfeito. No Modelo 2, `900 = 30 x 30`.
