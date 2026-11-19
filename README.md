# OpenDrums (v0.6, protótipo)

Instrumento VSTi de bateria baseado em samples, pensado para usar **kits open source** e permitir que o
usuário **veja o kit** e **troque cada peça** por uma peça de outro kit.

## Estado desta versão (v0.6)

| Parte | Estado |
|---|---|
| Núcleo (kits, motor, mixer, EQ, compressor, reverb, MIDI, grooves) | Escrito e **testado** (`tests/core_test.cpp`, tudo passando) |
| Plugin JUCE (4 telas, parâmetros, 8 saídas estéreo, CC, matrix, estado) | **Compilado no Linux (JUCE 8.0.6, Standalone) e aberto de verdade** num display virtual; telas conferidas por screenshots reais |
| Caminho de áudio do plugin | Teste automático headless (`tests/processor_test.cpp`): rota para 3/4 e mono, mute, filtro de canal MIDI, CC → fader, player de grooves, salvar/carregar estado |
| Windows / VST3 numa DAW | **Ainda não testado** (compilar com VS2022; abrir numa DAW real) |
| Kits | 2 kits **sintéticos** (CC0) só para teste. Kits reais ainda precisam de conversão |
| Grooves | 1.738 clipes **originais** (CC0), 15 estilos, gerados por `tools/make_grooves.py`. É um conjunto inicial, **não** equivale ao tamanho das bibliotecas comerciais |

## As 4 telas

1. **KIT** – kit escolhido, lista de kits, troca de peças e a caixa **Sala** (Garagem, Studio pequeno/médio/grande,
   Club pequeno/médio/grande, Estádio). Cada sala muda o reverb e o **cenário cartoon** atrás do kit.
2. **MIX** – console: por peça, fader, pan, nome, imagem, M/S/Ø, saída (Master, estéreo 1/2…15/16, mono 1…16),
   aba SENDS (reverb e paralelo), compressor (on, threshold, ratio, makeup, release, FAST/SLOW) e EQ estilo SSL
   (HF kHz/dB/bell, HMF kHz/dB/Q, LMF kHz/dB/Q, LF Hz/dB/bell). Fixos à direita: bus de **compressão paralela**,
   bus de **reverb** (sala, decay, pré-delay, tom, largura, cortes) e **master**.
3. **GROOVES** – abas Estilos/Favoritos; colunas Estilo › Compasso/BPM › Seção (intro, verso, pré-refrão, refrão,
   ponte, viradas, especiais, finais); lista de clipes com mini piano-roll, estrela e **arrastar o .mid para a DAW**.
   Duplo clique toca em loop; BPM do clipe, do host ou manual.
4. **MATRIX** – saídas de áudio por peça, canais MIDI por peça, mapeamento de MIDI CC para fader/pan/sends/mute/solo/master,
   e notas MIDI de cada articulação.

## Reverbs

- Reverb próprio (estrutura Freeverb, domínio público, + pré-delay, reflexões iniciais e RT60 calibrado por sala).
  Medi o RT60 de cada sala contra o alvo (teste automático).
- Outras opções avaliadas, **não incluídas**: RoomReverb (GPL3), Dragonfly Reverb (licença a confirmar; acredito ser GPL3),
  respostas ao impulso OpenAIR (licença por arquivo). Convolução com IRs é uma evolução possível.

## Saídas separadas

O plugin declara 8 saídas estéreo (1/2 ativa; 3/4…15/16 você ativa no host). Cada peça vai para as saídas marcadas
na Mix ou na Matrix. Os buses de reverb e de paralelo entram no Master.

## Formato da biblioteca de grooves

`Grooves/<Estilo>/<num>-<den> <bpm>bpm/<NN Seção>/<clipe>.mid` (ex.: `Funk/4-4 90bpm/02 Verso/Verso 01.mid`).
Basta colocar pastas nesse formato na pasta de grooves (botão "Pasta de grooves…" ou variável `OPENDRUMS_GROOVES`).

## Como compilar (Windows)

Precisa de: Visual Studio 2022 (carga "Desenvolvimento para desktop com C++"), CMake 3.22+, Git e Python 3
com `numpy` (só para gerar os kits de demonstração).

```bat
cd OpenDrums
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target OpenDrums_VST3
```

O CMake baixa o JUCE 8.0.6 sozinho (precisa de internet na primeira vez). O plugin fica em
`build\plugin\OpenDrums_artefacts\Release\VST3\OpenDrums.vst3`; copie para `C:\Program Files\Common Files\VST3`.
Também é gerado um aplicativo `Standalone`, bom para testar sem DAW.

