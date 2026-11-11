# Registro de cambios: optimización de envíos, informe de modo modelo y procesado de datos

Registro de los cambios de la librería, del firmware y de los scripts de procesado, con su motivo, para documentarlos
en la memoria.
Rama: `clasificador-estados` (las longitudes de mensaje también están en `main`).

---

## 1. Mensajes con su longitud real

**Problema.** Todos los mensajes UWB se enviaban con `LEN_DATA` (90 bytes), lleven lo que lleven: un ACK útil de
10 bytes ocupaba 92 en el aire (90 + 2 de CRC), con restos de mensajes anteriores detrás. A 110 kbps con preámbulo de
2048 símbolos, una trama de 92 bytes tarda unos 10 ms frente a unos 3,5 ms de una de 16 bytes.

**Cambios.**
- `transmit()` recibe la longitud real (por defecto `LEN_DATA`, para poder migrar mensaje a mensaje) y usa el buffer que
  recibe como parámetro (antes ignoraba el parámetro y usaba siempre `data`).
- Red de seguridad en `transmit()`: una longitud 0 o mayor que `LEN_DATA` no se envía y se avisa.
- Cada `transmitXxx()` pasa su longitud, comentada campo a campo: `// Length: [MAC (9)][type (1)][cycle_id (1)]`.

| Mensaje | Bytes |
|---|---|
| ACK, STOP, REQUEST_DATA, RANGE_FAILED... | 10 |
| POLL_ACK, MODE_SWITCH_ACK | 11 |
| TOKEN_HANDOFF | 12 (con el tipo de informe, ver 3) |
| BLINK | 12 |
| MODE_SWITCH | 13 |
| POLL (unicast) | 16 |
| RANGING_INIT | 17 |
| RANGE_REPORT | 18 |
| RANGE (unicast) | 28 |
| POLL / RANGE broadcast | 12 + 4n / 11 + 17n |
| Informes | Variable (la posición final del mensaje) |

- POLL y RANGE en broadcast comprueban **antes de escribir** que caben. Antes, un RANGE en broadcast con 5 o más
  dispositivos (17 bytes por dispositivo) escribía fuera del buffer.
- Recepción: se lee solo la longitud real de la trama (nueva `DW1000.getReceivedDataLength()`, que, a diferencia de
  `getDataLength()`, no depende de si el chip está en modo TX o RX) y el resto del buffer se pone a cero.
- `transmitInit()` pone el buffer a cero antes de construir cada mensaje.
- Como el buffer `data` es compartido entre envío y recepción, nodo y coordinador leen el informe recibido **antes** de
  enviar su ACK (antes funcionaba solo porque el ACK pisaba los 10 primeros bytes y el informe se leía desde el byte 10).
- Informe local: distancia saturada al rango de `uint16` y comentario del tamaño corregido.
- El `cycle_id` (POLL_ACK, TOKEN_HANDOFF) queda documentado como reservado para el Mesh Token Passing: en el Chain
  Token Passing vale siempre 0 y no se usa.

**Verificación.** En PC (librería compilada con stubs de Arduino): los 20 tipos de mensaje se envían con su longitud y
sin ningún byte distinto de cero detrás. **Pendiente:** medir en las placas la duración del ciclo antes y después
(estimación teórica: del orden de 300 ms menos por ciclo con 5 dispositivos).

**Pendiente para más adelante.** Probar a bajar `DEFAULT_REPLY_DELAY_TIME` (8 ms), probablemente sobredimensionado
porque cada trama tardaba ~10 ms.

---

## 2. Selección de variables del modelo (análisis previo)

Resumen de la decisión que motiva el nuevo informe (detalle en el análisis de columnas):
- Variables del modelo, por enlace: desviación, diferencia de aceleración longitudinal (`dif_acel_x`), visto y ciclos
  sin ver (16 columnas, frente a 37).
- Fuera del modelo: las 20 `dev_media` (empeoran la detección por sobreajuste) y la duración del ciclo (comportamiento
  opuesto en simulador y placas; se sigue registrando por si se unifica su criterio).
- De la IMU solo hacen falta `acel_x` (y se envía `acel_y` por si se necesita en el futuro).

---

## 3. Informe de modo modelo (`DATA_REPORT_MODEL`)

