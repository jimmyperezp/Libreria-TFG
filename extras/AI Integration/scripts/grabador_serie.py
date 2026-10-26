"""
Grabador del puerto serie: registra un ensayo con las ESP32.

Lee lo que imprime el coordinador y guarda, en grabaciones/<ensayo>/:
  crudo_<ensayo>.csv           filas RAW_DATA, con el mismo formato que el CSV en crudo del simulador
  columnas_placa_<ensayo>.csv  filas FEATURES: las 37 columnas que calculó la propia placa
  marcas_<ensayo>.csv          las marcas de fase pulsadas durante el ensayo
  monitor_<ensayo>.txt         todo el monitor serie, tal cual llega (con la hora del PC)
  info_<ensayo>.json           fecha, puerto, commit del repositorio y resumen de la grabación

Marcas de fase: durante la grabación, pulsa una de las teclas definidas en config_grabador.json
(por defecto: o = OK, l = LOST_INTEGRITY). La marca se guarda con el RELOJ DEL COORDINADOR:
la fase nueva empieza en el ciclo siguiente al último recibido (columna desde_ciclo).
Al empezar se pone automáticamente una marca con la fase inicial (fase_inicial).

Para terminar: la tecla de salida (por defecto q) o Ctrl+C. Los archivos se escriben línea a
línea, así que no se pierde nada si el programa se corta.

Uso (desde la carpeta scripts/):
  python grabador_serie.py ../config_grabador.json --ensayo desconexion_v3
  python grabador_serie.py ../config_grabador.json --ensayo prueba --puerto COM5 --ver-monitor

Requisito: pyserial (pip install pyserial).
"""

import argparse
import csv
import json
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

from formato_crudo import columnas_crudo

PREFIJO_CABECERA = 'RAW_HEADER:'
PREFIJO_FILA = 'RAW_DATA:'
PREFIJO_CABECERA_COLUMNAS = 'FEATURES_HEADER:'
PREFIJO_COLUMNAS = 'FEATURES:'
NUM_COLUMNAS_MODELO = 37


class ErrorGrabacion(Exception):
    """Situación en la que seguir grabando produciría datos inservibles."""


