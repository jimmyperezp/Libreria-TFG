"""
Cálculo de las columnas del modelo a partir del CSV en crudo.

Es el mismo cálculo para los datos del simulador y para los de las placas:
solo ve el CSV en crudo (formato_crudo.py) y no sabe de dónde viene.
El coordinador hace el mismo cálculo en C (model_features.cpp y link_references.cpp),
con las mismas columnas, en el mismo orden y con la misma calibración.

Columnas de entrada al modelo (25):
  Por cada enlace (c_w1, w1_w2, w2_w3, w3_w4), 4 columnas:
    link_X_Y_seen                    1 si se recibió la medida directa del enlace en este ciclo
    link_X_Y_unseen_cycles           ciclos seguidos sin recibirla (0 si visto; tope 10)
    link_X_Y_deviation_m             distancia - distancia de referencia (0 si no visto o sin referencia)
    link_X_Y_acc_x_diff_ms2          acc_x(trasero) - acc_x(delantero) (0 si no visto)
  Después, por cada par a dos saltos (nieto: c_w2, w1_w3, w2_w4), 3 columnas:
    hop2_X_Z_seen                    1 si se recibió la medida del par en este ciclo
    hop2_X_Z_unseen_cycles           ciclos seguidos sin recibirla (tope 10)
    hop2_X_Z_deviation_m             distancia - distancia de referencia del par (0 si no visto o sin referencia)

Cada columna sale de una sola medida: no se combinan enlaces y pares. Es el modelo el que aprende,
por ejemplo, que "W2-W3 no se ve, pero W1-W3 sí y en su sitio" significa tren íntegro.

Del CSV en crudo NO entran al modelo (se graban para análisis futuros): la duración del
ciclo y la aceleración lateral (acc_y).

Distancia de referencia de cada enlace y de cada par: la misma regla que el coordinador.
  - Se calibra con las PRIMERAS medidas que se reciben de esa pareja, cuando lleguen (un nieto
    que solo se ve a partir de una curva se calibra entonces).
  - Mientras calibra, la referencia es la mediana de las medidas que lleva (incluida la del ciclo).
    Con menos de "calibracion_min_medidas" (3) no se usa: la desviación vale 0.
  - Con "calibracion_n_medidas" (30) medidas, la referencia queda fija hasta el final del ensayo.
  - Los ciclos anteriores a "ciclos_descartados_inicio" no se usan para calibrar (transitorio
    de arranque del simulador; en las placas es 0).
Hipótesis: cuando una pareja empieza a verse, el tren está íntegro. Si no es así, el resumen lo avisa.
"""

import numpy as np
import pandas as pd

from formato_crudo import (ENLACES, PARES_SALTO2, nombre_enlace, nombre_salto2,
                           COL_CICLO, COL_TIEMPO, SUF_VISTO, SUF_DISTANCIA, EJE_ACEL_X)

# Nombres de las columnas del modelo (en inglés, iguales que FEATURE_NAMES en model_features.cpp)
SUF_SIN_VER = 'unseen_cycles'
SUF_DESVIACION = 'deviation_m'
SUF_DIF_ACEL_X = 'acc_x_diff_ms2'

PARAMETROS_POR_DEFECTO = {
    'ciclos_descartados_inicio': 0,
    'calibracion_n_medidas': 30,      # = LINK_REF_SAMPLES del coordinador
    'calibracion_min_medidas': 3,     # = LINK_REF_MIN_SAMPLES del coordinador
    'tope_ciclos_sin_ver': 10,        # = UNSEEN_CYCLES_CAP del coordinador
}

# Una calibración se considera tardía si empieza más de estos ciclos después del primer ciclo usable
MARGEN_CALIBRACION_TARDIA = 10


def columnas_modelo():
    cols = []
    for a, b in ENLACES:
        e = nombre_enlace(a, b)
        cols += [f'{e}_{SUF_VISTO}', f'{e}_{SUF_SIN_VER}', f'{e}_{SUF_DESVIACION}', f'{e}_{SUF_DIF_ACEL_X}']
    for a, b in PARES_SALTO2:
        s = nombre_salto2(a, b)
        cols += [f'{s}_{SUF_VISTO}', f'{s}_{SUF_SIN_VER}', f'{s}_{SUF_DESVIACION}']
    return cols


