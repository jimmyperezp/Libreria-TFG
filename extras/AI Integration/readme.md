# Generación de datasets de entrenamiento a partir del simulador

## Uso

Requisitos: Python 3.9 o superior, con `pandas` y `numpy` (`pip install -r requirements.txt`).

1. Copia las carpetas de los ensayos del simulador dentro de `datos_simulador/`. De cada ensayo solo hacen falta `config.json`, `coord/rx_coord.csv` y `coord/imu_crd.csv`.
2. Revisa `ensayos_simulador.json`: en él están las rutas, el instante de cada evento y los parámetros.
3. Abre `scripts/exportar_edge_impulse.py`, pon `REGISTRO = '../ensayos_simulador.json'` en el bloque de configuración del principio y pulsa el botón de ejecutar de VS Code. Desde la terminal también funciona, pasando el registro como argumento: `python exportar_edge_impulse.py ../ensayos_simulador.json`.

Para añadir un ensayo nuevo basta con añadir una entrada en `ensayos_simulador.json`. No hay que tocar el código.

## Qué hay en cada carpeta

| Carpeta | Contenido |
|---|---|
| `scripts/formato_crudo.py` | Definición del CSV en crudo, el formato común al simulador y a las ESP32 |
| `scripts/adaptador_simulador.py` | Datos del simulador → CSV en crudo. Se puede ejecutar solo |
| `scripts/calculo_columnas.py` | CSV en crudo → las 37 columnas del modelo. Es el mismo cálculo para los dos orígenes |
| `scripts/exportar_edge_impulse.py` | Ejecuta la cadena completa y escribe los CSV para Edge Impulse |
| `datos/crudo/` | Un CSV en crudo por ensayo, más su archivo de composición |
| `datos/columnas/` | Todos los ciclos con las 37 columnas, su uso y su etiqueta. Sirve para revisar |
| `datos/edge_impulse/` | **Los CSV de entrenamiento**, uno por ensayo y etiqueta |
| `datos/revision/` | Figuras de revisión de las columnas principales |
| `datos/resumen.json` | Referencias de calibración, número de filas, ciclos descartados |

## Cadena de procesamiento

```
simulador ──adaptador──► CSV en crudo ──cálculo de columnas──► columnas + etiqueta ──► CSV Edge Impulse
ESP32 ─────grabador────► CSV en crudo ──(el mismo cálculo)───► ...
```

### 1. Adaptador: del simulador al CSV en crudo

- **Ciclo.** Una vuelta del round robin. Cada vagón tiene una ranura fija de 0,25 s y una vuelta completa dura 1 s. Los mensajes se asignan a su ciclo por el número de ranura, de modo que no depende de redondeos de tiempo.
- **Tiempo cero.** El inicio de la simulación, que es la referencia de los instantes de fallo del simulador (`FAIL_TIME`).
- **`tiempo_ms`** es el instante del último mensaje recibido en el ciclo. **`duracion_ciclo_ms`** va desde el inicio de la vuelta hasta el final de la última ranura con respuesta.
- **Enlace C–V1.** Distancia y potencia medidas por el coordinador a V1.
- **Enlaces entre vagones.** El simulador solo da distancias al coordinador, así que la separación se obtiene por resta: d(Vk) − d(Vk−1). Solo cuenta como visto si se recibieron los dos vagones.
- **Potencia RX.** Solo existe para C–V1. En los demás enlaces se deja vacía, porque el simulador no la da. No es una entrada del modelo.
- **IMU.** La de cada vagón viene en su mensaje. La del coordinador es la muestra de su propia IMU en el instante en que recibe a V1.
- **Lo no recibido** queda como celda vacía con `visto = 0`.
- **Último ciclo.** Se descarta si el registro termina a mitad de él.

### 2. Cálculo de columnas

Está documentado al principio de `calculo_columnas.py`. En resumen:

