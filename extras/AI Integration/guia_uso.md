# Guía de uso

Pasos a seguir en cada caso. Qué contiene cada archivo y cómo se calcula cada cosa está en `readme.md`.

Todos los scripts están en `scripts_procesado/` y se ejecutan **desde VS Code con el botón de Play**
(*Run Python File*), sin argumentos. Lo único que se cambia está en el bloque `CONFIGURACIÓN` del principio
de cada script.

---

## 1. Grabar un ensayo con las placas

**Antes de la prueba**, en `grabador_serie.py`:

| Variable | Qué poner |
|---|---|
| `ENSAYO` | El nombre del ensayo, p. ej. `'desconexion_v3_01'`. Letras sin tildes, números, `_` y `-`, sin espacios |
| `PUERTO` | El puerto del coordinador, p. ej. `'COM15'` (solo si cambia) |

El resto (`TECLAS`, `FASE_INICIAL`, `VER_MONITOR`…) normalmente no se toca.

**Durante la prueba:**

1. Conecta el coordinador al PC y pulsa Play. El grabador reinicia el coordinador y espera su cabecera.
   - Si ya existe un ensayo con ese nombre, pregunta si sobrescribirlo (ver apartado 3).
   - Si a los 5 s no llega la cabecera, pulsa el botón EN/RST de la placa.
2. Cuando aparezca `Cabecera RAW_HEADER del coordinador validada`, la grabación está en marcha. La línea de estado
   muestra el ciclo, la fase, las filas, los ciclos perdidos y las marcas.
3. Marca cada cambio de fase **en el terminal de VS Code** donde corre el script:
   - `0` = OK
   - `1` = LOST_INTEGRITY

   La grabación empieza ya en OK. La fase nueva cuenta desde el ciclo siguiente al último recibido.
4. Para terminar, pulsa `q` (o Ctrl+C).

**Qué queda:**
- `datasets_procesados/placas/<ensayo>/crudo/`: crudo, marcas y monitor serie.
- `datasets_procesados/placas/<ensayo>/resumen_<ensayo>.json`: fecha, datos de la grabación y una
  `descripcion` vacía.
- La entrada del ensayo en `datasets_procesados/placas/ensayos_placas.json` (la añade el grabador).

**Después (opcional, cuando quieras):** abre el `resumen_<ensayo>.json` y escribe en `"descripcion"` qué hiciste
en el ensayo. El exportador no la toca.

---

## 2. Exportar ensayos de las placas para Edge Impulse

En `exportar_edge_impulse.py`:

```python
ORIGEN = 'PLACAS'
ENSAYOS = ['desconexion_v3_01']       # solo estos
# ENSAYOS = 'TODOS'                   # todos los de ensayos_placas.json
```

Pulsa Play. Para cada ensayo, el exportador:
- calcula las 25 columnas a partir del crudo y etiqueta cada ciclo con las marcas;
- escribe `revision/` y `edge_impulse/` en la carpeta del ensayo;
- completa la parte `"procesado"` de su resumen (filas por etiqueta, referencias de calibración, avisos);
- apunta en `ensayos_placas.json` los parámetros que ha usado y dónde están los resultados.

Al terminar, comprueba en la consola o en el resumen las filas de cada etiqueta y los avisos de calibración.
Los CSV para subir están en `datasets_procesados/placas/<ensayo>/edge_impulse/`.

**Normalmente se exporta solo el ensayo nuevo.** `'TODOS'` sirve cuando hay que rehacer todo (apartado 5).

**Corregir una marca:** si pulsaste una tecla tarde, edita su `desde_ciclo` en
`crudo/marcas_<ensayo>.csv` y vuelve a exportar ese ensayo.

---

## 3. Repetir un ensayo

Deja el mismo `ENSAYO` y pulsa Play. El grabador pregunta:

```
El ensayo 'desconexion_v3_01' ya existe (grabado el 2026-11-10 10:32, 412 ciclos).
¿Sobrescribirlo? Se borrará entero, también lo exportado y su descripción [1 = sí / 0 = no]:
```

- `1`: borra la carpeta entera del ensayo (crudo, exportado, resumen y descripción) y graba de nuevo. Su entrada
  en `ensayos_placas.json` se sustituye.
- `0`: no toca nada y se detiene, indicando la línea de la variable `ENSAYO` para que pongas otro nombre.

