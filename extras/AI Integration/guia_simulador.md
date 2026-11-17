# Guía del simulador: cómo generar un ensayo nuevo

Pasos para sacar un ensayo del **Inartrans Simulator** (CARLA) en el ordenador del laboratorio y llevarlo a
`datasets_simulador/`. Lo que viene después (registrarlo y exportarlo para Edge Impulse) está en `guia_uso.md`,
apartado 4.

Si es la primera vez, lee el apartado 1 (conceptos) y haz una vez el apartado 2 (comprobaciones). Para un ensayo
normal basta con los apartados 3 a 7, y la ficha del apartado 10 sirve de lista de comprobación.

---

## 1. Conceptos

### 1.1 Las dos formas de tener CARLA

CARLA es un simulador construido sobre el motor gráfico **Unreal Engine 4** (UE4). En el ordenador hay dos
instalaciones distintas:

| | Versión binaria | Compilación desde código con el editor de Unreal |
|---|---|---|
| Qué es | CARLA 0.9.15 tal como lo publican sus autores, ya compilado y cerrado | El código fuente de CARLA (rama `ue4-dev`) compilado en el laboratorio, abierto en el editor de Unreal |
| Analogía | El juego instalado | El proyecto del estudio que hizo el juego |
| Contenedor | `carla-etsii-map2` (imagen `carlasim/carla:0.9.15`) | **`carla-build-1`** (imagen `carla-build`) |
| Entorno conda | `carla` | **`carla-ue4dev`** |
| Se lanza con | `./CarlaUE4.sh` | `make launch` → abrir nivel → **Play** |
| Editar mapas, añadir modelos | No | Sí |

**Usamos la segunda** por dos motivos:
1. El mapa ferroviario **`railway4_simple`** se creó en ella y solo existe ahí.
2. **El tren** (`vehicle.train.locomotivea4`, `vehicle.train.traincara4`) son modelos añadidos por el
   laboratorio. CARLA no trae trenes; en la versión binaria no existen.

Términos que aparecen:
- **`ue4-dev` / `ue5-dev`**: ramas del repositorio de CARLA. La primera es la línea con Unreal Engine 4.26 (la
  nuestra); la segunda, la nueva con Unreal Engine 5.5.
- **0.9.15, 0.9.15.2, 0.9.16**: versiones publicadas de la línea UE4. La 0.10.0 es la primera de UE5.

### 1.2 Docker: imágenes y contenedores

Docker mete un programa con todo lo que necesita (sistema, librerías, versiones exactas) en una caja aislada.
Así no hay que instalar en el Ubuntu del laboratorio las dependencias de compilación de Unreal.

- **Imagen**: la plantilla, de solo lectura. No se ejecuta.
- **Contenedor**: una instancia creada a partir de una imagen; es lo que se arranca (*Up*) y se para
  (*Exited*). Conserva sus cambios entre arranques.

En la extensión *Containers* de VS Code: `carla-build-1` es el **contenedor**; `carla-build` y
`carla-build:latest` son la misma **imagen** (`:latest` es su etiqueta); `sha256:bcdb62f…` es una imagen antigua sin
nombre que no se usa.

La carpeta `~/carla_linux_build/carla` del ordenador es la misma que `/app/carla` dentro del contenedor: lo que se
cambia en un lado se ve en el otro.

Comandos:

```bash
docker ps -a                                # lista los contenedores y su estado
docker start carla-build-1                  # arranca el contenedor
docker exec -it carla-build-1 /bin/bash     # abre una terminal DENTRO del contenedor
exit                                        # sale de esa terminal (el contenedor sigue arrancado)
docker stop carla-build-1                   # apaga el contenedor
```

Dentro del contenedor el prompt empieza por `docker@inartrans1:` (al entrar, en `/app`). Fuera, `(base) inartrans1@inartrans1:~$`.

### 1.3 Entornos de conda

Conda gestiona instalaciones de Python separadas (**entornos**), cada una con su versión de Python y sus paquetes.
El nombre del entorno activo aparece entre paréntesis al principio del prompt.

```bash
conda env list                  # lista los entornos
conda activate carla-ue4dev     # entra en el del simulador
conda deactivate                # sale
```