Só o núcleo e os testes (sem JUCE):

```bat
cmake -S . -B build-core -DOPENDRUMS_BUILD_PLUGIN=OFF
cmake --build build-core --config Release
build-core\Release\core_test.exe kits
build-core\Release\render_demo.exe kits demo-rock saida.wav snare=demo-tight/snare
```

## Instalar kits

O plugin procura kits em `%APPDATA%\OpenDrums\Kits` (uma subpasta por kit, cada uma com um `kit.json`).
Dá para escolher outra pasta no botão **Pasta de kits…**, ou definir a variável de ambiente `OPENDRUMS_KITS`.

```bat
python tools\make_demo_kits.py "%APPDATA%\OpenDrums\Kits"
```

## Formato do kit (`kit.json`)

```json
{
  "format": 1,
  "id": "meu-kit",
  "name": "Meu Kit",
  "author": "Fulano",
  "license": "CC-BY-4.0",
  "pieces": [
    {
      "id": "snare", "type": "snare",
      "label": "Caixa 14x5,5 (o que se sabe sobre a peça)",
      "image": "images/snare.svg", "diameter": 14,
      "gainDb": -1.0, "pan": 0.0, "tune": 0.0,
      "articulations": {
        "hit": { "layers": [
          { "velMin": 1,  "velMax": 60,  "files": ["samples/snare_soft_1.wav", "samples/snare_soft_2.wav"] },
          { "velMin": 61, "velMax": 127, "files": ["samples/snare_hard_1.wav", "samples/snare_hard_2.wav"] }
        ]},
        "rimshot":   { "layers": [ { "files": ["samples/snare_rim.wav"] } ] },
        "sidestick": { "layers": [ { "files": ["samples/snare_side.wav"] } ] }
      }
    }
  ],
  "layout": [ { "slot": "snare", "piece": "snare", "x": 0.35, "y": 0.60 } ]
}
```

- **Peça** (`pieces`): um tambor/prato com samples e imagem. `type`: `kick`, `snare`, `tom`, `hihat`, `crash`,
  `ride`, `splash`, `china`, `perc`. `diameter` (polegadas) define o tamanho na tela; a imagem é desenhada com essa largura.
- **Posição** (`layout`): onde a peça aparece (`x`, `y` de 0 a 1). Quem manda no MIDI é a posição, não a peça:
  por isso uma peça trocada continua respondendo às mesmas notas.
- **Posições conhecidas** (já com notas GM): `kick`, `snare`, `tom1`, `tom2`, `tom3`, `floor`, `hihat`, `crash1`, `crash2`,
  `ride`, `splash`, `china`, `cowbell`, `tambourine`. Dá para sobrescrever as notas com `"midi": { "hit": [38] }`.
- **Funções** (`articulations`): `hit`, `rimshot`, `sidestick`, `closed`/`pedal`/`open` (chimbal), `bell` (prato).
  Se a peça não tiver a função pedida, usa `hit`.
- Samples em **WAV** (PCM 8/16/24/32 bits ou float 32), mono ou estéreo, em qualquer taxa (é reamostrado).
- Descreva a peça só com o que o kit documenta. Evite logotipos e marcas nas imagens.
- Estantes, pezinhos, pedal e animações não vêm do kit: são desenhados pelo plugin a partir do tipo e do tamanho da peça.

Prévia sem abrir o plugin (congela a animação num instante, t em segundos):

```bat
build-core\Release\preview_kit.exe kits demo-rock tela.svg hit=kick:hit:0:120 snare=demo-tight/snare
```

## Estrutura

```
core/      núcleo sem JUCE: Sample, Kit (kit.json), Library, Rig (kit carregado), Engine (áudio),
           KitGeometry (estantes, pezinhos, pedal do bumbo e curvas de animação)
plugin/    JUCE: PluginProcessor, PluginEditor, KitView (tela do kit)
tools/     make_demo_kits.py (kits sintéticos), render_demo.cpp (renderiza uma levada em WAV),
           preview_kit.cpp (desenha o kit em SVG com a mesma geometria do plugin; use rsvg-convert para PNG)
tests/     core_test.cpp
kits/      kits de demonstração já gerados
third_party/nlohmann/json.hpp   (MIT)
```

## Licenças

- Código do OpenDrums: defina a licença que preferir antes de publicar.
- JUCE 8: AGPLv3 ou licença comercial (inclui o SDK do VST3). Distribuir o plugin fechado exige a licença comercial.
- nlohmann/json: MIT.
- Cada kit tem a própria licença no `kit.json`. CC-BY exige atribuição; CC-BY-SA exige compartilhar derivados
  sob a mesma licença; confira antes de redistribuir samples junto com o plugin.

