"""
Sonde integree CN0349/AD5934/ESP32 - Sweep du 30/06/2026
==========================================================
Script AUTONOME (aucun fichier externe requis) :
 - le CSV brut du sweep du 30/06 est embarque directement (EMBEDDED_CSV)
 - la reference utilisee est le sweep MANUEL du 19/06/2026 (salinites
   mesurees, doc "sweep_Valeurs_finales_19062026"), pas une valeur
   theorique PSS-78

Deux graphes generes :
  1) Salinite (PSU) vs Temps -> TOUTES les valeurs brutes du sweep 30/06
  2) Erreur de salinite (S_mesure_30_06 - S_ref_19_06) vs Salinite de
     reference (19/06) -> une valeur par palier (moyenne), avec barres
     d'erreur verticales = ecart-type des mesures stables du palier

A la fin, une fenetre s'ouvre avec l'image (plt.show()), en plus du
fichier PNG sauvegarde.
"""

import io
import pandas as pd               # lecture/traitement tabulaire du CSV embarque
import numpy as np                # tri numerique (np.argsort) pour le graphe 2
import matplotlib.pyplot as plt   # traçage des 2 graphes

# ------------------------------------------------------------------
# 0) Donnees brutes du sweep 30/06/2026 (sonde integree) - embarquees
# ------------------------------------------------------------------
# Le CSV n'est pas lu depuis un fichier externe : il est colle directement
# ici (triple-guillemets) pour que le script tourne sans dependance a un
# chemin de fichier. C'est le log brut ecrit par la sonde (une ligne par
# mesure, separateur ";"), avant tout filtrage/traitement.
EMBEDDED_CSV = """datetime ; temp_C ; gamme ; f_opt_khz ; sigma_T_mScm ; SP_PSS78_PSU ; sigma25_PSS78_mScm
2026-06-30 10:45:16 ; 23.477 ; HIGH ;   45.994 ;       43.094 ;      28.683 ;            44.425
2026-06-30 10:45:41 ; 23.477 ; HIGH ;   45.993 ;       43.101 ;      28.689 ;            44.433
2026-06-30 11:02:37 ; 21.419 ; HIGH ;    3.000 ;        1.719 ;       0.939 ;             1.853
2026-06-30 11:02:58 ; 21.333 ; HIGH ;    3.271 ;        1.661 ;       0.907 ;             1.794
2026-06-30 11:03:25 ; 21.566 ; HIGH ;    3.000 ;        1.598 ;       0.867 ;             1.717
2026-06-30 11:03:46 ; 21.756 ; HIGH ;    3.000 ;        1.480 ;       0.796 ;             1.584
2026-06-30 11:04:07 ; 21.721 ; HIGH ;    3.000 ;        1.477 ;       0.795 ;             1.582
2026-06-30 11:04:27 ; 21.436 ; HIGH ;    3.000 ;        1.828 ;       1.001 ;             1.969
2026-06-30 11:04:48 ; 21.902 ; HIGH ;   14.913 ;        1.419 ;       0.759 ;             1.513
2026-06-30 11:05:57 ; 21.428 ; HIGH ;    3.000 ;        2.637 ;       1.472 ;             2.841
2026-06-30 11:06:18 ; 21.091 ; HIGH ;    3.000 ;        2.588 ;       1.454 ;             2.809
2026-06-30 11:06:39 ; 21.152 ; HIGH ;    3.000 ;        2.504 ;       1.403 ;             2.715
2026-06-30 11:07:00 ; 21.298 ; HIGH ;    3.000 ;        2.231 ;       1.238 ;             2.411
2026-06-30 11:07:21 ; 21.445 ; HIGH ;    3.000 ;        2.235 ;       1.236 ;             2.407
2026-06-30 11:07:42 ; 21.635 ; HIGH ;    3.000 ;        2.140 ;       1.176 ;             2.296
2026-06-30 11:08:03 ; 21.436 ; HIGH ;    3.000 ;        2.183 ;       1.206 ;             2.352
2026-06-30 11:08:24 ; 21.445 ; HIGH ;    3.000 ;        2.198 ;       1.214 ;             2.368
2026-06-30 11:08:45 ; 21.393 ; HIGH ;    3.000 ;        2.205 ;       1.220 ;             2.378
2026-06-30 11:09:06 ; 21.212 ; HIGH ;    3.000 ;        1.818 ;       1.000 ;             1.969
2026-06-30 11:09:26 ; 22.170 ; LOW  ;    3.000 ;        0.007 ;       0.012 ;             0.006
2026-06-30 11:09:49 ; 24.559 ; HIGH ;    3.224 ;        8.487 ;       4.763 ;             8.563
2026-06-30 11:10:19 ; 24.781 ; HIGH ;    3.229 ;        8.494 ;       4.745 ;             8.532
2026-06-30 11:10:49 ; 24.756 ; HIGH ;    3.235 ;        8.514 ;       4.759 ;             8.556
2026-06-30 11:11:19 ; 24.756 ; HIGH ;    3.236 ;        8.506 ;       4.754 ;             8.548
2026-06-30 11:11:49 ; 24.773 ; HIGH ;    3.238 ;        8.513 ;       4.757 ;             8.552
2026-06-30 11:12:19 ; 24.764 ; HIGH ;    3.247 ;        8.510 ;       4.756 ;             8.550
2026-06-30 11:12:48 ; 24.747 ; HIGH ;    3.243 ;        8.513 ;       4.759 ;             8.556
2026-06-30 11:13:18 ; 24.747 ; HIGH ;    3.249 ;        8.516 ;       4.761 ;             8.560
2026-06-30 11:13:48 ; 24.739 ; HIGH ;    3.249 ;        8.515 ;       4.762 ;             8.560
2026-06-30 11:14:18 ; 24.747 ; HIGH ;    3.241 ;        8.519 ;       4.763 ;             8.563
2026-06-30 11:14:48 ; 24.747 ; HIGH ;    3.237 ;        8.514 ;       4.760 ;             8.557
2026-06-30 11:15:18 ; 24.730 ; HIGH ;    3.239 ;        8.518 ;       4.764 ;             8.565
2026-06-30 11:15:48 ; 23.684 ; HIGH ;    3.000 ;        3.879 ;       2.106 ;             3.985
2026-06-30 11:16:09 ; 22.428 ; HIGH ;    3.000 ;        1.600 ;       0.851 ;             1.687
2026-06-30 11:16:29 ; 22.462 ; LOW  ;    3.000 ;        0.032 ;       0.023 ;             0.032
2026-06-30 11:16:53 ; 21.790 ; LOW  ;    3.000 ;        0.035 ;       0.024 ;             0.035
2026-06-30 11:17:16 ; 21.445 ; LOW  ;    3.000 ;        0.027 ;       0.020 ;             0.026
2026-06-30 11:17:39 ; 21.497 ; LOW  ;    3.000 ;        0.025 ;       0.019 ;             0.024
2026-06-30 11:18:02 ; 24.781 ; HIGH ;   10.131 ;       17.099 ;      10.097 ;            17.174
2026-06-30 11:18:31 ; 24.790 ; HIGH ;   10.145 ;       17.100 ;      10.096 ;            17.172
2026-06-30 11:19:01 ; 24.781 ; HIGH ;   10.168 ;       17.106 ;      10.101 ;            17.181
2026-06-30 11:19:28 ; 24.756 ; HIGH ;   10.167 ;       17.118 ;      10.115 ;            17.202
2026-06-30 11:19:55 ; 24.764 ; HIGH ;   10.184 ;       17.130 ;      10.121 ;            17.212
2026-06-30 11:20:21 ; 24.773 ; HIGH ;   10.193 ;       17.131 ;      10.120 ;            17.210
2026-06-30 11:20:48 ; 24.764 ; HIGH ;   10.182 ;       17.143 ;      10.129 ;            17.225
2026-06-30 11:21:18 ; 24.764 ; HIGH ;   10.178 ;       17.137 ;      10.125 ;            17.219
2026-06-30 11:21:48 ; 24.756 ; HIGH ;   10.196 ;       17.153 ;      10.138 ;            17.238
2026-06-30 11:22:14 ; 24.764 ; HIGH ;   10.193 ;       17.147 ;      10.131 ;            17.228
2026-06-30 11:22:41 ; 24.756 ; HIGH ;   10.163 ;       17.124 ;      10.119 ;            17.208
2026-06-30 11:23:11 ; 24.764 ; HIGH ;   10.174 ;       17.122 ;      10.116 ;            17.204
2026-06-30 11:23:41 ; 24.764 ; HIGH ;   10.174 ;       17.130 ;      10.121 ;            17.211
2026-06-30 11:24:08 ; 24.764 ; HIGH ;    3.000 ;        0.341 ;       0.165 ;             0.342
2026-06-30 11:24:37 ; 22.841 ; LOW  ;    3.000 ;        0.038 ;       0.025 ;             0.038
2026-06-30 11:25:03 ; 22.841 ; LOW  ;    3.000 ;        0.045 ;       0.028 ;             0.046
2026-06-30 11:25:26 ; 22.333 ; LOW  ;    3.000 ;        0.046 ;       0.029 ;             0.047
2026-06-30 11:25:50 ; 24.036 ; HIGH ;   18.194 ;       24.446 ;      15.155 ;            24.925
2026-06-30 11:26:11 ; 24.858 ; HIGH ;   18.227 ;       24.457 ;      14.890 ;            24.526
2026-06-30 11:26:42 ; 24.850 ; HIGH ;   18.248 ;       24.470 ;      14.901 ;            24.543
2026-06-30 11:27:04 ; 24.850 ; HIGH ;   18.246 ;       24.509 ;      14.927 ;            24.583
2026-06-30 11:27:25 ; 24.850 ; HIGH ;   18.264 ;       24.514 ;      14.930 ;            24.587
2026-06-30 11:27:49 ; 24.850 ; HIGH ;   18.264 ;       24.521 ;      14.935 ;            24.595
2026-06-30 11:28:13 ; 24.858 ; HIGH ;   18.304 ;       24.530 ;      14.938 ;            24.599
2026-06-30 11:28:35 ; 24.858 ; HIGH ;   18.335 ;       24.546 ;      14.949 ;            24.616
2026-06-30 11:28:56 ; 24.867 ; HIGH ;   18.308 ;       24.541 ;      14.943 ;            24.607
2026-06-30 11:29:23 ; 24.867 ; HIGH ;   18.297 ;       24.543 ;      14.944 ;            24.608
2026-06-30 11:29:46 ; 24.876 ; HIGH ;   18.315 ;       24.568 ;      14.958 ;            24.629
2026-06-30 11:30:08 ; 24.876 ; HIGH ;   18.318 ;       24.545 ;      14.943 ;            24.606
2026-06-30 11:30:32 ; 24.884 ; HIGH ;   18.324 ;       24.553 ;      14.946 ;            24.610
2026-06-30 11:30:54 ; 24.884 ; HIGH ;   18.331 ;       24.559 ;      14.949 ;            24.616
2026-06-30 11:31:18 ; 24.893 ; HIGH ;    3.000 ;        0.299 ;       0.145 ;             0.300
2026-06-30 11:31:54 ; 22.790 ; LOW  ;    3.000 ;        0.020 ;       0.017 ;             0.019
2026-06-30 11:32:17 ; 22.247 ; LOW  ;    3.000 ;        0.012 ;       0.014 ;             0.011
2026-06-30 11:32:40 ; 21.954 ; LOW  ;    3.000 ;        0.011 ;       0.014 ;             0.009
2026-06-30 11:33:03 ; 22.342 ; HIGH ;   28.707 ;       32.293 ;      21.375 ;            34.080
2026-06-30 11:33:37 ; 24.841 ; HIGH ;   28.734 ;       32.282 ;      20.204 ;            32.384
2026-06-30 11:34:01 ; 24.841 ; HIGH ;   28.739 ;       32.326 ;      20.235 ;            32.429
2026-06-30 11:34:26 ; 24.841 ; HIGH ;   28.803 ;       32.350 ;      20.251 ;            32.453
2026-06-30 11:34:54 ; 24.833 ; HIGH ;   28.827 ;       32.379 ;      20.275 ;            32.487
2026-06-30 11:35:20 ; 24.824 ; HIGH ;   28.817 ;       32.373 ;      20.274 ;            32.486
2026-06-30 11:35:47 ; 24.824 ; HIGH ;   28.825 ;       32.411 ;      20.301 ;            32.525
2026-06-30 11:36:13 ; 24.824 ; HIGH ;   28.812 ;       32.397 ;      20.291 ;            32.511
2026-06-30 11:36:35 ; 24.833 ; HIGH ;   28.860 ;       32.398 ;      20.288 ;            32.506
2026-06-30 11:37:02 ; 24.833 ; HIGH ;   28.830 ;       32.420 ;      20.303 ;            32.528
2026-06-30 11:37:30 ; 24.824 ; HIGH ;   28.818 ;       32.372 ;      20.274 ;            32.485
2026-06-30 11:38:02 ; 24.824 ; HIGH ;    3.000 ;        0.044 ;       0.027 ;             0.044
2026-06-30 11:38:26 ; 23.658 ; LOW  ;    3.000 ;        0.002 ;       0.011 ;             0.004
2026-06-30 11:38:49 ; 23.202 ; LOW  ;    3.000 ;        0.002 ;       0.011 ;             0.004
2026-06-30 11:39:12 ; 22.592 ; LOW  ;    3.000 ;        0.002 ;       0.011 ;             0.003
2026-06-30 11:39:35 ; 22.660 ; LOW  ;   20.000 ;        0.002 ;       0.011 ;             0.003
2026-06-30 11:39:55 ; 24.858 ; HIGH ;   40.169 ;       39.615 ;      25.328 ;            39.726
2026-06-30 11:40:22 ; 24.876 ; HIGH ;   40.165 ;       39.657 ;      25.349 ;            39.755
2026-06-30 11:40:49 ; 24.867 ; HIGH ;   40.173 ;       39.650 ;      25.348 ;            39.754
2026-06-30 11:41:16 ; 24.867 ; HIGH ;   40.167 ;       39.638 ;      25.340 ;            39.743
2026-06-30 11:41:40 ; 24.867 ; HIGH ;   40.177 ;       39.696 ;      25.381 ;            39.801
2026-06-30 11:42:04 ; 24.867 ; HIGH ;   40.149 ;       39.677 ;      25.368 ;            39.782
2026-06-30 11:42:25 ; 24.867 ; HIGH ;   40.168 ;       39.709 ;      25.390 ;            39.814
2026-06-30 11:42:49 ; 24.867 ; HIGH ;   40.175 ;       39.669 ;      25.362 ;            39.773
2026-06-30 11:43:16 ; 24.876 ; HIGH ;   40.198 ;       39.698 ;      25.378 ;            39.796
2026-06-30 11:43:43 ; 24.876 ; HIGH ;   40.177 ;       39.697 ;      25.377 ;            39.795
2026-06-30 11:44:05 ; 24.876 ; HIGH ;   40.193 ;       39.656 ;      25.348 ;            39.754
2026-06-30 11:44:29 ; 24.876 ; HIGH ;   40.197 ;       39.641 ;      25.337 ;            39.738
2026-06-30 11:44:52 ; 24.876 ; HIGH ;   40.186 ;       39.651 ;      25.345 ;            39.749
2026-06-30 11:45:19 ; 24.284 ; LOW  ;    3.000 ;        0.873 ;       0.435 ;             0.885
2026-06-30 11:45:42 ; 23.615 ; LOW  ;    3.000 ;        0.172 ;       0.086 ;             0.176
2026-06-30 11:46:05 ; 23.159 ; LOW  ;    3.000 ;        0.003 ;       0.012 ;             0.004
2026-06-30 11:46:29 ; 24.233 ; HIGH ;   51.240 ;       46.083 ;      30.394 ;            46.790
2026-06-30 11:46:57 ; 24.858 ; HIGH ;   51.313 ;       46.083 ;      29.974 ;            46.212
2026-06-30 11:47:23 ; 24.867 ; HIGH ;   51.356 ;       46.165 ;      30.028 ;            46.287
2026-06-30 11:47:44 ; 24.867 ; HIGH ;   51.520 ;       46.143 ;      30.012 ;            46.264
2026-06-30 11:48:16 ; 24.858 ; HIGH ;   51.501 ;       46.172 ;      30.039 ;            46.301
2026-06-30 11:48:43 ; 24.858 ; HIGH ;   51.568 ;       46.169 ;      30.037 ;            46.299
2026-06-30 11:49:10 ; 24.858 ; HIGH ;   51.621 ;       46.226 ;      30.078 ;            46.355
2026-06-30 11:49:57 ; 24.850 ; HIGH ;   51.579 ;       46.200 ;      30.065 ;            46.338
2026-06-30 11:50:20 ; 24.850 ; HIGH ;   51.621 ;       46.223 ;      30.082 ;            46.361
2026-06-30 11:50:45 ; 24.841 ; HIGH ;   51.543 ;       46.179 ;      30.055 ;            46.324
2026-06-30 11:51:08 ; 24.807 ; HIGH ;   51.576 ;       46.210 ;      30.100 ;            46.386
2026-06-30 11:51:34 ; 24.807 ; HIGH ;   51.623 ;       46.234 ;      30.118 ;            46.410
2026-06-30 11:52:02 ; 24.824 ; HIGH ;    8.905 ;       18.775 ;      11.167 ;            18.841
2026-06-30 11:52:32 ; 23.967 ; LOW  ;    3.000 ;        0.002 ;       0.012 ;             0.004
2026-06-30 11:52:55 ; 22.841 ; LOW  ;    3.000 ;        0.002 ;       0.011 ;             0.003
2026-06-30 11:53:18 ; 22.686 ; LOW  ;    3.000 ;        0.003 ;       0.011 ;             0.003
2026-06-30 11:53:41 ; 23.495 ; HIGH ;   63.409 ;       52.396 ;      35.687 ;            53.989
2026-06-30 11:54:15 ; 24.293 ; HIGH ;   63.535 ;       52.406 ;      35.059 ;            53.145
2026-06-30 11:54:51 ; 24.301 ; HIGH ;   63.562 ;       52.485 ;      35.112 ;            53.215
2026-06-30 11:55:22 ; 24.327 ; HIGH ;   63.483 ;       52.483 ;      35.090 ;            53.186
2026-06-30 11:55:59 ; 24.336 ; HIGH ;   63.500 ;       52.424 ;      35.039 ;            53.117
2026-06-30 11:56:22 ; 24.344 ; HIGH ;   63.499 ;       52.475 ;      35.070 ;            53.160
2026-06-30 11:57:03 ; 24.361 ; HIGH ;   63.527 ;       52.500 ;      35.076 ;            53.167
2026-06-30 11:57:30 ; 24.361 ; HIGH ;   63.609 ;       52.530 ;      35.099 ;            53.198
2026-06-30 11:58:01 ; 24.370 ; HIGH ;   63.575 ;       52.487 ;      35.059 ;            53.144
2026-06-30 11:58:28 ; 24.370 ; HIGH ;   63.553 ;       52.525 ;      35.088 ;            53.183
2026-06-30 11:58:56 ; 23.864 ; LOW  ;    3.000 ;        0.002 ;       0.012 ;             0.004
2026-06-30 11:59:19 ; 23.958 ; LOW  ;    3.000 ;        0.003 ;       0.012 ;             0.005
2026-06-30 11:59:42 ; 24.087 ; LOW  ;   20.000 ;        0.003 ;       0.012 ;             0.004
2026-06-30 12:00:02 ; 23.641 ; LOW  ;   20.000 ;        0.004 ;       0.012 ;             0.005
2026-06-30 12:00:22 ; 24.867 ; HIGH ;   39.768 ;       39.451 ;      25.208 ;            39.556
2026-06-30 12:00:47 ; 24.876 ; HIGH ;   39.899 ;       39.508 ;      25.243 ;            39.606
2026-06-30 12:01:13 ; 24.876 ; HIGH ;   39.945 ;       39.557 ;      25.278 ;            39.655
2026-06-30 12:01:35 ; 24.876 ; HIGH ;   39.953 ;       39.530 ;      25.258 ;            39.627
2026-06-30 12:02:03 ; 24.876 ; HIGH ;   40.114 ;       39.589 ;      25.300 ;            39.686
2026-06-30 12:02:29 ; 24.867 ; HIGH ;   40.059 ;       39.603 ;      25.315 ;            39.708
2026-06-30 12:02:54 ; 24.858 ; HIGH ;   40.114 ;       39.611 ;      25.326 ;            39.723
2026-06-30 12:03:18 ; 24.858 ; HIGH ;   40.139 ;       39.602 ;      25.319 ;            39.714
2026-06-30 12:03:43 ; 24.850 ; HIGH ;   40.086 ;       39.617 ;      25.335 ;            39.736
2026-06-30 12:04:12 ; 24.841 ; HIGH ;   40.123 ;       39.637 ;      25.353 ;            39.762
2026-06-30 12:04:37 ; 24.841 ; HIGH ;   40.131 ;       39.622 ;      25.343 ;            39.747
2026-06-30 12:05:01 ; 24.833 ; HIGH ;   40.117 ;       39.603 ;      25.334 ;            39.735
2026-06-30 12:05:24 ; 24.704 ; HIGH ;    3.000 ;        5.114 ;       2.763 ;             5.145
2026-06-30 12:05:52 ; 23.142 ; LOW  ;    3.000 ;        0.000 ;       0.000 ;             0.000
"""

