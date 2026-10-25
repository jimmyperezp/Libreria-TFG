"""
Adaptador del simulador: datos del simulador -> CSV en crudo.

Lee la carpeta de un ensayo del simulador y genera, en la carpeta de salida:
  - crudo_<ensayo>.csv        una fila por ciclo, en el formato común (formato_crudo.py)
  - composicion_<ensayo>.json qué dispositivo ocupa cada posición y cómo se generó el archivo

Solo traduce: no calcula columnas para el modelo, no etiqueta y no descarta
nada salvo el último ciclo si el registro termina a mitad de él.

Archivos del simulador que usa:
  coord/rx_coord.csv   mensajes recibidos por el coordinador (uno por vagón y ciclo)
  coord/imu_crd.csv    IMU del coordinador (locomotora), 200 Hz
  config.json          número de vagones, frecuencia de comunicación, periodo de la IMU

Traducción (ver README):
  - Ciclo: una vuelta del round robin. Cada vagón tiene una ranura fija de
    1/comm_rate_hz (0,25 s) y la vuelta completa dura num_wagons ranuras (1 s).
  - Enlace c-v1: distancia UWB que mide el coordinador a v1.
  - Enlace v(k-1)-vk: el simulador solo da distancias al coordinador, así que
    la separación entre vecinos se obtiene restando: d(vk) - d(vk-1).
    Visto solo si se recibieron los dos vagones en el ciclo.
  - Potencia RX: la del mensaje recibido por el coordinador. Solo existe para
    c-v1; para los demás enlaces el simulador no la da y se deja vacía.
  - IMU de cada vagón: la que viaja en su mensaje.
  - IMU del coordinador: la muestra de imu_crd tomada en el mismo instante
    en que se recibe el mensaje de v1 (o en su ranura, si no llegó).

Uso:
  python adaptador_simulador.py <carpeta_ensayo> <carpeta_salida> [--nombre NOMBRE]
"""

import argparse
import json
from pathlib import Path

import numpy as np
import pandas as pd

from formato_crudo import VEHICULOS, VAGONES, ENLACES, EJES_IMU, nombre_enlace, columnas_crudo

COLS_IMU_MENSAJE = ['ax', 'ay', 'az', 'gx', 'gy', 'gz']                 # en rx_coord.csv
COLS_IMU_COORD = ['ax_meas', 'ay_meas', 'az_meas', 'gx_meas', 'gy_meas', 'gz_meas']   # en imu_crd.csv


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
    # Identificador del simulador "cK" -> posición v(K+1)
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
        fila = {'ciclo': k}

        # Tiempo y duración del ciclo
        if recibidos:
            t_ultimo = g.t.max()
        else:
            t_ultimo = t_inicio + (n_vagones - 1) * ranura_s
        fila['tiempo_ms'] = int(round(t_ultimo * 1000))
        # Desde el inicio de la vuelta hasta el final de la última ranura con respuesta
        fila['duracion_ciclo_ms'] = int(round((t_ultimo - t_inicio + ranura_s) * 1000))

        # Enlaces
        def dist(p):
            return g.loc[p, 'uwb'] if p in recibidos else np.nan
        for i, (a, b) in enumerate(ENLACES):
            e = nombre_enlace(a, b)
            pb = i                       # posición del vagón trasero del enlace (v1 -> 0)
            if a == 'c':
                visto = pb in recibidos
                d = dist(pb)
                pot = g.loc[pb, 'power'] if visto else np.nan
            else:
                pa = i - 1
                visto = pa in recibidos and pb in recibidos
                d = dist(pb) - dist(pa) if visto else np.nan
                pot = np.nan             # el simulador no da potencia entre vecinos
            fila[f'{e}_visto'] = int(visto)
            fila[f'{e}_distancia_m'] = d
            fila[f'{e}_potencia_rx_dbm'] = pot

        # IMU del coordinador: en el instante de recepción de v1 (o en su ranura)
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
        'imu': 'simulada por el simulador (con ruido)',
        'potencia_rx': 'solo enlace_c_v1; vacía en los demás (el simulador no la da)',
        'ciclos': int(len(crudo)),
        'periodo_nominal_ms': int(round(n_vagones * ranura_s * 1000)),
        'tiempo_cero': 'inicio de la simulación',
    }
    return crudo, composicion


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('carpeta_ensayo')
    ap.add_argument('carpeta_salida')
    ap.add_argument('--nombre', help='nombre del ensayo (por defecto, el de la carpeta)')
    args = ap.parse_args()

    carpeta = Path(args.carpeta_ensayo)
    nombre = args.nombre or carpeta.name
    salida = Path(args.carpeta_salida)
    salida.mkdir(parents=True, exist_ok=True)

    cfg, rx, imu = leer_ensayo(carpeta)
    crudo, comp = adaptar(cfg, rx, imu)
    comp['ensayo'] = nombre
    comp['carpeta_origen'] = carpeta.name

    crudo.to_csv(salida / f'crudo_{nombre}.csv', index=False, float_format='%.6f')
    (salida / f'composicion_{nombre}.json').write_text(json.dumps(comp, indent=2, ensure_ascii=False), encoding='utf-8')
    print(f'{nombre}: {len(crudo)} ciclos -> {salida / f"crudo_{nombre}.csv"}')


if __name__ == '__main__':
    main()
