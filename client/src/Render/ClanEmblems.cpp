// ClanEmblems.cpp — MEAMU: clan emblems worn on the body.
//
// G (2026-11-13): every character belonging to a clan — human players AND
// phantoms — wears their clan's emblem in three places: on the ARM
// (upper arm), on the FRONT (chest) and on the BACK (upper back).
// Clans are the guilds "Senju" and "Uchiha"; guild-less characters get
// no emblem.
//
// Identity: keyed off the guild the client already receives for every
// viewport character — GuildMarkIndex (set by the 0x5A/0x5B/0x5C guild
// packets) resolved to a name with Guild_GetMarkName. Phantoms receive a
// GuildMarkIndex as soon as the GameServer stamps their fabricated objects
// with the clan guild (see the GS phantom-clan change); until then they
// simply show no emblem, like any guild-less character.
//
// Rendering: bone-attached textured quads drawn inside RenderCharacter
// (Entity_UpdateRender.cpp, right after the stock guild-mark-on-shield
// block, whose transform recipe this mirrors). The quads ride the
// character's ANIMATED bone matrices, so they follow the body over ANY
// armor set — no armor texture is touched. Emblem art is generated
// procedurally at first use (Uchiha fan / Senju vajra in a ring) into two
// private GL textures; nothing is loaded from disk.
//
// Anchors (player.bmd skeleton, verified from the file's bone table):
//   chest/back — bone 18 "Bip01 Spine1"; arm — bone 26 "Bip01 R UpperArm".
// Indices are re-resolved BY NAME from the rendered model when possible
// and fall back to those constants. Model space: front is -Y, back +Y.

#include "stdafx.h"
#include "Render/Gfx.h"
#include "Model/ModelTypes.h"
#include "Entity/EntityTypes.h"
#include "globals.h"
#include "functions.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>

extern "C" const char* Guild_GetMarkName(int row);

namespace {

// ── Emblem art (procedural, supersampled SDF -> 128x128 RGBA) ──────────────

constexpr int kArtHi = 512;   // rasterization resolution
constexpr int kArtLo = 128;   // final texture size

float DistSegment(float px, float py, float ax, float ay, float bx, float by)
{
    const float dx = bx - ax, dy = by - ay;
    const float len2 = dx * dx + dy * dy;
    float t = len2 > 0.0f ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0f;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    const float ex = ax + dx * t - px, ey = ay + dy * t - py;
    return std::sqrt(ex * ex + ey * ey);
}

// Uchiha fan (uchiwa): round paddle, crimson top half, white bottom half,
// dark outline, short handle. Returns coverage 0..1 and writes RGB.
float UchihaArt(float x, float y, float& r, float& g, float& b)
{
    const float cx = 256.0f, cy = 208.0f, rad = 168.0f;
    const float dCircle = std::fabs(std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy)) - 0.0f);
    float dShape = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy)) - rad;
    // Handle: tapered box from y=330 to y=472.
    if (y > 320.0f) {
        const float t = (y - 320.0f) / 152.0f;
        const float halfW = 30.0f - 8.0f * t;
        const float dHandle = std::fmax(std::fabs(x - cx) - halfW,
                                        std::fmax(320.0f - y, y - 472.0f));
        dShape = std::fmin(dShape, dHandle);
    }
    (void)dCircle;
    const float outline = 8.0f;
    if (dShape < -outline) {
        if (y < cy) {  // crimson top, lighter toward the top edge
            const float shade = 0.92f + 0.16f * (1.0f - (y / 512.0f));
            r = 0.83f * shade; g = 0.10f * shade; b = 0.20f * shade;
        } else {       // warm white bottom
            r = 0.96f; g = 0.94f; b = 0.89f;
        }
    } else if (dShape < 0.0f) {
        r = 0.10f; g = 0.11f; b = 0.13f;  // outline
    } else {
        return 0.0f;
    }
    const float aa = 0.5f - dShape / 3.0f;
    return aa < 0.0f ? 0.0f : (aa > 1.0f ? 1.0f : aa);
}