# Parsing du CSV embarque : separateur ";", conversion des types.
df = pd.read_csv(io.StringIO(EMBEDDED_CSV), sep=";")
df.columns = [c.strip() for c in df.columns]          # enleve les espaces autour des noms de colonnes
for c in df.columns:
    if df[c].dtype == object:
        df[c] = df[c].astype(str).str.strip()          # enleve les espaces dans les valeurs texte (ex: "HIGH ")
df["datetime"] = pd.to_datetime(df["datetime"])         # parse la colonne date/heure en vrai objet datetime
for c in ["temp_C", "f_opt_khz", "sigma_T_mScm", "SP_PSS78_PSU", "sigma25_PSS78_mScm"]:
    df[c] = pd.to_numeric(df[c], errors="coerce")        # force le type numerique (NaN si valeur invalide)
df = df.sort_values("datetime").reset_index(drop=True)   # garantit l'ordre chronologique

# ------------------------------------------------------------------
# 1) Detection des paliers (moyenne + ecart-type par palier, sonde 30/06)
# ------------------------------------------------------------------
# Le sweep du 30/06 alterne des paliers stables (sonde plongee dans un
# echantillon de salinite connue, sigma haut) et des passages de
# transition/rincage (sigma bas ou proche de 0, entre deux echantillons).
# Le but ici est d'isoler automatiquement chaque palier stable, pour
# calculer une salinite moyenne representative par echantillon, plutot
# que de garder les points de transition qui fausseraient la moyenne.

