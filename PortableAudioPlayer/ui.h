// ui.h — State Machine Antarmuka Layar (Now Playing, Library Menu, Track Browser, Volume Popup)
#pragma once
#include <Arduino.h>
#include "input.h"

enum UiScreen : uint8_t {
  SCREEN_NOW_PLAYING = 0,
  SCREEN_MAIN_MENU,
  SCREEN_TRACK_LIST,
  SCREEN_ARTIST_LIST,
  SCREEN_ALBUM_LIST,
  SCREEN_ABOUT
};

void uiBegin();
void uiHandleInput(InputEvent evt);
void uiTick();
UiScreen uiGetScreen();

