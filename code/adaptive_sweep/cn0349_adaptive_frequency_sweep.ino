// ═══════════════════════════════════════════════════════════════════════════
// cn0349_adaptive_frequency_sweep.ino
// CN0349 / AD5934 -- ESP32 -- IUEM Plouzane -- Samya Lmaikini
// Algorithme adaptatif: coarse sweep -> estimation f_opt -> fine sweep
// Compensation température + conversion salinité (PSS-78)
// ═══════════════════════════════════════════════════════════════════════════

#include <Wire.h>
#include <math.h>

#define AD5934_ADDR   0x0D
#define ADG715_ADDR   0x48
#define SDA_PIN       21
#define SCL_PIN       22

#define MCLK_HZ       1000000.0f

// ── PARAMETRES CAPTEUR IST LFS1K0.1305.6W.B.010-6 ──────────────────────
#define K_CELL_CM     0.86f           // cm^-1
#define RCAL_OHMS     100.0f
#define R2_SERIES     100.0f          // R2 en série avec la cellule (CN0349)

// ── PARAMETRES ALGORITHME ADAPTATIF ────────────────────────────────────
// Coarse sweep: 7 points pour estimer la gamme de conductivité
#define F_COARSE_START   3000.0f      // 3 kHz
#define F_COARSE_STOP    80000.0f     // 80 kHz
#define N_COARSE          7

// Fine sweep autour de f_opt estimée
#define F_FINE_SPAN_KHZ  10.0f        // ±10 kHz autour de f_opt
#define N_FINE           11           // 11 points pour le fine sweep

// ── MODELE EMPIRIQUE: f_opt = a * sigma^b ───────────────────────────────
// Fit sur données expérimentales Samya (WTW Mini Atlas)
#define MODEL_A         0.1295f
#define MODEL_B         1.4731f
#define MODEL_INV_B     (1.0f / MODEL_B)  // 0.6788

// ── LIMITES DE VALIDITE ───────────────────────────────────────────────
#define F_MIN_VALID     3000.0f       // 3 kHz
#define F_MAX_VALID     80000.0f      // 80 kHz

// ── TEMPERATURE (sera lue du Pt1000 intégré IST) ───────────────────────
// Pour l'instant en constante - remplacer par lecture ADC du Pt1000
#define TEMP_C          21.7f
#define ALPHA_TEMP      0.02f         // coeff. temp eau de mer ~2%/°C

// ── REGISTRES AD5934 ────────────────────────────────────────────────────
#define REG_CTRL_HB     0x80
#define REG_CTRL_LB     0x81
#define REG_SF1         0x82
#define REG_SF2         0x83
#define REG_SF3         0x84
#define REG_FI1         0x85
#define REG_FI2         0x86
#define REG_FI3         0x87
#define REG_NI1         0x88
#define REG_NI2         0x89
#define REG_ST1         0x8A
#define REG_ST2         0x8B
#define REG_STATUS      0x8F
#define REG_REAL_HB     0x94
#define REG_REAL_LB     0x95
#define REG_IMAG_HB     0x96
#define REG_IMAG_LB     0x97

#define CMD_INIT        0x11
#define CMD_START       0x21
#define CMD_INCREMENT   0x31
#define CMD_STANDBY     0xB1
#define CMD_POWERDOWN   0xA1
#define CTRL_LB_USE     0x09
#define CTRL_LB_RESET   0x10
#define STATUS_VALID    0x02

// ── STRUCTURES ─────────────────────────────────────────────────────────
struct SweepPoint {
    float freq;
    float modDFT;
    float Z_ohm;
    float phi_Z;
    float R_sol;
    float X_reac;
    float ratio_XR;
    float Yx;
    float Ycell;
    float Rcell;
    float sigma_Sm;      // S/m
    float sigma_mScm;    // mS/cm
    float sigma25_mScm;  // mS/cm à 25°C
    int16_t realV;
    int16_t imagV;
};

struct CalData {
    float freq;
    float gf;            // gain factor
    float phi_sys;       // phase système
};

