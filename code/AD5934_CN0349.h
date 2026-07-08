#ifndef AD5934_CN0349_H
#define AD5934_CN0349_H

// ═══════════════════════════════════════════════════════════════════════════
// AD5934_CN0349.h
// Pilote bas niveau CN0349 (AD5934 + ADG715) -- IUEM Plouzane -- Samya Lmaikini
//
// Regroupe tout ce qui ne change JAMAIS entre les differents sketches
// (algorithme adaptatif, sweep, etc.) : constantes du chip, I2C bas niveau,
// calibration et mesure d'impedance/conductivite. Copie exacte de ce qui
// etait duplique dans conductivite_adaptative_v9_pression.ino.
//
// A COPIER dans le dossier du sketch qui en a besoin (comme TSYS01.h ou
// DS3231.h) -- ce n'est PAS une bibliotheque installee via le Gestionnaire
// de bibliotheques, juste un fichier local inclus avec :
//     #include "AD5934_CN0349.h"
//
// A N'INCLURE QU'UNE SEULE FOIS dans un sketch (les variables cal_gf,
// cal_phi_sys, calDone, gamme_low sont definies ici, pas seulement
// declarees -- les inclure deux fois dans le meme projet provoquerait des
// definitions multiples).
//
// IMPORTANT -- ce header attend que le .ino qui l'inclut definisse une
// fonction :
//     void logMessage(String message);
// (appelee par calibrerFreq() en cas d'erreur, pour journaliser sur SD et/ou
// port serie). Si tu ne veux pas de journalisation, defini simplement un
// logMessage() qui ne fait que Serial.println(message).
// ═══════════════════════════════════════════════════════════════════════════

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

// ── Adresses I2C ────────────────────────────────────────────────────────────
#define AD5934_ADDR        0x0D
#define ADG715_ADDR        0x48

// ── Broches I2C (utilisees par i2cBusRecovery) ─────────────────────────────
#define SDA_PIN            21
#define SCL_PIN            22

// ── Horloge de reference AD5934 ─────────────────────────────────────────────
#define MCLK_HZ            1000000.0f

// ── Resistances / mux de calibration-mesure (gammes HIGH / LOW) ───────────
#define RCAL_HIGH          100.0f
#define R2_HIGH            100.0f
#define MUX_CAL_HIGH       0x09
#define MUX_MES_HIGH       0x81
#define RCAL_LOW           1000.0f
#define R2_LOW             1000.0f
#define MUX_CAL_LOW        0x12
#define MUX_MES_LOW        0x82

// ── Constante de cellule ─────────────────────────────────────────────────────
#define K_CELL_M           100.0f

// ── Registres AD5934 ─────────────────────────────────────────────────────────
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

// ── Commandes AD5934 ──────────────────────────────────────────────────────────
#define CMD_INIT           0x11
#define CMD_START          0x21
#define CMD_STANDBY        0xB1
#define CMD_POWERDOWN      0xA1
#define CTRL_LB_USE        0x09
#define CTRL_LB_RESET      0x10
#define STATUS_VALID       0x02

// ── Resultat d'une mesure d'impedance/conductivite ──────────────────────────
struct Mesure {
    float freq_hz; float sigma_mScm; float Rcell_ohm;
    float phi_Z_deg; float ratio_XR; bool valide; bool gamme_low;
};

// ── Etat de calibration courant (partage entre calibrerFreq/mesurerFreq) ───
float cal_gf = 0.0f, cal_phi_sys = 0.0f;
bool  calDone = false, gamme_low = false;

// A fournir par le .ino qui inclut ce header (voir note en tete de fichier)
void logMessage(String message);

// ═════════════════════════════════════════════════════════════════════════
// I2C BAS NIVEAU
// ═════════════════════════════════════════════════════════════════════════
bool adWrite(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(AD5934_ADDR); Wire.write(reg); Wire.write(val);
    uint8_t e = Wire.endTransmission();
    // Pas de logMessage() ici : adWrite est appele tres frequemment (chaque
    // ecriture de registre) -- un bus I2C en panne continue remplirait vite
    // le fichier log SD. On garde juste l'affichage serie pour ce cas-la.
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
// CALIBRATION
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
    if (!waitValid(5000)) { logMessage("[CAL ERR] Timeout f=" + String(fHz,1) + " Hz"); return false; }
    int16_t rv = adRead16(REG_REAL_HB, REG_REAL_LB);
    int16_t iv = adRead16(REG_IMAG_HB, REG_IMAG_LB);
    float mod = sqrtf((float)rv*rv + (float)iv*iv);
    if (mod < 1.0f) { logMessage("[CAL ERR] |DFT| nul f=" + String(fHz,1) + " Hz"); return false; }
    cal_gf = (1.0f/rcal)/mod; cal_phi_sys = atan2f((float)iv, (float)rv); calDone = true;
    Serial.print("  [CAL] f="); Serial.print(fHz/1000.0f,3);
    Serial.print(" kHz  Gamme="); Serial.print(gamme_low?"LOW":"HIGH");
    Serial.print("  GF="); Serial.print(cal_gf,8);
    Serial.print("  phi="); Serial.print(cal_phi_sys*180.0f/(float)M_PI,3); Serial.println(" deg");
    return true;
}

// ═════════════════════════════════════════════════════════════════════════
// MESURE
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
// I2C BUS RECOVERY
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

#endif // AD5934_CN0349_H