class Grabador:
    """Procesa las líneas del monitor serie y escribe los archivos del ensayo.
    No sabe de dónde vienen las líneas (puerto serie o archivo): así se puede probar sin placas."""

    def __init__(self, carpeta, ensayo, teclas, fase_inicial, info):
        self.carpeta = Path(carpeta)
        self.carpeta.mkdir(parents=True, exist_ok=True)
        self.ensayo = ensayo
        self.teclas = teclas
        self.columnas = columnas_crudo()

        self.f_crudo = open(self.carpeta / f'crudo_{ensayo}.csv', 'w', newline='', encoding='utf-8')
        self.f_placa = open(self.carpeta / f'columnas_placa_{ensayo}.csv', 'w', newline='', encoding='utf-8')
        self.f_marcas = open(self.carpeta / f'marcas_{ensayo}.csv', 'w', newline='', encoding='utf-8')
        self.f_monitor = open(self.carpeta / f'monitor_{ensayo}.txt', 'w', encoding='utf-8')
        self.w_crudo = csv.writer(self.f_crudo)
        self.w_placa = csv.writer(self.f_placa)
        self.w_marcas = csv.writer(self.f_marcas)
        self.w_crudo.writerow(self.columnas)
        self.w_marcas.writerow(['desde_ciclo', 'tiempo_ms_coordinador', 'fase', 'hora_pc'])
        self.cabecera_placa_escrita = False

        self.info = dict(info, ensayo=ensayo, inicio=datetime.now().isoformat(timespec='seconds'))
        self.cabecera_validada = False
        self.ultimo_ciclo = None
        self.ultimo_tiempo_ms = None
        self.filas = 0
        self.filas_placa = 0
        self.ciclos_perdidos = 0
        self.lineas_corruptas = 0
        self.marcas = 0
        self.fase_actual = None
        self.fase_inicial = fase_inicial
        self.fase_inicial_pendiente = fase_inicial is not None

    # --- Entrada de líneas -------------------------------------------------

    def procesar_linea(self, linea):
        linea = linea.strip()
        if not linea:
            return
        self.f_monitor.write(f'{datetime.now().strftime("%H:%M:%S.%f")[:-3]}  {linea}\n')

        if linea.startswith(PREFIJO_CABECERA):
            self._cabecera(linea[len(PREFIJO_CABECERA):])
        elif linea.startswith(PREFIJO_FILA):
            self._fila(linea[len(PREFIJO_FILA):])
        elif linea.startswith(PREFIJO_CABECERA_COLUMNAS):
            self._cabecera_placa(linea[len(PREFIJO_CABECERA_COLUMNAS):])
        elif linea.startswith(PREFIJO_COLUMNAS):
            self._fila_placa(linea[len(PREFIJO_COLUMNAS):])

    def _cabecera(self, texto):
        recibidas = texto.split(',')
        if recibidas != self.columnas:
            raise ErrorGrabacion(
                'La cabecera RAW_HEADER del coordinador no coincide con formato_crudo.py.\n'
                f'  Esperadas ({len(self.columnas)}): {self.columnas}\n  Recibidas ({len(recibidas)}): {recibidas}\n'
                'El firmware y los scripts no son de la misma versión.')
        if self.filas > 0:
            # Una cabecera a mitad de grabación significa que el coordinador se ha reiniciado:
            # el reloj y el número de ciclo vuelven a 0 y las marcas dejarían de cuadrar.
            raise ErrorGrabacion('El coordinador se ha reiniciado durante la grabación. '
                                 'Lo grabado hasta aquí se conserva; empieza una grabación nueva.')
        self.cabecera_validada = True
        print('Cabecera RAW_HEADER del coordinador validada.')

    def _fila(self, texto):
        valores = texto.split(',')
        if len(valores) != len(self.columnas):
            self.lineas_corruptas += 1
            return
        try:
            ciclo = int(valores[0])
            tiempo_ms = int(valores[1])
        except ValueError:
            self.lineas_corruptas += 1
            return

        if self.ultimo_ciclo is not None:
            if ciclo <= self.ultimo_ciclo:
                raise ErrorGrabacion(f'El número de ciclo ha retrocedido ({self.ultimo_ciclo} -> {ciclo}): '
                                     'el coordinador se ha reiniciado. Empieza una grabación nueva.')
            self.ciclos_perdidos += ciclo - self.ultimo_ciclo - 1

        if self.filas == 0 and not self.cabecera_validada:
            print('AVISO: no se recibió RAW_HEADER (el grabador arrancó después que el coordinador). '
                  'Solo se ha comprobado el número de columnas.')

        self.w_crudo.writerow(valores)
        self.f_crudo.flush()
        self.ultimo_ciclo, self.ultimo_tiempo_ms = ciclo, tiempo_ms
        self.filas += 1

        if self.fase_inicial_pendiente:
            # La fase inicial cubre desde la primera fila grabada
            self._escribir_marca(ciclo, tiempo_ms, self.fase_inicial)
            self.fase_inicial_pendiente = False

    def _cabecera_placa(self, texto):
        if not self.cabecera_placa_escrita:
            self.w_placa.writerow(texto.split(','))
            self.cabecera_placa_escrita = True

    def _fila_placa(self, texto):
        valores = texto.split(',')
        if len(valores) != NUM_COLUMNAS_MODELO + 1:
            self.lineas_corruptas += 1
            return
        self.w_placa.writerow(valores)
        self.f_placa.flush()
        self.filas_placa += 1

    # --- Marcas -----------------------------------------------------------

    def marcar(self, tecla):
        """Pone una marca de fase. Devuelve un texto para mostrar, o None si la tecla no es de fase."""
        fase = self.teclas.get(tecla)
        if fase is None:
            return None
        if self.ultimo_ciclo is None:
            # Aún no ha llegado ninguna fila: la fase elegida pasa a ser la inicial
            self.fase_inicial, self.fase_inicial_pendiente = fase, True
            return f'Fase inicial: {fase} (empezará con la primera fila recibida)'
        # La fase nueva empieza en el ciclo siguiente al último recibido
        self._escribir_marca(self.ultimo_ciclo + 1, self.ultimo_tiempo_ms, fase)
        return f'MARCA: {fase} desde el ciclo {self.ultimo_ciclo + 1}'

    def _escribir_marca(self, desde_ciclo, tiempo_ms, fase):
        self.w_marcas.writerow([desde_ciclo, tiempo_ms, fase, datetime.now().isoformat(timespec='milliseconds')])
        self.f_marcas.flush()
        self.marcas += 1
        self.fase_actual = fase

    # --- Estado y cierre ----------------------------------------------------

    def estado(self):
        return (f'ciclo {self.ultimo_ciclo if self.ultimo_ciclo is not None else "-"} | fase {self.fase_actual or "-"} | '
                f'filas {self.filas} | perdidos {self.ciclos_perdidos} | corruptas {self.lineas_corruptas} | marcas {self.marcas}')

    def cerrar(self, motivo):
        resumen = {
            'fin': datetime.now().isoformat(timespec='seconds'),
            'motivo_fin': motivo,
            'filas_crudo': self.filas,
            'filas_columnas_placa': self.filas_placa,
            'ciclos_perdidos': self.ciclos_perdidos,
            'lineas_corruptas': self.lineas_corruptas,
            'marcas': self.marcas,
            'cabecera_validada': self.cabecera_validada,
        }
        self.info.update(resumen)
        for f in (self.f_crudo, self.f_placa, self.f_marcas, self.f_monitor):
            f.close()
        (self.carpeta / f'info_{self.ensayo}.json').write_text(
            json.dumps(self.info, indent=2, ensure_ascii=False), encoding='utf-8')
        return resumen


