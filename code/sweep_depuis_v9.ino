// ═══════════════════════════════════════════════════════════════════════════
// sweep_depuis_v9.ino
// CN0349 / AD5934 -- ESP32 -- IUEM Plouzane -- Samya Lmaikini
//
// Sweep manuel construit a partir de conductivite_adaptative_v9.ino.
//
// AUCUNE fonction bas niveau n'est modifiee par rapport a v9 :
//   - constantes (CTRL_LB_USE=0x09, CTRL_LB_RESET=0x10, CMD_INIT/START/
//     STANDBY, MUX_CAL/MES_HIGH/LOW, RCAL/R2 HIGH/LOW, etc.)
//   - adWrite/adRead/adRead16/adWrite24/adWrite16r/muxSet/waitValid/
//     freqCode/getSettling/resetAD5934
//   - calibrerFreq() et mesurerFreq() -- copiees telles quelles
//   - calculerSP() et calculerC25() (PSS-78) -- copiees telles quelles
//   - lireTemperature(), RTC, SD -- copiees telles quelles
//
// SEUL changement : l'algorithme adaptatif (estimerFopt + iterations,
// mesureAdaptative) est REMPLACE par un sweep manuel simple : pour
// chaque frequence de FREQ_START a FREQ_STOP (pas INCR_HZ), on appelle
// calibrerFreq(f) puis mesurerFreq(f) -- exactement les memes fonctions
// que l'algorithme adaptatif utilise -- puis on calcule SP/sigma25 et on
// stocke le resultat. Pas de selection automatique du "meilleur point" :
// le tableau est affiche en entier, a toi de l'interpreter.
//
// COMMANDES SERIE (inchangees) :
//   r  --> lire fichier SD
//   T  --> regler RTC : T20260616102500
// ═══════════════════════════════════════════════════════════════════════════

#include <Wire.h>
#include <math.h>
#include <SPI.h>
#include <SD.h>
#include "TSYS01.h"
#include <DS3231.h>

#define SD_CS_PIN          5
#define LED_PIN            25
#define AD5934_ADDR        0x0D
#define ADG715_ADDR        0x48
#define SDA_PIN            21
#define SCL_PIN            22
#define MCLK_HZ            1000000.0f
#define RCAL_HIGH          100.0f
#define R2_HIGH            100.0f
#define MUX_CAL_HIGH       0x09
#define MUX_MES_HIGH       0x81
#define RCAL_LOW           1000.0f
#define R2_LOW             1000.0f
#define MUX_CAL_LOW        0x12
#define MUX_MES_LOW        0x82
#define SIGMA_SEUIL_GAMME  2.0f
#define K_CELL_M           100.0f
#define TEMP_FALLBACK      20.0f
#define REG_CTRL_HB        0x80
#define REG_CTRL_LB        0x81
#define REG_SF1            0x82
#define REG_SF2            0x83
#define REG_SF3            0x84
#define REG_FI1            0x85
#define REG_FI2            0x86
#define REG_FI3            0x87
#define REG_NI1            0x88
#define REG_NI2            0x89
#define REG_ST1            0x8A
#define REG_ST2            0x8B
#define REG_STATUS         0x8F
#define REG_REAL_HB        0x94
#define REG_REAL_LB        0x95
#define REG_IMAG_HB        0x96
#define REG_IMAG_LB        0x97
#define CMD_INIT           0x11
#define CMD_START          0x21
#define CMD_STANDBY        0xB1
#define CMD_POWERDOWN      0xA1
#define CTRL_LB_USE        0x09
#define CTRL_LB_RESET      0x10
#define STATUS_VALID       0x02

// ── Parametres du SWEEP (seule nouveaute -- gamme HIGH, eau de mer) ───────
#define SWEEP_FREQ_START   60000.0f
#define SWEEP_FREQ_STOP    65000.0f
#define SWEEP_INCR_HZ       200.0f
#define SWEEP_NB_POINTS        26   // (50000-10000)/1000 + 1

struct Mesure {
    float freq_hz; float sigma_mScm; float Rcell_ohm;
    float phi_Z_deg; float ratio_XR; bool valide; bool gamme_low;
};

struct PointSweep {
    float freq_hz;
    float sigma_mScm;
    float SP;
    float sigma25;
    bool  valide;
};

PointSweep tableauSweep[SWEEP_NB_POINTS];