## Limitações conhecidas

- Só WAV (sem FLAC/OGG/SFZ). Kits grandes são totalmente carregados na memória.
- Sem microfones separados (overhead/room) por peça ainda; o reverb é um bus de sala simulado.
- Os grooves são um conjunto inicial original; padrões são gerados por regras, com variações simples.
- Não implementado: aba SENDS para outros buses além de reverb/paralelo, edição de clipes MIDI, humanização.
- O player de grooves usa o tempo do host quando ele informa BPM; em hosts que não informam, usa o BPM manual.
- A troca de clipe no player pode, raramente, liberar memória na thread de áudio (o clipe também fica no cache da UI).
- Ganhos padrão do mixer (master −6 dB) foram escolhidos de ouvido sintético; ajuste com kits reais.

## Próximos passos sugeridos

1. Compilar no Windows (VS2022) e testar numa DAW (Reaper, FL, Cubase).
2. Conversor de kits DrumGizmo/AVL para `kit.json`, com as imagens das peças.
3. Ampliar a biblioteca de grooves (por exemplo importando o Groove MIDI Dataset, CC BY 4.0, com atribuição).

## Conversor de kits reais (`tools/convert_kit.py`)

Converte kits open source em pastas de samples para o formato `kit.json` do OpenDrums.

```
python3 tools/convert_kit.py tools/recipes/big-rusty-drums.json <pasta-do-kit-original> kits/big-rusty-drums \
        [--layers 6] [--rr 3] [--bits 16|24] [--jobs 8]
```

- Requer `ffmpeg` e Python 3. Lê FLAC/WAV, mistura os microfones com os pesos da *receita*, normaliza por peça,
  escolhe até N camadas de velocity e M round-robins, grava WAV (16 bits com dither, ou 24) e gera as imagens cartoon.
- A *receita* (`tools/recipes/*.json`) diz quais pastas viram cada peça/articulação, a mixagem de mics e a posição no palco.
- Incluída: **Big Rusty Drums** (Karoryfer, CC0) — https://github.com/sfzinstruments/karoryfer.big-rusty-drums
  (684 MB originais → ~118 MB convertidos, 282 samples). Baixe o repositório e rode o comando acima.
- Kits CC-BY (DRSKit, MuldjordKit) exigem atribuição ao autor e receita/parser próprio (SFZ ARIA com macros: não suportado).
- Limitações: mistura de mics fixa pela receita (sem CC), sem choke groups/hi-hat por CC do SFZ; todo o kit vai para a RAM.

### Receita do Unruly Drums (CC0)
`tools/recipes/unruly-drums.json` — https://github.com/sfzinstruments/karoryfer.unruly-drums
(771 MB originais → 93 MB, 10 peças, 258 samples). O kit não tem toms: as caixas de 13", 14" e 22" fazem esse papel.
Nas receitas, uma articulação pode ser `{ "dir": "s14", "prefix": "s14_center" }` (pasta com várias articulações misturadas,
estrutura `<instrumento>/<mic>/rrN/arquivo`), e um microfone mono pode ser posicionado: `"ohl": { "w": 0.4, "pan": -0.7 }`.

## Parser de kits DrumGizmo (`tools/convert_drumgizmo.py`)

```
python3 tools/convert_drumgizmo.py <pasta-do-kit> kits --id muldjord-kit --name "MuldjordKit" \
        --license CC-BY-4.0 --author "Lars Muldjord" --source <url> \
        --map "KdrumR=-,Tom4=floor,RideL=-,RideLBell=-,CrashL=crash1,CrashR=crash2,SnareRest=-"
```

- Aceita o formato nativo (`drumkit.xml` + XMLs de instrumento; cada `<sample power>` vira uma camada de velocity,
  cada `<audiofile channel>` um microfone) **ou** a pasta `Samples/<Instr>/<N>-<Instr>-<Canal>.flac` (N=1 é o mais suave).
- Mapeia instrumentos para as peças por nome (Kdrum, Snare, Tom1–4, Hihat*, Crash*, Ride*, China…); `--map` corrige.
  Use `--map "Inst=-"` para ignorar um instrumento. Mic próprio = 1,0; overheads = 0,9; sala = 0,4; vazamento = 0,12; trigger = 0.
