#!/usr/bin/env python3
"""Facing/part-pose harness — ronda v20261115 (facing jitter + erupciones).

Compila el loop REAL de producción a nivel de valores:
  MovePath_IDA_0043EA20 + CreateAngle/FarAngle/TurnAngle2 (giro del héroe),
  CharacterMoveSpeed (paso, stock y x1.9 Ninja Run), MoveCharacterPosition,
  BMD__PlayAnimation (cadencia del cuerpo y de las piezas: cuerpo 0.30/0.28
  de la tabla de OpenPlayers; ala 0.25 = PlaySpeed de RenderLinkObject),
  Blend/BlendAngle/RenderSnapshots_Interpolate (con el RenderPose real).

El driver reproduce el orden de producción por tick (25 Hz) y por frame de
render (60 fps, alpha = resto/40): AdvancePath -> MoveCharacterPosition ->
PlayAnimation(cuerpo) -> PlayAnimation(piezas, en Equipment_Tick) -> captura
Begin/End -> Interpolate -> pose (con lean si Ninja) y, para las piezas, el
avance EXTRA de PlayAnimation que hace RenderLinkObject dentro de la pasada
de dibujo. Se registran los ángulos SIM y los ángulos/frames POSADOS por
frame, y se valida la ventana de slerp que BMD_Animation asume (prior->frame
desenvuelto debe abarcar <= 1 clave; fuera de rango = ventana inválida).

Datos reales: player.bmd acción 15 (run con arma): 7 frames, LockPositions=1
=> loopLength 6, speed 0.30. Acción 1 (idle): 6 frames, lock 0 => loop 6,
speed 0.28. Wing01.bmd acción 0: 7 frames, lock 0 => loop 7. Helper custom
(tipo Anivia): 12 frames, lock 0 (conteo distinto al de los helpers vanilla).

BMD__PlayAnimation se extrae verbatim; sólo se reescriben sus accesos al
modelo (AsModel(self)->campo) por los offsets documentados del propio
archivo (NumActions +38, Actions +48, CurrentAction +160, CurrentAnimation
+164, CurrentAnimationFrame +168, stride de acción 16 = layout x86), porque
el BMD del fixture compartido no tiene esos campos. La lógica no cambia.
"""
import argparse
import os
from pathlib import Path
import re
import tempfile

from PlayerStopPointer import harness as stop_harness
from SceneObjectPointer import build_and_run, function, read_source

ROOT = Path(__file__).resolve().parent.parent


def extract_any(source, name):
    src = re.sub(r"//[^\n]*|/\*.*?\*/", "", source, flags=re.S)
    found = list(re.finditer(rf"(?:[\w:\*&<>\[\]]+\s+)+{name}\s*\([^;{{]*\)\s*\{{", src))
    if len(found) != 1:
        raise ValueError(f"Se esperaba una definición de {name}, hay {len(found)}")
    start = found[0].start()
    brace = src.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (src[end] == "{") - (src[end] == "}")
        end += 1
    return src[start:end]