**Motivo.** Enviar solo lo que necesita el modelo y recuperar la redundancia: cada nodo informa de su medida con su
hijo **y con su nieto**. Así un nodo caído (su nieto sigue alcanzable saltándolo) se distingue de una rotura (se pierde
todo lo que cruza el corte).

**Formato.**
```
[MAC (9)][tipo (1)][N (1)]
+ N bloques, en orden de cadena: [dirección (1)][control (1)][hijo: dest (1) + dist (2)][nieto: dest (1) + dist (2)][ax (2)][ay (2)]
```
- Byte de control: bits 0-1 = número de medidas del bloque (0, 1 o 2); bits 2-7 reservados (0).
- Bloque: 12 bytes (hijo y nieto), 9 (solo hijo) o 6 (cola).
- Con 5 dispositivos (coordinador + 4 nodos): **50 bytes**, frente a 82 del informe agregado (sin redundancia).
- Máximo con `LEN_DATA` = 90: 6 bloques completos (8 dispositivos).

| Paso (5 dispositivos) | Bytes |
|---|---|
| Informe agregado: hijo + RX Power, IMU de 6 ejes | 82 |
| IMU de 6 a 2 ejes (ax, ay) | 50 |
| Sin RX Power | 44 |
| Bloque por nodo (el origen de cada medida es el dueño del bloque), con byte de control | 44 |
| + medida con el nieto | 50 |

**Librería.**
- Nuevo tipo `DATA_REPORT_MODEL` (17) y callback `attachModelDataReport()`.
- `NodeBlock`: el bloque de un nodo con sus valores ya decodificados (metros, m/s²). La librería codifica y decodifica;
  el `.ino` trabaja solo con structs.
- `transmitModelDataReport(bloques, n, destino)`: calcula y comprueba la longitud antes de escribir; no envía bloques
  con más de 2 medidas.
- `readModelDataReport(informe, salida, max)`: lee bloque a bloque con el byte de control; nunca lee más allá de la
  longitud recibida; devuelve -1 si el informe está mal formado.
- `Measurement.hop`: 1 = medida con el hijo, 2 = con el nieto, 0 = medida propia.
- Tipo de informe: lo fija el coordinador (`setReportType()`), viaja en cada TOKEN_HANDOFF (+1 byte) y cada nodo lo
  toma del testigo que recibe y lo reenvía con el suyo. Un tipo desconocido se sustituye por el de modo modelo, con aviso.
- Los informes local y agregado se eliminan (ver apartado 4).

**Verificación (PC).** Envío y lectura de informes con 0, 1 y 2 medidas por bloque (valores leídos iguales a los
enviados), saturación de distancias y aceleraciones, informes cortados o mal formados (-1 sin leer fuera), informe que
no cabe (no se envía) y tipo de informe en el TOKEN_HANDOFF.

**Firmware.**
- Coordinador: `#define REPORT_TYPE` (por defecto `DATA_REPORT_MODEL`) y `setReportType(REPORT_TYPE)` al arrancar: es
  el único sitio donde se elige el informe, y viaja con el testigo a toda la cadena.
- Nodo: `buildReport()` construye el informe del tipo que llegó con el testigo (por ahora solo `buildModelReport()`). En el de modo modelo, su bloque va delante de los recibidos del hijo, que se reenvían sin
  modificar. Los reintentos reenvían la misma copia.
- Nodo: el nieto es el hijo que trae el bloque del hijo (el primero recibido); se usa la medida propia con él si se
  midió en el ciclo. `readImu()` lee la aceleración en m/s² (ceros mientras no haya sensor).
- Nodo y coordinador: las comprobaciones comunes del informe recibido (remitente esperado, informe repetido, informe
  ya no esperado) pasan a `acceptIncomingReport()`, compartida por los callbacks de los dos tipos de informe.
- Coordinador: cada bloque se convierte en sus medidas (`hop` 1 y 2, sin RX Power); su ax y ay se toman del bloque al
  construir la fila en crudo.
- **Composición: ignora las medidas con `hop = 2`.** `buildObservedChain()` avanza por la primera medida activa que
  sale del último nodo hacia uno que no esté en la cadena: con las medidas del nieto encontraría atajos. Ejemplo
  comprobado: si el sistema arranca con B2 caído, B1-B3 entra en la lista como medida con el hijo, por delante de
  B1-B2; cuando B2 vuelve, sin el filtro la cadena observada es C1 > B1 > B3 > B4 > B2.
