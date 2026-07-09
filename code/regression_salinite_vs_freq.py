# ===========================================================================
# regression_salinite_vs_freq.py
# Régression loi puissance : salinite = a x frequence^b
# Données : sonde integree (sweep_freq_3) 
# Samya Lmaikini
# ===========================================================================

import sys
import io
# Force la sortie standard en UTF-8 : evite les erreurs d'encodage sur les
# caracteres accentues (é, è, ²...) quand la console Windows est en CP1252.
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')

import numpy as np                      # tableaux numeriques + fonctions vectorisees (np.power, np.mean...)
import matplotlib.pyplot as plt          # traçage des graphes (courbe + residus)
from scipy.optimize import curve_fit     # ajustement non lineaire (moindres carres)

# ── 1. Données expérimentales ─────────────────────────────────────────────
# f_opt    : frequence optimale relevee (kHz) -- valeur lue dans le tableau
#            de sweep pour chaque echantillon (celle ou la mesure est la
#            plus stable, voir sweep_depuis_v9)
# salinite : salinite reelle/connue de l'echantillon correspondant (PSU)
# Les deux tableaux doivent avoir le meme nombre de points et etre dans
# le meme ordre (f_opt[i] correspond a salinite[i]).

f_opt    = np.array([4.90,   9.40,  19.20,  25.20,  35.20,  51.10,  60.80])
salinite = np.array([5.005, 10.006, 15.003, 20.025, 25.026, 30.022, 34.779])

# ── 2. Modèle loi puissance ───────────────────────────────────────────────
# Hypothese physique : la salinite suit une loi puissance de f_opt, du type
# salinite = a * f_opt^b. C'est ce modele qu'on va ajuster aux points
# experimentaux ci-dessus (a et b sont les inconnues a determiner).
def loi_puissance(x, a, b):
    return a * np.power(x, b)

# ── 3. Régression via scipy.optimize.curve_fit ───────────────────────────
# curve_fit ajuste a et b en minimisant la somme des carres des ecarts :
#   Σ (salinite_i - a*f_opt_i^b)²
# p0 = [1.0, 0.7] : estimation de depart pour l'algorithme d'optimisation
# (valeurs raisonnables pour demarrer, pas les valeurs finales).
# popt = [a, b] optimaux ; pcov = matrice de covariance (incertitude sur
# a et b, non utilisee ici mais renvoyee par curve_fit).
popt, pcov = curve_fit(loi_puissance, f_opt, salinite, p0=[1.0, 0.7])
a, b = popt

# ── Affichage des coefficients trouves ────────────────────────────────────
print("=" * 55)
print("  REGRESSION LOI PUISSANCE : salinite = a x f_opt^b")
print("=" * 55)
print(f"  a = {a:.6f}   (coefficient préfacteur)")
print(f"  b = {b:.6f}   (exposant)")
print(f"  → salinite (PSU) = {a:.4f} x f_opt (kHz)^{b:.4f}")
print()

# ── 4. Calcul R² ──────────────────────────────────────────────────────────
# sal_pred : salinite predite par le modele ajuste, pour chaque f_opt mesure
# SS_res   : somme des carres des residus (erreur du modele)
# SS_tot   : somme des carres des ecarts a la moyenne (variance totale)
# R2       : proportion de la variance expliquee par le modele (1 = parfait)
sal_pred = loi_puissance(f_opt, a, b)
SS_res   = np.sum((salinite - sal_pred) ** 2)
SS_tot   = np.sum((salinite - np.mean(salinite)) ** 2)
R2       = 1 - SS_res / SS_tot

print(f"  R² = {R2:.4f}  ({R2*100:.1f}% de la variance expliquée)")
print()

# ── 5. Tableau point par point ────────────────────────────────────────────
# Compare, pour chaque point experimental, la salinite reelle et celle
# predite par le modele, avec l'ecart (residu) correspondant. Permet de
# reperer visuellement si un point s'ecarte anormalement du modele.
print(f"  {'f_opt (kHz)':>12} | {'salinite reelle (PSU)':>22} | {'salinite predite (PSU)':>23} | {'écart (PSU)':>12}")
print("  " + "-" * 75)
for f, sr, sp in zip(f_opt, salinite, sal_pred):
    print(f"  {f:>12.2f} | {sr:>22.3f} | {sp:>23.3f} | {sr-sp:>+12.3f}")
print()

# ── 6. Constantes ──────────────────────────────────────────────────────────
# Coefficients a reporter directement dans le firmware (format #define C++)
# pour calculer la salinite a partir de f_opt sur la sonde integree.
print("  → Coefficients de la regression :")
print(f"  #define A_SAL  {a:.4f}f")
print(f"  #define B_SAL  {b:.4f}f")
print("=" * 55)

