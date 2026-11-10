"""
Exportador: CSV en crudo -> columnas del modelo -> CSV de entrenamiento para Edge Impulse.

Uso (VS Code): elige ORIGEN y ENSAYOS en la CONFIGURACIÓN de abajo y pulsa el botón de ejecutar (Play).

Lee el registro de ensayos de su origen:
  PLACAS     datasets_procesados/placas/ensayos_placas.json        (lo rellena el grabador)
  SIMULADOR  datasets_procesados/simulador/ensayos_simulador.json  (se escribe a mano)

Para cada ensayo elegido:
  1. Obtiene su CSV en crudo:
       - simulador: ejecuta el adaptador sobre datasets_simulador/<carpeta_simulador> y deja
         el crudo en <ensayo>/crudo/;
       - placas: usa el crudo que dejó el grabador en <ensayo>/crudo/.
  2. Calcula las 25 columnas del modelo (calculo_columnas.py), con la calibración de cada enlace
     y par a dos saltos igual que el coordinador (con sus primeras medidas, cuando lleguen).
  3. Asigna a cada ciclo su uso y su etiqueta:
       - descartado_arranque: los primeros ciclos ("ciclos_descartados_inicio")
       - descartado_fin:      a partir de "fin_s", si se indica (solo simulador)
       - sin_etiqueta:        ciclos anteriores a la primera marca (solo placas)
       - entrenamiento:       el resto
     La etiqueta sale de:
       - simulador: "evento_s". OK hasta el evento; a partir del primer ciclo que termina
         después de "evento_s", la etiqueta "etiqueta_tras_evento".
       - placas: el archivo de marcas del grabador. Cada ciclo lleva la fase de la última
         marca cuyo desde_ciclo sea menor o igual que su ciclo (cualquier número de fases).
  4. Escribe, en datasets_procesados/<origen>/<ensayo>/:
       revision/revision_<ensayo>.csv        todos los ciclos con las 25 columnas, su uso y su etiqueta
       edge_impulse/<ETIQUETA>.<ensayo>.csv  un archivo por etiqueta, listo para subir a Edge Impulse
       resumen_<ensayo>.json                 añade la parte "procesado" (recuentos, calibración, avisos)
                                             sin tocar "fecha", "descripcion" ni "grabacion"
     En las placas, apunta además en ensayos_placas.json los parámetros usados y las rutas de los resultados.
     Antes de escribir, se borran los CSV de Edge Impulse anteriores del ensayo.
     Si los ciclos de una etiqueta no son consecutivos (fases repetidas, ciclos perdidos),
     se escribe un archivo por tramo: <ETIQUETA>.<ensayo>_p1.csv, _p2...

Formato de los archivos para Edge Impulse:
  - Primera columna, tiempo_ms: tiempo NOMINAL equiespaciado (ciclo x periodo_nominal_ms),
    porque Edge Impulse supone muestras equiespaciadas.
  - Después, las 25 columnas de entrada, en el mismo orden en todos los archivos.

Avisos de calibración (no impiden exportar; van al resumen y a la consola):
  - calibración tardía: la pareja empezó a verse más tarde (normal en un nieto; en un enlace,
    suele ser un nodo que entró o se reinició a mitad de ensayo)
  - calibración incompleta: no llegó a 30 medidas; referencia provisional en todo el ensayo
  - sin referencia: menos de 3 medidas; su desviación vale 0 en todo el ensayo
  - calibrada con ciclos que no son OK: la referencia puede estar mal
  - Nombre "<ETIQUETA>.<ensayo>.csv": al subirlo, Edge Impulse toma la etiqueta de la
    parte del nombre anterior al primer punto.
"""

import json
import re
import sys
from pathlib import Path

import numpy as np
import pandas as pd

import adaptador_simulador
import calculo_columnas


# ============================== CONFIGURACIÓN ==============================