// ── VARIABLES GLOBALES ─────────────────────────────────────────────────
CalData   calCoarse[N_COARSE];
CalData   calFine[N_FINE];
SweepPoint spCoarse[N_COARSE];
SweepPoint spFine[N_FINE];
bool      calDone = false;

// ── FONCTIONS I2C AD5934 ───────────────────────────────────────────────
bool adWrite(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(AD5934_ADDR);
    Wire.write(reg); Wire.write(val);
    return (Wire.endTransmission() == 0);
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
    return false;
}

uint32_t freqCode(float f) {
    return (uint32_t)(f / MCLK_HZ * 134217728.0f + 0.5f);
}

uint16_t getSettling(float f) {
    uint16_t c = (uint16_t)(0.05f * f);
    if (c < 60)  c = 60;
    if (c > 511) c = 511;
    return c;
}

bool resetAD5934() {
    adWrite(REG_CTRL_HB, CMD_STANDBY); delay(10);
    adWrite(REG_CTRL_LB, CTRL_LB_RESET); delay(10);
    adWrite(REG_CTRL_LB, CTRL_LB_USE); delay(10);
    return (adRead(REG_CTRL_HB) != 0xFF);
}

// ── CONFIGURATION SWEEP ────────────────────────────────────────────────
bool configureSweep(float startHz, float incrHz, uint8_t nPts) {
    adWrite(REG_CTRL_HB, CMD_STANDBY);
    adWrite(REG_CTRL_LB, CTRL_LB_USE);
    delay(10);

    adWrite24(REG_SF1, freqCode(startHz));
    adWrite24(REG_FI1, freqCode(incrHz));
    adWrite16r(REG_NI1, (uint16_t)(nPts - 1));
    adWrite16r(REG_ST1, getSettling(startHz));

    adWrite(REG_CTRL_HB, CMD_INIT);
    adWrite(REG_CTRL_LB, CTRL_LB_USE);

    uint32_t wms = (uint32_t)((float)getSettling(startHz) / startHz * 1000.0f) + 300;
    delay(wms);

    adWrite(REG_CTRL_HB, CMD_START);
    adWrite(REG_CTRL_LB, CTRL_LB_USE);
    delay(5);
    return true;
}

// ── LECTURE POINT SWEEP ────────────────────────────────────────────────
bool readSweepPoint(SweepPoint &sp, CalData &cal, float f, bool isLast) {
    if (!waitValid(5000)) return false;

    int16_t rv = adRead16(REG_REAL_HB, REG_REAL_LB);
    int16_t iv = adRead16(REG_IMAG_HB, REG_IMAG_LB);

    float mod = sqrtf((float)rv*rv + (float)iv*iv);
    if (mod < 0.1f) { mod = 0.1f; } // éviter division par zéro

    float phi = atan2f((float)iv, (float)rv);
    float Z = 1.0f / (cal.gf * mod);

    float phZ = phi - cal.phi_sys;
    while (phZ > M_PI)  phZ -= 2.0f*M_PI;
    while (phZ < -M_PI) phZ += 2.0f*M_PI;

    float Rsol  = Z * cosf(phZ);
    float Xrea  = Z * sinf(phZ);
    float ratio = (fabsf(Rsol) > 0.01f) ? fabsf(Xrea / Rsol) : 9999.0f;

    float Yx=0, Ycell=0, Rcell=0, sigma=0, sigma25=0;

    if (Rsol > 0.01f) {
        Yx = 1.0f / Rsol;
        float denom = 1.0f - R2_SERIES * Yx;
        if (fabsf(denom) > 1e-6f) {
            Ycell = Yx / denom;
            if (Ycell > 1e-9f) {
                Rcell = 1.0f / Ycell;
                sigma = K_CELL_CM / (Rcell * 100.0f);  // K en cm^-1, R en ohm -> S/m
                sigma25 = sigma / (1.0f + ALPHA_TEMP * (TEMP_C - 25.0f));
            }
        }
    }

    sp.freq = f;
    sp.modDFT = mod;
    sp.Z_ohm = Z;
    sp.phi_Z = phZ;
    sp.R_sol = Rsol;
    sp.X_reac = Xrea;
    sp.ratio_XR = ratio;
    sp.Yx = Yx;
    sp.Ycell = Ycell;
    sp.Rcell = Rcell;
    sp.sigma_Sm = sigma;
    sp.sigma_mScm = sigma * 10.0f;       // S/m -> mS/cm
    sp.sigma25_mScm = sigma25 * 10.0f;
    sp.realV = rv;
    sp.imagV = iv;

    if (!isLast) {
        float fN = f + (f > 1000.0f ? f * 0.2f : 1000.0f); // incr relatif pour coarse
        // NOTE: pour le fine sweep on gère l'incr ailleurs
        adWrite16r(REG_ST1, getSettling(fN));
        adWrite(REG_CTRL_HB, CMD_INCREMENT);
        adWrite(REG_CTRL_LB, CTRL_LB_USE);
        delay((uint32_t)((float)getSettling(fN) / fN * 1000.0f) + 10);
    }
    return true;
}