- Salidas: el informe legible marca cada medida como hijo o nieto, omite la RX Power cuando el informe no la trae y
  muestra los bloques recibidos; la fila en crudo deja vacía la RX Power que no llega.
- `AI_DEPLOYMENT` desactivado hasta reentrenar el modelo con las nuevas entradas.

**Verificación del firmware (PC).** Nodo y coordinador compilan con stubs de Arduino. Simulación de tres ciclos con el
código real de `Node.ino` (4 nodos) y `Coordinator.ino`, cada uno en su propio espacio de nombres:
1. Arranque con B2 caído: B1 salta a B3 (hijo B3, nieto B4); composición observada C1 > B1 > B3 > B4.
2. B2 vuelve: informe de 50 B; B1 con hijo B2 y nieto B3 (el hijo de su hijo, no B4 aunque también lo mida);
   composición C1 > B1 > B2 > B3 > B4 (sin el filtro de `hop = 2`, falla).
3. B2 no alcanza a su nieto: su bloque va solo con el hijo; informe de 47 B; composición correcta.

**Pendiente:** reentrenar el modelo con las 16 columnas (el formato en crudo, los scripts y las 16 columnas en el
coordinador están en el apartado 5).

**Decisiones de diseño.**
- El nieto es **el hijo del hijo**, sacado del informe que llega del hijo (no el segundo más cercano, que puede fallar
  en curvas o con nodos juntos).
- Las medidas del nieto se usan para saber si un dispositivo está alcanzable; **no se les calcula valor característico
  por ahora**. Su distancia se graba en el crudo para evaluar más adelante si aporta.

**Posibles mejoras futuras.**
- Valor característico de las parejas a dos saltos (segunda medida de cada separación, rotura detrás de un nodo caído).
- Bits de estado en el byte de control (cola por tiempo agotado, IMU no válida, reintentos).
- Distintos tipos de informe según la funcionalidad (todas las medidas con RX Power, solo IMU, modelo...), con
  `readXxx()` para cada uno en la librería.


## 4. Eliminación de los informes local y agregado

El informe de modo modelo es ahora el único informe de datos. Se eliminan los dos anteriores, que ya no se usaban:

- `DATA_REPORT_LOCAL` (9): solo las medidas propias, pensado para la topología en estrella.
- `DATA_REPORT_AGGREGATED` (10): medidas con el hijo + RX Power y bloques de IMU de 6 ejes (82 B con 5 dispositivos).

Los códigos 9 y 10 quedan libres. `REQUEST_DATA` y `DATA_REPORT_ACK` se mantienen.

**Librería.** Fuera `transmitLocalDataReport()`, `transmitAggregatedDataReport()`, `readAggregatedImu()`, sus callbacks
(`attachLocalDataReport()`, `attachAggregatedDataReport()`), `ImuSample`, `IMU_BLOCK_LEN` y la codificación del giróscopo
(`encodeGyro()`, `decodeGyro()`, `IMU_GYRO_UNITS_PER_RADS`). Se mantienen `encodeAcc()` / `decodeAcc()`.
El tipo de informe en el TOKEN_HANDOFF se mantiene, con `DATA_REPORT_MODEL` como único tipo válido y por defecto: así,
añadir un tipo nuevo no cambia el formato del testigo.

**Nodo.** Fuera el callback del informe agregado, `buildAggregatedReport()`, la copia de medidas e IMU que se enviaba
(`report_measures`, `report_imu`), los bloques de IMU recibidos (`imu_received`, `storeReceivedImu()`) y `readOwnImu()`.
`readImu()` devuelve solo la aceleración.

**Coordinador.** Fuera el callback del informe agregado y la lista de muestras de IMU (`imu_received`). La fila en
crudo toma ax y ay de los bloques recibidos (`searchBlock()`) y del propio coordinador (`readImu()`); az y el giróscopo
quedan a 0 hasta rehacer el formato en crudo. El informe legible ya no tiene la sección de bloques de IMU (los bloques
de nodo ya muestran ax y ay).

**Verificación (PC).** Librería, nodo y coordinador compilan. Longitudes de todos los mensajes correctas (informe de
modo modelo con 4 nodos: 50 B); tipo desconocido o 9/10 en el testigo → modo modelo; simulación de los tres ciclos de
la cadena sin cambios en el resultado.


