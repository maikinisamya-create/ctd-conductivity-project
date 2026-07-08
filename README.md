# CN0349 / AD5934 — Mesure de conductivité sur ESP32

**IUEM Plouzané — Samya Lmaikini**

Ce dépôt contient les firmwares ESP32 pour le module Analog Devices **CN0349** (puce d'analyse d'impédance **AD5934** + multiplexeur **ADG715**), utilisé avec une cellule de conductivité 2 électrodes pour mesurer la conductivité de l'eau (typiquement eau de mer), la profondeur (capteur Bar30) et en déduire la salinité (PSS‑78).

| Fichier | Rôle |
|---|---|
| `i2c_scan` (dossier `code/i2c_scan/`) | **Test de câblage I2C** : scanne le bus et affiche les adresses détectées. À utiliser **en tout premier**, avant le sweep, pour vérifier que tous les capteurs répondent. |
| `sweep_freq_v3.ino` | **Balayage fréquentiel manuel** sur une plage fixe : mesure à chaque fréquence sans sélection automatique, affiche un tableau complet sur le port série. Outil de **diagnostic / calibration**, à utiliser **en premier**, avant de déployer le code adaptatif. |
| `AD5934_CN0349.h` | **Header partagé** : pilote bas niveau AD5934/ADG715 — I2C, calibration, mesure. Utilisé par le code adaptatif. |
| `PSS78.h` | **Header partagé** : calcul de la salinité pratique PSS‑78 (avec correction de pression). Utilisé par le code adaptatif. |
| `CTD_adaptatif_v3.ino` | Algorithme **adaptatif** : cherche automatiquement la fréquence de mesure optimale, itère jusqu'à convergence, mesure aussi la pression/profondeur, enregistre 1 mesure/cycle sur carte SD, journalise les erreurs. Code de **production** pour un déploiement autonome, à utiliser une fois le sweep exploité (voir §3). |

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

## 3. Pourquoi commencer par le sweep avant l'algorithme adaptatif

Le sweep et le code adaptatif ne sont pas deux alternatives indépendantes : ils s'utilisent **dans cet ordre** lors de la mise au point du capteur.

1. **Sweep d'abord** (`sweep_freq_v3.ino`) : on balaie une plage de fréquences sur l'eau/l'échantillon réel visé, et on regarde comment `sigma` (conductivité mesurée) varie en fonction de la fréquence. Ça permet de repérer, sans présumer d'aucune formule, la ou les fréquences qui donnent la mesure la plus stable/fiable pour une conductivité donnée.
2. **Régression** : en répétant des sweeps sur des échantillons/étalons de conductivités différentes et connues, on peut construire empiriquement la relation entre la conductivité mesurée et la fréquence optimale correspondante — typiquement une loi de puissance `f_opt = A × sigma^B`.
3. **Report dans le code adaptatif** : les coefficients `A` et `B` obtenus par cette régression sont exactement ceux utilisés dans `estimerFopt()` du code adaptatif :

```cpp
#define A_COEF   0.2147f
#define B_COEF   1.3582f
...
float estimerFopt(float s) {
    if (s < 0.1f) return F_MIN;
    float f = A_COEF * powf(s, B_COEF) * 1000.0f;
    ...
}
```

   Si tu refais des sweeps sur un nouvel échantillon/étalon et que la relation change, ce sont ces deux constantes qu'il faut mettre à jour dans le code adaptatif — pas l'algorithme lui‑même.

Autrement dit : le sweep sert à **calibrer/valider le modèle** que l'algorithme adaptatif utilise ensuite pour choisir sa fréquence automatiquement sur le terrain, sans opérateur.

---

## 4. Bibliothèques à installer (Arduino IDE)

1. **Core ESP32** : Fichier → Préférences → ajouter l'URL du gestionnaire de cartes Espressif, puis Outils → Type de carte → Gestionnaire de cartes → installer *esp32 by Espressif Systems*.
2. **Gestionnaire de bibliothèques** (Croquis → Inclure une bibliothèque → Gérer les bibliothèques), installer :
   - `TSYS01` (Blue Robotics)
   - `DS3231` (bibliothèque RTC DS3231)
   - `MS5837` (Blue Robotics — capteur Bar30, code adaptatif seulement)
