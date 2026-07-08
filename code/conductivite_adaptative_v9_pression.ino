// ═══════════════════════════════════════════════════════════════════════════
// conductivite_adaptative_v9_pression.ino
// CN0349 / AD5934 -- ESP32 -- IUEM Plouzane -- Samya Lmaikini
//
// Base : conductivite_adaptative_v9.ino (algorithme adaptatif inchange)
//
// NOUVEAUTE (capteur de pression) :
//   - Capteur de pression/profondeur Blue Robotics Bar30 (MS5837), comme
//     dans Module_CTD.ino, mais integre ici a la place de l'EZO-EC (qui est
//     remplace par le CN0349/AD5934 dans ce projet).
//   - lireProfondeur() lit P (mbar) et la profondeur (m) sur le MS5837.
//   - calculerSP() (dans PSS78.h) prend un 3e parametre P_dbar et applique
//     la correction de pression complete de PSS-78 / UNESCO 1983 (terme Rp).
//   - profondeur_m et pression_mbar sont ajoutes au ResultatCycle et a la
//     ligne CSV enregistree sur SD.
//
// NOUVEAUTE (journal d'erreurs) :
//   - logMessage() ecrit sur le port serie ET sur /log_AAAAMMJJ.txt (SD).
//   - lireLogSD() + commande serie "l" pour relire ce journal.
//
// NOUVEAUTE (factorisation en .h) :
//   - Tout ce qui etait identique entre les differents sketches (I2C bas
//     niveau AD5934/ADG715, calibration, mesure, PSS-78) est sorti dans deux
//     fichiers a copier dans ce meme dossier de sketch :
//       AD5934_CN0349.h -- pilote CN0349/AD5934/ADG715 (calibrerFreq,
//                           mesurerFreq, i2cBusRecovery, etc.)
//       PSS78.h         -- calculerSP() / calculerC25()
//     Ce .ino ne contient plus que la logique applicative : RTC, SD, log,
//     capteurs T/P, algorithme adaptatif, setup/loop.
//   - Le sketch sweep_depuis_v9.ino n'a PAS ete modifie / ne reutilise pas
//     ces headers (garde volontairement son code bas niveau en interne).
//
// COMMANDES SERIE :
//   r  --> lire fichier de donnees SD
//   l  --> lire fichier de log erreurs SD
//   T  --> regler RTC : T20260616102500
// ═══════════════════════════════════════════════════════════════════════════

#include <Wire.h>
#include <math.h>
#include <SPI.h>
#include <SD.h>
#include "TSYS01.h"
#include <DS3231.h>
#include "MS5837.h"          // capteur de pression Blue Robotics Bar30
#include "AD5934_CN0349.h"   // pilote bas niveau AD5934/ADG715 (calibration, mesure)
#include "PSS78.h"           // salinite pratique PSS-78 (avec correction de pression)

#define SD_CS_PIN          5
#define LED_PIN            25
#define SIGMA_SEUIL_GAMME  2.0f
#define TEMP_FALLBACK      20.0f
#define F_INIT             20000.0f
#define F_MIN              3000.0f
#define F_MAX              80000.0f
#define A_COEF             0.2147f
#define B_COEF             1.3582f
#define MAX_ITER           20
#define SEUIL_F_HZ         20.0f
#define SEUIL_SIGMA        0.1f
#define FLUID_DENSITY_SW   1029.0f    // densite eau de mer (kg/m3) pour depth()

struct ResultatCycle {
    String datetime;
    float  tempC;
    String gamme;
    float  f_opt_hz;
    float  sigma_T;
    float  SP;
    float  sigma25;
    String tempSource;
    float  sigma_init;
    float  Rcell;
    float  phi_Z;
    float  ratio_XR;
    int    n_iter;
    bool   converge;
    bool   valide;
    float  profondeur_m;
    float  pression_mbar;
};

TSYS01 tempSensor; bool tempSensorOk = false;
DS3231 rtc;        bool rtcOk = false, sdOk = false;
MS5837 sensor_bar30; bool pressureSensorOk = false;

// ═════════════════════════════════════════════════════════════════════════
// LED
// ═════════════════════════════════════════════════════════════════════════
void ledBlink(int n, int ms = 150) {
    for (int i = 0; i < n; i++) {
        digitalWrite(LED_PIN, HIGH); delay(ms);
        digitalWrite(LED_PIN, LOW);  delay(ms);
    }
}

