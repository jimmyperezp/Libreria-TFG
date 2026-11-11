"""
Definición del CSV en crudo.

Es el formato común a los dos orígenes de datos:
  - el simulador (lo genera adaptador_simulador.py),
  - las placas (lo imprime el coordinador como RAW_DATA y lo guarda grabador_serie.py).
Si cambia, hay que cambiar también printRawHeader() / printRawRow() del coordinador
(Coordinator.ino): el grabador comprueba que la cabecera RAW_HEADER coincide con esta.

Una fila por ciclo de comunicación. Columnas fijas para 5 vehículos:
el coordinador (c) y cuatro vagones (v1..v4), en el orden del tren.
Los nombres van por POSICIÓN; los identificadores reales de cada
dispositivo se guardan aparte (composición).

Columnas:
  ciclo, tiempo_ms, duracion_ciclo_ms
  Por cada enlace (vehículos consecutivos: c_v1, v1_v2, v2_v3, v3_v4):
    enlace_X_Y_visto, enlace_X_Y_distancia_m
  Por cada par a dos saltos (nieto: c_v2, v1_v3, v2_v4):
    salto2_X_Z_visto, salto2_X_Z_distancia_m
  Por cada vehículo (c, v1..v4):
    X_acel_x_ms2, X_acel_y_ms2

Contenido: lo que trae el informe de modo modelo (distancias con el hijo y el nieto, ax y ay de cada
nodo), más la duración del ciclo, que mide el coordinador. No hay potencia RX.
Se graba más de lo que usa el modelo (duración del ciclo, acel_y) por si entra en el futuro.

Convenio para lo que no se ha recibido: celda vacía y visto = 0.

Pares a dos saltos:
  - placas: c_v2 es la medida del coordinador con v2; v1_v3 y v2_v4, las de cada nodo con su nieto.
  - simulador: c_v2 es la distancia del coordinador a v2; v1_v3 y v2_v4 salen por resta.
"""

VEHICULOS = ['c', 'v1', 'v2', 'v3', 'v4']          # c = coordinador (locomotora)
VAGONES = VEHICULOS[1:]
ENLACES = [(VEHICULOS[i], VEHICULOS[i + 1]) for i in range(len(VEHICULOS) - 1)]   # consecutivos
PARES_SALTO2 = [(VEHICULOS[i], VEHICULOS[i + 2]) for i in range(len(VEHICULOS) - 2)]  # a dos saltos (nieto)

EJES_IMU = ['acel_x_ms2', 'acel_y_ms2']


def nombre_enlace(a, b):
    return f'enlace_{a}_{b}'


def nombre_salto2(a, b):
    return f'salto2_{a}_{b}'


def columnas_crudo():
    cols = ['ciclo', 'tiempo_ms', 'duracion_ciclo_ms']
    for a, b in ENLACES:
        e = nombre_enlace(a, b)
        cols += [f'{e}_visto', f'{e}_distancia_m']
    for a, b in PARES_SALTO2:
        s = nombre_salto2(a, b)
        cols += [f'{s}_visto', f'{s}_distancia_m']
    for v in VEHICULOS:
        cols += [f'{v}_{eje}' for eje in EJES_IMU]
    return cols
