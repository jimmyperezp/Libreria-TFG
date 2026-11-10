"""
Cálculo de las columnas del modelo a partir del CSV en crudo.

Es el mismo cálculo para los datos del simulador y para los de las placas:
solo ve el CSV en crudo (formato_crudo.py) y no sabe de dónde viene.
El coordinador hace el mismo cálculo en C (model_features.cpp y link_references.cpp),
con las mismas columnas, en el mismo orden y con la misma calibración.

Columnas de entrada al modelo (25):
  Por cada enlace (c_v1, v1_v2, v2_v3, v3_v4), 4 columnas:
    enlace_X_Y_visto                 1 si se recibió la medida directa del enlace en este ciclo
    enlace_X_Y_ciclos_sin_ver        ciclos seguidos sin recibirla (0 si visto; tope 10)
    enlace_X_Y_desviacion_m          distancia - distancia de referencia (0 si no visto o sin referencia)
    enlace_X_Y_dif_acel_x_ms2        acel_x(trasero) - acel_x(delantero) (0 si no visto)
  Después, por cada par a dos saltos (nieto: c_v2, v1_v3, v2_v4), 3 columnas:
    salto2_X_Z_visto                 1 si se recibió la medida del par en este ciclo
    salto2_X_Z_ciclos_sin_ver        ciclos seguidos sin recibirla (tope 10)
    salto2_X_Z_desviacion_m          distancia - distancia de referencia del par (0 si no visto o sin referencia)

Cada columna sale de una sola medida: no se combinan enlaces y pares. Es el modelo el que aprende,
por ejemplo, que "V2-V3 no se ve, pero V1-V3 sí y en su sitio" significa tren íntegro.

Del CSV en crudo NO entran al modelo (se graban para análisis futuros): la duración del
ciclo, la potencia RX y la aceleración lateral (acel_y).

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

from formato_crudo import ENLACES, PARES_SALTO2, nombre_enlace, nombre_salto2

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
        cols += [f'{e}_visto', f'{e}_ciclos_sin_ver', f'{e}_desviacion_m', f'{e}_dif_acel_x_ms2']
    for a, b in PARES_SALTO2:
        s = nombre_salto2(a, b)
        cols += [f'{s}_visto', f'{s}_ciclos_sin_ver', f'{s}_desviacion_m']
    return cols


def medidas_calibradas():
    """Nombres de las medidas que tienen referencia: los 4 enlaces y los 3 pares."""
    return [nombre_enlace(a, b) for a, b in ENLACES] + [nombre_salto2(a, b) for a, b in PARES_SALTO2]


def calcular_medida(ciclo, visto, distancia, p):
    """Para una medida (enlace o par), ciclo a ciclo y como el coordinador:
    devuelve (ciclos_sin_ver, desviacion_m, info de su calibración)."""
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
        corto = nombre.replace('enlace_', '').replace('salto2_', '')
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
    """Devuelve (DataFrame con ciclo, tiempo_ms y las 25 columnas, info de calibración)."""
    p = dict(PARAMETROS_POR_DEFECTO, **(parametros or {}))
    crudo = crudo.sort_values('ciclo').reset_index(drop=True)
    out = pd.DataFrame({'ciclo': crudo.ciclo, 'tiempo_ms': crudo.tiempo_ms})
    calibracion = {}

    for nombre in medidas_calibradas():
        visto = (crudo[f'{nombre}_visto'] == 1).values
        sin_ver, desv, calibracion[nombre] = calcular_medida(crudo.ciclo.values, visto,
                                                             crudo[f'{nombre}_distancia_m'].values, p)
        out[f'{nombre}_visto'] = visto.astype(int)
        out[f'{nombre}_ciclos_sin_ver'] = sin_ver
        out[f'{nombre}_desviacion_m'] = desv

    # Diferencia de aceleración longitudinal: solo en los enlaces
    for a, b in ENLACES:
        e = nombre_enlace(a, b)
        ok = (out[f'{e}_visto'] == 1) & crudo[f'{a}_acel_x_ms2'].notna() & crudo[f'{b}_acel_x_ms2'].notna()
        out[f'{e}_dif_acel_x_ms2'] = np.where(ok, crudo[f'{b}_acel_x_ms2'] - crudo[f'{a}_acel_x_ms2'], 0.0)

    info = {'calibracion': calibracion, 'avisos_calibracion': avisos(calibracion, p), 'parametros': p}
    return out[['ciclo', 'tiempo_ms'] + columnas_modelo()], info