// ════════════════════════════════════════════════════════════════════════
// ETAPE 1: CALIBRATION COARSE (avec Rcal)
// ════════════════════════════════════════════════════════════════════════
bool doCalibrationCoarse() {
    Serial.println("\n[CAL COARSE] Rcal=" + String(RCAL_OHMS,0) + " ohm");
    muxSet(0x00); delay(50);
    muxSet(0x09); // Canal calibration

    // Calcul des fréquences coarse (logarithmique)
    float f_log_start = log10f(F_COARSE_START);
    float f_log_stop  = log10f(F_COARSE_STOP);
    float f_step_log  = (f_log_stop - f_log_start) / (N_COARSE - 1);

    float freqs[N_COARSE];
    for (int i=0; i<N_COARSE; i++) {
        freqs[i] = powf(10.0f, f_log_start + i * f_step_log);
    }

    float incrHz = freqs[1] - freqs[0]; // approximatif pour config
    if (!configureSweep(freqs[0], incrHz, N_COARSE)) return false;

    for (int i=0; i<N_COARSE; i++) {
        float f = freqs[i];
        if (!waitValid(5000)) return false;

        int16_t rv = adRead16(REG_REAL_HB, REG_REAL_LB);
        int16_t iv = adRead16(REG_IMAG_HB, REG_IMAG_LB);
        float mod = sqrtf((float)rv*rv + (float)iv*iv);
        if (mod < 1.0f) return false;

        calCoarse[i].freq = f;
        calCoarse[i].gf = (1.0f / RCAL_OHMS) / mod;
        calCoarse[i].phi_sys = atan2f((float)iv, (float)rv);

        Serial.print("  "); Serial.print(f/1000.0f,1);
        Serial.print("kHz GF="); Serial.print(calCoarse[i].gf,9);
        Serial.println();

        if (i < N_COARSE - 1) {
            float fN = freqs[i+1];
            adWrite16r(REG_ST1, getSettling(fN));
            adWrite(REG_CTRL_HB, CMD_INCREMENT);
            adWrite(REG_CTRL_LB, CTRL_LB_USE);
            delay((uint32_t)((float)getSettling(fN)/fN*1000.0f) + 10);
        }
    }
    Serial.println("[CAL COARSE] OK");
    return true;
}

