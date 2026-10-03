<p align="center"><img src="Switch/icon.jpg" width="160" alt="SuperModel NX"></p>

# SuperModel NX

**Emulador de Sega Model 3 para Nintendo Switch** (homebrew `.nro`), fork de
[Supermodel](https://www.supermodel3.com) creado por **ToniiSound**.

- Recompilador PowerPC → ARM64 (JIT), también para coma flotante.
- Menú propio: lista de juegos de la SD, logo, ajustes con el botón **+**.
- − y + a la vez durante un juego: vuelve a la lista de juegos.
- NVRAM inicial para máquina individual (sin enlace) en los juegos que la necesitan.
- Selector de resolución (1280x720, 960x540, 854x480) y contador de FPS en pantalla.

> No incluye ROMs. Usa solo copias de juegos que tengas.
> No está afiliado ni respaldado por Sega ni por Nintendo; Sega, Model 3 y los títulos
> de los juegos son marcas de sus respectivos dueños.

## Créditos

- **Supermodel**: © 2003-2025 The Supermodel Team (Bart Trzynadlowski, Nik Henson,
  Ian Curtis y colaboradores). <https://www.supermodel3.com>
- **Libretro-Supermodel**: [libretro](https://github.com/libretro/Libretro-Supermodel) y
  [sgiannop](https://github.com/sgiannop/Libretro-Supermodel), base de este fork y del
  recompilador ARM64.
- devkitPro y libnx, SDL2, Mesa, glad, Dear ImGui (Omar Cornut), Musashi (Karl Stenerud),
  zlib y minizip.

La pestaña **Credits** del menú (+) muestra los mismos créditos.

## Licencia

GNU General Public License v3 o posterior: ver [LICENSE](LICENSE). Como fork de
Supermodel, todo el código de este repositorio se distribuye bajo esa misma licencia.

## Detalles técnicos

Port como `.nro` independiente de Supermodel (emulador de Sega Model 3), basado en
[Libretro-Supermodel](https://github.com/sgiannop/Libretro-Supermodel) por su
recompilador PowerPC → ARM64. Usa el frontend SDL2 de Supermodel (con su menú ImGui
para elegir juego), OpenGL de escritorio mediante Mesa/EGL y el JIT de libnx.

## Compilar (WSL o Linux con devkitPro)

```sh
sudo dkp-pacman -S switch-dev switch-sdl2 switch-mesa switch-zlib
make -f Makefile.switch -j$(nproc)
```

Opciones:

| Opción | Efecto |
| --- | --- |
| `NXLINK=1` | Envía la salida de consola al PC (`nxlink -s build/switch/supermodel.nro`) |
| `NO_JIT=1` | Solo el intérprete PowerPC (para descartar fallos del JIT) |
| `DEBUG=1` | `-O1 -g` |

El resultado es `build/switch/supermodel.nro`.

## Instalar

```
sdmc:/switch/supermodel/
├── supermodel.nro
└── ROMs/            ← zips estilo MAME: scud.zip, vf3.zip, daytona2.zip…
```

En el primer arranque se crean `Config/`, `NVRAM/`, `Saves/`, `Log/`, `Screenshots/` y
`Assets/`, y se copian `Supermodel.ini`, `Games.xml` y `Music.xml`. Nunca se
sobrescriben: edita `Config/Supermodel.ini` para cambiar ajustes y controles.

Arranca el `.nro` **en modo título** (mantén R al abrir un juego desde el menú de
inicio) para tener toda la RAM. El JIT necesita Atmosphère.

## Logo del menú

La parte superior de la lista de juegos muestra el logo que va dentro del `.nro`:
pon `Assets/logo.bmp` en el proyecto antes de compilar (BMP de 32 bits para tener
transparencia; se escala a ~120 px de alto manteniendo la proporción, p. ej. 800x120).
No se lee de la SD. Sin el archivo se escribe "SuperModel NX".

## Controles por defecto

| Botón | Acción |
| --- | --- |
| + / − | Start / Moneda |
| Stick izquierdo, cruceta | Joystick, volante |
| ZR / ZL | Acelerar / frenar, disparo (pistolas, Virtual On) |
| R / L | Cambio de marcha arriba / abajo |
| A B X Y | Botones del juego (VF3: Y defensa, B puñetazo, A patada, X escape) |
| Click stick izquierdo / derecho | Service / Test |
| − + + | Volver a la lista de juegos (Exit en la lista cierra el programa) |
| − + R / − + L | Guardar / cargar estado |
| − + cruceta derecha | Cambiar ranura de estado |
| − + cruceta abajo | Pausa |

## Rendimiento

Empieza con `ShowFrameRate = 1` (ya activado) y prueba primero juegos Step 1.0
(Virtua Fighter 3, Scud Race). Si va lento:

- Activa overclock con sys-clk.
- `PowerPCFrequency = 50` en el `[ Global ]` (o en la sección del juego) baja la
  frecuencia de la CPU emulada; muchos juegos lo toleran.
- `MultiThreaded = 1` reparte placa base, sonido y placa de control entre los núcleos.
- `JitNativeFP = 1` (por defecto) traduce también la coma flotante del PowerPC a ARM64.
  Si un juego se comporta raro (IA, físicas, tiempos), ponlo a `0` en la sección de ese
  juego, p. ej. `[ daytona2 ]` + `JitNativeFP = 0`.

## Qué cambia respecto a Libretro-Supermodel

- `Src/CPU/PowerPC/Jit/JitArm64.cpp`: soporte de doble mapeo (RW para escribir, RX
  para ejecutar) con la API `jit*` de libnx (`Src/OSD/Switch/SwitchJit.c`).
- `Src/OSD/Switch/`: rutas en la SD, carga de OpenGL con glad, mapeo de mandos
  y reparto de hilos por núcleo.
- `Src/OSD/SDL/`: contexto OpenGL de escritorio en Switch, menú controlable con mando.
- `Src/Model3/Model3.cpp`: la opción de núcleo de libretro solo se usa en la build libretro.
