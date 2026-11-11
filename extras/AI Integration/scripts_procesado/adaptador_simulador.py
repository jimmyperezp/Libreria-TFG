"""
Adaptador del simulador: datos del simulador -> CSV en crudo.

Lee la carpeta de un ensayo del simulador (en datasets_simulador/) y genera su CSV en crudo:
  - crudo_<ensayo>.csv        una fila por ciclo, en el formato común (formato_crudo.py)
  - composicion_<ensayo>.json qué dispositivo ocupa cada posición y cómo se generó el archivo

Normalmente no hace falta ejecutarlo: lo llama el exportador. Se puede ejecutar solo
(botón de Play) para obtener únicamente el crudo de un ensayo: ver CONFIGURACIÓN.

Solo traduce: no calcula columnas para el modelo, no etiqueta y no descarta
nada salvo el último ciclo si el registro termina a mitad de él.

Archivos del simulador que usa:
  coord/rx_coord.csv   mensajes recibidos por el coordinador (uno por vagón y ciclo)
  coord/imu_crd.csv    IMU del coordinador (locomotora), 200 Hz
  config.json          número de vagones, frecuencia de comunicación, periodo de la IMU

Traducción (ver README):
  - Ciclo: una vuelta del round robin. Cada vagón tiene una ranura fija de
    1/comm_rate_hz (0,25 s) y la vuelta completa dura num_wagons ranuras (1 s).
  - Enlace c-w1: distancia UWB que mide el coordinador a w1.
  - Enlace w(k-1)-wk: el simulador solo da distancias al coordinador, así que
    la separación entre vecinos se obtiene restando: d(wk) - d(wk-1).
    Visto solo si se recibieron los dos vagones en el ciclo.
  - Pares a dos saltos (hop2_*): c_w2 es la distancia del coordinador a w2 (medida directa);
    w1_w3 y w2_w4 se obtienen por resta, igual que los enlaces entre vagones: d(w3) - d(w1) y
    d(w4) - d(w2). Solo cuentan como vistos si se recibieron los dos vagones.
  - IMU: solo acc_x y acc_y (lo que viaja en el informe de las placas).
    La de cada vagón es la que viaja en su mensaje. La del coordinador, la muestra
    de imu_crd tomada en el mismo instante en que se recibe el mensaje de w1
    (o en su ranura, si no llegó).
"""

import json
from pathlib import Path

import numpy as np
import pandas as pd

from formato_crudo import (VAGONES, ENLACES, PARES_SALTO2, EJES_IMU, nombre_enlace, nombre_salto2, columnas_crudo,
                           COL_CICLO, COL_TIEMPO, COL_DURACION, SUF_VISTO, SUF_DISTANCIA)


# ============================== CONFIGURACIÓN ==============================
# Solo se usa al ejecutar este script por sí solo (el exportador no la usa).

CARPETA_ENSAYO = 'decoupling_w2_150s'   # Carpeta del ensayo dentro de datasets_simulador/
NOMBRE = 'desacople'                    # Nombre del ensayo: el crudo va a datasets_procesados/simulador/<NOMBRE>/crudo/

# ===========================================================================

RAIZ = Path(__file__).resolve().parent.parent          # extras/AI Integration
CARPETA_DATASETS_SIMULADOR = RAIZ / 'datasets_simulador'
CARPETA_PROCESADOS_SIMULADOR = RAIZ / 'datasets_procesados' / 'simulador'

COLS_IMU_MENSAJE = ['ax', 'ay']             # en rx_coord.csv (en el mismo orden que EJES_IMU)
COLS_IMU_COORD = ['ax_meas', 'ay_meas']     # en imu_crd.csv


def leer_ensayo(carpeta):
    carpeta = Path(carpeta)
    cfg = json.loads((carpeta / 'config.json').read_text(encoding='utf-8'))
    rx = pd.read_csv(carpeta / 'coord' / 'rx_coord.csv')
    imu = pd.read_csv(carpeta / 'coord' / 'imu_crd.csv')
    return cfg, rx, imu


