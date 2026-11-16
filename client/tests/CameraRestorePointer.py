#!/usr/bin/env python3
"""F11 Restore (MEAMU round v20261115-2132): el reset del zoom de distancia
de la cámara principal dispara también con la cámara 3D APAGADA; con ella
activa, la restauración completa (FOV/inclinación/altura/yaw + zoom) queda
como siempre; sin aviso cuando el F11 no cambia nada.

Compila el cuerpo REAL de CCamera3D::Restore extraído de
src/Render/Camera3D.cpp (con las constantes reales del namespace anónimo)
sobre una vista escalar de la clase con los mismos campos y defaults que
Camera3D.h. SceneFlag, CameraAngle y UI_AddNotice son stubs contables.
No certifica la sesión de juego: eso es el smoke de arranque del binario.
"""
import argparse
import re
import tempfile
from pathlib import Path

from SceneObjectPointer import build_and_run

ROOT = Path(__file__).resolve().parent.parent

HARNESS = r'''
#include <cstdio>
#include <cstdlib>
static void RequireImpl(bool ok, const char* expr, int line) {
    if (!ok) { std::fprintf(stderr, "FAIL line %d: %s\n", line, expr); std::exit(1); }
}
#define Require(x) RequireImpl((x), #x, __LINE__)

int SceneFlag;
float CameraAngle[3];
int gNotices = 0;
void UI_AddNotice(char*, int) { ++gNotices; }

CONSTANTS

class CCamera3D {
public:
    void Restore();
    bool  m_Enabled  = false;
    bool  m_Dragging = false;
    int   m_CursorX  = 0;
    int   m_CursorY  = 0;
    float m_Fov      = 50.0f;
    float m_Pitch    = 48.5f;
    float m_Height   = 150.0f;
    float m_ZoomTarget  = 1.0f;
    float m_ZoomCurrent = 1.0f;
};

RESTORE_BODY

static void ResetGlobals() { gNotices = 0; CameraAngle[0] = 0.0f; CameraAngle[1] = 0.0f; CameraAngle[2] = 0.0f; }

int main() {
    // 1) Apagada + zoom de distancia al máximo (el bug): F11 lo reinicia,
    //    avisa una vez y NO toca FOV/inclinación/altura/yaw.
    { ResetGlobals(); SceneFlag = 5; CCamera3D c; c.m_Fov = 77.0f; c.m_Pitch = 30.0f;
      c.m_Height = 999.0f; CameraAngle[2] = 123.0f;
      c.m_ZoomTarget = 2.0f; c.m_ZoomCurrent = 2.0f; c.Restore();
      Require(c.m_ZoomTarget == 1.0f && c.m_ZoomCurrent == 1.0f);
      Require(gNotices == 1);
      Require(c.m_Fov == 77.0f && c.m_Pitch == 30.0f && c.m_Height == 999.0f);
      Require(CameraAngle[2] == 123.0f); }
    // 2) Apagada + sólo el factor interpolado fuera de 1.0 (mitad del glide):
    //    también reinicia y avisa.
    { ResetGlobals(); SceneFlag = 5; CCamera3D c;
      c.m_ZoomTarget = 1.0f; c.m_ZoomCurrent = 1.35f; c.Restore();
      Require(c.m_ZoomTarget == 1.0f && c.m_ZoomCurrent == 1.0f);
      Require(gNotices == 1); }
    // 3) Apagada + zoom ya en 1.0: F11 sin efecto, SIN aviso (no spam).
    { ResetGlobals(); SceneFlag = 5; CCamera3D c; c.Restore();
      Require(c.m_ZoomTarget == 1.0f && c.m_ZoomCurrent == 1.0f);
      Require(gNotices == 0); }
    // 4) Activa + todo sucio: restauración completa de siempre + aviso.
    { ResetGlobals(); SceneFlag = 5; CCamera3D c; c.m_Enabled = true;
      c.m_Fov = 90.0f; c.m_Pitch = 25.0f; c.m_Height = 400.0f; CameraAngle[2] = 10.0f;
      c.m_ZoomTarget = 0.45f; c.m_ZoomCurrent = 0.5f; c.Restore();
      Require(c.m_Fov == kDefaultFov && c.m_Pitch == kDefaultPitch && c.m_Height == kDefaultHeight);
      Require(CameraAngle[2] == kDefaultYaw);
      Require(c.m_ZoomTarget == 1.0f && c.m_ZoomCurrent == 1.0f);
      Require(gNotices == 1); }
    // 5) Activa + ya en defaults: el aviso sale igual que hoy (comportamiento
    //    histórico intacto).
    { ResetGlobals(); SceneFlag = 5; CCamera3D c; c.m_Enabled = true; CameraAngle[2] = kDefaultYaw;
      c.Restore(); Require(gNotices == 1);
      Require(c.m_Fov == kDefaultFov && c.m_ZoomCurrent == 1.0f); }
    // 6) Fuera del mundo (login, SceneFlag 0), apagada con zoom fuera: nada.
    { ResetGlobals(); SceneFlag = 0; CCamera3D c;
      c.m_ZoomTarget = 2.0f; c.m_ZoomCurrent = 2.0f; c.Restore();
      Require(c.m_ZoomTarget == 2.0f && c.m_ZoomCurrent == 2.0f);
      Require(gNotices == 0); }
    // 7) Fuera del mundo, activa y sucia: nada (gate de escena intacto).
    { ResetGlobals(); SceneFlag = 0; CCamera3D c; c.m_Enabled = true; c.m_Fov = 90.0f;
      c.Restore(); Require(c.m_Fov == 90.0f); Require(gNotices == 0); }
    // 8) Selección de personaje (SceneFlag 4), apagada con zoom fuera: nada.
    { ResetGlobals(); SceneFlag = 4; CCamera3D c;
      c.m_ZoomTarget = 0.4f; c.m_ZoomCurrent = 0.4f; c.Restore();
      Require(c.m_ZoomTarget == 0.4f); Require(gNotices == 0); }
    std::puts("CameraRestore: 8 escenarios PASS (reset con camara apagada, restauracion activa intacta, sin spam, gate de escena intacto)");
    return 0;
}
'''


