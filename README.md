# Pokémon Esmeralda (PT-BR) — Emerald3DS

Tradução em Português do Brasil do Pokémon Esmeralda aplicada ao port nativo
para Nintendo 3DS **[Emerald 3DS / Pokémon Emerald 3Ds Dual Screen](https://github.com/ZallaxDev/emerald-3ds)** (v0.4.0):
tela dupla, interface de toque na tela de baixo e mundo voxel 3D opcional.

Este repositório é a árvore completa já mesclada: decomplicação do jogo +
port 3DS + tradução. Não contém ROM nem save.

## Origem do código

| Parte | Origem |
| :--- | :--- |
| Motor (decomp) | [pret/pokeemerald](https://github.com/pret/pokeemerald) no commit `76463dac` |
| Port 3DS | [ZallaxDev/emerald-3ds](https://github.com/ZallaxDev/emerald-3ds) v0.4.0 (`patches/pokeemerald/0001`–`0056`) |
| Tradução | [lucmsilva651/esmeralda-ptbr](https://github.com/lucmsilva651/esmeralda-ptbr) no commit `16899178` |

O merge que juntou os dois está no commit `389957db`.

## O que foi alterado

- **Textos do jogo em PT-BR** vindos da tradução do lucmsilva: diálogos de
  mapas (`data/maps/*/scripts.inc`), textos gerais (`data/text/`,
  `data/scripts/`, `data/event_scripts.s`), menus e mensagens do motor
  (`src/strings.c`), FALA FÁCIL/ritmo (`src/data/easy_chat`,
  `src/data/bard_music`) e a arte do título
  (`graphics/title_screen/emerald_version.png`).
- **Conflito resolvido** apenas em `src/strings.c`: foram mantidas as strings
  PT-BR; as condicionais do port para o espanhol continuam existindo para as
  outras variantes.
- **`charmap.txt`**: a tradução renomeia o marcador do jogador para
  `{JOGADOR}`. Foi adicionado `PLAYER` como alias (`FD 01`) para que os textos
  que o port adiciona (que ainda usam `{PLAYER}`) continuem compilando.
- **Nada do port foi removido**: `3ds_port/`, `builder/` e `tools/` estão
  incluídos como no upstream.

Observações da tradução original: caracteres com `~` (Ã, Õ etc.) aparecem como
trema `¨`, e a cobertura é a do repositório de origem (parte dos textos pode
continuar em inglês). A interface do port na tela de baixo permanece em inglês
nesta versão (v0.4.0).

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

O save fica em `SD:/3ds/emerald3ds/emerald3ds.sav` e não é alterado por
atualizações do executável/PAK (use sempre o `.3dsx` e o `.pak` do mesmo
build).

Para um ícone na tela inicial, instale o forwarder `Emerald3DS-Forwarder.cia`
com o FBI; ele abre `SD:/3ds/emerald3ds/Emerald3DS.3dsx`.

## Compilar

Requisitos:

- devkitPro com devkitARM, libctru, citro2d, citro3d e 3dstools
  (`pacman -S --needed libctru citro2d citro3d 3dstools`), incluindo `picasso`
- compilador C/C++ nativo no host (`gcc`/`g++`)
- Python 3.11+ com Pillow
- `libpng-dev` e `zlib1g-dev`

Na raiz desta árvore:

```sh
make tools
make generated
make -C 3ds_port -j8
```

Para gerar o executável de release (sem dados embutidos) e o PAK:

```sh
make -C 3ds_port release pak -j8
```

Saídas:

```text
3ds_port/dist/Emerald3DS.3dsx
3ds_port/dist/Emerald3DS.smdh
3ds_port/build/emerald3ds.pak
```

Exemplo com container (devkitPro + Pillow + libpng), sem instalar nada no host:

```sh
podman run --rm -v "$PWD:/work:Z" -w /work e3ds-build bash -lc '
  export DEVKITPRO=/opt/devkitpro
  export PATH="/opt/devkitpro/tools/bin:$PATH"
  make tools CC=gcc CXX=g++ AR=ar
  make generated CC=gcc CXX=g++ AR=ar
  make -C 3ds_port release pak -j8 PYTHON=python3'
```

## Créditos e aviso legal

- Tradução PT-BR: Lucas Gabriel ([lucmsilva651](https://github.com/lucmsilva651)), 2024.
- Port 3DS: [ZallaxDev](https://github.com/ZallaxDev/emerald-3ds).
- Decomplicação: [pret](https://github.com/pret/pokeemerald).

Projeto de fãs, sem vínculo com Nintendo, Game Freak, Creatures ou The Pokémon
Company. Use uma cópia legítima da sua própria ROM. Representantes das
detentoras dos direitos: leiam [LEGAL.md](LEGAL.md).
