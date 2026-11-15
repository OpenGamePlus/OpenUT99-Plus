# OpenUT99-Plus

<p align="center"><img src="docs/banner.svg" alt="OpenUT99-Plus" width="100%"></p>

![Status: research](https://img.shields.io/badge/status-research-orange)
![License: AGPL-3.0](https://img.shields.io/badge/license-AGPL--3.0-blue)
![Platform: Linux](https://img.shields.io/badge/platform-Linux-lightgrey)
![C++17 | OpenGL 3.3](https://img.shields.io/badge/C%2B%2B17-OpenGL%203.3-lightgrey)
![Unreal Tournament](https://img.shields.io/badge/Unreal-Tournament-6a4fb0)

[Español](README.es.md) · [English](README.md) · **Português** · [Français](README.fr.md) · [Deutsch](README.de.md) · [Русский](README.ru.md)

**Unreal Tournament em um motor novo.** OpenUT99-Plus é uma reimplementação independente do Unreal Engine 1, o motor do Unreal Tournament (1999), escrita do zero. Ela lê os arquivos do próprio jogo a partir da sua instalação e os executa em sistemas modernos, incluindo o código UnrealScript do jogo. O objetivo de longo prazo é jogar com e contra o jogo original: clientes originais no nosso servidor, e o nosso cliente em servidores originais.

<table>
  <tr>
    <td><img src="docs/images/curse.jpg" alt="Um bot correndo pelo castelo de DM-Curse]["></td>
    <td><img src="docs/images/coret.jpg" alt="Um corredor de CTF-Coret"></td>
  </tr>
  <tr>
    <td colspan="2"><img src="docs/images/morpheus.jpg" alt="Um bot nos telhados de DM-Morpheus sob o céu estrelado"></td>
  </tr>
</table>
<p align="center"><sub>DM-Curse][, CTF-Coret e DM-Morpheus, desenhados pelo OpenUT99-Plus a partir dos arquivos do jogo enquanto os bots jogam a partida.</sub></p>

## O que funciona hoje

- **Cada objeto da instalação é decodificado byte a byte**: os 396 pacotes, com seus mapas, texturas, sons, músicas, fontes, malhas, animações e o bytecode UnrealScript, lidos pelas ferramentas Python e conferidos com o motor C++.
- **Os 83 mapas carregam e são desenhados**: BSP texturizado, lightmaps pré-calculados, céus, decorações, itens e personagens animados.
- **O próprio UnrealScript do jogo roda**: nossa máquina virtual executa as 1.604 classes do jogo. Ela inicia um Deathmatch em cada mapa sem nenhum erro de script. Os bots percorrem a rede de caminhos, abrem portas, usam elevadores, pegam armas e itens, lutam entre si, morrem, pontuam e renascem.
- **Física medida contra o original**: nossos próprios scripts de sonda rodam no servidor dedicado original e registram exatamente como os pawns se movem ([ADR 0008](docs/architecture/adr/0008-probe-the-original.md)). Degraus, rampas caminháveis, deslizar em encostas íngremes, o limite de parede e o pulo para saliências coincidem com ele; o início de um deslizamento ainda não.

> [!NOTE]
> **Fase de pesquisa.** Ainda não há controle do jogador: você pode voar por qualquer mapa ou assistir aos bots jogando. Ainda não há som, HUD, menus nem rede, e boa parte da física e da IA ainda é provisória ([dívida conhecida](docs/roadmap/debt.md)).

> [!IMPORTANT]
> **Nenhum conteúdo do jogo é incluído: traga os seus próprios dados.** Este repositório é só código-fonte: sem pacotes, mapas, texturas, sons, músicas ou executáveis. O motor os lê de uma cópia do Unreal Tournament que **você possui legalmente**, chamada aqui de `<UT99_DIR>`. Foi testado com a versão da Steam e o patch 469f da [OldUnreal](https://www.oldunreal.com/).

## Como experimentar

Ainda não há builds prontas para uso. Compile o motor (veja [Compilação](#compilação)) e aponte-o para a sua pasta do Unreal Tournament, a que tem `Maps/` e `System/`:

```sh
build/ut_mapview "<UT99_DIR>" DM-Deck16][                    # voar por um mapa
build/ut_mapview "<UT99_DIR>" DM-Morpheus --play --follow    # assistir aos bots, seguindo um
build/ut_script "<UT99_DIR>" --map DM-Codex 60               # sem janela: um minuto de partida, impresso
```

| Controles | |
|---|---|
| Olhar | segurar um botão do mouse e arrastar, ou as setas |
| Voar | WASD, shift vai mais rápido |
| Assistindo a uma partida | F segue o próximo bot; Esc sai |

## Roteiro

A ordem e os critérios de saída estão em [docs/roadmap/roadmap.md](docs/roadmap/roadmap.md):

1. **Pacotes e objetos** (feito): cada objeto de cada pacote lido sem sobrar um byte.
2. **Visualizador de níveis** (feito): cada mapa do jogo no motor C++.
3. **Jogabilidade** (em andamento): atores, física, armas, bots e modos de jogo, até um Deathmatch contra bots.
4. **Fidelidade**: comparação lado a lado com o original.
5. **Rede**: um cliente original entra no nosso servidor, e o nosso cliente entra em um servidor original.

## Para colaboradores

Toda ajuda conta: experimentar e relatar o que está errado, comparar com o jogo original, ou código. A documentação técnica, em inglês, começa em [docs/README.md](docs/README.md): formatos de arquivo, decisões de arquitetura e dívida conhecida. [AGENTS.md](AGENTS.md) é o ponto de entrada para agentes de programação com IA.

## Compilação

C++17, CMake, SDL2 e OpenGL 3.3 (libepoxy). No Debian ou Ubuntu:

```sh
sudo apt install cmake g++ git pkg-config libsdl2-dev libepoxy-dev
cmake -S . -B build && cmake --build build -j
ctest --test-dir build
# -> build/ut_mapview (janela), build/ut_script (sem janela), build/ut_dump (inspetor de pacotes)
```

<details>
<summary><b>Ferramentas Python</b></summary>

Leitores dos formatos do jogo, apenas com a biblioteca padrão do Python 3.

```sh
python3 tools/upkg.py "<UT99_DIR>/Maps/DM-Deck16][.unr"        # cabeçalho, nomes, imports, exports
python3 tools/uprops.py --sweep "<UT99_DIR>"                     # lê cada objeto, contagens por classe
python3 -m unittest discover -s tools                            # testes
python3 tools/check_docs.py                                      # verificação da documentação
```

</details>

<details>
<summary><b>O que está onde</b></summary>

| Caminho | Conteúdo |
|---|---|
| `engine` | O motor C++: pacotes, níveis, colisão, a VM de UnrealScript, o mundo (física, IA, movers), o renderizador e os executáveis |
| `tools` | Leitores Python dos formatos, a sonda que mede o original, o verificador de documentação e seus testes |
| `docs/formats` | As especificações dos formatos de arquivo |
| `docs/architecture` | Camadas, limites e as decisões de arquitetura (ADRs) |
| `docs/roadmap` | O roteiro e a dívida conhecida |
| `docs/ai` | A camada de memória para agentes de IA |

</details>

## Transparência

- **Quem mantém:** OpenGamePlus. Gosto de jogos antigos e quero continuar jogando Unreal Tournament com amigos nos sistemas de hoje, com um motor aberto que a comunidade possa manter vivo.
- **Como o trabalho é feito:** trabalho com o [Claude Code](https://claude.com/claude-code), o assistente de programação da Anthropic. O código e a documentação, incluindo este README e suas traduções, são escritos com a ajuda dele. Os commits feitos assim levam uma linha `Co-Authored-By: Claude` na mensagem, para que possam ser identificados no histórico com `git log`.
- **Código original:** nenhum código do motor original é copiado para este repositório, e seus binários nativos não são desmontados. O motor é escrito a partir dos arquivos de dados do jogo, do próprio UnrealScript do jogo (lido para entender o comportamento, nunca copiado), da observação do jogo original rodando e de documentação pública ([ADR 0004](docs/architecture/adr/0004-clean-room-sources.md)).
- **O que não é:** não é um projeto oficial e não é afiliado à Epic Games. Não contém arquivos do jogo.

## Aviso

Esta é uma reimplementação independente do Unreal Engine 1. Não é afiliada, autorizada, patrocinada nem endossada pela Epic Games, Inc. ou por qualquer de suas afiliadas.

"Unreal" e "Unreal Tournament" são marcas registradas da Epic Games, Inc. São usadas neste repositório apenas para identificação e interoperabilidade, para descrever o que o motor executa. Nenhum direito sobre essas marcas é reivindicado.

Este projeto não fornece nem ajuda a obter dados de jogo protegidos por direitos autorais. Para usá-lo com conteúdo real do jogo, você deve fornecer os arquivos de dados de uma cópia do Unreal Tournament que possua legalmente.

Se você é titular de direitos e acredita que algo aqui não deveria ser distribuído, abra uma issue ou entre em contato com o mantenedor e isso será tratado prontamente.

As capturas de tela mostram conteúdo do Unreal Tournament, propriedade da Epic Games, Inc., apenas para identificação.

## Licença

[GNU Affero General Public License v3.0](LICENSE). Quem distribuir o motor, ou executar uma versão modificada como servidor de rede para outras pessoas, deve oferecer o código-fonte sob a mesma licença. As bibliotecas de terceiros mantêm as suas: SDL2 (zlib) e libepoxy (MIT).

## Créditos

- [SDL](https://www.libsdl.org/) e [libepoxy](https://github.com/anholt/libepoxy)
- [OldUnreal](https://www.oldunreal.com/), por manter o Unreal Tournament vivo com seus patches
- [ut99-mapgen](https://github.com/monteslu/ut99-mapgen) (MIT), usado para construir os mapas de calibração
- Epic Games, pelo jogo