DRIVER = r'''
// ── Servicios que el prefijo no trae (dobles controlados) ──────────────────
using byte = unsigned char;
float RequestTerrainHeight(float, float) { return 0.0f; }
float _DAT_00552868 = 0.0001f;   // epsilon de CreateAngle
float _DAT_00552580 = 0.0f;
float _DAT_005524ec = 180.0f;
float _DAT_00552860 = 57.29578f; // rad->deg
float _DAT_00552848 = 90.0f;
float _DAT_00552864 = 270.0f;
float _DAT_0055284c = 0.0f;
float _DAT_00552934 = 0.04f;     // acumulador de fase de MoveCharacterPosition

SPEED_NINJA
SPEED_STOCK
POSITION_NINJA
POSITION_STOCK
ANGLE_FUNCS
PLAYANIM
INTERP_FUNCS

// ── Modelo BMD falso con los offsets reales (ver docstring) ────────────────
struct FakeModel {
    alignas(8) unsigned char bytes[256];
    unsigned char actions[160 * 16];
    explicit FakeModel(short numActions) {
        std::memset(bytes, 0, sizeof(bytes)); std::memset(actions, 0, sizeof(actions));
        *(short*)(bytes + 38) = numActions;
        *(void**)(bytes + 48) = actions;
    }
    void SetAction(int idx, short frames, float speed, unsigned char lock) {
        unsigned char* a = actions + idx * 16;
        *(float*)(a + 4) = speed; *(short*)(a + 8) = frames; a[10] = lock; // a[0]=0 => en bucle
    }
    void SetCurrent(unsigned char act) { bytes[160] = act; }
};

// Muestra de cuerpo: espejo de ObjectSample + ActionLoopLength (RenderSnapshots.cpp).
static RenderPose SampleBody(const CHARACTER* c, float loopLen) {
    RenderPose p;
    std::memcpy(p.position, c->Object.Position, 12);
    std::memcpy(p.angle, c->Object.Angle, 12);
    std::memcpy(p.headAngle, c->Object.HeadAngle, 12);
    p.animationFrame = c->Object.AnimationFrame; p.priorAnimationFrame = c->Object.PriorAnimationFrame;
    p.action = c->Object.CurrentAction; p.priorAction = c->Object.PriorAction;
    p.loopLength = loopLen; p.scale = 1.0f; p.alpha = 1.0f;
    return p;
}
// Muestra de pieza: espejo de PartSample (pose a cero, scale/alpha 1, loopLength 0).
static RenderPose SamplePart(const PART_t& part) {
    RenderPose p;
    p.animationFrame = part.AnimationFrame; p.priorAnimationFrame = part.PriorAnimationFrame;
    p.action = part.CurrentAction; p.priorAction = part.PriorAction;
    return p;
}

// Ventana que BMD_Animation (AnimateWithScratch) asume: slerp entre la clave
// (int)prior y la clave (int)frame del MISMO ciclo desenvuelto; el avance por
// tick es < 1 frame, así que el span desenvuelto válido es [0, 2). Un span
// mayor (o un índice fuera de [0, FrameCount)) = ventana inválida.
static float WindowSpan(float frame, float prior, float loopLen) {
    if (loopLen <= 0.0f) return 0.0f;
    const int L = (int)loopLen;
    int d = ((int)frame - (int)prior) % L; if (d < 0) d += L;
    return (float)d + (frame - (float)(int)frame);
}
static bool FrameValid(float frame, float prior, float frameCount) {
    return frame >= 0.0f && frame < frameCount + 0.001f && prior >= 0.0f && prior < frameCount;
}

struct PartSim {
    PART_t* part; FakeModel* model; float playSpeed; float frameCount; const char* name;
};

struct Scenario {
    const char* name;
    unsigned char bodyAction; float bodySpeed; float bodyLoop; float bodyFrames;
    std::vector<std::pair<int,int>> path; // waypoints de grilla (vacío = idle)
};

struct Tally {
    double maxTickYaw = 0, maxFrameYaw = 0, maxFramePitch = 0, maxFrameRoll = 0;
    int snaps = 0, bodyViol = 0, partViol = 0, frames = 0, ticks = 0;
};

static float WrapDelta(float a, float b) { // delta con signo b-a por el camino corto
    float d = b - a; if (d > 180.0f) d -= 360.0f; if (d < -180.0f) d += 360.0f; return d;
}

static Tally RunScenario(const Scenario& sc, bool ninja, bool trace) {
    Tally t;
    CHARACTER hero; std::memset(&hero, 0, sizeof(hero));
    ((BYTE*)&hero.Object)[0] = 1; // Live (byte 0 del OBJECT real)
    hero.Object.Type = 390; hero.Object.Kind = 1;
    hero.Run = 41; hero.SafeZone = 0;
    hero.Wing.Type = -1; hero.Helper.Type = -1;
    hero.Object.CurrentAction = sc.bodyAction; hero.Object.PriorAction = sc.bodyAction;

    FakeModel bodyModel(141);
    bodyModel.SetAction(sc.bodyAction, (short)sc.bodyFrames, sc.bodySpeed, sc.bodyLoop < sc.bodyFrames ? 1 : 0);
    // Ala Wing01 (7 frames) y helper custom (12 frames): piezas vivas siempre,
    // para vigilar su pairing también durante la carrera.
    hero.Wing.Type = 790; hero.Wing.CurrentAction = 0; hero.Wing.PlaySpeed = 0.25f;
    hero.Helper.Type = 817; hero.Helper.CurrentAction = 0; hero.Helper.PlaySpeed = 0.30f;
    FakeModel wingModel(1); wingModel.SetAction(0, 7, 0.25f, 0);
    FakeModel helperModel(1); helperModel.SetAction(0, 12, 0.30f, 0);
    PartSim parts[2] = { { &hero.Wing, &wingModel, 0.25f, 7.0f, "wing" },
                         { &hero.Helper, &helperModel, 0.30f, 12.0f, "helper" } };

    if (!sc.path.empty()) {
        hero.PathNum = (BYTE)sc.path.size();
        for (std::size_t i = 0; i < sc.path.size(); ++i) {
            hero.PathX[i] = (BYTE)sc.path[i].first; hero.PathY[i] = (BYTE)sc.path[i].second;
        }
        hero.PositionX = sc.path[0].first; hero.PositionY = sc.path[0].second;
        hero.Object.Position[0] = (sc.path[0].first + 0.5f) * 100.0f;
        hero.Object.Position[1] = (sc.path[0].second + 0.5f) * 100.0f;
        hero.Object.Position[2] = 0.0f;
        hero.Object.Angle[2] = CreateAngle(hero.Object.Position[0], hero.Object.Position[1],
            (sc.path[1].first + 0.5f) * 100.0f, (sc.path[1].second + 0.5f) * 100.0f);
        hero.Movement = 1;
    }
    const float factor = ninja ? NinjaRun::kSpeedFactor : 1.0f;
    const int totalTicks = sc.path.empty() ? 300 : 400;
    // Pares publicados (último paso completado), como los Track de producción.
    RenderPosePair bodyPair, wingPair, helperPair;
    bool published = false;
    auto sampleAll = [&](RenderPosePair& b, RenderPosePair& w, RenderPosePair& h, bool begin) {
        RenderPose bs = SampleBody(&hero, sc.bodyLoop), ws = SamplePart(hero.Wing), hs = SamplePart(hero.Helper);
        if (begin) { b.previous = bs; w.previous = ws; h.previous = hs; }
        else { b.current = bs; w.current = ws; h.current = hs; b.continuous = w.continuous = h.continuous = true; }
    };
    float prevYaw = hero.Object.Angle[2], prevPosedYaw = prevYaw, prevPosedPitch = 0, prevPosedRoll = 0;
    bool havePosed = false;
    const double frameMs = 1000.0 / 60.0;
    const int totalFrames = (int)(totalTicks * 40.0 / frameMs);
    int tickDone = 0;
    for (int f = 0; f <= totalFrames; ++f) {
        const double timeMs = f * frameMs;
        const int due = (int)(timeMs / 40.0);
        while (tickDone < due && tickDone < totalTicks) {
            ++tickDone; ++t.ticks;
            RenderPosePair b, w, h; sampleAll(b, w, h, true);
            if (!sc.path.empty() && hero.Movement) {
                const unsigned int ok = MovePath_IDA_0043EA20((char*)&hero, '\x01');
                if (ok == 0) { if (ninja) MoveCharacterPosition(&hero); else MoveCharacterPositionStock(&hero); }
                else hero.Movement = 0;
            }
            bodyModel.SetCurrent(hero.Object.CurrentAction);
            BMD__PlayAnimation(&bodyModel, &hero.Object.AnimationFrame, &hero.Object.PriorAnimationFrame,
                               &hero.Object.PriorAction, sc.bodySpeed * factor);
            for (auto& ps : parts) {
                ps.model->SetCurrent(ps.part->CurrentAction);
                BMD__PlayAnimation(ps.model, &ps.part->AnimationFrame, &ps.part->PriorAnimationFrame,
                                   &ps.part->PriorAction, ps.playSpeed);
            }
            sampleAll(b, w, h, false);
            bodyPair = b; wingPair = w; helperPair = h; published = true;
            const float dy = std::fabs(WrapDelta(prevYaw, hero.Object.Angle[2]));
            if (dy > t.maxTickYaw) t.maxTickYaw = dy;
            if (dy >= 45.0f) ++t.snaps;
            prevYaw = hero.Object.Angle[2];
            if (trace) std::printf("  tick %3d pos=(%.1f,%.1f) yaw=%.2f cur=%d sub=%d frame=%.3f prior=%.3f wing=(%.3f,%.3f) helper=(%.3f,%.3f)\n",
                tickDone, hero.Object.Position[0], hero.Object.Position[1], hero.Object.Angle[2],
                (int)hero.PathCurrent, (int)hero.PathSubstep,
                hero.Object.AnimationFrame, hero.Object.PriorAnimationFrame,
                hero.Wing.AnimationFrame, hero.Wing.PriorAnimationFrame,
                hero.Helper.AnimationFrame, hero.Helper.PriorAnimationFrame);
            if (!hero.Movement && !sc.path.empty() && tickDone > 20) { /* seguir unos ticks en idle */ }
        }
        if (!published) continue;
        const float remainder = (float)(timeMs - tickDone * 40.0);
        const float alpha = remainder <= 0.0f ? 0.0f : remainder >= 40.0f ? 1.0f : remainder / 40.0f;
        RenderPose bp = RenderSnapshots_Interpolate(bodyPair, alpha);
        // Pose final del cuerpo: lean Ninja sobre Angle[0] si es locomoción.
        float posed[3] = { bp.angle[0], bp.angle[1], bp.angle[2] };
        if (ninja && NinjaRun::IsLocomotionAction(bp.action)) posed[0] += NinjaRun::kLeanDegrees;
        ++t.frames;
        if (havePosed) {
            const float dy = std::fabs(WrapDelta(prevPosedYaw, posed[2]));
            const float dp = std::fabs(posed[0] - prevPosedPitch), dr = std::fabs(posed[1] - prevPosedRoll);
            if (dy > t.maxFrameYaw) t.maxFrameYaw = dy;
            if (dp > t.maxFramePitch) t.maxFramePitch = dp;
            if (dr > t.maxFrameRoll) t.maxFrameRoll = dr;
            if ((dy > 30.0f || dp > 10.0f || dr > 10.0f) && trace)
                std::printf("  frame %3d alpha=%.2f POSED d(yaw,pitch,roll)=(%.1f,%.1f,%.1f) angle=(%.2f,%.2f,%.2f) frame=%.3f prior=%.3f\n",
                    f, alpha, dy, dp, dr, posed[0], posed[1], posed[2], bp.animationFrame, bp.priorAnimationFrame);
        }
        prevPosedYaw = posed[2]; prevPosedPitch = posed[0]; prevPosedRoll = posed[1]; havePosed = true;
        // Ventana del cuerpo (lo que BMD_Animation va a slerpear en el draw).
        if (!FrameValid(bp.animationFrame, bp.priorAnimationFrame, sc.bodyFrames) ||
            WindowSpan(bp.animationFrame, bp.priorAnimationFrame, sc.bodyLoop) >= 2.0f) {
            ++t.bodyViol;
            if (trace) std::printf("  frame %3d BODY WINDOW frame=%.3f prior=%.3f span=%.2f (pair prev=%.3f/%.3f cur=%.3f/%.3f alpha=%.2f)\n",
                f, bp.animationFrame, bp.priorAnimationFrame,
                WindowSpan(bp.animationFrame, bp.priorAnimationFrame, sc.bodyLoop),
                bodyPair.previous.animationFrame, bodyPair.previous.priorAnimationFrame,
                bodyPair.current.animationFrame, bodyPair.current.priorAnimationFrame, alpha);
        }
        // Piezas: Interpolate + el avance de RenderLinkObject dentro de la pasada.
        const RenderPosePair* pairs[2] = { &wingPair, &helperPair };
        for (int pi = 0; pi < 2; ++pi) {
            RenderPose pp = RenderSnapshots_Interpolate(*pairs[pi], alpha);
            float fr = pp.animationFrame, pr = pp.priorAnimationFrame; unsigned char pa = pp.priorAction;
            parts[pi].model->SetCurrent(pp.action);
            BMD__PlayAnimation(parts[pi].model, &fr, &pr, &pa, parts[pi].playSpeed);
            const float loop = parts[pi].frameCount; // lock=0 en ambas piezas
            if (!FrameValid(fr, pr, parts[pi].frameCount) || WindowSpan(fr, pr, loop) >= 2.0f) {
                ++t.partViol;
                if (trace) std::printf("  frame %3d PART %s WINDOW frame=%.3f prior=%.3f span=%.2f (interp=%.3f/%.3f pair prev=%.3f/%.3f cur=%.3f/%.3f alpha=%.2f)\n",
                    f, parts[pi].name, fr, pr, WindowSpan(fr, pr, loop), pp.animationFrame, pp.priorAnimationFrame,
                    pairs[pi]->previous.animationFrame, pairs[pi]->previous.priorAnimationFrame,
                    pairs[pi]->current.animationFrame, pairs[pi]->current.priorAnimationFrame, alpha);
            }
        }
        if (!hero.Movement && !sc.path.empty() && tickDone > 40 && f > 60) break;
    }
    std::printf("[%s|%s] ticks=%d frames=%d maxTickYaw=%.2f maxFrameYaw=%.2f maxFramePitch=%.2f maxFrameRoll=%.2f snaps45=%d bodyViol=%d partViol=%d\n",
        sc.name, ninja ? "ninja" : "stock", t.ticks, t.frames, t.maxTickYaw, t.maxFrameYaw,
        t.maxFramePitch, t.maxFrameRoll, t.snaps, t.bodyViol, t.partViol);
    return t;
}

int main() {
    const Scenario scenarios[] = {
        { "run-east",      15, 0.30f, 6.0f, 7.0f, {{10,10},{11,10},{12,10},{13,10},{14,10},{15,10},{16,10},{17,10},{18,10},{19,10},{20,10},{21,10},{22,10},{23,10}} },
        { "run-north",     15, 0.30f, 6.0f, 7.0f, {{10,23},{10,22},{10,21},{10,20},{10,19},{10,18},{10,17},{10,16},{10,15},{10,14},{10,13},{10,12},{10,11},{10,10}} },
        { "run-diag-ne",   15, 0.30f, 6.0f, 7.0f, {{10,23},{11,22},{12,21},{13,20},{14,19},{15,18},{16,17},{17,16},{18,15},{19,14},{20,13},{21,12},{22,11},{23,10}} },
        { "run-stair-2a1", 15, 0.30f, 6.0f, 7.0f, {{5,15},{6,15},{7,14},{8,14},{9,13},{10,13},{11,12},{12,12},{13,11},{14,11},{15,10},{16,10},{17,9},{18,9}} },
        { "run-corner-en", 15, 0.30f, 6.0f, 7.0f, {{5,10},{6,10},{7,10},{8,10},{9,10},{10,10},{11,10},{12,10},{12,9},{12,8},{12,7},{12,6},{12,5},{12,4}} },
        { "run-seam-nw",   15, 0.30f, 6.0f, 7.0f, {{12,23},{12,22},{11,21},{11,20},{11,19},{10,18},{10,17},{10,16},{9,15},{9,14},{9,13},{8,12},{8,11},{8,10}} },
        { "idle-stand",    1, 0.28f, 6.0f, 6.0f, {} },
    };
    const char* want = std::getenv("RF_TRACE");
    for (const auto& sc : scenarios) {
        const bool tr = want && std::string(want) == sc.name;
        const Tally st = RunScenario(sc, false, tr);
        const Tally nj = RunScenario(sc, true, tr);
        // Ventanas (frame,prior) del cuerpo y de las partes: siempre válidas,
        // también en el wrap en reposo con helper de otro conteo de frames.
        Require(st.bodyViol == 0 && nj.bodyViol == 0);
        Require(st.partViol == 0 && nj.partViol == 0);
        // Sin pitch/roll espurios en la pose interpolada.
        Require(st.maxFramePitch <= 0.01f && nj.maxFramePitch <= 0.01f);
        Require(st.maxFrameRoll <= 0.01f && nj.maxFrameRoll <= 0.01f);
        // Ninguna reversa de 180 en ningún escenario a ninguna velocidad.
        Require(st.maxTickYaw < 180.0f && nj.maxTickYaw < 180.0f);
        const bool straight = std::string(sc.name) == "run-east" ||
                              std::string(sc.name) == "run-north" ||
                              std::string(sc.name) == "run-diag-ne";
        if (straight) {
            // El defecto probado: a 28.5/tick la mirada saltaba 180 grados
            // cada pocos pasos en línea recta.  Debe ser CERO a ambas
            // velocidades (y el stock ya lo era: regresión cerrada).
            Require(st.maxTickYaw <= 0.01f && st.snaps == 0);
            Require(nj.maxTickYaw <= 0.01f && nj.snaps == 0);
            Require(nj.maxFrameYaw <= 0.01f);
        }
        if (std::string(sc.name) == "run-corner-en") {
            // La esquina de 90 grados se toma con UN giro (snap) limpio.
            Require(nj.snaps <= 1 && nj.maxTickYaw <= 91.0f);
            Require(st.snaps <= 1 && st.maxTickYaw <= 49.0f);
        }
        if (std::string(sc.name) == "run-stair-2a1") {
            // Zigzag sintético 2:1: la geometría del camino alterna ~52
            // grados a velocidad ninja; el stock queda fijado en su valor.
            Require(nj.maxTickYaw <= 60.0f);
            Require(st.maxTickYaw <= 20.0f && st.snaps == 0);
        }
        if (std::string(sc.name) == "run-seam-nw") {
            Require(nj.maxTickYaw <= 50.0f);
            Require(st.maxTickYaw <= 15.0f && st.snaps == 0);
        }
    }
    std::puts("RunFacing: OK — sin jitter de mirada (stock intacto, ninja limpio)");
    return 0;
}
'''