## 5. Formato en crudo nuevo, 16 columnas y reorganización del procesado de datos

### Formato en crudo (scripts y coordinador)

La fila en crudo (`formato_crudo.py` y `RAW_DATA` del coordinador) se adapta al informe de modo modelo:
- **Nuevo: pares a dos saltos** (`salto2_c_v2`, `salto2_v1_v3`, `salto2_v2_v4`), con `visto` y `distancia_m`. En las
  placas, `c_v2` es la medida del coordinador con v2 y los otros dos, las medidas de cada nodo con su nieto. El
  simulador no los da: quedan vacíos, también su `visto`. No son entrada del modelo; se graban para evaluar más
  adelante si aportan (valor característico a dos saltos, ver mejoras futuras).
- **IMU: solo `acel_x` y `acel_y`**, los ejes que viajan en el informe. Fuera `acel_z` y el giróscopo.
- Se mantienen la duración del ciclo y la potencia RX (solo `c_v1`: el informe de los nodos no la trae).
- 31 columnas, frente a 45.

### Columnas del modelo: de 37 a 16

`calculo_columnas.py` y su versión en C (`model_features.cpp`, `NUM_FEATURES = 16`): por cada enlace, `visto`,
`ciclos_sin_ver`, `desviacion_m` y `dif_acel_x_ms2`, según el análisis del apartado 2. Fuera la duración del ciclo y
las 20 `dev_media`. **Comprobación:** con los tres ensayos del simulador, las 16 columnas, el uso y la etiqueta de cada
ciclo coinciden exactamente con los de los datos de la versión anterior (diferencia máxima 0).

### Reorganización de carpetas

| Antes | Ahora |
|---|---|
| `scripts/` | `scripts_procesado/` |
| `datos_simulador/` (local) | `datasets_simulador/` (local, con un `LEEME.md` en Git) |
| `datos/` (procesados del simulador, todos los ensayos mezclados por tipo de archivo) | `datasets_procesados/simulador/<ensayo>/` |
| `datos placas/` (local) | `datasets_procesados/placas/<ensayo>/` (en Git) |
| `ensayos_simulador.json`, `ensayos_placas.json` en la raíz | Cada uno en la carpeta de su origen |
| `columnas_<ensayo>.csv` | `revision/revision_<ensayo>.csv` (mismo contenido; el nombre dice para qué sirve) |
| `info_<ensayo>.json` + `resumen.json` global + `resumen_<ensayo>.json` | Un único `resumen_<ensayo>.json` por ensayo |

Dentro de cada ensayo, siempre las mismas carpetas: `crudo/`, `revision/` y `edge_impulse/`, más su resumen. Motivo:
`datos/` no decía que solo tenía datos del simulador; el exportador guardaba de dos formas distintas según el origen;
y había tres archivos de resumen con información repetida.

Las grabaciones de las placas pasan a Git porque las de vía real son irrepetibles. Los ensayos completos del
simulador siguen fuera, porque pesan mucho y se pueden volver a pedir.

### Grabador

- **Rutas fijas:** graba siempre en `datasets_procesados/placas/<ENSAYO>/` (fuera `CARPETA_SALIDA` y los argumentos
  de terminal). Solo se cambia `ENSAYO` (y `PUERTO`).
- **Registro automático:** al terminar, apunta el ensayo en `ensayos_placas.json` (lo crea si no existe). Antes había
  que añadir la entrada a mano después de cada grabación.
- **Resumen:** crea `resumen_<ensayo>.json` con la fecha, una `descripcion` vacía para rellenar a mano y los datos de
  la grabación (antes, en `info_<ensayo>.json`).
- **Sobrescribir:** si el ensayo existe, pregunta (`1` = sí, se borra entero; `0` = no, y dice en qué línea está
  `ENSAYO`, buscándola en el propio script para que no se desfase).
- **Comprobaciones nuevas:** nombre válido (es nombre de carpeta) y `ensayos_placas.json` legible **antes** de empezar
  la prueba. Si al final no puede apuntar el ensayo, muestra la entrada para añadirla a mano (la grabación no se pierde).
- Las teclas de fase por defecto son `0` = OK y `1` = LOST_INTEGRITY.

### Exportador

