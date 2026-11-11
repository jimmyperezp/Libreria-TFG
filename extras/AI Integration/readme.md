# AI Integration: datasets para el modelo de integridad del tren

Scripts que convierten los ensayos (del simulador o grabados con las placas) en los CSV de
entrenamiento de Edge Impulse, y carpetas donde se guarda cada paso.

Hay tres documentos:

| Documento | Para qué |
|---|---|
| `readme.md` (este) | Qué hay en cada carpeta, qué contiene cada archivo y cómo se calcula cada cosa |
| `guia_uso.md` | Qué pasos seguir: grabar un ensayo, exportarlo, añadir un ensayo del simulador… |
| `registro_cambios.md` | Cambios hechos en el firmware y en los scripts, con sus motivos (para la memoria) |

Requisitos: Python 3.9 o superior con `pandas`, `numpy` y, para grabar, `pyserial`
(`pip install pandas numpy pyserial`).

## Estructura de carpetas

```
AI Integration/
├── readme.md
├── guia_uso.md
├── registro_cambios.md
│
├── scripts_procesado/
│   ├── formato_crudo.py            definición del CSV en crudo (común a simulador y placas)
│   ├── grabador_serie.py           PASO 1 (placas): monitor serie del coordinador -> crudo + marcas
│   ├── adaptador_simulador.py      PASO 1 (simulador): ensayo del simulador -> crudo
│   ├── calculo_columnas.py         PASO 2: crudo -> las 25 columnas del modelo
│   └── exportar_edge_impulse.py    PASOS 1-3: ejecuta la cadena y escribe los CSV de Edge Impulse
│
├── datasets_simulador/             ensayos completos tal como los da el simulador (NO se suben a Git)
│   ├── readme.md
│   ├── decoupling_w2_150s/         config.json, coord/rx_coord.csv, coord/imu_crd.csv...
│   └── ...
│
└── datasets_procesados/            todo lo que generan los scripts (NO se sube a Git: solo en local)
    ├── simulador/
    │   ├── ensayos_simulador.json  registro de ensayos del simulador (se escribe a mano)
    │   └── <ensayo>/               p. ej. desacople/
    │       ├── crudo/
    │       │   ├── crudo_<ensayo>.csv
    │       │   └── composicion_<ensayo>.json
    │       ├── revision/
    │       │   └── revision_<ensayo>.csv
    │       ├── edge_impulse/
    │       │   ├── OK.<ensayo>.csv
    │       │   └── LOST_INTEGRITY.<ensayo>.csv
    │       └── resumen_<ensayo>.json
    └── placas/
        ├── ensayos_placas.json     registro de ensayos de las placas (lo escriben los scripts)
        └── <ensayo>/               p. ej. prueba_04/
            ├── crudo/
            │   ├── crudo_<ensayo>.csv
            │   ├── marcas_<ensayo>.csv
            │   └── monitor_<ensayo>.txt
            ├── revision/
            │   └── revision_<ensayo>.csv
            ├── edge_impulse/
            │   ├── OK.<ensayo>.csv
            │   └── LOST_INTEGRITY.<ensayo>.csv
            └── resumen_<ensayo>.json
```

Ni `datasets_simulador/` ni `datasets_procesados/` se suben a Git (`.gitignore`): los datos viven solo en el PC
donde se graban y procesan. Las grabaciones de vía real son irrepetibles, así que conviene guardar una copia de
`datasets_procesados/placas/` fuera del repositorio.

Las rutas son fijas: los scripts las conocen y no hay que escribirlas en ningún sitio.
El nombre de cada ensayo es también el de su carpeta y el de sus archivos, así que solo puede
tener letras sin tildes, números, `_` y `-` (sin espacios). Los scripts lo comprueban.

## Cadena de procesamiento

```
                PASO 1                          PASO 2                       PASO 3
simulador ──adaptador_simulador──►┐
                                  ├─► crudo ──calculo_columnas──► 25 columnas ──etiquetado──► revision + edge_impulse + resumen
placas ─────grabador_serie───────►┘
```

- **Placas.** El paso 1 (grabador) se ejecuta durante la prueba, con la placa conectada. Los pasos 2 y 3
  los hace el exportador después, cuando se quiera.
- **Simulador.** El exportador hace los tres pasos seguidos.
- `formato_crudo.py` no es un paso: es la definición de las columnas del CSV en crudo, que comparten
  el grabador, el adaptador y el cálculo de columnas. La misma definición está en el firmware del
  coordinador (`printRawHeader()`), y el grabador comprueba al empezar que coinciden.