# ── 7. Graphe ─────────────────────────────────────────────────────────────
# f_th/sal_th : courbe theorique continue du modele ajuste (300 points
# entre 1 et 70 kHz), utilisee pour tracer la courbe lisse en fond du
# graphe de gauche (par opposition aux points experimentaux, discrets).
f_th   = np.linspace(1, 70, 300)
sal_th = loi_puissance(f_th, a, b)

# Figure a 2 sous-graphes cote a cote : courbe de calibration (gauche) et
# residus (droite). Titre general avec l'equation ajustee et le R².
fig, axes = plt.subplots(1, 2, figsize=(13, 5))
fig.suptitle(
    f"Régression loi puissance : salinite = {a:.4f} x f_opt^{b:.4f}   (R²={R2:.3f})\n"
    f"Sonde integree -- Samya Lmaikini",
    fontsize=12, fontweight='bold'
)

# ── Graphe gauche : courbe + points ──────────────────────────────────────
# - courbe bleue continue : modele ajuste (loi puissance)
# - points noirs : mesures reelles (verite terrain)
# - croix rouges : valeurs predites par le modele pour ces memes f_opt
# - segments rouges verticaux : relient chaque point reel a sa prediction,
#   pour visualiser l'ecart (residu) directement sur la courbe.
ax1 = axes[0]
ax1.plot(f_th, sal_th, '-', color='#0052A3', lw=2.5,
         label=f'Loi puissance\nS = {a:.4f}·f^{b:.4f}')
ax1.scatter(f_opt, salinite, color='black', s=70, zorder=5,
            label='Points expérimentaux', marker='o')
ax1.scatter(f_opt, sal_pred, color='#C0392B', s=40, zorder=4,
            label='Valeurs predites', marker='x')

for f, sr, sp in zip(f_opt, salinite, sal_pred):
    ax1.plot([f, f], [sr, sp], color='#C0392B', lw=0.8, alpha=0.5)

ax1.set_xlabel('f_opt (kHz)', fontsize=11)
ax1.set_ylabel('salinite (PSU)', fontsize=11)
ax1.set_title('Courbe de calibration', fontsize=11)
ax1.legend(fontsize=8.5)
ax1.grid(True, alpha=0.3)

# ── Graphe droit : residus ────────────────────────────────────────────────
# Barres = ecart (residu) reel - predit pour chaque point, dans l'ordre des
# echantillons (pas en fonction de f_opt directement, un point par barre).
# Rouge = residu positif (modele sous-estime), bleu = residu negatif
# (modele surestime). Lignes pointillees a +/-1 PSU = seuil de tolerance
# indicatif. RMSE affichee dans le titre = erreur quadratique moyenne
# globale du modele, en PSU.
ax2 = axes[1]
residus = salinite - sal_pred
colors  = ['#C0392B' if r > 0 else '#0052A3' for r in residus]
bars    = ax2.bar(range(len(f_opt)), residus, color=colors, alpha=0.8, width=0.6)
ax2.axhline(0, color='black', lw=1.2)
ax2.axhline( 1, color='gray', lw=0.8, ls='--', alpha=0.5, label='+/-1 PSU')
ax2.axhline(-1, color='gray', lw=0.8, ls='--', alpha=0.5)

ax2.set_xticks(range(len(f_opt)))
ax2.set_xticklabels([f'{f:.1f}' for f in f_opt], fontsize=9)
ax2.set_xlabel('f_opt (kHz)', fontsize=11)
ax2.set_ylabel('Residus salinite reelle - predite (PSU)', fontsize=11)
ax2.set_title(f'Residus de la régression\nRMSE = {np.sqrt(np.mean(residus**2)):.3f} PSU', fontsize=11)
ax2.legend(fontsize=9)
ax2.grid(True, alpha=0.3, axis='y')

# Etiquette la valeur exacte du residu au-dessus/en-dessous de chaque barre
for i, r in enumerate(residus):
    ax2.text(i, r + (0.08 if r >= 0 else -0.18),
             f'{r:+.2f}', ha='center', fontsize=8.5, fontweight='bold')

# ── 8. Sauvegarde et affichage ────────────────────────────────────────────
plt.tight_layout()
plt.savefig('regression_salinite_vs_freq.png', dpi=150, bbox_inches='tight')
print("\n[SAVE] Graphe sauvegarde : regression_salinite_vs_freq.png")
plt.show()