// ═════════════════════════════════════════════════════════════════════════
// RTC
// ═════════════════════════════════════════════════════════════════════════
String lireDatetime() {
    if (!rtcOk) return "0000-00-00 00:00:00";
    bool h12, PM, Century = false;
    char buf[20];
    sprintf(buf, "20%02d-%02d-%02d %02d:%02d:%02d",
            rtc.getYear(), rtc.getMonth(Century), rtc.getDate(),
            rtc.getHour(h12, PM), rtc.getMinute(), rtc.getSecond());
    return String(buf);
}

String lireDate() {
    if (!rtcOk) return "00000000";
    bool h12, PM, Century = false;
    char buf[9];
    sprintf(buf, "20%02d%02d%02d",
            rtc.getYear(), rtc.getMonth(Century), rtc.getDate());
    return String(buf);
}

void reglerRTCSerie(String cmd) {
    if (cmd.length() != 15) {
        Serial.println("[RTC ERR] Format : T20260616102500 (T+YYYYMMDDHHMMSS)");
        return;
    }
    int annee  = cmd.substring(1,5).toInt();
    int mois   = cmd.substring(5,7).toInt();
    int jour   = cmd.substring(7,9).toInt();
    int heure  = cmd.substring(9,11).toInt();
    int minute = cmd.substring(11,13).toInt();
    int sec    = cmd.substring(13,15).toInt();
    if (annee < 2024 || mois < 1 || mois > 12 || jour < 1 || jour > 31
        || heure > 23 || minute > 59 || sec > 59) {
        Serial.println("[RTC ERR] Valeurs invalides"); return;
    }
    rtc.setYear(annee - 2000); rtc.setMonth(mois); rtc.setDate(jour);
    rtc.setHour(heure); rtc.setMinute(minute); rtc.setSecond(sec);
    Serial.println("[RTC] Date regle : " + lireDatetime());
    ledBlink(3, 100);
}

// ═════════════════════════════════════════════════════════════════════════
// SD -- fichier de donnees + fichier de log
// ═════════════════════════════════════════════════════════════════════════
String getNomFichier() { return "/data_" + lireDate() + ".csv"; }

// Fichier de log des erreurs/avertissements, un par jour comme le CSV
String getNomFichierLog() { return "/log_" + lireDate() + ".txt"; }

// Ecrit un message a la fois sur le port serie ET dans le fichier log de la
// carte SD (avec horodatage RTC). C'est LA fonction que AD5934_CN0349.h
// attend (voir sa note en tete de fichier) pour journaliser les erreurs de
// calibration/mesure.
void logMessage(String message) {
    Serial.println(message);
    if (!sdOk) return;                       // pas de SD -> log serie uniquement
    File f = SD.open(getNomFichierLog(), FILE_APPEND);
    if (!f) return;                          // evite toute recursion/blocage si echec ouverture
    f.println(lireDatetime() + " ; " + message);
    f.close();
}

String getEnTeteCSV() {
    return "datetime            ; temp_C ; profondeur_m ; pression_mbar ; gamme ; "
           "f_opt_khz ; sigma_T_mScm ; SP_PSS78_PSU ; sigma25_PSS78_mScm";
}

void ecrireSD(ResultatCycle& r) {
    if (!sdOk) { Serial.println("[SD] Carte absente"); return; }
    String nomFichier = getNomFichier();
    if (!SD.exists(nomFichier)) {
        File f = SD.open(nomFichier, FILE_WRITE);
        if (f) { f.println(getEnTeteCSV()); f.close();
                 Serial.println("[SD] Nouveau fichier cree : " + nomFichier); }
        else   { Serial.println("[SD ERR] Impossible de creer " + nomFichier); return; }
    }
    File f = SD.open(nomFichier, FILE_APPEND);
    if (!f) { logMessage("[SD ERR] Impossible d'ouvrir " + nomFichier); return; }
    char ligne[180];
    sprintf(ligne,
        "%-20s; %6.3f ; %12.3f ; %13.2f ; %-4s ; %8.3f ; %12.3f ; %11.3f ; %17.3f",
        r.datetime.c_str(), r.tempC, r.profondeur_m, r.pression_mbar, r.gamme.c_str(),
        r.f_opt_hz / 1000.0f, r.sigma_T, r.SP, r.sigma25);
    f.println(ligne); f.close();
    Serial.println("[SD] Ligne ecrite --> " + nomFichier);
    // LED 2 secondes = mesure enregistree
    digitalWrite(LED_PIN, HIGH); delay(2000); digitalWrite(LED_PIN, LOW);
}

