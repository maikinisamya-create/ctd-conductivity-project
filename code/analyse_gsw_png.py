import sys
import io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')

# ===========================================================================
# analyse_gsw_v6.py
# Lecture serie ESP32 + calcul GSW (sigma specifique 25C + salinite SP)
# Compatible avec conductivite_adaptative_v6.ino
# IUEM Plouzane -- Samya Lmaikini
# ===========================================================================

import tkinter as tk
import serial
import serial.tools.list_ports
import gsw
from datetime import datetime
import time

LON_IUEM  = -4.55
LAT_IUEM  =  48.36
P_DBAR    =  0.0
BAUD_RATE = 115200

COLS_ARDUINO = [
    'temp_C', 'temp_source', 'gamme', 'n_iter', 'converge',
    'f_init_hz', 'sigma_init_mScm', 'f_opt_hz',
    'sigma_T_mScm', 'sigma25_mScm',
    'Rcell_ohm', 'phi_Z_deg', 'ratio_XR'
]

C_BG     = '#0A1628'
C_PANEL  = '#112240'
C_ACCENT = '#00C8FF'
C_GREEN  = '#00E676'
C_ORANGE = '#FF9800'
C_PURPLE = '#CE93D8'
C_WHITE  = '#E8F4FD'
C_GRAY   = '#8BA7C7'
C_RED    = '#FF5252'
C_YELLOW = '#FFD740'

# ═══════════════════════════════════════════════════════════════════════════
# DETECTION PORT SERIE
# ═══════════════════════════════════════════════════════════════════════════
def detect_port():
    ports = serial.tools.list_ports.comports()
    esp_ports = [p for p in ports if any(k in p.description
                 for k in ['CP210', 'CH340', 'UART', 'USB'])]
    if not esp_ports:
        for p in ports:
            print(f"  {p.device} -- {p.description}")
        return input("Entre le port (ex: COM3) : ").strip()
    if len(esp_ports) == 1:
        print(f"[PORT] ESP32 detecte : {esp_ports[0].device}")
        return esp_ports[0].device
    for i, p in enumerate(esp_ports):
        print(f"  [{i}] {p.device} -- {p.description}")
    return esp_ports[int(input("Choix : "))].device

# ═══════════════════════════════════════════════════════════════════════════
# CALCUL GSW -- sigma specifique 25C + salinite pratique SP uniquement
# ═══════════════════════════════════════════════════════════════════════════
def calculer_gsw(sigma_T_mScm, T_degC):
    try:
        # Etape 1 : sigma(T) + T → salinite pratique SP (PSU)
        SP = float(gsw.SP_from_C(sigma_T_mScm, T_degC, P_DBAR))

        # Etape 2 : SP → sigma specifique a 25C (mS/cm)
        sigma_spec_25 = float(gsw.C_from_SP(SP, 25.0, P_DBAR))

        return SP, sigma_spec_25
    except Exception as e:
        print(f"[GSW ERR] {e}")
        return None, None

# ═══════════════════════════════════════════════════════════════════════════
# PARSING CSV
# ═══════════════════════════════════════════════════════════════════════════
def parse_csv_line(line):
    parts = line.strip().split(',')
    if len(parts) != len(COLS_ARDUINO):
        return None
    try:
        row = {}
        for col, val in zip(COLS_ARDUINO, parts):
            if col in ('temp_source', 'gamme'):
                row[col] = val.strip()
            else:
                row[col] = float(val)
        return row
    except ValueError:
        return None

def read_one_cycle(ser):
    in_csv = False
    header_skipped = False
    while True:
        try:
            line = ser.readline().decode('utf-8', errors='replace').strip()
        except Exception:
            return None
        if not line:
            continue
        if '--- CSV ---' in line:
            in_csv = True
            header_skipped = False
            continue
        if '--- FIN CSV ---' in line:
            in_csv = False
            continue
        if in_csv:
            if not header_skipped:
                header_skipped = True
                continue
            row = parse_csv_line(line)
            if row:
                return row

