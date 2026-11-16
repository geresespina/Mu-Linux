#!/usr/bin/env python3
"""Interpolación de frame al cruzar el fin del ciclo (MEAMU v20261115-2132).

El descarte histórico de la mezcla cuando el frame envuelve (wrap) congelaba
la pose un paso entero mientras la posición se deslizaba el paso completo;
con la cadencia y el paso x1.9 de Ninja Run eso se volvió un tirón rítmico
al correr. Compila Blend/BlendAngle/RenderSnapshots_Interpolate REALES de
src/Game/RenderSnapshots.cpp (con el RenderPose real de RenderSnapshots.h) y
verifica: (a) la lógica anterior EFECTIVAMENTE congela en el wrap, (b) la
nueva desenvuelve-mezcla-reenvuelve con extremos exactos y prior según la
convención del SIM, (c) fuera del wrap la salida es IDÉNTICA a la anterior
(barrido de regresión contra una copia de la lógica previa), y (d) un
reinicio de acción (firma inválida) sigue sin mezclarse.
"""
import argparse
import re
import tempfile
from pathlib import Path

from SceneObjectPointer import build_and_run

ROOT = Path(__file__).resolve().parent.parent

HARNESS = r'''
#include <cmath>
#include <cstdio>
#include <cstdlib>
static void RequireImpl(bool ok, const char* expr, int line) {
    if (!ok) { std::fprintf(stderr, "FAIL line %d: %s\n", line, expr); std::exit(1); }
}
#define Require(x) RequireImpl((x), #x, __LINE__)
static bool NearEq(float a, float b) { return std::fabs(a - b) <= 1e-4f; }

#include "HEADER_PATH"

FUNCTIONS

// Copia de la lógica PREVIA a la ronda (sin loopLength): referencia de
// regresión para todo lo que no sea el wrap firmado.
static float RefBlend(float a, float b, float alpha) {
    if (alpha <= 0.0f) return a;
    if (alpha >= 1.0f) return b;
    return a + (b - a) * alpha;
}
static RenderPose ReferenceInterpolate(const RenderPosePair& pair, float alpha) {
    if (!pair.continuous || alpha >= 1.0f) return pair.current;
    RenderPose pose = pair.current;
    if (alpha < 0.0f) alpha = 0.0f;
    for (int axis = 0; axis < 3; ++axis) {
        pose.position[axis] = RefBlend(pair.previous.position[axis], pair.current.position[axis], alpha);
        pose.angle[axis] = BlendAngle(pair.previous.angle[axis], pair.current.angle[axis], alpha);
        pose.headAngle[axis] = BlendAngle(pair.previous.headAngle[axis], pair.current.headAngle[axis], alpha);
    }
    const bool sameAction = pair.previous.action == pose.action && pair.previous.priorAction == pose.priorAction;
    if (sameAction && pose.animationFrame >= pair.previous.animationFrame &&
        pose.priorAnimationFrame >= pair.previous.priorAnimationFrame) {
        pose.animationFrame = RefBlend(pair.previous.animationFrame, pose.animationFrame, alpha);
        pose.priorAnimationFrame = (int)pose.animationFrame == (int)pair.previous.animationFrame ?
            pair.previous.priorAnimationFrame : pair.current.priorAnimationFrame;
    }
    pose.scale = RefBlend(pair.previous.scale, pose.scale, alpha);
    pose.alpha = RefBlend(pair.previous.alpha, pose.alpha, alpha);
    return pose;
}

static RenderPosePair Pair(float pf, float pp, float cf, float cp, float loop,
                           unsigned char action = 15, bool continuous = true) {
    RenderPosePair pair;
    pair.continuous = continuous;
    pair.previous.animationFrame = pf; pair.previous.priorAnimationFrame = pp;
    pair.previous.action = action; pair.previous.priorAction = action;
    pair.previous.loopLength = loop;
    pair.current.animationFrame = cf; pair.current.priorAnimationFrame = cp;
    pair.current.action = action; pair.current.priorAction = action;
    pair.current.loopLength = loop;
    pair.previous.position[0] = 0.0f; pair.current.position[0] = 28.5f; // paso Ninja Run
    return pair;
}
static bool SamePose(const RenderPose& a, const RenderPose& b) {
    return a.animationFrame == b.animationFrame && a.priorAnimationFrame == b.priorAnimationFrame &&
           a.position[0] == b.position[0] && a.position[1] == b.position[1] &&
           a.scale == b.scale && a.alpha == b.alpha;
}

int main() {
    const float alphas[] = {0.0f, 0.25f, 0.5f, 0.75f, 0.999f};
    // (a) El wrap histórico congela: la referencia devuelve el frame post-wrap
    //     constante para todo alpha < 1 mientras la posición se desliza 28.5.
    { RenderPosePair w = Pair(8.6f, 6.0f, 0.6f, 8.0f, 10.0f);
      for (float a : alphas) { RenderPose r = ReferenceInterpolate(w, a);
          Require(r.animationFrame == 0.6f); }
      Require(NearEq(ReferenceInterpolate(w, 0.5f).position[0], 14.25f)); }
    // (b) El wrap nuevo: extremos exactos, avance monótono desenvuelto, prior
    //     = convención del SIM (previo antes de cruzar la clave, actual tras
    //     cruzarla y tras el wrap), posición lineal intacta.
    { RenderPosePair w = Pair(8.6f, 6.0f, 0.6f, 8.0f, 10.0f);
      RenderPose r0 = RenderSnapshots_Interpolate(w, 0.0f);
      Require(r0.animationFrame == 8.6f && r0.priorAnimationFrame == 6.0f);
      RenderPose r1 = RenderSnapshots_Interpolate(w, 0.25f);
      Require(NearEq(r1.animationFrame, 9.1f) && r1.priorAnimationFrame == 8.0f);
      RenderPose r2 = RenderSnapshots_Interpolate(w, 0.5f);
      Require(NearEq(r2.animationFrame, 9.6f) && r2.priorAnimationFrame == 8.0f);
      Require(NearEq(r2.position[0], 14.25f));
      RenderPose r3 = RenderSnapshots_Interpolate(w, 0.75f);
      Require(NearEq(r3.animationFrame, 0.1f) && r3.priorAnimationFrame == 8.0f);
      RenderPose r4 = RenderSnapshots_Interpolate(w, 0.999f);
      Require(NearEq(r4.animationFrame, 0.598f) && r4.priorAnimationFrame == 8.0f);
      float unwrapped = r0.animationFrame; // avance monótono en espacio desenvuelto
      for (RenderPose r : {r1, r2, r3, r4}) {
          float u = r.animationFrame < unwrapped - 5.0f ? r.animationFrame + 10.0f : r.animationFrame;
          Require(u > unwrapped); unwrapped = u; } }
    // (b2) Variante LockPositions: ciclo efectivo = FrameCount - 1 = 9.
    { RenderPosePair w = Pair(7.6f, 5.0f, 0.6f, 7.0f, 9.0f);
      RenderPose r = RenderSnapshots_Interpolate(w, 0.5f);
      Require(NearEq(r.animationFrame, 8.6f) && r.priorAnimationFrame == 7.0f);
      RenderPose r9 = RenderSnapshots_Interpolate(w, 0.9f);
      Require(NearEq(r9.animationFrame, 0.4f) && r9.priorAnimationFrame == 7.0f); }
    // (d) Firma inválida = reinicio de acción: sin mezcla, igual que antes.
    { RenderPosePair bad = Pair(8.6f, 6.0f, 0.6f, 0.0f, 10.0f);
      for (float a : alphas) Require(SamePose(RenderSnapshots_Interpolate(bad, a), ReferenceInterpolate(bad, a))); }
    // (d2) Sin longitud de ciclo (acción no en bucle / FX): igual que antes.
    { RenderPosePair nol = Pair(8.6f, 6.0f, 0.6f, 8.0f, 0.0f);
      for (float a : alphas) Require(SamePose(RenderSnapshots_Interpolate(nol, a), ReferenceInterpolate(nol, a))); }
    // (c) Barrido de regresión fuera del wrap: salida idéntica a la previa.
    { const float frames[] = {0.0f, 0.4f, 2.2f, 4.4f, 5.2f, 8.6f, 9.9f};
      for (float pf : frames) for (float cf : frames) {
          if (cf < pf) continue; // monótono: el dominio histórico
          for (float pp : {0.0f, 3.0f, 6.0f}) for (float cp : {0.0f, 3.0f, 6.0f}) {
              if (cp < pp) continue;
              RenderPosePair pr = Pair(pf, pp, cf, cp, 10.0f);
              for (float a : alphas)
                  Require(SamePose(RenderSnapshots_Interpolate(pr, a), ReferenceInterpolate(pr, a))); } }
      // cambio de acción, no continuo y alpha >= 1: idénticos
      RenderPosePair ch = Pair(4.4f, 3.0f, 5.2f, 4.0f, 10.0f); ch.current.action = 16;
      Require(SamePose(RenderSnapshots_Interpolate(ch, 0.5f), ReferenceInterpolate(ch, 0.5f)));
      RenderPosePair nc = Pair(8.6f, 6.0f, 0.6f, 8.0f, 10.0f, 15, false);
      Require(SamePose(RenderSnapshots_Interpolate(nc, 0.5f), ReferenceInterpolate(nc, 0.5f)));
      RenderPosePair w = Pair(8.6f, 6.0f, 0.6f, 8.0f, 10.0f);
      Require(SamePose(RenderSnapshots_Interpolate(w, 1.0f), ReferenceInterpolate(w, 1.0f))); }
    std::puts("RenderWrapInterpolate: PASS (wrap desenvuelto con extremos exactos; fuera del wrap, identico a la logica previa)");
    return 0;
}
'''


