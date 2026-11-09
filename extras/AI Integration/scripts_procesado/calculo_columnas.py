"""
Cálculo de las columnas del modelo a partir del CSV en crudo.

Es el mismo cálculo para los datos del simulador y para los de las placas:
solo ve el CSV en crudo (formato_crudo.py) y no sabe de dónde viene.
El coordinador hace el mismo cálculo en C (model_features.cpp), con las
mismas columnas y en el mismo orden.

Columnas de entrada al modelo (16). Por cada enlace (c_v1, v1_v2, v2_v3, v3_v4):
    enlace_X_Y_visto                     1 si se recibió la medida del enlace en este ciclo
    enlace_X_Y_ciclos_sin_ver            ciclos seguidos sin recibirla (0 si visto; tope 10)
    enlace_X_Y_desviacion_m              distancia - distancia de referencia (0 si no visto)
    enlace_X_Y_dif_acel_x_ms2            acel_x(trasero) - acel_x(delantero) (0 si no visto)

Del CSV en crudo NO entran al modelo (se graban para análisis futuros): la duración del
ciclo, la potencia RX, los pares a dos saltos (nieto) y la aceleración lateral (acel_y).

Distancia de referencia de cada enlace: mediana de la distancia durante los
ciclos de calibración (por defecto, 30 ciclos a partir del ciclo 3). Se
calcula una vez, al principio del ensayo, con la hipótesis de que el tren
está íntegro en ese momento.
"""

import numpy as np
import pandas as pd

from formato_crudo import VEHICULOS, ENLACES, nombre_enlace

PARAMETROS_POR_DEFECTO = {
    'calibracion_primer_ciclo': 3,
    'calibracion_n_ciclos': 30,
    'tope_ciclos_sin_ver': 10,
}


def columnas_modelo():
    cols = []
    for a, b in ENLACES:
        e = nombre_enlace(a, b)
        cols += [f'{e}_visto', f'{e}_ciclos_sin_ver', f'{e}_desviacion_m', f'{e}_dif_acel_x_ms2']
    return cols


def calibrar(crudo, primer_ciclo, n_ciclos):
    """Distancia de referencia de cada enlace = mediana durante la calibración."""
    cal = crudo[(crudo.ciclo >= primer_ciclo) & (crudo.ciclo < primer_ciclo + n_ciclos)]
    if cal.empty:
        raise ValueError(f'No hay ningún ciclo en la ventana de calibración ({primer_ciclo}-{primer_ciclo + n_ciclos - 1})')
    referencias, avisos = {}, []
    if len(cal) < n_ciclos:
        # En las grabaciones por puerto serie se puede perder algún ciclo. La regla no cambia
        # (mediana de la ventana), solo se calcula con los ciclos que hay.
        avisos.append(f'faltan {n_ciclos - len(cal)} de {n_ciclos} ciclos en la ventana de calibración')
    for a, b in ENLACES:
        e = nombre_enlace(a, b)
        vistos = cal[cal[f'{e}_visto'] == 1][f'{e}_distancia_m']
        if vistos.empty:
            raise ValueError(f'El enlace {e} no se ve en ningún ciclo de calibración')
        if len(vistos) < n_ciclos:
            avisos.append(f'{e}: visto en {len(vistos)} de {n_ciclos} ciclos de calibración')
        referencias[e] = float(vistos.median())
    return referencias, avisos


def ciclos_sin_ver(visto, tope):
    out, c = [], 0
    for x in visto:
        c = 0 if x == 1 else min(c + 1, tope)
        out.append(c)
    return np.array(out)


def calcular(crudo, parametros=None):
    """Devuelve (DataFrame con ciclo, tiempo_ms y las 16 columnas, info de calibración)."""
    p = dict(PARAMETROS_POR_DEFECTO, **(parametros or {}))
    crudo = crudo.sort_values('ciclo').reset_index(drop=True)
    referencias, avisos = calibrar(crudo, p['calibracion_primer_ciclo'], p['calibracion_n_ciclos'])

    out = pd.DataFrame({'ciclo': crudo.ciclo, 'tiempo_ms': crudo.tiempo_ms})

    # Vehículos recibidos en cada ciclo: los que tienen aceleración
    recibido = {v: crudo[f'{v}_acel_x_ms2'].notna() for v in VEHICULOS}

    for a, b in ENLACES:
        e = nombre_enlace(a, b)
        visto = crudo[f'{e}_visto'] == 1
        out[f'{e}_visto'] = visto.astype(int)
        out[f'{e}_ciclos_sin_ver'] = ciclos_sin_ver(visto.astype(int), p['tope_ciclos_sin_ver'])
        out[f'{e}_desviacion_m'] = np.where(visto, crudo[f'{e}_distancia_m'] - referencias[e], 0.0)
        ok = visto & recibido[a] & recibido[b]
        out[f'{e}_dif_acel_x_ms2'] = np.where(ok, crudo[f'{b}_acel_x_ms2'] - crudo[f'{a}_acel_x_ms2'], 0.0)

    info = {'referencias_m': referencias, 'avisos_calibracion': avisos,
            'ciclos_calibracion': [p['calibracion_primer_ciclo'],
                                   p['calibracion_primer_ciclo'] + p['calibracion_n_ciclos'] - 1],
            'parametros': p}
    return out[['ciclo', 'tiempo_ms'] + columnas_modelo()], info
