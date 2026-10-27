"""
Exportador: CSV en crudo -> CSV de entrenamiento para Edge Impulse.

Uso:
  - Desde VS Code: elige el registro en REGISTRO (configuración, abajo) y pulsa el botón de ejecutar (Play).
  - Desde la terminal (opcional, sustituye a REGISTRO):
      python exportar_edge_impulse.py ../ensayos_esp32.json

Para cada ensayo del archivo de configuración:
  1. Si el ensayo indica "carpeta_simulador", ejecuta antes el adaptador del
     simulador para obtener su CSV en crudo. Si indica "crudo", usa ese CSV
     directamente (así se tratarán los datos de las ESP32).
  2. Calcula las 37 columnas del modelo (calculo_columnas.py).
  3. Asigna a cada ciclo su uso y su etiqueta:
       - descartado_arranque: los primeros ciclos (transitorio de la simulación)
       - descartado_fin:      a partir de "fin_s", si se indica
       - sin_etiqueta:        ciclos anteriores a la primera marca (solo con "marcas")
       - entrenamiento:       el resto (los ciclos de calibración incluidos)
     La etiqueta sale de UNA de estas dos fuentes:
       - "evento_s" (simulador): OK hasta el evento; a partir del primer ciclo que
         termina después de "evento_s", la etiqueta "etiqueta_tras_evento".
       - "marcas" (ESP32): el archivo marcas_<ensayo>.csv del grabador. Cada ciclo lleva
         la fase de la última marca cuyo desde_ciclo sea menor o igual que su ciclo.
         Admite cualquier número de fases (OK -> LOST_INTEGRITY -> OK ...).
  4. Escribe:
       columnas/columnas_<ensayo>.csv          todos los ciclos, con uso y etiqueta (para revisar)
       edge_impulse/<ETIQUETA>.<ensayo>.csv    un archivo por etiqueta, listo para Edge Impulse
       resumen.json                            referencias de calibración, recuentos, avisos
     Si los ciclos de una etiqueta no son consecutivos (fases repetidas, o ciclos perdidos
     en la grabación), se escribe un archivo por tramo: <ETIQUETA>.<ensayo>_p1.csv, _p2...

Formato de los archivos para Edge Impulse:
  - Primera columna, tiempo_ms: tiempo NOMINAL equiespaciado (ciclo x periodo),
    porque Edge Impulse supone muestras equiespaciadas. La duración real
    de cada ciclo va como columna de entrada (duracion_ciclo_ms).
  - Después, las 37 columnas de entrada, en el mismo orden en todos los archivos.
  - Nombre "<ETIQUETA>.<ensayo>.csv": al subirlo, Edge Impulse puede tomar la
    etiqueta de la parte del nombre anterior al primer punto.
"""

import json
import sys
from pathlib import Path

import numpy as np
import pandas as pd

import adaptador_simulador
import calculo_columnas


# ============================== CONFIGURACIÓN ==============================

# Registro de ensayos que se exporta. Ruta absoluta o relativa a la carpeta de este script:
#   '../ensayos_esp32.json'       grabaciones de las placas
#   '../ensayos_simulador.json'   ensayos del simulador
REGISTRO = '../ensayos_esp32.json'

# ===========================================================================


def ruta(base, p):
    p = Path(p)
    return p if p.is_absolute() else (base / p).resolve()


def obtener_crudo(ens, base, carpeta_crudo):
    if 'carpeta_simulador' in ens:
        cfg, rx, imu = adaptador_simulador.leer_ensayo(ruta(base, ens['carpeta_simulador']))
        crudo, comp = adaptador_simulador.adaptar(cfg, rx, imu)
        comp['ensayo'] = ens['nombre']
        comp['carpeta_origen'] = Path(ens['carpeta_simulador']).name
        carpeta_crudo.mkdir(parents=True, exist_ok=True)
        crudo.to_csv(carpeta_crudo / f"crudo_{ens['nombre']}.csv", index=False, float_format='%.6f')
        (carpeta_crudo / f"composicion_{ens['nombre']}.json").write_text(
            json.dumps(comp, indent=2, ensure_ascii=False), encoding='utf-8')
        return crudo
    return pd.read_csv(ruta(base, ens['crudo']))


def etiquetas_por_marcas(col, ruta_marcas):
    """Fase de cada ciclo según el archivo de marcas del grabador (None antes de la primera marca)."""
    marcas = pd.read_csv(ruta_marcas).sort_values('desde_ciclo').reset_index(drop=True)
    if marcas.empty:
        raise ValueError(f'El archivo de marcas está vacío: {ruta_marcas}')
    i = np.searchsorted(marcas.desde_ciclo.values, col.ciclo.values, side='right') - 1
    return pd.Series([marcas.fase.iloc[k] if k >= 0 else None for k in i], index=col.index, dtype=object)