# --- Teclado sin bloquear (Windows y Linux/macOS) -----------------------------

class Teclado:
    def __enter__(self):
        if sys.platform == 'win32':
            import msvcrt
            self._msvcrt = msvcrt
        else:
            import termios, tty
            self._fd = sys.stdin.fileno()
            self._termios = termios
            self._antes = termios.tcgetattr(self._fd)
            tty.setcbreak(self._fd)
        return self

    def tecla(self):
        """Devuelve la tecla pulsada (en minúscula) o None si no hay ninguna."""
        if sys.platform == 'win32':
            if self._msvcrt.kbhit():
                return self._msvcrt.getwch().lower()
            return None
        import select
        if select.select([sys.stdin], [], [], 0)[0]:
            return sys.stdin.read(1).lower()
        return None

    def __exit__(self, *exc):
        if sys.platform != 'win32':
            self._termios.tcsetattr(self._fd, self._termios.TCSADRAIN, self._antes)


def commit_repositorio():
    try:
        r = subprocess.run(['git', 'rev-parse', '--short', 'HEAD'], capture_output=True, text=True,
                           cwd=Path(__file__).parent, timeout=5)
        return r.stdout.strip() or None
    except Exception:
        return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('config', help='config_grabador.json')
    ap.add_argument('--ensayo', required=True, help='nombre del ensayo (se usa en la carpeta y los archivos)')
    ap.add_argument('--puerto', help='puerto serie (sustituye al de la configuración)')
    ap.add_argument('--notas', default='', help='texto libre que se guarda en info_<ensayo>.json')
    ap.add_argument('--ver-monitor', action='store_true', help='mostrar también todo el monitor serie')
    args = ap.parse_args()

    ruta_config = Path(args.config).resolve()
    cfg = json.loads(ruta_config.read_text(encoding='utf-8'))
    teclas = {k.lower(): v for k, v in cfg['teclas'].items()}
    tecla_salir = cfg.get('tecla_salir', 'q').lower()
    if tecla_salir in teclas:
        sys.exit(f'La tecla de salida "{tecla_salir}" también está asignada a una fase.')
    puerto = args.puerto or cfg['puerto']
    carpeta = ruta_config.parent / cfg['carpeta_salida'] / args.ensayo
    if carpeta.exists() and any(carpeta.iterdir()):
        sys.exit(f'Ya existe una grabación en {carpeta}. Usa otro nombre de ensayo.')

    import serial  # pyserial
    info = {'puerto': puerto, 'baudios': cfg['baudios'], 'commit_repositorio': commit_repositorio(),
            'teclas': teclas, 'notas': args.notas}
    grabador = Grabador(carpeta, args.ensayo, teclas, cfg.get('fase_inicial'), info)

    print(f'Grabando "{args.ensayo}" desde {puerto} en {carpeta}')
    print('Teclas: ' + ', '.join(f'{k} = {v}' for k, v in teclas.items()) + f', {tecla_salir} = terminar\n')

    motivo = 'tecla de salida'
    try:
        with serial.Serial(puerto, cfg['baudios'], timeout=0.05) as ser, Teclado() as teclado:
            ultimo_estado = 0.0
            while True:
                linea = ser.readline()
                if linea:
                    texto = linea.decode('utf-8', errors='replace')
                    if args.ver_monitor:
                        print(texto.rstrip())
                    grabador.procesar_linea(texto)

                t = teclado.tecla()
                if t == tecla_salir:
                    break
                if t:
                    mensaje = grabador.marcar(t)
                    if mensaje:
                        print('\n' + mensaje)

                if not args.ver_monitor and time.time() - ultimo_estado > 0.5:
                    print('\r' + grabador.estado() + '   ', end='', flush=True)
                    ultimo_estado = time.time()
    except KeyboardInterrupt:
        motivo = 'Ctrl+C'
    except ErrorGrabacion as e:
        motivo = f'error: {e}'
        print(f'\n\nGRABACIÓN DETENIDA: {e}')
    except serial.SerialException as e:
        motivo = f'error del puerto serie: {e}'
        print(f'\n\nError con el puerto serie: {e}')

    resumen = grabador.cerrar(motivo)
    print('\n\nResumen:')
    for k, v in resumen.items():
        print(f'  {k}: {v}')
    print(f'Archivos en {carpeta}')


if __name__ == '__main__':
    main()