- **`ORIGEN` y `ENSAYOS`** sustituyen a `REGISTRO`: `ORIGEN = 'PLACAS'` o `'SIMULADOR'`, y `ENSAYOS = [...]` o
  `'TODOS'`. Antes se reexportaban siempre todos los ensayos del registro. `'TODOS'` sigue haciendo falta cuando cambia
  el cálculo o un parámetro general.
- Una sola forma de guardar, igual para los dos orígenes (fuera `carpeta_salida` y `salida_junto_a_la_grabacion`).
- Conserva `fecha`, `descripcion` y `grabacion` del resumen y reescribe solo la parte `procesado`.
- En las placas, apunta en `ensayos_placas.json` los parámetros usados y las rutas de los resultados.
- **Entradas huérfanas:** si la carpeta de un ensayo se borró a mano, avisa y lo salta (no borra la entrada). Un error
  en un ensayo ya no detiene la exportación de los demás.

### Documentación

`readme.md` reescrito con la estructura y los formatos; `guia_uso.md` nuevo, con los pasos de cada caso.

**Verificación (PC).**
- Simulador: los tres ensayos exportados con la estructura nueva (recuentos idénticos: 147/305, 127/270, 337).
- Firmware: nodo y coordinador compilan; la simulación de la cadena imprime la cabecera y una fila `RAW_DATA` del
  formato nuevo (`c_v2` y `v1_v3` vistos, `v2_v4` no, porque B2 no tiene nieto en ese ciclo).
- Placas: con esa cabecera real del coordinador y 80 ciclos simulados (rotura en el ciclo 50 y un ciclo perdido), el
  grabador valida la cabecera, apunta el ensayo y crea el resumen; el exportador conserva la descripción y escribe dos
  tramos de LOST_INTEGRITY. Probados también: sobrescribir (`1`, `0` y respuesta inválida), entrada repetida (se
  sustituye, no se duplica), entrada huérfana, JSON mal escrito, nombre con espacios y cabecera antigua.

**Pendiente:** probar en las placas la grabación con el firmware nuevo; reentrenar el modelo (ver apartado 6) y volver
a activar `AI_DEPLOYMENT`.


## 6. Redundancia del nieto en el modelo y calibración dinámica

### Motivo

Desde el apartado 3, las medidas con el nieto llegan al coordinador y se graban, pero ninguna decisión las usaba: si
se perdía la medida directa V2–V3, el enlace quedaba sin ver aunque V1 siguiera viendo a V3 en su sitio. El modelo no
podía distinguir un nodo apagado o un fallo de radio (tren íntegro) de una rotura.

### Análisis previo (simulador con fallos inyectados)

En el simulador los pares a dos saltos no aportan información independiente: salen de las mismas distancias al
coordinador que los enlaces, y en las fases OK no se pierde ningún mensaje. Por eso se inyectaron, en las fases OK, los
fallos que la redundancia debe distinguir: **fallo de radio** de un enlace y **nodo apagado** (5–40 ciclos), más un 3 %
de medidas perdidas al azar. Red neuronal (20, 10), entrenando con una rotura + frenado y probando con la otra rotura,
5 semillas. Variantes comparadas: modelo sin nieto, «dar por vivo» (el nieto solo como visto) y desviación del nieto.

| | Sin nieto | Dar por vivo | Desviación del nieto |
|---|---|---|---|
| Falsas alarmas con nodo apagado (entrenando sin fallos) | 44,0 % | 24,1 % | 13,5 % |
| Detección de la rotura (entrenando con fallos) | 50,2 % | 94,9 % | 95,9 % |
| Detección con la medida directa perdida y el nieto viendo al vagón que se aleja | 1,4 % | 3,6 % | 78,7 % |

- Sin nieto, el modelo no distingue «nodo apagado» de «rotura»: o da falsas alarmas, o deja de detectar la mitad de
  las roturas.
- «Dar por vivo» es peligroso: un vagón que se aleja a la deriva pero sigue al alcance del nieto se da por vivo y la
  rotura pasa desapercibida. **Que alguien lo vea no significa que esté en su sitio**: hace falta la distancia.

### Decisión: las medidas con el nieto como columnas propias (16 → 25)