- Grava a receita resolvida (`<out>/<id>.recipe.json`) para ajuste manual; `--no-convert` só gera a receita.
- Testado com o MuldjordKit (CC-BY-4.0, **exige atribuição a Lars Muldjord**): 670 MB → 32 MB, 11 peças, 78 samples.
- O caminho XML nativo foi testado só com um kit sintético que montei a partir do Muldjord, não com um kit nativo real
  (drumgizmo.org está inacessível daqui). Kits com `<velocities>`/`<channelmap>` mais complexos podem precisar de ajustes.

## Compilar e gerar instaladores (GitHub Actions)

O workflow `.github/workflows/build.yml` compila no Windows, macOS e Linux a cada push/PR, roda os testes do núcleo
e gera os instaladores como *artifacts* da execução (aba **Actions**):

| Sistema | Arquivo | Instala |
|---|---|---|
| Windows x64 | `OpenDrums-<versão>-Windows-x64-setup.exe` (Inno Setup) | VST3 em `C:\Program Files\Common Files\VST3`, app independente, kits/grooves em `C:\ProgramData\OpenDrums` |
| macOS (Intel + Apple Silicon) | `OpenDrums-<versão>-macOS-universal.dmg` com um `.pkg` | VST3 e AU em `/Library/Audio/Plug-Ins`, app em `/Applications`, conteúdo em `/Library/Application Support/OpenDrums` |
| Linux x86_64 | `.deb` e `.tar.gz` | VST3 em `/usr/lib/vst3`, `opendrums` em `/usr/bin`, conteúdo em `/usr/share/opendrums` |

Para publicar uma versão: `git tag v0.4.0 && git push --tags` — o workflow cria uma *Release* com os três instaladores.

O plugin procura kits/grooves nesta ordem: variável `OPENDRUMS_KITS`/`OPENDRUMS_GROOVES` → pasta do usuário
(`%APPDATA%\OpenDrums\Kits`, `~/Library/Application Support/OpenDrums/Kits`, `~/.config/OpenDrums/Kits`) se tiver conteúdo →
pasta instalada para todos os usuários. Kits reais convertidos podem ser copiados para a pasta do usuário.

Build local: `cmake -S . -B build && cmake --build build --config Release` (o CMake baixa o JUCE 8.0.6).
Linux precisa de: `libasound2-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libfreetype6-dev libfontconfig1-dev libgl1-mesa-dev`.

Observações: os instaladores não são assinados. No Windows o SmartScreen avisa ("Mais informações → Executar assim mesmo");
no macOS o `.pkg` tem assinatura ad-hoc, então use botão direito → Abrir (ou assine com Developer ID e notarize).

## Kits incluídos

| Kit | Tipo | Licença | Como entra |
|---|---|---|---|
| Demo Rock, Demo Tight | sintéticos de teste | CC0 | gerados por `tools/make_demo_kits.py` (o workflow roda) |
| Eletrônico 808, Eletrônico 909 | bateria eletrônica sintetizada | CC0 | gerados por `tools/make_electronic_kits.py` (o workflow roda) |
| DRSKit | acústico de estúdio, multi-microfone (Michael Wolff) | CC-BY-4.0 | baixado e convertido no release |
| MuldjordKit | acústico de estúdio, multi-microfone (Lars Muldjord) | CC-BY-4.0 | baixado e convertido no release |
| Big Rusty Drums | acústico com caráter (Karoryfer) | CC0 | baixado e convertido no release |
| Unruly Drums | acústico "sujo" (Karoryfer) | CC0 | baixado e convertido no release |
| Percussão: Cajón, Bongôs e Congas | percussão real (Gogodze Phu I + VCSL) | CC0 | baixado e convertido no release |

`tools/build_real_kits.sh <pasta-de-downloads> kits` faz o mesmo localmente (precisa de git, ffmpeg, python3 e numpy; ~6 GB de download).
No GitHub, a conversão roda no job **Real kits** ao publicar uma tag `v*` (ou em *Run workflow* manual) e o resultado entra nos três instaladores.
Os kits eletrônicos são sons sintetizados do zero com as receitas clássicas (seno varrido, tom + ruído, osciladores quadrados
desafinados); não são gravações de equipamentos Roland.

## Ambiência (reverb)
A sala escolhida no Kit view agora soa de imediato: os *sends* de reverb de cada canal começam abertos (antes eram 0) e há um
botão **AMBIENTE** ao lado da sala, que controla o retorno do reverb. O compressor de cada canal começa com *Fast attack* desligado.

Para rodar localmente sem o GitHub: `python tools/make_demo_kits.py kits && python tools/make_electronic_kits.py kits` (precisa de numpy e scipy);
a pasta `kits/` não é versionada.