def extract(source, name):
    src = re.sub(r"//[^\n]*|/\*.*?\*/", "", source, flags=re.S)
    found = list(re.finditer(
        rf"(?:float|RenderPose)\s+{name}\s*\([^;{{]*\)\s*\{{", src))
    if len(found) != 1:
        raise ValueError(f"Se esperaba una definición de {name}, hay {len(found)}")
    start = found[0].start()
    brace = src.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (src[end] == "{") - (src[end] == "}")
        end += 1
    return src[start:end]


def harness():
    source = (ROOT / "src/Game/RenderSnapshots.cpp").read_text(encoding="utf-8")
    functions = "\n".join(extract(source, n) for n in
                          ("Blend", "BlendAngle", "RenderSnapshots_Interpolate"))
    header = (ROOT / "src/Game/RenderSnapshots.h").as_posix()
    return HARNESS.replace("HEADER_PATH", header).replace("FUNCTIONS", functions)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--architecture", choices=("Win32", "x64"), default="x64")
    parser.add_argument("--generator")
    parser.add_argument("--reference-ref")
    args = parser.parse_args()
    if args.reference_ref:
        parser.error("Sin referencia histórica para esta prueba")
    with tempfile.TemporaryDirectory(prefix="mu-render-wrap-") as temp:
        current = build_and_run(Path(temp) / "current", harness(), args)
        print(current, end="")


if __name__ == "__main__":
    main()