Por cada par a dos saltos (`c_v2`, `v1_v3`, `v2_v4`): `visto`, `ciclos_sin_ver` y `desviacion_m`, calculadas igual
que en los enlaces y cada una a partir de su propia medida. Se descartaron:
- **Repartir los pares entre los enlaces que cruzan** (una columna por enlace con la desviación del nieto): obliga a
  combinar dos medidas en una columna con una regla (máximo, mínimo…) que puede fallar; con el máximo, la alarma de
  una rotura se repartía también a los enlaces sanos vecinos.
- **Que `ciclos_sin_ver` del enlace no suba si el nieto lo ve:** bajaba las falsas alarmas entrenando sin ejemplos de
  fallos, pero pierde información (cuánto tiempo lleva perdida la medida directa) y depende de una tabla de qué par
  cruza qué enlace. Se prefieren columnas «puras» y que el modelo aprenda la relación: los datos con fallos (gemelo
  digital y placas) llegarán, y así el modelo no tendrá que cambiar. La decisión es reversible sin repetir pruebas,
  porque el crudo guarda todas las medidas.

### Calibración dinámica, igual que el coordinador

Antes, Python calibraba con una ventana fija (los 30 primeros ciclos) y el coordinador con las primeras 30 medidas de
cada pareja, cuando llegaran. Para los enlaces apenas se notaba, pero un nieto que solo se ve a partir de una curva no
tendría referencia en Python y sí en la placa: el entrenamiento no vería lo mismo que el modelo en la placa.
Ahora `calculo_columnas.py` usa la regla del coordinador para enlaces y pares: referencia provisional (mediana de las
medidas que lleva) a partir de 3 medidas y fija a las 30. Los ciclos descartados al principio no calibran.
Parámetros: `calibracion_primer_ciclo` y `calibracion_n_ciclos` se sustituyen por `calibracion_n_medidas` (30) y
`calibracion_min_medidas` (3).

**Avisos en lugar de saltar el ensayo.** Antes, un enlace sin ver en toda la ventana de calibración, o una fase no OK
dentro de ella, hacían que el ensayo no se exportara. Ahora ningún caso de calibración lo impide; se anotan en el
resumen y en la consola: calibración tardía (más de 10 ciclos después del inicio), incompleta (menos de 30 medidas),
sin referencia (menos de 3) y calibrada con ciclos que no son OK. El resumen guarda, por enlace y par, la referencia,
el número de medidas, desde qué ciclo y en qué ciclo quedó fija.

### Cambios

- `adaptador_simulador.py`: rellena los pares del simulador (`c_v2` directo; `v1_v3` y `v2_v4` por resta).
- `calculo_columnas.py`: 25 columnas y calibración dinámica.
- `exportar_edge_impulse.py`: avisos de calibración y nueva sección `calibracion` del resumen.
- Registros de ensayos y grabador: nuevos parámetros generales.
- Coordinador: `NUM_FEATURES = 25` y las columnas de los pares en `model_features`; `updateCompositionLinks()`
  calibra también los pares; `link_references.cpp` no reutiliza la referencia de un par en uso; el informe legible
  muestra los pares con su referencia.

**Verificación (PC).**
- Simulador reexportado: mismos recuentos; las 16 columnas de antes son idénticas a partir del ciclo 32 (antes solo
  cambian por la referencia provisional); referencias de los pares en torno a 42 m.
- **C frente a Python:** los tres crudos del simulador, y uno modificado (nieto que aparece en el ciclo 85, V2 apagado
  30 ciclos, enlace calibrado al final), pasados por el código del coordinador y por `calculo_columnas.py`: mismos
  nombres y orden, diferencia máxima 3·10⁻⁵ (redondeo de `float`).
- Firmware: nodo y coordinador compilan; la simulación de la cadena da 25 entradas, con los pares calibrando.
- Placas (grabación simulada): un par que empieza a verse durante una fase LOST da sus dos avisos y se exporta.

**Pendiente:** ensayos con nodos apagados y fallos de radio con el tren íntegro (gemelo digital y placas), sin los que
el modelo no puede aprender la redundancia; reentrenar con las 25 columnas y activar `AI_DEPLOYMENT`.


## 7. Composición a partir del orden del informe

**Antes.** `buildObservedChain()` reconstruía la cadena siguiendo las medidas de hijo en hijo: tenía que saltarse
las medidas del nieto (`hop = 2`, que harían un atajo B1 → B3), las del propio coordinador y no fiarse del sentido
de cada medida.

