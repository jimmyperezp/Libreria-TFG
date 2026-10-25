"""
Exportador: CSV en crudo -> CSV de entrenamiento para Edge Impulse.

Uso:
  python exportar_edge_impulse.py <archivo_de_ensayos.json>

Para cada ensayo del archivo de configuración:
  1. Si el ensayo indica "carpeta_simulador", ejecuta antes el adaptador del
     simulador para obtener su CSV en crudo. Si indica "crudo", usa ese CSV
     directamente (así se tratarán los datos de las ESP32).
  2. Calcula las 37 columnas del modelo (calculo_columnas.py).
  3. Asigna a cada ciclo su uso y su etiqueta:
       - descartado_arranque: los primeros ciclos (transitorio de la simulación)
       - descartado_fin:      a partir de "fin_s", si se indica
       - entrenamiento:       el resto (los ciclos de calibración incluidos)
     Etiqueta: OK hasta el evento; a partir del primer ciclo que termina
     después de "evento_s", la etiqueta indicada en "etiqueta_tras_evento".
  4. Escribe:
       columnas/columnas_<ensayo>.csv          todos los ciclos, con uso y etiqueta (para revisar)
       edge_impulse/<ETIQUETA>.<ensayo>.csv    un archivo por etiqueta, listo para Edge Impulse
       resumen.json                            referencias de calibración, recuentos, avisos

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

import pandas as pd

import adaptador_simulador
import calculo_columnas


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


def etiquetar(col, ens, g):
    t_s = col.tiempo_ms / 1000.0
    uso = pd.Series('entrenamiento', index=col.index)
    uso[col.ciclo < g['ciclos_descartados_inicio']] = 'descartado_arranque'
    if ens.get('fin_s') is not None:
        uso[t_s >= ens['fin_s']] = 'descartado_fin'
    etiqueta = pd.Series('OK', index=col.index)
    if ens.get('evento_s') is not None:
        etiqueta[t_s >= ens['evento_s']] = ens['etiqueta_tras_evento']
    return uso, etiqueta


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    archivo = Path(sys.argv[1]).resolve()
    base = archivo.parent
    cfg = json.loads(archivo.read_text(encoding='utf-8'))
    g = cfg['general']
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
        uso, etiqueta = etiquetar(col, ens, g)
        if (etiqueta[col.ciclo <= info['ciclos_calibracion'][1]] != 'OK').any():
            raise ValueError(f'{nombre}: el evento ocurre durante la calibración')

        revision = col.copy()
        revision.insert(2, 'uso', uso)
        revision.insert(3, 'etiqueta', etiqueta)
        revision.to_csv(salida / 'columnas' / f'columnas_{nombre}.csv', index=False, float_format='%.6f')

        recuentos = {}
        validos = revision[revision.uso == 'entrenamiento']
        for etq, grupo in validos.groupby('etiqueta', sort=False):
            if (grupo.ciclo.diff().dropna() != 1).any():
                raise ValueError(f'{nombre}/{etq}: los ciclos no son consecutivos')
            ei = grupo[entradas].copy()
            ei.insert(0, 'tiempo_ms', grupo.ciclo * g['periodo_nominal_ms'])
            ei.to_csv(salida / 'edge_impulse' / f'{etq}.{nombre}.csv', index=False, float_format='%.6f')
            recuentos[etq] = {'filas': int(len(grupo)),
                              'ciclos': [int(grupo.ciclo.min()), int(grupo.ciclo.max())]}
        resumen['ensayos'][nombre] = {
            'descripcion': ens.get('descripcion', ''),
            'ciclos_totales': int(len(col)),
            'descartados': uso[uso != 'entrenamiento'].value_counts().to_dict(),
            'archivos': recuentos,
            'referencias_calibracion_m': {k: round(v, 4) for k, v in info['referencias_m'].items()},
            'avisos_calibracion': info['avisos_calibracion'],
        }
        print(f"{nombre}: " + ', '.join(f"{k} {v['filas']} filas" for k, v in recuentos.items()))

    (salida / 'resumen.json').write_text(json.dumps(resumen, indent=2, ensure_ascii=False), encoding='utf-8')
    print(f'Resumen en {salida / "resumen.json"}')


if __name__ == '__main__':
    main()