# ═══════════════════════════════════════════════════════════════════════════
# TABLEAU DE BORD TKINTER
# ═══════════════════════════════════════════════════════════════════════════
class Dashboard:
    def __init__(self, root):
        self.root = root
        self.root.title("CN0349 v6 -- Tableau de bord GSW")
        self.root.configure(bg=C_BG)
        self.root.geometry("1280x780")

        f_title  = ("Consolas", 15, "bold")
        f_sub    = ("Consolas", 10)
        f_big    = ("Consolas", 34, "bold")
        f_med    = ("Consolas", 26, "bold")
        f_small  = ("Consolas", 11)
        f_label  = ("Consolas", 10)
        f_footer = ("Consolas", 9)

        # ── En-tete ───────────────────────────────────────────────────
        hdr = tk.Frame(self.root, bg=C_PANEL, bd=2, relief='flat')
        hdr.pack(fill='x', padx=10, pady=(8,4))
        hdr_row1 = tk.Frame(hdr, bg=C_PANEL)
        hdr_row1.pack(fill='x')
        self.lbl_cycle = tk.Label(hdr_row1, text="Cycle #000",
                                  bg=C_PANEL, fg=C_WHITE, font=f_small)
        self.lbl_cycle.pack(side='left', padx=10)
        tk.Label(hdr_row1,
                 text="CN0349 / AD5934 / ESP32  --  IUEM Plouzane",
                 bg=C_PANEL, fg=C_ACCENT, font=f_title).pack(side='left', expand=True)
        self.lbl_time = tk.Label(hdr_row1, text="--:--:--",
                                 bg=C_PANEL, fg=C_WHITE, font=f_small)
        self.lbl_time.pack(side='right', padx=10)
        tk.Label(hdr,
                 text="Algorithme adaptatif iteratif  |  Conductivite + Salinite  |  TEOS-10/GSW",
                 bg=C_PANEL, fg=C_GRAY, font=f_sub).pack(pady=(2,6))

        # ── Ligne 1 : Temperature | sigma(T) brute | SP ──────────────
        row1 = tk.Frame(self.root, bg=C_BG)
        row1.pack(fill='x', padx=10, pady=4)
        self.lbl_temp   = self._panel(row1, "TEMPERATURE",        "?", "degC",  C_ORANGE, f_big)
        self.lbl_sigmaT = self._panel(row1, "sigma(T) brute",     "?", "mS/cm", C_GREEN,  f_med)
        self.lbl_SP     = self._panel(row1, "SP   gsw.SP_from_C", "?", "PSU",   C_PURPLE, f_med)

        # ── Ligne 2 : sigma spec 25C | Algo adaptatif | Qualite mesure ─
        row2 = tk.Frame(self.root, bg=C_BG)
        row2.pack(fill='x', padx=10, pady=4)
        self.lbl_s25 = self._panel(row2, "sigma specifique (25C)", "?", "mS/cm", C_GREEN, ("Consolas", 30, "bold"))

        # Algo
        p_algo = tk.Frame(row2, bg=C_PANEL, bd=1, relief='solid')
        p_algo.pack(side='left', fill='both', expand=True, padx=4)
        tk.Label(p_algo, text="ALGO ADAPTATIF",
                 bg=C_PANEL, fg=C_ACCENT, font=("Consolas", 10, "bold")).pack(pady=(8,0))
        tk.Label(p_algo, text="f_opt", bg=C_PANEL, fg=C_GRAY, font=f_label).pack()
        self.lbl_fopt = tk.Label(p_algo, text="?",
                                 bg=C_PANEL, fg=C_ORANGE, font=("Consolas", 24, "bold"))
        self.lbl_fopt.pack()
        tk.Label(p_algo, text="kHz", bg=C_PANEL, fg=C_ORANGE, font=f_small).pack()
        self.lbl_algo = tk.Label(p_algo, text="", bg=C_PANEL, fg=C_GRAY, font=f_footer)
        self.lbl_algo.pack()
        self.lbl_conv = tk.Label(p_algo, text="", bg=C_PANEL,
                                 fg=C_GREEN, font=("Consolas", 10, "bold"))
        self.lbl_conv.pack(pady=(0,8))

        # Qualite
        p_qual = tk.Frame(row2, bg=C_PANEL, bd=1, relief='solid')
        p_qual.pack(side='left', fill='both', expand=True, padx=(4,0))
        tk.Label(p_qual, text="QUALITE MESURE",
                 bg=C_PANEL, fg=C_ACCENT, font=("Consolas", 10, "bold")).pack(pady=(8,0))
        self.lbl_rcell = tk.Label(p_qual, text="Rcell   = ?",
                                  bg=C_PANEL, fg=C_WHITE, font=f_small)
        self.lbl_rcell.pack(anchor='w', padx=15, pady=(4,0))
        self.lbl_phase = tk.Label(p_qual, text="Phase Z = ?",
                                  bg=C_PANEL, fg=C_GREEN, font=f_small)
        self.lbl_phase.pack(anchor='w', padx=15)
        self.lbl_ratio = tk.Label(p_qual, text="|X|/R   = ?",
                                  bg=C_PANEL, fg=C_GREEN, font=f_small)
        self.lbl_ratio.pack(anchor='w', padx=15, pady=(0,8))

        # ── Ligne 3 : Detail calcul GSW / TEOS-10 (pleine largeur) ────
        p_gsw = tk.Frame(self.root, bg=C_PANEL, bd=1, relief='solid')
        p_gsw.pack(fill='x', padx=10, pady=4)
        tk.Label(p_gsw, text="DETAIL CALCUL GSW / TEOS-10",
                 bg=C_PANEL, fg=C_ACCENT, font=("Consolas", 10, "bold")).pack(pady=(8,0))
        self.lbl_gsw1 = tk.Label(p_gsw, text="", bg=C_PANEL, fg=C_WHITE, font=f_footer)
        self.lbl_gsw1.pack(anchor='w', padx=10, pady=(2,0))
        self.lbl_gsw2 = tk.Label(p_gsw, text="", bg=C_PANEL, fg=C_GREEN, font=f_footer)
        self.lbl_gsw2.pack(anchor='w', padx=10, pady=(0,8))

        # ── Pied de page ──────────────────────────────────────────────
        pf = tk.Frame(self.root, bg=C_PANEL)
        pf.pack(fill='x', padx=10, pady=(4,8))
        tk.Label(pf,
                 text="Loi puissance : f_opt = 0.2147 x sigma^1.3582  (R2=0.985)  |  "
                      "Calibre 9 points  |  Manip 29/04/2026  |  Precision 1 Hz",
                 bg=C_PANEL, fg=C_GRAY, font=f_footer).pack()
        tk.Label(pf,
                 text="K_cell = 1.0 cm-1  |  Rcal = 100 ohm  |  R2 = 100 ohm  |  MCLK = 1 MHz  |  "
                      "Samya Lmaikini  |  IUEM Plouzane",
                 bg=C_PANEL, fg=C_GRAY, font=f_footer).pack(pady=(0,4))

    def _panel(self, parent, label, init_val, unit, color, font_val):
        f = tk.Frame(parent, bg=C_PANEL, bd=1, relief='solid')
        f.pack(side='left', fill='both', expand=True, padx=4)
        tk.Label(f, text=label, bg=C_PANEL, fg=C_GRAY,
                 font=("Consolas", 10)).pack(pady=(8,0))
        lbl = tk.Label(f, text=init_val, bg=C_PANEL, fg=color, font=font_val)
        lbl.pack()
        tk.Label(f, text=unit, bg=C_PANEL, fg=color,
                 font=("Consolas", 13)).pack(pady=(0,8))
        return lbl

    def update(self, row, SP, sigma_spec, cycle, timestamp):
        T        = row['temp_C']
        sigma_T  = row['sigma_T_mScm']
        sigma_0  = row['sigma_init_mScm']
        f_opt    = row['f_opt_hz'] / 1000.0
        f_init   = row['f_init_hz'] / 1000.0
        Rcell    = row['Rcell_ohm']
        phi      = row['phi_Z_deg']
        ratio    = row['ratio_XR']
        n_iter   = int(row['n_iter'])
        converge = int(row['converge'])

        sp_str  = f"{SP:.3f}"         if SP         is not None else "ERREUR"
        s25_str = f"{sigma_spec:.3f}" if sigma_spec is not None else "ERREUR"

        qual_col  = C_GREEN  if abs(phi) < 5.0 and ratio < 0.1 else C_ORANGE
        conv_col  = C_GREEN  if converge else C_RED
        conv_txt  = "converge = OUI ✓" if converge else "converge = NON"

        self.lbl_cycle.config(text=f"Cycle #{cycle:03d}")
        self.lbl_time.config(text=timestamp)
        self.lbl_temp.config(text=f"{T:.1f}")
        self.lbl_sigmaT.config(text=f"{sigma_T:.3f}")
        self.lbl_SP.config(text=sp_str)
        self.lbl_s25.config(text=s25_str)
        self.lbl_fopt.config(text=f"{f_opt:.3f}")
        self.lbl_algo.config(
            text=f"f_init={f_init:.0f} kHz  sigma_0={sigma_0:.3f} mS/cm\n"
                 f"iterations = {n_iter}")
        self.lbl_conv.config(text=conv_txt, fg=conv_col)
        self.lbl_rcell.config(text=f"Rcell   = {Rcell:.3f} ohm")
        self.lbl_phase.config(text=f"Phase Z = {phi:.3f} deg", fg=qual_col)
        self.lbl_ratio.config(text=f"|X|/R   = {ratio:.4f}", fg=qual_col)

        self.lbl_gsw1.config(
            text=f"  sigma(T) = {sigma_T:.4f} mS/cm   |   T = {T:.1f} degC   |   SP = {sp_str} PSU")
        self.lbl_gsw2.config(
            text=f"  s25C = gsw.C_from_SP( SP, 25.0, 0 )  =  {s25_str} mS/cm  <- conductivite specifique")

        self.root.update()

