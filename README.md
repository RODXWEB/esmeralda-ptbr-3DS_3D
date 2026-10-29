# Pokémon Esmeralda PT-BR — Emerald3DS

Tradução em Português do Brasil **completa** aplicada ao port nativo para
Nintendo 3DS **[Emerald 3DS / Pokémon Emerald 3Ds Dual Screen](https://github.com/ZallaxDev/emerald-3ds)**
(tela dupla, interface de toque na tela de baixo e mundo voxel 3D opcional).

Esta árvore já está localizada para PT-BR e pronta para compilar. Não contém
ROM nem save.

## Origem

| Parte | Origem |
| :--- | :--- |
| Motor (decomp) | [pret/pokeemerald](https://github.com/pret/pokeemerald) no commit `76463dac` |
| Port 3DS | [ZallaxDev/emerald-3ds](https://github.com/ZallaxDev/emerald-3ds), branch `dev` (sync de 2026-10-29) |
| Tradução | Carlos Mozart (AllGenWiki), ~14.300 textos, aplicada com `--port-lang pt_br` |

A tradução inclui diálogos de todos os mapas, treinadores, batalhas, menus,
POKéDEX, PC, lojas, POKéNAV, concursos, Fronteira de Batalha, FALA FÁCIL e a
interface da tela de baixo (MAPA, OPÇÕES, MELHORIAS, TRAPAÇAS).

## Onde ficam os textos

- `tools/locales/pt_br/` — catálogos por arquivo de origem
  (`data/text/*.inc.txt`, `data/maps/**/scripts.inc.txt`,
  `data/scripts/*`, `data/event_scripts.s.txt`, `src/strings.c.txt`,
  `src/data/...`). Cada entrada é `[label] <hash>` + texto.
- `tools/localize_portuguese.py` aplica os catálogos na árvore.
- Interface do port: `3ds_port/src/3ds_bottom_ui.c` via
  `CTR_TEXT(...)`/`CTR_TEXT_PT(...)` de `3ds_port/include/3ds_locale.h`.
- Gráficos com palavras: `3ds_port/src/3ds_icons_ptbr.c`.
- Patches de suporte: `patches/pokeemerald/0057`–`0065` (til, ordem nas
  batalhas, nomes de tipo etc.).

## Instalar no 3DS

Requisitos: 3DS/2DS com CFW (Luma3DS) e Homebrew Launcher.

1. Copie para o cartão SD:

   ```text
   SD:/3ds/emerald3ds/Emerald3DS.3dsx
   SD:/3ds/emerald3ds/Emerald3DS.smdh
   SD:/3ds/emerald3ds/emerald3ds.pak
   ```

2. Garanta `SD:/3ds/dspfirm.cdc` (sem ele o jogo fica sem som).
3. Abra o Homebrew Launcher e inicie **Emerald 3DS**.

Save: `SD:/3ds/emerald3ds/emerald3ds.sav` (compatível entre versões).

Para um ícone na tela inicial, instale `Emerald3DS-Forwarder.cia` com o FBI;
ele abre `SD:/3ds/emerald3ds/Emerald3DS.3dsx`.

## Compilar

Requisitos:

- devkitPro com devkitARM, libctru, citro2d, citro3d e 3dstools
  (`pacman -S --needed libctru citro2d citro3d 3dstools`)
- compilador C/C++ no host (`gcc`/`g++`)
- Python 3.11+ com Pillow
- `libpng-dev` e `zlib1g-dev`

Na raiz desta árvore:

```sh
make tools
make generated
make -C 3ds_port -j8 PORT_LANG=pt_br
```

Release (executável sem dados embutidos + PAK):

```sh
make -C 3ds_port release pak -j8 PORT_LANG=pt_br
```

Saídas:

```text
3ds_port/dist/Emerald3DS.3dsx
3ds_port/dist/Emerald3DS.smdh
3ds_port/build/emerald3ds.pak
```

Sempre distribua o `.3dsx` e o `.pak` do mesmo build.

## Correções presentes nesta versão

- POKéDEX: a tela de registro de um POKéMON novo é composta como tela GBA
  centralizada (VBlank próprio), sem cortar a descrição
  (`src/pokedex.c`).
- Golpes de campo em tela toda (SURF, CUT, FLY): o banner é desenhado na
  largura toda de 400 px (`src/field_effect.c`,
  `CtrVideo_SetFieldBanner`).
- Cura no CENTRO POKéMON: os sprites da máquina seguem o mapa/câmera
  (`3ds_port/src/3ds_video.c`, `PlaceScreenObject`).

## Créditos e aviso legal

- Tradução PT-BR: Carlos Mozart (AllGenWiki).
- Port 3DS: Daniel Cazalla / [ZallaxDev](https://github.com/ZallaxDev/emerald-3ds).
- Decomplicação: [pret](https://github.com/pret/pokeemerald).

Projeto de fãs, sem vínculo com Nintendo, Game Freak, Creatures ou The Pokémon
Company. Use uma cópia legítima da sua própria ROM.
