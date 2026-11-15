# OpenUT99-Plus

<p align="center"><img src="docs/banner.svg" alt="OpenUT99-Plus" width="100%"></p>

![Status: research](https://img.shields.io/badge/status-research-ff9b5a)
![License: AGPL-3.0](https://img.shields.io/badge/license-AGPL--3.0-1c2a44)
![Platform: Linux](https://img.shields.io/badge/platform-Linux-7d8494)
![C++17 | OpenGL 3.3](https://img.shields.io/badge/C%2B%2B17-OpenGL%203.3-7d8494)
![Unreal Tournament](https://img.shields.io/badge/Unreal-Tournament-e2502a)
[![Docs](https://img.shields.io/badge/docs-online-6fb6ff)](https://opengameplus.github.io/OpenUT99-Plus/)

**Español** · [English](README.md) · [Português](README.pt.md) · [Français](README.fr.md) · [Deutsch](README.de.md) · [Русский](README.ru.md)

**Unreal Tournament en un motor nuevo.** OpenUT99-Plus es una reimplementación independiente de Unreal Engine 1, el motor de Unreal Tournament (1999), escrita desde cero. Lee los archivos del propio juego desde tu instalación y los ejecuta en sistemas modernos, incluido el código UnrealScript del juego. El objetivo a largo plazo es jugar con y contra el juego original: clientes originales en nuestro servidor, y nuestro cliente en servidores originales.

<table>
  <tr>
    <td><img src="docs/images/curse.jpg" alt="Un bot corriendo por el castillo de DM-Curse]["></td>
    <td><img src="docs/images/coret.jpg" alt="Un pasillo de CTF-Coret"></td>
  </tr>
  <tr>
    <td colspan="2"><img src="docs/images/morpheus.jpg" alt="Un bot en las azoteas de DM-Morpheus bajo el cielo estrellado"></td>
  </tr>
</table>
<p align="center"><sub>DM-Curse][, CTF-Coret y DM-Morpheus, dibujados por OpenUT99-Plus desde los archivos del juego mientras los bots juegan la partida.</sub></p>

## Qué funciona hoy

- **Cada objeto de la instalación se decodifica byte a byte**: los 396 paquetes, con sus mapas, texturas, sonidos, música, fuentes, mallas, animaciones y el bytecode de UnrealScript, leídos por las herramientas Python y contrastados con el motor C++.
- **Los 83 mapas cargan y se dibujan**: BSP con texturas, lightmaps horneados, cielos, decoraciones, objetos recogibles y personajes animados.
- **Corre el propio UnrealScript del juego**: nuestra máquina virtual ejecuta las 1.604 clases del juego. Inicia un Deathmatch en cada mapa sin ningún error de script. Los bots recorren la red de caminos, abren puertas, suben a ascensores, recogen armas y objetos, pelean entre ellos, mueren, puntúan y reaparecen.
- **Física medida contra el original**: nuestros propios scripts de sonda corren en el servidor dedicado original y registran exactamente cómo se mueven los pawns ([ADR 0008](docs/architecture/adr/0008-probe-the-original.md)). Escalones, rampas caminables, deslizamiento en pendientes empinadas, el límite de pared y el salto a salientes coinciden con él; el inicio de un deslizamiento todavía no.

> [!NOTE]
> **Etapa de investigación.** Puedes jugar un Deathmatch contra los bots, volar por cualquier mapa o ver jugar a los bots. Aún no hay sonido, HUD, arma en la mano, menús ni red, y buena parte de la física y la IA sigue siendo provisional ([deuda conocida](docs/roadmap/debt.md)).

> [!IMPORTANT]
> **No se incluye contenido del juego: trae tus propios datos.** Este repositorio es solo código fuente: sin paquetes, mapas, texturas, sonidos, música ni ejecutables. El motor los lee de una copia de Unreal Tournament que **posees legalmente**, llamada aquí `<UT99_DIR>`. Se probó con la versión de Steam y el parche 469f de [OldUnreal](https://www.oldunreal.com/).

## Cómo probarlo

Aún no hay compilaciones listas para usar. Compila el motor (ver [Compilar](#compilar)) y apúntalo a tu carpeta de Unreal Tournament, la que tiene `Maps/` y `System/`:

```sh
build/ut_mapview "<UT99_DIR>" DM-Deck16][                    # volar por un mapa
build/ut_mapview "<UT99_DIR>" DM-Morpheus --play --follow    # ver jugar a los bots, siguiendo a uno
build/ut_mapview "<UT99_DIR>" DM-Deck16][ --player --mouse-free   # jugar un Deathmatch contra los bots
build/ut_script "<UT99_DIR>" --map DM-Codex 60               # sin ventana: un minuto de partida, impreso
```

| Controles | |
|---|---|
| Mirar | mantener un botón del ratón y arrastrar, o las flechas |
| Volar | WASD, shift va más rápido |
| Jugando (`--player`) | WASD mueve, el ratón mira, botón izquierdo / derecho disparo / disparo alternativo, Espacio salta, Ctrl agacha, 1-0 armas |
| Viendo una partida | F sigue al siguiente bot; Esc sale |

## Hoja de ruta

El orden y los criterios de salida están en [docs/roadmap/roadmap.md](docs/roadmap/roadmap.md):

1. **Paquetes y objetos** (hecho): cada objeto de cada paquete leído sin que sobre un byte.
2. **Visor de niveles** (hecho): cada mapa del juego en el motor C++.
3. **Jugabilidad** (en curso): actores, física, armas, bots y modos de juego, hasta un Deathmatch contra bots.
4. **Fidelidad**: comparación lado a lado con el original.
5. **Red**: un cliente original entra a nuestro servidor, y nuestro cliente entra a un servidor original.

## Para colaboradores

Toda ayuda cuenta: probarlo y reportar lo que está mal, compararlo con el juego original, o código. La documentación técnica, en inglés, empieza en [docs/README.md](docs/README.md): formatos de archivo, decisiones de arquitectura y deuda conocida. [AGENTS.md](AGENTS.md) es el punto de entrada para agentes de programación con IA.

## Compilar

C++17, CMake, SDL2 y OpenGL 3.3 (libepoxy). En Debian o Ubuntu:

```sh
sudo apt install cmake g++ git pkg-config libsdl2-dev libepoxy-dev
cmake -S . -B build && cmake --build build -j
ctest --test-dir build
# -> build/ut_mapview (ventana), build/ut_script (sin ventana), build/ut_dump (inspector de paquetes)
```

<details>
<summary><b>Herramientas Python</b></summary>

Lectores de los formatos del juego, solo con la biblioteca estándar de Python 3.

```sh
python3 tools/upkg.py "<UT99_DIR>/Maps/DM-Deck16][.unr"        # cabecera, nombres, imports, exports
python3 tools/uprops.py --sweep "<UT99_DIR>"                     # lee cada objeto, cuentas por clase
python3 -m unittest discover -s tools                            # tests
python3 tools/check_docs.py                                      # comprobación de la documentación
```

</details>

<details>
<summary><b>Qué hay dónde</b></summary>

| Ruta | Contenido |
|---|---|
| `engine` | El motor C++: paquetes, niveles, colisión, la VM de UnrealScript, el mundo (física, IA, movers), el renderizador y los ejecutables |
| `tools` | Lectores Python de los formatos, la sonda que mide el original, el comprobador de documentación y sus tests |
| `docs/formats` | Las especificaciones de los formatos de archivo |
| `docs/architecture` | Capas, límites y las decisiones de arquitectura (ADR) |
| `docs/roadmap` | La hoja de ruta y la deuda conocida |
| `docs/ai` | La capa de memoria para agentes de IA |

</details>

## Transparencia

- **Quién lo mantiene:** OpenGamePlus. Me gustan los juegos antiguos y quiero seguir jugando Unreal Tournament con amigos en los sistemas de hoy, con un motor abierto que la comunidad pueda mantener vivo.
- **Cómo se trabaja:** trabajo con [Claude Code](https://claude.com/claude-code), el asistente de programación de Anthropic. El código y la documentación, incluidos este README y sus traducciones, se escriben con su ayuda. Los commits hechos así llevan una línea `Co-Authored-By: Claude` en su mensaje, así que se pueden identificar en el historial con `git log`.
- **Código original:** no se copia código del motor original en este repositorio, ni se desensamblan sus binarios nativos. El motor se escribe a partir de los archivos de datos del juego, del propio UnrealScript del juego (leído para entender el comportamiento, nunca copiado), de observar el juego original en marcha y de documentación pública ([ADR 0004](docs/architecture/adr/0004-clean-room-sources.md)).
- **Qué no es:** no es un proyecto oficial ni está afiliado a Epic Games. No contiene archivos del juego.

## Aviso

Esta es una reimplementación independiente de Unreal Engine 1. No está afiliada, autorizada, patrocinada ni respaldada por Epic Games, Inc. ni por ninguna de sus filiales.

"Unreal" y "Unreal Tournament" son marcas registradas de Epic Games, Inc. Se usan en este repositorio solo para identificación e interoperabilidad, para describir lo que ejecuta el motor. No se reclama ningún derecho sobre esas marcas.

Este proyecto no proporciona ni ayuda a obtener datos del juego protegidos por derechos de autor. Para usarlo con contenido real del juego, debes aportar los archivos de datos de una copia de Unreal Tournament que poseas legalmente.

Si eres titular de derechos y crees que algo de aquí no debería distribuirse, abre un issue o contacta al mantenedor y se atenderá con prontitud.

Las capturas muestran contenido de Unreal Tournament, propiedad de Epic Games, Inc., solo con fines de identificación.

## Licencia

[GNU Affero General Public License v3.0](LICENSE). Quien distribuya el motor, o ejecute una versión modificada como servidor de red para otros, debe ofrecer su código fuente bajo la misma licencia. Las bibliotecas de terceros conservan las suyas: SDL2 (zlib) y libepoxy (MIT).

## Créditos

- [SDL](https://www.libsdl.org/) y [libepoxy](https://github.com/anholt/libepoxy)
- [OldUnreal](https://www.oldunreal.com/), por mantener vivo Unreal Tournament con sus parches
- [ut99-mapgen](https://github.com/monteslu/ut99-mapgen) (MIT), usado para construir los mapas de calibración
- Epic Games, por el juego