float cal_gf = 0.0f, cal_phi_sys = 0.0f;
bool  calDone = false, gamme_low = false;
TSYS01 tempSensor; bool tempSensorOk = false;
DS3231 rtc;        bool rtcOk = false, sdOk = false;

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
// PSS-78 COMPLET : sigma(T) + T --> SP (PSU)
// Fofonoff & Millard, UNESCO 1983
// (copie exacte de v9, aucune modification)
// ═════════════════════════════════════════════════════════════════════════
float calculerSP(float C_mScm, float T_degC) {
    float T = T_degC;
    float rT = 0.6766097f
             + 0.0200564f   * T
             + 1.104259e-4f * T * T
             - 6.9698e-7f   * T * T * T
             + 1.0031e-9f   * T * T * T * T;
    float Rt     = (C_mScm / 42.914f) / rT;
    float sqrtRt = sqrtf(Rt);
    float SP = 0.008f
             - 0.1692f  * sqrtRt
             + 25.3851f * Rt
             + 14.0941f * Rt * sqrtRt
             - 7.0261f  * Rt * Rt
             + 2.7081f  * Rt * Rt * sqrtRt;
    float dT = T - 15.0f;
    float k  = 1.0f + 0.0162f * dT;
    float dSP = (dT / k) * (
                 0.0005f
               - 0.0056f * sqrtRt
               - 0.0066f * Rt
               - 0.0375f * Rt * sqrtRt
               + 0.0636f * Rt * Rt
               - 0.0144f * Rt * Rt * sqrtRt);
    float result = SP + dSP;
    if (result < 0.0f)  result = 0.0f;
    if (result > 45.0f) result = 45.0f;
    return result;
}

// ═════════════════════════════════════════════════════════════════════════
// PSS-78 : SP --> sigma specifique a 25C (mS/cm)
// (copie exacte de v9, aucune modification)
// ═════════════════════════════════════════════════════════════════════════
float calculerC25(float SP) {
    if (SP <= 0.0f) return 0.0f;
    float T25  = 25.0f;
    float rT25 = 0.6766097f
               + 0.0200564f   * T25
               + 1.104259e-4f * T25 * T25
               - 6.9698e-7f   * T25 * T25 * T25
               + 1.0031e-9f   * T25 * T25 * T25 * T25;
    float Rt = SP / 25.0f;
    for (int i = 0; i < 10; i++) {
        float sqR = sqrtf(Rt);
        float SP_calc = 0.008f - 0.1692f*sqR + 25.3851f*Rt
                      + 14.0941f*Rt*sqR - 7.0261f*Rt*Rt + 2.7081f*Rt*Rt*sqR;
        float dT  = T25 - 15.0f;
        float k   = 1.0f + 0.0162f * dT;
        float dSP = (dT/k)*(0.0005f - 0.0056f*sqR - 0.0066f*Rt
                   - 0.0375f*Rt*sqR + 0.0636f*Rt*Rt - 0.0144f*Rt*Rt*sqR);
        float SP_total = SP_calc + dSP;
        if (SP_total < 0.001f) break;
        Rt = Rt * (SP / SP_total);
    }
    return Rt * rT25 * 42.914f;
}