def adaptar(cfg, rx, imu):
    n_vagones = int(cfg['train']['num_wagons'])
    if n_vagones != len(VAGONES):
        raise ValueError(f'El formato en crudo es para {len(VAGONES)} vagones y el ensayo tiene {n_vagones}')
    dt_imu = float(cfg['cookie']['imu']['sensor_tick'])           # 0,005 s
    ranura_s = 1.0 / float(cfg['cookie']['comm_rate_hz'])          # 0,25 s
    ticks_ranura = int(round(ranura_s / dt_imu))                   # 50 ticks

    rx = rx[rx.msg_type == 'data'].copy()
    # Identificador del simulador "cK" -> posición w(K+1)
    rx['pos'] = rx.source_id.str[1:].astype(int)
    ids_reales = {VAGONES[k]: f'c{k}' for k in range(n_vagones)}

    # Reloj común. El "tick" de cada mensaje es el número de muestra de la IMU
    # en el instante de recepción, así que imu_crd sirve de tabla tick -> tiempo.
    # El tiempo cero es el inicio de la simulación: la primera muestra de la
    # IMU corresponde al primer paso (dt_imu).
    t_de_tick = pd.Series(imu.timestamp.values, index=imu.frame.values)
    t0 = imu.timestamp.iloc[0] - dt_imu
    imu_por_tick = imu.set_index('frame')[COLS_IMU_COORD]

    # Ranura de cada mensaje y ciclo al que pertenece
    tick_base = int((rx.tick - rx.pos * ticks_ranura).min())       # ranura 0 del ciclo 0
    rel = rx.tick - tick_base
    if (rel % ticks_ranura != 0).any():
        raise ValueError('Hay mensajes fuera de su ranura')
    rx['ranura'] = (rel // ticks_ranura).astype(int)
    if (rx.ranura % n_vagones != rx.pos).any():
        raise ValueError('Algún vagón transmite en una ranura que no es la suya')
    rx['ciclo'] = rx.ranura // n_vagones
    if rx.duplicated(['ciclo', 'pos']).any():
        raise ValueError('Mensajes duplicados en un mismo ciclo')
    rx['t'] = rx.reception_time - t0

    # El último ciclo se descarta si el registro termina antes de su última ranura
    ultimo = int(rx.ciclo.max())
    tick_ultima_ranura = tick_base + (ultimo * n_vagones + n_vagones - 1) * ticks_ranura
    if imu.frame.max() < tick_ultima_ranura:
        rx = rx[rx.ciclo < ultimo]
        ultimo -= 1

    por_ciclo = {k: g.set_index('pos') for k, g in rx.groupby('ciclo')}
    filas = []
    for k in range(ultimo + 1):
        g = por_ciclo.get(k)
        recibidos = set() if g is None else set(g.index)
        tick_inicio = tick_base + k * n_vagones * ticks_ranura
        t_inicio = t_de_tick[tick_inicio] - t0
        fila = {COL_CICLO: k}

        # Tiempo y duración del ciclo
        if recibidos:
            t_ultimo = g.t.max()
        else:
            t_ultimo = t_inicio + (n_vagones - 1) * ranura_s
        fila[COL_TIEMPO] = int(round(t_ultimo * 1000))
        # Desde el inicio de la vuelta hasta el final de la última ranura con respuesta
        fila[COL_DURACION] = int(round((t_ultimo - t_inicio + ranura_s) * 1000))

        # Enlaces
        def dist(p):
            return g.loc[p, 'uwb'] if p in recibidos else np.nan
        for i, (a, b) in enumerate(ENLACES):
            e = nombre_enlace(a, b)
            pb = i                       # posición del vagón trasero del enlace (w1 -> 0)
            if a == 'c':
                visto = pb in recibidos
                d = dist(pb)
            else:
                pa = i - 1
                visto = pa in recibidos and pb in recibidos
                d = dist(pb) - dist(pa) if visto else np.nan
            fila[f'{e}_{SUF_VISTO}'] = int(visto)
            fila[f'{e}_{SUF_DISTANCIA}'] = d

        # Pares a dos saltos (nieto): mismo criterio que los enlaces, saltando un vehículo
        for i, (a, b) in enumerate(PARES_SALTO2):
            s = nombre_salto2(a, b)
            pb = i + 1                   # posición del vagón trasero del par (w2 -> 1)
            if a == 'c':
                visto = pb in recibidos
                d = dist(pb)
            else:
                pa = i - 1
                visto = pa in recibidos and pb in recibidos
                d = dist(pb) - dist(pa) if visto else np.nan
            fila[f'{s}_{SUF_VISTO}'] = int(visto)
            fila[f'{s}_{SUF_DISTANCIA}'] = d

        # IMU del coordinador: en el instante de recepción de w1 (o en su ranura)
        tick_c = int(g.loc[0, 'tick']) if 0 in recibidos else tick_inicio
        for eje, col in zip(EJES_IMU, COLS_IMU_COORD):
            fila[f'c_{eje}'] = imu_por_tick.at[tick_c, col]

        # IMU de los vagones: la de su mensaje
        for p, v in enumerate(VAGONES):
            for eje, col in zip(EJES_IMU, COLS_IMU_MENSAJE):
                fila[f'{v}_{eje}'] = g.loc[p, col] if p in recibidos else np.nan
        filas.append(fila)

    crudo = pd.DataFrame(filas)[columnas_crudo()]
    composicion = {
        'origen': 'simulador',
        'vehiculos': [{'posicion': 'c', 'id': 'crd', 'papel': 'coordinador (locomotora)'}] +
                     [{'posicion': v, 'id': ids_reales[v], 'papel': 'vagón'} for v in VAGONES],
        'enlaces': [{'columna': nombre_enlace(a, b),
                     'id': f"{'crd' if a == 'c' else ids_reales[a]}-{ids_reales[b]}"} for a, b in ENLACES],
        'imu': 'simulada por el simulador (con ruido); solo acc_x y acc_y',
        'salto2': 'c_w2: distancia del coordinador a w2; w1_w3 y w2_w4: diferencia de las distancias al coordinador',
        'ciclos': int(len(crudo)),
        'periodo_nominal_ms': int(round(n_vagones * ranura_s * 1000)),
        'tiempo_cero': 'inicio de la simulación',
    }
    return crudo, composicion


def escribir(crudo, composicion, carpeta_crudo, nombre):
    """Escribe crudo_<nombre>.csv y composicion_<nombre>.json en carpeta_crudo."""
    carpeta_crudo = Path(carpeta_crudo)
    carpeta_crudo.mkdir(parents=True, exist_ok=True)
    crudo.to_csv(carpeta_crudo / f'crudo_{nombre}.csv', index=False, float_format='%.6f')
    (carpeta_crudo / f'composicion_{nombre}.json').write_text(
        json.dumps(composicion, indent=2, ensure_ascii=False), encoding='utf-8')


def procesar(carpeta_ensayo, nombre, carpeta_crudo):
    """Ensayo del simulador -> CSV en crudo (y su composición) en carpeta_crudo. Devuelve el crudo."""
    carpeta_ensayo = Path(carpeta_ensayo)
    cfg, rx, imu = leer_ensayo(carpeta_ensayo)
    crudo, comp = adaptar(cfg, rx, imu)
    comp['ensayo'] = nombre
    comp['carpeta_origen'] = carpeta_ensayo.name
    escribir(crudo, comp, carpeta_crudo, nombre)
    return crudo


def main():
    carpeta = CARPETA_DATASETS_SIMULADOR / CARPETA_ENSAYO
    if not carpeta.exists():
        raise SystemExit(f'No existe el ensayo del simulador: {carpeta}')
    salida = CARPETA_PROCESADOS_SIMULADOR / NOMBRE / 'crudo'
    crudo = procesar(carpeta, NOMBRE, salida)
    print(f'{NOMBRE}: {len(crudo)} ciclos -> {salida / f"crudo_{NOMBRE}.csv"}')


if __name__ == '__main__':
    main()