def medidas_calibradas():
    """Nombres de las medidas que tienen referencia: los 4 enlaces y los 3 pares."""
    return [nombre_enlace(a, b) for a, b in ENLACES] + [nombre_salto2(a, b) for a, b in PARES_SALTO2]


def calcular_medida(ciclo, visto, distancia, p):
    """Para una medida (enlace o par), ciclo a ciclo y como el coordinador:
    devuelve (unseen_cycles, deviation_m, info de su calibración)."""
    n_cal, n_min, tope = p['calibracion_n_medidas'], p['calibracion_min_medidas'], p['tope_ciclos_sin_ver']
    muestras, ciclos_muestras = [], []
    referencia, fija, sin_ver = None, False, 0
    fijada_en = None
    out_sin_ver = np.zeros(len(ciclo), dtype=int)
    out_desv = np.zeros(len(ciclo))
    for i, (k, v, d) in enumerate(zip(ciclo, visto, distancia)):
        if not v:
            sin_ver = min(sin_ver + 1, tope)
        else:
            sin_ver = 0
            if not fija and k >= p['ciclos_descartados_inicio']:
                muestras.append(d)
                ciclos_muestras.append(int(k))
                referencia = float(np.median(muestras))
                if len(muestras) >= n_cal:
                    fija, fijada_en = True, int(k)
            if referencia is not None and (fija or len(muestras) >= n_min):
                out_desv[i] = d - referencia
        out_sin_ver[i] = sin_ver
    info = {
        'referencia_m': None if len(muestras) < n_min else referencia,
        'medidas': len(muestras),
        'desde_ciclo': ciclos_muestras[0] if ciclos_muestras else None,
        'fijada_en_ciclo': fijada_en,
        'ciclos_muestras': ciclos_muestras,
    }
    return out_sin_ver, out_desv, info


def avisos(calibracion, p):
    """Avisos de calibración que se pueden dar sin conocer las etiquetas (tardía, incompleta, sin referencia)."""
    out = []
    for nombre, c in calibracion.items():
        corto = nombre.replace('link_', '').replace('hop2_', '')
        if c['referencia_m'] is None:
            out.append(f'{corto}: sin referencia en todo el ensayo ({c["medidas"]} medidas de calibración)')
            continue
        if c['desde_ciclo'] > p['ciclos_descartados_inicio'] + MARGEN_CALIBRACION_TARDIA:
            out.append(f'{corto}: calibrado a partir del ciclo {c["desde_ciclo"]}')
        if c['fijada_en_ciclo'] is None:
            out.append(f'{corto}: solo {c["medidas"]} de {p["calibracion_n_medidas"]} medidas de calibración; '
                       'referencia provisional')
    return out


def calcular(crudo, parametros=None):
    """Devuelve (DataFrame con cycle, time_ms y las 25 columnas, info de calibración)."""
    p = dict(PARAMETROS_POR_DEFECTO, **(parametros or {}))
    crudo = crudo.sort_values(COL_CICLO).reset_index(drop=True)
    out = pd.DataFrame({COL_CICLO: crudo[COL_CICLO], COL_TIEMPO: crudo[COL_TIEMPO]})
    calibracion = {}

    for nombre in medidas_calibradas():
        visto = (crudo[f'{nombre}_{SUF_VISTO}'] == 1).values
        sin_ver, desv, calibracion[nombre] = calcular_medida(crudo[COL_CICLO].values, visto,
                                                             crudo[f'{nombre}_{SUF_DISTANCIA}'].values, p)
        out[f'{nombre}_{SUF_VISTO}'] = visto.astype(int)
        out[f'{nombre}_{SUF_SIN_VER}'] = sin_ver
        out[f'{nombre}_{SUF_DESVIACION}'] = desv

    # Diferencia de aceleración longitudinal: solo en los enlaces
    for a, b in ENLACES:
        e = nombre_enlace(a, b)
        ax_a, ax_b = crudo[f'{a}_{EJE_ACEL_X}'], crudo[f'{b}_{EJE_ACEL_X}']
        ok = (out[f'{e}_{SUF_VISTO}'] == 1) & ax_a.notna() & ax_b.notna()
        out[f'{e}_{SUF_DIF_ACEL_X}'] = np.where(ok, ax_b - ax_a, 0.0)

    info = {'calibracion': calibracion, 'avisos_calibracion': avisos(calibracion, p), 'parametros': p}
    return out[[COL_CICLO, COL_TIEMPO] + columnas_modelo()], info