// Relit et affiche le fichier log d'erreurs du jour (commande serie "l")
void lireLogSD() {
    if (!sdOk) { Serial.println("[SD] Carte absente"); return; }
    String nomFichier = getNomFichierLog();
    if (!SD.exists(nomFichier)) {
        Serial.println("[SD] Aucun fichier log aujourd'hui (aucune erreur) : " + nomFichier); return; }
    File f = SD.open(nomFichier);
    if (!f) { Serial.println("[SD ERR] Impossible d'ouvrir"); return; }
    Serial.println("\n╔══════════════════════════════════════════════╗");
    Serial.println("║        CONTENU FICHIER LOG (erreurs)         ║");
    Serial.println("║  " + nomFichier + "                 ║");
    Serial.println("╚══════════════════════════════════════════════╝");
    int lignes = 0;
    while (f.available()) { Serial.println(f.readStringUntil('\n')); lignes++; }
    f.close();
    Serial.println("╔══════════════════════════════════════════════╗");
    Serial.print("║  Total : "); Serial.print(lignes);
    Serial.println(" lignes de log                    ║");
    Serial.println("╚══════════════════════════════════════════════╝");
}

void lireSD() {
    if (!sdOk) { Serial.println("[SD] Carte absente"); return; }
    String nomFichier = getNomFichier();
    if (!SD.exists(nomFichier)) {
        Serial.println("[SD] Fichier introuvable : " + nomFichier); return; }
    File f = SD.open(nomFichier);
    if (!f) { Serial.println("[SD ERR] Impossible d'ouvrir"); return; }
    Serial.println("\n╔══════════════════════════════════════════════╗");
    Serial.println("║        CONTENU FICHIER SD                    ║");
    Serial.println("║  " + nomFichier + "                 ║");
    Serial.println("╚══════════════════════════════════════════════╝");
    int lignes = 0;
    while (f.available()) { Serial.println(f.readStringUntil('\n')); lignes++; }
    f.close();
    Serial.println("╔══════════════════════════════════════════════╗");
    Serial.print("║  Total : "); Serial.print(lignes - 1);
    Serial.println(" mesures enregistrees            ║");
    Serial.println("╚══════════════════════════════════════════════╝");
}

// ═════════════════════════════════════════════════════════════════════════
// TEMPERATURE
// ═════════════════════════════════════════════════════════════════════════
float lireTemperature() {
    if (!tempSensorOk) { Serial.println("[TEMP] Fallback"); return TEMP_FALLBACK; }
    tempSensor.read();
    float t = tempSensor.temperature();
    if (t < -2.0f || t > 40.0f) { Serial.println("[TEMP] Hors plage"); return TEMP_FALLBACK; }
    Serial.print("[TEMP] T = "); Serial.print(t, 3); Serial.println(" C  (TSYS01)");
    return t;
}

// ═════════════════════════════════════════════════════════════════════════
// PRESSION / PROFONDEUR -- MS5837 (Blue Robotics Bar30)
// Renvoie la profondeur en m (utilisee comme approximation de P en dbar
// pour la correction PSS-78 : 1 dbar =~ 1 m en eau de mer) et fournit la
// pression brute (mbar) via le pointeur de sortie pour info/log.
// ═════════════════════════════════════════════════════════════════════════
float lireProfondeur(float* pression_mbar_out) {
    if (!pressureSensorOk) {
        Serial.println("[PRESS] Fallback -- P = 0 dbar (surface, capteur absent)");
        if (pression_mbar_out) *pression_mbar_out = 0.0f;
        return 0.0f;
    }
    sensor_bar30.read();
    float p_mbar  = sensor_bar30.pressure();   // pression absolue, mbar
    float depth_m = sensor_bar30.depth();      // m, via densite eau de mer
    if (depth_m < 0.0f) depth_m = 0.0f;        // securite si capteur a l'air libre
    if (pression_mbar_out) *pression_mbar_out = p_mbar;
    Serial.print("[PRESS] P = "); Serial.print(p_mbar, 2);
    Serial.print(" mbar  |  Profondeur = "); Serial.print(depth_m, 3);
    Serial.println(" m  (MS5837 Bar30)");
    return depth_m;
}

