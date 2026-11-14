#pragma once

// PhantomNames — the ONE phantom-name check for the client (MEAMU).
//
// The 100 Kage/villager phantoms are server-fabricated user objects;
// to the client they look exactly like players (Kind 1, player
// models). The only tell is the NAME, matched against the shipped
// list Data/Local/phantom_names.txt (generated from the roster
// source builds/konoha-pack-2026-11-12/roster/villagers.json by
// gen_phantom_names.py in the labels pack; regenerate it when the
// roster changes).
//
// Used by the world labels (Render/HumanLabels.cpp — humans get a
// name label, phantoms don't) and by the Tab map markers
// (UI/MiniMap.cpp — humans are named yellow/green dots, phantoms are
// small grey anonymous dots). The implementation lives in
// HumanLabels.cpp next to the loader.
//
// Fail-safe direction (shared law): an unlisted name is HUMAN, and a
// missing file leaves the set empty — everyone is treated human.
bool Phantom_IsPhantomName(const char* name);
