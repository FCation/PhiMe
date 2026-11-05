// Practice mode: loop / play once a chosen section of the chart, at 0.05x - 2x, optionally keeping pitch.
#pragma once
#include "mod.h"

enum PracticeState { PR_OFF, PR_IDLE, PR_EDIT, PR_RUN };

struct PracticeSession {
    PracticeState st = PR_OFF;
    float a = 0, b = 0;    // section, chart time (seconds)
    float songLen = 0;     // chart length (seconds)
    float speed = 1;       // 0.05 .. 2.0
    float flow = 1;        // note flow speed 0.5 .. 20 (LevelInformation.Speed x this while a section runs)
    bool loop = false;
    bool keepPitch = true;
};
extern PracticeSession g_prac;

// practice.cpp
void practicePreview(float t);   // editor: show the chart at time t (paused)
void practiceStart();            // editor "开始练习": play [a, b]
void practiceCancelEdit();       // editor "取消": back to the game's pause menu
void practiceSaveSettings();     // persist speed / loop / keepPitch
void practiceAudioJump(float clip);  // the music was moved to clip time `clip` (resume replay)
const char* practiceSongKey();       // the music's Addressables key ("" before the song is known)
float practiceClipOffset();          // clip time = chart time + this

// practice_ui.cpp
void editorOpen();
void editorClose();
void editorTick(float dt);
bool editorIsOpen();