def harness():
    prefix = stop_harness(None).split("void Call(CHARACTER* character)", 1)[0]
    types = read_source("src/Entity/EntityTypes.h")
    angle = re.search(r"\bvec3_t\s+Angle\s*;", types).group()
    head = re.search(r"\bvec3_t\s+HeadAngle\s*;", types).group()
    kind = re.search(r"\bBYTE\s+Kind\s*;", types).group()
    prefix = prefix.replace(
        "BYTE beforeAction[261 - 28];",
        "BYTE beforeAngle[0]; " + angle + " " + head +
        " BYTE beforeKind[132 - 52]; " + kind + " BYTE beforeAction[261 - 133];", 1)
    prefix += "\nstatic_assert(sizeof(void*) != 4 || (offsetof(OBJECT, Angle) == 28 && offsetof(OBJECT, HeadAngle) == 40 && offsetof(OBJECT, Kind) == 132));\n"
    ninja = read_source("src/Game/NinjaRun.h").replace("#pragma once", "")
    prefix += "\n" + ninja + "\nusing float10 = long double;\n"

    nav = read_source("src/Scene/Scene_CharSelect_Nav.cpp")
    path_src = read_source("src/Path/Path_Legacy.cpp")
    angle_funcs = "\n".join([
        function(nav, "CreateAngle"), function(nav, "TurnAngle2"),
        function(path_src, "FarAngle"),
        extract_any(path_src, "MovePath_IDA_0043EA20"),
    ])
    terrain = read_source("src/Terrain/Terrain.cpp")
    speed_ninja = function(terrain, "CharacterMoveSpeed")
    speed_stock = function(terrain, "CharacterMoveSpeed") \
        .replace("CharacterMoveSpeed(", "CharacterMoveSpeedStock(", 1) \
        .replace("NinjaRun::kSpeedFactor", "1.0f")
    motion = read_source("src/Entity/Character_Motion.cpp")
    pos_ninja = function(motion, "MoveCharacterPosition")
    pos_stock = function(motion, "MoveCharacterPosition") \
        .replace("CharacterMoveSpeed(", "CharacterMoveSpeedStock(") \
        .replace("MoveCharacterPosition(", "MoveCharacterPositionStock(", 1)
    play = function(read_source("src/Render/BMD_Anim.cpp"), "BMD__PlayAnimation")
    play = (play.replace("AsModel(self)->NumActions", "*(short*)((BYTE*)self + 38)")
                .replace("AsModel(self)->CurrentAction", "*((BYTE*)self + 160)")
                .replace("AsModel(self)->Actions", "*(void**)((BYTE*)self + 48)")
                .replace("AsModel(self)->CurrentAnimationFrame", "*(short*)((BYTE*)self + 168)")
                .replace("AsModel(self)->CurrentAnimation", "*(float*)((BYTE*)self + 164)")
                .replace("sizeof(ModelAnimationAction)", "16"))
    snaps = read_source("src/Game/RenderSnapshots.cpp")
    interp = "\n".join(extract_any(snaps, n) for n in
                       ("Blend", "BlendAngle", "RenderSnapshots_Interpolate"))
    header = (ROOT / "src/Game/RenderSnapshots.h").as_posix()
    driver = (DRIVER.replace("SPEED_NINJA", speed_ninja)
                    .replace("SPEED_STOCK", speed_stock)
                    .replace("POSITION_NINJA", pos_ninja)
                    .replace("POSITION_STOCK", pos_stock)
                    .replace("ANGLE_FUNCS", angle_funcs)
                    .replace("PLAYANIM", play)
                    .replace("INTERP_FUNCS", interp))
    return prefix + f'\n#include "{header}"\n' + driver


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--architecture", choices=("Win32", "x64"), default="x64")
    parser.add_argument("--generator")
    parser.add_argument("--reference-ref")
    args = parser.parse_args()
    if args.reference_ref:
        parser.error("Sin referencia histórica para esta prueba")
    with tempfile.TemporaryDirectory(prefix="mu-run-facing-") as temp:
        current = build_and_run(Path(temp) / "current", harness(), args)
        print(current, end="")


if __name__ == "__main__":
    main()
