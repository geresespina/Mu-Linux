#include "stdafx.h"
#include "Render/Gfx.h"
#include "Platform/Text.h"
#include "UI/CommandMenu.h"
#include "UI/ChatLogWindow.h"
#include "UI/MiniMap.h"
#include "UI/MoveList.h"
#include "UI/UIState.h"
#include "Core/Font.h"
#include "Core/Window.h"
#include "Local/ClientText.h"

// El mismo camino que al teclear el comando en el chat (Chat_InputTick.cpp):
// resuelve el objetivo y arma el paquete nativo (0x36/0x40/0x50).
extern "C" void Chat_SendChatLine(const char* text);

CCommandMenu gCommandMenu;
namespace {
// Panel centrado de 120 (ancla Ui::Menu, como el menú de opciones), apoyado
// justo sobre la barra inferior del HUD (la interfaz arranca en y=429).
struct CommandMenuLayout {
    static int X() { return gWindow.PanelOrigin(Ui::Menu); }
    static constexpr int Width = 120;
    static constexpr int Pad = 6;
    static constexpr int TitleOffset = 8, TitleHeight = 14;
    static constexpr int ButtonHeight = 22, ButtonGap = 5;
    static constexpr int ButtonsOffset = TitleOffset + TitleHeight + 6;
    static constexpr int Height = ButtonsOffset + 3 * ButtonHeight + 2 * ButtonGap + Pad;
    static constexpr int Bottom = 424;
    static constexpr int Y = Bottom - Height;
    static constexpr int ButtonY(int i) { return Y + ButtonsOffset + i * (ButtonHeight + ButtonGap); }
};
using Layout = CommandMenuLayout;
bool Inside(int x, int y, int w, int h)
{
    return Ui::Rect{x, y, w, h}.Contains((int)MouseX, (int)MouseY);
}
void TextCentered(int x, int y, const char* text, int width)
{
    RenderText(x, y, const_cast<char*>(text),
        (int)(width * gWindow.GetWidth() / 640), 1, nullptr);
}
void Rect(int x, int y, int w, int h, float r, float g, float b, float a)
{
    EnableAlphaTest(true);
    Gfx::Color4f(r, g, b, a);
    GL_DrawRect((float)x, (float)y, (float)w, (float)h);
    GL_ResetState();
    Gfx::Color4f(1, 1, 1, 1);
}
}

bool CCommandMenu::Blocked() const
{
    return !UIState::CanOpenInformationalPanel();
}

void CCommandMenu::Toggle()
{
    if (Blocked()) { m_Open = false; return; }
    // Comparte pantalla con M, Tab y L: se cierran mutuamente, como ellos.
    if (!m_Open) { gMoveList.Close(); gMiniMap.Close(); gChatLogWindow.Close(); }
    m_Open = !m_Open;
    PlayBuffer(25, 0, 0);
}

void CCommandMenu::Fire(int button)
{
    static const char* const commands[3] = { "/trade", "/party", "/guild" };
    Chat_SendChatLine(commands[button]);
    Close();
}

void CCommandMenu::UpdateMouse()
{
    if (Blocked()) { m_Open = false; return; }
    if (!m_Open) return;
    const int x = Layout::X();
    if (!Inside(x, Layout::Y, Layout::Width, Layout::Height)) {
        // DESVIACION: clic fuera = cerrar, sin consumir el clic (sigue al juego).
        if (MouseLButtonPush) Close();
        return;
    }
    UIState::CaptureMouseForUI();
    const bool click = MouseLButtonPush != 0;
    MouseLButton = 0;
    MouseLButtonPush = 0;
    MouseLButtonPop = 0;
    if (!click) return;
    for (int i = 0; i < 3; ++i) {
        if (Inside(x + Layout::Pad, Layout::ButtonY(i),
                   Layout::Width - 2 * Layout::Pad, Layout::ButtonHeight)) {
            Fire(i);
            return;
        }
    }
}

void CCommandMenu::Render()
{
    if (!m_Open || Blocked()) return;
    const int x = Layout::X();
    const DWORD color = m_dwTextColor, back = m_dwBackColor;
    HGDIOBJ font = gFont.SelectFont(gFont.GetTextDC(), gFont.GetFont(FONT_NORMAL));
    m_dwBackColor = 0;
    Rect(x, Layout::Y, Layout::Width, Layout::Height, 0, 0, 0, .8f);
    m_dwTextColor = 0xFF00FFFF;
    TextCentered(x, Layout::Y + Layout::TitleOffset,
                 gClientText.Get(ClientTextId::CmdMenuTitle), Layout::Width);
    static const ClientTextId labels[3] = {
        ClientTextId::CmdTrade, ClientTextId::CmdParty, ClientTextId::CmdGuild };
    for (int i = 0; i < 3; ++i) {
        const int by = Layout::ButtonY(i);
        const bool hover = Inside(x + Layout::Pad, by,
                                  Layout::Width - 2 * Layout::Pad, Layout::ButtonHeight);
        if (hover)
            Rect(x + Layout::Pad, by, Layout::Width - 2 * Layout::Pad, Layout::ButtonHeight,
                 .8f, .8f, .1f, .6f);
        else
            Rect(x + Layout::Pad, by, Layout::Width - 2 * Layout::Pad, Layout::ButtonHeight,
                 .1f, .1f, .25f, .6f);
        m_dwTextColor = 0xFFFFFFFF;
        TextCentered(x, by + 3, gClientText.Get(labels[i]), Layout::Width);
    }
    gFont.SelectFont(gFont.GetTextDC(), font);
    m_dwTextColor = color;
    m_dwBackColor = back;
    Gfx::Color4f(1, 1, 1, 1);
}
