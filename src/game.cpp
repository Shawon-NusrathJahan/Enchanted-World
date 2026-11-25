// game.cpp - session score, event feed and cross-scene consequences (no drawing: see game_hud.cpp)

#include "game.h"


#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <deque>

using namespace gfx;

namespace game
{
namespace
{

int   score_ = 0;
int   perWhere_[(int)Where::COUNT] = {0, 0, 0, 0};
float shown_ = 0.0f;                 // displayed score rolls towards score_ (exponential smoothing)
float shownWhere_[(int)Where::COUNT] = {0, 0, 0, 0};   // the same for every scene's own score
Where lastWhere_ = Where::Garden;
float pulse_ = 0.0f;                 // panel "bump" when points arrive
int   lastDelta_ = 0;
float lastDeltaT_ = 10.0f;
int   restarts_ = 0;
std::vector<Event>   events_;
std::deque<Injury>   injured_;
std::deque<int>      healed_;

// a "+10" token flying from where it happened to the score along a quadratic Bezier
std::vector<Token> tokens_;



void push(Where w, int delta, const char* text)
{
    Event e;
    std::snprintf(e.text, sizeof(e.text), "%s", text);
    e.delta = delta; e.where = w; e.age = 0.0f;
    events_.push_back(e);
    if (events_.size() > 60) events_.erase(events_.begin());
    if (delta != 0)
    {
        score_ += delta;                 // the TOTAL score (all scenes together)
        perWhere_[(int)w] += delta;      // the score of this scene only
        lastWhere_ = w;
    }
}

}   // namespace

void init()
{
    score_ = 0; shown_ = 0.0f; pulse_ = 0.0f; lastDelta_ = 0; lastDeltaT_ = 10.0f; restarts_ = 0;
    for (int& v : perWhere_) v = 0;
    for (float& v : shownWhere_) v = 0.0f;
    events_.clear(); injured_.clear(); healed_.clear(); tokens_.clear();
}

void update(double dtIn)
{
    const float dt = (float)std::min(dtIn, 0.1);
    for (Event& e : events_) e.age += dt;
    shown_ += (score_ - shown_) * (1.0f - std::exp(-dt * 6.0f));
    if (std::fabs(score_ - shown_) < 0.02f) shown_ = (float)score_;
    for (int i = 0; i < (int)Where::COUNT; i++)
    {
        shownWhere_[i] += (perWhere_[i] - shownWhere_[i]) * (1.0f - std::exp(-dt * 6.0f));
        if (std::fabs(perWhere_[i] - shownWhere_[i]) < 0.02f) shownWhere_[i] = (float)perWhere_[i];
    }
    pulse_ = std::max(0.0f, pulse_ - dt * 2.5f);
    lastDeltaT_ += dt;
    for (Token& t : tokens_)
    {
        const float before = t.t;
        t.t += dt / 0.9f;
        if (before < 1.0f && t.t >= 1.0f) { pulse_ = 1.0f; }
    }
    tokens_.erase(std::remove_if(tokens_.begin(), tokens_.end(), [](const Token& t) { return t.t > 1.25f; }), tokens_.end());
}

int score() { return score_; }
int scoreAt(Where w) { return perWhere_[(int)w]; }
const std::vector<Event>& events() { return events_; }
int restarts() { return restarts_; }
void countRestart() { restarts_++; }

void add(Where w, int delta, const char* fmt, ...)
{
    char buf[96];
    va_list ap; va_start(ap, fmt); std::vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    push(w, delta, buf);
    if (delta != 0) { lastDelta_ = delta; lastDeltaT_ = 0.0f; pulse_ = 1.0f; }
}

void addAt(Where w, int delta, float x, float y, const char* fmt, ...)
{
    char buf[96];
    va_list ap; va_start(ap, fmt); std::vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    push(w, delta, buf);
    if (delta != 0)
    {
        lastDelta_ = delta; lastDeltaT_ = 0.0f;
        // control point above the midpoint -> a gentle arc up to the score
        float sx, sy; scorePos(sx, sy);
        tokens_.push_back(Token{x, y, (x + sx) * 0.5f, std::max(y, sy) + 0.35f, 0.0f, delta});   // arc to the scene score
    }
}

void sendToHospital(const Injury& inj) { injured_.push_back(inj); }
bool takeInjured(Injury& out)
{
    if (injured_.empty()) return false;
    out = injured_.front(); injured_.pop_front();
    return true;
}
int  injuredWaiting() { return (int)injured_.size(); }
void kidHealed(int kidId) { healed_.push_back(kidId); }
bool takeHealed(int& kidId)
{
    if (healed_.empty()) return false;
    kidId = healed_.front(); healed_.pop_front();
    return true;
}

// where the scene-score number sits in the HUD (the flying tokens land there; see game_hud.cpp)
void scorePos(float& x, float& y) { x = -0.10f - 0.12f; y = 0.845f + 0.035f; }

float shownScore() { return shown_; }
float shownSceneScore(Where w) { return shownWhere_[(int)w]; }
Where lastDeltaWhere() { return lastWhere_; }
float pulse()      { return pulse_; }
int   lastDelta()  { return lastDelta_; }
float lastDeltaAge() { return lastDeltaT_; }
const std::vector<Token>& tokens() { return tokens_; }

}   // namespace game