**Ahora.** El informe de modo modelo ya llega en el orden del tren, porque cada nodo pone su bloque delante de los
que recibe: `[B1][B2][B3][B4]`. Al leer el informe, `modelDataReport()` pasa ese orden a una lista de direcciones
(`chain_order`) y la composición la copia tal cual. Si el último bloque tiene hijo, se añade al final: ese nodo
existe aunque su bloque no haya llegado (informe cortado a mitad), igual que con el método anterior.

**Separación.** La composición (`composition.cpp`) solo recibe la lista ordenada y no depende del formato del
informe. Regla para cualquier tipo de informe futuro: debe permitir obtener los nodos en el orden de la cadena, y
su callback debe rellenar `chain_order`.

**De paso.**
- Si un informe llega mal formado, se vacían los bloques y el orden: `readModelDataReport()` puede haber escrito
  parte de ellos antes de detectar el error.
- El aviso «provisional» de la predicción del modelo tiene en cuenta también las referencias de los pares a dos
  saltos, no solo las de los enlaces.

**Verificación (PC).** Simulación de la cadena con la misma composición observada que antes en sus tres ciclos (B2
caído, tren íntegro, B2 sin nieto). Casos límite de `buildObservedChain()`: informe completo, cortado (con el hijo
del último bloque), sin informe con y sin hijo del coordinador, y dirección repetida. Nodo y coordinador compilan.


## 8. Fila en crudo con el formato del informe de modo modelo

**Motivo.** El criterio de la fila en crudo no era uniforme: guardaba la potencia RX de los 4 enlaces, aunque solo
existía para c_v1 (las otras 3 columnas iban siempre vacías, en las placas y en el simulador), mientras que la IMU se
había recortado a ax y ay. La fila en crudo existe para generar los datos del modelo, así que pasa a llevar lo que
trae el informe de modo modelo.

**Contenido de la fila en crudo (27 columnas, antes 31):** distancias y `visto` de enlaces y pares a dos saltos, y
ax/ay de cada posición (lo que trae el informe), más la duración del ciclo (la mide el coordinador). Fuera la
potencia RX: no viaja en el informe y no se usa. Se mantienen `acel_y` y la duración del ciclo, que el modelo no
usa, por si entran en el futuro.

**Barrera por tipo de informe.** `USE_TINYML_MODEL` (`REPORT_TYPE == DATA_REPORT_MODEL`, en `Coordinator.ino`).
Solo con el informe de modo modelo se imprime `RAW_HEADER` en `setup()`, se construyen la fila en crudo y las
entradas del modelo en `processCycle()`, se imprimen `RAW_DATA` y las entradas, y se ejecuta el modelo
(`showAIPrediction()`). Con otro tipo de informe no se genera nada de esto y el modelo no se usa. La composición y
las referencias siguen funcionando siempre, porque alimentan también el informe legible. El grabador avisa a los 5 s
si no le llega la cabecera.

**Ampliación.** La constante pasa a llamarse `USE_TINYML_MODEL`. Las referencias de los enlaces y pares
(`updateCompositionLinks()`) entran también en la barrera: fuera del modelo solo servían para mostrar la referencia y
los ciclos sin ver en el informe legible, que con otro tipo de informe deja de mostrarlos (`showComposition()`). La
ejecución del modelo (`showAIPrediction()`) pasa de `END_CYCLE` a `processCycle()`, justo después de calcular sus
entradas, y sigue necesitando además `AI_DEPLOYMENT`.

**Cambios.** `formato_crudo.py` y `adaptador_simulador.py` (sin potencia), `RawRow` sin `link_rx_power`,
`printRawHeader()` / `printRawRow()`, la barrera en `Coordinator.ino`, y el readme.

**Grabaciones anteriores.** Las grabadas con el formato anterior (con potencia) se siguen pudiendo exportar: el
cálculo de columnas lee cada columna por su nombre y las de potencia se ignoran (comprobado: mismas 25 columnas con
un crudo antiguo y con uno nuevo). Lo que ya no aceptaría el grabador es grabar con un firmware anterior.

**Verificación (PC).** Simulador reexportado (mismos recuentos). C frente a Python con los tres crudos: mismos
nombres y orden, diferencia máxima 3·10⁻⁵. Simulación de la cadena correcta, con la cabecera y la fila nuevas.
El coordinador compila con `REPORT_TYPE` distinto del de modo modelo. Grabador y exportador con la cabecera nueva.