// ═════════════════════════════════════════════════════════════════════════
// estimerFopt -- specifique a l'algorithme adaptatif, reste dans ce .ino
// ═════════════════════════════════════════════════════════════════════════
float estimerFopt(float s) {
    if(s<0.1f) return F_MIN;
    float f=A_COEF*powf(s,B_COEF)*1000.0f;
    if(f<F_MIN) f=F_MIN; if(f>F_MAX) f=F_MAX; return roundf(f); }

// ═════════════════════════════════════════════════════════════════════════
// ALGORITHME ADAPTATIF
// calibrerFreq/mesurerFreq viennent de AD5934_CN0349.h,
// calculerSP/calculerC25 viennent de PSS78.h.
// ═════════════════════════════════════════════════════════════════════════
ResultatCycle mesureAdaptative() {
    ResultatCycle res; res.valide=false;
    Serial.println("\n╔══════════════════════════════════════════════╗");
    Serial.println("║   ALGORITHME ADAPTATIF -- ITERATIONS v9     ║");
    Serial.println("╚══════════════════════════════════════════════╝");
    res.datetime=lireDatetime(); Serial.println("  datetime : "+res.datetime);
    float tempC=lireTemperature();
    res.tempC=tempC;
    res.tempSource=tempSensorOk?"TSYS01":"FALLBACK";

    float pression_mbar = 0.0f;
    float profondeur_m  = lireProfondeur(&pression_mbar);
    res.profondeur_m  = profondeur_m;
    res.pression_mbar = pression_mbar;

    Serial.print("  Capteur T  : "); Serial.println(res.tempSource);
    Serial.print("  f_init     : "); Serial.print(F_INIT/1000.0f,3); Serial.println(" kHz");
    Serial.print("  K_cell     : "); Serial.println("1.000 cm-1");

    Serial.println("\n[ETAPE 1] Mesure initiale...");
    gamme_low=false;
    if(!calibrerFreq(F_INIT)){logMessage("[ERR] Cal echouee (gamme HIGH, f_init)");return res;}
    Mesure m1=mesurerFreq(F_INIT);
    if(!m1.valide||m1.sigma_mScm<SIGMA_SEUIL_GAMME){
        Serial.println("  --> Basculement gamme LOW"); gamme_low=true;
        if(!calibrerFreq(F_INIT)){logMessage("[ERR] Cal LOW echouee");return res;}
        m1=mesurerFreq(F_INIT);
        if(!m1.valide){
            logMessage("[WARN] Sonde hors eau -- sigma=0 stocke");
            res.gamme="LOW"; res.sigma_init=0.0f; res.f_opt_hz=F_MIN;
            res.sigma_T=0.0f; res.SP=0.0f; res.sigma25=0.0f;
            res.Rcell=0.0f; res.phi_Z=0.0f; res.ratio_XR=0.0f;
            res.n_iter=0; res.converge=false; res.valide=true;
            return res;
        }
    }
    res.gamme=gamme_low?"LOW":"HIGH";
    res.sigma_init=m1.sigma_mScm;
    Serial.print("  Gamme      : "); Serial.println(res.gamme);
    Serial.print("  sigma_init : "); Serial.print(m1.sigma_mScm,3); Serial.println(" mS/cm");

    float f_opt=estimerFopt(m1.sigma_mScm);
    if(f_opt < F_MIN) f_opt = F_MIN;
    Serial.print("\n[ETAPE 2] f_opt = "); Serial.print(f_opt/1000.0f,3); Serial.println(" kHz");
    Serial.println("\n[ETAPE 3] Iterations...");
    float f_prev=F_INIT, sigma_prev=m1.sigma_mScm;
    Mesure m_final=m1; bool converge=false; int n_iter=0;
    for(int iter=1;iter<=MAX_ITER;iter++){
        n_iter=iter;
        bool ng=(sigma_prev<SIGMA_SEUIL_GAMME);
        if(ng!=gamme_low){gamme_low=ng;Serial.println(gamme_low?"  --> Gamme LOW":"  --> Gamme HIGH");}
        if(!calibrerFreq(f_opt)) break;
        Mesure m_new=mesurerFreq(f_opt);
        if(!m_new.valide){ logMessage("[WARN] iter invalide -- arret (iter=" + String(iter) + ")"); break; }
        float delta_f=fabsf(f_opt-f_prev), delta_s=fabsf(m_new.sigma_mScm-sigma_prev);
        Serial.print("  iter=");Serial.print(iter);
        Serial.print("  f=");Serial.print(f_opt/1000.0f,3);
        Serial.print(" kHz  sigma=");Serial.print(m_new.sigma_mScm,3);
        Serial.print("  df=");Serial.print(delta_f,1);
        Serial.print("  ds=");Serial.print(delta_s,3);
        m_final=m_new; f_prev=f_opt; sigma_prev=m_new.sigma_mScm;
        if(delta_f<SEUIL_F_HZ&&delta_s<SEUIL_SIGMA){
            Serial.println("  --> CONVERGE"); converge=true; break;}
        else{Serial.println("  --> continue"); f_opt=estimerFopt(m_new.sigma_mScm);}}

    float SP      = calculerSP(m_final.sigma_mScm, tempC, profondeur_m);
    float sigma25 = calculerC25(SP);
    res.f_opt_hz=m_final.freq_hz; res.sigma_T=m_final.sigma_mScm;
    res.SP=SP; res.sigma25=sigma25;
    res.Rcell=m_final.Rcell_ohm; res.phi_Z=m_final.phi_Z_deg;
    res.ratio_XR=m_final.ratio_XR; res.n_iter=n_iter;
    res.converge=converge; res.valide=true;

    Serial.println("\n╔══════════════════════════════════════════════╗");
    Serial.println("║              RESULTAT FINAL                  ║");
    Serial.println("╠══════════════════════════════════════════════╣");
    Serial.print("║ datetime        : ");Serial.println(res.datetime);
    Serial.print("║ T               : ");Serial.print(tempC,3);
    Serial.print(" C  (");Serial.print(res.tempSource);Serial.println(")");
    Serial.print("║ Profondeur      : ");Serial.print(res.profondeur_m,3);Serial.println(" m");
    Serial.print("║ Pression        : ");Serial.print(res.pression_mbar,2);Serial.println(" mbar");
    Serial.print("║ Gamme           : ");Serial.println(res.gamme);
    Serial.print("║ Iterations      : ");Serial.println(n_iter);
    Serial.print("║ Convergence     : ");Serial.println(converge?"OUI":"NON");
    Serial.println("╠══════════════════════════════════════════════╣");
    Serial.print("║ f_init          : ");Serial.print(F_INIT/1000.0f,3);Serial.println(" kHz");
    Serial.print("║ f_opt           : ");Serial.print(res.f_opt_hz/1000.0f,3);Serial.println(" kHz");
    Serial.print("║ sigma(T)        : ");Serial.print(res.sigma_T,3);Serial.println(" mS/cm");
    Serial.print("║ >>> SP (PSS-78) : ");Serial.print(res.SP,3);Serial.println(" PSU  <<<");
    Serial.print("║ >>> sigma(25C)  : ");Serial.print(res.sigma25,3);Serial.println(" mS/cm  <<<");
    Serial.println("╠══════════════════════════════════════════════╣");
    Serial.print("║ Rcell           : ");Serial.print(res.Rcell,3);Serial.println(" ohm");
    Serial.print("║ Phase Z         : ");Serial.print(res.phi_Z,3);Serial.println(" deg");
    Serial.print("║ |X|/R           : ");Serial.println(res.ratio_XR,3);
    Serial.println("╚══════════════════════════════════════════════╝");
    return res;
}