SIGMA_MIN = 3.0        # seuil de conductivite (mS/cm) au-dessus duquel on considere qu'on est dans un palier (pas en rincage)
MIN_RAW_PTS = 5        # nombre minimum de points bruts dans un bloc pour le considerer comme un palier candidat
STAB_TOL = 0.03        # tolerance relative (3%) sur sigma/temp pour qu'un point soit juge "stable" au sein du palier
MIN_STABLE_PTS = 5     # nombre minimum de points stables requis pour valider le palier
targets = [5, 10, 15, 20, 25, 30, 34.7]   # salinites cibles attendues (memes echantillons que le 19/06)

# mask = True quand on est au-dessus du seuil sigma (donc "dans" un palier).
# group_id incremente a chaque changement d'etat True/False -> chaque
# groupe consecutif de "True" forme un bloc = un palier candidat.
mask = df["sigma_T_mScm"] > SIGMA_MIN
group_id = (mask != mask.shift(fill_value=False)).cumsum()
blocks = [g.index.tolist() for _, g in df[mask].groupby(group_id[mask])]

plateaus = []
for b in blocks:
    if len(b) < MIN_RAW_PTS:
        continue  # bloc trop court -> probablement un artefact, pas un vrai palier

    sub = df.loc[b]
    # On regarde la fin du bloc (les derniers points), car c'est la que
    # la mesure a le plus de chances d'etre stabilisee (apres l'algorithme
    # adaptatif de convergence de frequence).
    tail = sub.tail(min(8, len(sub)))
    med_sigma = tail["sigma_T_mScm"].median()
    med_t = tail["temp_C"].median()

    # Parmi TOUT le bloc, ne garde que les points proches (± STAB_TOL) de
    # la mediane de sigma ET de la temperature en fin de bloc : ca exclut
    # la phase de montee/stabilisation initiale du palier.
    stable = sub[
        ((sub["sigma_T_mScm"] - med_sigma).abs() / med_sigma < STAB_TOL)
        & ((sub["temp_C"] - med_t).abs() / med_t < 0.02)
    ]
    if len(stable) < MIN_STABLE_PTS:
        continue  # pas assez de points stables -> palier rejete

    # Salinite moyenne et ecart-type sur les points stables retenus.
    sp_mean = stable["SP_PSS78_PSU"].mean()
    sp_std = stable["SP_PSS78_PSU"].std()
    # Associe ce palier a la salinite cible la plus proche (5, 10, 15...).
    target = min(targets, key=lambda x: abs(x - sp_mean))

    plateaus.append({
        "t_debut": sub["datetime"].iloc[0],
        "target_S": target,
        "S_mesure_30_06": sp_mean,
        "S_std_30_06": sp_std,
    })