## Archivos de cada ensayo

| Archivo | Lo escribe | Contenido |
|---|---|---|
| `crudo/crudo_<ensayo>.csv` | grabador / adaptador | Una fila por ciclo con lo observado (formato abajo) |
| `crudo/marcas_<ensayo>.csv` | grabador (placas) | Una fila por marca de fase: `desde_ciclo`, `tiempo_ms_coordinador`, `fase`, `hora_pc` |
| `crudo/monitor_<ensayo>.txt` | grabador (placas) | Todo el monitor serie del coordinador, con la hora del PC en cada línea |
| `crudo/composicion_<ensayo>.json` | adaptador (simulador) | Qué dispositivo ocupa cada posición y cómo se generó el crudo |
| `revision/revision_<ensayo>.csv` | exportador | **Todos** los ciclos con las 25 columnas del modelo, más `uso` y `etiqueta`. Sirve para revisar el cálculo y el etiquetado, y para hacer figuras. No se sube a Edge Impulse |
| `edge_impulse/<ETIQUETA>.<ensayo>.csv` | exportador | **Los CSV de entrenamiento**: solo los ciclos de entrenamiento, un archivo por etiqueta |
| `resumen_<ensayo>.json` | grabador + exportador | El archivo del ensayo que se abre a mano (ver abajo) |

### `resumen_<ensayo>.json`

```json
{
  "fecha": "2026-11-10 10:32",
  "descripcion": "",
  "grabacion": { "puerto": "COM15", "commit_repositorio": "1ece0ae", "inicio": "...", "fin": "...",
                 "motivo_fin": "tecla de salida", "filas_crudo": 412, "ciclos_perdidos": 2,
                 "lineas_corruptas": 0, "marcas": 2, "cabecera_validada": true, ... },
  "procesado": { "ciclos_totales": 412, "descartados": {...}, "archivos_edge_impulse": {...},
                 "calibracion": { "c_v1": {"referencia_m": 21.47, "medidas": 30, "desde_ciclo": 0,
                                           "fijada_en_ciclo": 29}, ... },
                 "avisos_calibracion": [] }
}
```

- `fecha` y `grabacion`: los escribe el grabador al terminar (solo placas).
- `descripcion`: se rellena **a mano**, describiendo qué se hizo en el ensayo. El exportador no la toca.
  En el simulador se copia de `ensayos_simulador.json` y no hay `fecha` ni `grabacion`.
- `procesado`: lo escribe el exportador y lo reescribe en cada exportación. Contiene:
  - `ciclos_totales` y `descartados`: cuántos ciclos hay y cuántos no van a Edge Impulse, y por qué;
  - `archivos_edge_impulse`: por etiqueta, filas y ciclos (y los tramos, si hubo ciclos perdidos);
  - `calibracion`: para cada enlace y par a dos saltos, su referencia, con cuántas medidas se calculó,
    desde qué ciclo y en qué ciclo quedó fija (ver «Calibración» más abajo);
  - `avisos_calibracion`: los casos de calibración que conviene revisar.

## Registros de ensayos

### `datasets_procesados/placas/ensayos_placas.json`

Lo escriben los scripts. **Solo se edita a mano `"general"`**, que se aplica a lo que se exporte después.

```json
{
  "general": {
    "ciclos_descartados_inicio": 0,
    "calibracion_n_medidas": 30,
    "calibracion_min_medidas": 3,
    "tope_ciclos_sin_ver": 10,
    "periodo_nominal_ms": 1000
  },
  "ensayos": [
    {
      "nombre": "prueba_04",
      "crudo": "prueba_04/crudo/crudo_prueba_04.csv",
      "marcas": "prueba_04/crudo/marcas_prueba_04.csv",
      "resumen": "prueba_04/resumen_prueba_04.json",
      "parametros_usados": { "...": "copia de general en la última exportación" },
      "revision": "prueba_04/revision/revision_prueba_04.csv",
      "edge_impulse": "prueba_04/edge_impulse/"
    }
  ]
}
```

- El **grabador** crea el archivo si no existe (con los valores de arriba en `"general"`) y añade la
  entrada del ensayo al terminar la grabación (`nombre`, `crudo`, `marcas`, `resumen`). Si el ensayo se
  sobrescribe, su entrada se sustituye.
