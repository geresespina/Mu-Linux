#pragma once
// Sonidos de activación de los modos (Data/Sound/Modes/<modo>.wav): el
// impacto sonoro al ENCENDER Mangekyou (58), Sage Mode (59), Jinchuriki
// Mode (60) o Biju Mode (61). MEAMU 2026-11-18, pedido de G: "REALLY cool
// sound effects when casting the modes".
//
// El llamador (NetRecv_07, el paquete C1:07 de estado de efecto) ya filtra
// el flanco ascendente real del toggle; acá además se lleva el último
// estado conocido por personaje para que una aplicación duplicada o de
// resincronización NUNCA re-dispare el sonido. El apagado no suena: sólo
// rearma el flanco.

// Suena el .wav del modo `skill` (58..61) para la entidad `character`
// (puntero al slot del personaje), una sola vez por transición OFF→ON.
void ModeSound_OnModeOn(unsigned char* character, int skill);
// Registra el apagado del modo `skill` (sin sonido): rearma el flanco
// para el próximo encendido.
void ModeSound_OnModeOff(unsigned char* character, int skill);