// Senju emblem: deep-green ring with the vajra (double trident) glyph.
float SenjuArt(float x, float y, float& r, float& g, float& b)
{
    const float cx = 256.0f, cy = 256.0f;
    const float dc = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
    float d = std::fabs(dc - 172.0f) - 16.0f;              // ring
    const float stroke = 15.0f;
    d = std::fmin(d, DistSegment(x, y, cx, 150.0f, cx, 362.0f) - stroke);   // vertical bar
    d = std::fmin(d, DistSegment(x, y, cx, 168.0f, cx, 84.0f) - stroke);    // top spike
    d = std::fmin(d, DistSegment(x, y, cx, 344.0f, cx, 428.0f) - stroke);   // bottom spike
    d = std::fmin(d, DistSegment(x, y, cx, 196.0f, cx - 58.0f, 108.0f) - stroke);  // top prongs
    d = std::fmin(d, DistSegment(x, y, cx, 196.0f, cx + 58.0f, 108.0f) - stroke);
    d = std::fmin(d, DistSegment(x, y, cx, 316.0f, cx - 58.0f, 404.0f) - stroke);  // bottom prongs
    d = std::fmin(d, DistSegment(x, y, cx, 316.0f, cx + 58.0f, 404.0f) - stroke);
    const float diamond = std::fabs(x - cx) + std::fabs(y - cy) - 62.0f;    // center diamond
    d = std::fmin(d, diamond);
    if (d >= 0.0f) {
        const float aa = 0.5f - d / 3.0f;
        if (aa <= 0.0f) return 0.0f;
        r = 0.14f; g = 0.55f; b = 0.28f;
        return aa > 1.0f ? 1.0f : aa;
    }
    r = 0.14f; g = 0.55f; b = 0.28f;   // deep leaf green
    const float aa = 0.5f - d / 3.0f;
    return aa > 1.0f ? 1.0f : aa;
}

Gfx::Texture BuildEmblemTexture(bool uchiha)
{
    static std::uint8_t pixels[kArtLo * kArtLo * 4];
    constexpr int kStep = kArtHi / kArtLo;
    for (int ly = 0; ly < kArtLo; ++ly) {
        for (int lx = 0; lx < kArtLo; ++lx) {
            float accR = 0, accG = 0, accB = 0, accA = 0;
            for (int sy = 0; sy < kStep; ++sy) {
                for (int sx = 0; sx < kStep; ++sx) {
                    const float x = (float)(lx * kStep + sx) + 0.5f;
                    const float y = (float)(ly * kStep + sy) + 0.5f;
                    float r = 0, g = 0, b = 0;
                    const float a = uchiha ? UchihaArt(x, y, r, g, b)
                                           : SenjuArt(x, y, r, g, b);
                    accR += r * a; accG += g * a; accB += b * a; accA += a;
                }
            }
            const float n = (float)(kStep * kStep);
            std::uint8_t* out = &pixels[(ly * kArtLo + lx) * 4];
            if (accA > 0.001f) {
                out[0] = (std::uint8_t)(accR / accA * 255.0f);
                out[1] = (std::uint8_t)(accG / accA * 255.0f);
                out[2] = (std::uint8_t)(accB / accA * 255.0f);
            } else {
                out[0] = out[1] = out[2] = 0;
            }
            out[3] = (std::uint8_t)(accA / n * 255.0f);
        }
    }
    const Gfx::Texture texture = Gfx::CreateTexture();
    Gfx::BindTexture(texture);
    Gfx::TexImage2D(4, kArtLo, kArtLo, Gfx::Format::RGBA, pixels);
    // Level 0 only + mipmaps off by default: without explicit filters the
    // texture is incomplete (default MIN_FILTER expects a mip chain) and
    // samples opaque black. Pin non-mip filters + clamped wrap.
    Gfx::TexParameter(Gfx::TexParam::MinFilter, Gfx::TexValue::Linear);
    Gfx::TexParameter(Gfx::TexParam::MagFilter, Gfx::TexValue::Linear);
    Gfx::TexParameter(Gfx::TexParam::WrapS, Gfx::TexValue::ClampToEdge);
    Gfx::TexParameter(Gfx::TexParam::WrapT, Gfx::TexValue::ClampToEdge);
    return texture;
}

Gfx::Texture EmblemTexture(bool uchiha)
{
    static Gfx::Texture s_Uchiha = 0;
    static Gfx::Texture s_Senju = 0;
    Gfx::Texture& slot = uchiha ? s_Uchiha : s_Senju;
    if (!slot) slot = BuildEmblemTexture(uchiha);
    return slot;
}

// ── Bone anchors ────────────────────────────────────────────────────────────

struct BoneAnchors {
    int chest;   // Bip01 Spine1
    int armR;    // Bip01 R UpperArm
};

BoneAnchors AnchorsFor(const BMD* model)
{
    static std::map<const BMD*, BoneAnchors> s_Cache;
    const auto it = s_Cache.find(model);
    if (it != s_Cache.end()) return it->second;
    BoneAnchors anchors{ 18, 26 };  // player.bmd constants (fallback)
    if (model && model->Bones && model->NumBones > 0) {
        const auto* bones = reinterpret_cast<const ModelAnimationBone*>(model->Bones);
        for (int i = 0; i < model->NumBones; ++i) {
            if (std::strstr(bones[i].Name, "Spine1")) anchors.chest = i;
            else if (std::strstr(bones[i].Name, "R UpperArm")) anchors.armR = i;
        }
    }
    s_Cache[model] = anchors;
    return anchors;
}