- El **exportador** añade a la entrada los parámetros con los que se exportó y las rutas de los resultados.
- Las rutas se cuentan desde la carpeta `placas/`.
- `calibracion_n_medidas`, `calibracion_min_medidas` y `tope_ciclos_sin_ver` deben coincidir con `LINK_REF_SAMPLES` (30),
  `LINK_REF_MIN_SAMPLES` (3) y `UNSEEN_CYCLES_CAP` (10) del coordinador (`link_references.h`).

### `datasets_procesados/simulador/ensayos_simulador.json`

Se escribe **a mano**: tiene lo que no se puede deducir de los datos del simulador.

```json
{
  "general": { "ciclos_descartados_inicio": 3, "calibracion_n_medidas": 30, "calibracion_min_medidas": 3,
               "tope_ciclos_sin_ver": 10, "periodo_nominal_ms": 1000 },
  "ensayos": [
    {
      "nombre": "desacople",
      "descripcion": "Desacople instantáneo del enganche V2-V3 a los 150 s",
      "carpeta_simulador": "decoupling_w2_150s",
      "evento_s": 150.0,
      "etiqueta_tras_evento": "LOST_INTEGRITY",
      "fin_s": 455.0
    }
  ]
}
```

- `carpeta_simulador`: nombre de la carpeta del ensayo dentro de `datasets_simulador/`.
- `evento_s`: segundo del evento (`null` si no hay). Hasta él, OK; después, `etiqueta_tras_evento`.
- `fin_s`: a partir de ese segundo los ciclos se descartan (`null` para no recortar).

## Formato del CSV en crudo

Una fila por ciclo. Columnas fijas para 5 vehículos, **por posición** en el tren: el coordinador (`c`)
y cuatro vagones (`v1`…`v4`). Definido en `formato_crudo.py`.

| Columnas | Contenido |
|---|---|
| `ciclo`, `tiempo_ms`, `duracion_ciclo_ms` | Número de ciclo, instante de fin (reloj del coordinador o del simulador) y duración |
| `enlace_X_Y_visto`, `_distancia_m` | Por cada enlace entre vehículos consecutivos (`c_v1`, `v1_v2`, `v2_v3`, `v3_v4`) |
| `salto2_X_Z_visto`, `_distancia_m` | Por cada par a dos saltos, es decir, cada vehículo con su nieto (`c_v2`, `v1_v3`, `v2_v4`) |
| `X_acel_x_ms2`, `X_acel_y_ms2` | Por cada vehículo: aceleración longitudinal y lateral |

- **Contenido:** lo que trae el informe de modo modelo (las distancias de cada nodo con su hijo y su nieto, y
  ax/ay), más la duración del ciclo, que mide el coordinador. La fila en crudo solo se genera con ese informe
  (`USE_TINYML_MODEL` en el coordinador). No hay potencia RX: no viaja en el informe y no se usa.
- Lo no recibido es una celda vacía, con `visto = 0`.
- **Pares a dos saltos:**
  - en las placas, `c_v2` es la medida del coordinador con v2, y `v1_v3` y `v2_v4` son las medidas de cada
    nodo con su nieto que llegan en el informe de modo modelo;
  - en el simulador, `c_v2` es la distancia del coordinador a v2, y `v1_v3` y `v2_v4` salen por resta
    (ver el adaptador, más abajo).
- La IMU solo lleva `acel_x` y `acel_y`, que son los ejes que viajan en el informe. Mientras las placas
  no tengan IMU, valen 0.

## Columnas del modelo (25)

En este orden (`calculo_columnas.py`, y la misma versión en C en `model_features.cpp` del coordinador).

**Por cada enlace** (`c_v1`, `v1_v2`, `v2_v3`, `v3_v4`), 4 columnas:

| Columna | Cálculo |
|---|---|
| `enlace_X_Y_visto` | 1 si se recibió la medida directa del enlace en el ciclo |
| `enlace_X_Y_ciclos_sin_ver` | Ciclos seguidos sin recibirla (0 si visto; tope en `tope_ciclos_sin_ver`) |
| `enlace_X_Y_desviacion_m` | Distancia − distancia de referencia del enlace (0 si no visto o sin referencia) |
| `enlace_X_Y_dif_acel_x_ms2` | `acel_x` del vehículo trasero − la del delantero (0 si no visto) |

**Después, por cada par a dos saltos** (nieto: `c_v2`, `v1_v3`, `v2_v4`), 3 columnas:

| Columna | Cálculo |
|---|---|
| `salto2_X_Z_visto` | 1 si se recibió la medida del par en el ciclo |
| `salto2_X_Z_ciclos_sin_ver` | Ciclos seguidos sin recibirla (tope en `tope_ciclos_sin_ver`) |
| `salto2_X_Z_desviacion_m` | Distancia − distancia de referencia del par (0 si no visto o sin referencia) |

- **Cada columna sale de una sola medida:** no se combinan enlaces y pares. Es el modelo el que aprende, por
  ejemplo, que «V2–V3 no se ve, pero V1–V3 sí y en su sitio» significa tren íntegro (redundancia del nieto).
  Para aprenderlo necesita ejemplos de enlaces perdidos con el tren íntegro (nodos apagados, fallos de radio).
- **No entran al modelo**, pero se graban en el crudo por si entran en el futuro: la duración del ciclo
  y `acel_y`. El porqué de esta selección está en `registro_cambios.md` (apartados 2 y 6).

### Calibración (distancia de referencia)

La misma regla para enlaces y pares, y la misma que usa el coordinador (`link_references.cpp`):

- Cada enlace o par se calibra con sus **primeras medidas, cuando lleguen**. Un nieto que solo se ve a partir de
  una curva, o un nodo que entra en la cadena a mitad de ensayo, se calibra en ese momento.
- Mientras calibra, la referencia es la mediana de las medidas que lleva (incluida la del ciclo). Con menos de
  `calibracion_min_medidas` (3) no se usa y la desviación vale 0.
- Con `calibracion_n_medidas` (30), la referencia queda **fija** hasta el final del ensayo.
- Los ciclos anteriores a `ciclos_descartados_inicio` no se usan para calibrar (en el simulador, el transitorio de
  arranque; en las placas es 0).
- Hipótesis: cuando una pareja empieza a verse, el tren está íntegro.

Ningún caso de calibración impide exportar el ensayo. Los que conviene revisar se anotan en `avisos_calibracion`
del resumen y en la consola:

| Aviso | Qué significa |
|---|---|
| `v1_v3: calibrado a partir del ciclo 85` | Empezó a verse más de 10 ciclos después del inicio. En un par puede ser normal; en un enlace suele ser un nodo que entró o se reinició a mitad de ensayo |
| `v2_v4: solo 12 de 30 medidas de calibración; referencia provisional` | No llegó a 30 medidas: su referencia es provisional (menos fiable) en todo el ensayo |
| `v3_v4: sin referencia en todo el ensayo (0 medidas de calibración)` | Menos de 3 medidas: su desviación vale 0 en todo el ensayo. En un enlace casi siempre indica un problema |
| `v1_v3: 8 de sus 30 medidas de calibración son de fase LOST_INTEGRITY` | Se calibró, en parte, con el tren no íntegro: su referencia y sus desviaciones pueden estar mal |

## Etiquetado y exportación

Cada ciclo recibe un **uso** y una **etiqueta**, que se ven en el archivo de revisión.

| Uso | Ciclos |
|---|---|
| `descartado_arranque` | Los primeros `ciclos_descartados_inicio` (en el simulador, 0–2: transitorio de arranque con aceleraciones de hasta −125 m/s²) |
| `descartado_fin` | Desde `fin_s` (solo simulador) |
| `sin_etiqueta` | Antes de la primera marca (solo placas) |
| `entrenamiento` | El resto. **Solo estos van a Edge Impulse** |

- **Etiqueta en el simulador:** OK hasta el evento. Desde el primer ciclo que termina después de `evento_s`, `etiqueta_tras_evento`.
- **Etiqueta en las placas:** cada ciclo lleva la fase de la última marca anterior o igual a él. Admite
  cualquier número de fases. Si una marca se pulsó tarde, se corrige su línea en el archivo de marcas y se reexporta.
- **Formato para Edge Impulse:**
  - Primera columna, `tiempo_ms`: nominal y equiespaciado (ciclo × `periodo_nominal_ms`), porque Edge Impulse
    supone muestras equiespaciadas. Con ventanas de una fila no afecta al modelo, y así todos los datos del
    proyecto tienen la misma frecuencia.
  - Después, las 25 columnas, en el orden de las tablas anteriores.
  - Nombre `<ETIQUETA>.<ensayo>.csv`: Edge Impulse toma la etiqueta de lo que va antes del primer punto.
  - Si los ciclos de una etiqueta no son consecutivos (ciclos perdidos o fases repetidas), un archivo por
    tramo: `OK.<ensayo>_p1.csv`, `_p2`… Los tramos quedan en el resumen.
  - Antes de escribir, se borran los CSV de Edge Impulse anteriores del ensayo.