- **Calibración.** La distancia de referencia de cada enlace es la mediana de los ciclos 3 a 32. Se hace una sola vez y supone que el tren está íntegro al arrancar.
- **Por enlace:**
  - `visto`
  - `ciclos_sin_ver`, con tope en 10
  - `desviacion_m`, igual a distancia − referencia
  - `dif_acel_x_ms2`, igual a la aceleración longitudinal del vehículo trasero menos la del delantero
- **Por vagón:** desviación respecto a la media de los demás vehículos recibidos (locomotora incluida), en acel_y, acel_z, giro_x, giro_y y giro_z.
- **Lo no visto vale 0** en todas las columnas numéricas. Su ausencia ya la indican `visto` y `ciclos_sin_ver`.

### 3. Etiquetado y exportación

- **Ciclos 0–2: descartados.** Es el transitorio de arranque de la simulación, con aceleraciones de hasta −125 m/s².
- **Ciclos de calibración:** se incluyen como OK.
- **Etiqueta.** OK hasta el evento. A partir del primer ciclo que termina después del evento, `LOST_INTEGRITY`.
- **`fin_s`** recorta el final del ensayo cuando hace falta.
- **Formato para Edge Impulse:**
  - `tiempo_ms` es nominal y equiespaciado (ciclo × 1000 ms), porque Edge Impulse supone muestras equiespaciadas.
  - La duración real del ciclo va como entrada (`duracion_ciclo_ms`).
  - Los archivos se llaman `<ETIQUETA>.<ensayo>.csv`, así que Edge Impulse puede tomar la etiqueta del nombre del archivo.

## Ensayos y decisiones de etiquetado

| Ensayo | Evento | OK | LOST_INTEGRITY | Notas |
|---|---|---|---|---|
| desacople | Desacople V2–V3 a 150 s | ciclos 3–149 (147) | ciclos 150–454 (305) | Recortado desde 455 s: V3 y V4 vuelven a recibirse en los últimos segundos, un efecto del simulador sin sentido físico |
| prefallo | Rotura V2–V3 a 130 s | ciclos 3–129 (127) | ciclos 130–399 (270) | La degradación de 100–130 s se etiqueta OK: es unas 30 veces menor que el error UWB y no es detectable |
| frenado | Frenado a 300 s | ciclos 3–339 (337) | — | El frenado es operación normal: todo OK |

## Grabación de ensayos con las ESP32

`scripts/grabador_serie.py` registra un ensayo leyendo el monitor serie del coordinador. Es la versión nueva de `TinyML data collector.py`, adaptada al formato en crudo.

Requisito: `pip install pyserial`.

**Configuración:** al principio del script, en el bloque `CONFIGURACIÓN`:
- `ENSAYO`: nombre del ensayo (carpeta y archivos). Cámbialo en cada grabación: el script no sobrescribe una grabación existente.
- `CARPETA_SALIDA`: dónde se crea la carpeta del ensayo. Ruta absoluta o relativa a la carpeta del script. Por defecto, `extras/AI Integration/grabaciones/`.
- `PUERTO`, `BAUDIOS`, `FASE_INICIAL`, `TECLAS` (tecla → fase), `TECLA_SALIR`, `VER_MONITOR` y `NOTAS`.

**Ejecución:** con el botón de ejecutar de VS Code (*Run Python File*), o desde la terminal con argumentos opcionales que sustituyen a la configuración (`--ensayo`, `--carpeta`, `--puerto`, `--ver-monitor`, `--notas`). Con `REINICIAR_COORDINADOR = True` (por defecto), el grabador reinicia el coordinador al abrir el puerto, igual que el botón EN/RST, así que no hace falta cuadrar nada: la grabación empieza siempre en el ciclo 0, con su cabecera `RAW_HEADER` y con la ventana de calibración que usa el exportador. Si la placa no se reinicia sola, a los 5 s el grabador pide pulsar su botón EN/RST.