def etiquetar(col, ens, g, base):
    t_s = col.tiempo_ms / 1000.0
    if 'marcas' in ens and ens.get('evento_s') is not None:
        raise ValueError(f"{ens['nombre']}: usa 'evento_s' o 'marcas', no los dos")

    if 'marcas' in ens:
        etiqueta = etiquetas_por_marcas(col, ruta(base, ens['marcas']))
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


def main():
    if len(sys.argv) > 2:
        sys.exit(__doc__)
    archivo = Path(sys.argv[1]) if len(sys.argv) == 2 else Path(REGISTRO)
    if not archivo.is_absolute() and len(sys.argv) < 2:
        archivo = Path(__file__).resolve().parent / archivo   # REGISTRO se cuenta desde la carpeta del script
    archivo = archivo.resolve()
    if not archivo.exists():
        sys.exit(f'No existe el registro de ensayos: {archivo}')
    print(f'Registro: {archivo}')
    base = archivo.parent
    cfg = json.loads(archivo.read_text(encoding='utf-8'))
    g = cfg['general']
    if not cfg.get('ensayos'):
        sys.exit(f'La lista "ensayos" de {archivo.name} está vacía: no hay nada que exportar.\n'
                 'Añade cada grabación como una entrada DENTRO de "ensayos": [ ... ] '
                 '(con "nombre", "crudo" y "marcas"; o "carpeta_simulador" para el simulador).')
    salida = ruta(base, g['carpeta_salida'])
    (salida / 'columnas').mkdir(parents=True, exist_ok=True)
    (salida / 'edge_impulse').mkdir(parents=True, exist_ok=True)
    parametros = {k: g[k] for k in calculo_columnas.PARAMETROS_POR_DEFECTO if k in g}
    entradas = calculo_columnas.columnas_modelo()

    resumen = {'parametros': g, 'ensayos': {}}
    for ens in cfg['ensayos']:
        nombre = ens['nombre']
        crudo = obtener_crudo(ens, base, salida / 'crudo')
        col, info = calculo_columnas.calcular(crudo, parametros)
        if info['ciclos_calibracion'][0] < g['ciclos_descartados_inicio']:
            raise ValueError('La calibración no puede empezar en ciclos descartados')
        uso, etiqueta = etiquetar(col, ens, g, base)
        en_calibracion = col.ciclo <= info['ciclos_calibracion'][1]
        if (etiqueta[en_calibracion].notna() & (etiqueta[en_calibracion] != 'OK')).any():
            raise ValueError(f'{nombre}: hay una fase distinta de OK durante la calibración '
                             f'(ciclos hasta {info["ciclos_calibracion"][1]}): el tren debe estar íntegro al arrancar')

        revision = col.copy()
        revision.insert(2, 'uso', uso)
        revision.insert(3, 'etiqueta', etiqueta)
        revision.to_csv(salida / 'columnas' / f'columnas_{nombre}.csv', index=False, float_format='%.6f')

        recuentos = {}
        validos = revision[revision.uso == 'entrenamiento']
        for etq, grupo in validos.groupby('etiqueta', sort=False):
            # Edge Impulse supone filas equiespaciadas: cada tramo de ciclos consecutivos va en su archivo
            tramo = (grupo.ciclo.diff() != 1).cumsum()
            partes = [p for _, p in grupo.groupby(tramo)]
            for k, parte in enumerate(partes, start=1):
                sufijo = '' if len(partes) == 1 else f'_p{k}'
                ei = parte[entradas].copy()
                ei.insert(0, 'tiempo_ms', parte.ciclo * g['periodo_nominal_ms'])
                ei.to_csv(salida / 'edge_impulse' / f'{etq}.{nombre}{sufijo}.csv', index=False, float_format='%.6f')
            recuentos[etq] = {'filas': int(len(grupo)),
                              'ciclos': [int(grupo.ciclo.min()), int(grupo.ciclo.max())]}
            if len(partes) > 1:
                recuentos[etq]['tramos'] = [[int(p.ciclo.min()), int(p.ciclo.max())] for p in partes]
        resumen['ensayos'][nombre] = {
            'descripcion': ens.get('descripcion', ''),
            'ciclos_totales': int(len(col)),
            'descartados': uso[uso != 'entrenamiento'].value_counts().to_dict(),
            'archivos': recuentos,
            'referencias_calibracion_m': {k: round(v, 4) for k, v in info['referencias_m'].items()},
            'avisos_calibracion': info['avisos_calibracion'],
        }
        print(f"{nombre}: " + ', '.join(f"{k} {v['filas']} filas" + (f" en {len(v['tramos'])} tramos" if 'tramos' in v else '')
                                        for k, v in recuentos.items()))
        for aviso in info['avisos_calibracion']:
            print(f'   aviso de calibración: {aviso}')

    (salida / 'resumen.json').write_text(json.dumps(resumen, indent=2, ensure_ascii=False), encoding='utf-8')
    print(f'Resumen en {salida / "resumen.json"}')


if __name__ == '__main__':
    main()