// ═════════════════════════════════════════════════════════════════════════
// SETUP
// ═════════════════════════════════════════════════════════════════════════
void setup() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    Serial.begin(115200);
    delay(2000);
    ledBlink(3, 200);

    Serial.println("=================================================");
    Serial.println("CN0349 -- Algorithme adaptatif ITERATIF -- v9+P");
    Serial.println("LED       : GPIO25 (carte LittObs)");
    Serial.println("Capteur T : TSYS01 (I2C 0x77)");
    Serial.println("Capteur P : MS5837 Bar30 (I2C 0x76)");
    Serial.println("RTC       : DS3231M (I2C 0x68)");
    Serial.println("SD        : GPIO5 (SPI)");
    Serial.println("f_init    : 20.000 kHz  |  K_cell : 1.000 cm-1");
    Serial.println("PSS-78    : Fofonoff & Millard, UNESCO 1983 (avec correction P)");
    Serial.println("Cycle     : ~15-20 sec (LED 2s + refroid. 10s)");
    Serial.println("-------------------------------------------------");
    Serial.println("Commandes : r = lire fichier de donnees SD");
    Serial.println("            l = lire fichier de log erreurs SD");
    Serial.println("            T20260616102500 = regler heure RTC");
    Serial.println("=================================================\n");

    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(100000);
    delay(200);

    Wire.beginTransmission(0x68);
    if (Wire.endTransmission() == 0) {
        rtcOk = true;
        if (rtc.getYear() < 24) {
            Serial.println("[RTC] Date invalide -- tape T20260616HHMMSS pour regler");
        }
        Serial.println("[OK] DS3231M -- " + lireDatetime());
        Serial.println("[RTC] Pour regler : T + date ex: T20260616102500");
    } else {
        rtcOk = false;
        Serial.println("[WARN] DS3231M absent");   // pas encore de SD confirmee -> pas de logMessage ici
    }

    if (SD.begin(SD_CS_PIN)) {
        sdOk = true;
        Serial.println("[OK] Carte SD -- " + getNomFichier());
    } else {
        sdOk = false;
        Serial.println("[WARN] Carte SD absente");   // logMessage inutile : pas de SD pour ecrire le log
    }

    tempSensor.init(); delay(300); tempSensor.read();
    float tTest = tempSensor.temperature();
    if (tTest > -2.0f && tTest < 40.0f) {
        tempSensorOk = true;
        Serial.print("[OK] TSYS01 -- T = "); Serial.print(tTest,3); Serial.println(" C");
    } else {
        tempSensorOk = false;
        logMessage("[WARN] TSYS01 absent -- fallback " + String(TEMP_FALLBACK,3) + " C");
    }

    if (sensor_bar30.init()) {
        sensor_bar30.setFluidDensity(FLUID_DENSITY_SW);   // eau de mer (997 = eau douce)
        pressureSensorOk = true;
        sensor_bar30.read();
        Serial.print("[OK] MS5837 (Bar30) -- P = "); Serial.print(sensor_bar30.pressure(),2);
        Serial.print(" mbar  |  Profondeur = "); Serial.print(sensor_bar30.depth(),3); Serial.println(" m");
    } else {
        pressureSensorOk = false;
        logMessage("[WARN] MS5837 (Bar30) absent -- fallback P = 0 dbar (surface)");
    }

    uint8_t ctrl = adRead(REG_CTRL_HB);
    if (ctrl == 0xFF) { logMessage("[FATAL] AD5934 non detecte !"); while(1) delay(1000); }
    Serial.print("[OK] AD5934 -- CTRL_HB=0x"); Serial.println(ctrl,HEX);
    if (!resetAD5934()) { logMessage("[FATAL] Reset echoue !"); while(1) delay(1000); }
    muxSet(0x00);

    ledBlink(1, 500);
    Serial.println("\n[PRET] Demarrage des mesures...\n");
}