# ═══════════════════════════════════════════════════════════════════════════
# BOUCLE PRINCIPALE
# ═══════════════════════════════════════════════════════════════════════════
def main():
    print("=" * 65)
    print("  CN0349 -- TABLEAU DE BORD v6 -- GSW/TEOS-10")
    print(f"  IUEM Plouzane  lon={LON_IUEM}  lat={LAT_IUEM}")
    print("  Ctrl+C pour arreter")
    print("=" * 65)

    try:
        import gsw
        print(f"  [OK] gsw {gsw.__version__}")
    except ImportError:
        print("  [ERREUR] pip install gsw")
        return

    port = detect_port()
    print(f"  Connexion a {port} @ {BAUD_RATE} baud...")
    try:
        ser = serial.Serial(port, BAUD_RATE, timeout=2)
    except Exception as e:
        print(f"  [ERREUR] {e}")
        return

    time.sleep(2)
    ser.flushInput()

    root = tk.Tk()
    dash = Dashboard(root)
    root.update()
    print(f"  [OK] Fenetre creee -- attente des donnees...\n")

    cycle = 0
    try:
        while True:
            row = read_one_cycle(ser)
            if row is None:
                continue

            cycle += 1
            ts      = datetime.now().strftime("%H:%M:%S")
            T       = row['temp_C']
            sigma_T = row['sigma_T_mScm']
            gamme   = row['gamme']
            n_iter  = int(row['n_iter'])
            conv    = int(row['converge'])

            SP, sigma_spec = calculer_gsw(sigma_T, T)
            conv_str = "OUI" if conv else "NON"

            if SP is not None:
                print(f"[Cycle {cycle:3d}]  {ts}  "
                      f"T={T:.1f}C ({row['temp_source']})  "
                      f"gamme={gamme}  "
                      f"sigma={sigma_T:.3f} mS/cm  "
                      f"sigma25={sigma_spec:.3f} mS/cm  "
                      f"SP={SP:.3f} PSU  "
                      f"iter={n_iter}  conv={conv_str}")

            dash.update(row, SP, sigma_spec, cycle, ts)

    except KeyboardInterrupt:
        print("\n\nArret.")
    finally:
        ser.close()
        root.destroy()

if __name__ == '__main__':
    main()