def extract(source, name):
    src = re.sub(r"//[^\n]*|/\*.*?\*/", "", source, flags=re.S)
    found = list(re.finditer(rf"void\s+CCamera3D::{name}\(\)\s*\{{", src))
    if len(found) != 1:
        raise ValueError(f"Se esperaba una definición de CCamera3D::{name}, hay {len(found)}")
    start = found[0].start()
    brace = src.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (src[end] == "{") - (src[end] == "}")
        end += 1
    return src[start:end]


def extract_constants(source):
    src = re.sub(r"//[^\n]*|/\*.*?\*/", "", source, flags=re.S)
    out = []
    for name in ("kDefaultFov", "kDefaultPitch", "kDefaultHeight", "kDefaultYaw"):
        m = re.search(rf"const float {name}\s*=\s*(-?[0-9.]+)f;", src)
        if not m:
            raise ValueError(f"Falta la constante {name}")
        out.append(f"const float {name} = {m.group(1)}f;")
    return "\n".join(out)


def harness():
    source = (ROOT / "src/Render/Camera3D.cpp").read_text(encoding="utf-8")
    return (HARNESS
            .replace("CONSTANTS", extract_constants(source))
            .replace("RESTORE_BODY", extract(source, "Restore")))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--architecture", choices=("Win32", "x64"), default="x64")
    parser.add_argument("--generator")
    parser.add_argument("--reference-ref")
    args = parser.parse_args()
    if args.reference_ref:
        parser.error("Sin referencia histórica para esta prueba")
    with tempfile.TemporaryDirectory(prefix="mu-camera-restore-") as temp:
        current = build_and_run(Path(temp) / "current", harness(), args)
        print(current, end="")


if __name__ == "__main__":
    main()