// ═════════════════════════════════════════════════════════════════════════
// RTC (copie exacte de v9, aucune modification)
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
// SD (copie exacte de v9 -- non utilisee par le sweep, mais conservee)
// ═════════════════════════════════════════════════════════════════════════
String getNomFichier() { return "/data_" + lireDate() + ".csv"; }

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
// TEMPERATURE (copie exacte de v9, aucune modification)
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
// I2C BAS NIVEAU (copie exacte de v9, aucune modification)
// ═════════════════════════════════════════════════════════════════════════
bool adWrite(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(AD5934_ADDR); Wire.write(reg); Wire.write(val);
    uint8_t e = Wire.endTransmission();
    if (e) { Serial.print("[W ERR] 0x"); Serial.print(reg,HEX); Serial.print(" e="); Serial.println(e); }
    return (e == 0);
}
uint8_t adRead(uint8_t reg) {
    Wire.beginTransmission(AD5934_ADDR); Wire.write(0xB0); Wire.write(reg);
    if (Wire.endTransmission() != 0) return 0xFF;
    Wire.requestFrom((uint8_t)AD5934_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0xFF;
}
int16_t adRead16(uint8_t hi, uint8_t lo) {
    return (int16_t)(((uint16_t)adRead(hi) << 8) | adRead(lo)); }
void adWrite24(uint8_t r, uint32_t v) {
    adWrite(r,(v>>16)&0xFF); adWrite(r+1,(v>>8)&0xFF); adWrite(r+2,v&0xFF); }
void adWrite16r(uint8_t r, uint16_t v) {
    adWrite(r,(v>>8)&0xFF); adWrite(r+1,v&0xFF); }
void muxSet(uint8_t ch) {
    Wire.beginTransmission(ADG715_ADDR); Wire.write(ch); Wire.endTransmission(); delay(100); }
bool waitValid(uint32_t ms) {
    uint32_t t0 = millis();
    while (millis()-t0 < ms) { if (adRead(REG_STATUS) & STATUS_VALID) return true; delay(2); }
    return false; }
uint32_t freqCode(float f) { return (uint32_t)(f/MCLK_HZ*134217728.0f+0.5f); }
uint16_t getSettling(float f) {
    uint16_t c = (uint16_t)(0.05f*f);
    if (c < 60) c = 60; if (c > 511) c = 511; return c; }
bool resetAD5934() {
    adWrite(REG_CTRL_HB, CMD_STANDBY); delay(10);
    adWrite(REG_CTRL_LB, CTRL_LB_RESET); delay(10);
    adWrite(REG_CTRL_LB, CTRL_LB_USE); delay(10);
    return (adRead(REG_CTRL_HB) != 0xFF); }

// ═════════════════════════════════════════════════════════════════════════
// CALIBRATION (copie exacte de v9, aucune modification)
// ═════════════════════════════════════════════════════════════════════════
bool calibrerFreq(float fHz) {
    muxSet(0x00); delay(50);
    uint8_t mux_cal = gamme_low ? MUX_CAL_LOW : MUX_CAL_HIGH;
    float   rcal    = gamme_low ? RCAL_LOW    : RCAL_HIGH;
    muxSet(mux_cal);
    adWrite(REG_CTRL_HB, CMD_STANDBY); adWrite(REG_CTRL_LB, CTRL_LB_USE); delay(10);
    adWrite24(REG_SF1, freqCode(fHz)); adWrite24(REG_FI1, freqCode(fHz));
    adWrite16r(REG_NI1, 0); adWrite16r(REG_ST1, getSettling(fHz));
    adWrite(REG_CTRL_HB, CMD_INIT); adWrite(REG_CTRL_LB, CTRL_LB_USE);
    delay((uint32_t)((float)getSettling(fHz)/fHz*1000.0f)+300);
    adWrite(REG_CTRL_HB, CMD_START); adWrite(REG_CTRL_LB, CTRL_LB_USE); delay(5);
    if (!waitValid(5000)) { Serial.println("[CAL ERR] Timeout"); return false; }
    int16_t rv = adRead16(REG_REAL_HB, REG_REAL_LB);
    int16_t iv = adRead16(REG_IMAG_HB, REG_IMAG_LB);
    float mod = sqrtf((float)rv*rv + (float)iv*iv);
    if (mod < 1.0f) { Serial.println("[CAL ERR] |DFT| nul"); return false; }
    cal_gf = (1.0f/rcal)/mod; cal_phi_sys = atan2f((float)iv, (float)rv); calDone = true;
    Serial.print("  [CAL] f="); Serial.print(fHz/1000.0f,3);
    Serial.print(" kHz  Gamme="); Serial.print(gamme_low?"LOW":"HIGH");
    Serial.print("  GF="); Serial.print(cal_gf,8);
    Serial.print("  phi="); Serial.print(cal_phi_sys*180.0f/(float)M_PI,3); Serial.println(" deg");
    return true;
}

// ═════════════════════════════════════════════════════════════════════════
// MESURE (copie exacte de v9, aucune modification)
// ═════════════════════════════════════════════════════════════════════════
Mesure mesurerFreq(float fHz) {
    Mesure m; m.freq_hz=fHz; m.valide=false; m.gamme_low=gamme_low;
    if (!calDone) return m;
    muxSet(0x00); delay(50);
    uint8_t mux_mes = gamme_low ? MUX_MES_LOW : MUX_MES_HIGH;
    float   r2      = gamme_low ? R2_LOW      : R2_HIGH;
    muxSet(mux_mes);
    adWrite(REG_CTRL_HB, CMD_STANDBY); adWrite(REG_CTRL_LB, CTRL_LB_USE); delay(10);
    adWrite24(REG_SF1, freqCode(fHz)); adWrite24(REG_FI1, freqCode(fHz));
    adWrite16r(REG_NI1, 0); adWrite16r(REG_ST1, getSettling(fHz));
    adWrite(REG_CTRL_HB, CMD_INIT); adWrite(REG_CTRL_LB, CTRL_LB_USE);
    delay((uint32_t)((float)getSettling(fHz)/fHz*1000.0f)+300);
    adWrite(REG_CTRL_HB, CMD_START); adWrite(REG_CTRL_LB, CTRL_LB_USE); delay(5);
    if (!waitValid(5000)) return m;
    int16_t rv = adRead16(REG_REAL_HB, REG_REAL_LB);
    int16_t iv = adRead16(REG_IMAG_HB, REG_IMAG_LB);
    float mod=sqrtf((float)rv*rv+(float)iv*iv), phi=atan2f((float)iv,(float)rv);
    float Z=(mod>0.1f)?1.0f/(cal_gf*mod):9999.0f;
    float phZ=phi-cal_phi_sys;
    while(phZ>(float)M_PI) phZ-=2.0f*(float)M_PI;
    while(phZ<-(float)M_PI) phZ+=2.0f*(float)M_PI;
    float Rsol=Z*cosf(phZ), Xrea=Z*sinf(phZ);
    float ratio=(fabsf(Rsol)>0.01f)?fabsf(Xrea/Rsol):9999.0f;
    float Yx=0,Ycell=0,Rcell=0,sigma=0;
    if(Rsol>0.01f){Yx=1.0f/Rsol;float d=1.0f-r2*Yx;
        if(fabsf(d)>1e-6f){Ycell=Yx/d;if(Ycell>1e-9f){Rcell=1.0f/Ycell;sigma=K_CELL_M/Rcell;}}}
    m.sigma_mScm=sigma*10.0f; m.Rcell_ohm=Rcell;
    m.phi_Z_deg=phZ*180.0f/(float)M_PI; m.ratio_XR=ratio;
    m.valide=(Rcell>0.1f); return m;
}

// ═════════════════════════════════════════════════════════════════════════
// SWEEP MANUEL (seule partie nouvelle -- remplace mesureAdaptative())
// Pour chaque frequence : calibrerFreq(f) puis mesurerFreq(f), exactement
// les memes fonctions que ci-dessus, aucune modification de leur logique.
// ═════════════════════════════════════════════════════════════════════════
void faireSweep() {
    Serial.println("\n[SWEEP] Demarrage...");
    float tempC = lireTemperature();
    Serial.print("[SWEEP] Gamme : "); Serial.println(gamme_low ? "LOW" : "HIGH");
    Serial.print("[SWEEP] Plage : "); Serial.print(SWEEP_FREQ_START/1000.0f,1);
    Serial.print(" -> "); Serial.print(SWEEP_FREQ_STOP/1000.0f,1);
    Serial.print(" kHz, pas "); Serial.print(SWEEP_INCR_HZ,0); Serial.println(" Hz");

    for (int i = 0; i < SWEEP_NB_POINTS; i++) {
        float f = SWEEP_FREQ_START + i * SWEEP_INCR_HZ;

        if (!calibrerFreq(f)) {
            tableauSweep[i] = {f, 0, 0, 0, false};
            continue;
        }
        Mesure m = mesurerFreq(f);
        if (!m.valide) {
            tableauSweep[i] = {f, 0, 0, 0, false};
            continue;
        }
        float SP_pt = calculerSP(m.sigma_mScm, tempC);
        float sigma25_pt = calculerC25(SP_pt);
        tableauSweep[i] = {f, m.sigma_mScm, SP_pt, sigma25_pt, true};
    }

    // ── Tableau organise -- imprime UNE FOIS, a la fin du sweep ──────────
    char ligne[100];
    Serial.println();
    Serial.print("=== TABLEAU DE MESURE -- T = ");
    Serial.print(tempC, 2);
    Serial.println(" C (PSS-78 applique a chaque frequence) ===");

    sprintf(ligne, "%-9s| %-12s| %-8s| %-12s",
            "Freq_kHz", "sigma_mScm", "SP_PSU", "sigma25_mScm");
    Serial.println(ligne);
    sprintf(ligne, "---------+-------------+--------+------------");
    Serial.println(ligne);

    for (int i = 0; i < SWEEP_NB_POINTS; i++) {
        if (!tableauSweep[i].valide) {
            sprintf(ligne, "%9.2f| %12s| %8s| %12s",
                    tableauSweep[i].freq_hz / 1000.0f, "ERR", "ERR", "ERR");
        } else {
            sprintf(ligne, "%9.2f| %12.3f| %8.3f| %12.3f",
                    tableauSweep[i].freq_hz / 1000.0f,
                    tableauSweep[i].sigma_mScm,
                    tableauSweep[i].SP,
                    tableauSweep[i].sigma25);
        }
        Serial.println(ligne);
    }
    Serial.println();
}

// ═════════════════════════════════════════════════════════════════════════
// I2C BUS RECOVERY (copie exacte de v9, aucune modification)
// ═════════════════════════════════════════════════════════════════════════
bool i2cBusRecovery() {
    Wire.end(); delay(10);
    pinMode(SDA_PIN,OUTPUT); pinMode(SCL_PIN,OUTPUT);
    digitalWrite(SDA_PIN,HIGH); digitalWrite(SCL_PIN,HIGH); delay(5);
    pinMode(SDA_PIN,INPUT_PULLUP);
    if(digitalRead(SDA_PIN)==HIGH){Wire.begin(SDA_PIN,SCL_PIN);Wire.setClock(100000);delay(100);return true;}
    pinMode(SDA_PIN,OUTPUT);
    for(int i=0;i<9;i++){digitalWrite(SCL_PIN,LOW);delayMicroseconds(5);digitalWrite(SCL_PIN,HIGH);delayMicroseconds(5);}
    digitalWrite(SDA_PIN,LOW);delayMicroseconds(5);digitalWrite(SCL_PIN,HIGH);delayMicroseconds(5);
    digitalWrite(SDA_PIN,HIGH);delayMicroseconds(5);delay(50);
    Wire.begin(SDA_PIN,SCL_PIN);Wire.setClock(100000);delay(200);
    return (adRead(REG_CTRL_HB)!=0xFF);
}

// ═════════════════════════════════════════════════════════════════════════
// SETUP (copie exacte de v9, aucune modification)
// ═════════════════════════════════════════════════════════════════════════
void setup() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    Serial.begin(115200);
    delay(2000);
    ledBlink(3, 200);

    Serial.println("=================================================");
    Serial.println("CN0349 -- SWEEP (base sur v9, fonctions inchangees)");
    Serial.println("LED       : GPIO25 (carte LittObs)");
    Serial.println("Capteur T : TSYS01 (I2C 0x77)");
    Serial.println("RTC       : DS3231M (I2C 0x68)");
    Serial.println("SD        : GPIO5 (SPI)");
    Serial.println("K_cell    : 1.000 cm-1");
    Serial.println("PSS-78    : Fofonoff & Millard, UNESCO 1983");
    Serial.println("-------------------------------------------------");
    Serial.println("Commandes : r = lire SD");
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
        Serial.println("[WARN] DS3231M absent");
    }

    if (SD.begin(SD_CS_PIN)) {
        sdOk = true;
        Serial.println("[OK] Carte SD -- " + getNomFichier());
    } else {
        sdOk = false;
        Serial.println("[WARN] Carte SD absente");
    }

    tempSensor.init(); delay(300); tempSensor.read();
    float tTest = tempSensor.temperature();
    if (tTest > -2.0f && tTest < 40.0f) {
        tempSensorOk = true;
        Serial.print("[OK] TSYS01 -- T = "); Serial.print(tTest,3); Serial.println(" C");
    } else {
        tempSensorOk = false;
        Serial.println("[WARN] TSYS01 absent -- fallback " + String(TEMP_FALLBACK,3) + " C");
    }

    uint8_t ctrl = adRead(REG_CTRL_HB);
    if (ctrl == 0xFF) { Serial.println("[FATAL] AD5934 non detecte !"); while(1) delay(1000); }
    Serial.print("[OK] AD5934 -- CTRL_HB=0x"); Serial.println(ctrl,HEX);
    if (!resetAD5934()) { Serial.println("[FATAL] Reset echoue !"); while(1) delay(1000); }
    muxSet(0x00);

    gamme_low = false;   // gamme HIGH fixe (solution conductrice attendue)

    ledBlink(1, 500);
    Serial.println("\n[PRET] Demarrage du sweep...\n");
}

// ═════════════════════════════════════════════════════════════════════════
// LOOP -- appelle faireSweep() au lieu de mesureAdaptative()
// ═════════════════════════════════════════════════════════════════════════
void loop() {
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        if (cmd == "r" || cmd == "R") { lireSD(); return; }
        else if (cmd.startsWith("T") || cmd.startsWith("t")) {
            cmd.toUpperCase(); reglerRTCSerie(cmd); return; }
    }

    Serial.println("\n[CYCLE] Demarrage...");
    uint8_t ctrl = adRead(REG_CTRL_HB);
    if (ctrl == 0xFF) {
        Serial.println("[CYCLE] AD5934 ne repond pas -- recovery...");
        if (!i2cBusRecovery()) { delay(5000); return; }
        resetAD5934(); muxSet(0x00); delay(200);
    }

    faireSweep();

    muxSet(0x00);
    adWrite(REG_CTRL_HB, CMD_POWERDOWN);
    Serial.println("[POWERDOWN] Refroidissement 10s...\n");
    delay(10000);
    resetAD5934(); muxSet(0x00); delay(500);
}