## Adaptador del simulador: cómo se traduce cada ensayo

- **Ciclo.** Una vuelta del round robin. Cada vagón tiene una ranura fija de 0,25 s y una vuelta dura 1 s. Los
  mensajes se asignan a su ciclo por el número de ranura, así que no depende de redondeos de tiempo.
- **Tiempo cero.** El inicio de la simulación, que es la referencia de los instantes de fallo del simulador.
- **`tiempo_ms`** es el instante del último mensaje recibido en el ciclo. **`duracion_ciclo_ms`** va desde el inicio
  de la vuelta hasta el final de la última ranura con respuesta.
- **Enlace c–v1.** Distancia medida por el coordinador a v1.
- **Enlaces entre vagones.** El simulador solo da distancias al coordinador, así que la separación se obtiene por
  resta: d(vk) − d(vk−1). Solo cuenta como visto si se recibieron los dos vagones.
- **Pares a dos saltos.** `c_v2` es la distancia del coordinador a v2. `v1_v3` y `v2_v4` salen por resta, igual que
  los enlaces: d(v3) − d(v1) y d(v4) − d(v2), y solo cuentan como vistos si se recibieron los dos vagones.
- **IMU.** La de cada vagón viene en su mensaje. La del coordinador es la muestra de su propia IMU en el instante
  en que recibe a v1. Solo se pasan `acel_x` y `acel_y`.
- **Último ciclo.** Se descarta si el registro termina a mitad de él.

## Grabador: protecciones

- Antes de empezar comprueba que el nombre del ensayo es válido y que `ensayos_placas.json` se puede leer.
- Si el ensayo ya existe, pregunta si se sobrescribe (`1` = sí, se borra entero; `0` = no, y dice en qué línea
  está la variable `ENSAYO`).
- Con `REINICIAR_COORDINADOR = True` reinicia el coordinador al abrir el puerto, igual que el botón EN/RST, para que
  la grabación empiece en el ciclo 0 con su cabecera y su calibración. Si la placa no se reinicia sola, a los 5 s
  pide pulsar EN/RST.
- Comprueba que la cabecera `RAW_HEADER` del coordinador coincide con `formato_crudo.py`. Si no, firmware y scripts
  son de versiones distintas y se detiene.
- Si el coordinador se reinicia a mitad de grabación (cabecera nueva o el ciclo retrocede), se detiene conservando
  lo grabado.
- Cuenta los ciclos perdidos y las líneas corruptas, y escribe los archivos línea a línea.
- Si al terminar no puede apuntar el ensayo en el registro, la grabación no se pierde: muestra la entrada para
  añadirla a mano.

## Ensayos del simulador y decisiones de etiquetado

| Ensayo | Evento | OK | LOST_INTEGRITY | Notas |
|---|---|---|---|---|
| desacople | Desacople V2–V3 a 150 s | ciclos 3–149 (147) | ciclos 150–454 (305) | Recortado desde 455 s: V3 y V4 vuelven a recibirse en los últimos segundos, un efecto del simulador sin sentido físico |
| prefallo | Rotura V2–V3 a 130 s | ciclos 3–129 (127) | ciclos 130–399 (270) | La degradación de 100–130 s se etiqueta OK: es unas 30 veces menor que el error UWB y no es detectable |
| frenado | Frenado a 300 s | ciclos 3–339 (337) | — | El frenado es operación normal: todo OK |

## Diferencias conocidas entre el simulador y las placas

- **IMU.** En las placas viaja a ceros hasta que haya sensores, así que el modelo no recibe la información de
  aceleración con la que se entrenó. Los resultados en las placas sirven también para justificar la necesidad de
  añadir IMU a los dispositivos.
- **Distancias entre vecinos.** En el simulador salen por resta, así que el ruido de los enlaces es mayor que el de
  una medida directa.
- **Pares a dos saltos.** En el simulador salen de las mismas distancias al coordinador que los enlaces, así que no
  aportan información independiente: el par V1–V3 se pierde a la vez que V2–V3. En las placas son medidas de radio
  independientes, que es justo lo que aporta la redundancia. Para entrenarla hacen falta ensayos con nodos apagados
  y fallos de radio con el tren íntegro (del gemelo digital o de las placas).
- **Duración del ciclo.** En el simulador la vuelta dura 1000 ms fijos (500 ms cuando faltan V3 y V4); en las placas,
  unos 1400 ms, y más cuando hay fallos. Ya no es una entrada del modelo, pero se sigue grabando.
