# Registro de cambios: optimización de envíos e informe de modo modelo

Registro de los cambios de la librería y del firmware, con su motivo, para documentarlos en la memoria.
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
- `Measurement.hop`: 1 = medida con el hijo, 2 = con el nieto, 0 = propia o del informe antiguo.
- Tipo de informe: lo fija el coordinador (`setReportType()`), viaja en cada TOKEN_HANDOFF (+1 byte) y cada nodo lo
  toma del testigo que recibe y lo reenvía con el suyo. Un tipo desconocido se sustituye por el agregado, con aviso.
- El informe agregado se mantiene sin cambios (se rehará cuando se definan los distintos tipos de informe).

**Verificación (PC).** Envío y lectura de informes con 0, 1 y 2 medidas por bloque (valores leídos iguales a los
enviados), saturación de distancias y aceleraciones, informes cortados o mal formados (-1 sin leer fuera), informe que
no cabe (no se envía) y tipo de informe en el TOKEN_HANDOFF.

**Firmware.**
- Coordinador: `#define REPORT_TYPE` (por defecto `DATA_REPORT_MODEL`) y `setReportType(REPORT_TYPE)` al arrancar: es
  el único sitio donde se elige el informe, y viaja con el testigo a toda la cadena.
- Nodo: `buildReport()` construye el informe del tipo que llegó con el testigo (`buildModelReport()` o
  `buildAggregatedReport()`). En el de modo modelo, su bloque va delante de los recibidos del hijo, que se reenvían sin
  modificar. Los reintentos reenvían la misma copia.
- Nodo: el nieto es el hijo que trae el bloque del hijo (el primero recibido); se usa la medida propia con él si se
  midió en el ciclo. `readImu()` lee la IMU en unidades físicas para los dos informes (ceros mientras no haya sensor).
- Nodo y coordinador: las comprobaciones comunes del informe recibido (remitente esperado, informe repetido, informe
  ya no esperado) pasan a `acceptIncomingReport()`, compartida por los callbacks de los dos tipos de informe.
- Coordinador: cada bloque se convierte en sus medidas (`hop` 1 y 2, sin RX Power) y su muestra de IMU (solo ax y ay).
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

**Pendiente:** formato en crudo y scripts de Python; las 16 columnas del modelo en el coordinador; reentrenar.

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