# Un palier = une ligne, trie chronologiquement.
result = pd.DataFrame(plateaus).sort_values("t_debut").reset_index(drop=True)

# ------------------------------------------------------------------
# 2) Reference = sweep MANUEL du 19/06/2026 (salinites mesurees)
#    (source : sweep_Valeurs_finales_19062026.docx)
# ------------------------------------------------------------------
# Valeurs de salinite mesurees manuellement le 19/06 pour ces memes
# echantillons de reference (5, 10, 15... PSU nominaux) -- c'est la
# "verite terrain" a laquelle on compare les mesures de la sonde du 30/06.
S_ref_19_06 = {
    5:    5.005,
    10:   10.006,
    15:   15.003,
    20:   20.025,
    25:   25.026,
    30:   30.022,
    34.7: 34.779,
}
result["S_ref_19_06"] = result["target_S"].map(S_ref_19_06)
# Erreur = ce que mesure la sonde le 30/06 moins la reference du 19/06.
# Positif -> la sonde surestime la salinite ; negatif -> elle la sous-estime.
result["erreur_salinite"] = result["S_mesure_30_06"] - result["S_ref_19_06"]

print(result[["t_debut", "target_S", "S_mesure_30_06", "S_std_30_06",
              "S_ref_19_06", "erreur_salinite"]].to_string(index=False))
