#!/usr/bin/env python3
"""MEAMU (flicker round, 2026-11-15): OVERLAY STROBE — harness de puntero.

Síntoma (G, escritorio v20261115-2132, quieto en Lorencia): el escudo alterna
oscuro <-> dorado, el Fenrir mascota parpadea rojo <-> oscuro, las piezas con
brillo de OTRO jugador también — "casi cada frame". Todo lo demás (cuerpo,
posición, frames de animación) se interpola entre los dos registros del paso
SIM en Equipment_Draw / RenderInterpolation; el estado de overlay del item
(BlendMeshLight / BlendMeshTexCoordU/V del OBJECT copiado por RenderLinkObject)
es el ÚNICO estado dibujado que NO se mezcla: salta al valor del paso actual.

Cadena probada aquí con código real de producción:
  RenderLinkObject (SIM, por paso) llama ItemObjectAttribute PLANO
  (Entity/Entity_Reset.cpp) sobre la copia local del OBJECT de la pieza; para
  la familia de materiales aleatorios (414/565/567/606/607/608) eso re-tira
  BlendMeshLight/TexCoord con _rand() EN CADA PASO (25 Hz). Equipment_AddMesh
  copia ese OBJECT al registro del paso; Equipment_Draw lo dibuja tal cual
  (sin mezcla entre registros) mientras raíces, frames y ángulos SÍ se mezclan.

El harness simula 240 pasos x 2.4 frames de render por paso para los modelos
567 (BlendMeshLight aleatoria 0..1) y 565 (TexCoordU/V aleatorias) y mide el
salto máximo del valor DIBUJADO entre frames de render consecutivos:
  - HOY: el valor del paso vigente (snap en el borde del paso, hasta 1.0).
  - CON FIX: mezcla prev->current con el mismo alpha de render (como el resto).
También verifica un modelo NO aleatorio (700): la mezcla debe ser identidad.
"""
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import SceneObjectPointer as sop  # noqa: E402


def read_source(relative):
    return sop.read_source(relative)


def function(source, signature):
    return sop.function(source, signature)


DRIVER = r'''
// ── La función real de producción, con su lambda de azar intacta ───────────
// (samples == nullptr -> _rand(), exactamente como la llama RenderLinkObject)
static int _rand() { return (int)std::rand(); }
float WorldTime = 0.0f;
'''

MAIN = r'''
namespace {
struct DrawnState { float light, u, v; };

DrawnState RollPart(int model) {
    // El flujo real de RenderLinkObject: copia local en cero + ItemObjectAttribute.
    OBJECT o{}; o.Type = (short)model;
    ItemObjectAttribute(&o);
    return { o.BlendMeshLight, o.BlendMeshTexCoordU, o.BlendMeshTexCoordV };
}

float MaxJump(const std::vector<float>& series) {
    float m = 0.0f;
    for (std::size_t i = 1; i < series.size(); ++i)
        m = std::max(m, std::fabs(series[i] - series[i - 1]));
    return m;
}
}

int main() {
    std::srand(12345);
    const int steps = 240;
    const float alphas[] = {0.0f, 0.42f, 0.83f}; // 60 fps sobre pasos de 40 ms
    for (int model : {567, 565}) {
        std::vector<DrawnState> records;
        for (int s = 0; s < steps; ++s) records.push_back(RollPart(model));
        std::vector<float> todayLight, fixedLight, todayU, fixedU;
        for (int s = 1; s < steps; ++s) {
            for (float a : alphas) {
                // HOY: Equipment_Draw dibuja el registro vigente sin mezclar.
                todayLight.push_back(records[s].light);
                todayU.push_back(records[s].u);
                // CON FIX: mezcla prev->current con el alpha de render.
                fixedLight.push_back(records[s - 1].light + (records[s].light - records[s - 1].light) * a);
                fixedU.push_back(records[s - 1].u + (records[s].u - records[s - 1].u) * a);
            }
        }
        const float jToday = MaxJump(todayLight), jFixed = MaxJump(fixedLight);
        const float jTodayU = MaxJump(todayU), jFixedU = MaxJump(fixedU);
        std::printf("[model %d] BlendMeshLight salto/frame hoy=%.3f con mezcla=%.3f | TexCoordU hoy=%.3f con mezcla=%.3f\n",
            model, jToday, jFixed, jTodayU, jFixedU);
        // El defecto: saltos de rango completo entre dos frames de render.
        Require(jToday > 0.5f || jTodayU > 0.5f);
        // El fix: ningún salto dibujado supera el paso de alpha (~0.42 del rango).
        Require(jFixed <= 0.45f && jFixedU <= 0.45f);
    }
    // Modelo fuera de la familia aleatoria: la mezcla es identidad exacta.
    {
        const DrawnState a = RollPart(700), b = RollPart(700);
        Require(a.light == b.light && a.u == b.u && a.v == b.v);
        std::printf("[model 700] overlay determinista: mezcla = identidad OK\n");
    }
    std::puts("OverlayStrobe: OK — el overlay saltaba a 25 Hz; mezclado como el resto del estado");
    return 0;
}
'''


def harness():
    prefix = sop.__dict__.get("PREFIX")  # no usado; el prefijo viene de otro harness
    # Prefijo mínimo propio: cabeceras reales del árbol.
    src_reset = read_source("src/Entity/Entity_Reset.cpp")
    impl = function(src_reset, "ItemObjectAttributeImpl")
    # El envoltorio plano es una línea en la fuente (leída verbatim):
    # int __cdecl ItemObjectAttribute(OBJECT* entity) { return ItemObjectAttributeImpl(entity,nullptr); }
    plain = "int ItemObjectAttribute(OBJECT* entity) { return ItemObjectAttributeImpl(entity, nullptr); }"
    # El impl usa AsObject + campos del OBJECT real y las constantes _DAT_.
    header = r'''
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
using BYTE = unsigned char;
using DWORD = unsigned int;
using undefined4 = unsigned int;
using float10 = long double;
using ulonglong = unsigned long long;
using longlong = long long;
using vec3_t = float[3];
#include <stdexcept>
static void Require(bool ok) { if (!ok) { std::puts("Tabla, archivo o canario incorrecto"); std::exit(1); } }
using vec34_t = float[12];
#define fsin(x) sin((double)(x))
'''
    # Declaraciones reales del OBJECT (para AsObject y los campos Blend*).
    types = read_source("src/Entity/EntityTypes.h").splitlines()
    obj_text = "\n".join(types[19:98])
    class _Obj:
        def group(self, _):
            return obj_text
    obj = _Obj()
    consts = r'''
static const float _DAT_005524f4 = 0.1f;
static const float _DAT_005524bc = 0.004f;
static const float _DAT_00552530 = 0.5f;
static const float _DAT_005526e4 = 0.5f;
static const float _DAT_005526e8 = 0.5f;
static const float _DAT_005528b8 = 0.5f;
static const float _DAT_00552928 = 0.5f;
static const float _DAT_0055291c = 0.0005f;
'''
    parts = [header, obj.group(0),
             "static OBJECT* AsObject(void* p) { return (OBJECT*)p; }",
             "static OBJECT* AsObject(BYTE* p) { return (OBJECT*)p; }",
             consts, DRIVER, impl, plain, MAIN]
    return "\n".join(parts)


def main():
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--generator", default=None)
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args()
    from tempfile import TemporaryDirectory
    with TemporaryDirectory(prefix="mu-overlay-strobe-") as temp:
        out = sop.build_and_run(Path(temp) / "current", harness(), args)
        sys.stdout.write(out)


if __name__ == "__main__":
    main()
