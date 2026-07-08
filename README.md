# CN0349 / AD5934 — Mesure de conductivité sur ESP32

**IUEM Plouzané — Samya Lmaikini**

Ce dépôt contient les firmwares ESP32 pour le module Analog Devices **CN0349** (puce d'analyse d'impédance **AD5934** + multiplexeur **ADG715**), utilisé avec une cellule de conductivité 2 électrodes pour mesurer la conductivité de l'eau (typiquement eau de mer), la profondeur (capteur Bar30) et en déduire la salinité (PSS‑78).

| Fichier | Rôle |
|---|---|
| `AD5934_CN0349.h` | **Header partagé** : pilote bas niveau AD5934/ADG715 — I2C, calibration, mesure. Utilisé par le code adaptatif. |
| `PSS78.h` | **Header partagé** : calcul de la salinité pratique PSS‑78 (avec correction de pression). Utilisé par le code adaptatif. |
| `conductivite_adaptative_v9_pression.ino` | Algorithme **adaptatif** : cherche automatiquement la fréquence de mesure optimale, itère jusqu'à convergence, mesure aussi la pression/profondeur, enregistre 1 mesure/cycle sur carte SD, journalise les erreurs. Code de **production** pour un déploiement autonome. Utilise les deux headers ci‑dessus. |
| `sweep_depuis_v9.ino` | **Balayage fréquentiel manuel** sur une plage fixe : mesure à chaque fréquence sans sélection automatique, affiche un tableau complet sur le port série. Outil de **diagnostic / calibration**. **Fichier autonome, non modifié** : il contient encore sa propre copie du code bas niveau et de PSS‑78 (sans pression, sans log) et n'utilise pas les deux headers. |