// ═════════════════════════════════════════════════════════════════════════
// LOOP
// ═════════════════════════════════════════════════════════════════════════
void loop() {
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        if (cmd == "r" || cmd == "R") { lireSD(); return; }
        else if (cmd == "l" || cmd == "L") { lireLogSD(); return; }
        else if (cmd.startsWith("T") || cmd.startsWith("t")) {
            cmd.toUpperCase(); reglerRTCSerie(cmd); return; }
    }

    Serial.println("\n[CYCLE] Demarrage...");
    uint8_t ctrl = adRead(REG_CTRL_HB);
    if (ctrl == 0xFF) {
        logMessage("[CYCLE] AD5934 ne repond pas -- recovery...");
        if (!i2cBusRecovery()) { logMessage("[ERR] i2cBusRecovery echouee -- nouvelle tentative dans 5s"); delay(5000); return; }
        resetAD5934(); muxSet(0x00); delay(200);
    }

    ResultatCycle res = mesureAdaptative();
    ecrireSD(res);

    muxSet(0x00);
    adWrite(REG_CTRL_HB, CMD_POWERDOWN);
    Serial.println("[POWERDOWN] Refroidissement 10s...\n");
    delay(10000);                          // 10 secondes
    resetAD5934(); muxSet(0x00); delay(500);  // reset rapide
}