## 9. Nombres de columnas en inglés

Para que el código y los datos que comparte con el firmware usen nombres estándar, las columnas pasan al inglés. Los
nombres de carpetas y archivos, los registros de ensayos (JSON), los resúmenes, las variables de configuración, los
comentarios y la documentación no cambian.

| Antes | Ahora |
|---|---|
| posiciones `c, v1…v4` | `c, w1…w4` (*wagon*) |
| `ciclo`, `tiempo_ms`, `duracion_ciclo_ms` | `cycle`, `time_ms`, `cycle_duration_ms` |
| `enlace_X_Y_…` / `salto2_X_Z_…` | `link_X_Y_…` / `hop2_X_Z_…` |
| `_visto`, `_distancia_m` | `_seen`, `_distance_m` |
| `X_acel_x_ms2`, `X_acel_y_ms2` | `X_acc_x_ms2`, `X_acc_y_ms2` |
| `_ciclos_sin_ver`, `_desviacion_m`, `_dif_acel_x_ms2` | `_unseen_cycles`, `_deviation_m`, `_acc_x_diff_ms2` |
| revisión: `uso`, `etiqueta` | `use`, `label` |
| usos: `entrenamiento`, `descartado_arranque`, `descartado_fin`, `sin_etiqueta` | `training`, `discarded_startup`, `discarded_end`, `unlabeled` |
| marcas: `desde_ciclo`, `tiempo_ms_coordinador`, `fase`, `hora_pc` | `from_cycle`, `coordinator_time_ms`, `phase`, `pc_time` |

Ejemplo: `enlace_v2_v3_ciclos_sin_ver` → `link_w2_w3_unseen_cycles`.

**Dónde.** Firmware: `printRawHeader()`, `RAW_POSITION_NAMES`, `RAW_IMU_AXES`, `FEATURE_NAMES` y `printFeaturesHeader()`.
Scripts: los nombres están definidos una sola vez en `formato_crudo.py` (crudo y marcas), `calculo_columnas.py`
(entradas del modelo) y `exportar_edge_impulse.py` (uso y etiqueta), y el resto los importa.

**Consecuencias.** Las grabaciones hechas con los nombres anteriores ya no se pueden exportar sin renombrar sus
columnas. Hay que cargar el firmware nuevo en el coordinador (el grabador comprueba la cabecera) y regenerar los
datos procesados del simulador.

**Verificación (PC).** Los tres ensayos del simulador reexportados dan exactamente los mismos valores, uso y etiqueta
que antes, con los nombres nuevos. La cabecera `RAW_HEADER` del coordinador coincide con `formato_crudo.py`. C frente a
Python: mismos nombres y orden en las 25 entradas, diferencia máxima 3·10⁻⁵. Grabación simulada: marcas con las
columnas nuevas, exportación con el aviso de calibración esperado.


## 10. Informe legible: un bloque por ciclo

**Antes.** Cada ciclo imprimía dos bloques separados: la composición (`showComposition()`, desde `processCycle()`) y,
después, el DATA REPORT (`showData()`, desde `END_CYCLE`). La composición salía antes del informe de su propio ciclo,
así que lo que aparecía justo debajo de un DATA REPORT era la composición del ciclo siguiente. Además, el DATA REPORT
mostraba su propio tiempo («Time since last print») y un «Cycle n/10» que era en realidad el contador de intentos de
descubrimiento, no el número de ciclo.

**Ahora.** Un único bloque por ciclo, `showCycle()`, llamado desde `processCycle()`:

```
=========================== CYCLE 125 | 1412 ms ===========================
 COMPOSITION    guardada y observada, comparación y evento
 LINKS          cada enlace y par con el nieto: visto, distancia y referencia
 DATA REPORT    medidas del ciclo, bloques de nodo, IMU recibida, medidas inactivas
============================================================================
RAW_DATA:125,...
```

- Un solo número de ciclo (el real) y una sola duración.
- `COMPOSITION_OUTPUT` controla las secciones COMPOSITION y LINKS; `READABLE_OUTPUT`, la sección DATA REPORT.
- La línea `RAW_DATA` sale justo debajo del bloque; el grabador la encuentra igual (filtra por el prefijo).
- Se eliminan `showComposition()`, `showData()` y la variable `last_shown_data_timestamp`.