El simulador necesita `carla-ue4dev`: tiene el paquete `carla` de Python (el cliente), `numpy` y `pygame`.
En VS Code se elige con `Ctrl+Shift+P` → *Python: Select Interpreter* → `carla-ue4dev`.

### 1.4 Servidor y cliente

| | Servidor | Cliente |
|---|---|---|
| Qué es | CARLA (Unreal) con el mapa y la física del mundo 3D | `main.py` del Inartrans Simulator |
| Dónde corre | Dentro del contenedor `carla-build-1` | En el Ubuntu del ordenador, con el entorno `carla-ue4dev` |
| Qué hace | Abre el puerto 2000 y espera órdenes | Crea el tren, avanza el mundo tick a tick, calcula la dinámica (LTD), genera IMU y UWB y escribe los CSV |

Se comunican por red con `localhost:2000`. Funciona porque el contenedor comparte la red del ordenador
(`--net=host`).

Dos consecuencias:
- **`main.py` no abre ningún mapa**: usa el que tenga abierto el servidor (`client.get_world()`). Antes de lanzarlo,
  `railway4_simple` tiene que estar abierto y en **Play**.
- **Modo síncrono**: `main.py` pone el servidor en modo síncrono, con un paso fijo de 5 ms. El servidor no avanza
  solo; espera cada `world.tick()` del cliente. Si `main.py` se cae sin limpiar, el editor parece congelado (ver
  apartado 9).

### 1.5 Qué hace `main.py`

1. Crea la carpeta de salida (`parent_dir`) y guarda en ella `config.json`, una copia de todos los parámetros.
2. Se conecta al servidor y lo pone en modo síncrono (`DELTA_SECONDS = 0.005` s).
3. Crea la locomotora y los 4 vagones en el punto de aparición de `config.py` y les pone una cookie (IMU, GNSS,
   V2X, UWB). Calcula la geometría de la vía e inicializa la dinámica con velocidad 0.
4. En cada tick (5 ms de simulación):
   - si el tiempo de simulación llega a `FAIL_TIME`, mete **una vez** el evento activado;
   - mueve el tren según la dinámica;
   - genera las medidas de IMU y UWB de cada cookie;
   - cada 0,25 s, un vagón envía su mensaje al coordinador por turnos (V1, V2, V3, V4, V1…): **cada vagón
     transmite una vez por segundo**. En el semiciclo siguiente se envían los ACK.
5. Termina al llegar a `T_STOP`, si el tren se detiene (tras un frenado) o con `Ctrl+C`. En todos los casos borra
   los actores y devuelve el servidor a modo normal.

Con `ENABLE_RENDER = False` la ventana de pygame se queda en negro: es normal (no se dibuja la cámara, para ir más
rápido).

---

## 2. Comprobaciones (una sola vez)

Hazlas la primera vez, o si algo cambia en el ordenador.

1. **El entorno existe:**
   ```bash
   conda env list           # debe aparecer carla-ue4dev
   ```
2. **Versión del cliente y del servidor.** Deben coincidir. Con el servidor lanzado (apartado 5), descomenta
   en `main.py` el bloque de depuración del principio:
   ```python
   print("Python executable:", sys.executable)
   print("carla imported from:", carla.__file__)

   client = carla.Client('localhost', 2000)
   client.set_timeout(10.0)
   print("Client version:", client.get_client_version())
   print("Server version:", client.get_server_version())
   ```
   Ejecuta y comprueba que `Python executable` está dentro del entorno `carla-ue4dev` y que las dos versiones
   coinciden. Vuelve a comentarlo después. Otras formas de verlo:
   ```bash
   conda activate carla-ue4dev && pip show carla               # versión del cliente
   cd ~/carla_linux_build/carla && git describe --tags         # versión del código del servidor
   ```
   Si no coinciden, no lances ensayos todavía: hay que instalar en el entorno el cliente de la misma versión
   (`pip install carla==<versión>`), y conviene consultarlo antes.
3. **Existe la carpeta de salida.** `main.py` crea `parent_dir` con `os.mkdir`, que no crea carpetas intermedias.
   La carpeta que la contiene (ahora `Datasets_def/`) tiene que existir dentro de la carpeta del simulador.

---

## 3. Preparar los parámetros del modelo (`config/config.py`)