3. Déjà fournies avec le core ESP32, rien à installer : `Wire.h`, `SPI.h`, `SD.h`, `math.h`.
4. `AD5934_CN0349.h` et `PSS78.h` ne sont **pas** des bibliothèques à installer : ce sont des fichiers locaux, comme `TSYS01.h`/`DS3231.h` le sont déjà chez toi. Ils doivent juste être **copiés dans le dossier du sketch** qui les utilise (voir §5).

---

## 5. Importer et téléverser un code

Arduino exige que chaque `.ino` soit dans un dossier du même nom, et que ses fichiers `.h` locaux soient **dans ce même dossier** (Arduino les affiche comme des onglets du sketch).

**Sweep** (autonome, tout dans le `.ino`) :
```
sweep_freq_v3/
└── sweep_freq_v3.ino
```

**Code adaptatif** (utilise les headers) :
```
CTD_adaptatif_v3/
├── CTD_adaptatif_v3.ino
├── AD5934_CN0349.h
└── PSS78.h
```

Étapes :
1. Créer les dossiers ci‑dessus et y placer les fichiers correspondants (copier les deux `.h` dans le dossier du code adaptatif — l'IDE les affichera comme deux onglets à côté du `.ino` principal).
2. Ouvrir le `.ino` voulu dans Arduino IDE.
3. Outils → Type de carte → sélectionner la carte ESP32 utilisée.
4. Outils → Port → sélectionner le port COM de l'ESP32.
5. Vérifier/Téléverser (flèche →).
6. Ouvrir le **Moniteur série** à **115200 bauds** pour voir les logs et envoyer des commandes.

Commandes série :

| Commande | Sweep | Code adaptatif |
|---|---|---|
| `r` | relit et affiche le fichier CSV de données du jour (vide tant qu'aucune donnée n'a été écrite par le code adaptatif) | relit et affiche le fichier CSV de données du jour |
| `l` | non disponible | relit et affiche le fichier **log d'erreurs** du jour |
| `T20260616102500` | règle l'horloge RTC (format `T` + `AAAAMMJJHHMMSS`) | idem |

---

## 6. Avant le sweep : test de connexion I2C (scan SCL/SDA)

Avant de lancer le sweep ou le code adaptatif, il est recommandé de vérifier que tous les capteurs I2C sont bien détectés sur le bus — câblage SDA/SCL correct, capteurs alimentés, pas de conflit d'adresse. C'est plus rapide à diagnostiquer avec un scanner I2C qu'en lançant directement un sketch de mesure et en essayant de deviner pourquoi rien ne répond.

Le sketch de scan (dossier `code/i2c_scan/` du dépôt) parcourt les adresses I2C de `0x01` à `0x7F` et affiche celles qui répondent sur le bus.

Adresses attendues sur ce montage :

| Composant | Adresse I2C |
|---|---|
| AD5934 | `0x0D` |
| ADG715 | `0x48` |
| TSYS01 | `0x77` |
| MS5837 (Bar30) | `0x76` |
| DS3231 (RTC) | `0x68` |

Si une adresse manque à l'appel : vérifier le câblage SDA (GPIO21) / SCL (GPIO22), l'alimentation du composant concerné, et l'absence d'un autre composant qui utiliserait la même adresse.

Ce test ne remplace pas le sweep — il vérifie juste que le bus fonctionne, pas que les mesures sont correctes — mais il évite de perdre du temps à déboguer un sweep qui échoue simplement parce qu'un capteur n'est pas détecté.

---

## 7. `sweep_freq_v3.ino` (balayage manuel — à utiliser après le test I2C)

### Objectif

Ce sketch ne cherche **pas** la meilleure fréquence : il **mesure à toutes les fréquences d'une plage fixe** et laisse l'utilisateur interpréter le tableau résultant. Il sert à :

- **caractériser la réponse en fréquence** de la cellule/l'eau testée (repérer un plateau stable, un artefact, une zone de bruit) ;
- **fournir les points nécessaires à la régression** `f_opt = A × sigma^B` reportée ensuite dans `estimerFopt()` du code adaptatif (voir §3) ;
- **choisir manuellement** une fréquence fixe à utiliser si besoin, avant un déploiement.

C'est un outil de **labo / calibration**, à faire tourner avant de faire confiance à l'algorithme adaptatif sur le terrain — pas un code de logging continu.

### Ce sketch n'a volontairement pas été touché

`sweep_freq_v3.ino` n'utilise pas `AD5934_CN0349.h` ni `PSS78.h` : il garde sa propre copie interne des mêmes fonctions bas niveau et de `calculerSP()`/`calculerC25()` (version **sans** correction de pression), exactement comme dans la version précédente. Il n'a donc pas non plus le capteur MS5837 ni le système de log SD. Choix assumé pour ne pas risquer de casser un outil de diagnostic déjà validé — voir §12 si tu veux un jour l'aligner sur les nouveaux headers.

### Déroulement et données récupérées

Pour chaque fréquence de `SWEEP_FREQ_START` à `SWEEP_FREQ_STOP` (pas `SWEEP_INCR_HZ`) : `calibrerFreq(f)` puis `mesurerFreq(f)`, calcul de `SP`/`sigma25`, stockage dans `tableauSweep[i]` (pas de sélection automatique du meilleur point). Le tableau complet est imprimé **une seule fois** sur le port série à la fin du balayage :

```
Freq_kHz | sigma_mScm | SP_PSU | sigma25_mScm
```

**Uniquement affiché sur le port série** (aucune écriture SD dans ce code). Pour conserver les résultats, il faut copier/coller la sortie du moniteur série ou la rediriger vers un fichier côté PC.

`gamme_low` est **fixée à `false` (HIGH)** dans `setup()` — ce sweep suppose une eau assez conductrice (type eau de mer). Pour tester la gamme LOW, il faut dupliquer le code et forcer `gamme_low = true`.

### Paramètres modifiables du sweep

```cpp
#define SWEEP_FREQ_START   60000.0f   // frequence minimale du balayage (Hz)
#define SWEEP_FREQ_STOP    65000.0f   // frequence maximale du balayage (Hz)
#define SWEEP_INCR_HZ        200.0f   // pas entre deux points (Hz)
#define SWEEP_NB_POINTS        26     // doit etre recalcule si START/STOP/INCR changent
```

Tu peux changer `SWEEP_FREQ_START`, `SWEEP_FREQ_STOP` et `SWEEP_INCR_HZ` librement pour explorer une autre plage ou affiner la résolution (par exemple élargir à 1–100 kHz avec un grand pas pour un premier repérage grossier, puis resserrer autour de la zone intéressante avec un petit pas).

⚠️ Si tu changes ces trois valeurs, il faut **recalculer `SWEEP_NB_POINTS`** en conséquence : `(SWEEP_FREQ_STOP − SWEEP_FREQ_START) / SWEEP_INCR_HZ + 1`. Ce n'est pas automatique dans le code : chaque point est calculé comme `f = SWEEP_FREQ_START + i * SWEEP_INCR_HZ` pour `i` allant de `0` à `SWEEP_NB_POINTS − 1`, sans jamais être comparé à `SWEEP_FREQ_STOP`. Un `SWEEP_NB_POINTS` trop petit fait que le balayage s'arrête avant `STOP` ; trop grand, il continue au‑delà de `STOP` sans avertissement. Le commentaire d'origine dans le code (`(50000-10000)/1000 + 1`) est d'ailleurs un reliquat d'une ancienne plage de test — `26` reste juste pour 60→65 kHz au pas de 200 Hz, mais mérite d'être recalculé (et le commentaire corrigé) à chaque changement de plage.

### Jusqu'où peut-on monter en fréquence ? (lien avec le quartz de l'AD5934)

La fréquence maximale que l'AD5934 peut synthétiser dépend de son horloge de référence **MCLK** — le quartz/oscillateur qui cadence la puce, câblé sur sa broche MCLK. Dans ce code, cette horloge est fixée par (dans `AD5934_CN0349.h` pour le code adaptatif, et en local dans le sweep) :

```cpp
#define MCLK_HZ   1000000.0f   // 1 MHz -- doit correspondre a l'oscillateur reellement cable sur la carte CN0349
```

D'après la documentation Analog Devices de l'AD5934, le cœur DDS qui génère le signal de sortie plafonne à **`MCLK / 4`**. Avec `MCLK_HZ = 1 MHz`, la limite théorique est donc `1 000 000 / 4 = 250 kHz`. C'est ce plafond, combiné à `MCLK_HZ`, qui sert dans `freqCode()` à convertir une fréquence en Hz vers le code numérique écrit dans les registres de fréquence de la puce (`REG_SF1..3`/`REG_FI1..3`).

En pratique, la documentation Analog Devices recommande de rester dans la plage **1 kHz – 100 kHz**, même si le calcul ci‑dessus autoriserait davantage : au‑delà, l'amplificateur d'excitation et la chaîne de mesure interne de l'AD5934 ne sont plus caractérisés pour rester précis (les erreurs de gain et de phase augmentent), ce qui rend la calibration par `RCAL` moins fiable. C'est pourquoi la plage actuelle du sweep (60 → 65 kHz) reste dans cette zone sûre — tu peux l'élargir jusqu'à ~100 kHz sans risque particulier, mais il vaut mieux éviter d'aller au‑delà.

⚠️ `MCLK_HZ` doit correspondre à l'oscillateur **réellement câblé** sur la carte CN0349 (horloge interne de la puce ou quartz externe). Si cette valeur ne correspond pas au vrai quartz utilisé, toutes les fréquences réellement appliquées à la cellule seront décalées proportionnellement par rapport à ce que le code croit envoyer — par exemple, un vrai quartz deux fois plus rapide que la valeur renseignée ferait que toutes les fréquences réellement émises seraient deux fois plus hautes que prévu.

---

## 8. `AD5934_CN0349.h` — pilote bas niveau (code adaptatif)

Contient tout ce qui pilote directement la puce, indépendamment de l'algorithme utilisé par-dessus :

- Constantes : adresses I2C, broches `SDA_PIN`/`SCL_PIN`, `MCLK_HZ`, registres AD5934 (`REG_*`), commandes (`CMD_*`), résistances/mux des gammes HIGH et LOW.
- Struct `Mesure` (résultat d'une mesure d'impédance/conductivité).
- État partagé `cal_gf`, `cal_phi_sys`, `calDone`, `gamme_low`.
- I2C bas niveau : `adWrite`, `adRead`, `adRead16`, `adWrite24`, `adWrite16r`, `muxSet`, `waitValid`, `freqCode`, `getSettling`, `resetAD5934`.
- `calibrerFreq(f)` : calibration à la fréquence `f` sur la résistance `RCAL` connue → `cal_gf`, `cal_phi_sys`.
- `mesurerFreq(f)` : mesure à la fréquence `f` sur la cellule réelle → `sigma_mScm`, `Rcell_ohm`, phase, ratio réactance/résistance.
- `i2cBusRecovery()` : réinitialise le bus I2C si l'AD5934 ne répond plus.

**Contrat avec le sketch qui l'inclut** : le header appelle `logMessage(String message)` en cas d'erreur de calibration (timeout, `|DFT| nul`). Cette fonction n'est **pas** définie dans le header — c'est au `.ino` de la fournir (dans le code adaptatif, `logMessage()` écrit sur le port série et sur la SD ; un sketch plus simple pourrait juste faire `Serial.println(message)`). C'est une déclaration anticipée classique en C/C++ pour éviter que le header dépende d'une implémentation SD spécifique.

À inclure **une seule fois** par sketch (les variables `cal_gf`/`cal_phi_sys`/`calDone`/`gamme_low` sont *définies* dans le header, pas juste déclarées).

## 9. `PSS78.h` — salinité PSS‑78 (code adaptatif)

- `calculerSP(sigma_mScm, T_degC, P_dbar)` : PSS‑78 complet (Fofonoff & Millard, UNESCO 1983), avec correction de température **et** correction de pression `Rp` (coefficients `e1,e2,e3,d1,d2,d3,d4`). `P_dbar` peut être approximée par la profondeur en mètres (1 dbar ≈ 1 m en eau de mer).
- `calculerC25(SP)` : salinité → conductivité de référence à 25 °C / 0 dbar.

Header autonome, ne dépend que de `math.h`.

---

## 10. `CTD_adaptatif_v3.ino` (mesure autonome — après le sweep)

### Objectif

Déploiement de terrain sans opérateur (capteur immergé, bouée) : à chaque cycle, trouver automatiquement la fréquence de mesure la plus pertinente (à partir du modèle `A_COEF`/`B_COEF` calé sur les sweeps, voir §3), mesurer la pression/profondeur et la température, puis enregistrer un résultat unique sur carte SD avant de se remettre en veille.

### Ce qu'il reste dans le `.ino` (logique applicative, pas le pilotage matériel bas niveau)

- **LED / RTC / SD** : `ledBlink`, `lireDatetime`, `lireDate`, `reglerRTCSerie`, `getNomFichier`, `getEnTeteCSV`, `ecrireSD`, `lireSD`.
- **`lireTemperature()`** : lecture TSYS01, retombe sur `TEMP_FALLBACK = 20 °C` si absent/hors plage.
- **`lireProfondeur(&pression_mbar)`** : lecture MS5837 Bar30 → profondeur (m, utilisée comme approximation de `P_dbar`) + pression brute (mbar) en sortie. Retombe sur `P = 0` si absent.
- **`estimerFopt(sigma)`** : formule empirique `f = A_COEF × sigma^B_COEF` (`A_COEF=0.2147`, `B_COEF=1.3582` — issus de la régression faite avec le sweep, voir §3), bornée entre `F_MIN=3 kHz` et `F_MAX=80 kHz`.
- **`mesureAdaptative()`** — cœur de l'algorithme (appelle `calibrerFreq`/`mesurerFreq` du header, `calculerSP`/`calculerC25` de `PSS78.h`) :
  1. **Mesure initiale** à `F_INIT = 20 kHz` en gamme HIGH (+ lecture température/profondeur). Si `sigma < SIGMA_SEUIL_GAMME` (2,0 mS/cm), bascule en gamme LOW et remesure. Si toujours invalide → sonde probablement hors de l'eau (cas normal si on allume la sonde avant de l'immerger, le temps de la poser à l'eau) : un résultat "sigma=0" est stocké, avec juste un message d'info sur le port série, **pas** une erreur journalisée.
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

Niveaux : `[WARN]` (capteur absent, itération invalide), `[ERR]`/`[CAL ERR]` (calibration/mesure échouée, timeout), `[FATAL]` (AD5934 non détecté ou reset échoué — le programme se bloque après ce message). Consultable via la commande série `l`. Les erreurs I2C individuelles de très bas niveau (`adWrite`, appelée des centaines de fois par cycle) ne sont volontairement **pas** journalisées sur SD, seulement en série, pour éviter qu'une panne de bus continue ne remplisse rapidement la carte SD.

Cas particulier : **sonde hors eau** (`sigma=0` stocké) n'est **pas** considéré comme une erreur — c'est la situation normale au démarrage, quand on allume la sonde avant de l'immerger. Ce cas est seulement affiché sur le port série (`[INFO] Sonde hors eau...`), pas écrit dans le journal d'erreurs SD.

---

## 11. Comparaison

| | `sweep_freq_v3.ino` | `CTD_adaptatif_v3.ino` |
|---|---|---|
| Objectif | Diagnostic / calibration en labo, **utilisé en premier** | Mesure autonome de terrain, utilisé une fois le modèle calé |
| Code bas niveau | Copié en interne (non factorisé) | Factorisé dans `AD5934_CN0349.h`/`PSS78.h` |
| Sélection de fréquence | Aucune — balaie toute la plage fixe | Automatique (formule + itérations jusqu'à convergence) |
| Gamme HIGH/LOW | Fixée HIGH | Bascule automatique selon `sigma` |
| Points mesurés / cycle | 26 (60 → 65 kHz, pas 200 Hz, modifiable) | 1 (fréquence optimale finale) |
| Capteur de pression (MS5837) | Non | Oui — utilisé dans PSS‑78 |
| Journal d'erreurs SD | Non | Oui (`/log_AAAAMMJJ.txt`, commande `l`) |
| Sortie | Port série uniquement (tableau) | CSV sur SD + port série |
| Usage typique | Test avant déploiement, construction du modèle `A_COEF`/`B_COEF` | Bouée, capteur immergé, longue durée |

---

## 12. Points d'attention / pistes d'amélioration

- Le sweep prend plus de temps qu'un cycle adaptatif (26 calibrations + 26 mesures au lieu d'1 à 3‑4 en général) : à prévoir avant un test en continu.
- Sans écriture SD, les données du sweep sont perdues si le port série n'est pas enregistré côté PC.
- Le sweep ne teste que la gamme HIGH.
- Si tu élargis la plage du sweep, reste dans 1 kHz – 100 kHz (voir §7) et recalcule `SWEEP_NB_POINTS`.
- Le sweep n'a pas (encore) le capteur MS5837 ni le système de log SD — si tu veux les ajouter un jour, le plus simple serait de le faire passer aussi par `AD5934_CN0349.h`/`PSS78.h` (en lui ajoutant un `logMessage()` minimal, par exemple juste `Serial.println`), ce qui règlerait en même temps la duplication de code restante.