---

## 4. Añadir y exportar un ensayo del simulador

1. Copia la carpeta del ensayo, tal como la da el simulador, dentro de `datasets_simulador/`.
2. Añade su entrada en `datasets_procesados/simulador/ensayos_simulador.json` (a mano, dentro de `"ensayos"`):
   ```json
   {
     "nombre": "desacople_v3_v4",
     "descripcion": "Desacople del enganche V3-V4 a los 200 s",
     "carpeta_simulador": "decoupling_w3_200s",
     "evento_s": 200.0,
     "etiqueta_tras_evento": "LOST_INTEGRITY",
     "fin_s": null
   }
   ```
   - `evento_s`: segundo del evento, o `null` si no hay (todo OK).
   - `fin_s`: segundo a partir del cual se descarta el final, o `null`.
3. En `exportar_edge_impulse.py`: `ORIGEN = 'SIMULADOR'` y `ENSAYOS = ['desacople_v3_v4']`. Pulsa Play.
4. Revisa la consola y el resumen: filas OK y LOST_INTEGRITY, y que el instante del evento cuadra.

Los resultados quedan en `datasets_procesados/simulador/<nombre>/`. Recuerda poner otra vez `ORIGEN = 'PLACAS'`
cuando vuelvas a exportar grabaciones.

Para obtener solo el crudo de un ensayo del simulador, sin exportar, está `adaptador_simulador.py`: cambia
`CARPETA_ENSAYO` y `NOMBRE` y pulsa Play.

---

## 5. Rehacer todo

Hay que reexportar **todos** los ensayos cuando:
- cambia el cálculo de las columnas (`calculo_columnas.py`);
- cambias un parámetro de `"general"` en `ensayos_placas.json` o `ensayos_simulador.json` (por ejemplo, el número
  de medidas de calibración).

Pon `ENSAYOS = 'TODOS'` y pulsa Play, una vez con `ORIGEN = 'PLACAS'` y otra con `ORIGEN = 'SIMULADOR'`.

Si cambia el **formato en crudo** (`formato_crudo.py`), las grabaciones de las placas anteriores no se pueden
reaprovechar: el grabador ya no las aceptaría. Las del simulador sí se regeneran, porque se rehacen desde
`datasets_simulador/`.

---

## 6. Subir los datos a Edge Impulse

1. En el proyecto de Edge Impulse: *Data acquisition* → *Upload data*.
2. Selecciona los CSV de las carpetas `edge_impulse/` de los ensayos que quieras.
3. Elige que la etiqueta se tome del nombre del archivo (*Infer from filename*): es lo que va antes del primer punto
   (`OK` o `LOST_INTEGRITY`).
4. Las 25 columnas tienen el mismo nombre y orden en todos los archivos, sean del simulador o de las placas.

---

## 7. Si algo falla

| Mensaje | Qué pasa y qué hacer |
|---|---|
| `La cabecera RAW_HEADER del coordinador no coincide con formato_crudo.py` | El firmware y los scripts son de versiones distintas. Carga en el coordinador el firmware de la misma versión del repositorio |
| `El coordinador se ha reiniciado durante la grabación` | Lo grabado hasta ahí se conserva y queda apuntado. Repite el ensayo con otro nombre o sobrescríbelo |
| `ensayos_placas.json no es un JSON válido (línea X, columna Y)` | Se quedó mal al editarlo a mano (suele ser una coma de más o de menos). Corrige esa línea. El grabador lo comprueba antes de grabar |
| `NO se ha podido apuntar el ensayo en ensayos_placas.json` | La grabación está guardada. Copia la entrada que muestra el grabador dentro de `"ensayos"` |
| `AVISO: <ensayo> no se ha exportado: no existe su CSV en crudo` | La carpeta del ensayo se borró a mano. Borra también su entrada en `ensayos_placas.json` |
| `aviso de calibración: ...` | No impide exportar. Indica una calibración que conviene revisar (tardía, incompleta, sin referencia o con ciclos que no son OK). Qué significa cada uno: `readme.md`, apartado «Calibración» |
| `No están en ensayos_placas.json: [...]` | Un nombre de `ENSAYOS` está mal escrito. El mensaje lista los ensayos registrados |
| `El nombre del ensayo ... no es válido` | Quita espacios, tildes y caracteres especiales de `ENSAYO` |