**Para los ensayos normales no se toca.** Todo queda guardado en el `config.json` de cada ensayo, así que siempre se
sabe con qué parámetros se generó. Lo que puede interesar cambiar:

| Bloque | Parámetro | Valor | Qué controla |
|---|---|---|---|
| — | `DELTA_SECONDS` | `0.005` | Paso de simulación (s). No tocar |
| — | `v_inital` | `0.0` | Velocidad inicial (m/s) |
| `train` | `num_wagons` | `4` | Número de vagones, sin contar la locomotora |
| `train` | `distance` | `21.2` | Separación entre vehículos al aparecer (m) |
| `train` | `positions` | `[5]` | Punto de aparición del tren en el mapa |
| `ltd` | `v_ref` | `25.0` | Velocidad de crucero (m/s; 90 km/h) |
| `ltd` | `v_slew` | `0.4` | Rampa de la consigna (m/s²): tarda unos 60 s en llegar a crucero |
| `ltd` | `a_target_brake`, `jerk_target` | `0.6`, `0.25` | Deceleración y suavidad del frenado |
| `ltd` | masas, `*_davis`, `b`, `k1`…`k3`, `eps`… | — | Física de los vehículos y de los enganches. No tocar |
| `cookie` | `comm_rate_hz` | `4.0` | Mensajes por segundo del conjunto (uno por ranura de 0,25 s) |
| `cookie.imu` | `acc_white_sigma`, `acc_vib_sigma`… | — | Ruido de la IMU sintética |
| `cookie.uwb` | `sigma_0`, `sigma_dist` | `0.02`, `0.01` | Ruido UWB: base (m) y aumento por metro (m/m) |
| `cookie.uwb` | `p_nlos`, `nlos_scale` | `0.2`, `0.5` | Probabilidad y escala del sesgo NLOS |

Si cambias `num_wagons` o `comm_rate_hz`, comprueba después que `adaptador_simulador.py` sigue leyendo bien el
ensayo: supone una vuelta de 1 s con un mensaje por vagón.

---

## 4. Preparar el ensayo (`main.py`)

Cuatro cosas, al principio del archivo y en el bloque del evento.

### 4.1 Carpeta de salida

```python
parent_dir = "Datasets_def/decoupling_prefail30s_t100s"
```

**Un nombre nuevo en cada ejecución.** Si la carpeta ya existe, el script falla al empezar
(`FileExistsError`). Conviene que el nombre resuma el guion, p. ej. `nominal_400s`, `decoupling_w2_150s`,
`prefail_w2_30s_t100s`, `brake_300s`.

La ruta es relativa a la carpeta desde la que se lanza el script (la del simulador; ver apartado 6).

### 4.2 Tiempos

```python
FAIL_TIME = 100.0     # segundo de simulación en el que se mete el evento
T_STOP = 400.0        # segundo de simulación en el que termina el ensayo
```

- El tren arranca parado y llega a crucero hacia los 60 s. Con `FAIL_TIME ≥ 100` el evento ocurre a velocidad
  estable.
- Son segundos **de simulación**, no reales (ver apartado 6).

### 4.3 El evento

En el bucle principal hay cuatro bloques, dentro de `if ltd._t_now >= FAIL_TIME:`. Los que están entre `"""` están
desactivados. **Solo debe estar activo uno.** Para cambiar, rodea con `"""` el activo y quítalas del que quieras.

| Evento | Llamada | Qué hace |
|---|---|---|
| Desacople | `ltd.start_decoupling(wagon_idx=2)` | Rotura instantánea del enganche |
| Frenado | `ltd.start_braking()` | Frenado de servicio hasta parar. El ensayo termina solo al pararse |
| Fallo de acople | `ltd.start_coupler_failure(wagon_idx=2, t_fail_s=60, alpha=2)` | Fallo progresivo del enganche durante `t_fail_s` segundos |
| Fallo con degradación previa (activo ahora) | `ltd.start_coupler_prefail(coupler=2, t_prefail=30)` | El enganche pierde rigidez durante `t_prefail` segundos y después rompe |

**Qué enganche.** El índice `k` (`wagon_idx` o `coupler`) es el enganche entre V`k` y V`k+1`: con `2`, entre
**V2 y V3**, de modo que el tren queda dividido en C–V1–V2 delante y V3–V4 detrás. Así lo describen los ensayos
`decoupling_w2_150s` y `decoupling_prefail30s_t100s`. Se puede confirmar en `ltd_log/couplers.csv`: la fuerza de
ese enganche cae a cero en la rotura.