Base logicielle de référence pour la puce AD5934 / ADG715 : [joshagirgis/CN0349-Arduino-Based-Library — CN0349Test.ino](https://github.com/joshagirgis/CN0349-Arduino-Based-Library/blob/master/CN0349Test/CN0349Test.ino) (exemple d'origine pour la calibration et la mesure), voir aussi le [README du dépôt](https://github.com/joshagirgis/CN0349-Arduino-Based-Library) pour le pinout, le calcul des facteurs de gain et les précautions RF.

---

## 1. Principe de mesure

1. L'AD5934 injecte un signal AC à une fréquence donnée dans la cellule de conductivité et mesure la réponse par DFT (partie réelle `R` + imaginaire `I`) → module et phase de l'impédance.
2. **Calibration** : avant chaque mesure, on bascule le multiplexeur ADG715 sur une résistance connue (`RCAL`) pour calculer un facteur de gain (`GF`) et une phase système (`phi_sys`) de la chaîne de mesure, à la fréquence choisie.
3. **Mesure** : le mux bascule ensuite sur la cellule réelle ; on calcule l'impédance de la solution, puis la conductivité (`sigma`, mS/cm) via la constante de cellule `K_cell = 1 cm⁻¹`.
4. Deux gammes existent selon la conductivité de l'eau :
   - **HIGH** : `RCAL = R2 = 100 Ω` — eau conductrice (eau de mer)
   - **LOW** : `RCAL = R2 = 1000 Ω` — eau peu conductrice
5. La température (capteur **TSYS01**) et, pour le code adaptatif, la pression/profondeur (capteur **MS5837 Bar30**) sont combinées à `sigma` via **PSS‑78** (Fofonoff & Millard, UNESCO 1983) pour obtenir la salinité pratique `SP` (PSU), puis `sigma25` (conductivité ramenée à 25 °C).

---

## 2. Matériel

- ESP32 (carte « LittObs »)
- Module CN0349 (AD5934 + ADG715) + cellule de conductivité 2 électrodes
- Capteur de température TSYS01 (I2C)
- Capteur de pression/profondeur MS5837 Bar30, Blue Robotics (I2C) — **code adaptatif uniquement**
- RTC DS3231M (I2C)
- Lecteur de carte SD (SPI)
- LED témoin sur GPIO25

### Câblage

| Bus | Broches ESP32 |
|---|---|
| I2C (AD5934 0x0D, ADG715 0x48, TSYS01 0x77, MS5837 0x76, DS3231 0x68) | SDA → GPIO21, SCL → GPIO22 |
| SD (SPI) | CS → GPIO5 (+ MOSI/MISO/SCK VSPI standard) |
| LED témoin | GPIO25 |

Pour le câblage détaillé du CN0349 (connecteur 8 broches SCL/SDA/DGND/VDD), voir le pinout dans le [repo joshagirgis/CN0349-Arduino-Based-Library](https://github.com/joshagirgis/CN0349-Arduino-Based-Library#wiring). La LED de la carte CN0349 doit s'allumer si le câblage I2C est correct.

---

## 3. Bibliothèques à installer (Arduino IDE)

1. **Core ESP32** : Fichier → Préférences → ajouter l'URL du gestionnaire de cartes Espressif, puis Outils → Type de carte → Gestionnaire de cartes → installer *esp32 by Espressif Systems*.
2. **Gestionnaire de bibliothèques** (Croquis → Inclure une bibliothèque → Gérer les bibliothèques), installer :
   - `TSYS01` (Blue Robotics)
   - `DS3231` (bibliothèque RTC DS3231)
   - `MS5837` (Blue Robotics — capteur Bar30, code adaptatif seulement)
3. Déjà fournies avec le core ESP32, rien à installer : `Wire.h`, `SPI.h`, `SD.h`, `math.h`.
4. `AD5934_CN0349.h` et `PSS78.h` ne sont **pas** des bibliothèques à installer : ce sont des fichiers locaux, comme `TSYS01.h`/`DS3231.h` le sont déjà chez toi. Ils doivent juste être **copiés dans le dossier du sketch** qui les utilise (voir §4).

---

## 4. Importer et téléverser un code

Arduino exige que chaque `.ino` soit dans un dossier du même nom, et que ses fichiers `.h` locaux soient **dans ce même dossier** (Arduino les affiche comme des onglets du sketch).

**Code adaptatif** (utilise les headers) :
```
conductivite_adaptative_v9_pression/
├── conductivite_adaptative_v9_pression.ino
├── AD5934_CN0349.h
└── PSS78.h
```

**Sweep** (autonome, tout dans le `.ino`) :
```
sweep_depuis_v9/
└── sweep_depuis_v9.ino
```

Étapes :
1. Créer les dossiers ci‑dessus et y placer les fichiers correspondants (copier les deux `.h` dans le dossier du code adaptatif — l'IDE les affichera comme deux onglets à côté du `.ino` principal).
2. Ouvrir le `.ino` voulu dans Arduino IDE.
3. Outils → Type de carte → sélectionner la carte ESP32 utilisée.
4. Outils → Port → sélectionner le port COM de l'ESP32.
5. Vérifier/Téléverser (flèche →).
6. Ouvrir le **Moniteur série** à **115200 bauds** pour voir les logs et envoyer des commandes.

Commandes série :

| Commande | Code adaptatif | Sweep |
|---|---|---|
| `r` | relit et affiche le fichier CSV de données du jour | idem (mais vide tant que ce code n'écrit rien sur SD) |
| `l` | relit et affiche le fichier **log d'erreurs** du jour | non disponible |
| `T20260616102500` | règle l'horloge RTC (format `T` + `AAAAMMJJHHMMSS`) | idem |

---

## 5. `AD5934_CN0349.h` — pilote bas niveau (nouveau header)

Contient tout ce qui pilote directement la puce, indépendamment de l'algorithme utilisé par-dessus :

- Constantes : adresses I2C, broches `SDA_PIN`/`SCL_PIN`, registres AD5934 (`REG_*`), commandes (`CMD_*`), résistances/mux des gammes HIGH et LOW.
- Struct `Mesure` (résultat d'une mesure d'impédance/conductivité).
- État partagé `cal_gf`, `cal_phi_sys`, `calDone`, `gamme_low`.
- I2C bas niveau : `adWrite`, `adRead`, `adRead16`, `adWrite24`, `adWrite16r`, `muxSet`, `waitValid`, `freqCode`, `getSettling`, `resetAD5934`.
- `calibrerFreq(f)` : calibration à la fréquence `f` sur la résistance `RCAL` connue → `cal_gf`, `cal_phi_sys`.
- `mesurerFreq(f)` : mesure à la fréquence `f` sur la cellule réelle → `sigma_mScm`, `Rcell_ohm`, phase, ratio réactance/résistance.
- `i2cBusRecovery()` : réinitialise le bus I2C si l'AD5934 ne répond plus.

**Contrat avec le sketch qui l'inclut** : le header appelle `logMessage(String message)` en cas d'erreur de calibration (timeout, `|DFT| nul`). Cette fonction n'est **pas** définie dans le header — c'est au `.ino` de la fournir (dans le code adaptatif, `logMessage()` écrit sur le port série et sur la SD ; un sketch plus simple pourrait juste faire `Serial.println(message)`). C'est une déclaration anticipée classique en C/C++ pour éviter que le header dépende d'une implémentation SD spécifique.

À inclure **une seule fois** par sketch (les variables `cal_gf`/`cal_phi_sys`/`calDone`/`gamme_low` sont *définies* dans le header, pas juste déclarées).

## 6. `PSS78.h` — salinité PSS‑78 (nouveau header)

- `calculerSP(sigma_mScm, T_degC, P_dbar)` : PSS‑78 complet (Fofonoff & Millard, UNESCO 1983), avec correction de température **et** correction de pression `Rp` (coefficients `e1,e2,e3,d1,d2,d3,d4`). `P_dbar` peut être approximée par la profondeur en mètres (1 dbar ≈ 1 m en eau de mer).
- `calculerC25(SP)` : salinité → conductivité de référence à 25 °C / 0 dbar.

Header autonome, ne dépend que de `math.h`.

---

## 7. `conductivite_adaptative_v9_pression.ino` (mesure autonome)

### Objectif

Déploiement de terrain sans opérateur (capteur immergé, bouée) : à chaque cycle, trouver automatiquement la fréquence de mesure la plus pertinente, mesurer la pression/profondeur et la température, puis enregistrer un résultat unique sur carte SD avant de se remettre en veille.

### Ce qu'il reste dans le `.ino` (logique applicative, pas le pilotage matériel bas niveau)

- **LED / RTC / SD** : `ledBlink`, `lireDatetime`, `lireDate`, `reglerRTCSerie`, `getNomFichier`, `getEnTeteCSV`, `ecrireSD`, `lireSD`.
- **`lireTemperature()`** : lecture TSYS01, retombe sur `TEMP_FALLBACK = 20 °C` si absent/hors plage.
- **`lireProfondeur(&pression_mbar)`** : lecture MS5837 Bar30 → profondeur (m, utilisée comme approximation de `P_dbar`) + pression brute (mbar) en sortie. Retombe sur `P = 0` si absent.
- **`estimerFopt(sigma)`** : formule empirique `f = A_COEF × sigma^B_COEF` (`A_COEF=0.2147`, `B_COEF=1.3582`), bornée entre `F_MIN=3 kHz` et `F_MAX=80 kHz`.
- **`mesureAdaptative()`** — cœur de l'algorithme (appelle `calibrerFreq`/`mesurerFreq` du header, `calculerSP`/`calculerC25` de `PSS78.h`) :
  1. **Mesure initiale** à `F_INIT = 20 kHz` en gamme HIGH (+ lecture température/profondeur). Si `sigma < SIGMA_SEUIL_GAMME` (2,0 mS/cm), bascule en gamme LOW et remesure. Si toujours invalide → sonde hors de l'eau, résultat "sigma=0" stocké.
  2. **Estimation de `f_opt`** via `estimerFopt()`.
  3. **Itérations** (jusqu'à `MAX_ITER = 20`) : calibre + mesure à `f_opt`, réévalue la gamme, recalcule `f_opt`, s'arrête quand `f` varie de moins de `SEUIL_F_HZ = 20 Hz` **et** `sigma` de moins de `SEUIL_SIGMA = 0,1 mS/cm` (convergence).
- **`logMessage(msg)`** : affiche `msg` sur le port série **et** l'ajoute, horodaté, au fichier log SD du jour — c'est la fonction attendue par `AD5934_CN0349.h`.
- **`lireLogSD()`** : relit le fichier log (commande série `l`).
- **`setup()` / `loop()`** : initialise capteurs/SD/RTC/AD5934, puis à chaque cycle → mesure adaptative → écriture SD → mise en veille (`CMD_POWERDOWN`) → 10 s de refroidissement → reset AD5934 → cycle suivant (~15–20 s au total).

### Données récupérées

Fichier `/data_AAAAMMJJ.csv` sur la carte SD, une ligne par cycle :

```
datetime ; temp_C ; profondeur_m ; pression_mbar ; gamme ; f_opt_khz ; sigma_T_mScm ; SP_PSS78_PSU ; sigma25_PSS78_mScm
```

Le port série affiche en plus : source de température, itérations, convergence, impédance de la cellule, phase, ratio |X|/R.

### Journal d'erreurs

`/log_AAAAMMJJ.txt`, une ligne par événement anormal :

```
AAAA-MM-JJ HH:MM:SS ; [NIVEAU] message
```

Niveaux : `[WARN]` (capteur absent, sonde hors eau, itération invalide), `[ERR]`/`[CAL ERR]` (calibration/mesure échouée, timeout), `[FATAL]` (AD5934 non détecté ou reset échoué — le programme se bloque après ce message). Consultable via la commande série `l`. Les erreurs I2C individuelles de très bas niveau (`adWrite`, appelée des centaines de fois par cycle) ne sont volontairement **pas** journalisées sur SD, seulement en série, pour éviter qu'une panne de bus continue ne remplisse rapidement la carte SD.

---

## 8. `sweep_depuis_v9.ino` (balayage manuel de diagnostic, inchangé)

### Objectif

Contrairement au code adaptatif, ce sketch ne cherche **pas** la meilleure fréquence : il **mesure à toutes les fréquences d'une plage fixe** et laisse l'utilisateur interpréter le tableau résultant. Il sert à :

- **valider visuellement** que la formule empirique `estimerFopt()` du code adaptatif choisit une fréquence cohérente pour l'échantillon d'eau réel ;
- **caractériser la réponse en fréquence** de la cellule/l'eau testée (repérer un plateau stable, un artefact, une zone de bruit) ;
- **choisir manuellement** une fréquence fixe à utiliser si besoin, avant un déploiement.

C'est un outil de **labo / calibration**, à faire tourner avant de faire confiance à l'algorithme adaptatif sur le terrain — pas un code de logging continu.

### Ce sketch n'a volontairement pas été touché

Contrairement au code adaptatif, `sweep_depuis_v9.ino` n'utilise pas `AD5934_CN0349.h` ni `PSS78.h` : il garde sa propre copie interne des mêmes fonctions bas niveau et de `calculerSP()`/`calculerC25()` (version **sans** correction de pression), exactement comme dans la version précédente. Il n'a donc pas non plus le capteur MS5837 ni le système de log SD. C'est un choix assumé pour ne pas risquer de casser un outil de diagnostic déjà validé — voir §10 si tu veux un jour l'aligner sur les nouveaux headers.

Balayage : pour chaque fréquence de `SWEEP_FREQ_START` (60 kHz) à `SWEEP_FREQ_STOP` (65 kHz), pas `SWEEP_INCR_HZ` (200 Hz) → 26 points. Pour chaque point : `calibrerFreq(f)` puis `mesurerFreq(f)`, calcul de `SP`/`sigma25`, stockage dans `tableauSweep[i]` (pas de sélection automatique du meilleur point). Le tableau complet est imprimé **une seule fois** sur le port série à la fin du balayage :

```
Freq_kHz | sigma_mScm | SP_PSU | sigma25_mScm
```

> Le commentaire d'origine dans le code (`(50000-10000)/1000 + 1`) est un reliquat d'une ancienne plage de test — `SWEEP_NB_POINTS = 26` reste correct pour 60→65 kHz au pas de 200 Hz, mais le commentaire mériterait d'être corrigé.

`gamme_low` est **fixée à `false` (HIGH)** dans `setup()` — ce sweep suppose une eau assez conductrice (type eau de mer). Pour tester la gamme LOW, il faut dupliquer le code et forcer `gamme_low = true`.

### Données récupérées

**Uniquement affichées sur le port série** (aucune écriture SD dans ce code). Pour conserver les résultats, il faut copier/coller la sortie du moniteur série ou la rediriger vers un fichier côté PC.

---

## 9. Comparaison

| | `conductivite_adaptative_v9_pression.ino` | `sweep_depuis_v9.ino` |
|---|---|---|
| Objectif | Mesure autonome de terrain | Diagnostic / calibration en labo |
| Code bas niveau | Factorisé dans `AD5934_CN0349.h`/`PSS78.h` | Copié en interne (non factorisé) |
| Sélection de fréquence | Automatique (formule + itérations jusqu'à convergence) | Aucune — balaie toute la plage fixe |
| Gamme HIGH/LOW | Bascule automatique selon `sigma` | Fixée HIGH |
| Points mesurés / cycle | 1 (fréquence optimale finale) | 26 (60 → 65 kHz, pas 200 Hz) |
| Capteur de pression (MS5837) | Oui — utilisé dans PSS‑78 | Non |
| Journal d'erreurs SD | Oui (`/log_AAAAMMJJ.txt`, commande `l`) | Non |
| Sortie | CSV sur SD + port série | Port série uniquement (tableau) |
| Usage typique | Bouée, capteur immergé, longue durée | Test avant déploiement |

---

## 10. Points d'attention / pistes d'amélioration

- Le sweep prend plus de temps qu'un cycle adaptatif (26 calibrations + 26 mesures au lieu d'1 à 3‑4 en général) : à prévoir avant un test en continu.
- Sans écriture SD, les données du sweep sont perdues si le port série n'est pas enregistré côté PC.
- Le sweep ne teste que la gamme HIGH.
- Le sweep n'a pas (encore) le capteur MS5837 ni le système de log SD — si tu veux les ajouter un jour, le plus simple serait de le faire passer aussi par `AD5934_CN0349.h`/`PSS78.h` (en lui ajoutant un `logMessage()` minimal, par exemple juste `Serial.println`), ce qui règlerait en même temps la duplication de code restante.