// ════════════════════════════════════════════════════════════════════════
// ETAPE 2: SWEEP COARSE SUR SOLUTION (détection gamme)
// ════════════════════════════════════════════════════════════════════════
bool doCoarseSweep() {
    Serial.println("\n[SWEEP COARSE] Solution inconnue...");
    muxSet(0x00); delay(50);
    muxSet(0x81); // Canal mesure

    float f_log_start = log10f(F_COARSE_START);
    float f_log_stop  = log10f(F_COARSE_STOP);
    float f_step_log  = (f_log_stop - f_log_start) / (N_COARSE - 1);

    float freqs[N_COARSE];
    for (int i=0; i<N_COARSE; i++) {
        freqs[i] = powf(10.0f, f_log_start + i * f_step_log);
    }

    float incrHz = freqs[1] - freqs[0];
    if (!configureSweep(freqs[0], incrHz, N_COARSE)) return false;

    for (int i=0; i<N_COARSE; i++) {
        if (!readSweepPoint(spCoarse[i], calCoarse[i], freqs[i], i==N_COARSE-1)) {
            return false;
        }
    }

    // Affichage CSV coarse
    Serial.println("\n--- COARSE DATA ---");
    Serial.println("freq_kHz,ratio_XR,sigma25_mScm");
    for (int i=0; i<N_COARSE; i++) {
        Serial.print(spCoarse[i].freq/1000.0f,1); Serial.print(",");
        Serial.print(spCoarse[i].ratio_XR,4); Serial.print(",");
        Serial.println(spCoarse[i].sigma25_mScm,3);
    }
    Serial.println("--- END COARSE ---");
    return true;
}

// ════════════════════════════════════════════════════════════════════════
// ETAPE 3: ESTIMATION f_opt ET GAMME DE CONDUCTIVITE
// ════════════════════════════════════════════════════════════════════════
float estimateFoptAndSigma(float &sigma_est) {
    // Trouve le point avec le meilleur ratio |X|/R (minimum)
    int bestIdx = 0;
    float minRatio = 9999.0f;
    float bestSigma = 0.0f;

    for (int i=0; i<N_COARSE; i++) {
        if (spCoarse[i].Rcell > 0.5f && spCoarse[i].ratio_XR < minRatio) {
            minRatio = spCoarse[i].ratio_XR;
            bestIdx = i;
            bestSigma = spCoarse[i].sigma25_mScm;
        }
    }

    // Si on a un sigma valide, on utilise le modèle empirique
    // f_opt = a * sigma^b
    float f_opt;
    if (bestSigma > 0.5f) {
        f_opt = MODEL_A * powf(bestSigma, MODEL_B);
        sigma_est = bestSigma;
    } else {
        // Fallback: utilise la fréquence du meilleur point
        f_opt = spCoarse[bestIdx].freq / 1000.0f; // en kHz
        // Estime sigma inverse
        sigma_est = powf(f_opt / MODEL_A, MODEL_INV_B);
    }

    // Clamp dans les limites valides
    if (f_opt < F_MIN_VALID / 1000.0f) f_opt = F_MIN_VALID / 1000.0f;
    if (f_opt > F_MAX_VALID / 1000.0f) f_opt = F_MAX_VALID / 1000.0f;

    Serial.println("\n[ESTIMATION]");
    Serial.print("  Meilleur point coarse: "); Serial.print(spCoarse[bestIdx].freq/1000.0f,1);
    Serial.print(" kHz, ratio="); Serial.println(minRatio,4);
    Serial.print("  Sigma estimée: "); Serial.print(sigma_est,2); Serial.println(" mS/cm");
    Serial.print("  f_opt calculée: "); Serial.print(f_opt,1); Serial.println(" kHz");

    return f_opt; // en kHz
}

// ════════════════════════════════════════════════════════════════════════
// ETAPE 4: CALIBRATION FINE (autour de f_opt)
// ════════════════════════════════════════════════════════════════════════
bool doCalibrationFine(float f_opt_kHz) {
    Serial.println("\n[CAL FINE] autour de " + String(f_opt_kHz,1) + " kHz");
    muxSet(0x00); delay(50);
    muxSet(0x09);

    float f_center = f_opt_kHz * 1000.0f; // Hz
    float f_start  = f_center - F_FINE_SPAN_KHZ * 1000.0f;
    float f_stop   = f_center + F_FINE_SPAN_KHZ * 1000.0f;

    if (f_start < F_MIN_VALID) f_start = F_MIN_VALID;
    if (f_stop > F_MAX_VALID)  f_stop = F_MAX_VALID;

    float incrHz = (f_stop - f_start) / (N_FINE - 1);

    if (!configureSweep(f_start, incrHz, N_FINE)) return false;

    for (int i=0; i<N_FINE; i++) {
        float f = f_start + i * incrHz;
        if (!waitValid(5000)) return false;

        int16_t rv = adRead16(REG_REAL_HB, REG_REAL_LB);
        int16_t iv = adRead16(REG_IMAG_HB, REG_IMAG_LB);
        float mod = sqrtf((float)rv*rv + (float)iv*iv);
        if (mod < 1.0f) return false;

        calFine[i].freq = f;
        calFine[i].gf = (1.0f / RCAL_OHMS) / mod;
        calFine[i].phi_sys = atan2f((float)iv, (float)rv);

        if (i < N_FINE - 1) {
            adWrite16r(REG_ST1, getSettling(f + incrHz));
            adWrite(REG_CTRL_HB, CMD_INCREMENT);
            adWrite(REG_CTRL_LB, CTRL_LB_USE);
            delay((uint32_t)((float)getSettling(f+incrHz)/(f+incrHz)*1000.0f) + 10);
        }
    }
    Serial.println("[CAL FINE] OK");
    return true;
}