result.to_csv("erreur_salinite_30_06_vs_19_06.csv", index=False)

# ------------------------------------------------------------------
# 3) Graphes
# ------------------------------------------------------------------
plt.rcParams.update({
    "font.size": 10,
    "axes.grid": True,
    "grid.linestyle": "--",
    "grid.alpha": 0.6,
})

fig, axes = plt.subplots(2, 1, figsize=(9, 9))

# --- Graphe 1 : TOUTES les valeurs de salinite vs temps (sweep 30/06) ---
# Vue brute complete (paliers + transitions), pour visualiser d'un coup
# d'oeil le deroule chronologique de la manip du 30/06 (montees/descentes
# entre chaque echantillon, plateaux stables).
ax = axes[0]
ax.plot(df["datetime"], df["SP_PSS78_PSU"], "-o", color="steelblue",
        markersize=3, linewidth=1)
ax.set_title("Salinite (PSU) vs Temps - toutes les valeurs, sweep du 30/06/2026",
             fontsize=12, fontweight="bold")
ax.set_xlabel("Temps")
ax.set_ylabel("Salinite SP (PSU)")
fig.autofmt_xdate()  # incline les dates de l'axe X pour qu'elles restent lisibles

# --- Graphe 2 : Erreur de salinite vs Salinite de reference (19/06) ---
# Une valeur par palier (moyenne des points stables), triee par salinite
# de reference croissante. Barres d'erreur verticales = ecart-type des
# mesures stables du palier (30/06), pour visualiser la dispersion autour
# de la moyenne, comme sur le croquis manuscrit (points + barres a
# chapeaux relies par une ligne).
ax = axes[1]
x = result["S_ref_19_06"]
y = result["erreur_salinite"]
yerr = result["S_std_30_06"]
order = np.argsort(x.values)   # ordre croissant en salinite pour que la ligne reliant les points soit propre
ax.plot(x.values[order], y.values[order], "-", color="tab:red", linewidth=1.2, zorder=1)
ax.errorbar(x, y, yerr=yerr, fmt="s", color="red", ecolor="red",
            elinewidth=1.3, capsize=5, capthick=1.3, markersize=7, zorder=2)
ax.axhline(0, color="grey", linewidth=0.8)   # ligne horizontale a erreur=0 = reference parfaite
ax.set_title("Erreur de salinite (sonde 30/06 - reference 19/06) vs Salinite",
             fontsize=12, fontweight="bold")
ax.set_xlabel("Salinite de reference (PSU) - sweep manuel du 19/06")
ax.set_ylabel("Erreur de salinite (PSU)")

fig.tight_layout()
fig.savefig("salinite_vs_temps_et_erreur.png", dpi=200)
plt.show()

print("\nFichier genere : salinite_vs_temps_et_erreur.png")