**Ensayo sin evento (nominal).** Pon `FAIL_TIME` mayor que `T_STOP`, p. ej. `FAIL_TIME = 10000.0`.

### 4.4 Resto del archivo

No se toca. En particular: `ENABLE_RENDER = False` (más rápido) y el bloque de comunicaciones del bucle.

---

## 5. Lanzar el servidor CARLA (terminal 1)

1. Abre una terminal en el Ubuntu (`Ctrl+Alt+T`) y comprueba en qué pantalla se dibujan las ventanas:
   ```bash
   echo $DISPLAY
   ```
   Responde algo como `:0` o `:1`. **Compruébalo cada vez**: el valor cambia según la sesión (p. ej., si se ha
   entrado por escritorio remoto o hay más de una sesión abierta). Apúntalo para el punto 3. Ojo con el `$`:
   `echo DISPLAY`, sin él, solo repite la palabra.
2. Arranca el contenedor y entra en él:
   ```bash
   docker start carla-build-1
   docker exec -it carla-build-1 /bin/bash
   ```
   El prompt cambia a `docker@inartrans1:/app$`: se entra en `/app`, no en la carpeta de CARLA.
3. Ve a la carpeta de CARLA, indica la pantalla y abre el editor:
   ```bash
   cd carla
   export DISPLAY=:1      # el valor que dio echo $DISPLAY en el punto 1 (aquí, :1)
   make launch
   ```
   - `cd carla`: el `Makefile` está en `/app/carla`. Con `ls` deben aparecer, entre otros, `LibCarla`,
     `PythonAPI`, `Unreal` y `Makefile`.
   - `export DISPLAY=...`: la terminal del contenedor no hereda la pantalla del ordenador; sin esto el editor no
     sabe dónde abrir su ventana. Pon **exactamente** el valor que dio `echo $DISPLAY` en el punto 1, sin `$` y
     sin espacios alrededor del `=` (`DISPLAY=:1`, no `DISPLAY:=1`). Se puede comprobar dentro del contenedor con
     `echo $DISPLAY`. Hay que repetirlo cada vez que se abre una terminal nueva en el contenedor.
   - `make launch`: compila lo que haya cambiado y abre el editor. La primera vez de la sesión tarda unos minutos;
     verás mensajes de `Setup.sh`, `BuildUE4Plugins.sh` y `BuildCarlaUE4.sh`. `make launch-only` abre el editor sin
     compilar, y solo sirve si ya está compilado.
   - Si `make` dice `No rule to make target ...`, no estás en `/app/carla` (compruébalo con `pwd`).
4. Si sale el aviso *"Project file is out of date. Would you like to update it?"*, pulsa **Not Now**.
5. **File → Open Level** y abre **`railway4_simple`** (el archivo con el icono de los conos).
6. Pulsa **Play** en la barra superior. Debe verse la vía ovalada sobre fondo verde.
7. Deja el editor en Play y esta terminal abierta. No los toques mientras dure el ensayo.

---

## 6. Lanzar la simulación (terminal 2)

En una terminal **nueva**, fuera del contenedor:

```bash
conda activate carla-ue4dev
cd <carpeta del Inartrans Simulator>
python main.py
```

O desde VS Code: abre la carpeta del simulador como espacio de trabajo, elige el intérprete `carla-ue4dev`
(apartado 1.3) y pulsa Play sobre `main.py`. La carpeta abierta tiene que ser la del simulador, para que
`parent_dir` se cree en su sitio.

Qué debe aparecer en la consola:

```
Longitud vía: ... m
voy a mandar mensajes cada 0.25 segundos de simulación (comm_rate_hz=4.0 Hz)
esto equivale a mandar mensajes cada 25.0 ticks de simulación (DELTA_SECONDS=0.005 s)
Inciando fallo del acople                   <- al llegar a FAIL_TIME (cambia según el evento)
>>> Tren detenido. Finalizando simulación...
🧹 Limpiando actores...
✅ Limpieza completada.
```

En el editor se ve aparecer el tren y moverse por la vía.