// ════════════════════════════════════════════════════════════════════════
// ETAPE 5: SWEEP FINE (mesure précise)
// ════════════════════════════════════════════════════════════════════════
bool doFineSweep(float f_opt_kHz) {
    Serial.println("\n[SWEEP FINE] Solution...");
    muxSet(0x00); delay(50);
    muxSet(0x81);

    float f_center = f_opt_kHz * 1000.0f;
    float f_start  = f_center - F_FINE_SPAN_KHZ * 1000.0f;
    float f_stop   = f_center + F_FINE_SPAN_KHZ * 1000.0f;

    if (f_start < F_MIN_VALID) f_start = F_MIN_VALID;
    if (f_stop > F_MAX_VALID)  f_stop = F_MAX_VALID;

    float incrHz = (f_stop - f_start) / (N_FINE - 1);

    if (!configureSweep(f_start, incrHz, N_FINE)) return false;

    Serial.println("\n--- FINE DATA ---");
    Serial.println("freq_kHz,temp_C,real,imag,modDFT,Z_ohm,R_sol,X_reac,phi_Z_deg,ratio_XR,Yx_S,Ycell_S,Rcell_ohm,sigma_Sm,sigma_mScm,sigma25_mScm");

    for (int i=0; i<N_FINE; i++) {
        float f = f_start + i * incrHz;
        if (!readSweepPoint(spFine[i], calFine[i], f, i==N_FINE-1)) return false;

        Serial.print(spFine[i].freq/1000.0f,2); Serial.print(",");
        Serial.print(TEMP_C,1); Serial.print(",");
        Serial.print(spFine[i].realV); Serial.print(",");
        Serial.print(spFine[i].imagV); Serial.print(",");
        Serial.print(spFine[i].modDFT,2); Serial.print(",");
        Serial.print(spFine[i].Z_ohm,3); Serial.print(",");
        Serial.print(spFine[i].R_sol,3); Serial.print(",");
        Serial.print(spFine[i].X_reac,3); Serial.print(",");
        Serial.print(spFine[i].phi_Z * 180.0f / M_PI,2); Serial.print(",");
        Serial.print(spFine[i].ratio_XR,4); Serial.print(",");
        Serial.print(spFine[i].Yx,8); Serial.print(",");
        Serial.print(spFine[i].Ycell,8); Serial.print(",");
        Serial.print(spFine[i].Rcell,3); Serial.print(",");
        Serial.print(spFine[i].sigma_Sm,5); Serial.print(",");
        Serial.print(spFine[i].sigma_mScm,3); Serial.print(",");
        Serial.println(spFine[i].sigma25_mScm,3);
    }
    Serial.println("--- END FINE ---");
    return true;
}

