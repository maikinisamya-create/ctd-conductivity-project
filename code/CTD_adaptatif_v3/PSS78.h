#ifndef PSS78_H
#define PSS78_H

// ═══════════════════════════════════════════════════════════════════════════
// PSS78.h
// Salinite pratique PSS-78 (Fofonoff & Millard, UNESCO 1983) -- IUEM Plouzane
// -- Samya Lmaikini
//
// calculerSP()  : conductivite (mS/cm) + temperature (C) + pression (dbar)
//                 --> salinite pratique SP (PSU). Inclut la correction de
//                 pression Rp -- sans elle (P=0), le calcul suppose une
//                 mesure en surface.
// calculerC25() : SP --> conductivite specifique de reference a 25C / 0 dbar.
//
// A COPIER dans le dossier du sketch qui en a besoin (comme TSYS01.h) --
// fichier local, pas une bibliotheque a installer.
// ═══════════════════════════════════════════════════════════════════════════

#include <math.h>

// ═════════════════════════════════════════════════════════════════════════
// PSS-78 COMPLET : sigma(T,P) --> SP (PSU)
// P est en decibars (dbar) -- en oceanographie, 1 dbar =~ 1 m de profondeur
// en eau de mer, donc une profondeur en m peut etre utilisee directement
// comme approximation de P.
// ═════════════════════════════════════════════════════════════════════════
float calculerSP(float C_mScm, float T_degC, float P_dbar) {
    float T = T_degC;
    float P = P_dbar;

    float rT = 0.6766097f
             + 0.0200564f   * T
             + 1.104259e-4f * T * T
             - 6.9698e-7f   * T * T * T
             + 1.0031e-9f   * T * T * T * T;

    // Rapport de conductivite par rapport a l'eau de mer standard C(35,15,0)=42.914 mS/cm
    float R = C_mScm / 42.914f;

    // Correction de pression Rp (UNESCO 1983 / PSS-78 complet)
    const float e1 =  2.070e-5f, e2 = -6.370e-10f, e3 = 3.989e-15f;
    const float d1 =  3.426e-2f, d2 =  4.464e-4f, d3 = 4.215e-1f, d4 = -3.107e-3f;
    float Rp = 1.0f + (P * (e1 + e2 * P + e3 * P * P))
                     / (1.0f + d1 * T + d2 * T * T + (d3 + d4 * T) * R);

    float Rt     = R / (Rp * rT);
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
// PSS-78 : SP --> sigma specifique a 25C / 0 dbar (mS/cm)
// (grandeur de reference standardisee -- ne depend pas de la pression in situ)
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

#endif // PSS78_H
