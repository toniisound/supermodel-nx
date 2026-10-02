# Supermodel para Nintendo Switch

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
