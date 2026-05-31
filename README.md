# Desenvolvimento Integrado - Reconstrucao de Imagens

Este repositorio contem prototipos em Python e C++ para um projeto de reconstrucao de imagens usando metodos de gradiente conjugado sobre as equacoes normais. As especificacoes do projeto pedem implementacoes em uma linguagem interpretada e nao fortemente tipada, Python, e em uma linguagem compilada e fortemente tipada, C++.

O foco do projeto e receber sinais `g`, aplicar um modelo `H` e reconstruir uma imagem `f` usando os algoritmos CGNE e CGNR, medindo tempo de execucao, numero de iteracoes e recursos utilizados.

## Estrutura do repositorio

```text
.
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

## Estado atual

O projeto em Python implementa uma funcao `cgnr(A, b, ...)` e executa um teste local pequeno, imprimindo a solucao e o residuo final. Ele ainda nao esta integrado ao fluxo cliente-servidor e ainda nao gera imagens ou relatorios.

O projeto em C++ esta mais avancado. Ele possui:

- um servidor HTTP simples na porta `8080`;
- endpoint `POST /reconstruct`;
- implementacoes de `CGNE_Cpp` e `CGNR_Cpp`;
- cliente que gera `g` e `H` aleatorios e envia ao servidor;
- aplicacao de ganho no vetor `g`;
- geracao de imagens PNG em escala de cinza;
- metadados em JSON por imagem;
- relatorio comparativo em CSV.

Ainda existem requisitos do PDF que nao estao completos, listados na secao "Pendencias".

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

## Como executar o projeto Python

Na raiz do repositorio:

```bash
python Projeto-Python/Projeto.py
```

Resposta esperada no prototipo atual:

```text
Solution: [ 2. -2.]
Final residual: 6.968805455576195e-15
```

Esse resultado vem de uma matriz pequena de exemplo. Ele valida a rotina CGNR, mas ainda nao representa a reconstrucao completa de imagens do projeto.

## Como compilar o projeto C++

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

## Como executar o fluxo cliente-servidor

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
.\build-local\recon_server.exe
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
.\build-local\recon_client.exe
```

## Entrada esperada pelo servidor

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
    [1.0, 0.0, 0.0],
    [0.0, 1.0, 0.0],
    [0.0, 0.0, 1.0]
  ],
  "params": {
    "max_iter": 10,
    "tol": 0.0001
  },
  "image_size": 3,
  "seed": 123456
}
```

Campos:

- `g`: vetor de sinal.
- `H`: matriz de modelo.
- `params.max_iter`: limite de iteracoes. O PDF pede ate 10 iteracoes.
- `params.tol`: tolerancia de parada. O PDF pede erro menor que `1e-4`.
- `image_size`: quantidade de pixels usada para salvar a imagem.
- `seed`: semente usada pelo cliente para rastreabilidade.

Observacao: para gerar uma imagem 2D quadrada, `image_size` deve ser um quadrado perfeito, por exemplo `1024` para `32x32`. Caso contrario, a imagem gerada vira uma faixa de uma linha.

## Resposta esperada do servidor

O servidor retorna um JSON com um item para cada algoritmo executado:

```json
[
  {
    "algorithm": "cpp",
    "method": "CGNE",
    "start_iso": "2026-05-30T12:00:00Z",
    "end_iso": "2026-05-30T12:00:01Z",
    "start_ms": 1780000000000,
    "end_ms": 1780000000100,
    "elapsed_ms": 100,
    "size_pixels": 2048,
    "iterations": 10,
    "final_res_norm": 123.45,
    "image_path": "output/img_cpp_CGNE_1780000000000000000.png"
  },
  {
    "algorithm": "cpp",
    "method": "CGNR",
    "start_iso": "2026-05-30T12:00:01Z",
    "end_iso": "2026-05-30T12:00:02Z",
    "start_ms": 1780000000100,
    "end_ms": 1780000000200,
    "elapsed_ms": 100,
    "size_pixels": 2048,
    "iterations": 10,
    "final_res_norm": 67.89,
    "image_path": "output/img_cpp_CGNR_1780000000100000000.png"
  }
]
```

## Arquivos gerados

Ao processar uma requisicao, o servidor cria a pasta `output/` dentro de `Projeto-Cpp/server/`, quando executado a partir dessa pasta.

Arquivos esperados:

- `output/img_cpp_CGNE_*.png`: imagem reconstruida pelo CGNE.
- `output/img_cpp_CGNE_*.png.json`: metadados da imagem CGNE.
- `output/img_cpp_CGNR_*.png`: imagem reconstruida pelo CGNR.
- `output/img_cpp_CGNR_*.png.json`: metadados da imagem CGNR.
- `output/report_comparison.csv`: relatorio comparativo com tempo, iteracoes, residuo final e caminho das imagens.

## Parametros citados na especificacao

O PDF tambem define calculos auxiliares:

- Fator de reducao: `C = ||H^T * H||_2`
- Coeficiente de regularizacao: `lambda = max(abs(H^T * g)) * 0.10`
- Erro: `epsilon = ||r_{i+1}||_2 - ||r_i||_2`
- Ganho de sinal: `gamma_l = 100 + (1 / 20) * l * sqrt(l)`

No codigo atual, o cliente C++ aplica o ganho de sinal em `g`. Os calculos de `C` e `lambda` ainda aparecem como requisito da especificacao, mas nao foram incorporados aos algoritmos.

## Criterios de parada

Pela especificacao, o servidor deve executar ate que:

- o erro seja menor que `1e-4`; ou
- o numero de iteracoes chegue a `10`.

No C++, `max_iter` e `tol` sao recebidos em `params`. O cliente atual envia:

```json
{
  "max_iter": 10,
  "tol": 0.0001
}
```

## Pendencias

- Integrar a implementacao Python ao fluxo servidor/cliente.
- Implementar tambem CGNE em Python, para comparar Python e C++ como pedido no PDF.
- Gerar relatorio comparativo entre a versao interpretada e a versao compilada.
- Implementar envio de uma sequencia de sinais em intervalos de tempo aleatorios.
- Completar testes de saturacao.
- Medir ocupacao de CPU alem de tempo e memoria.
- Implementar rotina de controle para evitar saturacao do sistema.
- Incorporar, se necessario, os parametros `C` e `lambda` definidos nos adendos.
- Validar os resultados com dados experimentais reais.

## Observacoes para a equipe

- O vetor `g` enviado deve ser o mesmo para os algoritmos comparados.
- O servidor atualmente compara CGNE e CGNR em C++, nao Python contra C++.
- O cliente atual gera uma matriz `H` quadrada aleatoria de tamanho `2048 x 2048`, o que pode consumir bastante memoria.
- Para testes rapidos, reduza temporariamente o valor de `m` em `Projeto-Cpp/client/client.cpp`.
- Para imagens mais interpretaveis, use tamanhos quadrados perfeitos, como `1024`, `4096` ou `16384`.
