#include "stdafx.h"
#include "Render/Camera3D.h"
#include "globals.h"
#include "functions.h"

CCamera3D gCamera3D;

namespace {
    const float kDefaultFov    = 50.0f;
    const float kDefaultPitch  = 48.5f;
    const float kDefaultHeight = 150.0f;
    const float kDefaultYaw    = -45.0f;   // Game_CharSelectTick
}

void CCamera3D::Toggle()
{
    if (SceneFlag != 5) return;
    m_Enabled = !m_Enabled;
    m_Dragging = false;
    UI_AddNotice((char*)(m_Enabled ? "Camara 3D activada" : "Camara 3D desactivada"), 1);
}

void CCamera3D::Restore()
{
    if (!m_Enabled || SceneFlag != 5) return;
    m_Fov = kDefaultFov;
    m_Pitch = kDefaultPitch;
    m_Height = kDefaultHeight;
    m_ZoomTarget = 1.0f;
    m_ZoomCurrent = 1.0f;
    CameraAngle[2] = kDefaultYaw;
    UI_AddNotice((char*)"Camara 3D restaurada", 1);
}

void CCamera3D::BeginDrag(int x, int y)
{
    if (!m_Enabled || SceneFlag != 5) return;
    m_Dragging = true;
    m_CursorX = x;
    m_CursorY = y;
}

// Pasos y topes del DLL: 6 grados de giro por evento, 2.42 de inclinación y
// 44 de altura, inclinación entre 22.5 y 90.
void CCamera3D::Drag(int x, int y)
{
    if (!m_Enabled || !m_Dragging || SceneFlag != 5) return;
    if (x > m_CursorX) CameraAngle[2] += 6.0f;
    if (x < m_CursorX) CameraAngle[2] -= 6.0f;
    if (CameraAngle[2] > 315.0f || CameraAngle[2] < -405.0f) CameraAngle[2] = kDefaultYaw;
    if (y > m_CursorY && m_Pitch > 22.5f) { m_Pitch -= 2.42f; m_Height -= 44.0f; }
    if (y < m_CursorY && m_Pitch < 90.0f) { m_Pitch += 2.42f; m_Height += 44.0f; }
    m_CursorX = x;
    m_CursorY = y;
}

// Rueda hacia atrás aleja (más FOV), hacia adelante acerca; entre el 50 % y
// el 200 % del valor inicial, de a 2 grados.
bool CCamera3D::Wheel(int delta)
{
    if (!m_Enabled || m_Dragging || SceneFlag != 5 || delta == 0) return false;
    if (delta < 0 && m_Fov <= kDefaultFov * 2.0f) m_Fov += 2.0f;
    if (delta > 0 && m_Fov >= kDefaultFov * 0.5f) m_Fov -= 2.0f;
    return true;
}

// ── MEAMU (round v20261115-1341): zoom de distancia de la cámara principal ──
// Rango del factor: 0.40..2.00 sobre la distancia stock (offset horizontal
// 1000, cámara a 850 sobre el héroe; distancia 3D ≈ 1312).
//   - 0.40×: offset 400 / +340 de alto / ≈525 al héroe. El plano cercano es
//     20 (CameraViewNear): ni el héroe ni alas/armas llegan a recortarse.
//   - 2.00×: offset 2000 / +1700 / ≈2624 al héroe. MoveMainCamera estira
//     CameraViewFar con el mismo factor (6000), así el frustum de culling
//     (90 % del alcance = 5400 desde la cámara, ≈2776 más allá del héroe)
//     cubre el suelo que se ve por el borde superior (≈1908 con pitch 48.5
//     y FOV 50), y la niebla se estira con el alcance como ya hace
//     GL_BeginViewport. Espejo del tope 200 % que GetViewFar aplica al FOV.
// Paso multiplicativo por muesca (1.18): de 1.0 a 2.0 en ≈4 muescas, a 0.40
// en ≈5; se siente uniforme en todo el rango. Un paso por evento, igual
// que Wheel() con el FOV.
// La rueda sólo llega acá si el cursor está sobre el mundo (ver el caso
// Wheel de Input_WndProc): sobre una ventana/lista conserva su scroll.
namespace {
    const float kZoomMin  = 0.40f;
    const float kZoomMax  = 2.00f;
    const float kZoomStep = 1.18f;
}

bool CCamera3D::DistanceWheel(int delta)
{
    if (m_Dragging || SceneFlag != 5 || delta == 0) return false;
    if (delta > 0) m_ZoomTarget /= kZoomStep;   // rueda hacia adelante: acerca
    if (delta < 0) m_ZoomTarget *= kZoomStep;   // rueda hacia atrás: aleja
    if (m_ZoomTarget < kZoomMin) m_ZoomTarget = kZoomMin;
    if (m_ZoomTarget > kZoomMax) m_ZoomTarget = kZoomMax;
    return true;
}

// Interpola el factor una vez por paso de simulación (MoveMainCamera corre
// a 25 Hz desde PrepareCamera): 25 % del resto por paso asienta un salto
// completo en ≈0.5 s, sin escalones; la interpolación de render suaviza
// además la posición entre pasos.
float CCamera3D::UpdateDistanceZoom()
{
    const float diff = m_ZoomTarget - m_ZoomCurrent;
    if (diff > -0.001f && diff < 0.001f) m_ZoomCurrent = m_ZoomTarget;
    else m_ZoomCurrent += diff * 0.25f;
    return m_ZoomCurrent;
}

float CCamera3D::GetViewFar() const
{
    const float lift = m_Height - kDefaultHeight;
    // DESVIACION: al alejar con la rueda (más FOV) también crece el alcance.
    const float zoom = m_Fov > kDefaultFov ? m_Fov / kDefaultFov : 1.0f;
    return (3000.0f + (lift < 0.0f ? -lift : lift) * 3.0f) * zoom;
}