**Duración.** Cada segundo de simulación son 200 ticks: 400 s son 80 000 ticks. El ensayo tarda bastante más que
`T_STOP` en tiempo real. Apunta cuánto tarda el primero para planificar los siguientes.

**Cortar antes:** `Ctrl+C` en esta terminal. También limpia el servidor. Los CSV contienen lo simulado hasta ese
momento.

---

## 7. Comprobar el resultado

En `<carpeta del simulador>/Datasets_def/<parent_dir>/` debe haber:

```
config.json
coord/      tx_coord.csv, rx_coord.csv, gnss_coord.csv, imu_crd.csv
wagon_0/ … wagon_N/    imu, tx, rx, gnss y uwb de cada vagón
ltd_log/    dyn.csv, couplers.csv
```

Comprobaciones rápidas:
- `coord/rx_coord.csv` tiene filas de los cuatro vagones (columna `source_id`) y llega hasta cerca de `T_STOP`.
- Si hubo evento, en `ltd_log/couplers.csv` el enganche elegido cambia en `FAIL_TIME` (o en
  `FAIL_TIME + t_prefail`, con degradación previa).
- **No abras los CSV con Excel ni los guardes desde él**: con la configuración española cambia los decimales. Para
  mirarlos, usa VS Code o Python.

---

## 8. Llevarlo al flujo de procesado

1. Copia la carpeta entera del ensayo a `extras/AI Integration/datasets_simulador/` (en tu copia local; no se sube
   a Git).
2. Sigue `guia_uso.md`, apartado 4: añade su entrada en `ensayos_simulador.json` y expórtalo.

Cómo rellenar la entrada a partir de `main.py`:

| Campo | Valor |
|---|---|
| `carpeta_simulador` | El nombre de la carpeta (la última parte de `parent_dir`) |
| `evento_s` | Desacople: `FAIL_TIME`. Degradación previa: `FAIL_TIME + t_prefail` (el instante de la rotura). Nominal: `null` |
| `etiqueta_tras_evento` | `"LOST_INTEGRITY"` en las roturas |
| `fin_s` | `null`, o el segundo a partir del cual descartar el final |
| `descripcion` | El guion: evento, enganche, tiempos |

`evento_s` cuenta desde el inicio de la simulación, igual que `FAIL_TIME`: `adaptador_simulador.py` pone el cero en
la primera muestra.

Tras una rotura, el bloque trasero acaba fuera de alcance (en `decoupling_prefail30s_t100s`, unos 85 s después) y
el coordinador deja de recibir sus vagones. Esos ciclos siguen siendo `LOST_INTEGRITY`. Usa `fin_s` solo si quieres
recortar el final.

---

## 9. Apagar

1. En el editor, **Stop** (o `Esc`) y cierra la ventana.
2. En la terminal 1 (la del contenedor), `exit`.
3. `docker stop carla-build-1`.

Si vas a hacer varios ensayos seguidos, no hace falta apagar: deja el editor en Play y repite los apartados 4, 6 y
7 cambiando `parent_dir`.

---

## 10. Si algo falla

| Síntoma | Causa y solución |
|---|---|
| `FileExistsError` al empezar | `parent_dir` ya existe. Cambia el nombre |
| `FileNotFoundError` al crear `parent_dir` | No existe la carpeta que la contiene (`Datasets_def/`), o se ha lanzado desde otra carpeta. Ver apartados 2 y 6 |
| `RuntimeError: time-out ... while waiting for the simulator` | El servidor no responde: el editor no está en Play, no está abierto o el contenedor está parado. Apartado 5 |
| Error al crear el tren (blueprint `vehicle.train...` no encontrado) | El servidor no es el de `carla-build-1` (p. ej., la versión binaria está lanzada) o no está abierto `railway4_simple` |
| El editor parece congelado tras un error de `main.py` | El servidor se quedó en modo síncrono esperando ticks. **Stop** y **Play** en el editor |
| `ModuleNotFoundError: No module named 'carla'` | No está activo `carla-ue4dev` (o VS Code usa otro intérprete). Apartado 1.3 |
| Aviso de versiones distintas de cliente y servidor | Apartado 2, punto 2 |
| `make: *** No rule to make target ...` | No estás en `/app/carla`. Apartado 5, punto 3 |
| `bash: export: 'DISPLAY:=0': not a valid identifier` | Sobran los dos puntos antes del `=`. Es `export DISPLAY=:0` |
| La ventana del editor no se abre al lanzar `make launch` | Falta `export DISPLAY=...` o el valor no coincide con el de `echo $DISPLAY` en el ordenador (apartado 5, puntos 1 y 3). Si aun así no abre, probar `xhost +local:` en el ordenador (fuera del contenedor) y repetir |
| Ventana de pygame en negro | Normal con `ENABLE_RENDER = False` |
| Puerto 2000 ocupado | Hay otro servidor CARLA lanzado (p. ej., `carla-etsii-map2`). Páralo con `docker stop` |