ORIGEN = 'PLACAS'           # 'PLACAS' o 'SIMULADOR'
ENSAYOS = 'TODOS'           # Lista de ensayos a exportar, p. ej. ['prueba_04'], o 'TODOS' (todos los del registro)

# ===========================================================================


RAIZ = Path(__file__).resolve().parent.parent          # extras/AI Integration
CARPETA_DATASETS_SIMULADOR = RAIZ / 'datasets_simulador'
ORIGENES = {
    'PLACAS':    {'carpeta': RAIZ / 'datasets_procesados' / 'placas',    'registro': 'ensayos_placas.json'},
    'SIMULADOR': {'carpeta': RAIZ / 'datasets_procesados' / 'simulador', 'registro': 'ensayos_simulador.json'},
}
NOMBRE_VALIDO = re.compile(r'^[A-Za-z0-9_-]+$')


class AvisoEnsayo(Exception):
    """El ensayo no se puede exportar (falta su crudo, sus datos no son válidos...). Se salta."""


def leer_json(ruta):
    try:
        return json.loads(Path(ruta).read_text(encoding='utf-8'))
    except json.JSONDecodeError as e:
        sys.exit(f'{Path(ruta).name} no es un JSON válido (línea {e.lineno}, columna {e.colno}): {e.msg}.\n'
                 'Corrígelo (suele ser una coma de más o de menos) y vuelve a ejecutar.')