// ════════════════════════════════════════════════════════════════════════
// ETAPE 6: RESULTAT FINAL
// ════════════════════════════════════════════════════════════════════════
void printFinalResult(float f_opt_kHz, float sigma_est_coarse) {
    // Trouve le meilleur point du fine sweep
    int bestIdx = 0;
    float minRatio = 9999.0f;

    for (int i=0; i<N_FINE; i++) {
        if (spFine[i].Rcell > 0.5f && spFine[i].ratio_XR < minRatio) {
            minRatio = spFine[i].ratio_XR;
            bestIdx = i;
        }
    }

    SweepPoint &best = spFine[bestIdx];

    Serial.println("\n=================================================");
    Serial.println("RESULTAT FINAL");
    Serial.println("=================================================");
    Serial.print("f_opt (coarse)  : "); Serial.print(f_opt_kHz,1); Serial.println(" kHz");
    Serial.print("f_opt (fine)    : "); Serial.print(best.freq/1000.0f,2); Serial.println(" kHz");
    Serial.print("T mesure        : "); Serial.print(TEMP_C,1); Serial.println(" C");
    Serial.print("|X|/R min       : "); Serial.println(minRatio,4);
    Serial.print("R_sol           : "); Serial.print(best.R_sol,3); Serial.println(" ohm");
    Serial.print("Rcell           : "); Serial.print(best.Rcell,3); Serial.println(" ohm");
    Serial.println("-------------------------------------------------");
    Serial.print("sigma(T)        : "); Serial.print(best.sigma_mScm,3); Serial.println(" mS/cm");
    Serial.print(">>> sigma(25C)  : "); Serial.print(best.sigma25_mScm,3); Serial.println(" mS/cm <<<");
    Serial.println("=================================================");

    // Estimation de la salinité (approximation rapide)
    // Pour S ~ 35, sigma(25C) ~ 42.9 mS/cm (eau de mer standard)
    // Relation approximative: S ≈ sigma * 35/42.9 pour eau de mer
    if (best.sigma25_mScm > 2.0f) {
        float S_approx = best.sigma25_mScm * 35.0f / 42.914f;
        Serial.print("Salinité estimée: "); Serial.print(S_approx,2); Serial.println(" g/kg (approx)");
    }
}

// ════════════════════════════════════════════════════════════════════════
// SETUP & LOOP
// ════════════════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    delay(2000);

    Serial.println("=================================================");
    Serial.println("CN0349 -- Adaptive Sweep -- sigma(25C) + Salinité");
    Serial.print("T="); Serial.print(TEMP_C,1); Serial.println(" C");
    Serial.print("K="); Serial.print(K_CELL_CM,2); Serial.println(" cm^-1 (IST LFS1K0)");
    Serial.println("Modele: f_opt = 0.130 * sigma^1.473");
    Serial.println("=================================================");

    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(100000);
    delay(500);

    if (adRead(REG_CTRL_HB) == 0xFF) {
        Serial.println("[FATAL] AD5934 non detecte !");
        while(1) delay(1000);
    }

    if (!resetAD5934()) {
        Serial.println("[FATAL] Reset echoue !");
        while(1) delay(1000);
    }
}

void loop() {
    Serial.println("\n\n[CYCLE] ============================");

    // ÉTAPE 1: Calibration coarse
    if (!doCalibrationCoarse()) {
        Serial.println("[ERR] Calibration coarse echouee");
        delay(30000);
        return;
    }

    // ÉTAPE 2: Sweep coarse sur solution
    if (!doCoarseSweep()) {
        Serial.println("[ERR] Sweep coarse echoue");
        delay(30000);
        return;
    }

    // ÉTAPE 3: Estimation f_opt et sigma
    float sigma_est = 0.0f;
    float f_opt_kHz = estimateFoptAndSigma(sigma_est);

    // ÉTAPE 4: Calibration fine
    if (!doCalibrationFine(f_opt_kHz)) {
        Serial.println("[ERR] Calibration fine echouee");
        delay(30000);
        return;
    }

    // ÉTAPE 5: Sweep fine
    if (!doFineSweep(f_opt_kHz)) {
        Serial.println("[ERR] Sweep fine echoue");
        delay(30000);
        return;
    }

    // ÉTAPE 6: Résultat
    printFinalResult(f_opt_kHz, sigma_est);

    Serial.println("\nProchain cycle dans 30s...");
    delay(30000);
}
