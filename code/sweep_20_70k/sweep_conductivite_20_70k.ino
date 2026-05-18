// ═══════════════════════════════════════════════════════════════════════════
// sweep_conductivite_20_70k.ino
// CN0349 / AD5934 -- ESP32 -- IUEM Plouzane -- Samya Lmaikini
// Sweep 20 kHz -> 70 kHz, pas 500 Hz
// Sortie CSV pour Python
// ═══════════════════════════════════════════════════════════════════════════

#include <Wire.h>
#include <math.h>

#define AD5934_ADDR   0x0D
#define ADG715_ADDR   0x48
#define SDA_PIN       21
#define SCL_PIN       22

#define MCLK_HZ       1000000.0f

#define FREQ_START    20000.0f   // 20 kHz
#define FREQ_STOP     70000.0f   // 70 kHz
#define INCR_HZ         500.0f   // 500 Hz
#define NB_POINTS        101     // (70000-20000)/500 + 1


#define RCAL_OHMS     100.0f
#define R2_SERIES     100.0f
#define K_CELL_M      100.0f

// ── Temperature (mettre la valeur reelle) ────────────────────────────────
#define TEMP_C        21.7f
#define ALPHA_TEMP    0.02f

#define REG_CTRL_HB   0x80
#define REG_CTRL_LB   0x81
#define REG_SF1       0x82
#define REG_SF2       0x83
#define REG_SF3       0x84
#define REG_FI1       0x85
#define REG_FI2       0x86
#define REG_FI3       0x87
#define REG_NI1       0x88
#define REG_NI2       0x89
#define REG_ST1       0x8A
#define REG_ST2       0x8B
#define REG_STATUS    0x8F
#define REG_REAL_HB   0x94
#define REG_REAL_LB   0x95
#define REG_IMAG_HB   0x96
#define REG_IMAG_LB   0x97

#define CMD_INIT       0x11
#define CMD_START      0x21
#define CMD_INCREMENT  0x31
#define CMD_STANDBY    0xB1
#define CMD_POWERDOWN  0xA1
#define CTRL_LB_USE    0x09
#define CTRL_LB_RESET  0x10
#define STATUS_VALID   0x02

struct CalPoint { float gf; float phi_sys; };
struct MeasPoint {
    float freq, modDFT, Z_ohm, phi_Z, R_sol, X_reac, ratio;
    float Yx, Ycell, Rcell, sigma, sigma25, dRsol;
    int16_t realV, imagV;
};

CalPoint  cal[NB_POINTS];
MeasPoint mes[NB_POINTS];
bool calDone = false;

// ── I2C -- IDENTIQUE au code original ────────────────────────────────────
bool adWrite(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(AD5934_ADDR);
    Wire.write(reg); Wire.write(val);
    uint8_t e = Wire.endTransmission();
    if (e) { Serial.print("[W ERR] 0x"); Serial.print(reg,HEX); Serial.print(" e="); Serial.println(e); }
    return (e == 0);
}

