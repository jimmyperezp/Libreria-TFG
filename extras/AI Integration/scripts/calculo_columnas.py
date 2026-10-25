"""
Cálculo de las columnas del modelo a partir del CSV en crudo.

Es el mismo cálculo para los datos del simulador y para los de las ESP32:
solo ve el CSV en crudo (formato_crudo.py) y no sabe de dónde viene.
Cuando el modelo se despliegue en el coordinador, este cálculo se
reescribirá en C y se comprobará que da los mismos valores sobre el
mismo CSV en crudo.

Columnas de entrada al modelo (37):

  duracion_ciclo_ms                      duración real del ciclo de comunicación

  Por cada enlace adyacente (c_v1, v1_v2, v2_v3, v3_v4):
    enlace_X_Y_visto                     1 si se recibió la medida del enlace en este ciclo
    enlace_X_Y_ciclos_sin_ver            ciclos seguidos sin recibirla (0 si visto; tope 10)
    enlace_X_Y_desviacion_m              distancia - distancia de referencia (0 si no visto)
    enlace_X_Y_dif_acel_x_ms2            acel_x(trasero) - acel_x(delantero) (0 si no visto)

  Por cada vagón (v1..v4), en los ejes acel_y, acel_z, giro_x, giro_y, giro_z:
    vX_dev_media_<eje>                   valor del vagón - media de los demás vehículos
                                         recibidos en el ciclo (locomotora incluida);
                                         0 si el vagón no se recibió

Distancia de referencia de cada enlace: mediana de la distancia durante los
ciclos de calibración (por defecto, 30 ciclos a partir del ciclo 3). Se
calcula una vez, al principio del ensayo, con la hipótesis de que el tren
está íntegro en ese momento.
"""

import numpy as np
import pandas as pd

from formato_crudo import VEHICULOS, VAGONES, ENLACES, nombre_enlace

EJES_RELATIVOS = ['acel_y_ms2', 'acel_z_ms2', 'giro_x_rads', 'giro_y_rads', 'giro_z_rads']

PARAMETROS_POR_DEFECTO = {
    'calibracion_primer_ciclo': 3,
    'calibracion_n_ciclos': 30,
    'tope_ciclos_sin_ver': 10,
}


def columnas_modelo():
    cols = ['duracion_ciclo_ms']
    for a, b in ENLACES:
        e = nombre_enlace(a, b)
        cols += [f'{e}_visto', f'{e}_ciclos_sin_ver', f'{e}_desviacion_m', f'{e}_dif_acel_x_ms2']
    for v in VAGONES:
        cols += [f'{v}_dev_media_{eje}' for eje in EJES_RELATIVOS]
    return cols


def calibrar(crudo, primer_ciclo, n_ciclos):
    """Distancia de referencia de cada enlace = mediana durante la calibración."""
    cal = crudo[(crudo.ciclo >= primer_ciclo) & (crudo.ciclo < primer_ciclo + n_ciclos)]
    if len(cal) < n_ciclos:
        raise ValueError(f'Solo hay {len(cal)} ciclos para calibrar (se piden {n_ciclos})')
    referencias, avisos = {}, []
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
    """Devuelve (DataFrame con ciclo, tiempo_ms y las 37 columnas, info de calibración)."""
    p = dict(PARAMETROS_POR_DEFECTO, **(parametros or {}))
    crudo = crudo.sort_values('ciclo').reset_index(drop=True)
    referencias, avisos = calibrar(crudo, p['calibracion_primer_ciclo'], p['calibracion_n_ciclos'])

    out = pd.DataFrame({'ciclo': crudo.ciclo, 'tiempo_ms': crudo.tiempo_ms,
                        'duracion_ciclo_ms': crudo.duracion_ciclo_ms})

    # Vehículos recibidos en cada ciclo: los que tienen datos de IMU
    recibido = {v: crudo[f'{v}_acel_x_ms2'].notna() for v in VEHICULOS}

    for a, b in ENLACES:
        e = nombre_enlace(a, b)
        visto = crudo[f'{e}_visto'] == 1
        out[f'{e}_visto'] = visto.astype(int)
        out[f'{e}_ciclos_sin_ver'] = ciclos_sin_ver(visto.astype(int), p['tope_ciclos_sin_ver'])
        out[f'{e}_desviacion_m'] = np.where(visto, crudo[f'{e}_distancia_m'] - referencias[e], 0.0)
        ok = visto & recibido[a] & recibido[b]
        out[f'{e}_dif_acel_x_ms2'] = np.where(ok, crudo[f'{b}_acel_x_ms2'] - crudo[f'{a}_acel_x_ms2'], 0.0)

    for eje in EJES_RELATIVOS:
        valores = pd.DataFrame({v: crudo[f'{v}_{eje}'].where(recibido[v]) for v in VEHICULOS})
        suma = valores.sum(axis=1, skipna=True)
        n = valores.notna().sum(axis=1)
        for v in VAGONES:
            # media de los DEMÁS vehículos recibidos (sin incluir al propio vagón)
            n_otros = n - 1
            media_otros = (suma - valores[v]) / n_otros
            ok = recibido[v] & (n_otros > 0)
            out[f'{v}_dev_media_{eje}'] = np.where(ok, valores[v] - media_otros, 0.0)

    info = {'referencias_m': referencias, 'avisos_calibracion': avisos,
            'ciclos_calibracion': [p['calibracion_primer_ciclo'],
                                   p['calibracion_primer_ciclo'] + p['calibracion_n_ciclos'] - 1],
            'parametros': p}
    return out[['ciclo', 'tiempo_ms'] + columnas_modelo()], info
