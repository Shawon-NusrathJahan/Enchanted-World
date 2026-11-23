// hospital_config.h - EVERYTHING you may want to change about the hospital lives here.
//
// To rename the hospital, edit NAME below and rebuild. That is the only place.
// The name is drawn with the built-in pixel font (ui.cpp): letters A-Z, digits and
// the symbols  . , : - + / % ! ? ( ) = < > ' #  are supported (lower case is shown as
// capitals). The sign shrinks the text automatically so long names still fit.

#ifndef HOSPITAL_CONFIG_H
#define HOSPITAL_CONFIG_H

namespace hospitalcfg
{

constexpr const char* NAME = "Enchanted Heals";      // <-- hospital name (exterior sign, reception, map)

// ---- ambulances -------------------------------------------------------------
constexpr float AMBULANCE_INTERVAL = 22.0f;   // average seconds between ambulances
constexpr float AMBULANCE_JITTER   = 6.0f;    // +/- random seconds (seeded, controlled randomness)
constexpr float AMBULANCE_EMERGENCY_SHARE = 0.85f;   // share of ambulance patients that are emergencies

// ---- pedestrians (outside) --------------------------------------------------
constexpr int   PEDESTRIANS        = 5;       // people walking around the hospital
constexpr float ADMIT_THRESHOLD    = 0.50f;   // hidden symptom score (0..1) needed to be admitted
constexpr float EMERGENCY_THRESHOLD = 0.82f;  // symptom score from which a patient is an emergency
constexpr float SELF_ARRIVAL_SHARE = 0.50f;   // share of sick pedestrians who walk in by themselves

// ---- inside -----------------------------------------------------------------
constexpr float ER_COOLDOWN        = 12.0f;   // seconds a patient pulled out of the ER is locked out of it
constexpr float AUTO_TRIAGE_DELAY  = 30.0f;   // the nurse assigns patients the user left waiting this long
constexpr float WORSEN_AFTER       = 35.0f;   // an emergency patient left waiting this long gets worse
constexpr int   MAX_INSIDE         = 14;      // hospital is "FULL" above this many patients

constexpr float TREAT_SPEED        = 1.0f;    // 2.0 = treatments finish twice as fast

}   // namespace hospitalcfg

#endif