uint8_t adRead(uint8_t reg) {
    Wire.beginTransmission(AD5934_ADDR);
    Wire.write(0xB0); Wire.write(reg);
    if (Wire.endTransmission() != 0) return 0xFF;
    Wire.requestFrom((uint8_t)AD5934_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0xFF;
}

int16_t adRead16(uint8_t hi, uint8_t lo) {
    return (int16_t)(((uint16_t)adRead(hi) << 8) | adRead(lo));
}

void adWrite24(uint8_t r, uint32_t v) {
    adWrite(r,   (v>>16)&0xFF);
    adWrite(r+1, (v>>8) &0xFF);
    adWrite(r+2,  v     &0xFF);
}

void adWrite16r(uint8_t r, uint16_t v) {
    adWrite(r,   (v>>8)&0xFF);
    adWrite(r+1,  v    &0xFF);
}

void muxSet(uint8_t ch) {
    Wire.beginTransmission(ADG715_ADDR);
    Wire.write(ch); Wire.endTransmission();
    delay(100);
}

bool waitValid(uint32_t ms) {
    uint32_t t0 = millis();
    while (millis()-t0 < ms) {
        if (adRead(REG_STATUS) & STATUS_VALID) return true;
        delay(2);
    }
    Serial.print("[TIMEOUT] STATUS=0x"); Serial.print(adRead(REG_STATUS),HEX);
    Serial.print(" CTRL=0x"); Serial.println(adRead(REG_CTRL_HB),HEX);
    return false;
}

uint32_t freqCode(float f) { return (uint32_t)(f/MCLK_HZ*134217728.0f+0.5f); }

uint16_t getSettling(float f) {
    uint16_t c = (uint16_t)(0.05f*f);
    if (c < 60)  c = 60;
    if (c > 511) c = 511;
    return c;
}

bool resetAD5934() {
    Serial.println("[RESET] AD5934...");
    adWrite(REG_CTRL_HB, CMD_STANDBY); delay(10);
    adWrite(REG_CTRL_LB, CTRL_LB_RESET); delay(10);
    adWrite(REG_CTRL_LB, CTRL_LB_USE); delay(10);
    uint8_t c = adRead(REG_CTRL_HB);
    Serial.print("[RESET] CTRL_HB=0x"); Serial.println(c,HEX);
    return (c != 0xFF);
}

bool initAndStartSweep(float startHz, float incrHz, uint8_t nPts) {
    adWrite(REG_CTRL_HB, CMD_STANDBY);
    adWrite(REG_CTRL_LB, CTRL_LB_USE);
    delay(10);

    adWrite24(REG_SF1, freqCode(startHz));
    adWrite24(REG_FI1, freqCode(incrHz));
    adWrite16r(REG_NI1, (uint16_t)(nPts-1));
    adWrite16r(REG_ST1, getSettling(startHz));

    uint8_t b1=adRead(REG_SF1), b2=adRead(REG_SF2), b3=adRead(REG_SF3);
    float fv = (float)(((uint32_t)b1<<16)|((uint32_t)b2<<8)|b3)*MCLK_HZ/134217728.0f;
    Serial.print("  [INIT] f_start="); Serial.print(fv,1); Serial.println(" Hz OK");

    adWrite(REG_CTRL_HB, CMD_INIT);
    adWrite(REG_CTRL_LB, CTRL_LB_USE);

    uint32_t wms = (uint32_t)((float)getSettling(startHz)/startHz*1000.0f)+300;
    Serial.print("  [INIT] attente="); Serial.print(wms); Serial.println(" ms");
    delay(wms);

    adWrite(REG_CTRL_HB, CMD_START);
    adWrite(REG_CTRL_LB, CTRL_LB_USE);
    delay(5);
    return true;
}

bool doCalibration() {
    Serial.println("\n[CAL] GF+phase -- ADG715=0x09");
    Serial.print("[CAL] Rcal="); Serial.print(RCAL_OHMS,0); Serial.println(" ohm");

    muxSet(0x00); delay(50);
    muxSet(0x09);

    if (!initAndStartSweep(FREQ_START, INCR_HZ, NB_POINTS)) return false;

    for (uint8_t i = 0; i < NB_POINTS; i++) {
        float f = FREQ_START + i*INCR_HZ;
        if (!waitValid(5000)) { Serial.print("[CAL ERR] f="); Serial.println(f); return false; }

        int16_t rv = adRead16(REG_REAL_HB, REG_REAL_LB);
        int16_t iv = adRead16(REG_IMAG_HB, REG_IMAG_LB);
        float mod  = sqrtf((float)rv*rv + (float)iv*iv);

        if (mod < 1.0f) { Serial.println("[CAL ERR] |DFT| nul"); return false; }

        cal[i].gf      = (1.0f/RCAL_OHMS)/mod;
        cal[i].phi_sys = atan2f((float)iv, (float)rv);

        Serial.print("  "); Serial.print(f/1000.0f,2);
        Serial.print("kHz GF="); Serial.print(cal[i].gf,9);
        Serial.print(" phi="); Serial.print(cal[i].phi_sys*180.0f/(float)M_PI,2);
        Serial.println("deg");

        if (i < NB_POINTS-1) {
            float fN = f+INCR_HZ;
            adWrite16r(REG_ST1, getSettling(fN));
            adWrite(REG_CTRL_HB, CMD_INCREMENT);
            adWrite(REG_CTRL_LB, CTRL_LB_USE);
            delay((uint32_t)((float)getSettling(fN)/fN*1000.0f)+10);
        }
    }
    calDone = true;
    Serial.println("[CAL] OK\n");
    return true;
}

bool doSweep() {
    if (!calDone) { Serial.println("[ERR] Calibration manquante!"); return false; }

    Serial.println("[SWEEP] ADG715=0x81");
    muxSet(0x00); delay(50);
    muxSet(0x81);

    if (!initAndStartSweep(FREQ_START, INCR_HZ, NB_POINTS)) return false;

    Serial.println("--- DEBUT DONNEES ---");
    Serial.println("freq_hz,temp_C,real,imag,modDFT,Z_ohm,R_sol,X_reac,phi_Z_deg,ratio_XR,Yx_S,Ycell_S,Rcell_ohm,sigma_Sm,sigma_mScm,sigma25_mScm,dRsol_pct");

    float R_sol_prev = 0.0f;

    for (uint8_t i = 0; i < NB_POINTS; i++) {
        float f = FREQ_START + i*INCR_HZ;
        if (!waitValid(5000)) { Serial.print("[ERR] f="); Serial.println(f); return false; }

        int16_t rv = adRead16(REG_REAL_HB, REG_REAL_LB);
        int16_t iv = adRead16(REG_IMAG_HB, REG_IMAG_LB);

        float mod  = sqrtf((float)rv*rv + (float)iv*iv);
        float phi  = atan2f((float)iv, (float)rv);
        float Z    = (mod > 0.1f) ? 1.0f/(cal[i].gf*mod) : 9999.0f;

        float phZ = phi - cal[i].phi_sys;
        while (phZ >  (float)M_PI) phZ -= 2.0f*(float)M_PI;
        while (phZ < -(float)M_PI) phZ += 2.0f*(float)M_PI;

        float Rsol  = Z*cosf(phZ);
        float Xrea  = Z*sinf(phZ);
        float ratio = (fabsf(Rsol)>0.01f) ? fabsf(Xrea/Rsol) : 9999.0f;

        float Yx=0, Ycell=0, Rcell=0, sigma=0, sigma25=0;

        if (Rsol > 0.01f) {
            Yx = 1.0f/Rsol;
            float denom = 1.0f - R2_SERIES*Yx;
            if (fabsf(denom) > 1e-6f) {
                Ycell = Yx/denom;
                if (Ycell > 1e-9f) {
                    Rcell  = 1.0f/Ycell;
                    sigma  = K_CELL_M/Rcell;
                    sigma25 = sigma/(1.0f + ALPHA_TEMP*(TEMP_C-25.0f));
                }
            }
        }

        float dRsol = 0.0f;
        if (i>0 && fabsf(R_sol_prev)>0.1f)
            dRsol = fabsf(Rsol-R_sol_prev)/fabsf(R_sol_prev)*100.0f;
        R_sol_prev = Rsol;

        mes[i] = {f, mod, Z, phZ, Rsol, Xrea, ratio, Yx, Ycell, Rcell, sigma, sigma25, dRsol, rv, iv};

        Serial.print(f,0);                              Serial.print(",");
        Serial.print(TEMP_C,1);                         Serial.print(",");
        Serial.print(rv);                               Serial.print(",");
        Serial.print(iv);                               Serial.print(",");
        Serial.print(mod,2);                            Serial.print(",");
        Serial.print(Z,3);                              Serial.print(",");
        Serial.print(Rsol,3);                           Serial.print(",");
        Serial.print(Xrea,3);                           Serial.print(",");
        Serial.print(phZ*180.0f/(float)M_PI,2);         Serial.print(",");
        Serial.print(ratio,4);                          Serial.print(",");
        Serial.print(Yx,8);                             Serial.print(",");
        Serial.print(Ycell,8);                          Serial.print(",");
        Serial.print(Rcell,3);                          Serial.print(",");
        Serial.print(sigma,5);                          Serial.print(",");
        Serial.print(sigma*10.0f,3);                    Serial.print(",");
        Serial.print(sigma25*10.0f,3);                  Serial.print(",");
        Serial.println(dRsol,3);

        if (i < NB_POINTS-1) {
            float fN = f+INCR_HZ;
            adWrite16r(REG_ST1, getSettling(fN));
            adWrite(REG_CTRL_HB, CMD_INCREMENT);
            adWrite(REG_CTRL_LB, CTRL_LB_USE);
            delay((uint32_t)((float)getSettling(fN)/fN*1000.0f)+10);
        }
    }

    Serial.println("--- FIN DONNEES ---");
    return true;
}

void printSummary() {
    uint8_t idx_best = 0;
    float min_ratio = 9999.0f;

    for (uint8_t i = 0; i < NB_POINTS; i++) {
        if (mes[i].Rcell > 0.5f && mes[i].ratio < min_ratio) {
            min_ratio = mes[i].ratio;
            idx_best = i;
        }
    }

    MeasPoint &o = mes[idx_best];
    Serial.println("\n=================================================");
    Serial.println("RESULTAT FINAL");
    Serial.println("=================================================");
    Serial.print("f_opt         : "); Serial.print(o.freq/1000.0f,2); Serial.println(" kHz");
    Serial.print("T mesure      : "); Serial.print(TEMP_C,1); Serial.println(" C");
    Serial.print("|X|/R         : "); Serial.println(o.ratio,4);
    Serial.print("R_sol         : "); Serial.print(o.R_sol,3); Serial.println(" ohm");
    Serial.print("Rcell         : "); Serial.print(o.Rcell,3); Serial.println(" ohm");
    Serial.print("Phase Z       : "); Serial.print(o.phi_Z*180.0f/(float)M_PI,2); Serial.println(" deg");
    Serial.println("-------------------------------------------------");
    Serial.print("sigma(T)      : "); Serial.print(o.sigma*10.0f,3); Serial.println(" mS/cm");
    Serial.print(">>> sigma(25C): "); Serial.print(o.sigma25*10.0f,3); Serial.println(" mS/cm  <<<");
    Serial.println("=================================================\n");
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    Serial.println("=================================================");
    Serial.println("CN0349 -- Sweep 20-70 kHz -- sigma(25C)");
    Serial.print("T="); Serial.print(TEMP_C,1); Serial.println(" C | K=100 S/m | Rcal=100 ohm");
    Serial.println("=================================================\n");

    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(100000);
    delay(500);

    uint8_t ctrl = adRead(REG_CTRL_HB);
    if (ctrl == 0xFF) { Serial.println("[FATAL] AD5934 non detecte !"); while(1) delay(1000); }
    Serial.print("[OK] AD5934 CTRL_HB=0x"); Serial.println(ctrl,HEX);

    if (!resetAD5934()) { Serial.println("[FATAL] Reset echoue !"); while(1) delay(1000); }

    muxSet(0x00);

    if (!doCalibration()) { Serial.println("[FATAL] Calibration echouee !"); while(1) delay(1000); }
}

void loop() {
    Serial.println("[CYCLE] Sweep 20-70 kHz...");
    doCalibration();
    if (doSweep()) printSummary();
    Serial.println("Prochain cycle 30s\n");
    delay(30000);
}