def escribir_json(ruta, datos):
    Path(ruta).write_text(json.dumps(datos, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')


def elegir_ensayos(registro, nombre_registro):
    todos = [e['nombre'] for e in registro.get('ensayos', [])]
    if not todos:
        sys.exit(f'{nombre_registro} no tiene ningún ensayo: no hay nada que exportar.')
    if ENSAYOS == 'TODOS':
        return registro['ensayos']
    if isinstance(ENSAYOS, str) or not isinstance(ENSAYOS, (list, tuple)):
        sys.exit("ENSAYOS debe ser una lista, p. ej. ['prueba_04'], o 'TODOS'.")
    desconocidos = [n for n in ENSAYOS if n not in todos]
    if desconocidos:
        sys.exit(f'No están en {nombre_registro}: {desconocidos}.\nEnsayos registrados: {todos}')
    return [e for e in registro['ensayos'] if e['nombre'] in ENSAYOS]


# --- Paso 1: CSV en crudo ------------------------------------------------------

def obtener_crudo(ens, origen, dir_ensayo):
    if origen == 'SIMULADOR':
        carpeta = CARPETA_DATASETS_SIMULADOR / ens['carpeta_simulador']
        if not carpeta.exists():
            raise AvisoEnsayo(f'no existe su carpeta del simulador ({carpeta})')
        return adaptador_simulador.procesar(carpeta, ens['nombre'], dir_ensayo / 'crudo')
    ruta_crudo = ORIGENES['PLACAS']['carpeta'] / ens['crudo']
    if not ruta_crudo.exists():
        raise AvisoEnsayo(f'no existe su CSV en crudo ({ruta_crudo}). Si borraste la grabación, '
                          'borra también su entrada en ensayos_placas.json')
    return pd.read_csv(ruta_crudo)


# --- Paso 3: uso y etiqueta de cada ciclo ------------------------------------------

def etiquetas_por_marcas(col, ruta_marcas):
    """Fase de cada ciclo según el archivo de marcas del grabador (None antes de la primera marca)."""
    if not ruta_marcas.exists():
        raise AvisoEnsayo(f'no existe su archivo de marcas ({ruta_marcas})')
    marcas = pd.read_csv(ruta_marcas).sort_values('desde_ciclo').reset_index(drop=True)
    if marcas.empty:
        raise AvisoEnsayo(f'su archivo de marcas está vacío ({ruta_marcas})')
    i = np.searchsorted(marcas.desde_ciclo.values, col.ciclo.values, side='right') - 1
    return pd.Series([marcas.fase.iloc[k] if k >= 0 else None for k in i], index=col.index, dtype=object)


def etiquetar(col, ens, g, origen):
    t_s = col.tiempo_ms / 1000.0
    if origen == 'PLACAS':
        etiqueta = etiquetas_por_marcas(col, ORIGENES['PLACAS']['carpeta'] / ens['marcas'])
    else:
        etiqueta = pd.Series('OK', index=col.index, dtype=object)
        if ens.get('evento_s') is not None:
            etiqueta[t_s >= ens['evento_s']] = ens['etiqueta_tras_evento']

    uso = pd.Series('entrenamiento', index=col.index)
    uso[etiqueta.isna()] = 'sin_etiqueta'
    uso[col.ciclo < g['ciclos_descartados_inicio']] = 'descartado_arranque'
    if ens.get('fin_s') is not None:
        uso[t_s >= ens['fin_s']] = 'descartado_fin'
    return uso, etiqueta


# --- Un ensayo completo ---------------------------------------------------------

def exportar_ensayo(ens, g, origen):
    nombre = ens['nombre']
    if not NOMBRE_VALIDO.match(nombre):
        raise AvisoEnsayo('el nombre solo puede tener letras sin tildes, números, "_" y "-" (sin espacios)')
    dir_ensayo = ORIGENES[origen]['carpeta'] / nombre

    crudo = obtener_crudo(ens, origen, dir_ensayo)
    parametros = {k: g[k] for k in calculo_columnas.PARAMETROS_POR_DEFECTO if k in g}
    try:
        col, info = calculo_columnas.calcular(crudo, parametros)
    except (ValueError, KeyError) as e:
        raise AvisoEnsayo(f'no se pueden calcular sus columnas: {e}')
    uso, etiqueta = etiquetar(col, ens, g, origen)

    # Aviso si alguna referencia se ha calibrado con medidas de ciclos que no son OK
    avisos = list(info['avisos_calibracion'])
    fase = dict(zip(col.ciclo, etiqueta))
    for medida, c in info['calibracion'].items():
        no_ok = {}
        for k in c['ciclos_muestras']:
            if fase.get(k) is not None and fase[k] != 'OK':
                no_ok[fase[k]] = no_ok.get(fase[k], 0) + 1
        for etq, n in no_ok.items():
            corto = medida.replace('enlace_', '').replace('salto2_', '')
            avisos.append(f'{corto}: {n} de sus {c["medidas"]} medidas de calibración son de fase {etq}')

    # Revisión: todos los ciclos, con su uso y su etiqueta
    dir_revision, dir_ei = dir_ensayo / 'revision', dir_ensayo / 'edge_impulse'
    dir_revision.mkdir(parents=True, exist_ok=True)
    dir_ei.mkdir(parents=True, exist_ok=True)
    revision = col.copy()
    revision.insert(2, 'uso', uso)
    revision.insert(3, 'etiqueta', etiqueta)
    revision.to_csv(dir_revision / f'revision_{nombre}.csv', index=False, float_format='%.6f')

    # Edge Impulse: un archivo por etiqueta (y por tramo de ciclos consecutivos)
    for viejo in dir_ei.glob('*.csv'):
        viejo.unlink()
    entradas = calculo_columnas.columnas_modelo()
    recuentos = {}
    validos = revision[revision.uso == 'entrenamiento']
    for etq, grupo in validos.groupby('etiqueta', sort=False):
        tramo = (grupo.ciclo.diff() != 1).cumsum()
        partes = [p for _, p in grupo.groupby(tramo)]
        for k, parte in enumerate(partes, start=1):
            sufijo = '' if len(partes) == 1 else f'_p{k}'
            ei = parte[entradas].copy()
            ei.insert(0, 'tiempo_ms', parte.ciclo * g['periodo_nominal_ms'])
            ei.to_csv(dir_ei / f'{etq}.{nombre}{sufijo}.csv', index=False, float_format='%.6f')
        recuentos[etq] = {'filas': int(len(grupo)), 'ciclos': [int(grupo.ciclo.min()), int(grupo.ciclo.max())]}
        if len(partes) > 1:
            recuentos[etq]['tramos'] = [[int(p.ciclo.min()), int(p.ciclo.max())] for p in partes]

    # Resumen: se conservan fecha, descripción y datos de la grabación; se reescribe "procesado"
    ruta_resumen = dir_ensayo / f'resumen_{nombre}.json'
    resumen = leer_json(ruta_resumen) if ruta_resumen.exists() else {}
    if origen == 'SIMULADOR':
        resumen = {'descripcion': ens.get('descripcion', '')}
    else:
        resumen = {'fecha': resumen.get('fecha', ''), 'descripcion': resumen.get('descripcion', ''),
                   **{k: v for k, v in resumen.items() if k not in ('fecha', 'descripcion', 'procesado')}}
    resumen['procesado'] = {
        'ciclos_totales': int(len(col)),
        'descartados': {k: int(v) for k, v in uso[uso != 'entrenamiento'].value_counts().items()},
        'archivos_edge_impulse': recuentos,
        'calibracion': {k.replace('enlace_', '').replace('salto2_', ''): {
            'referencia_m': None if c['referencia_m'] is None else round(c['referencia_m'], 4),
            'medidas': c['medidas'], 'desde_ciclo': c['desde_ciclo'], 'fijada_en_ciclo': c['fijada_en_ciclo']}
            for k, c in info['calibracion'].items()},
        'avisos_calibracion': avisos,
    }
    escribir_json(ruta_resumen, resumen)

    # Registro de las placas: parámetros usados y rutas de los resultados
    if origen == 'PLACAS':
        ens['parametros_usados'] = dict(g)
        ens['revision'] = f'{nombre}/revision/revision_{nombre}.csv'
        ens['edge_impulse'] = f'{nombre}/edge_impulse/'
        ens['resumen'] = f'{nombre}/resumen_{nombre}.json'

    print(f'  {nombre}: ' + ', '.join(f"{k} {v['filas']} filas" + (f" en {len(v['tramos'])} tramos" if 'tramos' in v else '')
                                      for k, v in recuentos.items()))
    for aviso in avisos:
        print(f'     aviso de calibración: {aviso}')


def main():
    if ORIGEN not in ORIGENES:
        sys.exit(f"ORIGEN debe ser 'PLACAS' o 'SIMULADOR' (ahora es {ORIGEN!r}).")
    carpeta = ORIGENES[ORIGEN]['carpeta']
    ruta_registro = carpeta / ORIGENES[ORIGEN]['registro']
    if not ruta_registro.exists():
        extra = ' Se crea solo al terminar la primera grabación con grabador_serie.py.' if ORIGEN == 'PLACAS' else ''
        sys.exit(f'No existe el registro de ensayos {ruta_registro}.{extra}')
    registro = leer_json(ruta_registro)
    g = registro['general']
    elegidos = elegir_ensayos(registro, ruta_registro.name)

    print(f'Exportando {len(elegidos)} ensayo(s) de {ORIGEN} ({ruta_registro.name}):')
    saltados = []
    for ens in elegidos:
        try:
            exportar_ensayo(ens, g, ORIGEN)
        except AvisoEnsayo as e:
            saltados.append(ens['nombre'])
            print(f"  AVISO: {ens['nombre']} no se ha exportado: {e}")

    if ORIGEN == 'PLACAS':
        escribir_json(ruta_registro, registro)   # parámetros usados y rutas de los resultados
    print(f'\nHecho: {len(elegidos) - len(saltados)} exportado(s)' +
          (f', {len(saltados)} saltado(s): {saltados}' if saltados else '') + f'. Resultados en {carpeta}')


if __name__ == '__main__':
    main()