**Sobre `No module named 'carla.libcarla.command'`.** Aparece al ejecutar el ejemplo `generate_traffic.py` de
`~/carla_linux_build/carla/PythonAPI/examples`, que tiene en su línea 34
`from carla.libcarla.command import SpawnActor, ...`. `libcarla` es una librería compilada que lleva `command` como
atributo, y Python no lo puede importar con esa sintaxis. No afecta al simulador: `main.py` no la usa, y en el
mismo entorno `manual_control.py` funciona. Si algún día hace falta ese ejemplo, se cambia la línea por:

```python
SpawnActor = carla.command.SpawnActor
SetAutopilot = carla.command.SetAutopilot
FutureActor = carla.command.FutureActor
DestroyActor = carla.command.DestroyActor
```

---

## 11. Ficha de ensayo

Para copiar y rellenar en cada ensayo:

```
[ ] config.py sin cambios (o anotados: ...)
[ ] main.py: parent_dir = ...                  (nombre nuevo)
[ ] main.py: FAIL_TIME = ...   T_STOP = ...
[ ] main.py: evento activo = ...   enganche = ...   (t_prefail / t_fail_s = ...)
[ ] Terminal 1: docker start + exec, cd carla, echo $DISPLAY (fuera) → export DISPLAY=<ese valor> (dentro), make launch, railway4_simple, Play
[ ] Terminal 2: conda activate carla-ue4dev, cd simulador, python main.py
[ ] Consola: evento insertado y "Limpieza completada"
[ ] Carpeta con coord/, wagon_*/, ltd_log/ y config.json
[ ] Copiada a datasets_simulador/
[ ] Entrada en ensayos_simulador.json (evento_s = ...)
[ ] Exportado y revisado (guia_uso.md, apartado 4)
```

---

## 12. Anexo: crear un mapa nuevo

No hace falta para el TFM (`railway4_simple` basta), pero el procedimiento es:

1. **Diseñar el trazado en RoadRunner** (MathWorks, con licencia) y exportar dos archivos:
   - `.xodr` (OpenDRIVE): la lógica de la vía (carriles, conexiones, sentidos);
   - `.fbx`: la geometría 3D.
2. **Copiarlos a `~/carla_linux_build/carla/Import`** (`/app/carla/Import` dentro del contenedor).
3. **Importar**, dentro de `carla-build-1`:
   ```bash
   make import
   ```
   El mapa queda en `Unreal/CarlaUE4/Content/map_package/Maps/<nombre>`.
4. **Abrirlo**: `make launch` → **File → Open Level** → el mapa.
5. (Opcional) La navegación de peatones del PDF *Abrir un mapa en Unreal* no hace falta para el tren.

Para que sirva al simulador del tren:
- CARLA no conoce las vías de tren: la vía es una **carretera** del `.xodr` y el tren es un vehículo que el
  simulador recoloca en cada tick a lo largo de ella. El trazado debe ser un único carril continuo, mejor cerrado:
  400 s a 25 m/s son unos 10 km.
- Hay que elegir un **punto de aparición** sobre ese trazado y ponerlo en `config.py` → `train.positions` (se
  consultan con `visualize_map.py`).
- Ese mapa debe estar abierto y en Play antes de lanzar `main.py`.
- Antes de usarlo, conviene revisar `track/track_geometry.py` y `utils/spawner.py` para confirmar que no dependen
  de nada propio de `railway4_simple`.

Desde la compilación con editor también se puede generar un binario propio (`make package`) con el mapa y el tren,
para lanzar ensayos sin abrir el editor. Solo compensa si el editor se vuelve un cuello de botella.