Durante la grabación:
- **Marcas de fase con teclas fijas** (por defecto `o` = OK y `l` = LOST_INTEGRITY), pulsadas en el terminal donde corre el script. La marca se guarda con el reloj del coordinador: la fase nueva empieza en el ciclo siguiente al último recibido. Al empezar se marca automáticamente `FASE_INICIAL`.
- **Terminar:** tecla `q` o Ctrl+C.

Archivos que deja en la carpeta del ensayo:

| Archivo | Contenido |
|---|---|
| `crudo_<ensayo>.csv` | Filas `RAW_DATA`, con el mismo formato que el CSV en crudo del simulador |
| `marcas_<ensayo>.csv` | Marcas de fase: desde qué ciclo, tiempo del coordinador, fase y hora del PC |
| `monitor_<ensayo>.txt` | Todo el monitor serie, con la hora del PC |
| `info_<ensayo>.json` | Puerto, commit del repositorio, notas y resumen (filas, ciclos perdidos, líneas corruptas) |

Protecciones:
- Comprueba que la cabecera `RAW_HEADER` del coordinador coincide con `formato_crudo.py`. Si no coincide, el firmware y los scripts son de versiones distintas y se detiene.
- Si el coordinador se reinicia a mitad de grabación (cabecera nueva o el ciclo retrocede), se detiene conservando lo grabado: el reloj vuelve a 0 y las marcas dejarían de cuadrar.
- Cuenta los ciclos perdidos y las líneas corruptas, y escribe los archivos línea a línea.

## Exportar las grabaciones de las ESP32

El mismo exportador convierte las grabaciones en CSV para Edge Impulse, con su propio registro de ensayos, `ensayos_esp32.json`. Cada grabación es una entrada de la lista `"ensayos"` que apunta al CSV en crudo y al archivo de marcas (hay un ejemplo en `"ejemplo_de_ensayo"`):

```json
{
  "nombre": "desconexion_v3",
  "crudo": "grabaciones/desconexion_v3/crudo_desconexion_v3.csv",
  "marcas": "grabaciones/desconexion_v3/marcas_desconexion_v3.csv"
}
```

Para exportar: en `scripts/exportar_edge_impulse.py`, `REGISTRO = '../ensayos_esp32.json'` (es el valor por defecto) y botón de ejecutar.

Los resultados van a `datos_esp32/`, separados de los del simulador.

- **Etiquetas a partir de las marcas:** cada ciclo lleva la fase de la última marca anterior o igual a él. Admite cualquier número de fases. Si una marca se pulsó tarde, se corrige esa línea del archivo de marcas y se vuelve a exportar.
- **Ciclos perdidos:** si los ciclos de una etiqueta no son consecutivos, se escribe un archivo por tramo (`OK.desconexion_v3_p1.csv`, `_p2`…), para que cada archivo sea equiespaciado. Los tramos quedan listados en `resumen.json`.
- **Calibración:** misma regla que con el simulador (mediana de la ventana de calibración), aquí desde el ciclo 0, como en la placa. Si falta algún ciclo de la ventana, se calcula con los que hay y se avisa.
- **`periodo_nominal_ms` = 1000**, igual que en el simulador, aunque los ciclos reales duren otra cosa: con ventanas de una fila no afecta al modelo, y así todos los datos del proyecto de Edge Impulse tienen la misma frecuencia. La duración real va en `duracion_ciclo_ms`.

## Diferencias conocidas con las ESP32

- **Duración del ciclo.** En el simulador la vuelta es fija: dura 1000 ms y baja a 500 ms cuando faltan V3 y V4. En las ESP32 dura unos 1400 ms y sube cuando hay fallos, por los tiempos de espera. Para probar en las placas un modelo entrenado con el simulador, conviene valorar si se quita esta columna.
- **IMU.** En las ESP32 viaja a ceros hasta que haya sensores, así que en las placas el modelo no recibe la información de la IMU con la que se entrenó. Los resultados en las placas sirven también para justificar la necesidad de añadir IMU a los dispositivos.
- **Distancias entre vecinos.** En el simulador salen por resta, así que el ruido de los enlaces es mayor que el de una medida directa.