// ── Quad drawing ────────────────────────────────────────────────────────────

struct Vec3 { float x, y, z; };

Vec3 Col(const float* m, int c) { return { m[c], m[4 + c], m[8 + c] }; }
Vec3 Add(Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
Vec3 Mul(Vec3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }

// Builds the billboard matrix GL_DrawBillboard expects: its local corners
// are (±w,±w,±h) with the plane spanned by local (1,1,0) and (0,0,1), so
// for a quad with right/up/normal axes (r,u,n) centered at c the matrix
// columns are ((r+n)/√2, (r−n)/√2, u) with translation c.
void DrawEmblemAt(const float* boneMatrix, Vec3 right, Vec3 up, Vec3 normal,
                  Vec3 center, Gfx::Texture texture)
{
    (void)boneMatrix;
    const float k = 0.70710678f;
    const Vec3 c1 = Mul(Add(right, normal), k);
    const Vec3 c2 = Mul(Add(right, Mul(normal, -1.0f)), k);
    float mat[12];
    mat[0] = c1.x; mat[1] = c2.x; mat[2] = up.x; mat[3] = center.x;
    mat[4] = c1.y; mat[5] = c2.y; mat[6] = up.y; mat[7] = center.y;
    mat[8] = c1.z; mat[9] = c2.z; mat[10] = up.z; mat[11] = center.z;

    EnableAlphaTest(true);
    Gfx::Color3f(1.0f, 1.0f, 1.0f);
    GL_BindTextureSlot(-(int)texture);
    GL_DrawBillboard(6.4f, 9.0f, mat);   // ≈18 x 18 world units
}

} // namespace

// Called from RenderCharacter (Entity_UpdateRender.cpp) after the stock
// guild-mark block: the entity's bone matrices are final for this frame.
void ClanEmblems_Render(intptr_t entity)
{
    OBJECT* object = AsObject(entity);
    if (!object || object->Type != 390 || !object->BoneMatrices) return;  // player model only
    if (object->Alpha == 0.0f) return;

    const short guildRow = AsCharacter(entity)->GuildMarkIndex;
    if (guildRow < 0) return;
    const char* guildName = Guild_GetMarkName(guildRow);
    bool uchiha;
    if (guildName && std::strcmp(guildName, "Uchiha") == 0) uchiha = true;
    else if (guildName && std::strcmp(guildName, "Senju") == 0) uchiha = false;
    else return;  // any other guild (or none): no clan emblem

    const BMD* model = (const BMD*)((const BYTE*)(uintptr_t)DAT_05828d58 +
                                    (uintptr_t)object->Type * BMD_STRIDE);
    const BoneAnchors anchors = AnchorsFor(model);
    const int boneCount = model ? model->NumBones : 0;
    if (anchors.chest >= boneCount || anchors.armR >= boneCount) return;

    const float* bones = (const float*)object->BoneMatrices;
    const Gfx::Texture texture = EmblemTexture(uchiha);
    if (!texture) return;

    Gfx::PushMatrix();
    Gfx::Translate(object->Position[0], object->Position[1], object->Position[2]);

    // Torso frame from the chest bone (basis columns of its matrix).
    const float* chest = bones + anchors.chest * 12;
    const Vec3 bx = Col(chest, 0), by = Col(chest, 1), bz = Col(chest, 2);
    const Vec3 chestPos = { chest[3], chest[7], chest[11] };
    const Vec3 front = Mul(by, -1.0f);   // model space: front is -Y
    const Vec3 back = by;

    // FRONT (chest): ~14 units off the spine axis.
    DrawEmblemAt(chest, bx, bz, front,
                 Add(chestPos, Mul(front, 14.0f)), texture);
    // BACK (upper back): mirrored right so the emblem reads correctly
    // from behind.
    DrawEmblemAt(chest, Mul(bx, -1.0f), bz, back,
                 Add(chestPos, Mul(back, 14.0f)), texture);

    // ARM (right upper arm): centered on the arm bone, pushed outward
    // along the torso's -X (the right side), facing outward.
    const float* arm = bones + anchors.armR * 12;
    const Vec3 armPos = { arm[3], arm[7], arm[11] };
    const Vec3 outward = Mul(bx, -1.0f);
    DrawEmblemAt(arm, by, bz, outward,
                 Add(armPos, Mul(outward, 11.0f)), texture);

    Gfx::PopMatrix();
}
